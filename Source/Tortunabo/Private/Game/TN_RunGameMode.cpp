#include "Game/TN_RunGameMode.h"
#include "Game/TN_MatchStartRules.h"
#include "Game/TN_UnderTerrainGuard.h"
#include "Core/TN_Log.h"
#include "Core/TN_CoopGameState.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_GameModeSpawnUtils.h"
#include "Player/MP_GamePlayerController.h"
#include "Player/TortugaCharacter.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Multiplayer/TN_TravelFailureSubsystem.h"
#include "World/TN_RescuePickup.h"
#include "Player/TN_InventoryComponent.h"
#include "World/TN_DeathZoneVolume.h"
#include "World/TN_StormVolume.h"
#include "World/TN_ChunkManager.h"
#include "World/TN_CollectionZone.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "TimerManager.h"

ATN_RunGameMode::ATN_RunGameMode()
{
	GameStateClass = ATN_CoopGameState::StaticClass();
	PlayerStateClass = ATN_CoopPlayerState::StaticClass();
	PlayerControllerClass = AMP_GamePlayerController::StaticClass();
	DefaultPawnClass = ATortugaCharacter::StaticClass();
	bUseSeamlessTravel = true;
	UnderTerrainGuard = CreateDefaultSubobject<UTN_UnderTerrainGuardComponent>(TEXT("UnderTerrainGuard"));
}

void ATN_RunGameMode::BeginPlay()
{
	Super::BeginPlay();
	EnsureFallbackPlayerStart();

	// ── Safety check: detectar si el mapa cargó con la clase C++ base en vez del BP ──
	// Si GetClass() es exactamente ATN_RunGameMode (no un BP hijo), significa que
	// WorldSettings no tiene GameMode Override → se está usando la clase C++ base,
	// que spawna TortugaCharacter sin mesh ni input en vez de BP_TortugaCharacter.
	if (GetClass() == ATN_RunGameMode::StaticClass())
	{
		UE_LOG(LogTortunabo, Error, TEXT("[RunGameMode] ════════════════════════════════════════════════════════"));
		UE_LOG(LogTortunabo, Error, TEXT("[RunGameMode] ¡USANDO CLASE C++ BASE! No hay BP GameMode."));
		UE_LOG(LogTortunabo, Error, TEXT("[RunGameMode] Esto causa: sin tortuga visible, sin input, sin HUD."));
		UE_LOG(LogTortunabo, Error, TEXT("[RunGameMode] FIX: En LVL_Run → WorldSettings → GameMode Override"));
		UE_LOG(LogTortunabo, Error, TEXT("[RunGameMode]       → seleccionar BP_RunGameMode."));
		UE_LOG(LogTortunabo, Error, TEXT("[RunGameMode] ════════════════════════════════════════════════════════"));
	}

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		EnsurePlayerSpawned(It->Get());
	}

	NextFinishRank = 1;

	// Suscribirse a las CollectionZones del nivel para que el GameMode reaccione
	// cuando completen su goal (sumar bonus, log, futuras señales de progresión).
	for (TActorIterator<ATN_CollectionZone> It(GetWorld()); It; ++It)
	{
		if (ATN_CollectionZone* Zone = *It)
		{
			Zone->OnZoneGoalReached.AddUObject(this, &ATN_RunGameMode::HandleCollectionZoneGoal);
		}
	}

	// ── Leer cuántos jugadores había en el lobby ──────────────────────────
	if (const UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance()))
	{
		ExpectedPlayersFromLobby = FMath::Max(1, GI->PendingTravelPlayerCount);
	}
	else
	{
		ExpectedPlayersFromLobby = 1;
	}

	// ── Fase de staging: esperar a que reconecten todos los clientes ──────
	SetFlowState(ETNMatchFlowState::WaitingForPlayers);

	if (ATN_CoopGameState* TNGS = GetGameState<ATN_CoopGameState>())
	{
		TNGS->FinishedPlayers = 0;
		TNGS->ServerMatchElapsedTime = 0.f;
		TNGS->CountdownValue = 0;
		TNGS->ExpectedPlayers = ExpectedPlayersFromLobby;
	}

	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		if (ATN_CoopPlayerState* TNPS = Cast<ATN_CoopPlayerState>(BasePS))
		{
			TNPS->ResetForNewRace();
		}
	}

	UE_LOG(LogTortunabo, Log, TEXT("[RunGameMode] BeginPlay: Waiting for %d players (staging)"), ExpectedPlayersFromLobby);

	// Timeout de seguridad: si no llegan todos, arranca igual
	GetWorldTimerManager().SetTimer(WaitingTimeoutTimerHandle, this,
		&ATN_RunGameMode::OnWaitingTimeout, WaitingForPlayersTimeoutSeconds, false);

	// Abre la espera e intenta arrancar de inmediato si el host ya cuenta como 1/1. El PostLogin del jugador local
	// pudo llegar antes de este BeginPlay (LoadMap hace SpawnPlayActor antes de UWorld::BeginPlay) y no arrancó.
	const FTNMatchStartState Staged = TNMatchStartLogic::BeginStaging({ bStagingBegun, bMatchStarted });
	bStagingBegun = Staged.bStagingBegun;
	TryStartMatch();
}

FString ATN_RunGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options,
	const FString& Portal)
{
	const FString Result = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	TN_RestoreFullPlayerName(this, NewPlayerController, Options);
	return Result;
}

void ATN_RunGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	UE_LOG(LogTortunabo, Log, TEXT("[RunGameMode] PostLogin: %s  (Pawn=%s)"),
		*GetNameSafe(NewPlayer),
		NewPlayer ? *GetNameSafe(NewPlayer->GetPawn()) : TEXT("NULL"));

	// El estado del PlayerState (de cero, el que traía al volver o fuera de la partida) ya lo dejó FindInactivePlayer,
	// dentro de Super. Quien entra a mirar no lleva pawn.
	const bool bSpectating = NewPlayer && NewPlayer->PlayerState && NewPlayer->PlayerState->IsOnlyASpectator();
	if (!bSpectating)
	{
		EnsurePlayerSpawned(NewPlayer);
	}

	// Actualizar el conteo de jugadores conectados en el GameState
	if (ATN_CoopGameState* TNGS = GetGameState<ATN_CoopGameState>())
	{
		const int32 TotalPlayers = TN_CountConnectedCoopPlayers(GameState);
		TNGS->ConnectedPlayers = TotalPlayers;
		TNGS->ExpectedPlayers = ExpectedPlayersFromLobby;

		UE_LOG(LogTortunabo, Log, TEXT("[RunGameMode] PostLogin: ConnectedPlayers=%d  ExpectedFromLobby=%d  MatchStarted=%s"),
			TotalPlayers, ExpectedPlayersFromLobby, bMatchStarted ? TEXT("YES") : TEXT("NO"));
	}

	// Si aún estamos en staging, comprobar si ya tenemos a todos
	TryStartMatch();
}

