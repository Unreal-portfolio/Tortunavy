#include "Rally/TN_RallyCircuit.h"

#include "Algo/BinarySearch.h"
#include "Dom/JsonObject.h"
#include "Rally/TN_RallyLogic.h"

namespace TNRallyCircuit
{
	namespace CircuitDetail
	{
		/** Gravedad (cm/s²). */
		constexpr double GravityCms2 = 981.0;

		/** [a, b] de un campo de dos números; false si no viene o no es un par. */
		bool ReadRange(const FJsonObject& Object, const TCHAR* Field, double& OutA, double& OutB)
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Object.TryGetArrayField(Field, Values) || !Values || Values->Num() < 2)
			{
				return false;
			}
			return (*Values)[0]->TryGetNumber(OutA) && (*Values)[1]->TryGetNumber(OutB);
		}

		bool ReadElement(const FJsonObject& Object, FElement& Out)
		{
			FString Type;
			if (!Object.TryGetStringField(TEXT("type"), Type) || !ReadRange(Object, TEXT("s_m"), Out.StartM, Out.EndM))
			{
				return false;
			}
			Out.Kind = KindFromName(Type);
			Object.TryGetStringField(TEXT("id"), Out.Id);
			double Key = -1.0;
			if (Object.TryGetNumberField(TEXT("lip_s_m"), Key) || Object.TryGetNumberField(TEXT("crest_s_m"), Key))
			{
				Out.KeyM = Key;
			}
			double LandingStart = 0.0;
			double LandingEnd = 0.0;
			if (ReadRange(Object, TEXT("landing_s_m"), LandingStart, LandingEnd))
			{
				Out.LandingEndM = LandingEnd;
			}
			Object.TryGetNumberField(TEXT("v_design_kmh"), Out.DesignKmh);
			Object.TryGetNumberField(TEXT("radius_m"), Out.RadiusM);
			return true;
		}

		/** Velocidad (km/h) de una horquilla de radio RadiusM con LateralG de lateral. */
		float HairpinKmh(double RadiusM, float LateralG)
		{
			const double Cms = FMath::Sqrt(GravityCms2 * FMath::Max(0.0, RadiusM) * 100.0 * FMath::Max(0.f, LateralG));
			return static_cast<float>(TNRally::CmsToKmh(Cms));
		}
	}

	EElementKind KindFromName(const FString& Type)
	{
		if (Type == TEXT("recta")) { return EElementKind::Straight; }
		if (Type == TEXT("curva_peraltada")) { return EElementKind::BankedCorner; }
		if (Type == TEXT("horquilla")) { return EElementKind::Hairpin; }
		if (Type == TEXT("chicane")) { return EElementKind::Chicane; }
		if (Type == TEXT("salto")) { return EElementKind::Jump; }
		if (Type == TEXT("rasante")) { return EElementKind::Crest; }
		return EElementKind::Other;
	}

	bool ReadCircuitFields(const FJsonObject& Root, int32 RoadPoints, TArray<double>& OutBankDeg, TArray<FElement>& OutElements,
		FString& OutError)
	{
		OutBankDeg.Reset();
		OutElements.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Bank = nullptr;
		if (Root.TryGetArrayField(TEXT("bank_deg"), Bank) && Bank)
		{
			OutBankDeg.Reserve(Bank->Num());
			for (const TSharedPtr<FJsonValue>& Value : *Bank)
			{
				double Deg = 0.0;
				if (!Value.IsValid() || !Value->TryGetNumber(Deg))
				{
					OutError = TEXT("bank_deg con un valor que no es un número");
					return false;
				}
				OutBankDeg.Add(Deg);
			}
			if (OutBankDeg.Num() != RoadPoints)
			{
				UE_LOG(LogTNRally, Warning, TEXT("[RallyCircuit] bank_deg trae %d valores y road_uu %d puntos: se ignora el peralte."),
					OutBankDeg.Num(), RoadPoints);
				OutBankDeg.Reset();
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Elements = nullptr;
		if (Root.TryGetArrayField(TEXT("elements"), Elements) && Elements)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Elements)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				FElement Element;
				if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object || !Object->IsValid()
					|| !CircuitDetail::ReadElement(**Object, Element))
				{
					OutError = TEXT("elements con una entrada sin type o sin s_m");
					return false;
				}
				OutElements.Add(MoveTemp(Element));
			}
		}
		return true;
	}

	FRotator GateRotation(double YawDeg, double BankDeg)
	{
		// En Unreal, un alabeo (Roll) positivo baja el eje Y (la derecha): el peralte positivo del manifest es el mismo signo.
		return FRotator(0.0, YawDeg, FMath::Clamp(BankDeg, -45.0, 45.0));
	}

	TArray<double> RoadPointArcs(const TArray<FVector>& Road, bool bClosed, double SplineLengthCm, double& OutRoadLengthCm)
	{
		TArray<double> Arcs;
		OutRoadLengthCm = 0.0;
		if (Road.Num() < 2)
		{
			return Arcs;
		}
		Arcs.Reserve(Road.Num());
		double Cumulative = 0.0;
		Arcs.Add(0.0);
		for (int32 Index = 1; Index < Road.Num(); ++Index)
		{
			Cumulative += FVector::Dist(Road[Index - 1], Road[Index]);
			Arcs.Add(Cumulative);
		}
		OutRoadLengthCm = Cumulative + (bClosed ? FVector::Dist(Road.Last(), Road[0]) : 0.0);
		const double Scale = OutRoadLengthCm > KINDA_SMALL_NUMBER && SplineLengthCm > 0.0 ? SplineLengthCm / OutRoadLengthCm : 1.0;
		for (double& Arc : Arcs)
		{
			Arc *= Scale;
		}
		return Arcs;
	}

	double RoadMetersToArc(double Meters, double RoadLengthCm, double SplineLengthCm, bool bClosed)
	{
		const double Scale = RoadLengthCm > KINDA_SMALL_NUMBER && SplineLengthCm > 0.0 ? SplineLengthCm / RoadLengthCm : 1.0;
		return TNRally::WrapArc(Meters * 100.0 * Scale, SplineLengthCm, bClosed);
	}

	TArray<FFeatureArc> ToFeatureArcs(TConstArrayView<FElement> Elements, double RoadLengthCm, double SplineLengthCm, bool bClosed)
	{
		TArray<FFeatureArc> Out;
		Out.Reserve(Elements.Num());
		for (const FElement& Element : Elements)
		{
			FFeatureArc Feature;
			Feature.Kind = Element.Kind;
			Feature.StartCm = RoadMetersToArc(Element.StartM, RoadLengthCm, SplineLengthCm, bClosed);
			Feature.EndCm = RoadMetersToArc(Element.EndM, RoadLengthCm, SplineLengthCm, bClosed);
			Feature.KeyCm = Element.KeyM >= 0.0 ? RoadMetersToArc(Element.KeyM, RoadLengthCm, SplineLengthCm, bClosed) : Feature.StartCm;
			Feature.DesignKmh = Element.DesignKmh;
			Feature.RadiusM = Element.RadiusM;
			Out.Add(Feature);
		}
		return Out;
	}

	double BankAtArc(TConstArrayView<double> SampleArcs, TConstArrayView<double> BankDeg, double Arc, double LengthCm, bool bClosed)
	{
		const int32 Num = FMath::Min(SampleArcs.Num(), BankDeg.Num());
		if (Num == 0)
		{
			return 0.0;
		}
		if (Num == 1 || LengthCm <= 0.0)
		{
			return BankDeg[0];
		}
		const double S = TNRally::WrapArc(Arc, LengthCm, bClosed);
		// Último índice con arco <= S.
		const int32 Upper = Algo::UpperBound(SampleArcs.Left(Num), S);
		const int32 Below = FMath::Max(0, Upper - 1);
		if (Below >= Num - 1)
		{
			if (!bClosed)
			{
				return BankDeg[Num - 1];
			}
			// Entre el último punto y el primero, que cierra el lazo.
			const double Span = LengthCm - SampleArcs[Num - 1];
			const double Alpha = Span > KINDA_SMALL_NUMBER ? (S - SampleArcs[Num - 1]) / Span : 0.0;
			return FMath::Lerp(BankDeg[Num - 1], BankDeg[0], FMath::Clamp(Alpha, 0.0, 1.0));
		}
		const double Span = SampleArcs[Below + 1] - SampleArcs[Below];
		const double Alpha = Span > KINDA_SMALL_NUMBER ? (S - SampleArcs[Below]) / Span : 0.0;
		return FMath::Lerp(BankDeg[Below], BankDeg[Below + 1], FMath::Clamp(Alpha, 0.0, 1.0));
	}

	float FeatureSpeedLimitKmh(TConstArrayView<FFeatureArc> Features, double ArcCm, double LengthCm, bool bClosed, double ProbeCm,
		double DecelCms2, float MaxKmh, const FBrakeTuning& Tuning)
	{
		float Limit = MaxKmh;
		if (LengthCm <= 0.0)
		{
			return Limit;
		}
		for (const FFeatureArc& Feature : Features)
		{
			float TargetKmh = 0.f;
			double DistanceCm = 0.0;
			if (Feature.Kind == EElementKind::Jump && Feature.DesignKmh > 0.0)
			{
				TargetKmh = static_cast<float>(Feature.DesignKmh) * Tuning.JumpLipSpeedFactor;
				DistanceCm = TNRally::ForwardArc(ArcCm, Feature.KeyCm, LengthCm, bClosed);
			}
			else if (Feature.Kind == EElementKind::Hairpin && Feature.RadiusM > 0.0)
			{
				TargetKmh = CircuitDetail::HairpinKmh(Feature.RadiusM, Tuning.HairpinLateralG);
				const double Into = TNRally::ForwardArc(Feature.StartCm, ArcCm, LengthCm, bClosed);
				const bool bInside = Into >= 0.0 && Into <= TNRally::ForwardArc(Feature.StartCm, Feature.EndCm, LengthCm, bClosed);
				DistanceCm = bInside ? 0.0 : TNRally::ForwardArc(ArcCm, Feature.StartCm, LengthCm, bClosed);
			}
			else
			{
				continue;
			}
			if (DistanceCm < 0.0 || DistanceCm > ProbeCm)
			{
				continue;
			}
			const float Floor = FMath::Max(Tuning.MinKmh, TargetKmh);
			Limit = FMath::Min(Limit, TNRally::ApproachSpeedKmh(Floor, DistanceCm, DecelCms2));
		}
		return Limit;
	}
}
