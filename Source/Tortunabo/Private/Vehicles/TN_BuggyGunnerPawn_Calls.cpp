// ATN_BuggyGunnerPawn: la artillera canta notas y avisos rápidos a la conductora (#330). La artillera local elige la
// próxima nota con su pista; el servidor la vuelve a buscar con la suya, la valida (TNRallyCrewCalls) y la manda solo a
// las dos ocupantes (ATN_RallyPlayerController::ClientRallyCrewCall).

#include "Vehicles/TN_BuggyGunnerPawn.h"
#include "Engine/World.h"
#include "InputActionValue.h"
#include "Rally/TN_RallyGameState.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyTrack.h"
#include "Vehicles/TN_Buggy.h"

namespace TNGunnerCallsDetail
{
	ATN_RallyTrack* FindTrack(const UWorld* World)
	{
		const ATN_RallyGameState* RallyState = World ? World->GetGameState<ATN_RallyGameState>() : nullptr;
		return RallyState ? RallyState->GetTrack() : nullptr;
	}
}

bool ATN_BuggyGunnerPawn::RequestCallNote()
{
	UWorld* World = GetWorld();
	const ATN_RallyTrack* Track = TNGunnerCallsDetail::FindTrack(World);
	if (!Buggy || !Track || !NoteFollower.Sync(*Track))
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	if (Now - LastCallRequest < TNRallyCrewCalls::MinSecondsBetweenCalls)
	{
		return false;
	}
	const TArray<TNRallyPaceNotes::FNoteAhead> Ahead = NoteFollower.NotesAheadOf(Buggy->GetActorLocation());
	if (Ahead.Num() == 0)
	{
		UE_LOG(LogTNRally, Verbose, TEXT("[RallyCall] %s: no hay nota en los próximos %.0f m"), *GetName(), TNRallyCrewCalls::MaxNoteAheadCm / 100.0);
		return false;
	}
	LastCallRequest = Now;
	ServerCallNote(static_cast<float>(Ahead[0].Note.ArcCm));
	return true;
}

bool ATN_BuggyGunnerPawn::RequestQuickCall(ETNRallyQuickCall Quick)
{
	const UWorld* World = GetWorld();
	if (!Buggy || !World || World->GetTimeSeconds() - LastCallRequest < TNRallyCrewCalls::MinSecondsBetweenCalls)
	{
		return false;
	}
	LastCallRequest = World->GetTimeSeconds();
	ServerQuickCall(Quick);
	return true;
}

void ATN_BuggyGunnerPawn::OnCallNote(const FInputActionValue& Value)
{
	RequestCallNote();
}

void ATN_BuggyGunnerPawn::OnQuickCall(const FInputActionValue& Value)
{
	ETNRallyQuickCall Quick = ETNRallyQuickCall::Boost;
	if (TNRallyCrewCalls::QuickFromAxis(Value.Get<float>(), Quick))
	{
		RequestQuickCall(Quick);
	}
}

bool ATN_BuggyGunnerPawn::ServerCallNote_Validate(float NoteArcCm)
{
	return FMath::IsFinite(NoteArcCm) && NoteArcCm >= 0.f;
}

void ATN_BuggyGunnerPawn::ServerCallNote_Implementation(float NoteArcCm)
{
	const ATN_RallyTrack* Track = TNGunnerCallsDetail::FindTrack(GetWorld());
	if (!IsSeatedGunner() || !Track || !NoteFollower.Sync(*Track))
	{
		return;
	}
	// La nota se busca otra vez en el servidor: solo vale una que empiece entre el buggy y 600 m por delante.
	const TArray<TNRallyPaceNotes::FNoteAhead> Ahead = NoteFollower.NotesAheadOf(Buggy->GetActorLocation());
	const int32 Index = TNRallyCrewCalls::FindCalledNote(Ahead, NoteArcCm);
	const double Since = GetWorld()->GetTimeSeconds() - LastServerCall;
	if (Index == INDEX_NONE || !TNRallyCrewCalls::CanGunnerCall(true, Ahead[Index].DistanceCm, Since))
	{
		UE_LOG(LogTNRally, Log, TEXT("[RallyCall] %s: canto rechazado (nota en el arco %.0f %s, %.2f s desde el último)"), *Buggy->GetName(),
			NoteArcCm, Index == INDEX_NONE ? TEXT("no está en los próximos 600 m") : TEXT("encontrada"), Since);
		return;
	}
	UE_LOG(LogTNRally, Log, TEXT("[RallyCall] %s: la artillera canta «%s» a %.0f m"), *Buggy->GetName(),
		*TNRallyPaceNotes::NoteText(Ahead[Index].Note).ToString(), Ahead[Index].DistanceCm / 100.0);
	BroadcastCall(TNRallyCrewCalls::MakeNoteCall(Ahead[Index].Note));
}

void ATN_BuggyGunnerPawn::ServerQuickCall_Implementation(ETNRallyQuickCall Quick)
{
	const double Since = GetWorld()->GetTimeSeconds() - LastServerCall;
	if (!TNRallyCrewCalls::CanGunnerQuickCall(IsSeatedGunner(), Since))
	{
		return;
	}
	UE_LOG(LogTNRally, Log, TEXT("[RallyCall] %s: la artillera avisa %s"), *Buggy->GetName(), *TNRallyCrewCalls::QuickHeadline(Quick).ToString());
	BroadcastCall(TNRallyCrewCalls::MakeQuickCall(Quick));
}

void ATN_BuggyGunnerPawn::BroadcastCall(const FTNRallyCrewCall& Call)
{
	LastServerCall = GetWorld()->GetTimeSeconds();
	TNRallyCrewCalls::SendToCrew(*Buggy, Call);
}
