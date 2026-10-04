#include "Kart/TN_KartRoutePlan.h"
#include "Algo/BinarySearch.h"

namespace TNKart
{
	namespace
	{
		/** Paso con el que se busca sitio para una puerta adelantada (cm). */
		constexpr double RouteGateSearchStepCm = 200.0;
		/** Margen de la meta respecto al final del camino (cm). */
		constexpr double RouteFinishEndMarginCm = 1000.0;
		/** Margen de la línea del piloto IA respecto al borde de la calzada (cm). */
		constexpr double RouteLineEdgeMarginCm = 250.0;

		/** Índice del segmento [I, I + 1] que contiene el arco S (recortado a la polilínea). */
		int32 SegmentAt(const TArray<double>& Arc, double S)
		{
			if (Arc.Num() < 2)
			{
				return 0;
			}
			const int32 Upper = Algo::UpperBound(Arc, S);
			return FMath::Clamp(Upper - 1, 0, Arc.Num() - 2);
		}

		double Alpha(const TArray<double>& Arc, int32 Index, double S)
		{
			const double Span = Arc[Index + 1] - Arc[Index];
			return Span > KINDA_SMALL_NUMBER ? FMath::Clamp((S - Arc[Index]) / Span, 0.0, 1.0) : 0.0;
		}
	}