void ATN_RunGameMode::Logout(AController* Exiting)
{
	// Limpiar los restos de muerte del que se va ANTES de Super::Logout (después
	// el PlayerState puede no ser accesible). Sin esto, su RescuePickup fantasma
	// seguía interactuable y su pawn-cadáver (Owner reasignado al GameMode en
	// MarkPlayerDead, así que el engine no lo destruye en cascada con el PC)
	// sobrevivía replicando hasta el fin de la ronda.
	if (const ATN_CoopPlayerState* ExitingPS = Exiting ? Exiting->GetPlayerState<ATN_CoopPlayerState>() : nullptr)
	{
		const int32 ExitingId = ExitingPS->GetPlayerId();

		if (TWeakObjectPtr<ATN_RescuePickup>* PickupPtr = RescuePickups.Find(ExitingId))
		{
			if (PickupPtr->IsValid())
			{
				PickupPtr->Get()->Destroy();
			}
			RescuePickups.Remove(ExitingId);
		}

		if (TWeakObjectPtr<APawn>* PawnPtr = DeadPlayerPawns.Find(ExitingId))
		{
			if (PawnPtr->IsValid())
			{
				PawnPtr->Get()->Destroy();
			}
			DeadPlayerPawns.Remove(ExitingId);
			UE_LOG(LogTortunabo, Log, TEXT("[RunGameMode] Logout: limpiado cadáver+pickup de PlayerId=%d"), ExitingId);
		}
	}

	PendingJoins.Remove(Cast<APlayerController>(Exiting));
	Super::Logout(Exiting);

	// Actualizar conteo tras desconexión (TN_CountConnectedCoopPlayers ya no cuenta al que se va, #558)
	if (ATN_CoopGameState* TNGS = GetGameState<ATN_CoopGameState>())
	{
		TNGS->ConnectedPlayers = TN_CountConnectedCoopPlayers(GameState);
		// ExpectedPlayers se mantiene desde el lobby para que la UI muestre "X / ExpectedFromLobby"
	}

	UE_LOG(LogTortunabo, Log, TEXT("[RunGameMode] Logout: %s"), *GetNameSafe(Exiting));

	// Comprobar si todos los jugadores restantes han terminado
	if (bMatchStarted)
	{
		UpdateRoundProgressAndMaybeFinish();
	}
	else
	{
		// Si aún estamos en staging y alguien se desconecta,
		// reducir la expectativa para no esperarle
		ExpectedPlayersFromLobby = FMath::Max(1, ExpectedPlayersFromLobby - 1);
		if (ATN_CoopGameState* TNGS = GetGameState<ATN_CoopGameState>())
		{
			TNGS->ExpectedPlayers = ExpectedPlayersFromLobby;
		}
		TryStartMatch();
	}
}

// ── Staging: esperar a todos los jugadores antes de empezar ────────────────────

void ATN_RunGameMode::TryStartMatch()
{
	// Antes del BeginPlay (PostLogin del host durante LoadMap) o con la partida ya empezada no hay nada que arrancar.
	const FTNMatchStartState State{ bStagingBegun, bMatchStarted };
	if (!TNMatchStartLogic::IsWaitingForPlayers(State))
	{
		return;
	}

	const int32 ConnectedNow = TN_CountConnectedCoopPlayers(GameState);

	UE_LOG(LogTortunabo, Log, TEXT("[RunGameMode] TryStartMatch: Connected=%d  Expected=%d"),
		ConnectedNow, ExpectedPlayersFromLobby);

	if (TNMatchStartLogic::ShouldStart(State, ConnectedNow, ExpectedPlayersFromLobby))
	{
		OnWaitingTimeout(); // Reutiliza la misma función de arranque
	}
}

void ATN_RunGameMode::OnWaitingTimeout()
{
	if (bMatchStarted)
	{
		return;
	}

	bMatchStarted = true;
	GetWorldTimerManager().ClearTimer(WaitingTimeoutTimerHandle);

	MatchStartServerTime = GetWorld()->GetTimeSeconds();
	SetFlowState(ETNMatchFlowState::InProgress);

	// ── Iniciar cronómetro de carrera (actualiza ServerMatchElapsedTime cada 0.5s) ──
	GetWorldTimerManager().SetTimer(RaceClockTimerHandle, this,
		&ATN_RunGameMode::TickRaceClock, 0.5f, true);

	// Actualizar conteo final
	if (ATN_CoopGameState* TNGS = GetGameState<ATN_CoopGameState>())
	{
		const int32 TotalPlayers = TN_CountConnectedCoopPlayers(GameState);
		TNGS->ConnectedPlayers = TotalPlayers;
		TNGS->ExpectedPlayers = TotalPlayers; // Ahora sí es "de verdad"
	}

	UE_LOG(LogTortunabo, Log, TEXT("[RunGameMode] ═══ MATCH STARTED! ═══  (waited for players or timeout)"));
}

void ATN_RunGameMode::DropStaleConnectionOf(const APlayerController* NewPlayer)
{
	const FUniqueNetIdRepl NewId = NewPlayer && NewPlayer->PlayerState ? NewPlayer->PlayerState->GetUniqueId() : FUniqueNetIdRepl();
	if (!NewId.IsValid())
	{
		return;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Other = It->Get();
		if (!Other || Other == NewPlayer || Other->IsLocalController() || !Other->PlayerState || Other->PlayerState->GetUniqueId() != NewId)
		{
			continue;
		}
		// Se cerró el juego y vuelve antes de que el servidor dé su conexión por perdida (ConnectionTimeout): la vieja
		// se despide ya, como en AGameSession::KickPlayer, para que su Logout guarde el PlayerState que se va a recuperar.
		UE_LOG(LogTortunabo, Log, TEXT("[Join] %s vuelve con la conexión anterior aún abierta: se cierra %s."),
			*Other->PlayerState->GetPlayerName(), *GetNameSafe(Other));
		Other->Destroy();
		return;
	}
}

bool ATN_RunGameMode::FindInactivePlayer(APlayerController* PC)
{
	DropStaleConnectionOf(PC);
	const bool bReactivated = Super::FindInactivePlayer(PC);
	const ATN_CoopPlayerState* TNPS = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
	if (!TNPS)
	{
		return bReactivated;
	}

	FTNJoinContext Context;
	Context.Policy = GetLateJoinPolicy();
	Context.bMatchInProgress = IsMatchInProgressForJoin();
	Context.bReactivated = bReactivated;
	Context.bWasAlive = TNPS->bIsAlive;
	Context.bHadFinished = TNPS->bHasFinishedRun;
	const FTNJoinDecision Decision = TNLateJoinLogic::DecideJoin(Context);
	ApplyJoinDecision(PC, Decision);
	PendingJoins.Add(PC, Decision);

	UE_LOG(LogTortunabo, Log, TEXT("[Join] %s · %s · partida %s · rol %d · reinicia %d · fuera %d · vivo %d · meta %d · puntos %d"),
		*TNPS->GetPlayerName(), bReactivated ? TEXT("vuelve") : TEXT("nuevo"), Context.bMatchInProgress ? TEXT("en juego") : TEXT("sin empezar"),
		static_cast<int32>(Decision.Role), Decision.bResetRaceState, Decision.bSitsOut, TNPS->bIsAlive, TNPS->bHasFinishedRun, TNPS->RaceScore);
	return bReactivated;
}

void ATN_RunGameMode::AddInactivePlayer(APlayerState* PlayerState, APlayerController* PC)
{
	// AGameMode no guarda a quien MustSpectate: los muertos y los que llegaron a la meta, que esperan como espectadores.
	// Siguen en la partida y su estado tiene que volver con ellos. Quien entró solo a mirar (SitOut) no se guarda.
	const bool bRaceSpectator = PlayerState && PlayerState->IsOnlyASpectator() && !SitOutPlayerIds.Contains(PlayerState->GetPlayerId());
	if (!bRaceSpectator)
	{
		Super::AddInactivePlayer(PlayerState, PC);
		return;
	}
	PlayerState->SetIsOnlyASpectator(false);
	Super::AddInactivePlayer(PlayerState, PC);
	PlayerState->SetIsOnlyASpectator(true);
}

void ATN_RunGameMode::ApplyJoinDecision(APlayerController* PlayerController, const FTNJoinDecision& Decision)
{
	ATN_CoopPlayerState* TNPS = PlayerController ? PlayerController->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
	if (!TNPS)
	{
		return;
	}
	// En el viaje no sin cortes los PlayerStates se recrean: el reinicio garantiza que todos arranquen limpios.
	if (Decision.bResetRaceState)
	{
		TNPS->ResetForNewRace();
	}
	if (Decision.bSitsOut)
	{
		TNPS->bIsAlive = false;
		SitOutPlayerIds.Add(TNPS->GetPlayerId());
	}
	else
	{
		SitOutPlayerIds.Remove(TNPS->GetPlayerId());
	}
	TNPS->ForceNetUpdate();
}

