#include "Game/TN_SurvivalGameMode.h"
#include "Core/TN_Log.h"
#include "Core/TN_CoopGameState.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_GameModeSpawnUtils.h"
#include "Game/TN_RoundLeftovers.h"
#include "Player/MP_GamePlayerController.h"
#include "Multiplayer/MP_GameInstance.h"
#include "World/TN_ChunkManager.h"
#include "World/TN_StormVolume.h"
#include "World/ProcMap/TN_PathStorm.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "World/ProcMap/TN_SurvivalCatalog.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "TimerManager.h"

namespace
{
	/** Distancia a la meta cuando no se puede medir (sin pawn o sin manager): la peor posible. */
	constexpr float UnknownRemaining = 1.e9f;
}

ATN_SurvivalGameMode::ATN_SurvivalGameMode()
{
	bAllowRevive = false;
	// El panel de resultados dice qué la ha eliminado (#728).
	bRecordDeathCause = true;

	// Los mismos Blueprints que BP_RunGameMode (como ATN_ProcMapGameMode): la clase C++ sirve tal cual como
	// ?game=Survival sin un BP propio.
	static ConstructorHelpers::FClassFinder<APawn> TurtleBP(TEXT("/Game/Blueprints/Characters/BP_TortugaCharacter"));
	if (TurtleBP.Succeeded())
	{
		DefaultPawnClass = TurtleBP.Class;
	}
	static ConstructorHelpers::FClassFinder<APlayerController> ControllerBP(TEXT("/Game/Blueprints/Gameplay/Controllers/BP_GamePlayerController"));
	if (ControllerBP.Succeeded())
	{
		PlayerControllerClass = ControllerBP.Class;
	}
}

void ATN_SurvivalGameMode::StartPlay()
{
	// StartPlay va antes del BeginPlay de los actores del nivel: así el manager genera el mapa del nivel 1 y no los
	// chunks del Clásico. ?SurvivalSeed=N repite una partida (los mismos mapas del catálogo en cada nivel) y
	// ?SurvivalMap=<semilla> juega ese mapa del catálogo en el nivel 1 (#518).
	if (ATN_ChunkManager* Manager = FindChunkManager())
	{
		const FString SeedOption = UGameplayStatics::ParseOption(OptionsString, TEXT("SurvivalSeed"));
		const int32 Seed = SeedOption.IsEmpty() ? FMath::RandRange(1, 1 << 30) : FCString::Atoi(*SeedOption);
		UE_LOG(LogTortunabo, Log, TEXT("[Survival] Semilla de la partida: %d (?SurvivalSeed=%d para repetirla)."), Seed, Seed);
		// La dificultad elegida con el general decide en qué mapa del catálogo se empieza (#730); ?ProcDifficulty= manda.
		const ETNProcDifficulty Difficulty = ResolveDifficulty();
		const int32 StartDifficulty = TNSurvivalLogic::StartMapDifficulty(Difficulty);
		const int32 TrapsPer100mTenths = TNSurvivalLogic::TrapsPer100mTenths(Difficulty);
		const int32 SearchPer100mTenths = TNSurvivalLogic::SearchSpotsPer100mTenths(Difficulty);
		UE_LOG(LogTortunabo, Log, TEXT("[Survival] Dificultad %s: el nivel 1 juega un mapa de dificultad %d, cada nivel sube una hasta 5 y se buscan %.1f trampas y %.1f rebuscables cada 100 m."),
			*UEnum::GetValueAsString(Difficulty), StartDifficulty, TrapsPer100mTenths / 10.0, SearchPer100mTenths / 10.0);
		Manager->SetLevelMode(true, Seed, ParseFirstLevelMap(), StartDifficulty, TrapsPer100mTenths, SearchPer100mTenths);
	}
	else
	{
		UE_LOG(LogTortunabo, Error, TEXT("[Survival] No hay ATN_ChunkManager en el mapa: Supervivencia necesita LVL_Run."));
	}

	// La tormenta va por el camino de cada nivel (StartLevelStorm): la caja de LVL_Run avanzaría en línea recta y cruzaría
	// el camino por donde le tocara. Se quita antes del BeginPlay de los actores.
	TArray<ATN_StormVolume*> BoxStorms;
	for (TActorIterator<ATN_StormVolume> It(GetWorld()); It; ++It)
	{
		BoxStorms.Add(*It);
	}
	for (ATN_StormVolume* BoxStorm : BoxStorms)
	{
		BoxStorm->Destroy();
	}

	Super::StartPlay();
	PublishLevel();
}

