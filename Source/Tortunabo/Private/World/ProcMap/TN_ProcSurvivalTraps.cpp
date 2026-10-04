#include "World/ProcMap/TN_ProcSurvivalTraps.h"
#include "World/ProcMap/TN_ProcMapActorUtils.h"
#include "World/ProcMap/TN_ProcPuzzleActors.h"
#include "World/TN_PressurePlate.h"
#include "World/TN_QuadActor.h"
#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
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

// ─────────────────────────────────────────────────────────────────────────────
// Cruce de quads
// ─────────────────────────────────────────────────────────────────────────────

ATN_ProcQuadCrossing::ATN_ProcQuadCrossing()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Las franjas: un decal que proyecta hacia abajo (su X apunta al suelo); tras girarlo, su Z es la dirección del cruce.
	Stripes = CreateDefaultSubobject<UDecalComponent>(TEXT("Stripes"));
	Stripes->SetupAttachment(Root);
	Stripes->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
}

void ATN_ProcQuadCrossing::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_ProcQuadCrossing, Shape);
	DOREPLIFETIME(ATN_ProcQuadCrossing, FirstPassServerTime);
}

void ATN_ProcQuadCrossing::BeginPlay()
{
	Super::BeginPlay();
	// Sin el material (Scripts/create_survival_decals.py) no hay franjas, pero el quad cruza igual.
	if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_QuadCrossingDecal.M_QuadCrossingDecal"), nullptr, LOAD_NoWarn))
	{
		Stripes->SetDecalMaterial(Material);
		StripesMaterial = Stripes->CreateDynamicMaterialInstance();
	}
	OnRep_Shape();
}

void ATN_ProcQuadCrossing::Setup(float InHalfSpan, float InPathHalfWidth, float FirstDelay, TSubclassOf<ATN_QuadActor> InQuadClass)
{
	Shape = FVector2D(FMath::Max(500.f, InHalfSpan), FMath::Max(150.f, InPathHalfWidth));
	OnRep_Shape();
	QuadClass = InQuadClass;
	FirstPassServerTime = ServerTime() + FirstDelay;
	if (HasAuthority() && QuadClass)
	{
		// El quad sale con tiempo de llegar al centro justo al acabar el aviso.
		const float Lead = Shape.X / QuadSpeed;
		GetWorldTimerManager().SetTimer(PassTimer, this, &ATN_ProcQuadCrossing::SpawnQuad, Interval, true, FMath::Max(0.1f, FirstDelay - Lead));
	}
}

void ATN_ProcQuadCrossing::OnRep_Shape()
{
	// Profundidad para llegar al suelo aunque el camino tenga algo de pendiente; ancho, el del camino con un margen.
	Stripes->DecalSize = FVector(400.f, StripeHalfDepth, Shape.Y + 50.f);
	Stripes->MarkRenderStateDirty();
}

float ATN_ProcQuadCrossing::ServerTime() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	return GS ? static_cast<float>(GS->GetServerWorldTimeSeconds()) : (World ? World->GetTimeSeconds() : 0.f);
}

void ATN_ProcQuadCrossing::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!StripesMaterial) { return; }
	// Warn: 0 en reposo; en los WarnSeconds antes de que el quad pase por el centro, parpadeo cada vez más rápido.
	const float T = ServerTime() - FirstPassServerTime;
	float Warn = 0.f;
	const float ToNext = T < 0.f ? -T : Interval - FMath::Fmod(T, Interval);
	if (ToNext <= WarnSeconds)
	{
		const float Urgency = 1.f - ToNext / WarnSeconds;
		Warn = 0.5f + 0.5f * FMath::Sin(T * (8.f + 10.f * Urgency));
	}
	StripesMaterial->SetScalarParameterValue(TEXT("Warn"), Warn);
}

void ATN_ProcQuadCrossing::SpawnQuad()
{
	UWorld* World = GetWorld();
	if (!World || !QuadClass) { return; }
	// Alterna el lado de salida. El cruce va en la X del actor.
	const FVector Across = GetActorForwardVector();
	const float Sign = bFromLeft ? -1.f : 1.f;
	bFromLeft = !bFromLeft;
	const FVector Start = GetActorLocation() + Across * (Sign * Shape.X);
	const FVector End = GetActorLocation() - Across * (Sign * Shape.X);
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ATN_QuadActor* Quad = World->SpawnActor<ATN_QuadActor>(QuadClass, Start, (End - Start).Rotation(), Params))
	{
		Quad->InitializeTravel(End, QuadSpeed);
	}
}

void ATN_ProcQuadCrossing::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(PassTimer);
	// Los quads en marcha se van con el mapa.
	UWorld* World = GetWorld();
	if (World && HasAuthority())
	{
		for (TActorIterator<ATN_QuadActor> It(World); It; ++It)
		{
			if (It->GetOwner() == this) { It->Destroy(); }
		}
	}
	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cerrojo del atajo
// ─────────────────────────────────────────────────────────────────────────────

ATN_ProcShortcutLock::ATN_ProcShortcutLock()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
}

void ATN_ProcShortcutLock::Setup(const TArray<ATN_PressurePlate*>& InPlates, ATN_ProcSabotageGate* InGate)
{
	Gate = InGate;
	if (InGate) { InGate->SetBlocking(true); }
	for (ATN_PressurePlate* Plate : InPlates)
	{
		if (!Plate) { continue; }
		Plates.Add(Plate);
		Plate->OnOccupancyChanged.AddUObject(this, &ATN_ProcShortcutLock::OnPlateChanged);
	}
}

void ATN_ProcShortcutLock::OnPlateChanged(ATN_PressurePlate* /*Plate*/, bool /*bOccupied*/)
{
	if (bOpened) { return; }
	for (const TWeakObjectPtr<ATN_PressurePlate>& Plate : Plates)
	{
		if (!Plate.IsValid() || !Plate->IsOccupied()) { return; }
	}
	bOpened = true;
	if (ATN_ProcSabotageGate* G = Gate.Get()) { G->SetBlocking(false); }
}
