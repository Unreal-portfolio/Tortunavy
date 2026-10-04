// Reglas puras del decorado del trazado del Rally (TNRallyDressing): curvatura, límites, reparto del decorado y del
// público, y el muestreo de ATN_RallyTrack. Sin actores ni trazas: las prueban los tests Tortunabo.Rally.Dressing.*.
#include "Rally/TN_RallyTrackDressing.h"

#include "Rally/TN_RallyTrack.h"
#include "TN_RallyTrackDressingDetail.h"

namespace TNRallyDressing
{
	namespace Detail
	{
		uint32 Hash32(int32 Seed, int32 A, int32 B)
		{
			uint32 H = static_cast<uint32>(Seed) * 0x9E3779B1u + 0x7F4A7C15u;
			H ^= static_cast<uint32>(A) * 0x85EBCA6Bu + (H << 6) + (H >> 2);
			H ^= static_cast<uint32>(B) * 0xC2B2AE35u + (H << 6) + (H >> 2);
			H ^= H >> 16;
			H *= 0x7FEB352Du;
			H ^= H >> 15;
			H *= 0x846CA68Bu;
			H ^= H >> 16;
			return H;
		}

		/** Número estable en [0, 1) por semilla y dos índices: igual en todas las máquinas y sin depender del orden. */
		double Rand01(int32 Seed, int32 A, int32 B)
		{
			return static_cast<double>(Hash32(Seed, A, B) & 0xFFFFFFu) / 16777216.0;
		}

		int32 WrapIndex(int32 Index, int32 Num, bool bClosed)
		{
			if (Num <= 0)
			{
				return INDEX_NONE;
			}
			if (bClosed)
			{
				return ((Index % Num) + Num) % Num;
			}
			return Index >= 0 && Index < Num ? Index : INDEX_NONE;
		}

		FVector RightOf(const FVector& Direction)
		{
			return FVector(-Direction.Y, Direction.X, 0.0).GetSafeNormal();
		}

		double YawRadians(const FVector& Direction)
		{
			return FMath::Atan2(Direction.Y, Direction.X);
		}

		double ArcDistance(double A, double B, double Length, bool bClosed)
		{
			const double Delta = FMath::Abs(A - B);
			return bClosed ? FMath::Min(Delta, Length - Delta) : Delta;
		}

		bool GapCovers(const FTNRallyDressingGap& Gap, double Arc, int32 Side, bool bClosed)
		{
			const bool bSide = Gap.Side == 0 || (Gap.Side < 0) == (Side == LeftSide);
			if (!bSide)
			{
				return false;
			}
			if (Gap.StartArcCm <= Gap.EndArcCm)
			{
				return Arc >= Gap.StartArcCm && Arc <= Gap.EndArcCm;
			}
			return bClosed && (Arc >= Gap.StartArcCm || Arc <= Gap.EndArcCm);
		}

		/** Cada marca se extiende Radius muestras a cada lado. */
		TArray<bool> Dilated(const TArray<bool>& Flags, int32 Radius, bool bClosed)
		{
			TArray<bool> Out = Flags;
			const int32 Num = Flags.Num();
			for (int32 Index = 0; Index < Num && Radius > 0; ++Index)
			{
				if (!Flags[Index])
				{
					continue;
				}
				for (int32 Delta = -Radius; Delta <= Radius; ++Delta)
				{
					const int32 Other = WrapIndex(Index + Delta, Num, bClosed);
					if (Other != INDEX_NONE)
					{
						Out[Other] = true;
					}
				}
			}
			return Out;
		}

		/** Cierra los huecos de MaxHole muestras o menos entre dos marcas (en punto a punto, no los de los extremos). */
		TArray<bool> FilledHoles(const TArray<bool>& Flags, int32 MaxHole, bool bClosed)
		{
			TArray<bool> Out = Flags;
			const int32 Num = Flags.Num();
			const int32 Start = Flags.IndexOfByKey(true);
			if (MaxHole <= 0 || Start == INDEX_NONE)
			{
				return Out;
			}
			const int32 Steps = bClosed ? Num : Num - 1 - Start;
			int32 HoleBegin = INDEX_NONE;
			int32 HoleLength = 0;
			for (int32 Step = 1; Step <= Steps; ++Step)
			{
				const int32 Index = (Start + Step) % Num;
				if (!Flags[Index])
				{
					HoleBegin = HoleLength == 0 ? Index : HoleBegin;
					++HoleLength;
					continue;
				}
				for (int32 Offset = 0; HoleLength <= MaxHole && Offset < HoleLength; ++Offset)
				{
					Out[(HoleBegin + Offset) % Num] = true;
				}
				HoleLength = 0;
			}
			return Out;
		}