ETNProcDifficulty ATN_SurvivalGameMode::ResolveDifficulty() const
{
	ETNProcDifficulty Difficulty = ETNProcDifficulty::Normal;
	if (const UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance()))
	{
		Difficulty = GI->SelectedProcDifficulty;
	}
	const FString Option = UGameplayStatics::ParseOption(OptionsString, TEXT("ProcDifficulty"));
	if (Option.Equals(TEXT("Easy"), ESearchCase::IgnoreCase)) { Difficulty = ETNProcDifficulty::Easy; }
	else if (Option.Equals(TEXT("Normal"), ESearchCase::IgnoreCase)) { Difficulty = ETNProcDifficulty::Normal; }
	else if (Option.Equals(TEXT("Hard"), ESearchCase::IgnoreCase)) { Difficulty = ETNProcDifficulty::Hard; }
	return Difficulty;
}

uint32 ATN_SurvivalGameMode::ParseFirstLevelMap() const
{
	const FString MapOption = UGameplayStatics::ParseOption(OptionsString, TEXT("SurvivalMap"));
	if (MapOption.IsEmpty())
	{
		return 0u;
	}

	const int64 MapSeed = MapOption.IsNumeric() ? FCString::Atoi64(*MapOption) : -1;
	const TNSurvivalCatalog::FMapEntry* Entry = MapSeed > 0 && MapSeed <= MAX_uint32
		? TNSurvivalCatalog::FindMap(static_cast<uint32>(MapSeed)) : nullptr;
	if (!Entry)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Survival] ?SurvivalMap=%s no es una semilla del catálogo: se elige el mapa del nivel 1."), *MapOption);
		return 0u;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Survival] ?SurvivalMap=%u: el nivel 1 juega «%s» (dificultad %d)."), Entry->Seed, Entry->Name, Entry->Difficulty);
	return Entry->Seed;
}

void ATN_SurvivalGameMode::PublishLevel()
{
	if (ATN_CoopGameState* TNGS = GetGameState<ATN_CoopGameState>())
	{
		TNGS->CurrentLevel = CurrentLevel;
	}
}

ATN_ChunkManager* ATN_SurvivalGameMode::FindChunkManager() const
{
	for (TActorIterator<ATN_ChunkManager> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ATN_SurvivalGameMode::OnWaitingTimeout()
{
	const bool bWasStarted = bMatchStarted;
	Super::OnWaitingTimeout();

	if (!bWasStarted && bMatchStarted)
	{
		StartingPlayers = FMath::Max(1, TN_CountConnectedCoopPlayers(GameState));
		UE_LOG(LogTortunabo, Log, TEXT("[Survival] Empieza con %d jugador(es)%s."),
			StartingPlayers, StartingPlayers == 1 ? TEXT(" (solitario: hasta que muera)") : TEXT(""));

		// Del corral de LVL_Run a la salida del mapa del nivel 1.
		BeginLevelWhenReady();
	}
}

AActor* ATN_SurvivalGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	const ATN_ChunkManager* Manager = FindChunkManager();
	const ATN_ProcMapGenerator* Generator = Manager ? Manager->GetLevelGenerator() : nullptr;
	if (!Generator || !Generator->IsMapReady())
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	TArray<AActor*> Starts;
	for (int32 Index = 0; APlayerStart* Start = Generator->GetStartPlayerStart(Index); ++Index)
	{
		Starts.Add(Start);
	}
	if (Starts.Num() == 0)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}
	AActor* Start = TN_PickSpreadPlayerStart(GetWorld(), Starts, Player, DefaultPawnClass, TEXT("Survival"));
	return Start ? Start : Starts[0];
}

