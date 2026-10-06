#include "Game/TN_RoundLeftovers.h"
#include "Game/TN_BeachRaceDecisions.h"
#include "World/TN_ConchPickup.h"
#include "World/TN_PickupInteractableBase.h"
#include "World/TN_ThrowableItemActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace TNRoundLeftoversDetail
{
	/** Junta en Out los actores de TActorClass que quedan sueltos y no son de ninguna ronda (TNBeachRaceRules::ShouldClearRoundLeftover). */
	template <typename TActorClass>
	void Gather(UWorld* World, TArray<AActor*>& Out)
	{
		for (TActorIterator<TActorClass> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (TNBeachRaceRules::ShouldClearRoundLeftover(Actor->IsNetStartupActor(), !IsValid(Actor) || Actor->IsActorBeingDestroyed()))
			{
				Out.AddUnique(Actor);
			}
		}
	}
}

int32 TNRoundLeftovers::DestroyPlayerLeftovers(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return 0;
	}
	TArray<AActor*> Leftovers;
	TNRoundLeftoversDetail::Gather<ATN_ThrowableItemActor>(World, Leftovers);
	TNRoundLeftoversDetail::Gather<ATN_PickupInteractableBase>(World, Leftovers);
	TNRoundLeftoversDetail::Gather<ATN_ConchPickup>(World, Leftovers);
	for (AActor* Leftover : Leftovers)
	{
		Leftover->Destroy();
	}
	return Leftovers.Num();
}
