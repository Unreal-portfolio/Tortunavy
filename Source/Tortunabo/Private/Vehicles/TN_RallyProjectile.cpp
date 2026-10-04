#include "Vehicles/TN_RallyProjectile.h"
#include "Rally/TN_RallyHitReport.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyHealthComponent.h"
#include "Vehicles/TN_BuggyMath.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Vehicles/TN_RallyAnchor.h"
#include "Vehicles/TN_RallyFXParticles.h"
#include "Vehicles/TN_RallyTurretLogic.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace TNRallyFX
{
	const TCHAR* const SphereMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* const CylinderMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* const ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	const FName ColorParameter(TEXT("Color"));

	/** Las mallas básicas miden 100 cm: escala para un radio dado. */
	constexpr float BasicShapeRadiusCm = 50.f;

	/** Segundos que el proyectil no choca con su propio buggy al salir del cañón. */
	constexpr float OwnerIgnoreSeconds = 0.4f;

	/** Envíos por segundo del proyectil: solo lleva la munición y el final; el vuelo lo simula cada cliente. */
	constexpr float ProjectileNetUpdateFrequency = 20.f;

	float RadiusFor(ETNRallyAmmo Ammo)
	{
		switch (Ammo)
		{
		case ETNRallyAmmo::Burbuja: return 70.f;
		case ETNRallyAmmo::Mortero: return 28.f;
		case ETNRallyAmmo::Alga: return 24.f;
		case ETNRallyAmmo::Ancla: return 26.f;
		default: return 18.f;
		}
	}

	FLinearColor ColorFor(ETNRallyBurstKind Kind)
	{
		switch (Kind)
		{
		case ETNRallyBurstKind::Explosion: return FLinearColor(1.00f, 0.55f, 0.10f);
		case ETNRallyBurstKind::Ink: return FLinearColor(0.05f, 0.02f, 0.10f);
		case ETNRallyBurstKind::BubblePop: return FLinearColor(0.60f, 0.85f, 1.00f);
		case ETNRallyBurstKind::Shield: return FLinearColor(0.40f, 0.90f, 1.00f);
		case ETNRallyBurstKind::Sand: return FLinearColor(0.85f, 0.72f, 0.45f);
		case ETNRallyBurstKind::MuzzleFlash: return FLinearColor(1.00f, 0.85f, 0.40f);
		case ETNRallyBurstKind::Sparks: return FLinearColor(1.00f, 0.70f, 0.20f);
		case ETNRallyBurstKind::Smoke: return FLinearColor(0.16f, 0.16f, 0.17f);
		default: return FLinearColor(0.55f, 0.35f, 0.15f);
		}
	}

	/** Velocidad de subida del humo (cm/s); las demás ráfagas no se mueven. */
	constexpr float SmokeRiseCms = 160.f;

	/** Duración de cada ráfaga (s): el fogonazo y las chispas son más cortos; la nube de arena, más larga. */
	float BurstSeconds(ETNRallyBurstKind Kind)
	{
		switch (Kind)
		{
		case ETNRallyBurstKind::MuzzleFlash: return 0.15f;
		case ETNRallyBurstKind::Sparks: return 0.25f;
		case ETNRallyBurstKind::Sand: return 1.2f;
		case ETNRallyBurstKind::Smoke: return 1.1f;
		default: return 0.5f;
		}
	}

	UStaticMeshComponent* MakeVisual(AActor* Owner, UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Comp = Owner->CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
		Comp->SetStaticMesh(Mesh);
		if (Material)
		{
			Comp->SetMaterial(0, Material);
		}
		Comp->SetCollisionProfileName(TEXT("NoCollision"));
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCastShadow(false);
		return Comp;
	}

	void SpawnTrail(UNiagaraSystem* System, USceneComponent* AttachTo)
	{
		if (System && AttachTo && AttachTo->GetNetMode() != NM_DedicatedServer)
		{
			UNiagaraFunctionLibrary::SpawnSystemAttached(System, AttachTo, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::KeepRelativeOffset, true);
		}
	}
}