bool ATN_RunGameMode::StartJoiningPlayer(APlayerController* PlayerController, const FTNJoinDecision& Decision)
{
	if (Decision.Role != ETNJoinRole::Spectate)
	{
		return false;
	}
	if (Decision.bSitsOut)
	{
		SitOutAsSpectator(PlayerController);
	}
	else
	{
		MovePlayerToSpectator(PlayerController);
	}
	return true;
}

void ATN_RunGameMode::SitOutAsSpectator(APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		return;
	}
	if (ATN_CoopPlayerState* TNPS = PlayerController->GetPlayerState<ATN_CoopPlayerState>())
	{
		TNPS->bIsAlive = false;
		TNPS->bIsDBNO = false;
		SitOutPlayerIds.Add(TNPS->GetPlayerId());
		TNPS->ForceNetUpdate();
	}
	if (APawn* Pawn = PlayerController->GetPawn())
	{
		PlayerController->UnPossess();
		Pawn->Destroy();
	}
	MovePlayerToSpectator(PlayerController);
}

void ATN_RunGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Solo los que llegan por PostLogin traen decisión; el viaje sin cortes arranca como siempre.
	FTNJoinDecision Decision;
	const bool bFromLogin = PendingJoins.RemoveAndCopyValue(NewPlayer, Decision);
	if (bFromLogin && StartJoiningPlayer(NewPlayer, Decision))
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Join] %s entra como espectador."), *GetNameSafe(NewPlayer));
		return;
	}

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
	EnsurePlayerSpawned(NewPlayer);
	if (bFromLogin && Decision.Role == ETNJoinRole::PlayOnPath && !PlaceMidMatchJoiner(NewPlayer))
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Join] %s: sin sitio seguro en el camino, espera como espectador."), *GetNameSafe(NewPlayer));
		SitOutAsSpectator(NewPlayer);
	}

	UE_LOG(LogTortunabo, Log, TEXT("[RunGameMode] HandleStartingNewPlayer: %s  (Pawn=%s)"),
		*GetNameSafe(NewPlayer),
		NewPlayer ? *GetNameSafe(NewPlayer->GetPawn()) : TEXT("NULL"));
}

AActor* ATN_RunGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	TArray<AActor*> PlayerStarts;
	UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), PlayerStarts);
	if (PlayerStarts.Num() == 0)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	// LVL_Run trae cuatro PlayerStart y caben ocho jugadores: los que sobran salen en sitios nuevos junto a los del mapa.
	if (AActor* Start = TN_PickSpreadPlayerStart(GetWorld(), PlayerStarts, Player, DefaultPawnClass, TEXT("Run")))
	{
		return Start;
	}

	// Fallback: todos ocupados y sin hueco cerca → devolver el primero (barajado)
	UE_LOG(LogTortunabo, Warning, TEXT("[RunGameMode] ChoosePlayerStart: todos los PlayerStarts ocupados — usando fallback"));
	return PlayerStarts[0];
}

void ATN_RunGameMode::EnsurePlayerSpawned(APlayerController* PlayerController)
{
	TN_EnsurePlayerSpawned(this, PlayerController, [this]() { return EnsureFallbackPlayerStart(); }, TEXT("Run"));
}

APlayerStart* ATN_RunGameMode::EnsureFallbackPlayerStart()
{
	return TN_EnsureFallbackPlayerStart(GetWorld(), TEXT("RunFallbackPlayerStart"), TEXT("Run"), TEXT("run map"));
}

void ATN_RunGameMode::MarkPlayerFinished(APlayerController* PlayerController)
{
	if (!HasAuthority() || !PlayerController)
	{
		return;
	}

	ATN_CoopPlayerState* TNPS = PlayerController->GetPlayerState<ATN_CoopPlayerState>();
	if (!TNPS)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[MarkPlayerFinished] '%s' — sin PlayerState, ignorado."), *GetNameSafe(PlayerController));
		return;
	}
	if (TNPS->bHasFinishedRun)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[MarkPlayerFinished] '%s' — ya terminó (bHasFinishedRun=true), ignorado."), *GetNameSafe(PlayerController));
		return;
	}
	if (!TNPS->bIsAlive)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[MarkPlayerFinished] '%s' — bIsAlive=false, ignorado (¿murió antes de llegar?)."), *GetNameSafe(PlayerController));
		return;
	}

	UE_LOG(LogTortunabo, Log, TEXT("[MarkPlayerFinished] ══ '%s' cruzó la meta ══ Rank=%d"), *GetNameSafe(PlayerController), NextFinishRank);
	TNPS->bHasFinishedRun = true;
	TNPS->FinishTimeSeconds = GetWorld()->GetTimeSeconds() - MatchStartServerTime;
	TNPS->FinishRank = NextFinishRank++;
	// El PlayerState replica a 5 Hz (1 Hz en reposo): los cambios de estado salen ya.
	TNPS->ForceNetUpdate();

	// ── Asignar puntos finales: RankScore + TimeBonus ────────────────────────
	// El RaceScore actual ya contiene los puntos de ScorePickups recogidos
	// durante la run + bonus de CollectionZones. Aquí sumamos los componentes
	// finales: posición de llegada y bonus por velocidad.
	// Ocho puestos (partidas de hasta ocho): del quinto en adelante bajan poco a poco hasta los 50 de siempre para el resto.
	static const int32 RankScoreTable[] = { 400, 300, 200, 100, 80, 65, 55, 50 };
	const int32 RankIndex = TNPS->FinishRank - 1;
	const int32 RankScore = (RankIndex >= 0 && RankIndex < static_cast<int32>(UE_ARRAY_COUNT(RankScoreTable))) ? RankScoreTable[RankIndex] : 50;

	// TimeBonus: premia llegar antes del baseline. Capeado a 0 (no negativo).
	const float TimeUnderBaseline = TimeBonusBaselineSeconds - TNPS->FinishTimeSeconds;
	const int32 TimeBonus = FMath::Max(0, FMath::FloorToInt(TimeUnderBaseline * TimeBonusPointsPerSecond));

	const int32 PickupAndZoneScore = TNPS->RaceScore;  // lo que llevaba antes de finish
	TNPS->AddRaceScore(RankScore + TimeBonus);         // difunde OnRaceScoreChanged en el host

	UE_LOG(LogTortunabo, Log, TEXT("[FINISH] '%s' Rank=%d Time=%.1fs · Rank+%d · Pickups+%d · TimeBonus+%d → Total=%d"),
		*GetNameSafe(PlayerController), TNPS->FinishRank, TNPS->FinishTimeSeconds,
		RankScore, PickupAndZoneScore, TimeBonus, TNPS->RaceScore);

	// Scoreboard global
	if (ATN_CoopGameState* GS = GetGameState<ATN_CoopGameState>())
	{
		GS->Server_UpsertRaceResult(TNPS->GetPlayerId(), TNPS->GetPlayerName(),
			TNPS->FinishRank, TNPS->FinishTimeSeconds, TNPS->RaceScore, false);
	}

	// Limpiar DBNO si llegaron a meta mientras estaban noqueados
	if (TNPS->bIsDBNO)
	{
		TNPS->bIsDBNO = false;
		TNPS->DBNOBleedoutTimeRemaining = -1.f;
		DBNOPlayers.Remove(PlayerController);
		if (DBNOPlayers.Num() == 0)
		{
			GetWorldTimerManager().ClearTimer(DBNOBleedoutTimerHandle);
		}
	}

	// ── Detener el pawn y ocultarlo para que no se vea en la meta ──
	if (APawn* Pawn = PlayerController->GetPawn())
	{
		if (ACharacter* Ch = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* CMC = Ch->GetCharacterMovement())
			{
				CMC->StopMovementImmediately();
				CMC->DisableMovement();
			}
		}
		Pawn->DisableInput(PlayerController);

		// Ocultar al jugador que terminó (su personaje "desaparece")
		Pawn->SetActorHiddenInGame(true);
		Pawn->SetActorEnableCollision(false);
	}

	MovePlayerToSpectator(PlayerController);
	UpdateRoundProgressAndMaybeFinish();
}

