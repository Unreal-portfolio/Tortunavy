// Rondas de Todos contra Todos (ATN_TctGameMode): preparación, salida, vigilancia de las caídas, cierre, recuento y campeona.

#include "Game/TN_TctGameMode.h"
#include "Game/TN_TctItemComponent.h"
#include "Game/TN_TctGameState.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_Log.h"
#include "Player/TortugaCharacter.h"
#include "Lobby/TN_LobbyMission.h"
#include "World/TN_TctArena.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerStart.h"
#include "TimerManager.h"

namespace TNTctRoundDetail
{
	/** CountdownValue que mantiene la pantalla de la campeona hasta que el anfitrión elige (como en la carrera). */
	constexpr int32 ChampionHoldCountdown = 99;
	/** Cada cuánto se mira si alguien ha caído (s). */
	constexpr float WatchInterval = 0.1f;
	/** Media altura de la cápsula si el peón no es un personaje. */
	constexpr float DefaultHalfHeight = 90.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Salida
// ─────────────────────────────────────────────────────────────────────────────

void ATN_TctGameMode::OnWaitingTimeout()
{
	// La base avisa cuando han llegado todas del lobby (o venció la espera): primera ronda, con las tortugas ya en sus
	// salidas (ChoosePlayerStart) y quietas. La cuenta de salida de esta ronda es el huevo de la pantalla de carga.
	if (bMatchStarted)
	{
		return;
	}
	if (!bArenaReady)
	{
		AbortWithoutArena();
		return;
	}
	ResetMatchScores();
	CurrentRound = 1;
	// Las que llegaron antes de medir la arena (viaje sin cortes) salieron en el PlayerStart del nivel: todas a su sitio.
	PlaceAllForRound();
	Super::OnWaitingTimeout();
	BeginRound(true);
}

void ATN_TctGameMode::AbortWithoutArena()
{
	// Sin arena no hay salidas, límites ni plan del agua: ninguna ronda. Una sola vez (bMatchStarted corta las demás llamadas).
	bMatchStarted = true;
	bMatchOver = true;
	UE_LOG(LogTortunabo, Error, TEXT("[TcT] Sin arena (%s): no se juega y se vuelve al lobby."), *DefaultArenaVariant.ToString());
	for (APlayerController* PC : GetPlayingControllers())
	{
		FreezePlayer(PC);
	}
	// La misión vuelve al cooperativo: el lobby no vuelve a mandar aquí a la tropa.
	TNLobbyMission::SetMode(this, ETNProcGameMode::Coop);
	LeaveAfterDelay([this]() { FinishRoundAndReturnToLobby(); });
}

void ATN_TctGameMode::PrepareRound()
{
	CancelRoundTimers();
	bRoundLive = false;
	bTimeUp = false;
	++CurrentRound;

	if (ATN_TctGameState* State = GetTctState())
	{
		State->RoundWinner = nullptr;
		State->RoundHalfShells.Reset();
		State->RoundArrivals.Reset();
		State->RoundEndReason = ETNBeachRoundEnd::None;
		State->FinishCountdown = ETNBeachFinishCountdown::None;
		State->RoundEndServerTime = 0.f;
		State->RoundTimeLimitSeconds = 0.f;
		State->PhaseSecondsLeft = 0.f;
	}
	HoldWater(FloodPlan.BaseZ);
	StopItemPads();
	// Waiting con el reloj a 0: las pantallas cierran la cáscara con «RONDA N» y esperan al 3, 2, 1.
	SetPhase(ETNBeachRacePhase::Waiting);
	SyncRoundInfo();

	PlaceAllForRound();
	GetWorldTimerManager().SetTimer(PhaseEndHandle, this, &ATN_TctGameMode::StartCountdown, PrepSeconds, false);
}

void ATN_TctGameMode::PlaceAllForRound()
{
	// Cada ronda, las salidas rotan: nadie repite siempre el mismo sitio.
	const TArray<APlayerController*> Controllers = GetPlayingControllers();
	for (int32 Index = 0; Index < Controllers.Num(); ++Index)
	{
		APlayerController* PC = Controllers[Index];
		FTransform Spawn;
		const APlayerStart* Start = SpawnPoints.Num() > 0 ? SpawnPoints[(Index + CurrentRound - 1) % SpawnPoints.Num()].Get() : nullptr;
		if (IsValid(Start))
		{
			Spawn = Start->GetActorTransform();
		}
		else if (const AActor* Fallback = ChoosePlayerStart(PC))
		{
			Spawn = Fallback->GetActorTransform();
		}
		PlaceForRound(PC, Spawn);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] ═══ Ronda %d: %d tortugas en sus salidas ═══"), CurrentRound, Controllers.Num());
}

void ATN_TctGameMode::StartCountdown()
{
	BeginPhaseClock(CountdownSeconds);
	GetWorldTimerManager().SetTimer(PhaseEndHandle, FTimerDelegate::CreateUObject(this, &ATN_TctGameMode::BeginRound, false),
		FMath::Max(0.01f, CountdownSeconds), false);
}

void ATN_TctGameMode::BeginRound(bool bFromLoading)
{
	GetWorldTimerManager().ClearTimer(PhaseClockHandle);
	GetWorldTimerManager().ClearTimer(PhaseEndHandle);
	ATN_TctGameState* State = GetTctState();
	if (State)
	{
		State->PhaseSecondsLeft = 0.f;
		State->CountdownValue = 0;
	}
	if (!bFromLoading)
	{
		// La primera ronda ya pasó a InProgress con la base (rompe el huevo con «¡ADELANTE!»); las siguientes, aquí.
		MatchStartServerTime = GetWorld()->GetTimeSeconds();
		SetFlowState(ETNMatchFlowState::InProgress);
	}

	StartingPlayers = 0;
	for (const FTNTctFighter& Fighter : GatherFighters())
	{
		StartingPlayers += (Fighter.bConnected && Fighter.bAlive) ? 1 : 0;
	}
	if (CurrentRound == 1)
	{
		bSoloMatch = StartingPlayers < TNTctRules::MinPlayers;
		if (bSoloMatch)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[TcT] Partida de una sola tortuga (prueba): las rondas acaban al caer o con el tiempo."));
		}
	}