namespace TNRallyLook
{
	FLinearColor AmmoColor(ETNRallyAmmo Ammo)
	{
		switch (Ammo)
		{
		case ETNRallyAmmo::Alga: return FLinearColor(0.10f, 0.55f, 0.15f);
		case ETNRallyAmmo::Burbuja: return FLinearColor(0.60f, 0.85f, 1.00f);
		case ETNRallyAmmo::Mortero: return FLinearColor(0.12f, 0.12f, 0.12f);
		case ETNRallyAmmo::Tinta: return FLinearColor(0.05f, 0.02f, 0.10f);
		case ETNRallyAmmo::Ancla: return FLinearColor(0.45f, 0.47f, 0.50f);
		default: return FLinearColor(0.35f, 0.20f, 0.08f);
		}
	}

	void Tint(UStaticMeshComponent* Mesh, const FLinearColor& Color)
	{
		if (!Mesh)
		{
			return;
		}
		UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
		if (!Mid)
		{
			Mid = Mesh->CreateDynamicMaterialInstance(0);
		}
		if (Mid)
		{
			Mid->SetVectorParameterValue(TNRallyFX::ColorParameter, Color);
		}
	}
}

// ── Proyectil ─────────────────────────────────────────────────────────────────

ATN_RallyProjectile::ATN_RallyProjectile()
{
	using namespace TNRallyFX;
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	// Sin movimiento replicado: con él, cada envío devolvía al cliente a la posición del servidor de hace una latencia
	// (a 60 m/s, varios metros) y el proyectil iba a saltos. Cada cliente simula el vuelo desde LaunchVelocity.
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(ProjectileNetUpdateFrequency);

	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->InitSphereRadius(RadiusFor(ETNRallyAmmo::Coco));
	Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Sphere->SetCollisionObjectType(ECC_WorldDynamic);
	Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	Sphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Sphere->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Block);
	Sphere->SetGenerateOverlapEvents(true);
	Sphere->CanCharacterStepUpOn = ECB_No;
	RootComponent = Sphere;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(SphereMeshPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(ShapeMaterialPath);
	Mesh = MakeVisual(this, SphereMesh.Object, ShapeMaterial.Object);
	Mesh->SetupAttachment(Sphere);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Sphere;
	Movement->bRotationFollowsVelocity = true;
	Movement->bShouldBounce = false;
	// Init da la velocidad en mundo; con el valor por defecto (local) un disparo hacia atrás saldría hacia delante.
	Movement->bInitialVelocityInLocalSpace = false;
	Movement->InitialSpeed = 0.f;
	Movement->MaxSpeed = 0.f;
}

void ATN_RallyProjectile::Init(ETNRallyAmmo InAmmo, const FVector& Velocity, ATN_Buggy* FiredBy)
{
	Ammo = InAmmo;
	Shooter = FiredBy;
	LaunchVelocity = Velocity;
	Movement->Velocity = Velocity;
	Movement->ProjectileGravityScale = TNRallyTurret::SpecFor(Ammo).GravityScale;
}

void ATN_RallyProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ATN_RallyProjectile, Ammo, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ATN_RallyProjectile, LaunchVelocity, COND_InitialOnly);
}

void ATN_RallyProjectile::StartLocalSimulation()
{
	// Las propiedades iniciales llegan antes de BeginPlay: el cliente sale con la misma velocidad que el servidor.
	Movement->Velocity = LaunchVelocity;
	Movement->UpdateComponentVelocity();
}

void ATN_RallyProjectile::BeginPlay()
{
	Super::BeginPlay();
	ApplyLook();
	Movement->ProjectileGravityScale = TNRallyTurret::SpecFor(Ammo).GravityScale;
	SetLifeSpan(TNRallyTurret::SpecFor(Ammo).LifeSeconds);
	if (!HasAuthority())
	{
		StartLocalSimulation();
	}
	TNRallyFX::SpawnTrail(TrailFX, Sphere);
	if (Ammo == ETNRallyAmmo::Burbuja)
	{
		// Se coge al tocarla: solapa con los buggies en vez de chocar.
		Sphere->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Overlap);
	}
	// En los clientes Init no ha corrido: el buggy que dispara es el dueño replicado.
	if (!Shooter.IsValid())
	{
		Shooter = Cast<ATN_Buggy>(GetOwner());
	}
	if (ATN_Buggy* ShooterBuggy = Shooter.Get())
	{
		Sphere->IgnoreActorWhenMoving(ShooterBuggy, true);
		GetWorldTimerManager().SetTimer(OwnerIgnoreTimer, this, &ATN_RallyProjectile::StopOwnerIgnore, TNRallyFX::OwnerIgnoreSeconds, false);
	}
	if (HasAuthority())
	{
		Sphere->OnComponentHit.AddDynamic(this, &ATN_RallyProjectile::OnSphereHit);
		Sphere->OnComponentBeginOverlap.AddDynamic(this, &ATN_RallyProjectile::OnSphereOverlap);
	}
}

