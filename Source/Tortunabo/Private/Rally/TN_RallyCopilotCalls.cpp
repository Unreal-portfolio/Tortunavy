#include "Rally/TN_RallyCopilotCalls.h"

namespace TNRallyCopilot
{
	namespace
	{
		/** Dos arcos son la misma nota si coinciden a menos de esto (cm): las notas no cambian mientras la pista no cambia. */
		constexpr double SameArcToleranceCm = 1.0;
		/** Subida de tono por cada grado más cerrado que 6. */
		constexpr float PitchPerGrade = 0.06f;

		bool ContainsArc(TConstArrayView<double> Arcs, double Arc)
		{
			for (const double Called : Arcs)
			{
				if (FMath::Abs(Called - Arc) <= SameArcToleranceCm)
				{
					return true;
				}
			}
			return false;
		}
	}

	double CallDistanceCm(double SpeedCms, double LeadSeconds, double MinCm)
	{
		return FMath::Max(MinCm, FMath::Max(0.0, SpeedCms) * FMath::Max(0.0, LeadSeconds));
	}

	bool IsTimeToCall(double DistanceCm, double SpeedCms, double LeadSeconds, double MinCm)
	{
		return DistanceCm >= 0.0 && DistanceCm <= CallDistanceCm(SpeedCms, LeadSeconds, MinCm);
	}

	bool IsAutoCopilotActive(bool bLocalIsDriver, bool bHasHumanGunner)
	{
		return bLocalIsDriver && !bHasHumanGunner;
	}

	FCallSignal SignalFor(const TNRallyPaceNotes::FPaceNote& Note)
	{
		using namespace TNRallyPaceNotes;
		FCallSignal Signal;
		switch (Note.Kind)
		{
		case ENoteKind::Turn:
		{
			const int32 Grade = FMath::Clamp(Note.Grade, MinGrade, MaxGrade);
			Signal.Beeps = Grade;
			Signal.Pan = Note.Direction == ETurnDirection::Left ? -1.f : (Note.Direction == ETurnDirection::Right ? 1.f : 0.f);
			Signal.Pitch = 1.f + PitchPerGrade * static_cast<float>(MaxGrade - Grade);
			return Signal;
		}
		case ENoteKind::Crest: Signal.Sound = ECallSound::Crest; return Signal;
		case ENoteKind::Jump: Signal.Sound = ECallSound::Jump; return Signal;
		default: Signal.Sound = ECallSound::Water; return Signal;
		}
	}

	int32 PickNoteToCall(TConstArrayView<TNRallyPaceNotes::FNoteAhead> Ahead, double SpeedCms, TConstArrayView<double> CalledArcs,
		double SecondsSinceLastCall)
	{
		for (int32 Index = 0; Index < Ahead.Num(); ++Index)
		{
			const TNRallyPaceNotes::FNoteAhead& Entry = Ahead[Index];
			if (ContainsArc(CalledArcs, Entry.Note.ArcCm))
			{
				continue;
			}
			// Van de la más cercana a la más lejana: si a esta aún no le toca, a las siguientes tampoco.
			if (!IsTimeToCall(Entry.DistanceCm, SpeedCms))
			{
				return INDEX_NONE;
			}
			return SecondsSinceLastCall >= MinSecondsBetweenCalls ? Index : INDEX_NONE;
		}
		return INDEX_NONE;
	}

	FText PlateHeadline(const TNRallyPaceNotes::FPaceNote& Note)
	{
		using namespace TNRallyPaceNotes;
		if (Note.Kind != ENoteKind::Turn)
		{
			return NoteText(Note);
		}
		// Sin palabras: el lado con un ángulo de Roboto y el grado; vale igual en todos los idiomas.
		const int32 Grade = FMath::Clamp(Note.Grade, MinGrade, MaxGrade);
		const FString Line = Note.Direction == ETurnDirection::Left ? FString::Printf(TEXT("‹ %d"), Grade)
			: FString::Printf(TEXT("%d ›"), Grade);
		return FText::AsCultureInvariant(Line);
	}

	FText PlateDetail(const TNRallyPaceNotes::FPaceNote& Note)
	{
		return Note.Kind == TNRallyPaceNotes::ENoteKind::Turn ? TNRallyPaceNotes::NoteText(Note) : FText::GetEmpty();
	}

	TArray<double> KeepCalledAhead(TConstArrayView<double> CalledArcs, TConstArrayView<TNRallyPaceNotes::FNoteAhead> Ahead)
	{
		TArray<double> Kept;
		for (const double Arc : CalledArcs)
		{
			const bool bStillAhead = Ahead.ContainsByPredicate([Arc](const TNRallyPaceNotes::FNoteAhead& Entry)
			{
				return FMath::Abs(Entry.Note.ArcCm - Arc) <= SameArcToleranceCm;
			});
			if (bStillAhead)
			{
				Kept.Add(Arc);
			}
		}
		return Kept;
	}
}
