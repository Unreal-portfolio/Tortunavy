#include "Game/TN_TctItemRules.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del
// bloque.
namespace TNTctItemRulesDetail
{
	/**
	 * Fichas en el orden de ETNTctItem. Pesos: lo que empuja y se entiende al momento sale más (pala, trabuco, balón); lo que
	 * decide una ronda de un golpe (ancla, pistola de noqueo) o solo molesta (tinta, cabezota), menos.
	 */
	const FTNTctItemSpec Specs[] =
	{
		{ ETNTctItem::None,           TEXT("None"),           ETNTctItemSource::Code,    0, 0.f },
		{ ETNTctItem::KnockoutPistol, TEXT("KnockoutPistol"), ETNTctItemSource::Code,    3, 0.8f },
		{ ETNTctItem::AirBlunderbuss, TEXT("AirBlunderbuss"), ETNTctItemSource::Code,    3, 1.f },
		{ ETNTctItem::Grapple,        TEXT("Grapple"),        ETNTctItemSource::Code,    3, 0.9f },
		{ ETNTctItem::Shovel,         TEXT("Shovel"),         ETNTctItemSource::Code,    4, 1.f },
		{ ETNTctItem::BeachBall,      TEXT("BeachBall"),      ETNTctItemSource::Code,    1, 1.f },
		{ ETNTctItem::Anchor,         TEXT("Anchor"),         ETNTctItemSource::Code,    1, 0.6f },
		{ ETNTctItem::JellyDart,      TEXT("JellyDart"),      ETNTctItemSource::Code,    3, 0.8f },
		{ ETNTctItem::InkPistol,      TEXT("InkPistol"),      ETNTctItemSource::Code,    3, 0.6f },
		{ ETNTctItem::Ball,           TEXT("Ball"),           ETNTctItemSource::Catalog, 1, 0.8f },
		{ ETNTctItem::ConchTrap,      TEXT("ConchTrap"),      ETNTctItemSource::Catalog, 1, 0.6f },
		{ ETNTctItem::BigHead,        TEXT("BigHead"),        ETNTctItemSource::Catalog, 1, 0.4f },
		{ ETNTctItem::SandMine,       TEXT("SandMine"),       ETNTctItemSource::Race,    1, 0.7f },
		{ ETNTctItem::Frisbee,        TEXT("Frisbee"),        ETNTctItemSource::Race,    1, 0.7f },
	};
	static_assert(UE_ARRAY_COUNT(Specs) == static_cast<int32>(ETNTctItem::Count), "Una ficha por objeto, en el orden del enum");

	const TCHAR* const IdPrefix = TEXT("Tct_");

	/** Forward en el plano, normalizado (X si no tiene). */
	FVector Flat(const FVector& Forward)
	{
		const FVector Out = FVector(Forward.X, Forward.Y, 0.0).GetSafeNormal();
		return Out.IsNearlyZero() ? FVector::ForwardVector : Out;
	}
}

const FTNTctItemSpec& TNTctItemRules::Spec(ETNTctItem Kind)
{
	const int32 Index = static_cast<int32>(Kind);
	return TNTctItemRulesDetail::Specs[(Index >= 0 && Index < static_cast<int32>(ETNTctItem::Count)) ? Index : 0];
}

TArray<ETNTctItem> TNTctItemRules::AllKinds()
{
	TArray<ETNTctItem> Out;
	for (int32 Index = 1; Index < static_cast<int32>(ETNTctItem::Count); ++Index)
	{
		Out.Add(static_cast<ETNTctItem>(Index));
	}
	return Out;
}

FName TNTctItemRules::MakeItemId(ETNTctItem Kind, int32 Charges)
{
	if (Kind == ETNTctItem::None || Kind >= ETNTctItem::Count || Charges <= 0)
	{
		return NAME_None;
	}
	return FName(*FString::Printf(TEXT("%s%s_%d"), TNTctItemRulesDetail::IdPrefix, Spec(Kind).Code, Charges));
}

bool TNTctItemRules::ParseItemId(FName ItemId, ETNTctItem& OutKind, int32& OutCharges)
{
	OutKind = ETNTctItem::None;
	OutCharges = 0;
	const FString Text = ItemId.ToString();
	if (!Text.StartsWith(TNTctItemRulesDetail::IdPrefix, ESearchCase::CaseSensitive))
	{
		return false;
	}
	int32 Underscore = INDEX_NONE;
	if (!Text.FindLastChar(TEXT('_'), Underscore) || Underscore <= FCString::Strlen(TNTctItemRulesDetail::IdPrefix))
	{
		return false;
	}
	const FString Code = Text.Mid(FCString::Strlen(TNTctItemRulesDetail::IdPrefix), Underscore - FCString::Strlen(TNTctItemRulesDetail::IdPrefix));
	const FString Number = Text.Mid(Underscore + 1);
	if (Number.IsEmpty() || !Number.IsNumeric())
	{
		return false;
	}
	const int32 Charges = FCString::Atoi(*Number);
	for (const ETNTctItem Kind : AllKinds())
	{
		if (Code.Equals(Spec(Kind).Code, ESearchCase::CaseSensitive) && Charges > 0)
		{
			OutKind = Kind;
			OutCharges = Charges;
			return true;
		}
	}
	return false;
}

