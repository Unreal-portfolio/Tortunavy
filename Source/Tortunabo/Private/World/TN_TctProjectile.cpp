#include "World/TN_TctProjectile.h"
#include "Core/TN_Log.h"
#include "Game/TN_TctGameState.h"
#include "Game/TN_TctItemComponent.h"
#include "Game/TN_TctItemRules.h"
#include "Game/TN_TctItems.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "World/TN_TctAlgaPuddle.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_BeachStun.h"

namespace TNTctProjectileDetail
{
	/** Física de cada proyectil. */
	struct FFlight
	{
		float Radius = 20.f;
		float Gravity = 1.f;
		bool bBounce = false;
		float Bounciness = 0.f;
		float Friction = 0.2f;
		bool bFollowVelocity = false;
		float Life = 5.f;
		/** Segundos en que no golpea a quien lo lanza (el balón, después, sí: rebota). */
		float SelfSafeSeconds = 0.35f;
		bool bCanHitThrower = false;
	};

	FFlight FlightOf(ETNTctItem Kind)
	{
		using namespace TNTctItemTuning;
		FFlight Out;
		switch (Kind)
		{
		case ETNTctItem::BeachBall:
			Out.Radius = 26.f; Out.Gravity = 0.8f; Out.bBounce = true; Out.Bounciness = 0.78f; Out.Friction = 0.08f;
			Out.Life = BallLifeSeconds; Out.SelfSafeSeconds = 0.6f; Out.bCanHitThrower = true;
			break;
		case ETNTctItem::Anchor:
			Out.Radius = 22.f; Out.Gravity = 1.7f; Out.bFollowVelocity = false; Out.Life = AnchorLifeSeconds;
			break;
		case ETNTctItem::JellyDart:
			Out.Radius = 8.f; Out.Gravity = 0.12f; Out.bFollowVelocity = true; Out.Life = DartLifeSeconds;
			break;
		case ETNTctItem::Cocobomba:
			// Rueda poco y se queda donde cae hasta que explota (manda la mecha, no la vida).
			Out.Radius = 16.f; Out.Gravity = 1.f; Out.bBounce = true; Out.Bounciness = 0.3f; Out.Friction = 0.7f;
			Out.Life = CocoFuseSeconds + 3.f;
			break;
		case ETNTctItem::Alga:
			Out.Radius = 12.f; Out.Gravity = 1.f; Out.Life = 4.f;
			break;
		case ETNTctItem::Red:
			// Lenta de ver y de poco alcance: baja pronto.
			Out.Radius = 30.f; Out.Gravity = 0.6f; Out.Life = NetLifeSeconds;
			break;
		default:
			break;
		}
		return Out;
	}

	float SpeedOf(ETNTctItem Kind)
	{
		switch (Kind)
		{
		case ETNTctItem::BeachBall: return TNTctItemTuning::BallSpeed;
		case ETNTctItem::Anchor:    return TNTctItemTuning::AnchorSpeed;
		case ETNTctItem::JellyDart: return TNTctItemTuning::DartSpeed;
		case ETNTctItem::Cocobomba: return TNTctItemTuning::CocoSpeed;
		case ETNTctItem::Alga:      return TNTctItemTuning::AlgaSpeed;
		case ETNTctItem::Red:       return TNTctItemTuning::NetSpeed;
		default:                    return 0.f;
		}
	}

	/** Tras golpear o clavarse, lo que se queda a la vista antes de irse. */
	constexpr float LingerSeconds = 1.2f;

	/** Fogonazo de la cocobomba: lo que dura y su tamaño final (escala de la esfera del motor de 100 uu). */
	constexpr float BlastSeconds = 0.35f;
	constexpr float BlastScale = 2.f * TNTctItemTuning::CocoBlastRadius / 100.f;
}

ATN_TctProjectile::ATN_TctProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	// Cada máquina simula el vuelo desde el lanzamiento replicado (como la bola): sin movimiento replicado.
	SetReplicateMovement(false);
	SetNetCullDistanceSquared(FMath::Square(25000.f));

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(20.f);
	// Solo choca con el escenario: a las tortugas las barre el servidor (ServerSweep) para decidir él los golpes.
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Collision->SetCanEverAffectNavigation(false);
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->SetUpdatedComponent(Collision);
	Movement->bAutoActivate = false;
	Movement->InitialSpeed = 0.f;
	Movement->MaxSpeed = 0.f;
	// #708: pasos fijos de 1/60 s: el rebote contra una rampa no depende del fotograma de cada máquina.
	Movement->bForceSubStepping = true;
	Movement->MaxSimulationTimeStep = 1.f / 60.f;
	Movement->MaxSimulationIterations = 12;
}

