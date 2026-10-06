#include "Core/TN_CoopGameState.h"
#include "Core/TN_Log.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_GameModeSpawnUtils.h"
#include "Core/TN_MatchFlowTypes.h"
#include "Core/TN_ScoreDecisions.h"
#include "Multiplayer/MP_GameInstance.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

ATN_CoopGameState::ATN_CoopGameState()
{
}

void ATN_CoopGameState::SeamlessTravelTransitionCheckpoint(bool bToTransitionMap)
{
	// El motor marca cada PlayerState sin mirar si es nulo: uno que no viajó deja un hueco nulo tras el GC del mapa viejo (#711).
	if (const int32 Removed = TN_RemoveStalePlayerStates(this))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[CoopGameState] Viaje sin cortes: %d PlayerState que no viajaron fuera del PlayerArray."), Removed);
	}
	Super::SeamlessTravelTransitionCheckpoint(bToTransitionMap);
}

void ATN_CoopGameState::OnRep_MatchFlowState()
{
	// Fires on remote clients when the replicated value arrives. Como en el servidor (BroadcastFlowStateChange): fuera de
	// Results la partida siguiente empieza sin nada guardado (#567).
	PersistedScoreThisRace = TNScoreLogic::PersistedAfterFlowChange(PersistedScoreThisRace, MatchFlowState == ETNMatchFlowState::Results);
	PersistLocalPlayerScoreIfResults();
	OnMatchFlowStateChanged.Broadcast(MatchFlowState);
}

void ATN_CoopGameState::AddQuickChatEntry(int32 SenderPlayerId, uint8 MessageID, float ServerTimeSeconds)
{
	FTN_QuickChatEntry Entry;
	Entry.Sequence = ++NextQuickChatSequence;
	Entry.SenderPlayerId = SenderPlayerId;
	Entry.MessageID = MessageID;
	Entry.ServerTime = ServerTimeSeconds;

	// Multicast envía la entrada individualmente — más eficiente que replicar todo el array.
	// MulticastNewChatEntry_Implementation actualiza el historial local y dispara el delegate.
	MulticastNewChatEntry(Entry);
}

void ATN_CoopGameState::MulticastNewChatEntry_Implementation(FTN_QuickChatEntry Entry)
{
	QuickChatHistory.Add(Entry);
	if (QuickChatHistory.Num() > MaxQuickChatEntries)
	{
		QuickChatHistory.RemoveAt(0);
	}

	OnQuickChatReceived.Broadcast(Entry);
}

FText ATN_CoopGameState::ResolveQuickChatSenderName(int32 SenderPlayerId) const
{
	for (APlayerState* PS : PlayerArray)
	{
		if (!PS)
		{
			continue;
		}

		const int32 LocalPlayerId = PS->GetPlayerId();
		if (LocalPlayerId == SenderPlayerId)
		{
			return FText::AsCultureInvariant(PS->GetPlayerName());
		}
	}

	// Remitente desconocido: un signo de interrogación no depende del idioma.
	return INVTEXT("?");
}

void ATN_CoopGameState::BroadcastFlowStateChange()
{
	// Must be called by game modes on the server after setting MatchFlowState.
	// OnRep does NOT fire on the authoritative machine, so we broadcast manually.
	// Al salir de Results, resetear el acumulador para el siguiente ciclo.
	const bool bResults = MatchFlowState == ETNMatchFlowState::Results;
	PersistedScoreThisRace = TNScoreLogic::PersistedAfterFlowChange(PersistedScoreThisRace, bResults);
	PersistedDollsThisRace = TNScoreLogic::PersistedAfterFlowChange(PersistedDollsThisRace, bResults);
	PersistedCoopScoreThisRace = TNScoreLogic::PersistedAfterFlowChange(PersistedCoopScoreThisRace, bResults);
	if (bResults && HasAuthority())
	{
		AwardEndTitles();
	}
	PersistLocalPlayerScoreIfResults();
	OnMatchFlowStateChanged.Broadcast(MatchFlowState);
}

void ATN_CoopGameState::AwardEndTitles()
{
	TArray<const ATN_CoopPlayerState*> Players;
	for (const APlayerState* PS : PlayerArray)
	{
		if (const ATN_CoopPlayerState* TNPS = Cast<ATN_CoopPlayerState>(PS))
		{
			Players.Add(TNPS);
		}
	}
	// Orden de la sala: quien entró antes (PlayerId menor) gana los empates.
	Players.StableSort([](const ATN_CoopPlayerState& A, const ATN_CoopPlayerState& B) { return A.GetPlayerId() < B.GetPlayerId(); });
	TArray<TNEndTitles::FEntry> Jumps;
	for (const ATN_CoopPlayerState* TNPS : Players)
	{
		Jumps.Add({ TNPS->GetPlayerId(), TNPS->JumpCount });
	}
	const int32 Top = TNEndTitles::PickTop(Jumps);
	JumperTitle = FTN_EndTitle();
	if (Players.IsValidIndex(Top))
	{
		JumperTitle.PlayerId = Players[Top]->GetPlayerId();
		JumperTitle.PlayerName = Players[Top]->GetPlayerName();
		JumperTitle.Count = Players[Top]->JumpCount;
		UE_LOG(LogTortunabo, Log, TEXT("[CoopGameState] Saltarín: %s (%d saltos)."), *JumperTitle.PlayerName, JumperTitle.Count);
	}
	ForceNetUpdate();
}

