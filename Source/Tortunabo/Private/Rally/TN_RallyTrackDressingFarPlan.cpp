// Reglas puras del decorado lejano del Rally (#303): piezas grandes en una franja separada del borde del corredor, sobre
// suelo firme y fuera del agua, con presupuesto de piezas y de distancia de dibujado. El suelo llega como consulta
// (FGroundQuery): el actor lo traza sobre el terreno y los tests lo simulan. Tortunabo.Rally.Dressing.Far*.
#include "Rally/TN_RallyTrackDressing.h"

#include "TN_RallyTrackDressingDetail.h"

namespace TNRallyDressing
{
	namespace FarDetail
	{
		/** Índice de la entrada que toca con Roll en [0, 1) según los pesos (INDEX_NONE si ninguna pesa). */
		int32 PickFarEntry(const TArray<FTNRallyFarDecorEntry>& Entries, double Roll)
		{
			double Total = 0.0;
			for (const FTNRallyFarDecorEntry& Entry : Entries)
			{
				Total += FMath::Max(0.f, Entry.Weight);
			}
			if (Total <= 0.0)
			{
				return INDEX_NONE;
			}
			double Cursor = Roll * Total;
			for (int32 Index = 0; Index < Entries.Num(); ++Index)
			{
				Cursor -= FMath::Max(0.f, Entries[Index].Weight);
				if (Cursor < 0.0)
				{
					return Index;
				}
			}
			return Entries.Num() - 1;
		}

		/**
		 * Suelo de una huella: el centro y cuatro puntos de su contorno han de tener suelo firme, por encima del agua y con
		 * poco desnivel entre ellos. Devuelve la cota más baja (la pieza no flota por el lado de abajo).
		 */
		bool FootprintGround(const FTrackData& Track, const FFarDecorParams& Params, const FVector& Center, double RadiusCm,
			FGroundQuery Ground, double& OutZ)
		{
			static const FVector2D Offsets[] = { FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(-1.0, 0.0), FVector2D(0.0, 1.0), FVector2D(0.0, -1.0) };
			const double Ring = RadiusCm * Params.ProbeRadiusFraction;
			double Low = TNumericLimits<double>::Max();
			double High = -TNumericLimits<double>::Max();
			for (const FVector2D& Offset : Offsets)
			{
				double Z = 0.0;
				const FVector Probe(Center.X + Offset.X * Ring, Center.Y + Offset.Y * Ring, Center.Z);
				if (!Ground(Probe, Z) || (Track.bHasWater && Z <= Track.WaterZ + Params.WaterMarginCm))
				{
					return false;
				}
				Low = FMath::Min(Low, Z);
				High = FMath::Max(High, Z);
			}
			if (High - Low > Params.MaxGroundStepCm)
			{
				return false;
			}
			OutZ = Low;
			return true;
		}

		/** Pieza candidata del hueco (Index, Side) en el intento Attempt, sin comprobar dónde cae. */
		FSpot Candidate(const FTrackData& Track, const FBarrierPlan& Plan, const TArray<FTNRallyFarDecorEntry>& Entries, const FFarDecorParams& Params,
			int32 Seed, int32 Index, int32 Side, int32 Attempt, int32 Stride)
		{
			const int32 Salt = 128 + Side * 32 + Attempt * 8;
			FSpot Spot;
			Spot.Kind = ESpotKind::Far;
			Spot.Entry = PickFarEntry(Entries, Detail::Rand01(Seed, Index, Salt));
			if (Spot.Entry == INDEX_NONE)
			{
				return Spot;
			}
			const FTNRallyFarDecorEntry& Def = Entries[Spot.Entry];
			const double MinRadius = FMath::Max(100.0, static_cast<double>(FMath::Min(Def.MinRadiusCm, Def.MaxRadiusCm)));
			const double MaxRadius = FMath::Max(MinRadius, static_cast<double>(Def.MaxRadiusCm));
			Spot.RadiusCm = FMath::Lerp(MinRadius, MaxRadius, Detail::Rand01(Seed, Index, Salt + 1));
			Spot.YawDeg = 360.0 * Detail::Rand01(Seed, Index, Salt + 2);
			Spot.Seed = SubSeed(Seed, Index, Salt + 3);
			// Desde el borde del corredor (el límite si lo hay): el hueco, la huella y un tanto de la franja.
			const FAxisSample& Sample = Track.Samples[Index];
			const double Lateral = Plan.EdgeCm(Side, Index) + Params.MinFromEdgeCm + Spot.RadiusCm + Params.BandCm * Detail::Rand01(Seed, Index, Salt + 4);
			const double Along = (Detail::Rand01(Seed, Index, Salt + 5) - 0.5) * 0.8 * Stride * Track.StepCm;
			Spot.Location = LateralPoint(Sample, Side, Lateral) + Sample.Direction * Along;
			return Spot;
		}
	}

	double FarMinAxisClearanceCm(const FBarrierPlan& Plan, const FFarDecorParams& Params)
	{
		return Plan.BaseOffsetCm + Params.MinFromEdgeCm;
	}

	double FarCullDistanceCm(double RadiusCm, const FFarDecorParams& Params)
	{
		const double Cull = Params.MinCullCm + Params.CullPerRadius * FMath::Max(0.0, RadiusCm);
		return FMath::Clamp(Cull, FMath::Min(Params.MinCullCm, Params.MaxCullCm), Params.MaxCullCm);
	}

	TArray<FSpot> PlanFarDecor(const FTrackData& Track, const FBarrierPlan& Plan, const TArray<FTNRallyFarDecorEntry>& Entries,
		const FFarDecorParams& Params, int32 Seed, FGroundQuery Ground)
	{
		TArray<FSpot> Spots;
		const int32 Num = Track.Samples.Num();
		const int32 DensityStride = Detail::StrideFor(Track, Params.PerKm);
		if (Num < 3 || Params.MaxPieces <= 0 || DensityStride <= 0)
		{
			return Spots;
		}
		// El presupuesto se reparte por todo el trazado: como mucho dos huecos (uno por lado) por cada MaxPieces / 2 tramos.
		const int32 Stride = FMath::Max(DensityStride, FMath::DivideAndRoundUp(2 * Num, Params.MaxPieces));
		const double MinClear = FarMinAxisClearanceCm(Plan, Params);
		const int32 Attempts = FMath::Max(1, Params.Attempts);
		for (int32 Index = 0; Index < Num; Index += Stride)
		{
			for (int32 Side = LeftSide; Side <= RightSide; ++Side)
			{
				for (int32 Attempt = 0; Attempt < Attempts && Spots.Num() < Params.MaxPieces; ++Attempt)
				{
					FSpot Spot = FarDetail::Candidate(Track, Plan, Entries, Params, Seed, Index, Side, Attempt, Stride);
					double GroundZ = 0.0;
					// Ni sobre la pista ni junto a ella (tampoco otro tramo del trazado), sin pisar otra pieza y en tierra firme.
					if (Spot.Entry == INDEX_NONE || !IsClearOfTrack(Track, Spot.Location, MinClear + Spot.RadiusCm)
						|| Detail::OverlapsSpots(Spots, Spot.Location, Spot.RadiusCm)
						|| !FarDetail::FootprintGround(Track, Params, Spot.Location, Spot.RadiusCm, Ground, GroundZ))
					{
						continue;
					}
					Spot.Location.Z = GroundZ;
					Spots.Add(Spot);
					break;
				}
			}
		}
		return Spots;
	}
}
