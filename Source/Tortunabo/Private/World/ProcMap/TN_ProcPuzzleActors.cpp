#include "World/ProcMap/TN_ProcPuzzleActors.h"
#include "World/ProcMap/TN_ProcMapActorUtils.h"
#include "Art/TN_Art.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"

namespace
{
	constexpr float TNProcRampThickness = 30.f;
	constexpr float TNProcStowedPitch = -84.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Muro de lanzamiento
// ─────────────────────────────────────────────────────────────────────────────

ATN_ProcThrowWall::ATN_ProcThrowWall()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));

	Block = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Block"));
	Block->SetupAttachment(Root);
	Block->SetCollisionProfileName(TEXT("BlockAll"));
	if (Cube.Succeeded()) { Block->SetStaticMesh(Cube.Object); }

	RampHinge = CreateDefaultSubobject<USceneComponent>(TEXT("RampHinge"));
	RampHinge->SetupAttachment(Root);

	Ramp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ramp"));
	Ramp->SetupAttachment(RampHinge);
	Ramp->SetCollisionProfileName(TEXT("BlockAll"));
	if (Cube.Succeeded()) { Ramp->SetStaticMesh(Cube.Object); }
}

void ATN_ProcThrowWall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_ProcThrowWall, Dimensions);
	DOREPLIFETIME(ATN_ProcThrowWall, bRampDown);
}

void ATN_ProcThrowWall::BeginPlay()
{
	Super::BeginPlay();
	TNProcActors::Tint(Block, FLinearColor(0.34f, 0.3f, 0.26f));
	TNProcActors::Tint(Ramp, FLinearColor(0.5f, 0.33f, 0.16f));
	ApplyDimensions();
	RampAlpha = bRampDown ? 1.f : 0.f;
	// Mallas de arte (Docs/Arte_Assets.md): hijas del cubo de cada uno, con su escala (las medidas del muro y de la rampa).
	TNArt::ApplyToComponent(Block, TN_ART("ProcMap.Puzzle.ThrowWallBlock"));
	TNArt::ApplyToComponent(Ramp, TN_ART("ProcMap.Puzzle.ThrowWallRamp"));
}

void ATN_ProcThrowWall::Setup(float InWidth, float InHeight, float InLength)
{
	Dimensions = FVector(FMath::Max(300.f, InLength), FMath::Max(300.f, InWidth), FMath::Max(200.f, InHeight));
	ApplyDimensions();
}

void ATN_ProcThrowWall::OnRep_Dimensions()
{
	ApplyDimensions();
}

void ATN_ProcThrowWall::ApplyDimensions()
{
	const float L = Dimensions.X;
	const float W = Dimensions.Y;
	const float H = Dimensions.Z;
	Block->SetRelativeScale3D(FVector(L / 100.f, W / 100.f, H / 100.f));
	Block->SetRelativeLocation(FVector(0.f, 0.f, H * 0.5f));

	const float RampLen = FMath::Sqrt(H * H + RampRun * RampRun);
	RampHinge->SetRelativeLocation(FVector(-L * 0.5f, 0.f, H));
	Ramp->SetRelativeScale3D(FVector(RampLen / 100.f, (W * 0.45f) / 100.f, TNProcRampThickness / 100.f));
	Ramp->SetRelativeLocation(FVector(-RampLen * 0.5f, 0.f, -TNProcRampThickness * 0.5f));
}

FVector ATN_ProcThrowWall::GetSwitchLocation() const
{
	const FVector Local(Dimensions.X * 0.5f - 160.f, Dimensions.Y * 0.5f - 160.f, Dimensions.Z);
	return GetActorTransform().TransformPosition(Local);
}

void ATN_ProcThrowWall::LowerRamp(float Duration)
{
	if (!HasAuthority()) { return; }
	bRampDown = true;
	OnRep_RampDown();
	GetWorldTimerManager().SetTimer(RampTimer, this, &ATN_ProcThrowWall::RaiseRamp, Duration, false);
}

void ATN_ProcThrowWall::RaiseRamp()
{
	bRampDown = false;
	OnRep_RampDown();
}

void ATN_ProcThrowWall::OnRep_RampDown()
{
	if (RampSound) { UGameplayStatics::PlaySoundAtLocation(this, RampSound, GetActorLocation()); }
}

void ATN_ProcThrowWall::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Interpolación local de la rampa en todas las máquinas a partir del estado replicado.
	const float Target = bRampDown ? 1.f : 0.f;
	RampAlpha = FMath::FInterpConstantTo(RampAlpha, Target, DeltaTime, 1.5f);
	const float DownPitch = FMath::RadiansToDegrees(FMath::Atan2(Dimensions.Z, RampRun));
	RampHinge->SetRelativeRotation(FRotator(FMath::Lerp(TNProcStowedPitch, DownPitch, RampAlpha), 0.f, 0.f));
	// Recogida no debe estorbar al que vuela por encima; bajada es suelo firme.
	const ECollisionEnabled::Type Wanted = RampAlpha > 0.95f ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision;
	if (Ramp->GetCollisionEnabled() != Wanted) { Ramp->SetCollisionEnabled(Wanted); }
}