	bRoundLive = true;
	bTimeUp = false;
	UnfreezePlayers();
	StartItemPads();

	if (State)
	{
		const float Now = static_cast<float>(State->GetServerWorldTimeSeconds());
		FTNTctFloodState& Flood = State->Flood;
		Flood.StartServerTime = Now;
		Flood.HoldZ = FloodPlan.BaseZ;
		Flood.BaseZ = FloodPlan.BaseZ;
		Flood.Levels = FloodPlan.Levels;
		Flood.SuddenDeathZ = FloodPlan.SuddenDeathZ;
		Flood.StartDelay = FloodPlan.StartDelay;
		Flood.StepSeconds = FloodPlan.StepSeconds;
		Flood.RiseSeconds = FloodPlan.RiseSeconds;
		Flood.SuddenDeathRiseSeconds = FloodPlan.SuddenDeathRiseSeconds;
		State->FightersAlive = StartingPlayers;
		State->RoundTimeLimitSeconds = RoundTimeLimitSeconds;
		State->RoundEndServerTime = Now + RoundTimeLimitSeconds;
		State->ForceNetUpdate();
	}
	SetPhase(ETNBeachRacePhase::Racing);
	SyncRoundInfo();

	GetWorldTimerManager().SetTimer(WatchHandle, this, &ATN_TctGameMode::WatchFighters, TNTctRoundDetail::WatchInterval, true);
	GetWorldTimerManager().SetTimer(RoundLimitHandle, this, &ATN_TctGameMode::OnRoundTimeLimit, RoundTimeLimitSeconds, false);
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] ¡Ronda %d en juego con %d tortugas!"), CurrentRound, StartingPlayers);
}

