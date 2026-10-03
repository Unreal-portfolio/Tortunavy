#include "Rally/TN_RallyCrewCalls.h"
#include "Rally/TN_RallyPlayerController.h"
#include "Rally/TN_RallyTrack.h"
#include "Rally/TN_RallyVehicle.h"
#include "Vehicles/TN_Buggy.h"

namespace TNRallyCrewCallsDetail
{
	/** Si el buggy queda a más de esto del arco que se seguía (reaparición), se busca en toda la pista (cm). */
	constexpr double ArcLostDistanceCm = 4000.0;
	/** Pitidos y tono de los avisos rápidos: el turbo, urgente y agudo; el freno, grave. */
	constexpr int32 BoostBeeps = 3;
	constexpr float BoostPitch = 1.5f;
	constexpr int32 BrakeBeeps = 2;
	constexpr float BrakePitch = 0.7f;
	/** Fracción del eje con que se decide si una entrada es un aviso (cruceta o teclas 1 y 2). */
	constexpr float AxisThreshold = 0.5f;
}

bool TNRallyCrewCalls::CanGunnerCall(bool bSeatedGunner, double NoteDistanceCm, double SecondsSinceLastCall)
{
	return bSeatedGunner && NoteDistanceCm >= 0.0 && NoteDistanceCm <= MaxNoteAheadCm && SecondsSinceLastCall >= MinSecondsBetweenCalls;
}

bool TNRallyCrewCalls::CanGunnerQuickCall(bool bSeatedGunner, double SecondsSinceLastCall)
{
	return bSeatedGunner && SecondsSinceLastCall >= MinSecondsBetweenCalls;
}

int32 TNRallyCrewCalls::FindCalledNote(TConstArrayView<TNRallyPaceNotes::FNoteAhead> Ahead, double NoteArcCm)
{
	for (int32 Index = 0; Index < Ahead.Num(); ++Index)
	{
		if (FMath::Abs(Ahead[Index].Note.ArcCm - NoteArcCm) <= ArcMatchToleranceCm)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FTNRallyCrewCall TNRallyCrewCalls::MakeNoteCall(const TNRallyPaceNotes::FPaceNote& Note)
{
	FTNRallyCrewCall Call;
	Call.bQuick = false;
	Call.Kind = static_cast<uint8>(Note.Kind);
	Call.Direction = static_cast<uint8>(Note.Direction);
	Call.Grade = static_cast<uint8>(FMath::Clamp(Note.Grade, 0, TNRallyPaceNotes::MaxGrade));
	Call.bDontCut = Note.bDontCut;
	Call.AngleDeg = static_cast<float>(Note.AngleDeg);
	return Call;
}

TNRallyPaceNotes::FPaceNote TNRallyCrewCalls::NoteFromCall(const FTNRallyCrewCall& Call)
{
	using namespace TNRallyPaceNotes;
	FPaceNote Note;
	// Un valor fuera de rango (un paquete corrupto) se queda en una curva centrada sin grado.
	Note.Kind = Call.Kind <= static_cast<uint8>(ENoteKind::Water) ? static_cast<ENoteKind>(Call.Kind) : ENoteKind::Turn;
	Note.Direction = Call.Direction <= static_cast<uint8>(ETurnDirection::Right) ? static_cast<ETurnDirection>(Call.Direction)
		: ETurnDirection::None;
	Note.Grade = FMath::Clamp(static_cast<int32>(Call.Grade), 0, MaxGrade);
	Note.bDontCut = Call.bDontCut;
	Note.AngleDeg = FMath::Max(0.0, static_cast<double>(Call.AngleDeg));
	return Note;
}

FTNRallyCrewCall TNRallyCrewCalls::MakeQuickCall(ETNRallyQuickCall Quick)
{
	FTNRallyCrewCall Call;
	Call.bQuick = true;
	Call.Quick = Quick;
	return Call;
}

TNRallyCopilot::FCallSignal TNRallyCrewCalls::QuickSignal(ETNRallyQuickCall Quick)
{
	using namespace TNRallyCrewCallsDetail;
	TNRallyCopilot::FCallSignal Signal;
	Signal.Sound = TNRallyCopilot::ECallSound::Beep;
	Signal.Pan = 0.f;
	Signal.Beeps = Quick == ETNRallyQuickCall::Boost ? BoostBeeps : BrakeBeeps;
	Signal.Pitch = Quick == ETNRallyQuickCall::Boost ? BoostPitch : BrakePitch;
	return Signal;
}

FText TNRallyCrewCalls::QuickHeadline(ETNRallyQuickCall Quick)
{
	return Quick == ETNRallyQuickCall::Boost ? NSLOCTEXT("Rally", "QuickCallBoost", "¡Turbo ya!")
		: NSLOCTEXT("Rally", "QuickCallBrake", "¡Frena!");
}

FLinearColor TNRallyCrewCalls::QuickColor(ETNRallyQuickCall Quick)
{
	return Quick == ETNRallyQuickCall::Boost ? FLinearColor(0.3f, 0.95f, 1.f) : FLinearColor(1.f, 0.3f, 0.2f);
}

bool TNRallyCrewCalls::QuickFromAxis(float Axis, ETNRallyQuickCall& OutQuick)
{
	if (FMath::Abs(Axis) < TNRallyCrewCallsDetail::AxisThreshold)
	{
		return false;
	}
	OutQuick = Axis > 0.f ? ETNRallyQuickCall::Boost : ETNRallyQuickCall::Brake;
	return true;
}

bool TNRallyCrewCalls::FNoteFollower::Sync(const ATN_RallyTrack& InTrack)
{
	if (!InTrack.IsBuilt())
	{
		return false;
	}
	const float Length = InTrack.GetTrackLengthCm();
	if (Track.Get() == &InTrack && FMath::IsNearlyEqual(Length, TrackLengthCm, 1.f) && Notes.IsValid())
	{
		return true;
	}
	Notes = TNRallyPaceNotes::BuildForTrack(InTrack);
	Track = &InTrack;
	TrackLengthCm = Length;
	bHasArc = false;
	return Notes.IsValid();
}

double TNRallyCrewCalls::FNoteFollower::NoteArcAt(const FVector& Location)
{
	const ATN_RallyTrack* Current = Track.Get();
	if (!Current || TrackLengthCm <= 0.f)
	{
		return 0.0;
	}
	double Arc = bHasArc ? Current->FindArcNear(Location, TrackArcCm) : Current->FindArcGlobal(Location);
	if (FVector::Dist2D(Current->GetLocationAtArc(Arc), Location) > TNRallyCrewCallsDetail::ArcLostDistanceCm)
	{
		Arc = Current->FindArcGlobal(Location);
	}
	TrackArcCm = Arc;
	bHasArc = true;
	// El eje de las notas es una polilínea de la spline: su longitud difiere unas milésimas.
	return TrackArcCm * (Notes.LengthCm / TrackLengthCm);
}

TArray<TNRallyPaceNotes::FNoteAhead> TNRallyCrewCalls::FNoteFollower::NotesAheadOf(const FVector& Location)
{
	return TNRallyPaceNotes::NotesAhead(Notes, NoteArcAt(Location), MaxNoteAheadCm);
}

void TNRallyCrewCalls::SendToCrew(ATN_Buggy& Buggy, const FTNRallyCrewCall& Call)
{
	for (const ETNRallySeat Seat : { ETNRallySeat::Driver, ETNRallySeat::Gunner })
	{
		if (ATN_RallyPlayerController* Player = Cast<ATN_RallyPlayerController>(Buggy.GetSeatController(Seat)))
		{
			Player->ClientRallyCrewCall(Call);
		}
	}
}
