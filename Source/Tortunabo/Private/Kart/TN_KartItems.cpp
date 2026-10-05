#include "Kart/TN_KartItems.h"
#include "Rally/TN_RallyLogic.h"
#include "Vehicles/TN_RallyTurretLogic.h"

namespace TNKart
{
	namespace
	{
		/** Pesos de la primera (Lead) y de la última (Last); el resto, en medio según el puesto. */
		struct FItemCurve
		{
			ETNKartItem Item;
			float Lead;
			float Last;
		};

		constexpr FItemCurve ItemCurves[] = {
			{ ETNKartItem::Coco, 26.f, 14.f },
			{ ETNKartItem::TripleCoco, 0.f, 18.f },
			{ ETNKartItem::Concha, 30.f, 12.f },
			{ ETNKartItem::ConchaGuiada, 6.f, 22.f },
			{ ETNKartItem::Alga, 38.f, 4.f },
			{ ETNKartItem::Tinta, 0.f, 12.f },
			{ ETNKartItem::Estrella, 0.f, 18.f },
			// #774: el mortero, el arpón y la medusa, más para los de atrás; el pez globo y los erizos, para los de delante. La
			// suma de las cinco no cambia con el puesto (44): el reparto de los demás sigue igual de equilibrado.
			{ ETNKartItem::Mortero, 0.f, 12.f },
			{ ETNKartItem::Erizos, 18.f, 6.f },
			{ ETNKartItem::Medusa, 4.f, 14.f },
			{ ETNKartItem::PezGlobo, 22.f, 4.f },
			{ ETNKartItem::Arpon, 0.f, 8.f },
		};

		/** Pasado esto con el objeto en la mano, el bot lo usa aunque no venga a cuento (s). */
		constexpr float BotMaxHoldSeconds = 8.f;
		constexpr float BotShellRangeCm = 6000.f;
		constexpr float BotAlgaRangeCm = 4000.f;
		constexpr float BotBoostDelaySeconds = 1.5f;
	}

	float FItemWeights::Total() const
	{
		float Sum = 0.f;
		for (const float Weight : Weights)
		{
			Sum += FMath::Max(0.f, Weight);
		}
		return Sum;
	}

	FItemWeights ItemWeightsForPlace(int32 Place, int32 NumKarts)
	{
		FItemWeights Out;
		const int32 Karts = FMath::Max(1, NumKarts);
		// 0 = en cabeza, 1 = la última; sola en la carrera cuenta como en cabeza.
		const float Back = Karts > 1 ? FMath::Clamp(static_cast<float>(Place - 1) / static_cast<float>(Karts - 1), 0.f, 1.f) : 0.f;
		for (const FItemCurve& Curve : ItemCurves)
		{
			Out.Weights[static_cast<int32>(Curve.Item)] = FMath::Lerp(Curve.Lead, Curve.Last, Back);
		}
		// La primera no tiene a nadie delante a quien manchar de tinta ni le hace falta la estrella.
		if (Place <= 1)
		{
			Out.Weights[static_cast<int32>(ETNKartItem::Tinta)] = 0.f;
			Out.Weights[static_cast<int32>(ETNKartItem::Estrella)] = 0.f;
			// Ni a quién clavarle el arpón (#774).
			Out.Weights[static_cast<int32>(ETNKartItem::Arpon)] = 0.f;
		}
		return Out;
	}

	ETNKartItem PickItem(const FItemWeights& Weights, float Roll01)
	{
		const float Total = Weights.Total();
		if (Total <= 0.f)
		{
			return ETNKartItem::None;
		}
		float Remaining = FMath::Clamp(Roll01, 0.f, 0.9999f) * Total;
		ETNKartItem Last = ETNKartItem::None;
		for (int32 Index = 1; Index < ItemKinds; ++Index)
		{
			const float Weight = FMath::Max(0.f, Weights.Weights[Index]);
			if (Weight <= 0.f)
			{
				continue;
			}
			Last = static_cast<ETNKartItem>(Index);
			if (Remaining < Weight)
			{
				return Last;
			}
			Remaining -= Weight;
		}
		return Last;
	}