void ATN_SurvivalGameMode::Logout(AController* Exiting)
{
	// Irse en la espera no cuenta: quien vuelve antes de empezar juega la partida entera.
	if (const APlayerState* ExitingPS = Exiting && bMatchStarted ? Exiting->PlayerState : nullptr)
	{
		LeftPlayerIds.Add(ExitingPS->GetPlayerId());
		FinishedPawns.Remove(ExitingPS->GetPlayerId());
	}

	// La base vuelve a evaluar el nivel (UpdateRoundProgressAndMaybeFinish) sin el que se va.
	Super::Logout(Exiting);
}

void ATN_SurvivalGameMode::MarkPlayerFinished(APlayerController* PlayerController)
{
	if (bMatchOver)
	{
		return;
	}

	ATN_CoopPlayerState* TNPS = PlayerController ? PlayerController->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (TNPS && Pawn && TNPS->bIsAlive && !TNPS->bHasFinishedRun)
	{
		// Antes de Super: al llegar pasa a espectador y deja de poseer el pawn, que se reutiliza en el siguiente nivel.
		FinishedPawns.Add(TNPS->GetPlayerId(), Pawn);
	}

	Super::MarkPlayerFinished(PlayerController);
}

void ATN_SurvivalGameMode::MarkPlayerDead(APlayerController* PlayerController)
{
	if (bMatchOver)
	{
		return;
	}

	ATN_CoopPlayerState* TNPS = PlayerController ? PlayerController->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
	if (HasAuthority() && TNPS && TNPS->bIsAlive && !TNPS->bHasFinishedRun)
	{
		// Antes de Super, que ya evalúa el nivel con este muerto y deja de poseer el pawn.
		FDeathRecord Record;
		Record.Level = CurrentLevel;
		Record.Time = GetWorld()->GetTimeSeconds();
		const APawn* Pawn = PlayerController->GetPawn();
		const ATN_ChunkManager* Manager = FindChunkManager();
		Record.Remaining = Pawn && Manager ? Manager->GetRemainingDistance(Pawn->GetActorLocation()) : UnknownRemaining;
		DeathRecords.Add(TNPS->GetPlayerId(), Record);

		UE_LOG(LogTortunabo, Log, TEXT("[Survival] '%s' cae en el nivel %d a %.0f uu de la meta."),
			*GetNameSafe(PlayerController), Record.Level, Record.Remaining);
	}

	Super::MarkPlayerDead(PlayerController);

	// El tótem le salvó: sigue viva y el apunte no vale.
	if (TNPS && TNPS->bIsAlive)
	{
		DeathRecords.Remove(TNPS->GetPlayerId());
	}
}

TArray<FTNSurvivalPlayer> ATN_SurvivalGameMode::GatherPlayers() const
{
	TArray<FTNSurvivalPlayer> Players;
	if (!GameState)
	{
		return Players;
	}

	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		const ATN_CoopPlayerState* TNPS = Cast<ATN_CoopPlayerState>(BasePS);
		// Fuera quien se fue y quien entró con la partida en marcha (SitOutPlayerIds): ni sigue ni gana.
		if (!TNPS || LeftPlayerIds.Contains(TNPS->GetPlayerId()) || SitOutPlayerIds.Contains(TNPS->GetPlayerId()))
		{
			continue;
		}

		FTNSurvivalPlayer& P = Players.AddDefaulted_GetRef();
		P.Id = TNPS->GetPlayerId();
		P.bAlive = TNPS->bIsAlive;
		P.bFinishedLevel = TNPS->bIsAlive && TNPS->bHasFinishedRun;
		if (!P.bAlive)
		{
			const FDeathRecord* Record = DeathRecords.Find(P.Id);
			P.LevelDied = Record ? Record->Level : CurrentLevel;
			P.DeathTime = Record ? Record->Time : 0.f;
			P.DeathRemaining = Record ? Record->Remaining : UnknownRemaining;
		}
	}
	return Players;
}