// ─────────────────────────────────────────────────────────────────────────────
// Caídas y cierre de la ronda
// ─────────────────────────────────────────────────────────────────────────────

void ATN_TctGameMode::WatchFighters()
{
	const ATN_TctGameState* State = GetTctState();
	if (!bRoundLive || !State)
	{
		return;
	}
	const float WaterZ = State->GetWaterZ();
	for (APlayerController* PC : GetPlayingControllers())
	{
		const ATN_CoopPlayerState* PS = PC->GetPlayerState<ATN_CoopPlayerState>();
		if (!bRoundLive || !PS || !PS->bIsAlive)
		{
			continue;
		}
		const APawn* Pawn = PC->GetPawn();
		if (!Pawn)
		{
			// El motor la ha destruido (por debajo del KillZ del nivel): ha caído.
			UE_LOG(LogTortunabo, Log, TEXT("[TcT] '%s' sin tortuga en plena ronda: eliminada."), *PS->GetPlayerName());
			MarkPlayerDead(PC);
			continue;
		}
		FTNTctBody Body;
		Body.Location = Pawn->GetActorLocation();
		const ACharacter* Character = Cast<ACharacter>(Pawn);
		Body.HalfHeight = Character && Character->GetCapsuleComponent()
			? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : TNTctRoundDetail::DefaultHalfHeight;
		ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PC->GetPawn());
		UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOn(Turtle);
		const ETNTctFall Cause = TNTctRules::FallCause(Body, ArenaBounds, WaterZ);
		// El agua es veneno (#831): tocarla no mata, intoxica mientras se está dentro (el flotador salva, #777) y se elimina al
		// llegar al máximo. Caer fuera de la arena elimina como siempre.
		bool bEliminated = Cause == ETNTctFall::OutOfArena;
		if (!bEliminated)
		{
			bEliminated = Effects ? Effects->ServerTickWater(Cause == ETNTctFall::Water) : Cause == ETNTctFall::Water;
		}
		if (bEliminated)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[TcT] '%s' %s (pies a %.0f, agua a %.0f): eliminada."),
				*PS->GetPlayerName(), Cause == ETNTctFall::OutOfArena ? TEXT("cae fuera de la arena") : TEXT("muere envenenada"),
				Body.Location.Z - Body.HalfHeight, WaterZ);
			MarkPlayerDead(PC);
			continue;
		}
		if (Effects && Effects->ServerTakeRescue())
		{
			RescueFromWater(Turtle, WaterZ);
		}
	}
}

void ATN_TctGameMode::UpdateRoundProgressAndMaybeFinish()
{
	if (!bRoundLive)
	{
		return;
	}
	if (ATN_TctGameState* State = GetTctState())
	{
		int32 Alive = 0;
		for (const FTNTctFighter& Fighter : GatherFighters())
		{
			Alive += (Fighter.bConnected && Fighter.bAlive) ? 1 : 0;
		}
		State->FightersAlive = Alive;
	}
	// Se decide un poco después: dos caídas casi a la vez son un empate, no una ganadora.
	if (!GetWorldTimerManager().IsTimerActive(DecisionHandle))
	{
		GetWorldTimerManager().SetTimer(DecisionHandle, this, &ATN_TctGameMode::EvaluateRound, FMath::Max(0.01f, DecisionGraceSeconds), false);
	}
}

void ATN_TctGameMode::OnRoundTimeLimit()
{
	if (!bRoundLive)
	{
		return;
	}
	bTimeUp = true;
	GetWorldTimerManager().ClearTimer(DecisionHandle);
	EvaluateRound();
}

void ATN_TctGameMode::EvaluateRound()
{
	if (!bRoundLive)
	{
		return;
	}
	const FTNTctRoundDecision Decision = TNTctRules::DecideRound(GatherFighters(), StartingPlayers, bTimeUp);
	switch (Decision.Outcome)
	{
	case ETNTctRoundOutcome::Winner:
		EndRound(FindPlayerState(Decision.WinnerId));
		break;
	case ETNTctRoundOutcome::Draw:
		EndRound(nullptr);
		break;
	default:
		break;
	}
}

