#include "Vehicles/TN_RallyHarpoon.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
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

namespace TNRallyHarpoonDetail
{
	const TCHAR* const ConeMeshPath = TEXT("/Engine/BasicShapes/Cone.Cone");
	const TCHAR* const CylinderMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* const ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	/** Las mallas básicas miden 100 cm. */
	constexpr float BasicShapeSizeCm = 100.f;
	constexpr float RopeThicknessCm = 5.f;
	constexpr float TipLengthCm = 40.f;
	constexpr float TipWidthCm = 16.f;
	const FLinearColor RopeColor(0.85f, 0.80f, 0.65f);

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

ATN_RallyHarpoonTether::ATN_RallyHarpoonTether()
{
	using namespace TNRallyHarpoonDetail;
	PrimaryActorTick.bCanEverTick = true;
	// Después de la física: la cuerda sigue a los dos buggies en su posición del frame.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(5.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(ConeMeshPath);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(CylinderMeshPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(ShapeMaterialPath);
	TipMesh = MakeShape(this, TEXT("TipMesh"), ConeMesh.Object, ShapeMaterial.Object);
	TipMesh->SetupAttachment(Root);
	RopeMesh = MakeShape(this, TEXT("RopeMesh"), CylinderMesh.Object, ShapeMaterial.Object);
	RopeMesh->SetupAttachment(Root);
}

void ATN_RallyHarpoonTether::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ATN_RallyHarpoonTether, Puller, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ATN_RallyHarpoonTether, Target, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ATN_RallyHarpoonTether, LocalHook, COND_InitialOnly);
}

ATN_RallyHarpoonTether* ATN_RallyHarpoonTether::Attach(ATN_Buggy* InPuller, ATN_Buggy* InTarget, const FVector& WorldHook)
{
	UWorld* World = InTarget ? InTarget->GetWorld() : nullptr;
	if (!World || !InPuller || InPuller == InTarget || !InTarget->HasAuthority())
	{
		return nullptr;
	}
	// Un solo arpón por buggy que tira: el nuevo sustituye al anterior (no se suma el remolque).
	for (TActorIterator<ATN_RallyHarpoonTether> It(World); It; ++It)
	{
		if (It->Puller == InPuller)
		{
			It->Destroy();
		}
	}
	const FTransform Where(WorldHook);
	ATN_RallyHarpoonTether* Tether = World->SpawnActorDeferred<ATN_RallyHarpoonTether>(ATN_RallyHarpoonTether::StaticClass(), Where,
		InPuller, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Tether)
	{
		return nullptr;
	}
	Tether->Puller = InPuller;
	Tether->Target = InTarget;
	Tether->LocalHook = InTarget->GetActorTransform().InverseTransformPosition(WorldHook);
	Tether->FinishSpawning(Where);
	UE_LOG(LogTNBuggy, Verbose, TEXT("%s: arpón clavado en %s, remolca %.0f s"), *InPuller->GetName(), *InTarget->GetName(),
		TNRallyTurret::HarpoonSeconds);
	return Tether;
}

void ATN_RallyHarpoonTether::BeginPlay()
{
	Super::BeginPlay();
	SetLifeSpan(TNRallyTurret::HarpoonSeconds);
	TNRallyLook::Tint(TipMesh, TNRallyLook::AmmoColor(ETNRallyAmmo::Arpon));
	TNRallyLook::Tint(RopeMesh, TNRallyHarpoonDetail::RopeColor);
	UpdateVisual();
}

FVector ATN_RallyHarpoonTether::GetHookLocation() const
{
	return IsValid(Target) ? Target->GetActorTransform().TransformPosition(LocalHook) : GetActorLocation();
}

FVector ATN_RallyHarpoonTether::GetRopeStart() const
{
	if (!IsValid(Puller))
	{
		return GetActorLocation();
	}
	const UTN_BuggyTurretComponent* Turret = Puller->GetTurret();
	return Turret ? Turret->GetComponentLocation() : Puller->GetActorLocation();
}

void ATN_RallyHarpoonTether::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsValid(Puller) || !IsValid(Target))
	{
		if (HasAuthority())
		{
			Destroy();
		}
		return;
	}
	if (HasAuthority())
	{
		// La reaparición teletransporta el mismo actor: sin esto, la cuerda cruzaba el mapa y tiraba hacia el punto de salida.
		const float Distance = static_cast<float>(FVector::Dist(Puller->GetActorLocation(), Target->GetActorLocation()));
		if (!TNRallyTurret::HarpoonHolds(Distance, Puller->IsRespawnProtected(), Target->IsRespawnProtected()))
		{
			UE_LOG(LogTNBuggy, Verbose, TEXT("%s: arpón suelto (%.0f cm, reaparición o demasiado lejos)"), *Puller->GetName(), Distance);
			Destroy();
			return;
		}
		ApplyPull(DeltaSeconds);
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateVisual();
	}
}

void ATN_RallyHarpoonTether::ApplyPull(float DeltaSeconds)
{
	USkeletalMeshComponent* Chassis = Puller->GetMesh();
	if (!Chassis || !Chassis->IsSimulatingPhysics() || Puller->IsEngineLocked())
	{
		return;
	}
	const FVector Accel = TNRallyTurret::HarpoonPullAccel(Chassis->GetPhysicsLinearVelocity(),
		Target->GetActorLocation() - Puller->GetActorLocation(), DeltaSeconds);
	if (!Accel.IsNearlyZero())
	{
		// Aceleración (bAccelChange) en el centro de masas: el remolque no depende de la masa ni hace volcar.
		Chassis->AddForce(Accel, NAME_None, true);
	}
}

void ATN_RallyHarpoonTether::UpdateVisual()
{
	using namespace TNRallyHarpoonDetail;
	const FVector Hook = GetHookLocation();
	const FVector Start = GetRopeStart();
	const FVector Span = Hook - Start;
	const float Length = static_cast<float>(Span.Size());
	if (Length < 1.f)
	{
		RopeMesh->SetVisibility(false);
		TipMesh->SetVisibility(false);
		return;
	}
	const FVector Dir = Span / Length;
	// El cilindro y el cono básicos miden 100 cm a lo largo de su Z: la cuerda se estira entre la torreta y el gancho, y la
	// punta queda clavada con el pico hacia dentro.
	const FRotator Along = FRotationMatrix::MakeFromZ(Dir).Rotator();
	RopeMesh->SetVisibility(true);
	RopeMesh->SetWorldLocationAndRotation(Start + Span * 0.5f, Along);
	const float Thickness = RopeThicknessCm / BasicShapeSizeCm;
	RopeMesh->SetWorldScale3D(FVector(Thickness, Thickness, Length / BasicShapeSizeCm));
	TipMesh->SetVisibility(true);
	TipMesh->SetWorldLocationAndRotation(Hook, Along);
	TipMesh->SetWorldScale3D(FVector(TipWidthCm, TipWidthCm, TipLengthCm) / BasicShapeSizeCm);
}