void ATN_SurvivalGameMode::UpdateRoundProgressAndMaybeFinish()
{
	if (bMatchOver || !bMatchStarted || bLevelLoading || GetWorldTimerManager().IsTimerActive(LevelTransitionTimerHandle))
	{
		return;
	}

	const TArray<FTNSurvivalPlayer> Players = GatherPlayers();

	if (ATN_CoopGameState* TNGS = GetGameState<ATN_CoopGameState>())
	{
		int32 Resolved = 0;
		for (const FTNSurvivalPlayer& P : Players)
		{
			Resolved += (!P.bAlive || P.bFinishedLevel) ? 1 : 0;
		}
		TNGS->FinishedPlayers = Resolved;
		TNGS->ExpectedPlayers = Players.Num();
		TNGS->ServerMatchElapsedTime = GetWorld()->GetTimeSeconds() - MatchStartServerTime;
	}

	const FTNSurvivalDecision Decision = TNSurvivalLogic::DecideLevelOutcome(Players, StartingPlayers);
	switch (Decision.Outcome)
	{
	case ETNSurvivalOutcome::Advance:
		StopLevelStorm();
		UE_LOG(LogTortunabo, Log, TEXT("[Survival] Nivel %d superado. El siguiente sale en %.1f s."), CurrentLevel, LevelTransitionSeconds);
		GetWorldTimerManager().SetTimer(LevelTransitionTimerHandle, this, &ATN_SurvivalGameMode::AdvanceLevel, LevelTransitionSeconds, false);
		break;
	case ETNSurvivalOutcome::Winner:
	case ETNSurvivalOutcome::SoloOver:
		FinishSurvival(Decision.WinnerId);
		break;
	default:
		break;
	}
}

void ATN_SurvivalGameMode::AdvanceLevel()
{
	if (bMatchOver)
	{
		return;
	}

	ATN_ChunkManager* Manager = FindChunkManager();
	if (!Manager)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[Survival] Sin ATN_ChunkManager no hay siguiente nivel: fin de partida."));
		FinishSurvival(INDEX_NONE);
		return;
	}

	++CurrentLevel;
	PublishLevel();
	NextFinishRank = 1;

	// Los cuerpos del nivel anterior se quedarían cayendo al desaparecer su mapa.
	for (const TPair<int32, TWeakObjectPtr<APawn>>& Pair : DeadPlayerPawns)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->Destroy();
		}
	}
	DeadPlayerPawns.Reset();

	// Ni lo soltado ni las conchas trampa pasan al nivel siguiente (#569): con el terreno nuevo quedarían flotando o
	// enterrados, y una trampa armada inmovilizaría a quien pasara por ella. Antes de construir: el nivel nuevo es síncrono.
	const int32 Removed = TNRoundLeftovers::DestroyPlayerLeftovers(GetWorld());
	if (Removed > 0)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Survival] Nivel %d: %d objetos sueltos del nivel anterior quitados."), CurrentLevel, Removed);
	}

	if (!Manager->BuildLevel(CurrentLevel))
	{
		FinishSurvival(INDEX_NONE);
		return;
	}
	BeginLevelWhenReady();
}

void ATN_SurvivalGameMode::BeginLevelWhenReady()
{
	bLevelLoading = true;
	LevelLoadStartTime = GetWorld()->GetTimeSeconds();
	GetWorldTimerManager().SetTimer(LevelReadyPollHandle, this, &ATN_SurvivalGameMode::PollLevelReady, 0.25f, true);
	PollLevelReady();
}

