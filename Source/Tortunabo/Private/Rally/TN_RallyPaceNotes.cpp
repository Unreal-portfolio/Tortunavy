#include "Rally/TN_RallyPaceNotes.h"

#include "Core/TN_LocText.h"
#include "Rally/TN_RallyTrack.h"

namespace TNRallyPaceNotes
{
	namespace
	{
		/** Tramo de vértices seguidos con la misma clave (signo del giro, rotura o agua), en orden de recorrido. */
		struct FRun
		{
			int32 First = 0;
			int32 Count = 0;
			int32 Key = 0;
		};

		/** Índice del vecino a Offset muestras; en punto a punto, INDEX_NONE si se sale del eje. */
		int32 Neighbor(int32 Index, int32 Offset, int32 Num, bool bClosed)
		{
			const int32 Raw = Index + Offset;
			if (bClosed)
			{
				return ((Raw % Num) + Num) % Num;
			}
			return (Raw >= 0 && Raw < Num) ? Raw : INDEX_NONE;
		}

		/** Índice del elemento Step de un tramo (da la vuelta en circuito). */
		int32 RunIndex(const FRun& Run, int32 Step, int32 Num)
		{
			return (Run.First + Step) % Num;
		}

		/**
		 * Tramos seguidos de vértices con la misma clave distinta de 0. En circuito se empieza en un vértice con clave 0
		 * para no partir en dos un tramo que cruce el índice 0.
		 */
		template <typename FKeyOf>
		TArray<FRun> FindRuns(int32 Num, bool bClosed, FKeyOf KeyOf)
		{
			TArray<FRun> Runs;
			int32 Start = 0;
			if (bClosed)
			{
				Start = INDEX_NONE;
				for (int32 Index = 0; Index < Num && Start == INDEX_NONE; ++Index)
				{
					if (KeyOf(Index) == 0) { Start = Index; }
				}
				if (Start == INDEX_NONE)
				{
					// Todo el lazo cumple (un círculo): un único tramo.
					if (Num > 0) { Runs.Add({ 0, Num, KeyOf(0) }); }
					return Runs;
				}
			}
			int32 CurrentKey = 0;
			for (int32 Step = 0; Step < Num; ++Step)
			{
				const int32 Index = (Start + Step) % Num;
				const int32 Key = KeyOf(Index);
				if (Key != 0 && Key == CurrentKey) { ++Runs.Last().Count; }
				else if (Key != 0) { Runs.Add({ Index, 1, Key }); }
				CurrentKey = Key;
			}
			return Runs;
		}

		/** Giro con signo en cada vértice, en planta (grados; positivo = derecha, porque en UE la Y apunta a la derecha). */
		TArray<double> VertexTurnsDeg(const TArray<FVector>& Points, bool bClosed)
		{
			const int32 Num = Points.Num();
			TArray<double> Turns;
			Turns.Init(0.0, Num);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const int32 Prev = Neighbor(Index, -1, Num, bClosed);
				const int32 Next = Neighbor(Index, 1, Num, bClosed);
				if (Prev == INDEX_NONE || Next == INDEX_NONE) { continue; }
				const FVector2D In(Points[Index] - Points[Prev]);
				const FVector2D Out(Points[Next] - Points[Index]);
				if (In.IsNearlyZero() || Out.IsNearlyZero()) { continue; }
				Turns[Index] = FMath::RadiansToDegrees(FMath::Atan2(FVector2D::CrossProduct(In, Out), FVector2D::DotProduct(In, Out)));
			}
			return Turns;
		}