		/** Media de los desplazamientos con límite en la ventana (los que no tienen límite no cuentan). */
		TArray<double> SmoothedOffsets(const TArray<double>& Offsets, int32 Radius, bool bClosed)
		{
			const int32 Num = Offsets.Num();
			TArray<double> Out;
			Out.Init(0.0, Num);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				if (Offsets[Index] <= 0.0)
				{
					continue;
				}
				double Sum = 0.0;
				int32 Count = 0;
				for (int32 Delta = -Radius; Delta <= Radius; ++Delta)
				{
					const int32 Other = WrapIndex(Index + Delta, Num, bClosed);
					if (Other != INDEX_NONE && Offsets[Other] > 0.0)
					{
						Sum += Offsets[Other];
						++Count;
					}
				}
				Out[Index] = Sum / FMath::Max(1, Count);
			}
			return Out;
		}

		/**
		 * Si otro tramo del trazado (horquilla, cruce) pasa por el lado Side, el límite no puede meterse en su corredor: se
		 * lleva a la mitad entre los dos ejes o, si ni así cabe la calzada propia, se quita (0).
		 */
		double CrowdedOffset(const FTrackData& Track, int32 Index, int32 Side, double OffsetCm, double RoadHalfCm, const FBarrierParams& Params)
		{
			const FAxisSample& Here = Track.Samples[Index];
			const FVector Outward = RightOf(Here.Direction) * SideSign(Side);
			double Nearest = TNumericLimits<double>::Max();
			for (const FAxisSample& Other : Track.Samples)
			{
				if (ArcDistance(Here.Arc, Other.Arc, Track.LengthCm, Track.bClosed) < Params.OtherSectionExcludeArcCm
					|| FMath::Abs(Other.Location.Z - Here.Location.Z) > Params.OtherSectionMaxDzCm)
				{
					continue;
				}
				const FVector Flat(Other.Location.X - Here.Location.X, Other.Location.Y - Here.Location.Y, 0.0);
				const double Across = FVector::DotProduct(Flat, Outward);
				const double Along = FMath::Abs(FVector::DotProduct(Flat, Here.Direction));
				if (Across > 0.0 && Along <= Track.StepCm + OffsetCm)
				{
					Nearest = FMath::Min(Nearest, Across);
				}
			}
			if (Nearest - OffsetCm >= RoadHalfCm + Params.OtherSectionMarginCm)
			{
				return OffsetCm;
			}
			const double Middle = 0.5 * Nearest;
			// Con la barrera continua (#303) basta con que no pise la calzada: un hueco ahí uniría los dos tramos.
			const double MinMiddle = RoadHalfCm + (Params.bContinuous ? 0.0 : Params.InsideMinMarginCm);
			return Middle >= MinMiddle ? FMath::Min(OffsetCm, Middle) : 0.0;
		}

		/** Tramos seguidos con límite, en el orden de la carrera; en circuito, un tramo que cruza la salida es uno solo. */
		TArray<TArray<int32>> CollectRuns(const TArray<double>& Offsets, bool bClosed)
		{
			TArray<TArray<int32>> Runs;
			const int32 Num = Offsets.Num();
			int32 Start = 0;
			if (bClosed)
			{
				Start = Offsets.IndexOfByPredicate([](double Value) { return Value <= 0.0; });
				if (Start == INDEX_NONE)
				{
					TArray<int32>& All = Runs.AddDefaulted_GetRef();
					for (int32 Index = 0; Index < Num; ++Index)
					{
						All.Add(Index);
					}
					return Runs;
				}
			}
			bool bInRun = false;
			for (int32 Step = 0; Step < Num; ++Step)
			{
				const int32 Index = (Start + Step) % Num;
				if (Offsets[Index] <= 0.0)
				{
					bInRun = false;
					continue;
				}
				if (!bInRun)
				{
					Runs.AddDefaulted();
					bInRun = true;
				}
				Runs.Last().Add(Index);
			}
			return Runs;
		}