void ATN_RallyProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority() && !bImpacted)
	{
		const FVector Where = GetActorLocation();
		UE_LOG(LogTNBuggy, Verbose, TEXT("%s sin impacto: acaba en (%.0f, %.0f, %.0f) vel=(%.0f, %.0f, %.0f) gravedad=%.2f"),
			*UEnum::GetValueAsString(Ammo), Where.X, Where.Y, Where.Z, Movement->Velocity.X, Movement->Velocity.Y, Movement->Velocity.Z,
			Movement->ProjectileGravityScale);
	}
	Super::EndPlay(EndPlayReason);
}

void ATN_RallyProjectile::StopOwnerIgnore()
{
	if (ATN_Buggy* ShooterBuggy = Shooter.Get())
	{
		Sphere->IgnoreActorWhenMoving(ShooterBuggy, false);
	}
	Shooter.Reset();
	// Una burbuja que ya solapaba con su buggy al acabar la espera: se coge ahora.
	if (HasAuthority() && Ammo == ETNRallyAmmo::Burbuja)
	{
		TArray<AActor*> Overlapping;
		Sphere->GetOverlappingActors(Overlapping, ATN_Buggy::StaticClass());
		if (Overlapping.Num() > 0)
		{
			Impact(Cast<ATN_Buggy>(Overlapping[0]), GetActorLocation());
		}
	}
}

void ATN_RallyProjectile::OnRep_Ammo()
{
	ApplyLook();
}

void ATN_RallyProjectile::ApplyLook()
{
	const float Radius = TNRallyFX::RadiusFor(Ammo);
	Sphere->SetSphereRadius(Radius);
	Mesh->SetRelativeScale3D(FVector(Radius / TNRallyFX::BasicShapeRadiusCm));
	TNRallyLook::Tint(Mesh, TNRallyLook::AmmoColor(Ammo));
}

void ATN_RallyProjectile::OnSphereHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s choca con %s (%s)"), *UEnum::GetValueAsString(Ammo), *GetNameSafe(OtherActor), *GetNameSafe(OtherComp));
	if (Ammo == ETNRallyAmmo::Burbuja && !Cast<ATN_Buggy>(OtherActor))
	{
		// La burbuja se queda flotando donde choca hasta que alguien la coge o se acaba su vida.
		return;
	}
	const bool bGunnerHit = OtherComp && OtherComp->ComponentHasTag(UTN_BuggyHealthComponent::GunnerHitboxTag);
	Impact(Cast<ATN_Buggy>(OtherActor), Hit.ImpactPoint, bGunnerHit);
}

void ATN_RallyProjectile::OnSphereOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	ATN_Buggy* Buggy = Cast<ATN_Buggy>(OtherActor);
	if (!Buggy || Ammo != ETNRallyAmmo::Burbuja || Buggy == Shooter.Get())
	{
		return;
	}
	Impact(Buggy, GetActorLocation());
}