FName TNTctItemRules::ItemIdAfterUse(FName ItemId)
{
	ETNTctItem Kind = ETNTctItem::None;
	int32 Charges = 0;
	if (!ParseItemId(ItemId, Kind, Charges) || Charges <= 1)
	{
		return NAME_None;
	}
	return MakeItemId(Kind, Charges - 1);
}

int32 TNTctItemRules::PickWeighted(const TArray<float>& Weights, float Roll)
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

ETNTctItem TNTctItemRules::PickPadItem(const TArray<ETNTctItem>& Available, ETNTctItem Last, float Roll)
{
	bool bHasOther = false;
	for (const ETNTctItem Kind : Available)
	{
		bHasOther |= (Kind != Last && Spec(Kind).PadWeight > 0.f);
	}
	TArray<float> Weights;
	for (const ETNTctItem Kind : Available)
	{
		Weights.Add((bHasOther && Kind == Last) ? 0.f : Spec(Kind).PadWeight);
	}
	const int32 Index = PickWeighted(Weights, Roll);
	return Index == INDEX_NONE ? ETNTctItem::None : Available[Index];
}

bool TNTctItemRules::IsPadSubmerged(float PadZ, float WaterZ, float Clearance)
{
	return WaterZ > PadZ - Clearance;
}

int32 TNTctItemRules::ActivePadCount(int32 Players, int32 PadCount)
{
	return FMath::Min(FMath::Max(0, PadCount), FMath::Max(4, Players + 2));
}

float TNTctItemRules::PadFirstSpawnDelay(int32 PadIndex)
{
	// Tres tandas: no salen todos de golpe, pero a los dos segundos ya está todo puesto.
	return TNTctItemTuning::PadFirstSpawnSeconds + (FMath::Max(0, PadIndex) % 4) * TNTctItemTuning::PadStaggerSeconds;
}

TArray<int32> TNTctItemRules::PickPadPoints(const TArray<FVector>& Candidates, int32 Count, const FVector& Center,
	const TArray<FVector>& Avoid, float MinFromAvoid)
{
	TArray<int32> Picked;
	const int32 Wanted = FMath::Min(Count, Candidates.Num());
	if (Wanted <= 0)
	{
		return Picked;
	}
	// Lejos de las salidas; si así no caben todos, cualquiera.
	TArray<int32> Pool;
	const double AvoidSq = FMath::Square(static_cast<double>(MinFromAvoid));
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		bool bNear = false;
		for (const FVector& Spot : Avoid)
		{
			bNear |= FVector::DistSquared2D(Candidates[Index], Spot) < AvoidSq;
		}
		if (!bNear)
		{
			Pool.Add(Index);
		}
	}
	if (Pool.Num() < Wanted)
	{
		Pool.Reset();
		for (int32 Index = 0; Index < Candidates.Num(); ++Index) { Pool.Add(Index); }
	}

	int32 Next = 0;
	for (int32 Slot = 1; Slot < Pool.Num(); ++Slot)
	{
		if (FVector::DistSquared2D(Candidates[Pool[Slot]], Center) < FVector::DistSquared2D(Candidates[Pool[Next]], Center))
		{
			Next = Slot;
		}
	}
	TArray<double> Nearest;
	Nearest.Init(TNumericLimits<double>::Max(), Pool.Num());
	TArray<bool> Taken;
	Taken.Init(false, Pool.Num());
	while (Picked.Num() < Wanted)
	{
		Picked.Add(Pool[Next]);
		Taken[Next] = true;
		int32 Best = INDEX_NONE;
		for (int32 Slot = 0; Slot < Pool.Num(); ++Slot)
		{
			Nearest[Slot] = FMath::Min(Nearest[Slot], FVector::DistSquared2D(Candidates[Pool[Slot]], Candidates[Pool[Next]]));
			if (!Taken[Slot] && (Best == INDEX_NONE || Nearest[Slot] > Nearest[Best]))
			{
				Best = Slot;
			}
		}
		if (Best == INDEX_NONE)
		{
			break;
		}
		Next = Best;
	}
	return Picked;
}

FVector TNTctItemRules::KnockoutImpulse(const FVector& ShotDirection)
{
	return TNTctItemRulesDetail::Flat(ShotDirection) * TNTctItemTuning::KnockoutPush + FVector::UpVector * TNTctItemTuning::KnockoutUp;
}

