#include "World/TN_TctJellyPad.h"
#include "../Game/TN_TctItemMeshes.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/TN_TctItemRules.h"
#include "Game/TN_TctItems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "Player/TortugaCharacter.h"

namespace TNTctJellyPadDetail
{
	/** Aparece, se aplasta al botar y se recoge al final (s). */
	constexpr float GrowSeconds = 0.2f;
	constexpr float SquashSeconds = 0.35f;
	constexpr float ShrinkSeconds = 0.5f;
	/** Hasta dónde busca suelo bajo el punto donde se planta (uu). */
	constexpr double GroundProbe = 500.0;
	/** Lo que conserva del avance quien bota (para que no se quede clavada en vertical). */
	constexpr double KeepFlat = 0.5;
}

ATN_TctJellyPad::ATN_TctJellyPad()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetCullDistanceSquared(FMath::Square(25000.f));

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetMobility(EComponentMobility::Movable);
}

ATN_TctJellyPad* ATN_TctJellyPad::ServerPlant(ATortugaCharacter* Planter)
{
	UWorld* World = Planter ? Planter->GetWorld() : nullptr;
	if (!World || !Planter->HasAuthority())
	{
		return nullptr;
	}
	const FVector Forward = FVector(Planter->GetActorForwardVector().X, Planter->GetActorForwardVector().Y, 0.0).GetSafeNormal();
	const FVector Top = Planter->GetActorLocation() + Forward * TNTctItemTuning::JellyForward + FVector(0.0, 0.0, 60.0);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TctJellyPad), false, Planter);
	if (!World->LineTraceSingleByObjectType(Hit, Top, Top - FVector(0.0, 0.0, TNTctJellyPadDetail::GroundProbe),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		return nullptr;
	}
	FActorSpawnParameters Spawn;
	Spawn.Instigator = Planter;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATN_TctJellyPad* Jelly = World->SpawnActor<ATN_TctJellyPad>(ATN_TctJellyPad::StaticClass(),
		FTransform(FRotator(0.0, Planter->GetActorRotation().Yaw, 0.0), Hit.ImpactPoint), Spawn);
	if (Jelly)
	{
		Jelly->SetLifeSpan(TNTctItemTuning::JellyLifeSeconds);
		TNTctItems::PlayCue(Planter, ETNRaceSound::Splat, 1.4f);
		UE_LOG(LogTortunabo, Log, TEXT("[TcT] %s planta una medusa trampolín."), *GetNameSafe(Planter));
	}
	return Jelly;
}

void ATN_TctJellyPad::BeginPlay()
{
	Super::BeginPlay();
	if (UStaticMesh* Look = TNTctItemMeshes::JellyDome())
	{
		Mesh->SetStaticMesh(Look);
	}
	Mesh->SetRelativeScale3D(FVector(0.05));
}

int32 ATN_TctJellyPad::ServerBounceTurtles()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return 0;
	}
	const FVector Base = GetActorLocation();
	int32 Bounced = 0;
	for (TActorIterator<ATortugaCharacter> It(World); It; ++It)
	{
		ATortugaCharacter* Turtle = *It;
		const UCharacterMovementComponent* Movement = Turtle ? Turtle->GetCharacterMovement() : nullptr;
		// Dentro del caparazón la mueve su caja con física: no bota.
		if (!Movement || !TNTctItems::CanAffect(Turtle, true))
		{
			continue;
		}
		const float* Until = RearmUntil.Find(Turtle);
		const FVector Velocity = Turtle->GetVelocity();
		const FVector Feet = Turtle->GetActorLocation() - FVector(0.0, 0.0, Turtle->GetSimpleCollisionHalfHeight());
		if ((Until && Age < *Until) || !TNTctItemRules::JellyTouches(Base, Feet, static_cast<float>(Velocity.Z)))
		{
			continue;
		}
		const float Up = TNTctItemRules::BounceSpeed(TNTctItemTuning::JellyBounceHeight, Movement->GetGravityZ());
		const FVector Launch(Velocity.X * TNTctJellyPadDetail::KeepFlat, Velocity.Y * TNTctJellyPadDetail::KeepFlat, Up);
		UTN_TurtleMovementComponent::LaunchFromServer(Turtle, Launch);
		TNTctItems::PlayCue(Turtle, ETNRaceSound::Boing, 0.9f);
		RearmUntil.Add(Turtle, Age + TNTctItemTuning::JellyRearmSeconds);
		++Bounced;
	}
	if (Bounced > 0)
	{
		MulticastSquash();
	}
	return Bounced;
}

void ATN_TctJellyPad::MulticastSquash_Implementation()
{
	SquashAge = 0.f;
}

void ATN_TctJellyPad::Tick(float DeltaSeconds)
{
	using namespace TNTctJellyPadDetail;
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (HasAuthority())
	{
		ServerBounceTurtles();
	}

	// Aparece, late despacio, se aplasta al botar y se recoge al final.
	const float Left = HasAuthority() ? GetLifeSpan() : TNTctItemTuning::JellyLifeSeconds - Age;
	const float Grow = FMath::Clamp(Age / GrowSeconds, 0.f, 1.f);
	const float Shrink = Left > 0.f ? FMath::Clamp(Left / ShrinkSeconds, 0.f, 1.f) : 1.f;
	float Squash = 0.f;
	if (SquashAge >= 0.f)
	{
		SquashAge += DeltaSeconds;
		const float Alpha = SquashAge / SquashSeconds;
		Squash = Alpha < 1.f ? FMath::Sin(Alpha * PI) * (1.f - Alpha) : 0.f;
		SquashAge = Alpha < 1.f ? SquashAge : -1.f;
	}
	const float Pulse = 0.04f * FMath::Sin(Age * 3.f);
	const float Size = TNTctItemTuning::JellyRadius / 100.f * FMath::Max(0.05f, FMath::Min(Grow, Shrink));
	Mesh->SetRelativeScale3D(FVector(Size * (1.f + Pulse + 0.35f * Squash), Size * (1.f + Pulse + 0.35f * Squash),
		Size * (1.f - Pulse - 0.6f * Squash)));
}