void ATN_TctProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ATN_TctProjectile, Shot, COND_InitialOnly);
}

bool ATN_TctProjectile::ServerLaunch(ATortugaCharacter* Thrower, uint8 Kind, const FVector& Direction)
{
	UWorld* World = Thrower ? Thrower->GetWorld() : nullptr;
	const ETNTctItem Item = static_cast<ETNTctItem>(Kind);
	const float Speed = TNTctProjectileDetail::SpeedOf(Item);
	if (!World || !Thrower->HasAuthority() || Speed <= 0.f)
	{
		return false;
	}
	FVector Dir = Direction.GetSafeNormal();
	const FVector Flat = FVector(Dir.X, Dir.Y, 0.0).GetSafeNormal();
	const FVector Origin = Thrower->GetActorLocation() + FVector(0.0, 0.0, 50.0) + Flat * 70.0;
	// Hacia la mira (#707): los que caen en parábola, con el arco justo para llegar al punto del centro de la pantalla; el
	// dardo, casi recto, directo a él.
	if (Thrower->UsesCameraThrowAim())
	{
		const float Gravity = TNTctProjectileDetail::FlightOf(Item).Gravity;
		FVector Target;
		if (Gravity >= 0.5f)
		{
			Dir = Thrower->GetThrowDirectionToCrosshair(Origin, Thrower->GetTurtleAimRotation(), Speed, Gravity * FMath::Max(1.f, -World->GetGravityZ()));
		}
		else if (Thrower->GetCrosshairPoint(Target))
		{
			Dir = TNTctItemRules::AimToward(Origin, Target, Dir);
		}
	}
	// Lo que ya corría la tortuga se suma a medias (como al lanzar la bola corriendo).
	const FVector Carry = FVector(Thrower->GetVelocity().X, Thrower->GetVelocity().Y, 0.0) * 0.5;

	FActorSpawnParameters Params;
	Params.Owner = Thrower;
	Params.Instigator = Thrower;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	ATN_TctProjectile* Projectile = World->SpawnActor<ATN_TctProjectile>(ATN_TctProjectile::StaticClass(), FTransform(Dir.Rotation(), Origin), Params);
	if (!Projectile)
	{
		return false;
	}
	// En el primer paquete: los clientes nacen ya con el lanzamiento.
	Projectile->Shot.Kind = Kind;
	Projectile->Shot.Origin = Origin;
	Projectile->Shot.Velocity = Dir * Speed + Carry;
	Projectile->FinishSpawning(FTransform(Dir.Rotation(), Origin));
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] %s lanza %s."), *GetNameSafe(Thrower), TNTctItemRules::Spec(Item).Code);
	return true;
}

void ATN_TctProjectile::BeginPlay()
{
	Super::BeginPlay();
	Movement->OnProjectileStop.AddDynamic(this, &ATN_TctProjectile::OnStop);
	Movement->OnProjectileBounce.AddDynamic(this, &ATN_TctProjectile::OnBounce);
	ApplyShot();
}

void ATN_TctProjectile::OnRep_Shot()
{
	ApplyShot();
}

void ATN_TctProjectile::ApplyShot()
{
	const ETNTctItem Kind = static_cast<ETNTctItem>(Shot.Kind);
	if (bShotApplied || Kind == ETNTctItem::None || !HasActorBegunPlay())
	{
		return;
	}
	bShotApplied = true;
	const TNTctProjectileDetail::FFlight Flight = TNTctProjectileDetail::FlightOf(Kind);
	Collision->SetSphereRadius(Flight.Radius);
	if (UStaticMesh* Look = TNTctItems::LoadMesh(Kind, true))
	{
		Mesh->SetStaticMesh(Look);
		Mesh->SetRelativeScale3D(TNTctItems::MeshScale(Kind, true, Look->GetPathName().StartsWith(TEXT("/Engine/BasicShapes/"))));
	}
	Movement->ProjectileGravityScale = Flight.Gravity;
	Movement->bShouldBounce = Flight.bBounce;
	Movement->Bounciness = Flight.Bounciness;
	Movement->Friction = Flight.Friction;
	Movement->bRotationFollowsVelocity = Flight.bFollowVelocity;
	Movement->BounceVelocityStopSimulatingThreshold = 60.f;
	SetActorLocation(Shot.Origin, false, nullptr, ETeleportType::TeleportPhysics);
	Movement->Velocity = Shot.Velocity;
	Movement->Activate(true);
	PrevLocation = Shot.Origin;
	if (HasAuthority())
	{
		SetLifeSpan(Flight.Life);
	}
}

