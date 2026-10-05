#include "World/ProcMap/TN_SurvivalSearchSpot.h"
#include "Game/TN_SurvivalLoot.h"
#include "Core/TN_Log.h"

ATN_SurvivalSearchSpot::ATN_SurvivalSearchSpot()
{
	LootChance = TNSurvivalLoot::SearchLuck;
	// Se rellena: quien va primera no deja vacíos los de las demás.
	bRepeatable = true;
	RepeatCooldown = TNSurvivalLoot::RefillSeconds;
	MaxLootLying = TNSurvivalLoot::MaxLootLying;
	// Los pesos son los de Supervivencia (TNSurvivalLoot::Weight): los del cooperativo, fuera.
	LootWeights.Reset();
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
