#include "Rally/TN_RallyPodium.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Rally/TN_RallyLogic.h"
#include "TN_RallyMeshUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicles/TN_RallyProjectile.h"
#include "../UI/HUD/TN_HUDArt.h"

namespace TNRallyPodiumDetail
{
	const TCHAR* const ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	/** La losa sobresale por detrás de los escalones para la fila del 4.º en adelante (cm, a lo largo de Facing). */
	constexpr double SlabCenterBackCm = 300.0;
	/** Holgura sobre el escalón al aparcar (cm): el buggy cae un palmo y se asienta. */
	constexpr double ParkLiftCm = 20.0;

	UStaticMeshComponent* MakeBlock(AActor* Owner, const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Block = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Block->SetStaticMesh(Mesh);
		if (Material)
		{
			Block->SetMaterial(0, Material);
		}
		Block->SetCollisionProfileName(TEXT("BlockAll"));
		Block->SetGenerateOverlapEvents(false);
		Block->SetMobility(EComponentMobility::Movable);
		return Block;
	}

	FLinearColor StepColor(int32 FinishOrder)
	{
		switch (FinishOrder)
		{
		case 1: return TNHUDArt::Gold;
		case 2: return FLinearColor(0.75f, 0.78f, 0.82f);
		default: return FLinearColor(0.72f, 0.45f, 0.22f);
		}
	}
}

ATN_RallyPodium::ATN_RallyPodium()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	// Flota sobre la meta: lo ven también los espectadores lejos de ella.
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TNRallyMesh::BeamPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TNRallyPodiumDetail::ShapeMaterialPath);
	const FVector& SlabSize = TNRallyCamera::PodiumSlabSizeCm;
	Slab = TNRallyPodiumDetail::MakeBlock(this, TEXT("Slab"), CubeMesh.Object, ShapeMaterial.Object);
	Slab->SetupAttachment(Root);
	Slab->SetRelativeTransform(TNRallyMesh::FitToBox(CubeMesh.Object,
		FVector(-TNRallyPodiumDetail::SlabCenterBackCm, 0.0, -0.5 * SlabSize.Z), SlabSize));

	const TCHAR* const StepNames[3] = { TEXT("Step1"), TEXT("Step2"), TEXT("Step3") };
	for (int32 Order = 1; Order <= 3; ++Order)
	{
		UStaticMeshComponent* Step = TNRallyPodiumDetail::MakeBlock(this, StepNames[Order - 1], CubeMesh.Object, ShapeMaterial.Object);
		Step->SetupAttachment(Root);
		const double Height = TNRallyCamera::PodiumStepHeightCm(Order);
		const double Side = Order == 1 ? 0.0 : (Order == 2 ? 1.0 : -1.0);
		Step->SetRelativeTransform(TNRallyMesh::FitToBox(CubeMesh.Object, FVector(0.0, Side * TNRallyCamera::PodiumStepWidthCm, 0.5 * Height),
			FVector(TNRallyCamera::PodiumStepDepthCm, TNRallyCamera::PodiumStepWidthCm - 20.0, Height)));
		Steps.Add(Step);
	}
}

void ATN_RallyPodium::BeginPlay()
{
	Super::BeginPlay();
	// Cosmético en cada máquina: arena clara la losa y oro, plata y bronce los escalones.
	TNRallyLook::Tint(Slab, TNHUDArt::SandLight);
	for (int32 Index = 0; Index < Steps.Num(); ++Index)
	{
		TNRallyLook::Tint(Steps[Index], TNRallyPodiumDetail::StepColor(Index + 1));
	}
}

ATN_RallyPodium* ATN_RallyPodium::SpawnAtFinish(UWorld* World, const FVector& FinishCenter, const FVector& RaceForward)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	const TNRallyCamera::FPodiumFrame Frame = TNRallyCamera::PodiumFrameFromFinish(FinishCenter, RaceForward);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATN_RallyPodium* Podium = World->SpawnActor<ATN_RallyPodium>(ATN_RallyPodium::StaticClass(), Frame.Origin, Frame.Facing.Rotation(), Params);
	UE_CLOG(Podium != nullptr, LogTNRally, Log, TEXT("[RallyPodium] Podio sobre la meta en %s."), *Frame.Origin.ToCompactString());
	return Podium;
}

ATN_RallyPodium* ATN_RallyPodium::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ATN_RallyPodium> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

TNRallyCamera::FPodiumFrame ATN_RallyPodium::GetFrame() const
{
	TNRallyCamera::FPodiumFrame Frame;
	Frame.Origin = GetActorLocation();
	Frame.Facing = GetActorForwardVector().GetSafeNormal2D();
	Frame.Right = FVector::CrossProduct(FVector::UpVector, Frame.Facing);
	return Frame;
}

FTransform ATN_RallyPodium::GetSlotTransform(int32 FinishOrder, double OriginAboveBottomCm) const
{
	const TNRallyCamera::FPodiumFrame Frame = GetFrame();
	const FVector Location = TNRallyCamera::PodiumSlotLocation(Frame, FinishOrder)
		+ FVector::UpVector * (OriginAboveBottomCm + TNRallyPodiumDetail::ParkLiftCm);
	return FTransform(Frame.Facing.Rotation(), Location);
}
