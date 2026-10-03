#include "Rally/TN_RallyGate.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
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
	LeftPost->SetRelativeTransform(TNRallyMesh::FitToBox(PostMesh, FVector(0.0, -HalfSpan, Height * 0.5), FVector(PostSize, PostSize, Height)));
	RightPost->SetRelativeTransform(TNRallyMesh::FitToBox(PostMesh, FVector(0.0, HalfSpan, Height * 0.5), FVector(PostSize, PostSize, Height)));
	Beam->SetRelativeTransform(TNRallyMesh::FitToBox(BeamMesh, FVector(0.0, 0.0, Height), FVector(PostSize, 2.0 * HalfSpan + PostSize, bFinish ? 150.0 : 80.0)));
}

FTransform ATN_RallyGate::GetCrossingTransform() const
{
	return FTransform(GetActorQuat(), GetActorLocation() + GetActorQuat().RotateVector(CrossingCenterOffset()));
}

FVector ATN_RallyGate::GetHalfExtent() const
{
	return CrossingHalfExtent();
}