void ATN_SurvivalGameMode::PollLevelReady()
{
	if (bMatchOver)
	{
		GetWorldTimerManager().ClearTimer(LevelReadyPollHandle);
		return;
	}

	// La colisión del terreno se cocina en segundo plano: sin ella, quien aparece en la salida cae al vacío.
	const ATN_ChunkManager* Manager = FindChunkManager();
	const ATN_ProcMapGenerator* Generator = Manager ? Manager->GetLevelGenerator() : nullptr;
	bool bGroundReady = Generator && Generator->IsMapReady();
	for (int32 Index = 0; bGroundReady; ++Index)
	{
		const APlayerStart* Start = Generator->GetStartPlayerStart(Index);
		if (!Start)
		{
			break;
		}
		bGroundReady = Generator->MapCollisionUnder(Start->GetActorLocation());
	}

	const float Waited = GetWorld()->GetTimeSeconds() - LevelLoadStartTime;
	if (!bGroundReady && Waited < LevelReadyTimeoutSeconds)
	{
		return;
	}
	if (!bGroundReady)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Survival] El suelo del nivel %d no tiene colisión tras %.1f s: salen igualmente."), CurrentLevel, Waited);
	}
	// Y cada cliente con el mismo mapa montado en su máquina (#828): sin él, su tortuga pisaría otro suelo que la del servidor.
	FString Waiting;
	if (Generator && CountClientsWithoutMap(Generator->GetRequestedGeneration(), Waiting) > 0)
	{
		if (Waited < ClientLevelReadyTimeoutSeconds)
		{
			return;
		}
		UE_LOG(LogTortunabo, Warning, TEXT("[Survival] Nivel %d: %s no ha dicho que tenga el mapa tras %.1f s: salen igualmente (quietos en su máquina hasta tenerlo)."),
			CurrentLevel, *Waiting, Waited);
	}

	GetWorldTimerManager().ClearTimer(LevelReadyPollHandle);
	SendSurvivorsToLevelStart();
}

int32 ATN_SurvivalGameMode::CountClientsWithoutMap(int32 Generation, FString& OutWaiting) const
{
	int32 Missing = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AMP_GamePlayerController* PC = Cast<AMP_GamePlayerController>(It->Get());
		// El anfitrión usa el mapa del servidor.
		if (!PC || PC->IsLocalController() || PC->GetReportedProcMapGeneration() >= Generation)
		{
			continue;
		}
		++Missing;
		OutWaiting += (OutWaiting.IsEmpty() ? TEXT("") : TEXT(", ")) + GetNameSafe(PC);
	}
	return Missing;
}

void ATN_SurvivalGameMode::SendSurvivorsToLevelStart()
{
	// Las vivas que siguen en la partida.
	TArray<APlayerController*> Survivors;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		ATN_CoopPlayerState* TNPS = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
		if (!TNPS || !TNPS->bIsAlive || LeftPlayerIds.Contains(TNPS->GetPlayerId()))
		{
			continue;
		}
		TNPS->bHasFinishedRun = false;
		TNPS->FinishRank = 0;
		TNPS->DeathZoneTimeRemaining = -1.f;
		TNPS->ForceNetUpdate();
		Survivors.Add(PC);
	}

	for (APlayerController* PC : Survivors)
	{
		ReleaseSurvivor(PC);
	}
	FinishedPawns.Reset();
	bLevelLoading = false;
	// La tormenta sale con ellos, por detrás de la salida (#448: cada nivel empieza igual para todos).
	StartLevelStorm();

	UE_LOG(LogTortunabo, Log, TEXT("[Survival] ═══ Nivel %d ═══"), CurrentLevel);
	UpdateRoundProgressAndMaybeFinish();
}

void ATN_SurvivalGameMode::ReleaseSurvivor(APlayerController* PC)
{
	ATN_CoopPlayerState* TNPS = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
	if (!TNPS)
	{
		return;
	}
	const ATN_ChunkManager* Manager = FindChunkManager();
	// ChoosePlayerStart y no FindPlayerStart: este devuelve el sitio de la vez anterior (el corral en el nivel 1).
	AActor* Start = ChoosePlayerStart(PC);
	PC->StartSpot = Start;
	const FVector FallbackLocation = Manager ? Manager->GetActorLocation() : FVector::ZeroVector;
	const FVector StartLocation = Start ? Start->GetActorLocation() : FallbackLocation + FVector(0.f, 0.f, 100.f);
	const FRotator StartRotation(0.f, Start ? Start->GetActorRotation().Yaw : (Manager ? Manager->GetActorRotation().Yaw : 0.f), 0.f);

	APawn* Pawn = FinishedPawns.FindRef(TNPS->GetPlayerId()).Get();
	APawn* CurrentPawn = PC->GetPawn();
	if (Pawn)
	{
		// Mismo camino que una reanimación: visible, con colisión, poseído y con el input restaurado.
		RestorePossessionAfterRevive(PC, Pawn, StartLocation, true);
		Pawn->SetActorRotation(StartRotation);
		PC->ClientSetRotation(StartRotation);
	}
	else if (CurrentPawn && !TNPS->IsOnlyASpectator())
	{
		// Sigue en juego (en el nivel 1, desde el corral): se le lleva a la salida tal cual.
		ACharacter* Character = Cast<ACharacter>(CurrentPawn);
		UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
		if (Move)
		{
			Move->StopMovementImmediately();
		}
		CurrentPawn->SetActorLocationAndRotation(StartLocation, StartRotation, false, nullptr, ETeleportType::TeleportPhysics);
		PC->ClientSetRotation(StartRotation, true);
		if (Move)
		{
			Move->SetMovementMode(MOVE_Falling);
		}
	}
	else
	{
		if (PC->PlayerState)
		{
			PC->PlayerState->SetIsOnlyASpectator(false);
		}
		RestartPlayer(PC);
	}
	FinishedPawns.Remove(TNPS->GetPlayerId());
}

