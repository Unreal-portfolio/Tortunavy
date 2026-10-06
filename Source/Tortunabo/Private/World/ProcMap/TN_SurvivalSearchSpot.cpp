#include "World/ProcMap/TN_SurvivalSearchSpot.h"
#include "Game/TN_SurvivalLoot.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

namespace
{
	int32 PlayerIdOf(const APawn* Pawn)
	{
		const APlayerState* PS = Pawn ? Pawn->GetPlayerState() : nullptr;
		return PS ? PS->GetPlayerId() : INDEX_NONE;
	}
}

ATN_SurvivalSearchSpot::ATN_SurvivalSearchSpot()
{
	LootChance = TNSurvivalLoot::SearchLuck;
	// Una vez por tortuga: se puede volver a rebuscar al momento, pero no quien ya lo hizo (IsSpentFor).
	bRepeatable = true;
	RepeatCooldown = 0.f;
	MaxLootLying = 0;
	// Los pesos son los de Supervivencia (TNSurvivalLoot::Weight): los del cooperativo, fuera.
	LootWeights.Reset();
}

void ATN_SurvivalSearchSpot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_SurvivalSearchSpot, SearchedBy);
}

bool ATN_SurvivalSearchSpot::PickLoot(FTN_InventoryItem& OutItem, const APawn* Searcher) const
{
	// Sin el sorteo de siempre de respaldo: daría el tótem.
	if (TNSurvivalLoot::Roll(Searcher, GetLootTable(), OutItem))
	{
		return true;
	}
	UE_LOG(LogTortunabo, Warning, TEXT("[Search] %s: el sorteo de Supervivencia no ha dado nada."), *GetName());
	return false;
}

void ATN_SurvivalSearchSpot::OnSearchStateChanged(const FTNSearchSpotState& OldState)
{
	Super::OnSearchStateChanged(OldState);
	// Servidor, al acabar una búsqueda (con objeto o sin él): quien rebuscaba ya no puede volver a hacerlo aquí.
	const FTNSearchSpotState& State = GetSearchState();
	if (HasAuthority() && State.SearchCount != OldState.SearchCount && State.Outcome != ETNSearchOutcome::None)
	{
		const int32 PlayerId = PlayerIdOf(OldState.Searcher.Get());
		if (PlayerId != INDEX_NONE)
		{
			SearchedBy.AddUnique(PlayerId);
		}
	}
}

bool ATN_SurvivalSearchSpot::IsSpentFor(const APawn* Interactor) const
{
	return Super::IsSpentFor(Interactor) || SearchedBy.Contains(PlayerIdOf(Interactor));
}

bool ATN_SurvivalSearchSpot::IsSpentForLocalView() const
{
	if (Super::IsSpentForLocalView())
	{
		return true;
	}
	// Con pantalla partida, las chispitas y el anillo se ven mientras a alguna de esta pantalla le quede por rebuscarlo.
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	bool bAnyLocal = false;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController())
		{
			continue;
		}
		bAnyLocal = true;
		if (!PC->PlayerState || !SearchedBy.Contains(PC->PlayerState->GetPlayerId()))
		{
			return false;
		}
	}
	return bAnyLocal;
}
