#include "Game/TN_SurvivalLoot.h"
#include "World/Beach/TN_BeachLoot.h"

// Supervivencia reparte y sortea como la playa: si cambian allí, que cambien aquí.
static_assert(TNSurvivalLoot::SearchLuck == TNBeachLoot::SearchLuck, "La suerte de los rebuscables de Supervivencia es la de la playa.");
static_assert(TNSurvivalLoot::MinSpacing == TNBeachLoot::MinSearchSpacing, "La separación de los rebuscables de Supervivencia es la de la playa.");
static_assert(TNSurvivalLoot::MinRimGap == TNBeachLoot::MinSearchRimGap, "La separación de borde a borde de Supervivencia es la de la playa.");

bool TNSurvivalLoot::AllowsCatalogUse(ETN_ItemUseType Use)
{
	switch (Use)
	{
		case ETN_ItemUseType::SelfStaminaBoost:
		case ETN_ItemUseType::Throwable:
		case ETN_ItemUseType::BigHead:
		case ETN_ItemUseType::Conch:
		case ETN_ItemUseType::InkThrower:
			return true;
		// El tótem no: en Supervivencia morir es definitivo.
		default:
			return false;
	}
}

bool TNSurvivalLoot::AllowsRaceItem(ETNRaceItem Kind, int32 Place, int32 Racers)
{
	switch (Kind)
	{
		case ETNRaceItem::Coconut:
		case ETNRaceItem::TripleCoconut3:
		case ETNRaceItem::GoldenCoconut:
		case ETNRaceItem::SandMine:
		case ETNRaceItem::Frisbee:
			return true;
		// Va a por la de delante: la primera (y quien juega sola) no tiene a quién.
		case ETNRaceItem::HomingCrab:
			return Racers >= 2 && Place > 0;
		// Cae sobre todas las demás: en solitario no hay a quién.
		case ETNRaceItem::StormCloud:
			return Racers >= 2;
		default:
			return false;
	}
}

float TNSurvivalLoot::Weight(ETN_ItemUseType Use, ETNRaceItem Kind, float RaceWeight, int32 Place, int32 Racers)
{
	if (Kind != ETNRaceItem::None)
	{
		return AllowsRaceItem(Kind, Place, Racers) ? FMath::Max(0.f, RaceWeight) : 0.f;
	}
	if (!AllowsCatalogUse(Use))
	{
		return 0.f;
	}
	return Use == ETN_ItemUseType::BigHead ? BigHeadWeight : FMath::Max(0.f, RaceWeight);
}

double TNSurvivalLoot::SpotChance(int32 Priority)
{
	// Los de TNBeachLoot::SearchChance: lo grande, lo mediano y lo pequeño que aún se puede revolver.
	if (Priority <= 0)
	{
		return 1.0;
	}
	return Priority <= 2 ? 0.85 : 0.6;
}

ETNRaceItem TNSurvivalLoot::StartItemFor(int32 Place, int32 Finishers)
{
	// Premiadas: todas menos la última, como mucho tantas como peldaños. La primera lleva el peldaño más alto que le toca.
	const int32 Rewarded = FMath::Min(Finishers - 1, static_cast<int32>(UE_ARRAY_COUNT(StartItemLadder)));
	if (Place < 0 || Place >= Rewarded)
	{
		return ETNRaceItem::None;
	}
	return StartItemLadder[Rewarded - 1 - Place];
}

bool TNSurvivalLoot::Roll(const APawn* Picker, const UDataTable* Catalog, FTN_InventoryItem& OutItem)
{
	return TNRaceItems::RollLoot(Picker, ETNRaceLootSource::Search, Catalog,
		[](ETN_ItemUseType Use, ETNRaceItem Kind, float RaceWeight, const FTNRaceRank& Rank)
		{
			return Weight(Use, Kind, RaceWeight, Rank.Place, Rank.Count);
		}, OutItem);
}
