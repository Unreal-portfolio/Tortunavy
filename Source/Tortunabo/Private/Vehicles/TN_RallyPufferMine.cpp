#include "Vehicles/TN_RallyPufferMine.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_RallyProjectile.h"
#include "Vehicles/TN_RallyTurretLogic.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace TNRallyPufferDetail
{
	const TCHAR* const SphereMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* const ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	/** La esfera básica mide 100 cm. */
	constexpr float BasicShapeRadiusCm = 50.f;
	/** Comprobaciones de los buggies por segundo (servidor). */
	constexpr float CheckInterval = 0.05f;
	/** Subida sobre el punto de caída desde la que se busca el suelo (cm) y bajada máxima. */
	constexpr float ProbeUpCm = 100.f;
	constexpr float ProbeDownCm = 5000.f;
}

ATN_RallyPufferMine::ATN_RallyPufferMine()
{
	using namespace TNRallyPufferDetail;
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(10.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(SphereMeshPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(ShapeMaterialPath);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetStaticMesh(SphereMesh.Object);
	if (ShapeMaterial.Object)
	{
		Mesh->SetMaterial(0, ShapeMaterial.Object);
	}
	Mesh->SetCollisionProfileName(TEXT("NoCollision"));
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCastShadow(false);
	// Apoyada en el suelo: el centro de la esfera, a su radio sobre la raíz (que está en el suelo).
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, TNRallyTurret::PufferRadiusCm));
	Mesh->SetRelativeScale3D(FVector(TNRallyTurret::PufferRadiusCm / BasicShapeRadiusCm));
}

void ATN_RallyPufferMine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_RallyPufferMine, bTriggered);
}

ATN_RallyPufferMine* ATN_RallyPufferMine::SpawnOnGround(UWorld* World, const FVector& Where, ATN_Buggy* Thrower)
{
	using namespace TNRallyPufferDetail;
	if (!World)
	{
		return nullptr;
	}
	// Solo el escenario: un buggy o una tortuga debajo no la sostienen (como el charco de alga, #770).
	FHitResult Ground;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNRallyPufferGround), true);
	if (!World->LineTraceSingleByObjectType(Ground, Where + FVector(0.f, 0.f, ProbeUpCm), Where - FVector(0.f, 0.f, ProbeDownCm),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		UE_LOG(LogTNBuggy, Verbose, TEXT("Pez globo sin suelo bajo (%.0f, %.0f, %.0f)"), Where.X, Where.Y, Where.Z);
		return nullptr;
	}
	const FVector Normal = TNRallyTurret::IsPuddleGround(Ground.ImpactNormal) ? FVector(Ground.ImpactNormal) : FVector::UpVector;
	const FVector Forward = Thrower ? Thrower->GetActorForwardVector() : FVector::ForwardVector;
	const FTransform Spawn(TNRallyTurret::PuddleRotation(Normal, Forward), Ground.ImpactPoint);
	ATN_RallyPufferMine* Mine = World->SpawnActorDeferred<ATN_RallyPufferMine>(ATN_RallyPufferMine::StaticClass(), Spawn, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Mine)
	{
		Mine->Thrower = Thrower;
		Mine->FinishSpawning(Spawn);
	}
	return Mine;
}

void ATN_RallyPufferMine::BeginPlay()
{
	Super::BeginPlay();
	SetLifeSpan(TNRallyTurret::PufferLifeSeconds);
	TNRallyLook::Tint(Mesh, TNRallyLook::AmmoColor(ETNRallyAmmo::PezGlobo));
	// En los clientes solo hace falta para hincharse (OnRep_Triggered lo enciende).
	SetActorTickEnabled(HasAuthority());
}

void ATN_RallyPufferMine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bTriggered)
	{
		ApplyInflate();
	}
	if (!HasAuthority() || bExploded)
	{
		return;
	}
	if (bTriggered)
	{
		if (GetWorld()->GetTimeSeconds() - TriggeredAt >= TNRallyTurret::PufferInflateSeconds)
		{
			Explode();
		}
		return;
	}
	CheckAccumulator += DeltaSeconds;
	if (CheckAccumulator >= TNRallyPufferDetail::CheckInterval)
	{
		CheckAccumulator = 0.f;
		CheckTrigger();
	}
}

void ATN_RallyPufferMine::CheckTrigger()
{
	const float Age = GetGameTimeSinceCreation();
	const FVector Center = Mesh->GetComponentLocation();
	for (TActorIterator<ATN_Buggy> It(GetWorld()); It; ++It)
	{
		ATN_Buggy* Buggy = *It;
		const float Distance = static_cast<float>(FVector::Dist(Buggy->GetActorLocation(), Center));
		if (Buggy->IsRespawnProtected() || !TNRallyTurret::PufferTriggers(Age, Distance, Buggy == Thrower.Get()))
		{
			continue;
		}
		bTriggered = true;
		TriggeredAt = GetWorld()->GetTimeSeconds();
		TriggeredBy = Buggy;
		ForceNetUpdate();
		UE_LOG(LogTNBuggy, Verbose, TEXT("Pez globo: lo dispara %s a %.0f cm"), *Buggy->GetName(), Distance);
		return;
	}
}

void ATN_RallyPufferMine::Explode()
{
	bExploded = true;
	const FVector Where = Mesh->GetComponentLocation();
	// La explosión del mortero (radio 5 m, impulso vertical, daño) y su ráfaga, por la torreta del que la ha pisado.
	ATN_RallyProjectile::MortarBlastAt(GetWorld(), Thrower.Get(), nullptr, Where, FVector::UpVector, false, ETNRallyAmmo::PezGlobo);
	ATN_Buggy* Via = TriggeredBy.IsValid() ? TriggeredBy.Get() : Thrower.Get();
	if (Via)
	{
		ATN_RallyBurstFX::Broadcast(Via, ETNRallyBurstKind::Explosion, Where, TNRallyTurret::MortarRadiusCm);
	}
	Destroy();
}

void ATN_RallyPufferMine::OnRep_Triggered()
{
	if (bTriggered && TriggeredAt < 0.0 && GetWorld())
	{
		TriggeredAt = GetWorld()->GetTimeSeconds();
		SetActorTickEnabled(true);
	}
}

void ATN_RallyPufferMine::ApplyInflate()
{
	if (TriggeredAt < 0.0 || !GetWorld())
	{
		return;
	}
	const float Scale = TNRallyTurret::PufferInflate(static_cast<float>(GetWorld()->GetTimeSeconds() - TriggeredAt));
	const float Radius = TNRallyTurret::PufferRadiusCm * Scale;
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, Radius));
	Mesh->SetRelativeScale3D(FVector(Radius / TNRallyPufferDetail::BasicShapeRadiusCm));
}