		/** Media de cada valor con sus HalfWindow vecinos a cada lado (los que existan). */
		TArray<double> SmoothValues(const TArray<double>& Values, int32 HalfWindow, bool bClosed)
		{
			const int32 Num = Values.Num();
			TArray<double> Out;
			Out.Init(0.0, Num);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				double Sum = 0.0;
				int32 Used = 0;
				for (int32 Offset = -HalfWindow; Offset <= HalfWindow; ++Offset)
				{
					const int32 Other = Neighbor(Index, Offset, Num, bClosed);
					if (Other == INDEX_NONE) { continue; }
					Sum += Values[Other];
					++Used;
				}
				Out[Index] = Used > 0 ? Sum / Used : 0.0;
			}
			return Out;
		}

		/** Máxima separación en planta entre la cuerda del tramo y sus puntos (cm). */
		double SagittaCm(const TArray<FVector>& Points, const FRun& Run)
		{
			const int32 Num = Points.Num();
			const FVector2D Start(Points[Run.First]);
			const FVector2D Chord = FVector2D(Points[RunIndex(Run, Run.Count - 1, Num)]) - Start;
			const double ChordLength = Chord.Size();
			double Max = 0.0;
			for (int32 Step = 0; Step < Run.Count; ++Step)
			{
				const FVector2D Offset = FVector2D(Points[RunIndex(Run, Step, Num)]) - Start;
				const double Distance = ChordLength > KINDA_SMALL_NUMBER
					? FMath::Abs(FVector2D::CrossProduct(Chord, Offset)) / ChordLength : Offset.Size();
				Max = FMath::Max(Max, Distance);
			}
			return Max;
		}

		/** Nota de una curva a partir de su tramo; false si gira demasiado poco para cantarla. */
		bool MakeTurnNote(const FTrackNotes& Track, const TArray<double>& Raw, const TArray<double>& Smoothed, const FRun& Run,
			const FParams& Params, FPaceNote& OutNote)
		{
			const int32 Num = Track.Points.Num();
			double Angle = 0.0;
			double MaxAbs = 0.0;
			for (int32 Step = 0; Step < Run.Count; ++Step)
			{
				const int32 Index = RunIndex(Run, Step, Num);
				Angle += Raw[Index];
				MaxAbs = FMath::Max(MaxAbs, FMath::Abs(Smoothed[Index]));
			}
			if (FMath::Abs(Angle) < Params.MinTurnDeg || MaxAbs <= KINDA_SMALL_NUMBER)
			{
				return false;
			}
			OutNote = FPaceNote();
			OutNote.Kind = ENoteKind::Turn;
			OutNote.ArcCm = Run.First * Track.StepCm;
			OutNote.LengthCm = FMath::Max(0, Run.Count - 1) * Track.StepCm;
			OutNote.Direction = Angle > 0.0 ? ETurnDirection::Right : ETurnDirection::Left;
			OutNote.AngleDeg = FMath::Abs(Angle);
			OutNote.RadiusCm = Track.StepCm / FMath::DegreesToRadians(MaxAbs);
			OutNote.Grade = GradeFor(OutNote.RadiusCm, OutNote.AngleDeg);
			OutNote.bDontCut = SagittaCm(Track.Points, Run) >= Params.DontCutSagittaCm;
			return true;
		}

		void AddTurnNotes(FTrackNotes& Track, const FParams& Params)
		{
			const TArray<double> Raw = VertexTurnsDeg(Track.Points, Track.bClosed);
			const TArray<double> Smoothed = SmoothValues(Raw, FMath::Max(0, Params.CurvatureHalfWindow), Track.bClosed);
			// Giro por vértice que corresponde al radio de recta: por debajo, el eje es recto.
			const double StraightDeg = FMath::RadiansToDegrees(Track.StepCm / FMath::Max(1.0, Params.StraightRadiusCm));
			const TArray<FRun> Runs = FindRuns(Track.Points.Num(), Track.bClosed, [&Smoothed, StraightDeg](int32 Index)
			{
				return FMath::Abs(Smoothed[Index]) < StraightDeg ? 0 : (Smoothed[Index] > 0.0 ? 1 : -1);
			});
			for (const FRun& Run : Runs)
			{
				FPaceNote Note;
				if (MakeTurnNote(Track, Raw, Smoothed, Run, Params, Note)) { Track.Notes.Add(Note); }
			}
		}

		/** Pendiente antes del vértice y rotura (después menos antes), medidas con una base de Window muestras. */
		struct FSlopeBreak
		{
			double Before = 0.0;
			double Break = 0.0;
		};

		TArray<FSlopeBreak> SlopeBreaks(const FTrackNotes& Track, int32 Window)
		{
			const int32 Num = Track.Points.Num();
			const double Base = Window * Track.StepCm;
			TArray<FSlopeBreak> Breaks;
			Breaks.SetNum(Num);
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const int32 Prev = Neighbor(Index, -Window, Num, Track.bClosed);
				const int32 Next = Neighbor(Index, Window, Num, Track.bClosed);
				if (Prev == INDEX_NONE || Next == INDEX_NONE) { continue; }
				const double Z = Track.Points[Index].Z;
				Breaks[Index].Before = (Z - Track.Points[Prev].Z) / Base;
				Breaks[Index].Break = (Track.Points[Next].Z - Z) / Base - Breaks[Index].Before;
			}
			return Breaks;
		}

		/** Cresta o salto de un tramo convexo: en el vértice de mayor rotura; salto si viene de una rampa. */
		FPaceNote MakeVerticalNote(const FTrackNotes& Track, const TArray<FSlopeBreak>& Breaks, const FRun& Run, const FParams& Params)
		{
			const int32 Num = Track.Points.Num();
			int32 Sharpest = Run.First;
			double MaxBefore = 0.0;
			for (int32 Step = 0; Step < Run.Count; ++Step)
			{
				const int32 Index = RunIndex(Run, Step, Num);
				MaxBefore = FMath::Max(MaxBefore, Breaks[Index].Before);
				if (Breaks[Index].Break < Breaks[Sharpest].Break) { Sharpest = Index; }
			}
			const bool bJump = MaxBefore >= Params.JumpRampSlope && Breaks[Sharpest].Break <= -Params.JumpBreak;
			FPaceNote Note;
			Note.Kind = bJump ? ENoteKind::Jump : ENoteKind::Crest;
			Note.ArcCm = Sharpest * Track.StepCm;
			return Note;
		}

		void AddVerticalNotes(FTrackNotes& Track, const FParams& Params)
		{
			const int32 Window = FMath::Max(1, FMath::RoundToInt32(Params.SlopeBaseCm / Track.StepCm));
			const TArray<FSlopeBreak> Breaks = SlopeBreaks(Track, Window);
			const TArray<FRun> Runs = FindRuns(Track.Points.Num(), Track.bClosed, [&Breaks, &Params](int32 Index)
			{
				return Breaks[Index].Break <= -Params.CrestBreak ? 1 : 0;
			});
			const int32 FirstVertical = Track.Notes.Num();
			for (const FRun& Run : Runs)
			{
				const FPaceNote Note = MakeVerticalNote(Track, Breaks, Run, Params);
				FPaceNote* Last = Track.Notes.Num() > FirstVertical ? &Track.Notes.Last() : nullptr;
				if (Last && FMath::Abs(Note.ArcCm - Last->ArcCm) < Params.MinVerticalSpacingCm)
				{
					// Dos roturas seguidas se cantan como una; si una es salto, manda el salto.
					if (Note.Kind == ENoteKind::Jump) { Last->Kind = ENoteKind::Jump; }
					continue;
				}
				Track.Notes.Add(Note);
			}
		}

		void AddWaterNotes(FTrackNotes& Track, const FParams& Params)
		{
			if (!Track.bHasWater) { return; }
			const double Limit = Track.WaterZ - Params.WaterMarginCm;
			const TArray<FRun> Runs = FindRuns(Track.Points.Num(), Track.bClosed, [&Track, Limit](int32 Index)
			{
				return Track.Points[Index].Z < Limit ? 1 : 0;
			});
			for (const FRun& Run : Runs)
			{
				FPaceNote Note;
				Note.Kind = ENoteKind::Water;
				Note.ArcCm = Run.First * Track.StepCm;
				Note.LengthCm = Run.Count * Track.StepCm;
				Track.Notes.Add(Note);
			}
		}

		FText TurnText(const FPaceNote& Note)
		{
			const bool bLeft = Note.Direction == ETurnDirection::Left;
			if (Note.Grade <= MinGrade && Note.AngleDeg >= HairpinMinAngleDeg)
			{
				return bLeft ? NSLOCTEXT("Rally", "PaceHairpinLeft", "horquilla izquierda")
					: NSLOCTEXT("Rally", "PaceHairpinRight", "horquilla derecha");
			}
			const FText Pattern = bLeft ? NSLOCTEXT("Rally", "PaceLeft", "izquierda {0}") : NSLOCTEXT("Rally", "PaceRight", "derecha {0}");
			return FText::Format(Pattern, TNLocText::Int(Note.Grade));
		}
	}

	TArray<FVector> Resample(const TArray<FVector>& Polyline, double StepCm, bool bClosed, double& OutStepCm, double& OutLengthCm)
	{
		OutStepCm = 0.0;
		OutLengthCm = 0.0;
		TArray<FVector> Path = Polyline;
		if (bClosed && Path.Num() >= 2 && !Path.Last().Equals(Path[0], 1.0))
		{
			// Copia antes de añadir: Add(Path[0]) con el array lleno lee el elemento después de realojarlo (assert en DebugGame).
			const FVector First = Path[0];
			Path.Add(First);
		}
		if (Path.Num() < 2 || StepCm <= 0.0) { return {}; }

		TArray<double> Cumulative;
		Cumulative.Reserve(Path.Num());
		Cumulative.Add(0.0);
		for (int32 Index = 1; Index < Path.Num(); ++Index)
		{
			Cumulative.Add(Cumulative.Last() + FVector::Dist(Path[Index - 1], Path[Index]));
		}
		const double Length = Cumulative.Last();
		if (Length <= KINDA_SMALL_NUMBER) { return {}; }

		const int32 Segments = FMath::Max(bClosed ? 3 : 1, FMath::RoundToInt32(Length / StepCm));
		const double Step = Length / Segments;
		const int32 Count = bClosed ? Segments : Segments + 1;
		TArray<FVector> Out;
		Out.Reserve(Count);
		int32 Segment = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Arc = FMath::Min(Index * Step, Length);
			while (Segment < Path.Num() - 2 && Cumulative[Segment + 1] < Arc) { ++Segment; }
			const double SegmentLength = Cumulative[Segment + 1] - Cumulative[Segment];
			const double Alpha = SegmentLength > KINDA_SMALL_NUMBER ? (Arc - Cumulative[Segment]) / SegmentLength : 0.0;
			Out.Add(FMath::Lerp(Path[Segment], Path[Segment + 1], FMath::Clamp(Alpha, 0.0, 1.0)));
		}
		OutStepCm = Step;
		OutLengthCm = Length;
		return Out;
	}

	int32 GradeFor(double RadiusCm, double AngleDeg)
	{
		// Radio máximo (cm) de los grados 1 a 5; más abierta es un 6. Calzada de 14 m y buggy de unos 100 km/h.
		static constexpr double Limits[] = { 2000.0, 3500.0, 5500.0, 8500.0, 13000.0 };
		int32 Grade = MaxGrade;
		for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Limits)); ++Index)
		{
			if (RadiusCm < Limits[Index])
			{
				Grade = Index + 1;
				break;
			}
		}
		// Un toque corto se toma más rápido de lo que dice su radio; una curva muy larga, más lenta.
		if (AngleDeg < 30.0) { ++Grade; }
		else if (AngleDeg >= 120.0) { --Grade; }
		return FMath::Clamp(Grade, MinGrade, MaxGrade);
	}

	FTrackNotes Build(const TArray<FVector>& Polyline, bool bClosed, bool bHasWater, double WaterZ, const FParams& Params)
	{
		FTrackNotes Track;
		Track.bClosed = bClosed;
		Track.bHasWater = bHasWater;
		Track.WaterZ = WaterZ;
		Track.Points = Resample(Polyline, FMath::Max(10.0, Params.StepCm), bClosed, Track.StepCm, Track.LengthCm);
		if (!Track.IsValid() || Track.Points.Num() < 3)
		{
			return Track;
		}
		AddTurnNotes(Track, Params);
		AddVerticalNotes(Track, Params);
		AddWaterNotes(Track, Params);
		Track.Notes.StableSort([](const FPaceNote& A, const FPaceNote& B) { return A.ArcCm < B.ArcCm; });
		return Track;
	}

	FTrackNotes BuildForTrack(const ATN_RallyTrack& Track, const FParams& Params)
	{
		const double Length = Track.GetTrackLengthCm();
		if (!Track.IsBuilt() || Length <= 0.0)
		{
			return FTrackNotes();
		}
		// Muestreo de la spline al doble de fino que el análisis: el remuestreo uniforme reparte luego el paso exacto.
		const double SampleStep = FMath::Max(100.0, Params.StepCm * 0.5);
		const int32 Count = FMath::Max(2, FMath::CeilToInt32(Length / SampleStep));
		const int32 Last = Track.IsCircuit() ? Count - 1 : Count;
		TArray<FVector> Axis;
		Axis.Reserve(Last + 1);
		for (int32 Index = 0; Index <= Last; ++Index)
		{
			Axis.Add(Track.GetLocationAtArc(FMath::Min(Length, Index * Length / Count)));
		}
		return Build(Axis, Track.IsCircuit(), Track.HasWaterZ(), Track.GetWaterZ(), Params);
	}

	FVector LocationAtArc(const FTrackNotes& Track, double ArcCm)
	{
		if (!Track.IsValid())
		{
			return FVector::ZeroVector;
		}
		const int32 Num = Track.Points.Num();
		const double Span = Track.bClosed ? Track.LengthCm : (Num - 1) * Track.StepCm;
		const double Arc = Track.bClosed ? FMath::Fmod(FMath::Fmod(ArcCm, Span) + Span, Span) : FMath::Clamp(ArcCm, 0.0, Span);
		const double Exact = Arc / Track.StepCm;
		const int32 Floor = FMath::FloorToInt32(Exact);
		const int32 A = Track.bClosed ? Floor % Num : FMath::Min(Floor, Num - 1);
		const int32 B = Track.bClosed ? (A + 1) % Num : FMath::Min(A + 1, Num - 1);
		return FMath::Lerp(Track.Points[A], Track.Points[B], FMath::Clamp(Exact - Floor, 0.0, 1.0));
	}

	TArray<FNoteAhead> NotesAhead(const FTrackNotes& Track, double FromArcCm, double RangeCm)
	{
		TArray<FNoteAhead> Out;
		if (!Track.IsValid())
		{
			return Out;
		}
		for (const FPaceNote& Note : Track.Notes)
		{
			double Distance = Note.ArcCm - FromArcCm;
			if (Track.bClosed)
			{
				Distance = FMath::Fmod(FMath::Fmod(Distance, Track.LengthCm) + Track.LengthCm, Track.LengthCm);
			}
			if (Distance >= 0.0 && Distance <= RangeCm)
			{
				Out.Add({ Note, Distance });
			}
		}
		Out.StableSort([](const FNoteAhead& A, const FNoteAhead& B) { return A.DistanceCm < B.DistanceCm; });
		return Out;
	}

	TArray<double> ArcsAhead(TConstArrayView<double> ArcsCm, double LengthCm, bool bClosed, double FromArcCm, double RangeCm)
	{
		TArray<double> Out;
		if (LengthCm <= 0.0)
		{
			return Out;
		}
		for (const double Arc : ArcsCm)
		{
			double Distance = Arc - FromArcCm;
			if (bClosed)
			{
				Distance = FMath::Fmod(FMath::Fmod(Distance, LengthCm) + LengthCm, LengthCm);
			}
			if (Distance >= 0.0 && Distance <= RangeCm)
			{
				Out.Add(Distance);
			}
		}
		Out.Sort();
		return Out;
	}

	FText NoteText(const FPaceNote& Note)
	{
		FText Base;
		switch (Note.Kind)
		{
		case ENoteKind::Turn: Base = TurnText(Note); break;
		case ENoteKind::Crest: Base = NSLOCTEXT("Rally", "PaceCrest", "cresta"); break;
		case ENoteKind::Jump: Base = NSLOCTEXT("Rally", "PaceJump", "salto"); break;
		case ENoteKind::Water: Base = NSLOCTEXT("Rally", "PaceWater", "agua"); break;
		default: break;
		}
		return Note.bDontCut ? FText::Format(NSLOCTEXT("Rally", "PaceDontCut", "{0}, no cortes"), Base) : Base;
	}

	FText DistanceText(double DistanceCm)
	{
		const int32 Meters = FMath::RoundToInt32(FMath::Max(0.0, DistanceCm) / 1000.0) * 10;
		return FText::Format(NSLOCTEXT("Rally", "PaceDistance", "{0} m"), TNLocText::Int(Meters));
	}
}