void ATN_TctGameMode::EndRound(ATN_CoopPlayerState* Winner)
{
	if (!bRoundLive)
	{
		return;
	}
	bRoundLive = false;
	GetWorldTimerManager().ClearTimer(WatchHandle);
	GetWorldTimerManager().ClearTimer(DecisionHandle);
	GetWorldTimerManager().ClearTimer(RoundLimitHandle);

	StopItemPads();
	ATN_TctGameState* State = GetTctState();
	// El agua se queda donde está durante el recuento.
	HoldWater(State ? State->GetWaterZ() : FloodPlan.BaseZ);
	if (Winner)
	{
		// Una concha entera por ronda ganada (el recuento de la carrera cuenta en medias).
		Winner->RaceShellHalves += 2;
		++Winner->RoundWins;
		Winner->ForceNetUpdate();
	}
	if (State)
	{
		State->RoundWinner = Winner;
		State->RoundHalfShells.Reset();
		State->RoundEndReason = (bTimeUp && !Winner) ? ETNBeachRoundEnd::TimeLimit : ETNBeachRoundEnd::None;
		State->RoundEndServerTime = 0.f;
		State->FinishCountdown = ETNBeachFinishCountdown::None;
	}
	for (APlayerController* PC : GetPlayingControllers())
	{
		FreezePlayer(PC);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] ═══ Fin de la ronda %d: %s%s ═══"), CurrentRound,
		Winner ? *FString::Printf(TEXT("gana '%s' (%d rondas)"), *Winner->GetPlayerName(), Winner->RoundWins) : TEXT("empate"),
		bTimeUp ? TEXT(" (tiempo agotado)") : TEXT(""));

	if (ContinueOrFinish())
	{
		return;
	}
	SetPhase(ETNBeachRacePhase::RoundResults);
	SyncRoundInfo();
	BeginPhaseClock(RoundResultsSeconds);
	GetWorldTimerManager().SetTimer(PhaseEndHandle, this, &ATN_TctGameMode::AfterRoundResults, RoundResultsSeconds, false);
}

void ATN_TctGameMode::AfterRoundResults()
{
	if (ContinueOrFinish())
	{
		return;
	}
	PrepareRound();
}

bool ATN_TctGameMode::ContinueOrFinish()
{
	if (bMatchOver)
	{
		return true;
	}
	const FTNTctMatchDecision Decision = TNTctRules::DecideMatch(GatherFighters(), WinsToWin, bSoloMatch);
	switch (Decision.Outcome)
	{
	case ETNTctMatchOutcome::Champion:
		EnterChampion(FindPlayerState(Decision.ChampionId));
		return true;
	case ETNTctMatchOutcome::Abandoned:
		UE_LOG(LogTortunabo, Log, TEXT("[TcT] No queda nadie en la partida: vuelta al lobby."));
		bMatchOver = true;
		CancelRoundTimers();
		FinishRoundAndReturnToLobby();
		return true;
	default:
		return false;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Campeona y «Volver a jugar»
// ─────────────────────────────────────────────────────────────────────────────

void ATN_TctGameMode::EnterChampion(ATN_CoopPlayerState* Champion)
{
	CancelRoundTimers();
	bRoundLive = false;
	bMatchOver = true;
	for (APlayerController* PC : GetPlayingControllers())
	{
		FreezePlayer(PC);
	}

	ATN_TctGameState* State = GetTctState();
	if (State)
	{
		const TArray<int32> Ranked = TNTctRules::RankFighters(GatherFighters(), Champion ? Champion->GetPlayerId() : INDEX_NONE);
		State->Champion = Champion;
		State->Podium.Reset();
		State->RaceResults.Reset();
		for (int32 Index = 0; Index < Ranked.Num(); ++Index)
		{
			ATN_CoopPlayerState* PS = FindPlayerState(Ranked[Index]);
			if (!PS)
			{
				continue;
			}
			if (State->Podium.Num() < 3)
			{
				State->Podium.Add(PS);
			}
			// La tabla de siempre (HUD de resultados y música de fin de partida): puesto por rondas ganadas.
			State->Server_UpsertRaceResult(PS->GetPlayerId(), PS->GetPlayerName(), Index + 1, 0.f, PS->RoundWins, false);
		}
		SetFlowState(ETNMatchFlowState::Results);
		State->CountdownValue = TNTctRoundDetail::ChampionHoldCountdown;
		State->PhaseSecondsLeft = 0.f;
	}
	SetPhase(ETNBeachRacePhase::Champion);
	SyncRoundInfo();
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] ═══ ¡%s es la campeona tras %d rondas! Esperando al anfitrión. ═══"),
		Champion ? *Champion->GetPlayerName() : TEXT("nadie"), CurrentRound);
}

