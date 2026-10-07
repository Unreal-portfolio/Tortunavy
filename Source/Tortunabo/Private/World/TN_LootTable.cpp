#include "World/TN_LootTable.h"

DEFINE_LOG_CATEGORY(LogTNLoot);

TArray<float> TNLootRules::SanitizeChances(TConstArrayView<float> AtLeast)
{
	TArray<float> Out;
	const int32 Count = FMath::Min(AtLeast.Num(), MaxChapaTiers);
	Out.Reserve(Count);
	float Previous = 1.f;
	for (int32 i = 0; i < Count; ++i)
	{
		// Un valor no finito cuenta como 0; cada umbral, en [0, 1] y nunca por encima del anterior.
		const float Raw = FMath::IsFinite(AtLeast[i]) ? AtLeast[i] : 0.f;
		const float Clean = FMath::Min(FMath::Clamp(Raw, 0.f, 1.f), Previous);
		Out.Add(Clean);
		Previous = Clean;
	}
	return Out;
}

int32 TNLootRules::ChapasFromRoll(TConstArrayView<float> AtLeast, float Roll)
{
	const TArray<float> Chances = SanitizeChances(AtLeast);
	int32 Count = 0;
	for (const float Chance : Chances)
	{
		// Los umbrales bajan: en cuanto uno no se cumple, los siguientes tampoco.
		if (Roll >= Chance)
		{
			break;
		}
		++Count;
	}
	return Count;
}

float TNLootRules::ChanceOfExactly(TConstArrayView<float> AtLeast, int32 Count)
{
	const TArray<float> Chances = SanitizeChances(AtLeast);
	if (Count < 0 || Count > Chances.Num())
	{
		return 0.f;
	}
	const float AtLeastCount = Count == 0 ? 1.f : Chances[Count - 1];
	const float AtLeastNext = Chances.IsValidIndex(Count) ? Chances[Count] : 0.f;
	return FMath::Max(0.f, AtLeastCount - AtLeastNext);
}

int32 TNLootRules::ItemsFromRoll(int32 MinItems, int32 MaxItems, float Roll)
{
	const int32 Low = FMath::Max(0, MinItems);
	const int32 High = FMath::Max(Low, MaxItems);
	const float Clamped = FMath::Clamp(Roll, 0.f, 1.f);
	return FMath::Min(High, Low + FMath::FloorToInt(Clamped * static_cast<float>(High - Low + 1)));
}

FTNLootRoll TNLootRules::Roll(const FTNLootTableDef& Def, FRandomStream& Stream)
{
	FTNLootRoll Out;
	Out.ItemCount = ItemsFromRoll(Def.MinItems, Def.MaxItems, Stream.FRand());
	Out.ChapaCount = ChapasFromRoll(Def.ChapaChances, Stream.FRand());
	return Out;
}

FTNLootTableDef TNLootRules::SupplyCrateDefaults()
{
	FTNLootTableDef Def;
	Def.MinItems = 1;
	Def.MaxItems = 1;
	Def.ChapaChances = { 0.25f, 0.07f, 0.f };
	return Def;
}

FTNLootTableDef TNLootRules::AirdropDefaults()
{
	FTNLootTableDef Def;
	Def.MinItems = 2;
	Def.MaxItems = 2;
	Def.ChapaChances = { 0.7f, 0.3f, 0.15f };
	return Def;
}