void ATN_RunGameMode::MarkPlayerDeadBy(APlayerController* PlayerController, ETNDeathCause Cause)
{
	// Las subclases pasan por aquí con Super::MarkPlayerDead: la causa llega hasta donde se elimina.
	const ETNDeathCause Previous = PendingDeathCause;
	PendingDeathCause = Cause;
	MarkPlayerDead(PlayerController);
	PendingDeathCause = Previous;
}

void ATN_RunGameMode::MarkPlayerDead(APlayerController* PlayerController)
{
	if (!HasAuthority() || !PlayerController)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[DEATH-EARLY-OUT] MarkPlayerDead cancelado · HasAuthority=%d PC=%s"),
			HasAuthority(), *GetNameSafe(PlayerController));
		return;
	}

	ATN_CoopPlayerState* TNPS = PlayerController->GetPlayerState<ATN_CoopPlayerState>();
	if (!TNPS || !TNPS->bIsAlive)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[DEATH-EARLY-OUT] '%s' · TNPS=%s bIsAlive=%d (ya muerto o sin PS)"),
			*GetNameSafe(PlayerController),
			TNPS ? TEXT("OK") : TEXT("NULL"),
			TNPS ? TNPS->bIsAlive : -1);
		return;
	}
	// Un jugador que ya cruzó la meta no puede morir retroactivamente.
	// Evita que un countdown de death zone rezagado sobreescriba el rank ganado.
	if (TNPS->bHasFinishedRun)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[DEATH-EARLY-OUT] '%s' · bHasFinishedRun=true (ya cruzó meta)"),
			*GetNameSafe(PlayerController));
		return;
	}

	// ── Tótem auto-revive: si el jugador lleva un tótem en el inventario,
	//    se consume automáticamente y cancela la muerte. ─────────────────────────
	if (TryTotemAutoRevive(PlayerController))
	{
		return;
	}

	TNPS->bIsAlive = false;
	TNPS->bHasFinishedRun = true;
	TNPS->bIsDBNO = false;
	TNPS->DBNOBleedoutTimeRemaining = -1.f;
	// Los eliminados NO consumen un puesto de carrera (NextFinishRank sólo avanza al cruzar la meta).
	// FinishRank = 0 para eliminados; la UI usa bIsEliminated para distinguirlos.
	// Guardamos el tiempo real de muerte para que la UI pueda ordenar eliminados por tiempo.
	TNPS->FinishRank = 0;
	TNPS->bIsEliminated = true;
	TNPS->DeathCause = bRecordDeathCause ? PendingDeathCause : ETNDeathCause::Unknown;
	TNPS->FinishTimeSeconds = GetWorld()->GetTimeSeconds() - MatchStartServerTime;
	TNPS->DeathZoneTimeRemaining = -1.f;
	TNPS->ForceNetUpdate();

	UE_LOG(LogTortunabo, Log, TEXT("[DEATH] '%s' eliminated · time=%.2fs · score=%d · causa=%s"),
		*GetNameSafe(PlayerController), TNPS->FinishTimeSeconds, TNPS->RaceScore, *UEnum::GetValueAsString(TNPS->DeathCause));

	// Scoreboard global — eliminado al final del array
	if (ATN_CoopGameState* GS = GetGameState<ATN_CoopGameState>())
	{
		GS->Server_UpsertRaceResult(TNPS->GetPlayerId(), TNPS->GetPlayerName(),
			0, TNPS->FinishTimeSeconds, TNPS->RaceScore, true);
	}

	// Clean up from DBNO tracking if present
	DBNOPlayers.Remove(PlayerController);
	ReviveImmunePlayers.Remove(PlayerController);

	// Cancelar el timer de inmunidad post-revive si sigue activo.
	// Sin esto, un timer antiguo puede eliminar la inmunidad de una segunda revivida.
	if (FTimerHandle* T = ImmunityTimers.Find(PlayerController))
	{
		GetWorldTimerManager().ClearTimer(*T);
		ImmunityTimers.Remove(PlayerController);
	}

	FVector DeathLocation = FVector::ZeroVector;

	if (APawn* Pawn = PlayerController->GetPawn())
	{
		DeathLocation = Pawn->GetActorLocation();
		ApplyDeathVisuals(Pawn, PlayerController);
	}

	// ── Spawnear pickup de rescate en la posición de muerte ──
	SpawnRescuePickupForDeath(TNPS, DeathLocation, PlayerController);

	// ── Guardar referencia al pawn ANTES de entrar en espectador ──────────
	// EnterSpectateMode → ChangeState(Spectating) → UnPossess → GetPawn() devuelve nullptr.
	// Sin esta referencia, RevivePlayer no podría encontrar el pawn para re-poseerlo.
	if (APawn* DeadPawn = PlayerController->GetPawn())
	{
		DeadPlayerPawns.Add(TNPS->GetPlayerId(), DeadPawn);
		UE_LOG(LogTortunabo, Log, TEXT("[Death] Saved pawn ref for PlayerId=%d → %s"), TNPS->GetPlayerId(), *GetNameSafe(DeadPawn));

		// FIX ragdoll despawn intermitente: cambiar el Owner del pawn muerto al
		// GameMode. Sin este cambio, si el PC hace Logout (timeout, quit,
		// PIE-shutdown), UE engine puede destruir cascada los actors Owner=PC
		// → ragdoll se evapora junto al PC. Owner=GameMode desliga el pawn del
		// ciclo de vida del PC. El pawn solo se destruirá explícitamente desde
		// RevivePlayer (cuando se reataca el mesh) o FinishRoundAndReturnToLobby.
		DeadPawn->SetOwner(this);
		// Garantía adicional: lifespan infinito (por si algo lo asignó por otro lado).
		DeadPawn->SetLifeSpan(0.f);
		UE_LOG(LogTortunabo, Log, TEXT("[Death] Detached pawn from PC ownership · pawn=%s now owned by GameMode"),
			*GetNameSafe(DeadPawn));
	}

	MovePlayerToSpectator(PlayerController);
	UpdateRoundProgressAndMaybeFinish();
}

bool ATN_RunGameMode::TryTotemAutoRevive(APlayerController* PlayerController)
{
	if (APawn* DyingPawn = PlayerController->GetPawn())
	{
		if (ATortugaCharacter* DyingChar = Cast<ATortugaCharacter>(DyingPawn))
		{
			if (UTN_InventoryComponent* Inv = DyingChar->GetInventoryComponent())
			{
				FTN_InventoryItem ConsumedTotem;
				if (Inv->TryConsumeItemByUseType(ETN_ItemUseType::Totem, ConsumedTotem))
				{
					// Tótem consumido → cancelar muerte + feedback visual
					UE_LOG(LogTortunabo, Log, TEXT("[Totem] Auto-revive activado para %s — totem consumido."),
						*GetNameSafe(PlayerController));
					DyingChar->Multicast_OnTotemAutoRevive();
					return true;
				}
			}
		}
	}
	return false;
}

void ATN_RunGameMode::BankRoundScoresToProfiles()
{
	if (!GameState)
	{
		return;
	}
	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		if (ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS))
		{
			PS->BankRoundScoreToProfile();
		}
	}
}