void ATN_RallyProjectile::Impact(ATN_Buggy* HitBuggy, const FVector& Where, bool bGunnerHit)
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || bImpacted || !World)
	{
		return;
	}
	bImpacted = true;
	const FVector Dir = Movement->Velocity.GetSafeNormal();
	ATN_Buggy* Via = HitBuggy ? HitBuggy : Cast<ATN_Buggy>(GetOwner());
	UE_LOG(LogTNBuggy, Verbose, TEXT("Impacto de %s en (%.0f, %.0f, %.0f) contra %s%s"), *UEnum::GetValueAsString(Ammo),
		Where.X, Where.Y, Where.Z, HitBuggy ? *HitBuggy->GetName() : TEXT("el escenario"), bGunnerHit ? TEXT(" (artillera)") : TEXT(""));

	switch (Ammo)
	{
	case ETNRallyAmmo::Alga:
		SpawnAlgaPuddle(HitBuggy, Where);
		HitBuggyWith(HitBuggy, Where, Dir, bGunnerHit);
		break;
	case ETNRallyAmmo::Burbuja:
		if (HitBuggy)
		{
			HitBuggy->GrantShield();
		}
		ATN_RallyBurstFX::Broadcast(Via, ETNRallyBurstKind::BubblePop, Where, 160.f);
		break;
	case ETNRallyAmmo::Mortero:
		MortarBlast(HitBuggy, Where, Dir, bGunnerHit);
		ATN_RallyBurstFX::Broadcast(Via, ETNRallyBurstKind::Explosion, Where, TNRallyTurret::MortarRadiusCm);
		break;
	case ETNRallyAmmo::Tinta:
		HitBuggyWith(HitBuggy, Where, Dir, bGunnerHit);
		ATN_RallyBurstFX::Broadcast(Via, ETNRallyBurstKind::Ink, Where, 150.f);
		break;
	case ETNRallyAmmo::Coco:
	case ETNRallyAmmo::Ancla:
		HitBuggyWith(HitBuggy, Where, Dir, bGunnerHit);
		ATN_RallyBurstFX::Broadcast(Via, ETNRallyBurstKind::CocoHit, Where, 80.f);
		break;
	default:
		break;
	}
	Destroy();
}

void ATN_RallyProjectile::HitBuggyWith(ATN_Buggy* HitBuggy, const FVector& Where, const FVector& Dir, bool bGunnerHit)
{
	if (!HitBuggy)
	{
		return;
	}
	if (UTN_BuggyHealthComponent* Health = HitBuggy->FindComponentByClass<UTN_BuggyHealthComponent>())
	{
		// Tras reaparecer es un fantasma: el impacto no existe y no se avisa a nadie (#332).
		const bool bGhost = HitBuggy->IsRespawnProtected();
		const bool bLanded = Health->ReceiveAmmoHit(Ammo, Where, Dir, bGunnerHit);
		if (!bGhost)
		{
			TNRallyHitLog::NotifyServer(Shooter.Get(), HitBuggy, Ammo, Where, !bLanded);
		}
		return;
	}
	// Sin componente de vida (un buggy de otra clase): los efectos de siempre, sin daño ni empujón en el punto.
	switch (Ammo)
	{
	case ETNRallyAmmo::Coco: HitBuggy->ApplyCocoHit(Dir); break;
	case ETNRallyAmmo::Tinta: HitBuggy->ApplyInk(); break;
	case ETNRallyAmmo::Mortero: HitBuggy->ApplyMortarBlast(); break;
	case ETNRallyAmmo::Ancla:
		if (!HitBuggy->TryConsumeShield())
		{
			ATN_RallyAnchorTether::Attach(HitBuggy, Where);
		}
		break;
	default: break;
	}
}