void ATN_TctProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	TickBlast(DeltaSeconds);
	const FVector Here = GetActorLocation();
	if (HasAuthority() && bShotApplied && !bFinished)
	{
		ServerSweep(PrevLocation, Here);
		// En el agua de la arena se hunde (la cocobomba se apaga sin explotar).
		const ATN_TctGameState* State = GetWorld() ? GetWorld()->GetGameState<ATN_TctGameState>() : nullptr;
		if (!bFinished && State && Here.Z < State->GetWaterZ())
		{
			ServerFinish(Here);
		}
		if (!bFinished && static_cast<ETNTctItem>(Shot.Kind) == ETNTctItem::Cocobomba && Age >= TNTctItemTuning::CocoFuseSeconds)
		{
			ServerCocoBlast(Here);
		}
	}
	PrevLocation = Here;
}

// ─────────────────────────────────────────────────────────────────────────────
// Golpes (servidor)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_TctProjectile::ServerSweep(const FVector& From, const FVector& To)
{
	UWorld* World = GetWorld();
	const ETNTctItem Kind = static_cast<ETNTctItem>(Shot.Kind);
	const TNTctProjectileDetail::FFlight Flight = TNTctProjectileDetail::FlightOf(Kind);
	if (!World)
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TctProjectile), false, this);
	if (Age < Flight.SelfSafeSeconds || !Flight.bCanHitThrower)
	{
		Params.AddIgnoredActor(GetInstigator());
	}
	TArray<FHitResult> Hits;
	const FVector End = From.Equals(To, 0.5) ? To + FVector(0.0, 0.0, 1.0) : To;
	World->SweepMultiByObjectType(Hits, From, End, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(Flight.Radius + 25.f), Params);
	for (const FHitResult& Hit : Hits)
	{
		ATortugaCharacter* Victim = Cast<ATortugaCharacter>(Hit.GetActor());
		const float* Until = Victim ? RehitUntil.Find(Victim) : nullptr;
		if (!Victim || Victim->IsDead() || (Until && Age < *Until))
		{
			continue;
		}
		ServerHitTurtle(Victim);
		if (bFinished)
		{
			return;
		}
	}
}

void ATN_TctProjectile::ServerHitTurtle(ATortugaCharacter* Victim)
{
	using namespace TNTctItemTuning;
	const ETNTctItem Kind = static_cast<ETNTctItem>(Shot.Kind);
	RehitUntil.Add(Victim, Age + BallRehitSeconds);
	switch (Kind)
	{
	case ETNTctItem::BeachBall:
	{
		const FVector Push = TNTctItemRules::BallPush(Movement->Velocity);
		if (Push.IsZero() || !TNTctItems::CanAffect(Victim, true))
		{
			return;
		}
		UTN_TurtleMovementComponent::LaunchFromServer(Victim, Push);
		TNTctItems::PlayCue(Victim, ETNRaceSound::Boing, 1.15f);
		// Rebota hacia atrás y algo hacia arriba; todas las máquinas siguen desde aquí.
		const FVector Old = Movement->Velocity;
		const FVector Bounced(-Old.X * 0.45, -Old.Y * 0.45, FMath::Max(Old.Z, 300.0));
		Movement->Velocity = Bounced;
		MulticastResync(GetActorLocation(), Bounced);
		break;
	}
	case ETNTctItem::Anchor:
		ServerAnchorSplash(GetActorLocation());
		break;
	case ETNTctItem::Red:
		// La red clava a quien toca (no la empuja): 2,5 s sin poder andar ni saltar.
		if (TNTctItems::CanAffect(Victim, false))
		{
			if (UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOrAddOn(Victim))
			{
				Effects->GrantFx(ETNTctFx::Net, NetRootSeconds);
			}
			TNTctItems::PlayCue(Victim, ETNRaceSound::Catch, 0.8f);
			UE_LOG(LogTortunabo, Log, TEXT("[TcT] La red clava a %s."), *GetNameSafe(Victim));
		}
		ServerFinish(GetActorLocation());
		break;
	case ETNTctItem::JellyDart:
		if (TNTctItems::CanAffect(Victim, false))
		{
			Victim->MulticastApplyMareoEffect(DartDizzySeconds);
			TNTctItems::PlayCue(Victim, ETNRaceSound::Zap, 1.3f);
			UE_LOG(LogTortunabo, Log, TEXT("[TcT] El dardo de medusa marea a %s."), *GetNameSafe(Victim));
		}
		ServerFinish(GetActorLocation());
		break;
	default:
		break;
	}
}