// ─────────────────────────────────────────────────────────────────────────────
// Compuerta de sabotaje
// ─────────────────────────────────────────────────────────────────────────────

ATN_ProcSabotageGate::ATN_ProcSabotageGate()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Gate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Gate"));
	Gate->SetupAttachment(Root);
	Gate->SetCollisionProfileName(TEXT("BlockAll"));
	if (Cube.Succeeded()) { Gate->SetStaticMesh(Cube.Object); }
}

void ATN_ProcSabotageGate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_ProcSabotageGate, Size);
	DOREPLIFETIME(ATN_ProcSabotageGate, bRaised);
}

void ATN_ProcSabotageGate::BeginPlay()
{
	Super::BeginPlay();
	TNProcActors::Tint(Gate, FLinearColor(0.7f, 0.15f, 0.1f));
	OnRep_Size();
	RaiseAlpha = bRaised ? 1.f : 0.f;
	// Malla de arte (Docs/Arte_Assets.md): hija del cubo, con su escala (las medidas de la compuerta); sube y baja con él.
	TNArt::ApplyToComponent(Gate, TN_ART("ProcMap.Puzzle.SabotageGate"));
}

void ATN_ProcSabotageGate::Setup(float InWidth, float InHeight)
{
	Size = FVector2D(FMath::Max(200.f, InWidth), FMath::Max(150.f, InHeight));
	OnRep_Size();
}

void ATN_ProcSabotageGate::OnRep_Size()
{
	Gate->SetRelativeScale3D(FVector(1.2f, Size.X / 100.f, Size.Y / 100.f));
}

void ATN_ProcSabotageGate::Raise(float Duration)
{
	if (!HasAuthority()) { return; }
	bRaised = true;
	OnRep_Raised();
	GetWorldTimerManager().SetTimer(GateTimer, this, &ATN_ProcSabotageGate::Lower, Duration, false);
}

void ATN_ProcSabotageGate::SetBlocking(bool bBlock)
{
	if (!HasAuthority()) { return; }
	GetWorldTimerManager().ClearTimer(GateTimer);
	bRaised = bBlock;
	OnRep_Raised();
}

void ATN_ProcSabotageGate::Lower()
{
	bRaised = false;
	OnRep_Raised();
}

void ATN_ProcSabotageGate::OnRep_Raised()
{
	if (RaiseSound) { UGameplayStatics::PlaySoundAtLocation(this, RaiseSound, GetActorLocation()); }
}

void ATN_ProcSabotageGate::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	RaiseAlpha = FMath::FInterpConstantTo(RaiseAlpha, bRaised ? 1.f : 0.f, DeltaTime, 2.5f);
	const float H = static_cast<float>(Size.Y);
	// Enterrada: la parte superior queda 20 cm bajo el suelo; levantada: apoyada en el suelo.
	Gate->SetRelativeLocation(FVector(0.f, 0.f, FMath::Lerp(-H * 0.5f - 20.f, H * 0.5f, RaiseAlpha)));
}

// ─────────────────────────────────────────────────────────────────────────────
// Interruptor
// ─────────────────────────────────────────────────────────────────────────────

ATN_ProcSwitch::ATN_ProcSwitch()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Mesh && Cylinder.Succeeded())
	{
		Mesh->SetStaticMesh(Cylinder.Object);
		Mesh->SetRelativeScale3D(FVector(0.6f, 0.6f, 0.4f));
	}
	PromptText = NSLOCTEXT("Tortunabo", "ProcSwitchPrompt", "Pulsar");
	CooldownSeconds = 1.f;
}

void ATN_ProcSwitch::BeginPlay()
{
	Super::BeginPlay();
	// Malla de arte (Docs/Arte_Assets.md): hija del cilindro, con su escala.
	TNArt::ApplyToComponent(Mesh, TN_ART("ProcMap.Puzzle.Switch"));
}

void ATN_ProcSwitch::SetTarget(AActor* InTarget, float InEffectSeconds)
{
	Target = InTarget;
	EffectSeconds = InEffectSeconds;
	// Anti-spam: no se puede volver a pulsar mientras dura el efecto.
	CooldownSeconds = InEffectSeconds + 1.f;
	TNProcActors::Tint(Mesh, Cast<ATN_ProcSabotageGate>(InTarget) ? FLinearColor(0.9f, 0.1f, 0.1f) : FLinearColor(0.1f, 0.8f, 0.2f));
}

void ATN_ProcSwitch::OnInteracted_Implementation(APawn* Interactor)
{
	Super::OnInteracted_Implementation(Interactor);
	if (!HasAuthority()) { return; }

	if (ATN_ProcThrowWall* Wall = Cast<ATN_ProcThrowWall>(Target.Get()))
	{
		Wall->LowerRamp(EffectSeconds);
	}
	else if (ATN_ProcSabotageGate* SabotageGate = Cast<ATN_ProcSabotageGate>(Target.Get()))
	{
		SabotageGate->Raise(EffectSeconds);
	}
	if (PressSound) { UGameplayStatics::PlaySoundAtLocation(this, PressSound, GetActorLocation()); }
}