void ATN_RunGameMode::ApplyDeathVisuals(APawn* Pawn, APlayerController* PlayerController)
{
	if (ATortugaCharacter* Character = Cast<ATortugaCharacter>(Pawn))
	{
		// Sin el arpegio de reanimar: muere, no se levanta (#348).
		Character->RecoverFromKnockdownSilently();
	}

	if (ACharacter* Ch = Cast<ACharacter>(Pawn))
	{
		if (UCharacterMovementComponent* CMC = Ch->GetCharacterMovement())
		{
			CMC->StopMovementImmediately();
		}
	}
	Pawn->DisableInput(PlayerController);

	// Pawn muerto SIEMPRE relevante para todos los clientes. Sin esto, cuando el
	// PC muerto entra espectador y la cámara se aleja (ej. viendo a un compañero
	// vivo que avanza), el netcull quita el pawn muerto del cliente → ragdoll
	// "desaparece tras unos segundos" aunque el servidor lo tenga vivo. Restaurado
	// a default en RevivePlayer.
	Pawn->bAlwaysRelevant = true;
	Pawn->SetNetDormancy(DORM_Awake);

	// Q1-13: activar ragdoll de muerte — pawn queda visible (es el visual del rescate)
	if (ATortugaCharacter* Character = Cast<ATortugaCharacter>(Pawn))
	{
		Character->SetDeadVisual(true);
		// Fallback: si no hay PhysicsAsset, ragdoll imposible → ocultar pawn.
		// NO usar IsSimulatingPhysics() aquí: Chaos puede devolver false
		// inmediatamente tras SetAllBodiesSimulatePhysics(true) por asincronía
		// → el pawn se ocultaba por error aunque el ragdoll se activara bien
		// en el tick siguiente. Check estático: existe PhysicsAsset → ragdoll OK.
		USkeletalMeshComponent* SM = Character->GetMesh();
		const bool bHasRagdollSetup = SM && SM->GetPhysicsAsset();
		if (!bHasRagdollSetup)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Death] Sin PhysicsAsset en BP — ocultando pawn %s (fallback sin ragdoll)"), *GetNameSafe(Pawn));
			Pawn->SetActorHiddenInGame(true);
			Pawn->SetActorEnableCollision(false);
		}
	}
	else
	{
		Pawn->SetActorHiddenInGame(true);
		Pawn->SetActorEnableCollision(false);
	}
}

void ATN_RunGameMode::SpawnRescuePickupForDeath(ATN_CoopPlayerState* TNPS, const FVector& DeathLocation, APlayerController* PlayerController)
{
	if (!bAllowRevive)
	{
		return;
	}

	if (RescuePickupClass)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ATN_RescuePickup* Pickup = GetWorld()->SpawnActor<ATN_RescuePickup>(
			RescuePickupClass, DeathLocation + FVector(0.f, 0.f, 30.f), FRotator::ZeroRotator, SpawnParams);

		if (Pickup)
		{
			Pickup->SetDeadPlayerId(TNPS->GetPlayerId());
			// El ragdoll del pawn es el visual. El pickup queda invisible y solo actúa
			// como trigger de interacción, siguiendo en servidor al hueso pelvis.
			Pickup->HideInteractableMesh();
			if (APawn* DyingPawn = PlayerController->GetPawn())
			{
				Pickup->FollowDeadPawn(DyingPawn);
			}

			RescuePickups.Add(TNPS->GetPlayerId(), Pickup);
			UE_LOG(LogTortunabo, Log, TEXT("[Death] Spawned RescuePickup for %s (PlayerId=%d) at (%.0f,%.0f,%.0f)"),
				*GetNameSafe(PlayerController), TNPS->GetPlayerId(),
				DeathLocation.X, DeathLocation.Y, DeathLocation.Z);
		}
	}
	else
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Death] RescuePickupClass is not set! Assign it in BP_RunGameMode → Class Defaults."));
	}
}

// ── DBNO (Down But Not Out) ────────────────────────────────────────────────────

void ATN_RunGameMode::EnterDBNO(APlayerController* PlayerController)
{
	if (!HasAuthority() || !PlayerController)
	{
		return;
	}

	ATN_CoopPlayerState* TNPS = PlayerController->GetPlayerState<ATN_CoopPlayerState>();
	if (!TNPS || !TNPS->bIsAlive || TNPS->bIsDBNO)
	{
		return;  // Already dead, already DBNO, or invalid
	}

	// Sin reanimación (Supervivencia): nadie puede levantarle, así que muere ya.
	if (!bAllowRevive)
	{
		MarkPlayerDead(PlayerController);
		return;
	}

	// Check revive immunity — recently revived players can't be downed again immediately
	if (ReviveImmunePlayers.Contains(PlayerController))
	{
		UE_LOG(LogTortunabo, Log, TEXT("[DBNO] %s has revive immunity — ignoring DBNO trigger"), *GetNameSafe(PlayerController));
		return;
	}

	TNPS->bIsDBNO = true;
	TNPS->DBNOBleedoutTimeRemaining = DBNOBleedoutSeconds;
	TNPS->ForceNetUpdate();

	// Apply infinite knockdown (Duration=0 means permanent — we'll clear it manually on revive/death)
	if (ATortugaCharacter* Character = Cast<ATortugaCharacter>(PlayerController->GetPawn()))
	{
		Character->ApplyKnockdown(DBNOBleedoutSeconds + 5.f);  // Generous duration, death/revive clears it
	}

	// Register in DBNO tracking map
	DBNOPlayers.Add(PlayerController, DBNOBleedoutSeconds);

	// Start the shared bleedout timer if not already running
	if (!GetWorldTimerManager().IsTimerActive(DBNOBleedoutTimerHandle))
	{
		GetWorldTimerManager().SetTimer(DBNOBleedoutTimerHandle, this,
			&ATN_RunGameMode::TickDBNOBleedout, 0.1f, true);
	}

	UE_LOG(LogTortunabo, Log, TEXT("[DBNO] %s entered DBNO state (%.1fs bleedout)"), *GetNameSafe(PlayerController), DBNOBleedoutSeconds);

	// Check if ALL alive players are now in DBNO (no one to revive)
	CheckAllAliveDBNO();
}

void ATN_RunGameMode::RevivePlayer(APlayerController* PlayerController)
{
	if (!HasAuthority() || !PlayerController)
	{
		return;
	}

	ATN_CoopPlayerState* TNPS = PlayerController->GetPlayerState<ATN_CoopPlayerState>();
	if (!TNPS)
	{
		return;
	}

	const bool bWasDBNO        = TNPS->bIsDBNO;
	const bool bWasDead        = !TNPS->bIsAlive && TNPS->bIsEliminated;
	const ATortugaCharacter* TurtleCheck = Cast<ATortugaCharacter>(PlayerController->GetPawn());
	const bool bWasKnockedDown = TurtleCheck ? TurtleCheck->IsKnockedDown() : false;

	if (!bWasDBNO && !bWasDead && !bWasKnockedDown)
	{
		return; // Nothing to revive
	}

	// ── Knockdown-only revival: pawn sigue poseído, solo cancelar el knockdown ──
	// bIsAlive=true, bIsDBNO=false → no hay que re-poseer ni restaurar estado.
	if (bWasKnockedDown && !bWasDBNO && !bWasDead)
	{
		if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PlayerController->GetPawn()))
		{
			Turtle->RecoverFromKnockdown();
		}
		return;
	}

	// ── Restaurar estado ──────────────────────────────────────────────────
	TNPS->bIsDBNO = false;
	TNPS->DBNOBleedoutTimeRemaining = -1.f;
	TNPS->bIsAlive = true;
	TNPS->bHasFinishedRun = false;
	TNPS->bIsEliminated = false;
	TNPS->FinishRank = 0;
	TNPS->FinishTimeSeconds = -1.f;
	TNPS->DeathZoneTimeRemaining = -1.f;
	TNPS->ForceNetUpdate();
	// NOTA: NO resetear RaceScore aquí. Revivir a un compañero a mitad de carrera NO
	// debe borrar los puntos ya ganados (ScorePickups/zonas). El reset a 0 solo procede
	// al ARRANCAR la run (BeginPlay/PostSeamlessTravel), no en un revive.

	// Remove from DBNO tracking
	DBNOPlayers.Remove(PlayerController);
	if (DBNOPlayers.Num() == 0)
	{
		GetWorldTimerManager().ClearTimer(DBNOBleedoutTimerHandle);
	}

	FVector ReviveTargetLocation = FVector::ZeroVector;
	bool bHasReviveTargetLocation = false;

	// ── Destruir el pickup de rescate si existe ───────────────────────────
	if (TWeakObjectPtr<ATN_RescuePickup>* PickupPtr = RescuePickups.Find(TNPS->GetPlayerId()))
	{
		if (PickupPtr->IsValid())
		{
			ATN_RescuePickup* Pickup = PickupPtr->Get();
			ReviveTargetLocation = Pickup->GetActorLocation();
			bHasReviveTargetLocation = true;
			Pickup->Destroy();
		}
		RescuePickups.Remove(TNPS->GetPlayerId());
	}

	// ── Restaurar pawn visual y movimiento ────────────────────────────────
	// GetPawn() puede ser null si el jugador está en modo espectador (UnPossess).
	// En ese caso, buscamos el pawn guardado en DeadPlayerPawns.
	APawn* Pawn = PlayerController->GetPawn();
	if (!Pawn)
	{
		if (TWeakObjectPtr<APawn>* PawnPtr = DeadPlayerPawns.Find(TNPS->GetPlayerId()))
		{
			Pawn = PawnPtr->Get();
		}
	}
	// ── Conceder inmunidad ANTES de re-habilitar colisión ────────────────────
	// SetActorEnableCollision(true) dispara OnBoxBeginOverlap sincrónicamente.
	// Si el pawn revivido cae dentro de una death zone, OnBoxBeginOverlap ve
	// IsPlayerReviveImmune=false si la inmunidad se añade después → B6 ineficaz.
	// Al añadirla aquí, OnBoxBeginOverlap recibe PC ya inmune → descarta el overlap.
	GrantReviveImmunity(PlayerController);

	if (Pawn)
	{
		RestorePossessionAfterRevive(PlayerController, Pawn, ReviveTargetLocation, bHasReviveTargetLocation);
	}
	else
	{
		UE_LOG(LogTortunabo, Error, TEXT("[Revive] No pawn found for %s (PlayerId=%d) — cannot restore visual/control!"),
			*GetNameSafe(PlayerController), TNPS->GetPlayerId());
	}

	// Limpiar la entrada de DeadPlayerPawns
	DeadPlayerPawns.Remove(TNPS->GetPlayerId());

	UE_LOG(LogTortunabo, Log, TEXT("[Revive] %s revived! (%.1fs immunity) wasDBNO=%s wasDead=%s wasKnocked=%s"),
		*GetNameSafe(PlayerController), ReviveImmunitySeconds,
		bWasDBNO ? TEXT("YES") : TEXT("NO"),
		bWasDead ? TEXT("YES") : TEXT("NO"),
		bWasKnockedDown ? TEXT("YES") : TEXT("NO"));
}