void ATN_TctProjectile::ServerAnchorSplash(const FVector& Center)
{
	if (bFinished)
	{
		return;
	}
	TArray<ATortugaCharacter*> Turtles;
	// Quien la lanza no se libra si se queda debajo, salvo justo al lanzarla.
	TNTctItems::GatherTurtles(this, Age < 0.3f ? GetInstigator() : nullptr, Turtles);
	int32 Hit = 0;
	for (ATortugaCharacter* Turtle : Turtles)
	{
		FVector Impulse;
		if (!TNTctItems::CanAffect(Turtle, false) || !TNTctItemRules::AnchorSplash(Center, Turtle->GetActorLocation(), Impulse))
		{
			continue;
		}
		TNBeach::KnockDownTurtle(Turtle, TNTctItemTuning::AnchorKnockSeconds, Impulse);
		if (UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOrAddOn(Turtle))
		{
			Effects->GrantHeavy(TNTctItemTuning::AnchorHeavySeconds);
		}
		TNTctItems::PlayCue(Turtle, ETNRaceSound::Bonk, 0.7f);
		++Hit;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] El ancla cae: %d tortugas derribadas y lastradas."), Hit);
	ServerFinish(Center);
}

void ATN_TctProjectile::ServerCocoBlast(const FVector& Center)
{
	if (bFinished)
	{
		return;
	}
	TArray<ATortugaCharacter*> Turtles;
	// Quien la lanza tampoco se libra si se queda cerca.
	TNTctItems::GatherTurtles(this, nullptr, Turtles);
	int32 Pushed = 0;
	for (ATortugaCharacter* Turtle : Turtles)
	{
		FVector Push;
		// El caparazón protege del empujón.
		if (!TNTctItems::CanAffect(Turtle, true) || !TNTctItemRules::CocoBlast(Center, Turtle->GetActorLocation(), Push))
		{
			continue;
		}
		UTN_TurtleMovementComponent::LaunchFromServer(Turtle, Push);
		TNTctItems::PlayCue(Turtle, ETNRaceSound::Bonk, 0.6f);
		++Pushed;
	}
	if (ACharacter* Thrower = Cast<ACharacter>(GetInstigator()))
	{
		TNTctItems::PlayCue(Thrower, ETNRaceSound::Rumble, 1.7f);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] La cocobomba explota: %d tortugas lanzadas."), Pushed);
	ServerFinish(Center);
}

void ATN_TctProjectile::ServerDropPuddle(const FVector& Where)
{
	if (bFinished)
	{
		return;
	}
	ATN_TctAlgaPuddle::ServerSpawn(GetWorld(), Where, GetInstigator());
	if (ACharacter* Thrower = Cast<ACharacter>(GetInstigator()))
	{
		TNTctItems::PlayCue(Thrower, ETNRaceSound::Splat, 0.8f);
	}
	ServerFinish(Where);
}

void ATN_TctProjectile::ServerFinish(const FVector& Location)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	MulticastImpact(Location);
	SetLifeSpan(TNTctProjectileDetail::LingerSeconds);
}