void ATN_SurvivalGameMode::FinishSurvival(int32 WinnerId)
{
	if (bMatchOver)
	{
		return;
	}
	bMatchOver = true;
	bLevelLoading = false;
	GetWorldTimerManager().ClearTimer(LevelTransitionTimerHandle);
	GetWorldTimerManager().ClearTimer(LevelReadyPollHandle);
	StopLevelStorm();

	const TArray<FTNSurvivalPlayer> Players = GatherPlayers();
	const TArray<int32> Ranked = TNSurvivalLogic::RankPlayers(Players, WinnerId);
	const float Now = GetWorld()->GetTimeSeconds() - MatchStartServerTime;

	ATN_CoopGameState* TNGS = GetGameState<ATN_CoopGameState>();
	for (int32 Index = 0; Index < Ranked.Num(); ++Index)
	{
		for (APlayerState* BasePS : GameState->PlayerArray)
		{
			ATN_CoopPlayerState* TNPS = Cast<ATN_CoopPlayerState>(BasePS);
			if (!TNPS || TNPS->GetPlayerId() != Ranked[Index])
			{
				continue;
			}

			TNPS->FinishRank = Index + 1;
			TNPS->ForceNetUpdate();
			const float Time = TNPS->bIsAlive ? Now : TNPS->FinishTimeSeconds;
			if (TNGS)
			{
				// Todos con puesto (sin «eliminado»): el marcador se ordena por FinishRank.
				TNGS->Server_UpsertRaceResult(TNPS->GetPlayerId(), TNPS->GetPlayerName(), Index + 1, Time, TNPS->RaceScore, false);
			}
			break;
		}
	}

	UE_LOG(LogTortunabo, Log, TEXT("[Survival] ═══ Fin de partida en el nivel %d · %s PlayerId=%d ═══"),
		CurrentLevel, StartingPlayers <= 1 ? TEXT("solitario,") : TEXT("gana"), WinnerId);

	StartResults();
}

void ATN_SurvivalGameMode::StartLevelStorm()
{
	const ATN_ChunkManager* Manager = FindChunkManager();
	ATN_ProcMapGenerator* Generator = Manager ? Manager->GetLevelGenerator() : nullptr;
	if (!Generator || !Generator->IsMapReady())
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Survival] El nivel %d no tiene mapa listo: va sin tormenta."), CurrentLevel);
		return;
	}
	if (!Storm)
	{
		Storm = ATN_PathStorm::SpawnFor(GetWorld(), Generator);
	}
	if (!Storm)
	{
		return;
	}
	const float Speed = StormSpeed;
	// Sale a la vez que las tortugas, sin espera: solo la ventaja de aparecer 30 m por detrás de la salida.
	Storm->StartStorm(Generator, Speed, 0.f);
	UE_LOG(LogTortunabo, Log, TEXT("[Survival] Tormenta del nivel %d: %.0f cm/s por un camino de %.0f m."),
		CurrentLevel, Speed, Generator->GetMainPathLength() / 100.f);
}

void ATN_SurvivalGameMode::StopLevelStorm()
{
	if (Storm)
	{
		Storm->StopStorm();
	}
}
