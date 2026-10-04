#include "Rally/TN_RallyGate.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "TN_RallyMeshUtils.h"
#include "UObject/ConstructorHelpers.h"

ATN_RallyGate::ATN_RallyGate()
{
	PrimaryActorTick.bCanEverTick = false;
	// Cada máquina construye su pista con los mismos datos del manifest: la puerta no se replica.
	bReplicates = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	Volume->SetupAttachment(Root);
	Volume->SetBoxExtent(CrossingHalfExtent());
	Volume->SetRelativeLocation(CrossingCenterOffset());
	Volume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Volume->SetHiddenInGame(true);

	auto MakeArchPiece = [this, Root](const TCHAR* Name)
	{
		UStaticMeshComponent* Piece = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Piece->SetupAttachment(Root);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetCanEverAffectNavigation(false);
		return Piece;
	};
	LeftPost = MakeArchPiece(TEXT("LeftPost"));
	RightPost = MakeArchPiece(TEXT("RightPost"));
	Beam = MakeArchPiece(TEXT("Beam"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PostFinder(TNRallyMesh::PostPath);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BeamFinder(TNRallyMesh::BeamPath);
	PostMesh = PostFinder.Object;
	BeamMesh = BeamFinder.Object;
}

void ATN_RallyGate::Configure(int32 InGateIndex, bool bInFinish)
{
	GateIndex = InGateIndex;
	bFinish = bInFinish;
	LayoutArch();
}

void ATN_RallyGate::LayoutArch()
{
	// El arco va justo fuera del volumen para que los postes no estorben en la calzada; la meta, más alta y gruesa.
	const double Height = bFinish ? HeightCm * 1.2 : HeightCm * 0.9;
	const double PostSize = bFinish ? 90.0 : 60.0;
	const double HalfSpan = WidthCm * 0.5 + PostSize;
	LeftPost->SetStaticMesh(PostMesh);
	RightPost->SetStaticMesh(PostMesh);
	Beam->SetStaticMesh(BeamMesh);
	// Cada pata baja hasta el suelo que tiene debajo (peralte o ladera: una flotaba, #665); el travesaño no se mueve.
	const double LeftDrop = MeasureFootDrop(FVector(0.0, -HalfSpan, 0.0));
	const double RightDrop = MeasureFootDrop(FVector(0.0, HalfSpan, 0.0));
	LeftPost->SetRelativeTransform(TNRallyMesh::FitToBox(PostMesh, FVector(0.0, -HalfSpan, (Height - LeftDrop) * 0.5),
		FVector(PostSize, PostSize, Height + LeftDrop)));
	RightPost->SetRelativeTransform(TNRallyMesh::FitToBox(PostMesh, FVector(0.0, HalfSpan, (Height - RightDrop) * 0.5),
		FVector(PostSize, PostSize, Height + RightDrop)));
	Beam->SetRelativeTransform(TNRallyMesh::FitToBox(BeamMesh, FVector(0.0, 0.0, Height), FVector(PostSize, 2.0 * HalfSpan + PostSize, bFinish ? 150.0 : 80.0)));
}

double ATN_RallyGate::MeasureFootDrop(const FVector& LocalFoot) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	// Traza vertical desde un poco por encima del pie: lo que haya entre el pie y el suelo, a lo largo del eje de la pata.
	const FVector Foot = GetActorTransform().TransformPosition(LocalFoot);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNRallyGateFoot), false, this);
	if (AActor* TrackOwner = GetOwner())
	{
		Params.AddIgnoredActor(TrackOwner);
	}
	FHitResult Hit;
	if (!World->LineTraceSingleByObjectType(Hit, Foot + FVector(0.0, 0.0, FootProbeUpCm), Foot - FVector(0.0, 0.0, FootProbeDownCm),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		return 0.0;
	}
	const double UpZ = FMath::Max(0.2, static_cast<double>(GetActorUpVector().Z));
	return TNRallyGateFit::FootDropAlongPost(Foot.Z, Hit.ImpactPoint.Z, UpZ, FootProbeDownCm);
}

FTransform ATN_RallyGate::GetCrossingTransform() const
{
	return FTransform(GetActorQuat(), GetActorLocation() + GetActorQuat().RotateVector(CrossingCenterOffset()));
}

FVector ATN_RallyGate::GetHalfExtent() const
{
	return CrossingHalfExtent();
}