void ATN_RallyProjectile::SpawnAlgaPuddle(ATN_Buggy* HitBuggy, const FVector& Where)
{
	UWorld* World = GetWorld();
	// El charco va al suelo bajo el impacto.
	FVector Ground = Where;
	FHitResult Down;
	FCollisionQueryParams Params(FName(TEXT("TNRallyPuddle")), false, this);
	if (HitBuggy)
	{
		Params.AddIgnoredActor(HitBuggy);
	}
	if (World->LineTraceSingleByChannel(Down, Where + FVector(0.f, 0.f, 50.f), Where - FVector(0.f, 0.f, 1000.f), ECC_WorldStatic, Params))
	{
		Ground = Down.ImpactPoint;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World->SpawnActor<ATN_RallyAlgaPuddle>(ATN_RallyAlgaPuddle::StaticClass(), FTransform(Ground), Spawn);
}

void ATN_RallyProjectile::MortarBlast(ATN_Buggy* HitBuggy, const FVector& Where, const FVector& Dir, bool bGunnerHit)
{
	for (TActorIterator<ATN_Buggy> It(GetWorld()); It; ++It)
	{
		ATN_Buggy* Buggy = *It;
		if (FVector::Dist(Buggy->GetActorLocation(), Where) > TNRallyTurret::MortarRadiusCm)
		{
			continue;
		}
		// El alcanzado recibe el golpe en el punto del impacto; el resto, en su centro. La artillera, solo con impacto directo.
		const FVector Point = Buggy == HitBuggy ? Where : Buggy->GetActorLocation();
		HitBuggyWith(Buggy, Point, Dir, bGunnerHit && Buggy == HitBuggy);
	}
}

// ── Charco de alga ────────────────────────────────────────────────────────────

ATN_RallyAlgaPuddle::ATN_RallyAlgaPuddle()
{
	using namespace TNRallyFX;
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(CylinderMeshPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(ShapeMaterialPath);
	Mesh = MakeVisual(this, CylinderMesh.Object, ShapeMaterial.Object);
	RootComponent = Mesh;
	const float Scale = TNRallyTurret::AlgaPuddleRadiusCm / BasicShapeRadiusCm;
	// Disco de 6 m de radio y 4 cm de alto, apoyado en el suelo.
	Mesh->SetRelativeScale3D(FVector(Scale, Scale, 0.04f));

	static ConstructorHelpers::FObjectFinder<USoundBase> SplashFinder(TEXT("/Game/Audio/Rally/SFX_Impact_Splash.SFX_Impact_Splash"));
	SplashSound = SplashFinder.Object;
}

void ATN_RallyAlgaPuddle::BeginPlay()
{
	Super::BeginPlay();
	SetLifeSpan(TNRallyTurret::AlgaPuddleSeconds);
	TNRallyLook::Tint(Mesh, TNRallyLook::AmmoColor(ETNRallyAmmo::Alga));
	// La comprobación de los buggies es del servidor: los clientes no necesitan el Tick.
	SetActorTickEnabled(HasAuthority());
	if (SplashSound && GetNetMode() != NM_DedicatedServer)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, SplashSound, GetActorLocation());
	}
}

void ATN_RallyAlgaPuddle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
	CheckAccumulator += DeltaSeconds;
	if (CheckAccumulator < 0.1f)
	{
		return;
	}
	CheckAccumulator = 0.f;
	const FVector Center = GetActorLocation();
	for (TActorIterator<ATN_Buggy> It(GetWorld()); It; ++It)
	{
		ATN_Buggy* Buggy = *It;
		const FVector Delta = Buggy->GetActorLocation() - Center;
		if (FVector(Delta.X, Delta.Y, 0.f).Size() > TNRallyTurret::AlgaPuddleRadiusCm || FMath::Abs(Delta.Z) > 300.f)
		{
			continue;
		}
		if (Immune.Contains(Buggy))
		{
			continue;
		}
		if (Buggy->TryConsumeShield())
		{
			Immune.Add(Buggy);
			continue;
		}
		Buggy->NotePuddleContact();
	}
}

// ── Ráfaga cosmética ──────────────────────────────────────────────────────────

