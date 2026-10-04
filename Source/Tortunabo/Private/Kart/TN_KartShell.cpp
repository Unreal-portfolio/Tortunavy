#include "Kart/TN_KartShell.h"

#include "../Rally/TN_RallyMeshUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kart/TN_KartItemComponent.h"
#include "Kart/TN_KartItems.h"
#include "Net/UnrealNetwork.h"
#include "Rally/TN_RallyLogic.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_RallyProjectile.h"

namespace TNKartShellDetail
{
	/** Tamaño de la concha (cm), altura sobre el suelo (cm) y giro cosmético (grados por segundo). */
	constexpr float SizeCm = 75.f;
	constexpr float HoverCm = 40.f;
	constexpr float SpinDegPerSecond = 540.f;
	/** La concha no toca a quien la lanza en este tiempo (s). */
	constexpr float ShooterGraceSeconds = 1.f;
	/** Rebotes en paredes antes de romperse; una pared es una superficie con la normal más tumbada que esto. */
	constexpr int32 MaxBounces = 3;
	constexpr float WallNormalZ = 0.6f;
	/** Sin suelo debajo, cae a esta velocidad (cm/s). */
	constexpr float FallCms = 900.f;
	const FLinearColor StraightColor(0.25f, 0.85f, 0.45f, 1.f);
	const FLinearColor HomingColor(0.95f, 0.25f, 0.2f, 1.f);
}

ATN_KartShell::ATN_KartShell()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(30.f);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ShellFinder(TNRallyMesh::AmmoShellPath);
	if (ShellFinder.Succeeded())
	{
		Mesh->SetStaticMesh(ShellFinder.Object);
		Mesh->SetRelativeTransform(TNRallyMesh::FitToBox(ShellFinder.Object, FVector::ZeroVector, FVector(TNKartShellDetail::SizeCm)));
	}
}

ATN_KartShell* ATN_KartShell::LaunchShell(UWorld* World, ATN_Buggy* Shooter, const FVector& Where, const FVector& Direction,
	ATN_Buggy* Target, bool bHoming)
{
	if (!World)
	{
		return nullptr;
	}
	const FVector Flat = FVector(Direction.X, Direction.Y, 0.f).GetSafeNormal();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATN_KartShell* Shell = World->SpawnActorDeferred<ATN_KartShell>(ATN_KartShell::StaticClass(),
		FTransform(Flat.Rotation(), Where), Shooter, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Shell)
	{
		return nullptr;
	}
	Shell->Shooter = Shooter;
	Shell->Target = Target;
	Shell->bHoming = bHoming;
	Shell->Direction = Flat.IsNearlyZero() ? FVector::ForwardVector : Flat;
	Shell->FinishSpawning(FTransform(Shell->Direction.Rotation(), Where));
	return Shell;
}

void ATN_KartShell::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ATN_KartShell, bHoming, COND_InitialOnly);
}

void ATN_KartShell::BeginPlay()
{
	Super::BeginPlay();
	OnRep_Homing();
	if (HasAuthority())
	{
		SetLifeSpan(bHoming ? TNKart::HomingShellLifeSeconds : TNKart::ShellLifeSeconds);
	}
}

void ATN_KartShell::OnRep_Homing()
{
	TNRallyLook::Tint(Mesh, bHoming ? TNKartShellDetail::HomingColor : TNKartShellDetail::StraightColor);
}

void ATN_KartShell::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() != NM_DedicatedServer)
	{
		Mesh->AddLocalRotation(FRotator(0.f, TNKartShellDetail::SpinDegPerSecond * DeltaSeconds, 0.f));
	}
	if (HasAuthority() && !StepServer(DeltaSeconds))
	{
		Burst();
		Destroy();
	}
}

bool ATN_KartShell::StepServer(float DeltaSeconds)
{
	using namespace TNKartShellDetail;
	UWorld* World = GetWorld();
	Age += DeltaSeconds;
	const FVector Location = GetActorLocation();
	if (bHoming)
	{
		if (const ATN_Buggy* Chased = Target.Get())
		{
			Direction = TNKart::SteerShell(Direction, Chased->GetActorLocation() - Location, TNKart::ShellTurnDegPerSecond * DeltaSeconds);
		}
	}
	FVector Next = Location + Direction * (TNKart::ShellSpeedCms * DeltaSeconds);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNKartShell), false, this);
	for (TActorIterator<ATN_Buggy> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	// Pared por delante: rebota (en el plano).
	FHitResult Wall;
	if (World->LineTraceSingleByChannel(Wall, Location, Next + Direction * 60.f, ECC_WorldStatic, Params)
		&& Wall.ImpactNormal.Z < WallNormalZ)
	{
		if (++Bounces > MaxBounces)
		{
			return false;
		}
		const FVector Normal = FVector(Wall.ImpactNormal.X, Wall.ImpactNormal.Y, 0.f).GetSafeNormal();
		Direction = (Direction - 2.f * FVector::DotProduct(Direction, Normal) * Normal).GetSafeNormal2D();
		Next = Location + Direction * (TNKart::ShellSpeedCms * DeltaSeconds);
	}
	// Pegada al suelo.
	FHitResult Down;
	if (World->LineTraceSingleByChannel(Down, Next + FVector(0.f, 0.f, 250.f), Next - FVector(0.f, 0.f, 800.f), ECC_WorldStatic, Params))
	{
		Next.Z = Down.ImpactPoint.Z + HoverCm;
	}
	else
	{
		Next.Z = Location.Z - FallCms * DeltaSeconds;
	}
	SetActorLocationAndRotation(Next, Direction.Rotation());

	// El primer kart que toca (menos quien la lanza al salir).
	for (TActorIterator<ATN_Buggy> It(World); It; ++It)
	{
		ATN_Buggy* Kart = *It;
		if ((Kart == Shooter.Get() && Age < ShooterGraceSeconds)
			|| FVector::DistSquared(Kart->GetActorLocation(), Next) > FMath::Square(TNKart::ShellHitRadiusCm))
		{
			continue;
		}
		UTN_KartItemComponent::SpinOut(*Kart, Direction);
		UE_LOG(LogTNRally, Verbose, TEXT("[KartItems] La concha de %s da a %s."), *GetNameSafe(Shooter.Get()), *Kart->GetName());
		return false;
	}
	return true;
}

void ATN_KartShell::Burst()
{
	if (ATN_Buggy* Via = Shooter.Get())
	{
		ATN_RallyBurstFX::Broadcast(Via, ETNRallyBurstKind::BubblePop, GetActorLocation(), 160.f);
	}
}
