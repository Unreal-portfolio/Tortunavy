#include "Vehicles/TN_RallyAnchor.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_RallyCombatLogic.h"
#include "Vehicles/TN_RallyProjectile.h"
#include "Vehicles/TN_RallyTurretLogic.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace TNRallyAnchorDetail
{
	const TCHAR* const CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const CylinderMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* const ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	/** Las mallas básicas miden 100 cm. */
	constexpr float BasicShapeSizeCm = 100.f;
	constexpr float RopeThicknessCm = 6.f;
	constexpr float AnchorSizeCm = 45.f;
	/** Bajada máxima desde el gancho para buscar el suelo del ancla (cm). */
	constexpr float GroundProbeCm = 400.f;
	const FLinearColor RopeColor(0.30f, 0.20f, 0.10f);

	UStaticMeshComponent* MakeShape(AActor* Owner, const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Comp = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Comp->SetStaticMesh(Mesh);
		if (Material)
		{
			Comp->SetMaterial(0, Material);
		}
		Comp->SetCollisionProfileName(TEXT("NoCollision"));
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCastShadow(false);
		Comp->SetUsingAbsoluteLocation(true);
		Comp->SetUsingAbsoluteRotation(true);
		Comp->SetUsingAbsoluteScale(true);
		return Comp;
	}
}

ATN_RallyAnchorTether::ATN_RallyAnchorTether()
{
	using namespace TNRallyAnchorDetail;
	PrimaryActorTick.bCanEverTick = true;
	// Después de la física: la cuerda sigue al buggy en su posición del frame.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(5.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(CubeMeshPath);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(CylinderMeshPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(ShapeMaterialPath);
	AnchorMesh = MakeShape(this, TEXT("AnchorMesh"), CubeMesh.Object, ShapeMaterial.Object);
	AnchorMesh->SetupAttachment(Root);
	RopeMesh = MakeShape(this, TEXT("RopeMesh"), CylinderMesh.Object, ShapeMaterial.Object);
	RopeMesh->SetupAttachment(Root);
}

void ATN_RallyAnchorTether::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ATN_RallyAnchorTether, Target, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ATN_RallyAnchorTether, LocalHook, COND_InitialOnly);
}

ATN_RallyAnchorTether* ATN_RallyAnchorTether::Attach(ATN_Buggy* InTarget, const FVector& WorldHook)
{
	UWorld* World = InTarget ? InTarget->GetWorld() : nullptr;
	if (!World || !InTarget->HasAuthority())
	{
		return nullptr;
	}
	// Una sola ancla por buggy: la nueva sustituye a la anterior (no se acumula el freno).
	for (TActorIterator<ATN_RallyAnchorTether> It(World); It; ++It)
	{
		if (It->Target == InTarget)
		{
			It->Destroy();
		}
	}
	const FTransform Where(WorldHook);
	ATN_RallyAnchorTether* Tether = World->SpawnActorDeferred<ATN_RallyAnchorTether>(ATN_RallyAnchorTether::StaticClass(), Where,
		InTarget, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Tether)
	{
		return nullptr;
	}
	Tether->Target = InTarget;
	Tether->LocalHook = InTarget->GetActorTransform().InverseTransformPosition(WorldHook);
	Tether->FinishSpawning(Where);
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s: ancla enganchada %.0f s"), *InTarget->GetName(), TNRallyTurret::AnchorSeconds);
	return Tether;
}

void ATN_RallyAnchorTether::BeginPlay()
{
	Super::BeginPlay();
	SetLifeSpan(TNRallyTurret::AnchorSeconds);
	TNRallyLook::Tint(AnchorMesh, TNRallyLook::AmmoColor(ETNRallyAmmo::Ancla));
	TNRallyLook::Tint(RopeMesh, TNRallyAnchorDetail::RopeColor);
	AnchorMesh->SetWorldScale3D(FVector(TNRallyAnchorDetail::AnchorSizeCm / TNRallyAnchorDetail::BasicShapeSizeCm));
	UpdateVisual();
}

FVector ATN_RallyAnchorTether::GetHookLocation() const
{
	return Target ? Target->GetActorTransform().TransformPosition(LocalHook) : GetActorLocation();
}

void ATN_RallyAnchorTether::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Target)
	{
		if (HasAuthority())
		{
			Destroy();
		}
		return;
	}
	if (HasAuthority())
	{
		ApplyDrag();
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateVisual();
	}
}

void ATN_RallyAnchorTether::ApplyDrag()
{
	USkeletalMeshComponent* Chassis = Target->GetMesh();
	if (!Chassis || !Chassis->IsSimulatingPhysics())
	{
		return;
	}
	const FVector Accel = TNRallyCombat::AnchorDragAccel(Chassis->GetPhysicsLinearVelocity());
	if (!Accel.IsNearlyZero())
	{
		// Aceleración (bAccelChange): el freno no depende de la masa del buggy.
		Chassis->AddForce(Accel, NAME_None, true);
	}
}

void ATN_RallyAnchorTether::UpdateVisual()
{
	using namespace TNRallyAnchorDetail;
	const FVector Hook = GetHookLocation();
	if (!bAnchorPlaced)
	{
		AnchorLocation = Hook;
		bAnchorPlaced = true;
	}
	AnchorLocation = TNRallyCombat::DragAnchor(AnchorLocation, Hook, TNRallyCombat::AnchorRopeLengthCm);
	// El ancla va por el suelo bajo su posición (sin el buggy, que taparía el rayo).
	FHitResult Ground;
	FCollisionQueryParams Params(FName(TEXT("TNRallyAnchor")), false, this);
	Params.AddIgnoredActor(Target);
	const FVector Probe(AnchorLocation.X, AnchorLocation.Y, Hook.Z);
	if (GetWorld()->LineTraceSingleByChannel(Ground, Probe, Probe - FVector(0.f, 0.f, GroundProbeCm), ECC_WorldStatic, Params))
	{
		AnchorLocation.Z = Ground.ImpactPoint.Z + AnchorSizeCm * 0.5f;
	}
	AnchorMesh->SetWorldLocation(AnchorLocation);

	const FVector Span = Hook - AnchorLocation;
	const float Length = static_cast<float>(Span.Size());
	if (Length < 1.f)
	{
		RopeMesh->SetVisibility(false);
		return;
	}
	// El cilindro básico mide 100 cm a lo largo de su Z: se estira entre el ancla y el gancho.
	RopeMesh->SetVisibility(true);
	RopeMesh->SetWorldLocationAndRotation(AnchorLocation + Span * 0.5f, FRotationMatrix::MakeFromZ(Span / Length).Rotator());
	const float Thickness = RopeThicknessCm / BasicShapeSizeCm;
	RopeMesh->SetWorldScale3D(FVector(Thickness, Thickness, Length / BasicShapeSizeCm));
}
