#include "World/Beach/TN_BeachNearby.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"

namespace TNBeachNearbyDetail
{
	/** Una lista por mundo (en el PIE, servidor y clientes son mundos distintos en el mismo proceso). */
	struct FFrameList
	{
		TWeakObjectPtr<const UWorld> World;
		uint64 Frame = MAX_uint64;
		TArray<TWeakObjectPtr<ACharacter>> Characters;
	};

	/** Mundos que se recuerdan a la vez: el servidor y tres clientes del PIE. */
	constexpr int32 MaxWorlds = 4;

	FFrameList& SlotFor(const UWorld* World)
	{
		static FFrameList Lists[MaxWorlds];
		for (FFrameList& List : Lists)
		{
			if (List.World.Get() == World)
			{
				return List;
			}
		}
		// Un hueco libre (mundo ya destruido) o, si no, el que lleva más tiempo sin usarse.
		FFrameList* Oldest = &Lists[0];
		for (FFrameList& List : Lists)
		{
			if (!List.World.IsValid())
			{
				Oldest = &List;
				break;
			}
			if (List.Frame < Oldest->Frame)
			{
				Oldest = &List;
			}
		}
		Oldest->World = World;
		Oldest->Frame = MAX_uint64;
		Oldest->Characters.Reset();
		return *Oldest;
	}
}

const TArray<TWeakObjectPtr<ACharacter>>& TNBeachNearby::FrameCharacters(const UWorld* World)
{
	TNBeachNearbyDetail::FFrameList& List = TNBeachNearbyDetail::SlotFor(World);
	if (List.Frame == GFrameCounter)
	{
		return List.Characters;
	}
	List.Frame = GFrameCounter;
	List.Characters.Reset();
	if (World)
	{
		for (TActorIterator<ACharacter> It(const_cast<UWorld*>(World)); It; ++It)
		{
			List.Characters.Add(*It);
		}
	}
	return List.Characters;
}

void TNBeachNearby::Gather(const UWorld* World, const FVector& Center, double Radius, TArray<ACharacter*>& Out)
{
	Out.Reset();
	for (const TWeakObjectPtr<ACharacter>& Weak : FrameCharacters(World))
	{
		ACharacter* Character = Weak.Get();
		if (IsValid(Character) && IsWithin2D(Character->GetActorLocation(), Center, Radius))
		{
			Out.Add(Character);
		}
	}
}
