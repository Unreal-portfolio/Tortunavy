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
		default:                    return 0.f;
		}
	}

	/** Tras golpear o clavarse, lo que se queda a la vista antes de irse. */
	constexpr float LingerSeconds = 1.2f;
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
	const FVector Dir = Direction.GetSafeNormal();
	const FVector Flat = FVector(Dir.X, Dir.Y, 0.0).GetSafeNormal();
	const FVector Origin = Thrower->GetActorLocation() + FVector(0.0, 0.0, 50.0) + Flat * 70.0;
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
		Mesh->SetRelativeScale3D(TNTctItems::MeshScale(Kind, true, Look->GetPathName().StartsWith(TEXT("/Engine/"))));
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
	const FVector Here = GetActorLocation();
	if (HasAuthority() && bShotApplied && !bFinished)
	{
		ServerSweep(PrevLocation, Here);
		// En el agua de la arena se hunde.
		const ATN_TctGameState* State = GetWorld() ? GetWorld()->GetGameState<ATN_TctGameState>() : nullptr;
		if (!bFinished && State && Here.Z < State->GetWaterZ())
		{
			ServerFinish(Here);
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
	case ETNTctItem::JellyDart:
		if (TNTctItems::CanAffect(Victim, false))
		{
			Victim->ApplyMareoEffect(DartDizzySeconds);
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
		// Clavado en el escenario un momento.
		ServerFinish(GetActorLocation());
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
}

void ATN_TctProjectile::StopAt(const FVector& Location)
{
	Movement->StopMovementImmediately();
	Movement->Deactivate();
	SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
}