void ATN_CoopGameState::PersistLocalPlayerScoreIfResults()
{
	if (MatchFlowState != ETNMatchFlowState::Results)
	{
		return;
	}

	UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance());
	if (!GI)
	{
		return;
	}

	// Find the local player's PlayerState and persist their race score.
	// Por DELTA: si Results llegó antes que el último update replicado de RaceScore
	// (race de replicación en clientes), aquí se suma lo visible ahora y
	// OnRep_RaceScore reinvocará este método para persistir lo que falte.
	const UWorld* World = GetWorld();
	const APlayerController* LocalPC = World ? World->GetFirstPlayerController() : nullptr;
	for (APlayerState* PS : PlayerArray)
	{
		if (PS && LocalPC && PS == LocalPC->PlayerState)
		{
			if (const ATN_CoopPlayerState* TNPS = Cast<ATN_CoopPlayerState>(PS))
			{
				const int32 Delta = TNScoreLogic::ComputePersistDelta(TNPS->RaceScore, PersistedScoreThisRace);
				if (Delta > 0)
				{
					GI->AddRaceScore(Delta);
					PersistedScoreThisRace = TNPS->RaceScore;
					UE_LOG(LogTortunabo, Log, TEXT("[CoopGameState] Persisted RaceScore delta=%d (total=%d) for local player '%s'"),
						Delta, PersistedScoreThisRace, *PS->GetPlayerName());
				}
				// Muñecos tortuga (#797): al contador del perfil, por diferencia igual que los puntos.
				const int32 DollDelta = TNScoreLogic::ComputePersistDelta(TNPS->TurtleDollsCollected, PersistedDollsThisRace);
				if (DollDelta > 0)
				{
					GI->AddTurtleDolls(DollDelta);
					PersistedDollsThisRace = TNPS->TurtleDollsCollected;
				}
				// Puntuación final del Coop (#789): aparte de las conchas, por diferencia igual que los puntos.
				const int32 CoopDelta = TNPS->CoopScore.bValid
					? TNScoreLogic::ComputePersistDelta(TNPS->CoopScore.Total, PersistedCoopScoreThisRace) : 0;
				if (CoopDelta > 0)
				{
					GI->AddCoopScore(CoopDelta);
					PersistedCoopScoreThisRace = TNPS->CoopScore.Total;
				}
			}
			break;
		}
	}
}

void ATN_CoopGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATN_CoopGameState, MatchFlowState);
	DOREPLIFETIME(ATN_CoopGameState, JumperTitle);
	DOREPLIFETIME(ATN_CoopGameState, ReadyPlayers);
	DOREPLIFETIME(ATN_CoopGameState, ConnectedPlayers);
	DOREPLIFETIME(ATN_CoopGameState, PlayersInStartZone);
	DOREPLIFETIME(ATN_CoopGameState, ExpectedPlayers);
	DOREPLIFETIME(ATN_CoopGameState, CountdownValue);
	// ServerMatchElapsedTime cambia cada frame → SkipOwner reduce tráfico
	// (el listen-server ya lo calcula localmente)
	DOREPLIFETIME_CONDITION(ATN_CoopGameState, ServerMatchElapsedTime, COND_SkipOwner);
	DOREPLIFETIME(ATN_CoopGameState, FinishedPlayers);
	DOREPLIFETIME(ATN_CoopGameState, CurrentLevel);
	DOREPLIFETIME(ATN_CoopGameState, RaceResults);
}

void ATN_CoopGameState::OnRep_RaceResults()
{
	OnRaceResultsUpdated.Broadcast();
}

void ATN_CoopGameState::Server_UpsertRaceResult(int32 InPlayerId, const FString& InPlayerName,
	int32 InFinishRank, float InFinishTime, int32 InRaceScore, bool bInEliminated)
{
	if (!HasAuthority()) { return; }

	FTN_RaceResultEntry NewEntry;
	NewEntry.PlayerId          = InPlayerId;
	NewEntry.PlayerName        = InPlayerName;
	NewEntry.FinishRank        = InFinishRank;
	NewEntry.FinishTimeSeconds = InFinishTime;
	NewEntry.RaceScore         = InRaceScore;
	NewEntry.bIsEliminated     = bInEliminated;

	const int32 ExistingIdx = RaceResults.IndexOfByKey(NewEntry);
	if (ExistingIdx != INDEX_NONE)
	{
		RaceResults[ExistingIdx] = NewEntry;
	}
	else
	{
		RaceResults.Add(NewEntry);
	}

	// Sort: ranks 1,2,3,4 primero, eliminados al final (FinishRank=0).
	RaceResults.Sort([](const FTN_RaceResultEntry& A, const FTN_RaceResultEntry& B)
	{
		return TNScoreLogic::ComputeResultSortKey(A.bIsEliminated, A.FinishRank)
		     < TNScoreLogic::ComputeResultSortKey(B.bIsEliminated, B.FinishRank);
	});

	// Listen-server: dispara delegate manualmente (OnRep no llega al host).
	OnRaceResultsUpdated.Broadcast();
}