void ATN_RunGameMode::GrantReviveImmunity(APlayerController* PlayerController)
{
	ReviveImmunePlayers.Add(PlayerController);
	// Cancelar timer anterior si existe (ej: jugador revivido dos veces rápido).
	// Sin esto, el timer antiguo expiraría y eliminaría la inmunidad activa de la segunda revivida.
	FTimerHandle& ImmunityTimer = ImmunityTimers.FindOrAdd(PlayerController);
	GetWorldTimerManager().ClearTimer(ImmunityTimer);
	TWeakObjectPtr<APlayerController> WeakImmunityPC = PlayerController;
	TWeakObjectPtr<ATN_RunGameMode> WeakGM(this);
	GetWorldTimerManager().SetTimer(ImmunityTimer, [WeakGM, WeakImmunityPC]()
	{
		ATN_RunGameMode* GM = WeakGM.Get();
		if (!GM) { return; }
		GM->ReviveImmunePlayers.Remove(WeakImmunityPC);
		GM->ImmunityTimers.Remove(WeakImmunityPC);

		// ── Forzar re-evaluación de death zones y storms ─────────────────────
		// SetActorEnableCollision(true) no dispara OnBoxBeginOverlap de forma
		// fiable cuando el pawn ya estaba geométricamente dentro del volumen.
		// Tras expirar la inmunidad, comprobamos manualmente death zones y storms.
		if (APlayerController* ImmunityPC = WeakImmunityPC.Get())
		{
			for (TActorIterator<ATN_DeathZoneVolume> It(GM->GetWorld()); It; ++It)
			{
				(*It)->ForceCheckPlayer(ImmunityPC);
			}
			for (TActorIterator<ATN_StormVolume> It(GM->GetWorld()); It; ++It)
			{
				(*It)->ForceCheckPlayer(ImmunityPC);
			}
		}
	}, ReviveImmunitySeconds, false);
}

void ATN_RunGameMode::RestorePossessionAfterRevive(APlayerController* PlayerController, APawn* Pawn, const FVector& ReviveTargetLocation, bool bHasReviveTargetLocation)
{
	// ORDEN CRÍTICO (2026-04-24):
	// 1. SetDeadVisual(false) PRIMERO → apaga ragdoll, re-attachea mesh al capsule,
	//    restaura bReplicateMovement=true. Sin este paso previo, SetActorLocation
	//    sobre un pawn con mesh fully-simulated dispara el warning
	//    "Attempting to move a fully simulated skeletal mesh".
	// 2. SetActorLocation DESPUÉS → teleporta a zona segura con capsule+mesh ya
	//    re-sincronizados.
	if (ATortugaCharacter* Character = Cast<ATortugaCharacter>(Pawn))
	{
		Character->RecoverFromKnockdown();
		Character->SetDeadVisual(false); // Restaurar extremidades + apagar ragdoll
	}

	// Restaurar CMC antes del teleport
	if (ACharacter* Ch = Cast<ACharacter>(Pawn))
	{
		if (UCharacterMovementComponent* CMC = Ch->GetCharacterMovement())
		{
			CMC->SetMovementMode(MOVE_Walking);
		}
	}

	// Revivir en la posición del RescuePickup. El pickup invisible siguió el
	// pelvis del ragdoll; el pawn no persigue su propio ragdoll en Tick.
	{
		const FVector BaseReviveLoc = bHasReviveTargetLocation ? ReviveTargetLocation : Pawn->GetActorLocation();
		const FVector ReviveLoc = BaseReviveLoc + FVector(0.f, 0.f, 20.f);
		UE_LOG(LogTortunabo, Log, TEXT("[Revive] In-place at pickup location (%.0f,%.0f,%.0f)"),
			ReviveLoc.X, ReviveLoc.Y, ReviveLoc.Z);
		Pawn->SetActorLocation(ReviveLoc, false, nullptr, ETeleportType::TeleportPhysics);
	}

	// Restaurar visibilidad (el pawn fue ocultado en MarkPlayerDead)
	Pawn->SetActorHiddenInGame(false);
	Pawn->SetActorEnableCollision(true);
	// Restaurar relevancy default (tras MarkPlayerDead forzado bAlwaysRelevant=true)
	Pawn->bAlwaysRelevant = false;

	// ── Sacar del modo espectador: re-poseer el pawn ──────────────────
	// 1) Resetear flag de espectador en el PlayerState
	if (PlayerController->PlayerState)
	{
		PlayerController->PlayerState->SetIsOnlyASpectator(false);
	}
	// 2) Forzar UnPossess si el PC aún posee al pawn.
	//    En el listen-server, ChangeState(Spectating) puede NO llamar UnPossess
	//    internamente, dejando al PC poseyendo al pawn muerto. Si Possess(Pawn)
	//    recibe el mismo pawn que ya posee, es un no-op → ChangeState(Playing)
	//    nunca se llama y el host se queda atrapado en estado Spectating.
	if (PlayerController->GetPawn() == Pawn)
	{
		PlayerController->UnPossess();
	}
	// 3) Re-poseer el pawn (ahora garantizado que no es no-op)
	PlayerController->Possess(Pawn);
	// 4) Safety: forzar ChangeState(Playing) por si Possess no lo hizo
	PlayerController->ChangeState(NAME_Playing);

	// 5) EnableInput DESPUÉS de Possess — APawn::EnableInput verifica
	//    que el PC == Pawn->Controller. Si se llama antes de Possess,
	//    Controller es nullptr (por UnPossess) y EnableInput falla
	//    silenciosamente, dejando bInputEnabled = false para siempre.
	Pawn->EnableInput(PlayerController);

	// 6) ClientRestart: dice al cliente que ahora controla este pawn
	PlayerController->ClientRestart(Pawn);
	// 7) Apuntar cámara al pawn (limpia el ViewTarget del espectador)
	PlayerController->SetViewTarget(Pawn);
	// 8) ClientRestorePlayerInput: limpia IgnoreMoveInput/IgnoreLookInput
	if (AMP_GamePlayerController* TNPC = Cast<AMP_GamePlayerController>(PlayerController))
	{
		TNPC->ClientRestorePlayerInput();
		// Client RPCs no se ejecutan en el listen-server — llamada directa para el host.
		if (TNPC->IsLocalController())
		{
			TNPC->ForceRestoreInput();
		}

		// 9) Timer repetitivo de seguridad: re-aplica input cada 0.1s durante 1s.
		//    Cubre TODOS los edge cases donde la cadena
		//    Possess → AcknowledgedPawn → ClientRestart → PawnClientRestart
		//    no ha terminado, o donde UE re-incrementa IgnoreMoveInput internamente.
		TWeakObjectPtr<ATortugaCharacter> WeakChar(Cast<ATortugaCharacter>(Pawn));
		TWeakObjectPtr<AMP_GamePlayerController> WeakPC(TNPC);
		TWeakObjectPtr<APawn> WeakPawn(Pawn);
		TWeakObjectPtr<ATN_RunGameMode> WeakGM2(this);
		TSharedPtr<FTimerHandle> RetryHandle = MakeShared<FTimerHandle>();
		TSharedPtr<int32> RetryCount = MakeShared<int32>(10); // 10 × 0.1s = 1s
		GetWorldTimerManager().SetTimer(*RetryHandle,
			[WeakChar, WeakPC, WeakPawn, RetryCount, RetryHandle, WeakGM2]()
		{
			ATN_RunGameMode* GM2 = WeakGM2.Get();
			if (!GM2 || *RetryCount <= 0)
			{
				if (GM2) { GM2->GetWorldTimerManager().ClearTimer(*RetryHandle); }
				return;
			}
			--(*RetryCount);

			if (WeakPawn.IsValid())
			{
				WeakPawn->EnableInput(nullptr); // nullptr bypasses Controller check
			}
			if (WeakChar.IsValid())
			{
				WeakChar->ReapplyInputMapping();
			}
			if (WeakPC.IsValid())
			{
				WeakPC->ForceRestoreInput();
			}
			if (WeakPawn.IsValid() && WeakPawn->GetController())
			{
				if (ACharacter* Ch = Cast<ACharacter>(WeakPawn.Get()))
				{
					if (UCharacterMovementComponent* CMC = Ch->GetCharacterMovement())
					{
						if (CMC->MovementMode == MOVE_None)
						{
							CMC->SetMovementMode(MOVE_Walking);
						}
					}
				}
			}
		}, 0.1f, true);
	}
}