void ATN_TctGameMode::PlayAgain()
{
	if (!HasAuthority() || bLeaving)
	{
		return;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] Volver a jugar: otra partida en la misma arena."));
	CancelRoundTimers();
	bMatchOver = false;
	ResetMatchScores();
	CurrentRound = 0;
	if (ATN_TctGameState* State = GetTctState())
	{
		State->Champion = nullptr;
		State->Podium.Reset();
		State->RaceResults.Reset();
		State->CountdownValue = 0;
	}
	SetFlowState(ETNMatchFlowState::WaitingForPlayers);
	PrepareRound();
}

// ─────────────────────────────────────────────────────────────────────────────
// Estado compartido
// ─────────────────────────────────────────────────────────────────────────────

void ATN_TctGameMode::CancelRoundTimers()
{
	FTimerManager& Timers = GetWorldTimerManager();
	Timers.ClearTimer(PhaseClockHandle);
	Timers.ClearTimer(PhaseEndHandle);
	Timers.ClearTimer(WatchHandle);
	Timers.ClearTimer(DecisionHandle);
	Timers.ClearTimer(RoundLimitHandle);
}

void ATN_TctGameMode::SetPhase(ETNBeachRacePhase Phase) const
{
	if (ATN_TctGameState* State = GetTctState())
	{
		State->RacePhase = Phase;
		State->NotifyRacePhaseChanged();
		// Un GameState quieto baja su ritmo de réplica: la fase nueva sale ya.
		State->ForceNetUpdate();
	}
}

void ATN_TctGameMode::BeginPhaseClock(float Seconds)
{
	PhaseEndTime = GetWorld()->GetTimeSeconds() + FMath::Max(0.f, Seconds);
	TickPhaseClock();
	GetWorldTimerManager().SetTimer(PhaseClockHandle, this, &ATN_TctGameMode::TickPhaseClock, 0.25f, true);
}

void ATN_TctGameMode::TickPhaseClock()
{
	const float Left = FMath::Max(0.f, PhaseEndTime - GetWorld()->GetTimeSeconds());
	if (ATN_TctGameState* State = GetTctState())
	{
		// PhaseSecondsLeft para las pantallas de la carrera; CountdownValue para la cuenta de siempre (música y HUD).
		State->PhaseSecondsLeft = Left;
		State->CountdownValue = FMath::CeilToInt(Left);
	}
	if (Left <= 0.f)
	{
		GetWorldTimerManager().ClearTimer(PhaseClockHandle);
	}
}

void ATN_TctGameMode::HoldWater(float Z) const
{
	if (ATN_TctGameState* State = GetTctState())
	{
		State->Flood.StartServerTime = -1.f;
		State->Flood.HoldZ = Z;
		State->ForceNetUpdate();
	}
}

void ATN_TctGameMode::SyncRoundInfo() const
{
	ATN_TctGameState* State = GetTctState();
	if (!State)
	{
		return;
	}
	State->ProcMode = ETNProcGameMode::FreeForAll;
	State->CurrentRound = CurrentRound;
	State->RoundTarget = WinsToWin;
	State->bRoundInProgress = bRoundLive;
	State->MapSeed = 0;
	State->NotifyRoundInfoChanged();
}