		struct FSideFlags
		{
			TArray<bool> Limited;
			TArray<bool> Drop;
		};

		/** Qué muestras llevan límite en un lado: curvas y caídas alargadas, huecos cortos cerrados y atajos abiertos. */
		FSideFlags SideFlags(const FTrackData& Track, const TArray<double>& Curvature, const TArray<uint8>& DropMask, int32 Side,
			const FBarrierParams& Params)
		{
			const int32 Num = Track.Samples.Num();
			const double Step = FMath::Max(1.0, Track.StepCm);
			TArray<bool> Curves;
			TArray<bool> Drops;
			Curves.Init(false, Num);
			Drops.Init(false, Num);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				Curves[Index] = FMath::Abs(Curvature[Index]) >= Params.CurveMinCurvature;
				Drops[Index] = DropMask.IsValidIndex(Index) && (DropMask[Index] & (1u << Side)) != 0;
			}
			FSideFlags Flags;
			Flags.Drop = Dilated(Drops, FMath::CeilToInt32(Params.DropLeadCm / Step), Track.bClosed);
			const TArray<bool> LongCurves = Dilated(Curves, FMath::CeilToInt32(Params.CurveLeadCm / Step), Track.bClosed);
			TArray<bool> Limited;
			Limited.Init(false, Num);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				// Continua (#303): todo el trazado; las curvas y las caídas solo deciden a qué distancia va.
				Limited[Index] = Params.bContinuous || LongCurves[Index] || Flags.Drop[Index];
			}
			Flags.Limited = FilledHoles(Limited, FMath::FloorToInt32(Params.MinGapCm / Step), Track.bClosed);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				for (const FTNRallyDressingGap& Gap : Track.Gaps)
				{
					Flags.Limited[Index] &= !GapCovers(Gap, Track.Samples[Index].Arc, Side, Track.bClosed);
				}
			}
			return Flags;
		}

		/** Desplazamiento de cada muestra con límite: el de la curva (o el borde en una caída), suavizado y sin pisar otro tramo. */
		FBarrierSide SideBarrier(const FTrackData& Track, const FBarrierPlan& Plan, const FSideFlags& Flags, int32 Side,
			const FBarrierParams& Params)
		{
			const int32 Num = Track.Samples.Num();
			TArray<double> Raw;
			Raw.Init(0.0, Num);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				if (!Flags.Limited[Index])
				{
					continue;
				}
				const double Curve = BarrierOffsetCm(Plan.Curvature[Index], Side, Plan.RoadHalfCm, Params);
				Raw[Index] = Flags.Drop[Index] ? FMath::Min(Curve, Plan.RoadHalfCm + Params.DropEdgeMarginCm) : Curve;
			}
			const int32 SmoothRadius = FMath::RoundToInt32(0.5 * Params.SmoothWindowCm / FMath::Max(1.0, Track.StepCm));
			const TArray<double> Smooth = SmoothedOffsets(Raw, SmoothRadius, Track.bClosed);
			FBarrierSide Result;
			Result.OffsetCm.Init(0.0, Num);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				if (Smooth[Index] > 0.0)
				{
					Result.OffsetCm[Index] = CrowdedOffset(Track, Index, Side, Smooth[Index], Plan.RoadHalfCm, Params);
				}
			}
			Result.Runs = CollectRuns(Result.OffsetCm, Track.bClosed);
			return Result;
		}

		int32 StrideFor(const FTrackData& Track, double PerKm)
		{
			if (PerKm <= 0.0)
			{
				return 0;
			}
			return FMath::Max(1, FMath::RoundToInt32(100000.0 / PerKm / FMath::Max(1.0, Track.StepCm)));
		}

		int32 PickEntry(const TArray<FTNRallyDecorEntry>& Entries, double Roll)
		{
			double Total = 0.0;
			for (const FTNRallyDecorEntry& Entry : Entries)
			{
				Total += FMath::Max(0.f, Entry.Weight);
			}
			double Cursor = Roll * Total;
			for (int32 Index = 0; Index < Entries.Num() && Total > 0.0; ++Index)
			{
				Cursor -= FMath::Max(0.f, Entries[Index].Weight);
				if (Cursor < 0.0)
				{
					return Index;
				}
			}
			return Total > 0.0 ? Entries.Num() - 1 : INDEX_NONE;
		}

		bool OverlapsSpots(const TArray<FSpot>& Spots, const FVector& Location, double RadiusCm)
		{
			return Spots.ContainsByPredicate([&](const FSpot& Spot)
			{
				return FVector::Dist2D(Spot.Location, Location) < Spot.RadiusCm + RadiusCm;
			});
		}

		/** Coloca Spot a Lateral del eje y Along a lo largo si queda fuera del corredor y no pisa otra pieza. */
		bool TryAddSpot(const FTrackData& Track, const FBarrierPlan& Plan, double ClearanceCm, FSpot Spot, int32 Index, int32 Side,
			double Lateral, double Along, TArray<FSpot>& Spots)
		{
			const FAxisSample& Sample = Track.Samples[Index];
			Spot.Location = LateralPoint(Sample, Side, Lateral) + Sample.Direction * Along;
			if (!IsClearOfTrack(Track, Spot.Location, Plan.BaseOffsetCm + ClearanceCm + Spot.RadiusCm)
				|| OverlapsSpots(Spots, Spot.Location, Spot.RadiusCm))
			{
				return false;
			}
			Spots.Add(Spot);
			return true;
		}

		void AddBeachSpots(const FTrackData& Track, const FBarrierPlan& Plan, const TArray<FTNRallyDecorEntry>& Entries,
			const FDecorParams& Params, int32 Seed, TArray<FSpot>& Spots)
		{
			const int32 Stride = StrideFor(Track, Params.BeachPerKm);
			for (int32 Index = 0; Stride > 0 && Index < Track.Samples.Num(); Index += Stride)
			{
				for (int32 Side = LeftSide; Side <= RightSide; ++Side)
				{
					const int32 Salt = Side * 16;
					const int32 Entry = PickEntry(Entries, Rand01(Seed, Index, Salt));
					if (Entry == INDEX_NONE)
					{
						continue;
					}
					const FTNRallyDecorEntry& Def = Entries[Entry];
					FSpot Spot;
					Spot.Kind = ESpotKind::Beach;
					Spot.Entry = Entry;
					Spot.Size = FMath::Lerp(FMath::Min(Def.MinSize, Def.MaxSize), FMath::Max(Def.MinSize, Def.MaxSize), static_cast<float>(Rand01(Seed, Index, Salt + 1)));
					Spot.RadiusCm = TNBeach::FootprintRadius(Def.Element) * Spot.Size;
					Spot.YawDeg = 360.0 * Rand01(Seed, Index, Salt + 4);
					Spot.Seed = SubSeed(Seed, Index, Salt + 5);
					const double Lateral = Plan.EdgeCm(Side, Index) + Params.ClearanceCm + Spot.RadiusCm + Params.BandCm * Rand01(Seed, Index, Salt + 2);
					const double Along = (Rand01(Seed, Index, Salt + 3) - 0.5) * 0.6 * Stride * Track.StepCm;
					TryAddSpot(Track, Plan, Params.ClearanceCm, Spot, Index, Side, Lateral, Along, Spots);
				}
			}
		}

		void AddCrabSpots(const FTrackData& Track, const FBarrierPlan& Plan, const FDecorParams& Params, int32 Seed, TArray<FSpot>& Spots)
		{
			const int32 Stride = StrideFor(Track, Params.CrabsPerKm);
			const double Clearance = 0.5 * Params.ClearanceCm;
			for (int32 Index = 0; Stride > 0 && Index < Track.Samples.Num(); Index += Stride)
			{
				for (int32 Side = LeftSide; Side <= RightSide; ++Side)
				{
					const int32 Salt = 64 + Side * 16;
					FSpot Spot;
					Spot.Kind = ESpotKind::Crab;
					Spot.Size = static_cast<float>(0.8 + 0.4 * Rand01(Seed, Index, Salt));
					Spot.RadiusCm = Params.CrabRadiusCm * Spot.Size;
					Spot.YawDeg = 360.0 * Rand01(Seed, Index, Salt + 1);
					Spot.Seed = SubSeed(Seed, Index, Salt + 2);
					const double Lateral = Plan.EdgeCm(Side, Index) + Clearance + Spot.RadiusCm + 0.5 * Params.BandCm * Rand01(Seed, Index, Salt + 3);
					const double Along = (Rand01(Seed, Index, Salt + 4) - 0.5) * Stride * Track.StepCm;
					TryAddSpot(Track, Plan, Clearance, Spot, Index, Side, Lateral, Along, Spots);
				}
			}
		}

		/** Grupo de público en dos filas, detrás del límite del lado Side y mirando a la calzada. */
		void AddSpectatorGroup(const FTrackData& Track, const FBarrierPlan& Plan, const FDecorParams& Params, int32 Index, int32 Side,
			int32 Count, int32 Seed, int32 Group, TArray<FSpot>& Spots)
		{
			const FAxisSample& Sample = Track.Samples[Index];
			const int32 Columns = FMath::Max(1, (Count + 1) / 2);
			const FVector Facing = -RightOf(Sample.Direction) * SideSign(Side);
			const double FacingYaw = FMath::RadiansToDegrees(YawRadians(Facing));
			for (int32 Slot = 0; Slot < Count; ++Slot)
			{
				const int32 Salt = Group * 64 + Slot;
				FSpot Spot;
				Spot.Kind = ESpotKind::Spectator;
				Spot.Entry = static_cast<int32>(Hash32(Seed, Salt, 7) % static_cast<uint32>(FMath::Max(1, Params.SpectatorVariants)));
				Spot.RadiusCm = 0.4 * Params.SpectatorSpacingCm;
				Spot.Size = static_cast<float>(0.85 + 0.3 * Rand01(Seed, Salt, 8));
				Spot.YawDeg = FacingYaw + 30.0 * (Rand01(Seed, Salt, 9) - 0.5);
				Spot.Seed = SubSeed(Seed, Salt, 10);
				const double Row = Slot / Columns;
				const double Column = (Slot % Columns) - 0.5 * (Columns - 1);
				const double Lateral = Plan.EdgeCm(Side, Index) + Params.SpectatorSetbackCm + Row * Params.SpectatorSpacingCm;
				Spot.Location = LateralPoint(Sample, Side, Lateral) + Sample.Direction * (Column * Params.SpectatorSpacingCm);
				if (IsClearOfTrack(Track, Spot.Location, Plan.BaseOffsetCm))
				{
					Spots.Add(Spot);
				}
			}
		}

		int32 NearestSample(const FTrackData& Track, const FVector& Location)
		{
			int32 Best = INDEX_NONE;
			double BestDistSq = TNumericLimits<double>::Max();
			for (int32 Index = 0; Index < Track.Samples.Num(); ++Index)
			{
				const double DistSq = FVector::DistSquared(Track.Samples[Index].Location, Location);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					Best = Index;
				}
			}
			return Best;
		}
	}

	int32 SubSeed(int32 Seed, int32 A, int32 B)
	{
		return static_cast<int32>(Detail::Hash32(Seed, A, B) & 0x7FFFFFFFu);
	}

	double RoadHalfWidthCm(const FTrackData& Track, const FBarrierParams& Params)
	{
		return 0.5 * (Track.RoadWidthCm > 0.0 ? Track.RoadWidthCm : Params.DefaultRoadWidthCm);
	}

	double BaseOffsetCm(double RoadHalfCm, const FBarrierParams& Params)
	{
		if (Params.bHugRoad)
		{
			return RoadHalfCm + FMath::Max(0.0, Params.RoadEdgeMarginCm);
		}
		return FMath::Max(RoadHalfCm + Params.ShoulderCm, Params.MinOffsetCm);
	}

	TArray<double> SignedCurvature(const TArray<FAxisSample>& Samples, bool bClosed, double WindowCm)
	{
		const int32 Num = Samples.Num();
		TArray<double> Out;
		Out.Init(0.0, Num);
		if (Num < 3)
		{
			return Out;
		}
		const double Step = FMath::Max(1.0, FMath::Abs(Samples[1].Arc - Samples[0].Arc));
		const int32 Half = FMath::Max(1, FMath::RoundToInt32(0.5 * WindowCm / Step));
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const int32 A = bClosed ? Detail::WrapIndex(Index - Half, Num, true) : FMath::Max(0, Index - Half);
			const int32 B = bClosed ? Detail::WrapIndex(Index + Half, Num, true) : FMath::Min(Num - 1, Index + Half);
			const int32 Span = bClosed ? 2 * Half : B - A;
			if (Span > 0)
			{
				const double Turn = FMath::FindDeltaAngleRadians(Detail::YawRadians(Samples[A].Direction), Detail::YawRadians(Samples[B].Direction));
				Out[Index] = Turn / (Span * Step);
			}
		}
		return Out;
	}

	double BarrierOffsetCm(double Curvature, int32 Side, double RoadHalfCm, const FBarrierParams& Params)
	{
		const double Base = BaseOffsetCm(RoadHalfCm, Params);
		if (Params.bHugRoad)
		{
			// Pegada al borde (#303): la barrera dibuja la calzada, también en las curvas.
			return Base;
		}
		const double Magnitude = FMath::Abs(Curvature);
		const bool bInside = Magnitude > UE_DOUBLE_SMALL_NUMBER && ((Curvature > 0.0) == (Side == RightSide));
		if (!bInside)
		{
			// Por fuera, escapatoria: el buggy que se abre tiene sitio antes de tocar el límite.
			return Base + Params.MaxRunoffCm * FMath::SmoothStep(Params.CurveMinCurvature, Params.CurveFullCurvature, Magnitude);
		}
		// Por dentro, sin pasar del centro de la curva (las cerradas se cruzarían) ni comerse la calzada.
		const double Room = Params.InsideRadiusFraction / Magnitude;
		return FMath::Max(RoadHalfCm + Params.InsideMinMarginCm, FMath::Min(Base, Room));
	}

	FVector LateralPoint(const FAxisSample& Sample, int32 Side, double OffsetCm)
	{
		return Sample.Location + Detail::RightOf(Sample.Direction) * (SideSign(Side) * OffsetCm);
	}

	FBarrierPlan PlanBarriers(const FTrackData& Track, const TArray<uint8>& DropMask, const FBarrierParams& Params)
	{
		FBarrierPlan Plan;
		Plan.RoadHalfCm = RoadHalfWidthCm(Track, Params);
		Plan.BaseOffsetCm = BaseOffsetCm(Plan.RoadHalfCm, Params);
		if (Track.Samples.Num() < 3)
		{
			return Plan;
		}
		Plan.Curvature = SignedCurvature(Track.Samples, Track.bClosed, Params.CurvatureWindowCm);
		for (int32 Side = LeftSide; Side <= RightSide; ++Side)
		{
			const Detail::FSideFlags Flags = Detail::SideFlags(Track, Plan.Curvature, DropMask, Side, Params);
			Plan.Sides[Side] = Detail::SideBarrier(Track, Plan, Flags, Side, Params);
		}
		return Plan;
	}

	TArray<FPolySpot> ResamplePolyline(const TArray<FVector>& Points, double SpacingCm)
	{
		TArray<FPolySpot> Spots;
		if (Points.Num() < 2 || SpacingCm <= 0.0)
		{
			return Spots;
		}
		TArray<double> Cumulative = { 0.0 };
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			Cumulative.Add(Cumulative.Last() + FVector::Dist2D(Points[Index - 1], Points[Index]));
		}
		const double Total = Cumulative.Last();
		if (Total <= UE_KINDA_SMALL_NUMBER)
		{
			return Spots;
		}
		const int32 Count = FMath::Max(1, FMath::CeilToInt32(Total / SpacingCm));
		const double Separation = Total / Count;
		int32 Segment = 0;
		for (int32 Slot = 0; Slot < Count; ++Slot)
		{
			const double At = (Slot + 0.5) * Separation;
			while (Segment < Points.Num() - 2 && Cumulative[Segment + 1] < At)
			{
				++Segment;
			}
			const double SegmentLength = Cumulative[Segment + 1] - Cumulative[Segment];
			const double Alpha = SegmentLength > 0.0 ? (At - Cumulative[Segment]) / SegmentLength : 0.0;
			const FVector Delta = Points[Segment + 1] - Points[Segment];
			FPolySpot& Spot = Spots.AddDefaulted_GetRef();
			Spot.Location = FMath::Lerp(Points[Segment], Points[Segment + 1], Alpha);
			Spot.YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
			Spot.SeparationCm = Separation;
		}
		return Spots;
	}

	TArray<double> BarrierGapsCm(const FTrackData& Track, const FBarrierPlan& Plan, int32 Side)
	{
		TArray<double> Gaps;
		const int32 Num = Track.Samples.Num();
		if (Num < 2 || (Side != LeftSide && Side != RightSide))
		{
			return Gaps;
		}
		const FBarrierSide& Barrier = Plan.Sides[Side];
		// Solo los tramos con carril (dos muestras o más): uno de una muestra no pone ninguna caja.
		TArray<const TArray<int32>*> Runs;
		for (const TArray<int32>& Run : Barrier.Runs)
		{
			if (Run.Num() >= 2)
			{
				Runs.Add(&Run);
			}
		}
		auto PointAt = [&](int32 Index)
		{
			return LateralPoint(Track.Samples[Index], Side, Barrier.OffsetCm.IsValidIndex(Index) ? Barrier.OffsetCm[Index] : Plan.BaseOffsetCm);
		};
		if (Runs.Num() == 0)
		{
			Gaps.Add(Track.LengthCm);
			return Gaps;
		}
		const bool bWholeLoop = Track.bClosed && Runs.Num() == 1 && Runs[0]->Num() == Num;
		if (bWholeLoop)
		{
			return Gaps;
		}
		if (!Track.bClosed && (*Runs[0])[0] > 0)
		{
			Gaps.Add(FVector::Dist2D(PointAt(0), PointAt((*Runs[0])[0])));
		}
		for (int32 RunIndex = 0; RunIndex < Runs.Num(); ++RunIndex)
		{
			const bool bLast = RunIndex + 1 == Runs.Num();
			if (bLast && !Track.bClosed)
			{
				break;
			}
			const TArray<int32>& Next = *Runs[bLast ? 0 : RunIndex + 1];
			Gaps.Add(FVector::Dist2D(PointAt(Runs[RunIndex]->Last()), PointAt(Next[0])));
		}
		if (!Track.bClosed && Runs.Last()->Last() < Num - 1)
		{
			Gaps.Add(FVector::Dist2D(PointAt(Runs.Last()->Last()), PointAt(Num - 1)));
		}
		return Gaps;
	}

	TArray<FVector> RunEdge(const FTrackData& Track, const FBarrierSide& Barrier, const TArray<int32>& Run, int32 Side)
	{
		TArray<FVector> Edge;
		Edge.Reserve(Run.Num() + 1);
		for (const int32 Index : Run)
		{
			if (Track.Samples.IsValidIndex(Index) && Barrier.OffsetCm.IsValidIndex(Index))
			{
				Edge.Add(LateralPoint(Track.Samples[Index], Side, Barrier.OffsetCm[Index]));
			}
		}
		if (Track.bClosed && Run.Num() == Track.Samples.Num() && Edge.Num() > 0)
		{
			// Copia antes de añadir: Add de un elemento del propio array salta el assert de TArray (CheckAddress).
			const FVector First = Edge[0];
			Edge.Add(First);
		}
		return Edge;
	}

	TArray<TArray<FVector>> BarrierSections(const TArray<FVector>& Edge, int32 NumStyles, double SectionCm)
	{
		if (NumStyles <= 1)
		{
			return Edge.Num() >= 2 ? TArray<TArray<FVector>>{ Edge } : TArray<TArray<FVector>>();
		}
		return ChunkPolyline(Edge, SectionCm);
	}

	TArray<TArray<FVector>> ChunkPolyline(const TArray<FVector>& Points, double MaxLengthCm)
	{
		TArray<TArray<FVector>> Chunks;
		if (Points.Num() < 2)
		{
			return Chunks;
		}
		TArray<FVector> Current = { Points[0] };
		double Length = 0.0;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			Current.Add(Points[Index]);
			Length += FVector::Dist2D(Points[Index - 1], Points[Index]);
			if (MaxLengthCm > 0.0 && Length >= MaxLengthCm && Index + 1 < Points.Num())
			{
				// El punto de corte abre el trozo siguiente: los dos estilos se tocan.
				Chunks.Add(Current);
				Current = { Points[Index] };
				Length = 0.0;
			}
		}
		if (Current.Num() >= 2)
		{
			Chunks.Add(Current);
		}
		return Chunks;
	}

	bool IsClearOfTrack(const FTrackData& Track, const FVector& Point, double ClearCm)
	{
		const double ClearSq = FMath::Square(ClearCm);
		return !Track.Samples.ContainsByPredicate([&](const FAxisSample& Sample)
		{
			return FVector::DistSquared2D(Sample.Location, Point) < ClearSq;
		});
	}

	TArray<FSpot> PlanDecor(const FTrackData& Track, const FBarrierPlan& Plan, const TArray<FTNRallyDecorEntry>& Entries,
		const FDecorParams& Params, int32 Seed, const TArray<FSpot>& Reserved)
	{
		if (Track.Samples.Num() < 3)
		{
			return TArray<FSpot>();
		}
		// Lo reservado (el decorado lejano) va delante para que nada lo pise y se quita al final.
		TArray<FSpot> Spots = Reserved;
		// Primero lo grande (playa) y después los cangrejos en los huecos que quedan.
		Detail::AddBeachSpots(Track, Plan, Entries, Params, Seed, Spots);
		Detail::AddCrabSpots(Track, Plan, Params, Seed, Spots);
		Spots.RemoveAt(0, Reserved.Num());
		return Spots;
	}

	TArray<FSpot> PlanSpectators(const FTrackData& Track, const FBarrierPlan& Plan, const FDecorParams& Params, int32 Seed)
	{
		TArray<FSpot> Spots;
		const int32 Num = Track.Samples.Num();
		if (Num < 3 || Plan.Curvature.Num() != Num)
		{
			return Spots;
		}
		int32 Group = 0;
		double LastArc = -TNumericLimits<double>::Max();
		for (int32 Index = 0; Params.SpectatorsPerGroup > 0 && Index < Num; ++Index)
		{
			const double Curvature = Plan.Curvature[Index];
			if (FMath::Abs(Curvature) < Params.SpectatorMinCurvature || Track.Samples[Index].Arc - LastArc < Params.SpectatorGroupSpacingCm)
			{
				continue;
			}
			// Por fuera de la curva, donde se ve llegar a los buggies de frente.
			const int32 Outside = Curvature > 0.0 ? LeftSide : RightSide;
			Detail::AddSpectatorGroup(Track, Plan, Params, Index, Outside, Params.SpectatorsPerGroup, Seed, Group++, Spots);
			LastArc = Track.Samples[Index].Arc;
		}
		if (Params.SpectatorsAtFinish > 0 && Track.Gates.IsValidIndex(Track.FinishGate))
		{
			const int32 Index = Detail::NearestSample(Track, Track.Gates[Track.FinishGate].GetLocation());
			const int32 PerSide = (Params.SpectatorsAtFinish + 1) / 2;
			for (int32 Side = LeftSide; Index != INDEX_NONE && Side <= RightSide; ++Side)
			{
				Detail::AddSpectatorGroup(Track, Plan, Params, Index, Side, PerSide, Seed, Group++, Spots);
			}
		}
		return Spots;
	}

	FTrackData SampleTrack(const ATN_RallyTrack& Track, double StepCm)
	{
		FTrackData Data;
		const double Length = Track.GetTrackLengthCm();
		const double Step = FMath::Max(100.0, StepCm);
		if (!Track.IsBuilt() || Length < 3.0 * Step)
		{
			return Data;
		}
		Data.bClosed = Track.IsCircuit();
		Data.LengthCm = Length;
		const int32 Intervals = FMath::Max(2, FMath::RoundToInt32(Length / Step));
		Data.StepCm = Length / Intervals;
		const int32 Count = Data.bClosed ? Intervals : Intervals + 1;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FAxisSample& Sample = Data.Samples.AddDefaulted_GetRef();
			Sample.Arc = FMath::Min(Index * Data.StepCm, Length);
			Sample.Location = Track.GetLocationAtArc(Sample.Arc);
			Sample.Direction = Track.GetDirectionAtArc(Sample.Arc).GetSafeNormal2D();
		}
		for (int32 Gate = 0; Gate < Track.GetGateCount(); ++Gate)
		{
			const double Arc = Track.GetGateArc(Gate);
			Data.Gates.Add(FTransform(FRotator(0.0, Track.GetDirectionAtArc(Arc).Rotation().Yaw, 0.0), Track.GetLocationAtArc(Arc)));
		}
		Data.FinishGate = Data.Gates.Num() == 0 ? INDEX_NONE : (Data.bClosed ? 0 : Data.Gates.Num() - 1);
		Data.GateHalfWidthCm = Track.GetGateHalfExtent().Y;
		// road_width_m del manifest (0 si no viene): la barrera va pegada a ese borde (#303).
		Data.RoadWidthCm = Track.GetRoadWidthCm();
		Data.bHasWater = Track.HasWaterZ();
		Data.WaterZ = Track.GetWaterZ();
		return Data;
	}
}