APawn* ATN_RunGameMode::GetDeadPlayerPawn(int32 PlayerId) const
{
	if (const TWeakObjectPtr<APawn>* PawnPtr = DeadPlayerPawns.Find(PlayerId))
	{
		return PawnPtr->Get();
	}
	return nullptr;
}

bool ATN_RunGameMode::IsPlayerReviveImmune(APlayerController* PC) const
{
	return PC && ReviveImmunePlayers.Contains(PC);
}

void ATN_RunGameMode::TickDBNOBleedout()
{
	if (!HasAuthority())
	{
		return;
	}

	TArray<TWeakObjectPtr<APlayerController>> Keys;
	DBNOPlayers.GetKeys(Keys);

	for (const TWeakObjectPtr<APlayerController>& WeakPC : Keys)
	{
		APlayerController* PC = WeakPC.Get();
		if (!PC)
		{
			DBNOPlayers.Remove(WeakPC);
			continue;
		}

		float* Remaining = DBNOPlayers.Find(WeakPC);
		if (!Remaining) { continue; }

		*Remaining = FMath::Max(0.f, *Remaining - 0.1f);

		// Sync to PlayerState for HUD
		if (ATN_CoopPlayerState* TNPS = PC->GetPlayerState<ATN_CoopPlayerState>())
		{
			TNPS->DBNOBleedoutTimeRemaining = *Remaining;
		}

		if (*Remaining <= KINDA_SMALL_NUMBER)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[DBNO] %s bleedout expired → dying for real"), *GetNameSafe(PC));
			DBNOPlayers.Remove(WeakPC);
			MarkPlayerDeadBy(PC, ETNDeathCause::Bleedout);
		}
	}

	if (DBNOPlayers.Num() == 0)
	{
		GetWorldTimerManager().ClearTimer(DBNOBleedoutTimerHandle);
	}
	else
	{
		// Comprobar si TODOS los vivos están ahora en DBNO (nadie puede revivir).
		// Esto cubre el caso de que el segundo jugador caiga mientras el primero
		// ya estaba en DBNO via el bleedout tick (EnterDBNO no se llama de nuevo).
		CheckAllAliveDBNO();
	}
}

void ATN_RunGameMode::CheckAllAliveDBNO()
{
	if (!HasAuthority() || !GameState)
	{
		return;
	}

	int32 AliveCount = 0;
	int32 DBNOCount = 0;

	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		if (const ATN_CoopPlayerState* CastPS = Cast<ATN_CoopPlayerState>(BasePS))
		{
			if (CastPS->bIsAlive)
			{
				++AliveCount;
				if (CastPS->bIsDBNO)
				{
					++DBNOCount;
				}
			}
		}
	}

	// If ALL alive players are in DBNO, no one can revive → kill everyone
	if (AliveCount > 0 && AliveCount == DBNOCount)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[DBNO] All %d alive players are in DBNO — killing everyone"), AliveCount);

		TArray<TWeakObjectPtr<APlayerController>> Keys;
		DBNOPlayers.GetKeys(Keys);
		for (const TWeakObjectPtr<APlayerController>& WeakPC : Keys)
		{
			if (APlayerController* PC = WeakPC.Get())
			{
				MarkPlayerDeadBy(PC, ETNDeathCause::Bleedout);
			}
		}
		DBNOPlayers.Empty();
		GetWorldTimerManager().ClearTimer(DBNOBleedoutTimerHandle);
	}
}

void ATN_RunGameMode::TickRaceClock()
{
	if (ATN_CoopGameState* TNGS = GetGameState<ATN_CoopGameState>())
	{
		TNGS->ServerMatchElapsedTime = GetWorld()->GetTimeSeconds() - MatchStartServerTime;
	}
}

void ATN_RunGameMode::UpdateRoundProgressAndMaybeFinish()
{
	ATN_CoopGameState* TNGS = GetGameState<ATN_CoopGameState>();
	if (!TNGS)
	{
		return;
	}

	// Sin el que se está yendo: desde Logout su PlayerState sigue en el PlayerArray (#558).
	const FTNCoopRoundCount Count = TN_CountCoopRound(GameState);

	TNGS->FinishedPlayers = Count.Resolved;
	TNGS->ExpectedPlayers = Count.Total;
	TNGS->ServerMatchElapsedTime = GetWorld()->GetTimeSeconds() - MatchStartServerTime;

	if (!Count.IsRoundOver())
	{
		return;
	}

	StartResults();
}

void ATN_RunGameMode::StartResults()
{
	ATN_CoopGameState* TNGS = GetGameState<ATN_CoopGameState>();
	if (!TNGS || GetWorldTimerManager().IsTimerActive(ResultsTimerHandle))
	{
		return;
	}

	SetFlowState(ETNMatchFlowState::Results);
	ResultsCountdownValue = FMath::CeilToInt(ResultsDurationSeconds);
	TNGS->CountdownValue = ResultsCountdownValue;
	GetWorldTimerManager().SetTimer(ResultsCountdownTimerHandle, this, &ATN_RunGameMode::TickResultsCountdown, 1.0f, true);
	GetWorldTimerManager().SetTimer(ResultsTimerHandle, this, &ATN_RunGameMode::FinishRoundAndReturnToLobby, ResultsDurationSeconds, false);
}