	int32 ItemCharges(ETNKartItem Item)
	{
		switch (Item)
		{
		case ETNKartItem::None:
		case ETNKartItem::Count:
			return 0;
		case ETNKartItem::TripleCoco:
			return 3;
		default:
			return 1;
		}
	}

	FText ItemName(ETNKartItem Item)
	{
		switch (Item)
		{
		case ETNKartItem::Coco: return NSLOCTEXT("Karts", "ItemCoco", "Coco turbo");
		case ETNKartItem::TripleCoco: return NSLOCTEXT("Karts", "ItemTripleCoco", "Triple coco turbo");
		case ETNKartItem::Concha: return NSLOCTEXT("Karts", "ItemConcha", "Concha");
		case ETNKartItem::ConchaGuiada: return NSLOCTEXT("Karts", "ItemConchaGuiada", "Concha teledirigida");
		case ETNKartItem::Alga: return NSLOCTEXT("Karts", "ItemAlga", "Alga resbaladiza");
		case ETNKartItem::Tinta: return NSLOCTEXT("Karts", "ItemTinta", "Tinta de calamar");
		case ETNKartItem::Estrella: return NSLOCTEXT("Karts", "ItemEstrella", "Estrella de mar");
		case ETNKartItem::Mortero: return NSLOCTEXT("Karts", "ItemMortero", "Mortero");
		case ETNKartItem::Erizos: return NSLOCTEXT("Karts", "ItemErizos", "Ráfaga de erizos");
		case ETNKartItem::Medusa: return NSLOCTEXT("Karts", "ItemMedusa", "Medusa saltarina");
		case ETNKartItem::PezGlobo: return NSLOCTEXT("Karts", "ItemPezGlobo", "Pez globo");
		case ETNKartItem::Arpon: return NSLOCTEXT("Karts", "ItemArpon", "Arpón");
		default: return FText::GetEmpty();
		}
	}


	bool ShouldBotUseItem(ETNKartItem Item, float HeldSeconds, float AheadCm, float BehindCm, bool bHopThreat)
	{
		if (Item == ETNKartItem::None || Item == ETNKartItem::Count)
		{
			return false;
		}
		if (HeldSeconds >= BotMaxHoldSeconds)
		{
			return true;
		}
		switch (Item)
		{
		case ETNKartItem::Concha:
		case ETNKartItem::ConchaGuiada:
			return AheadCm >= 0.f && AheadCm <= BotShellRangeCm;
		case ETNKartItem::Alga:
			return BehindCm >= 0.f && BehindCm <= BotAlgaRangeCm;
		case ETNKartItem::Tinta:
			return AheadCm >= 0.f;
		// #774, como los bots de la torreta del Rally.
		case ETNKartItem::Mortero:
			return AheadCm >= 0.f;
		case ETNKartItem::Erizos:
			return AheadCm >= 0.f && AheadCm <= TNRally::BotErizosRangeCm;
		case ETNKartItem::Arpon:
			return TNRallyTurret::BotHarpoonInRange(AheadCm);
		case ETNKartItem::PezGlobo:
			return BehindCm >= 0.f && BehindCm <= TNRallyTurret::BotPufferBehindCm;
		case ETNKartItem::Medusa:
			return bHopThreat || HeldSeconds >= TNRally::BotJellyfishDelaySeconds;
		default:
			return HeldSeconds >= BotBoostDelaySeconds;
		}
	}
}

namespace TNKart
{
	float MortarFlightSeconds(float DistanceCm)
	{
		return FMath::Clamp(FMath::Max(DistanceCm, 0.f) / MortarRangeCmPerSecond, MortarMinFlightSeconds, MortarMaxFlightSeconds);
	}

	FVector MortarLaunchVelocity(const FVector& Start, const FVector& Target, float GravityZ, float FlightSeconds)
	{
		const float T = FMath::Max(FlightSeconds, KINDA_SMALL_NUMBER);
		return (Target - Start) / T - FVector(0.f, 0.f, 0.5f * GravityZ * T);
	}
}
