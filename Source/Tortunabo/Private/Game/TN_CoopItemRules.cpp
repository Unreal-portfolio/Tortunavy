// Objetos del coop: reglas puras (catálogo, ItemId, apilado y tabla de botín). Ver TN_CoopItemRules.h.

#include "Game/TN_CoopItemRules.h"

namespace TNCoopItemRulesDetail
{
	const TCHAR* const IdPrefix = TEXT("Coop_");

	/** Fichas, en el orden del enum (el índice es el valor). Peso, límite de apilado y usos: hoja ObjectsData del Excel. */
	const FTNCoopItemSpec Specs[] = {
		{ ETNCoopItem::None, TEXT("None"), 1, 1, 0.f },
		// Pez Globo: consumir, 5 s, potencia x2, mareo, peso 15 %, límite de apilado 1.
		{ ETNCoopItem::PufferFish, TEXT("PufferFish"), 1, 1, 15.f },
		// Cáscaras resbaladizas: lanzar, rango 8 m, suelo o enemigo, desestabiliza, peso 15 %, límite de apilado 2.
		{ ETNCoopItem::SlipperyPeel, TEXT("SlipperyPeel"), 2, 1, 15.f },
		// Conchas: lanzar, aturdir, rango 10 m, objetivo enemigo, peso 10 %, límite de apilado 3.
		{ ETNCoopItem::StunShell, TEXT("StunShell"), 3, 1, 10.f },
		// Arpón: usable, pescar objetos y rescate, 15 usos, rango 15 m, peso 30 %, límite de apilado 1.
		{ ETNCoopItem::Harpoon, TEXT("Harpoon"), 1, 15, 30.f },
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

FVector TNCoopItemRules::ClampThrowTarget(const FVector& Origin, const FVector& Desired, float MaxRange)
{
	const FVector2D Flat(Desired.X - Origin.X, Desired.Y - Origin.Y);
	const double Range = FMath::Max(0.0, static_cast<double>(MaxRange));
	if (Flat.Size() <= Range)
	{
		return Desired;
	}
	const FVector2D Clamped = Flat.GetSafeNormal() * Range;
	return FVector(Origin.X + Clamped.X, Origin.Y + Clamped.Y, Desired.Z);
}

FVector TNCoopItemRules::ArcPoint(const FVector& From, const FVector& To, float Alpha, float Height)
{
	const double A = FMath::Clamp(static_cast<double>(Alpha), 0.0, 1.0);
	return FMath::Lerp(From, To, A) + FVector(0.0, 0.0, 4.0 * Height * A * (1.0 - A));
}

float TNCoopItemRules::ThrowFlightSeconds(float Distance)
{
	return FMath::Max(TNCoopItemTuning::ThrowMinSeconds, FMath::Max(0.f, Distance) / TNCoopItemTuning::ThrowSpeed);
}

float TNCoopItemRules::ThrowArcHeight(float Distance)
{
	return TNCoopItemTuning::ThrowArcBase + FMath::Max(0.f, Distance) * TNCoopItemTuning::ThrowArcPerCm;
}

bool TNCoopItemRules::IsOnPatch(const FVector& Patch, const FVector& Where)
{
	return FVector::Dist2D(Patch, Where) <= TNCoopItemTuning::PeelRadius && FMath::Abs(Where.Z - Patch.Z) <= TNCoopItemTuning::PeelStepHeight;
}

FVector TNCoopItemRules::SlipVelocity(const FVector& Velocity, const FVector& Facing)
{
	const FVector Flat(Velocity.X, Velocity.Y, 0.0);
	const double Speed = Flat.Size();
	FVector Dir = Speed > 50.0 ? Flat / Speed : FVector(Facing.X, Facing.Y, 0.0).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector::ForwardVector;
	}
	const double SlideSpeed = FMath::Max(static_cast<double>(TNCoopItemTuning::PeelSlipSpeed), Speed * 1.2);
	return Dir * SlideSpeed + FVector(0.0, 0.0, TNCoopItemTuning::PeelSlipUp);
}

bool TNCoopItemRules::CanShellStun(bool bIsTurtle, bool bIsEnemy, bool bAcceptsStun)
{
	return !bIsTurtle && bIsEnemy && bAcceptsStun;
}

bool TNCoopItemRules::IsShellHit(const FVector& AimedAt, const FVector& EnemyNow)
{
	return FVector::Dist(AimedAt, EnemyNow) <= TNCoopItemTuning::ShellHitSlack;
}

bool TNCoopItemRules::IsInShot(const FVector& Start, const FVector& Dir, float Range, const FVector& Point, float ExtraRadius, float& OutAlong)
{
	const double Along = FVector::DotProduct(Point - Start, Dir);
	OutAlong = static_cast<float>(Along);
	if (Along < 0.0 || Along > Range)
	{
		return false;
	}
	const double Lateral = FVector::Dist(Point, Start + Dir * Along);
	return Lateral <= TNCoopItemTuning::HarpoonAimRadius + FMath::Max(0.f, ExtraRadius);
}

bool TNCoopItemRules::CanRescue(bool bIsSelf, bool bDead, bool bKnockedDown, bool bInWater)
{
	return !bIsSelf && !bDead && (bKnockedDown || bInWater);
}

int32 TNCoopItemRules::PickHarpoonTarget(const TArray<FTNHarpoonCandidate>& Candidates)
{
	int32 Best = INDEX_NONE;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FTNHarpoonCandidate& Candidate = Candidates[Index];
		if (!Candidate.bValid || Candidate.Type == ETNHarpoonTarget::None)
		{
			continue;
		}
		if (Best == INDEX_NONE || Candidate.Along < Candidates[Best].Along)
		{
			Best = Index;
		}
	}
	return Best;
}

FVector TNCoopItemRules::RescuePull(const FVector& From, const FVector& To)
{
	using namespace TNCoopItemTuning;
	const FVector Rel(To.X - From.X, To.Y - From.Y, 0.0);
	const double Dist = Rel.Size();
	const double Travel = Dist - HarpoonStopShort;
	if (Travel <= 1.0)
	{
		return FVector::UpVector * (HarpoonPullUp * 0.5);
	}
	// Lo justo para llegar cerca en el vuelo del tirón (más cuanto más lejos), con tope.
	const double Speed = FMath::Clamp(Travel * 1.3, static_cast<double>(HarpoonPullMin), static_cast<double>(HarpoonPullMax));
	const double Up = HarpoonPullUp + FMath::Clamp(To.Z - From.Z, 0.0, 600.0) * 0.8;
	return Rel / Dist * Speed + FVector::UpVector * Up;
}