void ATN_RunGameMode::MovePlayerToSpectator(APlayerController* PlayerController) const
{
	if (!PlayerController)
	{
		return;
	}

	if (AMP_GamePlayerController* TNPC = Cast<AMP_GamePlayerController>(PlayerController))
	{
		TNPC->EnterSpectateMode();
	}
	else
	{
		PlayerController->ChangeState(NAME_Spectating);
		PlayerController->StartSpectatingOnly();
	}
}

void ATN_RunGameMode::TickResultsCountdown()
{
	ATN_CoopGameState* TNGS = GetGameState<ATN_CoopGameState>();
	if (!TNGS)
	{
		GetWorldTimerManager().ClearTimer(ResultsCountdownTimerHandle);
		return;
	}

	ResultsCountdownValue = FMath::Max(0, ResultsCountdownValue - 1);
	TNGS->CountdownValue = ResultsCountdownValue;

	if (ResultsCountdownValue <= 0)
	{
		GetWorldTimerManager().ClearTimer(ResultsCountdownTimerHandle);
	}
}

void ATN_RunGameMode::FinishRoundAndReturnToLobby()
{
	GetWorldTimerManager().ClearTimer(ResultsCountdownTimerHandle);
	GetWorldTimerManager().ClearTimer(RaceClockTimerHandle);

	if (ATN_CoopGameState* TNGS = GetGameState<ATN_CoopGameState>())
	{
		TNGS->CountdownValue = 0;
	}

	if (UWorld* World = GetWorld())
	{
		// ── Destroy all pawns for WASAPI cleanup ─────────────────────────────
		// ProximityVoiceComponent::EndPlay(Destroyed) fires while audio is alive.
		// With seamless travel the connection stays alive — no socket race condition.
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (!PC) { continue; }
			if (APawn* Pawn = PC->GetPawn())
			{
				Pawn->Destroy();
			}
		}

		// ── Destruir spectator pawns de jugadores eliminados ──────────────────
		// Los SpectatorPawns NO están controlados por PlayerController.GetPawn()
		// en el loop anterior → quedan como "cuerpos fantasma" en el lobby.
		for (TActorIterator<ASpectatorPawn> It(World); It; ++It)
		{
			It->Destroy();
		}

		// ── Destruir RescuePickups residuales ─────────────────────────────────
		for (TActorIterator<ATN_RescuePickup> It(World); It; ++It)
		{
			It->Destroy();
		}

		// ── Destruir todos los TortugaCharacters restantes (los poseídos ya se destruyeron arriba)
		// Evita que lleguen al lobby como meshes fantasma.
		for (TActorIterator<ATortugaCharacter> It(World); It; ++It)
		{
			ATortugaCharacter* Char = *It;
			if (IsValid(Char) && !Char->IsActorBeingDestroyed())
			{
				Char->Destroy();
			}
		}

		// ── Seamless ServerTravel — connection persists, no NetDriver destroy ─
		// Se vuelve al lobby del que se salió (lo apunta ATN_HQGameMode en la GameInstance); si no se sabe, LobbyMapPath.
		const UMP_GameInstance* TNGI = Cast<UMP_GameInstance>(World->GetGameInstance());
		const FString TravelURL = (TNGI && !TNGI->LobbyReturnMapPath.IsEmpty() ? TNGI->LobbyReturnMapPath : LobbyMapPath) + GetLobbyTravelOptions();
		UE_LOG(LogTortunabo, Log, TEXT("[RunGameMode] Seamless ServerTravel to: %s"), *TravelURL);
		World->ServerTravel(TravelURL);
	}
}

// ── Seamless Travel Handlers ──────────────────────────────────────────────────

void ATN_RunGameMode::HandleSeamlessTravelPlayer(AController*& C)
{
	// Limpiar estado espectador ANTES de Super — jugadores que murieron/terminaron
	// en la carrera estaban en modo espectador. Sin esto, PlayerCanRestart() devuelve
	// false y Super no les spawnea pawn.
	if (APlayerController* PC = Cast<APlayerController>(C))
	{
		// ── Destruir pawn prematuro ────────────────────────────────────────────
		// BeginPlay itera los PCs y llama EnsurePlayerSpawned, lo que puede
		// spawnear un pawn ANTES de que HandleSeamlessTravelPlayer lo haga.
		// Super::HandleSeamlessTravelPlayer → RestartPlayer NO destruye el pawn
		// existente → quedarían DOS pawns (ghost pawn).
		// Solución: destruir el pawn existente aquí para que Super spawne uno limpio.
		if (APawn* OldPawn = PC->GetPawn())
		{
			UE_LOG(LogTortunabo, Log, TEXT("[RunGameMode] HandleSeamlessTravelPlayer: destruyendo pawn prematuro '%s' de %s"),
				*GetNameSafe(OldPawn), *GetNameSafe(PC));
			OldPawn->Destroy();
		}

		if (PC->PlayerState)
		{
			PC->PlayerState->SetIsOnlyASpectator(false);
		}
	}

	Super::HandleSeamlessTravelPlayer(C);
}

bool ATN_RunGameMode::CanServerTravel(const FString& URL, bool bAbsolute)
{
	return Super::CanServerTravel(URL, bAbsolute) && UTN_TravelFailureSubsystem::CanServerTravelTo(GetWorld(), URL, bAbsolute);
}

void ATN_RunGameMode::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();

	UE_LOG(LogTortunabo, Log, TEXT("[RunGameMode] PostSeamlessTravel: initializing all players for run."));

	// Resetear estado de todos los jugadores que viajaron
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC) { continue; }

		// Reset PlayerState para la carrera
		if (ATN_CoopPlayerState* TNPS = PC->GetPlayerState<ATN_CoopPlayerState>())
		{

			TNPS->ResetForNewRace();

			// Asegurar que tiene pawn
			EnsurePlayerSpawned(PC);

			// Casco y skin llegan a los clientes por OnRep_Equipped* y el pawn nuevo los aplica desde el PlayerState
			// (BeginPlay, PawnClientRestart y OnRep_PlayerState); en el anfitrión, aquí (#78: sin multicast fiable).
			if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PC->GetPawn()))
			{
				Turtle->ApplyCosmeticsFromPlayerState();
			}
		}
		else
		{
			// Asegurar que tiene pawn aunque no tenga PlayerState
			EnsurePlayerSpawned(PC);
		}
	}

	// Actualizar conteo y comprobar si podemos arrancar
	if (ATN_CoopGameState* TNGS = GetGameState<ATN_CoopGameState>())
	{
		TNGS->ConnectedPlayers = TN_CountConnectedCoopPlayers(GameState);
		TNGS->ExpectedPlayers = ExpectedPlayersFromLobby;
	}

	TryStartMatch();
}

void ATN_RunGameMode::SetFlowState(ETNMatchFlowState NewState) const
{
	if (ATN_CoopGameState* TNGS = GetGameState<ATN_CoopGameState>())
	{
		TNGS->MatchFlowState = NewState;
		TNGS->BroadcastFlowStateChange();
	}
}

void ATN_RunGameMode::HandleCollectionZoneGoal(ATN_CollectionZone* Zone)
{
	if (!HasAuthority() || !Zone) { return; }

	const int32 Bonus = Zone->GoalReachedBonusScore;
	if (Bonus <= 0) { return; }

	// Bonus a TODOS los jugadores activos (no eliminados, no terminados aún).
	// Cooperativo: completar la zona beneficia al equipo entero.
	int32 Beneficiaries = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC) { continue; }
		ATN_CoopPlayerState* TNPS = PC->GetPlayerState<ATN_CoopPlayerState>();
		if (!TNPS || TNPS->bIsEliminated || TNPS->bHasFinishedRun) { continue; }
		TNPS->AddRaceScore(Bonus);  // difunde OnRaceScoreChanged en el host (listen-server)
		++Beneficiaries;
	}

	UE_LOG(LogTortunabo, Log, TEXT("[COLLECTION] GameMode reaccionó a zone '%s' goal · +%d a %d players activos"),
		*Zone->GetName(), Bonus, Beneficiaries);
}

