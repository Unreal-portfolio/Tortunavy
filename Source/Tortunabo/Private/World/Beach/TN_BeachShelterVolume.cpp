#include "World/Beach/TN_BeachShelterVolume.h"

#include "Engine/World.h"
#include "World/Beach/TN_BeachCreatureRules.h"

ATN_BeachShelterVolume::ATN_BeachShelterVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

TArray<TWeakObjectPtr<ATN_BeachShelterVolume>>& ATN_BeachShelterVolume::Registry()
{
	static TArray<TWeakObjectPtr<ATN_BeachShelterVolume>> Shelters;
	return Shelters;
}

void ATN_BeachShelterVolume::BeginPlay()
{
	Super::BeginPlay();
	Registry().AddUnique(this);
}

void ATN_BeachShelterVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Registry().RemoveAll([this](const TWeakObjectPtr<ATN_BeachShelterVolume>& Weak) { return !Weak.IsValid() || Weak.Get() == this; });
	Super::EndPlay(EndPlayReason);
}

bool ATN_BeachShelterVolume::ContainsPoint(const FVector& Point) const
{
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Point);
	return TNBeachCreatureRules::Shelter::IsInside(Local, HalfExtent);
}

bool ATN_BeachShelterVolume::IsPointSheltered(const UWorld* World, const FVector& Point)
{
	if (!World)
	{
		return false;
	}
	for (const TWeakObjectPtr<ATN_BeachShelterVolume>& Weak : Registry())
	{
		const ATN_BeachShelterVolume* Shelter = Weak.Get();
		if (Shelter && Shelter->GetWorld() == World && Shelter->ContainsPoint(Point))
		{
			return true;
		}
	}
	return false;
}

bool ATN_BeachShelterVolume::IsSheltered(const AActor* Actor)
{
	return Actor && Registry().Num() > 0 && IsPointSheltered(Actor->GetWorld(), Actor->GetActorLocation());
}

ATN_BeachShelterVolume* ATN_BeachShelterVolume::SpawnLocal(UWorld* World, const FTransform& Transform, const FVector& InHalfExtent, AActor* Owner)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Owner = Owner;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	ATN_BeachShelterVolume* Shelter = World->SpawnActor<ATN_BeachShelterVolume>(ATN_BeachShelterVolume::StaticClass(), Transform, Params);
	if (Shelter)
	{
		Shelter->SetShelterExtent(InHalfExtent);
	}
	return Shelter;
}
