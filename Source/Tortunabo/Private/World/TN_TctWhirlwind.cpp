#include "World/TN_TctWhirlwind.h"
#include "../Game/TN_TctItemMeshes.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/TN_TctItemRules.h"
#include "Game/TN_TctItems.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "Player/TortugaCharacter.h"

namespace TNTctWhirlwindDetail
{
	/** Aparece, gira y se recoge al final (s). */
	constexpr float GrowSeconds = 0.25f;
	constexpr float ShrinkSeconds = 0.6f;
	/** Vueltas por segundo del torbellino. */
	constexpr float TurnsPerSecond = 1.6f;
	/** Hasta dónde busca suelo bajo el punto donde se planta (uu). */
	constexpr double GroundProbe = 500.0;
	/** La malla del embudo mide 250 uu de alto y 135 de radio en el borde de arriba: a escala del radio del remolino. */
	constexpr float MeshRadius = 135.f;
}

ATN_TctWhirlwind::ATN_TctWhirlwind()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetCullDistanceSquared(FMath::Square(25000.f));

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);
	Mesh->SetMobility(EComponentMobility::Movable);
}

ATN_TctWhirlwind* ATN_TctWhirlwind::ServerPlant(ATortugaCharacter* Planter)
{
	UWorld* World = Planter ? Planter->GetWorld() : nullptr;
	if (!World || !Planter->HasAuthority())
	{
		return nullptr;
	}
	const FVector Forward = FVector(Planter->GetActorForwardVector().X, Planter->GetActorForwardVector().Y, 0.0).GetSafeNormal();
	const FVector Top = Planter->GetActorLocation() + Forward * TNTctItemTuning::WhirlForward + FVector(0.0, 0.0, 60.0);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TctWhirlwind), false, Planter);
	if (!World->LineTraceSingleByObjectType(Hit, Top, Top - FVector(0.0, 0.0, TNTctWhirlwindDetail::GroundProbe),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		return nullptr;
	}
	FActorSpawnParameters Spawn;
	Spawn.Instigator = Planter;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATN_TctWhirlwind* Whirl = World->SpawnActor<ATN_TctWhirlwind>(ATN_TctWhirlwind::StaticClass(),
		FTransform(FRotator(0.0, Planter->GetActorRotation().Yaw, 0.0), Hit.ImpactPoint), Spawn);
	if (Whirl)
	{
		Whirl->SetLifeSpan(TNTctItemTuning::WhirlLifeSeconds);
		TNTctItems::PlayCue(Planter, ETNRaceSound::Flap, 0.7f);
		UE_LOG(LogTortunabo, Log, TEXT("[TcT] %s planta un remolino de arena."), *GetNameSafe(Planter));
	}
	return Whirl;
}

void ATN_TctWhirlwind::BeginPlay()
{
	Super::BeginPlay();
	if (UStaticMesh* Look = TNTctItemMeshes::Funnel())
	{
		Mesh->SetStaticMesh(Look);
	}
	Mesh->SetRelativeScale3D(FVector(0.05));
}

int32 ATN_TctWhirlwind::ServerKickTurtles()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return 0;
	}
	const FVector Base = GetActorLocation();
	int32 Kicked = 0;
	for (TActorIterator<ATortugaCharacter> It(World); It; ++It)
	{
		ATortugaCharacter* Turtle = *It;
		if (!Turtle || !TNTctItems::CanAffect(Turtle, true))
		{
			continue;
		}
		const float* Until = RearmUntil.Find(Turtle);
		const FVector Feet = Turtle->GetActorLocation() - FVector(0.0, 0.0, Turtle->GetSimpleCollisionHalfHeight());
		FVector Launch;
		if ((Until && Age < *Until) || !TNTctItemRules::WhirlKick(Base, Feet, Launch))
		{
			continue;
		}
		UTN_TurtleMovementComponent::LaunchFromServer(Turtle, Launch);
		TNTctItems::PlayCue(Turtle, ETNRaceSound::Flap, 0.9f);
		RearmUntil.Add(Turtle, Age + TNTctItemTuning::WhirlKickSeconds);
		++Kicked;
	}
	return Kicked;
}

void ATN_TctWhirlwind::Tick(float DeltaSeconds)
{
	using namespace TNTctWhirlwindDetail;
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (HasAuthority())
	{
		ServerKickTurtles();
	}
	// Aparece, gira y se recoge al final (cada máquina con su reloj: es solo la forma).
	const float Left = HasAuthority() ? GetLifeSpan() : TNTctItemTuning::WhirlLifeSeconds - Age;
	const float Grow = FMath::Clamp(Age / GrowSeconds, 0.f, 1.f);
	const float Shrink = Left > 0.f ? FMath::Clamp(Left / ShrinkSeconds, 0.f, 1.f) : 1.f;
	const float Size = TNTctItemTuning::WhirlRadius / MeshRadius * FMath::Max(0.05f, FMath::Min(Grow, Shrink));
	Mesh->SetRelativeScale3D(FVector(Size));
	Mesh->SetRelativeRotation(FRotator(0.0, 360.0 * TurnsPerSecond * Age, 0.0));
}