bool TNTctItemRules::BlunderbussPush(const FVector& Origin, const FVector& Forward, const FVector& Victim, FVector& OutVelocity)
{
	using namespace TNTctItemTuning;
	OutVelocity = FVector::ZeroVector;
	const FVector Fwd = TNTctItemRulesDetail::Flat(Forward);
	const FVector Rel(Victim.X - Origin.X, Victim.Y - Origin.Y, 0.0);
	const double Dist = Rel.Size();
	if (Dist > BlunderbussRange || FMath::Abs(Victim.Z - Origin.Z) > BlunderbussRange * 0.5)
	{
		return false;
	}
	// Pegada al cañón cuenta siempre (la ráfaga sale ancha); si no, dentro del ángulo.
	const FVector Dir = Dist < 1.0 ? Fwd : Rel / Dist;
	const double Cos = FVector::DotProduct(Dir, Fwd);
	if (Dist > 80.0 && Cos < FMath::Cos(FMath::DegreesToRadians(BlunderbussHalfAngleDeg)))
	{
		return false;
	}
	const double Near = 1.0 - FMath::Clamp(Dist / BlunderbussRange, 0.0, 1.0);
	const double Speed = FMath::Lerp(static_cast<double>(BlunderbussPushFar), static_cast<double>(BlunderbussPushNear), Near);
	// Hacia fuera del cañón, con algo de la dirección del tiro para que no salgan de lado.
	const FVector Away = (Dir + Fwd).GetSafeNormal2D();
	OutVelocity = Away * Speed + FVector::UpVector * (BlunderbussUp * (0.5 + 0.5 * Near));
	return true;
}

FVector TNTctItemRules::BlunderbussRecoil(const FVector& Forward)
{
	return -TNTctItemRulesDetail::Flat(Forward) * TNTctItemTuning::BlunderbussRecoilSpeed + FVector::UpVector * TNTctItemTuning::BlunderbussRecoilUp;
}

FVector TNTctItemRules::GrapplePull(const FVector& From, const FVector& To)
{
	using namespace TNTctItemTuning;
	const FVector Rel(To.X - From.X, To.Y - From.Y, 0.0);
	const double Dist = Rel.Size();
	if (Dist < 1.0)
	{
		return FVector::UpVector * GrapplePullUp;
	}
	// Lo justo para llegar cerca en el vuelo del salto (más cuanto más lejos), con tope.
	const double Speed = FMath::Clamp(Dist * 1.3, static_cast<double>(GrapplePullMin), static_cast<double>(GrapplePullMax));
	const double Up = GrapplePullUp + FMath::Clamp(To.Z - From.Z, 0.0, 600.0) * 0.8;
	return Rel / Dist * Speed + FVector::UpVector * Up;
}

bool TNTctItemRules::ShovelHit(const FVector& Origin, const FVector& Forward, const FVector& Victim, FVector& OutVelocity)
{
	using namespace TNTctItemTuning;
	OutVelocity = FVector::ZeroVector;
	const FVector Fwd = TNTctItemRulesDetail::Flat(Forward);
	const FVector Rel(Victim.X - Origin.X, Victim.Y - Origin.Y, 0.0);
	const double Dist = Rel.Size();
	if (Dist > ShovelReach || FMath::Abs(Victim.Z - Origin.Z) > 180.0)
	{
		return false;
	}
	const FVector Dir = Dist < 1.0 ? Fwd : Rel / Dist;
	if (Dist > 60.0 && FVector::DotProduct(Dir, Fwd) < FMath::Cos(FMath::DegreesToRadians(ShovelHalfAngleDeg)))
	{
		return false;
	}
	OutVelocity = (Dir + Fwd).GetSafeNormal2D() * ShovelPush + FVector::UpVector * ShovelUp;
	return true;
}

FVector TNTctItemRules::BallPush(const FVector& BallVelocity)
{
	using namespace TNTctItemTuning;
	const FVector Flat(BallVelocity.X, BallVelocity.Y, 0.0);
	const double Speed = Flat.Size();
	if (Speed < BallMinPushSpeed)
	{
		return FVector::ZeroVector;
	}
	const double Strength = FMath::Clamp(Speed / BallSpeed, 0.35, 1.0);
	return Flat / Speed * (BallPushSpeed * Strength) + FVector::UpVector * (BallUp * Strength);
}

bool TNTctItemRules::AnchorSplash(const FVector& Center, const FVector& Victim, FVector& OutImpulse)
{
	using namespace TNTctItemTuning;
	OutImpulse = FVector::ZeroVector;
	const FVector Rel(Victim.X - Center.X, Victim.Y - Center.Y, 0.0);
	const double Dist = Rel.Size();
	if (Dist > AnchorSplashRadius || FMath::Abs(Victim.Z - Center.Z) > AnchorSplashRadius)
	{
		return false;
	}
	const FVector Away = Dist < 1.0 ? FVector::ForwardVector : Rel / Dist;
	const double Near = 1.0 - FMath::Clamp(Dist / AnchorSplashRadius, 0.0, 1.0);
	OutImpulse = Away * (AnchorImpulse * (0.4 + 0.6 * Near)) + FVector::UpVector * AnchorImpulseUp;
	return true;
}