void ATN_TctProjectile::OnBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
	// El balón y la cocobomba rebotan en cada máquina: tras cada rebote (como mucho cada 0,1 s) el servidor devuelve a todas a
	// su trayectoria (#708); contra una rampa, los rebotes seguidos separaban al cliente del anfitrión.
	const ETNTctItem Bouncer = static_cast<ETNTctItem>(Shot.Kind);
	if (HasAuthority() && !bFinished && (Bouncer == ETNTctItem::BeachBall || Bouncer == ETNTctItem::Cocobomba) && GetWorld()
		&& GetWorld()->GetTimeSeconds() - LastBounceSyncTime >= 0.1)
	{
		LastBounceSyncTime = GetWorld()->GetTimeSeconds();
		MulticastResync(GetActorLocation(), Movement->Velocity);
	}
	// El ancla no rebota: si el suelo la para de lado (sin OnStop), cae igual.
	if (HasAuthority() && static_cast<ETNTctItem>(Shot.Kind) == ETNTctItem::Anchor && ImpactResult.ImpactNormal.Z > 0.4)
	{
		ServerAnchorSplash(ImpactResult.ImpactPoint);
	}
}

void ATN_TctProjectile::OnStop(const FHitResult& ImpactResult)
{
	if (!HasAuthority() || bFinished)
	{
		return;
	}
	switch (static_cast<ETNTctItem>(Shot.Kind))
	{
	case ETNTctItem::Anchor:
		ServerAnchorSplash(GetActorLocation());
		break;
	case ETNTctItem::JellyDart:
	case ETNTctItem::Red:
		// Clavado en el escenario un momento.
		ServerFinish(GetActorLocation());
		break;
	case ETNTctItem::Alga:
		ServerDropPuddle(GetActorLocation());
		break;
	case ETNTctItem::Cocobomba:
		// Quieta en el suelo hasta que se acaba la mecha.
		break;
	default:
		// El balón quieto se va al poco.
		SetLifeSpan(FMath::Min(GetLifeSpan() > 0.f ? GetLifeSpan() : 2.f, 2.f));
		break;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Todas las máquinas
// ─────────────────────────────────────────────────────────────────────────────

void ATN_TctProjectile::MulticastResync_Implementation(FVector_NetQuantize Location, FVector_NetQuantize Velocity)
{
	if (HasAuthority() || bFinished)
	{
		return;
	}
	SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	if (!Movement->IsActive())
	{
		Movement->Activate(true);
	}
	Movement->Velocity = Velocity;
}

void ATN_TctProjectile::MulticastImpact_Implementation(FVector_NetQuantize Location)
{
	bFinished = true;
	StopAt(Location);
	if (static_cast<ETNTctItem>(Shot.Kind) != ETNTctItem::Cocobomba)
	{
		return;
	}
	const ATN_TctGameState* State = GetWorld() ? GetWorld()->GetGameState<ATN_TctGameState>() : nullptr;
	// En el agua se apaga sin fogonazo.
	if (!State || Location.Z >= State->GetWaterZ())
	{
		ShowBlast();
	}
	Mesh->SetVisibility(false);
}

void ATN_TctProjectile::ShowBlast()
{
	if (BlastMesh || IsRunningDedicatedServer() || !FApp::CanEverRender())
	{
		return;
	}
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	BlastMesh = Sphere ? NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient) : nullptr;
	if (!BlastMesh)
	{
		return;
	}
	BlastMesh->SetStaticMesh(Sphere);
	BlastMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BlastMesh->SetCastShadow(false);
	BlastMesh->SetupAttachment(RootComponent);
	BlastMesh->RegisterComponent();
	BlastMesh->SetRelativeScale3D(FVector(0.2));
	if (UMaterialInstanceDynamic* Material = BlastMesh->CreateDynamicMaterialInstance(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.55f, 0.12f));
	}
	BlastAge = 0.f;
}

void ATN_TctProjectile::TickBlast(float DeltaSeconds)
{
	using namespace TNTctProjectileDetail;
	if (!BlastMesh || BlastAge < 0.f)
	{
		return;
	}
	BlastAge += DeltaSeconds;
	const float Alpha = FMath::Clamp(BlastAge / BlastSeconds, 0.f, 1.f);
	if (Alpha >= 1.f)
	{
		BlastMesh->DestroyComponent();
		BlastMesh = nullptr;
		BlastAge = -1.f;
		return;
	}
	// Se hincha deprisa hasta el radio de la explosión y se deshincha al final.
	const float Fade = Alpha > 0.7f ? (1.f - Alpha) / 0.3f : 1.f;
	BlastMesh->SetRelativeScale3D(FVector(FMath::Max(0.05f, BlastScale * FMath::Sin(Alpha * PI * 0.5f) * Fade)));
}

void ATN_TctProjectile::StopAt(const FVector& Location)
{
	Movement->StopMovementImmediately();
	Movement->Deactivate();
	SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
}