ATN_RallyBurstFX::ATN_RallyBurstFX()
{
	using namespace TNRallyFX;
	PrimaryActorTick.bCanEverTick = true;
	// Cosmética: cada máquina crea la suya (Broadcast). Antes se replicaba un actor por impacto.
	bReplicates = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(SphereMeshPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(ShapeMaterialPath);
	Mesh = MakeVisual(this, SphereMesh.Object, ShapeMaterial.Object);
	RootComponent = Mesh;

	static ConstructorHelpers::FObjectFinder<USoundBase> CocoFinder(TEXT("/Game/Audio/Rally/SFX_Impact_Coco.SFX_Impact_Coco"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SplashFinder(TEXT("/Game/Audio/Rally/SFX_Impact_Splash.SFX_Impact_Splash"));
	static ConstructorHelpers::FObjectFinder<USoundBase> BubbleFinder(TEXT("/Game/Audio/Rally/SFX_Impact_Bubble_Pop.SFX_Impact_Bubble_Pop"));
	static ConstructorHelpers::FObjectFinder<USoundBase> BlastFinder(TEXT("/Game/Audio/Rally/SFX_Buggy_Explode.SFX_Buggy_Explode"));
	ImpactSounds.Add(ETNRallyBurstKind::CocoHit, CocoFinder.Object);
	ImpactSounds.Add(ETNRallyBurstKind::Ink, SplashFinder.Object);
	ImpactSounds.Add(ETNRallyBurstKind::BubblePop, BubbleFinder.Object);
	ImpactSounds.Add(ETNRallyBurstKind::Shield, BubbleFinder.Object);
	ImpactSounds.Add(ETNRallyBurstKind::Explosion, BlastFinder.Object);
}

ATN_RallyBurstFX* ATN_RallyBurstFX::Spawn(UWorld* World, ETNRallyBurstKind InKind, const FVector& Where, float InRadiusCm)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	ATN_RallyBurstFX* Burst = World->SpawnActorDeferred<ATN_RallyBurstFX>(ATN_RallyBurstFX::StaticClass(), FTransform(Where), nullptr,
		nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Burst)
	{
		Burst->Kind = InKind;
		Burst->RadiusCm = InRadiusCm;
		Burst->FinishSpawning(FTransform(Where));
	}
	return Burst;
}

void ATN_RallyBurstFX::Broadcast(ATN_Buggy* Via, ETNRallyBurstKind InKind, const FVector& Where, float InRadiusCm)
{
	UTN_BuggyTurretComponent* Turret = Via ? Via->GetTurret() : nullptr;
	if (Turret && Via->HasAuthority())
	{
		// El multicast también se ejecuta en el servidor escucha, que crea su ráfaga local.
		Turret->MulticastBurst(InKind, Where, InRadiusCm);
		return;
	}
	Spawn(Via ? Via->GetWorld() : nullptr, InKind, Where, InRadiusCm);
}

void ATN_RallyBurstFX::BeginPlay()
{
	Super::BeginPlay();
	Particles = MakeShared<TNRallyParticles::FEmitterSet>();
	const float ParticleLife = TNRallyParticles::SpawnBurst(this, *Particles, Kind, GetActorLocation(), RadiusCm);
	SetLifeSpan(FMath::Max(TNRallyFX::BurstSeconds(Kind), ParticleLife) + 0.1f);
	ApplyLook();
	// Pendiente (#301): las partículas aún no se ven en las capturas del Rally; hasta confirmarlas, la esfera se queda.
	// Spawn no crea ráfagas en un servidor dedicado: aquí siempre hay quien escuche.
	if (const TObjectPtr<USoundBase>* Sound = ImpactSounds.Find(Kind); Sound && *Sound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, *Sound, GetActorLocation());
	}
}

void ATN_RallyBurstFX::ApplyLook()
{
	TNRallyLook::Tint(Mesh, TNRallyFX::ColorFor(Kind));
	Mesh->SetRelativeScale3D(FVector(0.2f * RadiusCm / TNRallyFX::BasicShapeRadiusCm));
}

void ATN_RallyBurstFX::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (Kind == ETNRallyBurstKind::Smoke)
	{
		AddActorWorldOffset(FVector(0.f, 0.f, TNRallyFX::SmokeRiseCms * DeltaSeconds));
	}
	if (Particles.IsValid())
	{
		TNRallyParticles::Tick(*Particles, FMath::Min(DeltaSeconds, 0.1f), TNRallyParticles::LocalView(GetWorld(), GetActorLocation()));
	}
	const float Alpha = FMath::Clamp(Age / TNRallyFX::BurstSeconds(Kind), 0.f, 1.f);
	// Crece rápido hasta el radio final y se encoge al final para desaparecer.
	const float Grow = 1.f - FMath::Square(1.f - Alpha);
	const float Shrink = Alpha > 0.8f ? 1.f - (Alpha - 0.8f) / 0.2f : 1.f;
	Mesh->SetRelativeScale3D(FVector(FMath::Max(0.01f, Grow * Shrink) * RadiusCm / TNRallyFX::BasicShapeRadiusCm));
}
