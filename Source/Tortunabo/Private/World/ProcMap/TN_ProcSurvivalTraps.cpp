#include "World/ProcMap/TN_ProcSurvivalTraps.h"
#include "World/ProcMap/TN_ProcMapActorUtils.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

// ─────────────────────────────────────────────────────────────────────────────
// Puente que se rompe
// ─────────────────────────────────────────────────────────────────────────────

ATN_ProcBreakableBridge::ATN_ProcBreakableBridge()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) { PlatformMesh->SetStaticMesh(Cube.Object); }
	// El trigger va con su propia escala: la del actor es la del tablón.
	StandTrigger->SetUsingAbsoluteScale(true);

	// Cruzarlo corriendo (4 m/s) aguanta; andando (2 m/s) no llega al otro labio. Reaparece para los que vienen detrás.
	PlayerThreshold = 1;
	BreakMode = EBreakablePlatformMode::Reversible;
	TimeToBreak = 1.6f;
	ShakeDuration = 0.8f;
	ShakeAmplitude = 4.f;
	RespawnTime = 5.f;
}

void ATN_ProcBreakableBridge::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_ProcBreakableBridge, BoardSize);
}

void ATN_ProcBreakableBridge::BeginPlay()
{
	Super::BeginPlay();
	OnRep_BoardSize();
	TNProcActors::Tint(PlatformMesh, FLinearColor(0.42f, 0.27f, 0.13f));
}

void ATN_ProcBreakableBridge::SetBoardSize(const FVector& InSize)
{
	BoardSize = FVector(FMath::Max(100.0, InSize.X), FMath::Max(60.0, InSize.Y), FMath::Max(5.0, InSize.Z));
	OnRep_BoardSize();
}

void ATN_ProcBreakableBridge::OnRep_BoardSize()
{
	// El cubo del motor mide 100 cm y tiene el pivote en el centro.
	const FVector Scale = BoardSize / 100.0;
	SetActorScale3D(Scale);
	// Trigger fino sobre la cara de arriba (en unidades del cubo, porque la posición sí escala con el padre).
	StandTrigger->SetBoxExtent(FVector(BoardSize.X * 0.5, BoardSize.Y * 0.5, 15.0));
	StandTrigger->SetRelativeLocation(FVector(0.0, 0.0, 50.0 + 15.0 / Scale.Z));
}
