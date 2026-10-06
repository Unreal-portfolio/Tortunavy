#include "World/TN_TctAlgaPuddle.h"
#include "../Game/TN_TctItemMeshes.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/TN_TctGameState.h"
#include "Game/TN_TctItemComponent.h"
#include "Game/TN_TctItemRules.h"
#include "Player/TortugaCharacter.h"

namespace TNTctAlgaPuddleDetail
{
	/** Lo que tarda en extenderse al caer y en recogerse al acabarse (s). */
	constexpr float GrowSeconds = 0.25f;
	constexpr float ShrinkSeconds = 0.5f;
	/** Hasta dónde busca suelo bajo el punto donde cae (uu). */
	constexpr double GroundProbe = 800.0;
}

ATN_TctAlgaPuddle::ATN_TctAlgaPuddle()
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

ATN_TctAlgaPuddle* ATN_TctAlgaPuddle::ServerSpawn(UWorld* World, const FVector& Where, APawn* Instigator)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TctAlgaPuddle), false, Instigator);
	const FVector Top = Where + FVector(0.0, 0.0, 60.0);
	if (!World->LineTraceSingleByObjectType(Hit, Top, Top - FVector(0.0, 0.0, TNTctAlgaPuddleDetail::GroundProbe),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		return nullptr;
	}
	FActorSpawnParameters Spawn;
	Spawn.Instigator = Instigator;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FRotator Yaw(0.0, FMath::FRandRange(0.0, 360.0), 0.0);
	ATN_TctAlgaPuddle* Puddle = World->SpawnActor<ATN_TctAlgaPuddle>(ATN_TctAlgaPuddle::StaticClass(),
		FTransform(Yaw, Hit.ImpactPoint + FVector(0.0, 0.0, 1.0)), Spawn);
	if (Puddle)
	{
		Puddle->SetLifeSpan(TNTctItemTuning::AlgaPuddleSeconds);
		UE_LOG(LogTortunabo, Log, TEXT("[TcT] %s deja un charco de alga."), *GetNameSafe(Instigator));
	}
	return Puddle;
}

void ATN_TctAlgaPuddle::BeginPlay()
{
	Super::BeginPlay();
	if (UStaticMesh* Look = TNTctItemMeshes::AlgaPuddle())
	{
		Mesh->SetStaticMesh(Look);
	}
	Mesh->SetRelativeScale3D(FVector(0.01, 0.01, 1.0));
}

FName ATN_TctAlgaPuddle::SlipSource() const
{
	return FName(TEXT("TctAlga"), static_cast<int32>(GetUniqueID()));
}

void ATN_TctAlgaPuddle::SetSlipping(ATortugaCharacter* Turtle, bool bSlipping)
{
	if (UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOn(Turtle))
	{
		Effects->SetSlipping(SlipSource(), bSlipping);
	}
	if (bSlipping)
	{
		Slipping.Add(Turtle);
	}
	else
	{
		Slipping.Remove(Turtle);
	}
}

void ATN_TctAlgaPuddle::Tick(float DeltaSeconds)
{
	using namespace TNTctAlgaPuddleDetail;
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FVector Center = GetActorLocation();

	// El agua se lo lleva.
	const ATN_TctGameState* State = World->GetGameState<ATN_TctGameState>();
	if (HasAuthority() && State && State->GetWaterZ() > Center.Z)
	{
		Destroy();
		return;
	}

	// Cada máquina, a las tortugas que mueve ella (el servidor a todas; el cliente, la suya).
	for (TActorIterator<ATortugaCharacter> It(World); It; ++It)
	{
		ATortugaCharacter* Turtle = *It;
		if (!Turtle || !(Turtle->HasAuthority() || Turtle->IsLocallyControlled()))
		{
			continue;
		}
		const float HalfHeight = Turtle->GetCapsuleComponent() ? Turtle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
		const FVector Feet = Turtle->GetActorLocation() - FVector(0.0, 0.0, HalfHeight);
		const bool bInside = !Turtle->IsDead() && TNTctItemRules::IsInPuddle(Center, TNTctItemTuning::AlgaPuddleRadius, Feet);
		if (bInside != Slipping.Contains(Turtle))
		{
			SetSlipping(Turtle, bInside);
		}
	}

	// Se extiende al caer y se recoge al final.
	const float Left = HasAuthority() ? GetLifeSpan() : TNTctItemTuning::AlgaPuddleSeconds - Age;
	const float Grow = FMath::Clamp(Age / GrowSeconds, 0.f, 1.f);
	const float Shrink = Left > 0.f ? FMath::Clamp(Left / ShrinkSeconds, 0.f, 1.f) : 1.f;
	const double Scale = FMath::Max(0.01, TNTctItemTuning::AlgaPuddleRadius / 100.0 * FMath::Min(Grow, Shrink));
	Mesh->SetRelativeScale3D(FVector(Scale, Scale, 1.0));
}

void ATN_TctAlgaPuddle::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const TWeakObjectPtr<ATortugaCharacter>& Turtle : Slipping)
	{
		if (UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOn(Turtle.Get()))
		{
			Effects->SetSlipping(SlipSource(), false);
		}
	}
	Slipping.Reset();
	Super::EndPlay(EndPlayReason);
}
