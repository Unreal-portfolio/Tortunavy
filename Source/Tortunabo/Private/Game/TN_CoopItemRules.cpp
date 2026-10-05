// Objetos del coop: reglas puras (catálogo, ItemId, apilado y tabla de botín). Ver TN_CoopItemRules.h.

#include "Game/TN_CoopItemRules.h"

namespace TNCoopItemRulesDetail
{
	const TCHAR* const IdPrefix = TEXT("Coop_");

	/** Fichas, en el orden del enum (el índice es el valor). Peso, límite de apilado y usos: hoja ObjectsData del Excel. */
	const FTNCoopItemSpec Specs[] = {
		{ ETNCoopItem::None, TEXT("None"), 1, 1, 0.f },
	};
	static_assert(UE_ARRAY_COUNT(Specs) == static_cast<int32>(ETNCoopItem::Count), "Una ficha por objeto del coop, en el orden del enum");
}

const FTNCoopItemSpec& TNCoopItemRules::Spec(ETNCoopItem Kind)
{
	const int32 Index = static_cast<int32>(Kind);
	return (Index > 0 && Index < static_cast<int32>(ETNCoopItem::Count)) ? TNCoopItemRulesDetail::Specs[Index] : TNCoopItemRulesDetail::Specs[0];
}

TArray<ETNCoopItem> TNCoopItemRules::AllKinds()
{
	TArray<ETNCoopItem> Out;
	for (int32 Index = 1; Index < static_cast<int32>(ETNCoopItem::Count); ++Index)
	{
		Out.Add(static_cast<ETNCoopItem>(Index));
	}
	return Out;
}

int32 TNCoopItemRules::InitialCount(ETNCoopItem Kind)
{
	return FMath::Max(1, Spec(Kind).Uses);
}

FName TNCoopItemRules::MakeItemId(ETNCoopItem Kind, int32 Count)
{
	if (Kind == ETNCoopItem::None || Kind >= ETNCoopItem::Count || Count <= 0)
	{
		return NAME_None;
	}
	return FName(*FString::Printf(TEXT("%s%s_%d"), TNCoopItemRulesDetail::IdPrefix, Spec(Kind).Code, Count));
}

bool TNCoopItemRules::ParseItemId(FName ItemId, ETNCoopItem& OutKind, int32& OutCount)
{
	OutKind = ETNCoopItem::None;
	OutCount = 0;
	const FString Text = ItemId.ToString();
	const int32 PrefixLength = FCString::Strlen(TNCoopItemRulesDetail::IdPrefix);
	if (!Text.StartsWith(TNCoopItemRulesDetail::IdPrefix, ESearchCase::CaseSensitive))
	{
		return false;
	}
	int32 Underscore = INDEX_NONE;
	if (!Text.FindLastChar(TEXT('_'), Underscore) || Underscore <= PrefixLength)
	{
		return false;
	}
	const FString Code = Text.Mid(PrefixLength, Underscore - PrefixLength);
	const FString Number = Text.Mid(Underscore + 1);
	if (Number.IsEmpty() || !Number.IsNumeric())
	{
		return false;
	}
	const int32 Count = FCString::Atoi(*Number);
	if (Count <= 0)
	{
		return false;
	}
	for (const ETNCoopItem Kind : AllKinds())
	{
		if (Code.Equals(Spec(Kind).Code, ESearchCase::CaseSensitive))
		{
			OutKind = Kind;
			OutCount = Count;
			return true;
		}
	}
	return false;
}

FName TNCoopItemRules::ItemIdAfterUse(FName ItemId)
{
	ETNCoopItem Kind = ETNCoopItem::None;
	int32 Count = 0;
	if (!ParseItemId(ItemId, Kind, Count) || Count <= 1)
	{
		return NAME_None;
	}
	return MakeItemId(Kind, Count - 1);
}

ETNCoopStack TNCoopItemRules::DecideStackCounts(int32 MaxStack, int32 Uses, int32 HeldCount, int32 IncomingCount, int32& OutCount)
{
	const int32 Held = FMath::Max(0, HeldCount);
	const int32 Incoming = FMath::Max(0, IncomingCount);
	// Herramienta de varios usos: un hueco tiene una sola; coger otra deja los usos de la que tenga más.
	const int32 Merged = Uses > 1 ? FMath::Min(FMath::Max(Held, Incoming), Uses) : FMath::Min(Held + Incoming, FMath::Max(1, MaxStack));
	OutCount = FMath::Max(Held, Merged);
	return Merged > Held ? ETNCoopStack::Merge : ETNCoopStack::Full;
}

ETNCoopStack TNCoopItemRules::DecideStack(ETNCoopItem Held, int32 HeldCount, ETNCoopItem Incoming, int32 IncomingCount, int32& OutCount)
{
	OutCount = HeldCount;
	if (Held == ETNCoopItem::None || Held != Incoming)
	{
		return ETNCoopStack::Separate;
	}
	const FTNCoopItemSpec& Info = Spec(Held);
	return DecideStackCounts(Info.MaxStack, Info.Uses, HeldCount, IncomingCount, OutCount);
}

float TNCoopItemRules::CatalogWeight(ETN_ItemUseType Use, float BaseWeight)
{
	switch (Use)
	{
	case ETN_ItemUseType::None:
	case ETN_ItemUseType::RaceItem:
	case ETN_ItemUseType::TctItem:
	case ETN_ItemUseType::CoopItem:
		return 0.f;
	default:
		return FMath::Max(0.f, BaseWeight) * TNCoopItemTuning::CatalogWeightScale;
	}
}

int32 TNCoopItemRules::PickWeighted(const TArray<float>& Weights, float Roll)
{
	float Total = 0.f;
	for (const float Weight : Weights)
	{
		Total += FMath::Max(0.f, Weight);
	}
	if (Total <= 0.f)
	{
		return INDEX_NONE;
	}
	float Pick = FMath::Clamp(Roll, 0.f, 0.9999f) * Total;
	int32 LastPositive = INDEX_NONE;
	for (int32 Index = 0; Index < Weights.Num(); ++Index)
	{
		const float Weight = FMath::Max(0.f, Weights[Index]);
		if (Weight <= 0.f)
		{
			continue;
		}
		LastPositive = Index;
		if (Pick < Weight)
		{
			return Index;
		}
		Pick -= Weight;
	}
	return LastPositive;
}

FTNCoopLootPick TNCoopItemRules::PickLoot(const TArray<float>& CatalogWeights, float Roll)
{
	TArray<float> Weights = CatalogWeights;
	const TArray<ETNCoopItem> Kinds = AllKinds();
	for (const ETNCoopItem Kind : Kinds)
	{
		Weights.Add(Spec(Kind).LootWeight);
	}
	FTNCoopLootPick Pick;
	const int32 Index = PickWeighted(Weights, Roll);
	if (Index == INDEX_NONE)
	{
		return Pick;
	}
	if (Index < CatalogWeights.Num())
	{
		Pick.CatalogIndex = Index;
	}
	else
	{
		Pick.Kind = Kinds[Index - CatalogWeights.Num()];
	}
	return Pick;
}

float TNCoopItemRules::LootChance(const TArray<float>& CatalogWeights, ETNCoopItem Kind)
{
	float Total = 0.f;
	for (const float Weight : CatalogWeights)
	{
		Total += FMath::Max(0.f, Weight);
	}
	for (const ETNCoopItem Other : AllKinds())
	{
		Total += FMath::Max(0.f, Spec(Other).LootWeight);
	}
	return Total > 0.f ? FMath::Max(0.f, Spec(Kind).LootWeight) / Total : 0.f;
}