	TArray<double> CumulativeArc(const TArray<FVector>& Points)
	{
		TArray<double> Arc;
		Arc.SetNumUninitialized(Points.Num());
		double Total = 0.0;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			if (Index > 0)
			{
				Total += FVector::Dist(Points[Index - 1], Points[Index]);
			}
			Arc[Index] = Total;
		}
		return Arc;
	}

	double FinishArcForPath(const TArray<FRouteSample>& Samples, const TArray<double>& Arc, const FRoutePlanParams& Params)
	{
		if (Samples.Num() < 2 || Arc.Num() != Samples.Num())
		{
			return 0.0;
		}
		const double Total = Arc.Last();
		double Limit = FMath::Max(0.0, Total - RouteFinishEndMarginCm);
		// Donde el suelo baja de la cota mínima (la orilla) ya no puede ir la meta.
		if (Params.MinFinishZ > -1.0e9)
		{
			for (int32 Index = 1; Index < Samples.Num(); ++Index)
			{
				if (Samples[Index].bShore && Samples[Index].Location.Z < Params.MinFinishZ)
				{
					Limit = FMath::Min(Limit, FMath::Max(0.0, Arc[Index - 1] - 200.0));
					break;
				}
			}
		}
		for (int32 Index = 0; Index < Samples.Num(); ++Index)
		{
			if (Samples[Index].bShore)
			{
				return FMath::Min(Arc[Index] + Params.FinishIntoShoreCm, Limit);
			}
		}
		return FMath::Max(0.0, Total - Params.RunOffCm);
	}

	TArray<double> PlanGateArcs(double StartArc, double FinishArc, const FRoutePlanParams& Params, TFunctionRef<bool(double)> IsForbidden)
	{
		TArray<double> Gates;
		if (FinishArc <= StartArc)
		{
			return Gates;
		}
		Gates.Add(StartArc);
		const double Spacing = FMath::Max(Params.GateSpacingCm, 1000.0);
		const double MinGap = FMath::Clamp(Params.MinGateGapCm, 500.0, Spacing);
		double Last = StartArc;
		for (double Wanted = StartArc + Spacing; Wanted < FinishArc - MinGap; Wanted += Spacing)
		{
			// Adelanta la puerta hasta un sitio que valga (fuera de cuevas y estructuras), sin pasarse; si no lo hay, la
			// retrasa (una cueva larga deja la puerta antes de su boca).
			double Chosen = -1.0;
			for (double Probe = Wanted; Probe <= Wanted + Params.MaxGateShiftCm; Probe += RouteGateSearchStepCm)
			{
				if (!IsForbidden(Probe))
				{
					Chosen = Probe;
					break;
				}
			}
			for (double Probe = Wanted - RouteGateSearchStepCm; Chosen < 0.0 && Probe >= Wanted - Params.MaxGateShiftCm
				&& Probe - Last >= MinGap; Probe -= RouteGateSearchStepCm)
			{
				if (!IsForbidden(Probe))
				{
					Chosen = Probe;
				}
			}
			if (Chosen < 0.0 || Chosen - Last < MinGap || FinishArc - Chosen < MinGap)
			{
				continue;
			}
			Gates.Add(Chosen);
			Last = Chosen;
			// La siguiente cuenta desde la puerta puesta, no desde la que se quería.
			Wanted = Chosen;
		}
		Gates.Add(FinishArc);
		return Gates;
	}

	FRoutePlan PlanRouteFromPath(const TArray<FRouteSample>& Samples, const FRoutePlanParams& Params)
	{
		FRoutePlan Plan;
		if (Samples.Num() < 2)
		{
			return Plan;
		}
		TArray<FVector> Points;
		Points.Reserve(Samples.Num());
		for (const FRouteSample& Sample : Samples)
		{
			Points.Add(Sample.Location);
		}
		const TArray<double> Arc = CumulativeArc(Points);
		const double Total = Arc.Last();
		const double StartArc = FMath::Clamp(Params.StartGateArcCm, 0.0, Total);
		const double FinishArc = FinishArcForPath(Samples, Arc, Params);
		if (FinishArc - StartArc < FMath::Max(Params.MinGateGapCm, 1000.0))
		{
			return Plan;
		}

		const double MaxGateWidth = Params.MaxGateRoadWidthCm;
		auto SampleAt = [&Samples, &Arc, MaxGateWidth](double S, FVector& OutLocation, double& OutWidth, bool& bOutNoGate)
		{
			const int32 Index = SegmentAt(Arc, S);
			const double T = Alpha(Arc, Index, S);
			OutLocation = FMath::Lerp(Samples[Index].Location, Samples[Index + 1].Location, T);
			OutWidth = FMath::Lerp(Samples[Index].WidthCm, Samples[Index + 1].WidthCm, T);
			bOutNoGate = Samples[Index].bNoGate || Samples[Index + 1].bNoGate || OutWidth > MaxGateWidth;
		};

		// Eje de la spline cada RoadStep, de la primera muestra a la meta más la escapatoria.
		const double RoadEnd = FMath::Min(Total, FinishArc + Params.RunOffCm);
		const double Step = FMath::Max(Params.RoadStepCm, 100.0);
		for (double S = 0.0;; S += Step)
		{
			const double Clamped = FMath::Min(S, RoadEnd);
			FVector Location;
			double Width = 0.0;
			bool bNoGate = false;
			SampleAt(Clamped, Location, Width, bNoGate);
			Plan.Road.Add(Location);
			Plan.RoadWidthCm.Add(Width);
			if (Clamped >= RoadEnd)
			{
				break;
			}
		}
		Plan.RoadArcCm = CumulativeArc(Plan.Road);
		Plan.LengthCm = Plan.RoadArcCm.Last();

		// Puertas sobre el camino original (sus arcos casi coinciden con los del eje remuestreado).
		auto IsForbidden = [&SampleAt](double S)
		{
			FVector Location;
			double Width = 0.0;
			bool bNoGate = false;
			SampleAt(S, Location, Width, bNoGate);
			return bNoGate;
		};
		// La salida también va donde puede ir una puerta (no junto a una roca ni donde el camino es más ancho que el arco):
		// se adelanta lo que haga falta, sin pasar de la mitad del camino.
		double GateStart = StartArc;
		for (double Probe = StartArc; Probe < 0.5 * (StartArc + FinishArc); Probe += RouteGateSearchStepCm)
		{
			if (!IsForbidden(Probe))
			{
				GateStart = Probe;
				break;
			}
		}
		const TArray<double> GateArcs = PlanGateArcs(GateStart, FinishArc, Params, IsForbidden);
		for (const double GateArc : GateArcs)
		{
			FVector Location;
			double Width = 0.0;
			bool bNoGate = false;
			SampleAt(GateArc, Location, Width, bNoGate);
			FVector Ahead;
			FVector Behind;
			double Ignored = 0.0;
			SampleAt(FMath::Min(GateArc + 400.0, Total), Ahead, Ignored, bNoGate);
			SampleAt(FMath::Max(GateArc - 400.0, 0.0), Behind, Ignored, bNoGate);
			TNRally::FGateDef Gate;
			Gate.Location = Location;
			const FVector Direction = (Ahead - Behind).GetSafeNormal2D();
			Gate.YawDeg = Direction.IsNearlyZero() ? 0.0 : Direction.Rotation().Yaw;
			Gate.bHasYaw = true;
			Plan.Gates.Add(Gate);
			Plan.GateArcCm.Add(GateArc);
		}
		Plan.FinishArcCm = FinishArc;
		Plan.bValid = Plan.Gates.Num() >= 2 && Plan.Road.Num() >= 2;
		return Plan;
	}

	TArray<double> PlanItemRowArcs(double StartArc, double EndArc, const TArray<double>& GateArcs, double SpacingCm, double MinFromGateCm)
	{
		TArray<double> Rows;
		const double Spacing = FMath::Max(SpacingCm, 2000.0);
		for (double Arc = StartArc + 0.5 * Spacing; Arc < EndArc; Arc += Spacing)
		{
			double Placed = Arc;
			// Lejos de las puertas: se adelanta hasta quedar a MinFromGateCm de la que tiene cerca.
			for (int32 Guard = 0; Guard < 4; ++Guard)
			{
				bool bMoved = false;
				for (const double Gate : GateArcs)
				{
					if (FMath::Abs(Placed - Gate) < MinFromGateCm)
					{
						Placed = Gate + MinFromGateCm;
						bMoved = true;
					}
				}
				if (!bMoved)
				{
					break;
				}
			}
			if (Placed < EndArc && (Rows.Num() == 0 || Placed - Rows.Last() >= 0.5 * Spacing))
			{
				Rows.Add(Placed);
			}
		}
		return Rows;
	}

	int32 ItemBoxesForWidth(double RoadWidthCm)
	{
		return FMath::Clamp(FMath::FloorToInt32((RoadWidthCm - 200.0) / 400.0), 2, 6);
	}

	double ItemLateralSpacingCm(double RoadWidthCm, int32 Boxes)
	{
		return FMath::Min(450.0, FMath::Max(200.0, RoadWidthCm - 200.0) / FMath::Max(1, Boxes));
	}

	TArray<FVector> OffsetRoad(const TArray<FVector>& Road, const TArray<double>& Offsets)
	{
		TArray<FVector> Out = Road;
		const int32 Num = Road.Num();
		for (int32 Index = 0; Index < Num && Index < Offsets.Num(); ++Index)
		{
			if (FMath::IsNearlyZero(Offsets[Index]))
			{
				continue;
			}
			const FVector2D Dir = FVector2D(Road[FMath::Min(Num - 1, Index + 1)] - Road[FMath::Max(0, Index - 1)]).GetSafeNormal();
			Out[Index] += FVector(-Dir.Y, Dir.X, 0.0) * Offsets[Index];
		}
		return Out;
	}

	TArray<double> PlanRacingLineOffsets(const TArray<FVector>& Road, const TArray<double>& HalfWidthsCm,
		const TArray<FLineObstacle>& Obstacles, double ClearanceCm, double RampCm)
	{
		const int32 Num = Road.Num();
		TArray<double> Offsets;
		Offsets.SetNumZeroed(Num);
		if (Num < 2 || Obstacles.Num() == 0)
		{
			return Offsets;
		}
		const TArray<double> Arc = CumulativeArc(Road);
		auto Limit = [&HalfWidthsCm](int32 Index)
		{
			return HalfWidthsCm.IsValidIndex(Index) ? FMath::Max(0.0, HalfWidthsCm[Index] - RouteLineEdgeMarginCm) : 0.0;
		};

		// Puntos «núcleo»: los que tienen un obstáculo al lado (a lo largo, a menos de su radio más la holgura). Ahí la línea
		// va por el sitio libre más cercano al eje; entre núcleos, en rampa; lejos de todos, por el eje.
		TArray<bool> bCore;
		bCore.SetNumZeroed(Num);
		struct FBlock
		{
			double Lateral = 0.0;
			double Keep = 0.0;
		};
		TArray<FBlock> Blocks;
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const FVector2D Point(Road[Index]);
			const FVector2D Dir = FVector2D(Road[FMath::Min(Num - 1, Index + 1)] - Road[FMath::Max(0, Index - 1)]).GetSafeNormal();
			const FVector2D Right(-Dir.Y, Dir.X);
			const double Edge = Limit(Index);
			Blocks.Reset();
			bool bCentered = false;
			for (const FLineObstacle& Obstacle : Obstacles)
			{
				const FVector2D To = Obstacle.Center - Point;
				if (Obstacle.bKeepCentered)
				{
					// Bajo un arco, por el centro (los lados son más bajos y ahí están sus pies).
					bCentered |= FVector2D::Distance(Obstacle.Center, Point) <= Obstacle.RadiusCm;
					continue;
				}
				const double Keep = Obstacle.RadiusCm + ClearanceCm;
				const double Along = FVector2D::DotProduct(To, Dir);
				const double Lateral = FVector2D::DotProduct(To, Right);
				// Solo lo que está a su altura y dentro de la calzada (con la holgura).
				if (FMath::Abs(Along) <= Keep && FMath::Abs(Lateral) - Keep < Edge)
				{
					Blocks.Add({ Lateral, Keep });
				}
			}
			if (Blocks.Num() == 0)
			{
				if (bCentered)
				{
					bCore[Index] = true;
					Offsets[Index] = 0.0;
				}
				continue;
			}
			auto IsFree = [&Blocks](double Offset)
			{
				for (const FBlock& Block : Blocks)
				{
					if (FMath::Abs(Offset - Block.Lateral) < Block.Keep - 0.5)
					{
						return false;
					}
				}
				return true;
			};
			// Candidatos: el eje, los bordes de cada hueco libre y los de la calzada; gana el libre más cercano al eje.
			TArray<double> Candidates = { 0.0, -Edge, Edge };
			for (const FBlock& Block : Blocks)
			{
				Candidates.Add(Block.Lateral - Block.Keep);
				Candidates.Add(Block.Lateral + Block.Keep);
			}
			double Chosen = 0.0;
			double Best = TNumericLimits<double>::Max();
			double BestClear = -1.0;
			bool bAnyFree = false;
			for (const double Candidate : Candidates)
			{
				if (Candidate < -Edge - 0.5 || Candidate > Edge + 0.5)
				{
					continue;
				}
				if (IsFree(Candidate))
				{
					if (!bAnyFree || FMath::Abs(Candidate) < Best)
					{
						Best = FMath::Abs(Candidate);
						Chosen = Candidate;
					}
					bAnyFree = true;
				}
				else if (!bAnyFree)
				{
					// Sin sitio libre (calzada muy estrecha): lo más lejos posible de los obstáculos.
					double Clear = TNumericLimits<double>::Max();
					for (const FBlock& Block : Blocks)
					{
						Clear = FMath::Min(Clear, FMath::Abs(Candidate - Block.Lateral));
					}
					if (Clear > BestClear)
					{
						BestClear = Clear;
						Chosen = Candidate;
					}
				}
			}
			bCore[Index] = true;
			Offsets[Index] = FMath::Clamp(Chosen, -Edge, Edge);
		}

		// Entre núcleos, en línea recta de uno a otro (si están a menos de dos rampas); al salir y al entrar, rampa hasta el eje.
		const double Ramp = FMath::Max(RampCm, 100.0);
		int32 Previous = INDEX_NONE;
		for (int32 Index = 0; Index < Num; ++Index)
		{
			if (bCore[Index])
			{
				Previous = Index;
				continue;
			}
			int32 Next = Index + 1;
			while (Next < Num && !bCore[Next] && Arc[Next] - Arc[Index] <= Ramp)
			{
				++Next;
			}
			const bool bHasNext = Next < Num && bCore[Next];
			const bool bHasPrevious = Previous != INDEX_NONE && Arc[Index] - Arc[Previous] <= Ramp;
			double Value = 0.0;
			if (bHasPrevious && bHasNext)
			{
				const double Span = FMath::Max(1.0, Arc[Next] - Arc[Previous]);
				Value = FMath::Lerp(Offsets[Previous], Offsets[Next], (Arc[Index] - Arc[Previous]) / Span);
			}
			else if (bHasPrevious)
			{
				Value = Offsets[Previous] * (1.0 - (Arc[Index] - Arc[Previous]) / Ramp);
			}
			else if (bHasNext)
			{
				Value = Offsets[Next] * (1.0 - (Arc[Next] - Arc[Index]) / Ramp);
			}
			const double Edge = Limit(Index);
			Offsets[Index] = FMath::Clamp(Value, -Edge, Edge);
		}
		return Offsets;
	}
}
