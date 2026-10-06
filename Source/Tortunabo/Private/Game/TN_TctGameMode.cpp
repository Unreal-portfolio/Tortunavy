#include "Game/TN_TctGameMode.h"
#include "Game/TN_TctGameState.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_GameModeSpawnUtils.h"
#include "Core/TN_Log.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Player/MP_GamePlayerController.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/TN_TctArena.h"
#include "World/TN_TctItemPad.h"
#include "Misc/DateTime.h"

#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace TNTctGameModeDetail
{
	ATN_TctGameMode* FindGameMode(const UWorld* World)
	{
		return World ? World->GetAuthGameMode<ATN_TctGameMode>() : nullptr;
	}

	int32 IntArg(const TArray<FString>& Args, int32 Index, int32 Default)
	{
		return Args.IsValidIndex(Index) ? FCString::Atoi(*Args[Index]) : Default;
	}

	void WithGameMode(const UWorld* World, const TCHAR* Command, TFunctionRef<void(ATN_TctGameMode&)> Action)
	{
		if (ATN_TctGameMode* GM = FindGameMode(World))
		{
			Action(*GM);
			return;
		}
		UE_LOG(LogTortunabo, Warning, TEXT("[TcT] %s: solo en el anfitrión y en Todos contra Todos (?game=Tct)."), Command);
	}

	FAutoConsoleCommandWithWorldAndArgs CmdEliminate(TEXT("TN.Tct.Eliminate"),
		TEXT("Todos contra Todos: elimina a la tortuga N (0 = la primera en llegar, normalmente el anfitrión) como si cayera al agua. TN.Tct.Eliminate [jugadora = 0]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			WithGameMode(World, TEXT("TN.Tct.Eliminate"), [&Args](ATN_TctGameMode& GM) { GM.DebugEliminate(IntArg(Args, 0, 0)); });
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdWinRound(TEXT("TN.Tct.WinRound"),
		TEXT("Todos contra Todos: cierra la ronda en juego con la tortuga N como ganadora. TN.Tct.WinRound [jugadora = 0]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			WithGameMode(World, TEXT("TN.Tct.WinRound"), [&Args](ATN_TctGameMode& GM) { GM.DebugWinRound(IntArg(Args, 0, 0)); });
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdFlood(TEXT("TN.Tct.Flood"),
		TEXT("Todos contra Todos: el agua empieza ya su siguiente subida."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			WithGameMode(World, TEXT("TN.Tct.Flood"), [](ATN_TctGameMode& GM) { GM.DebugFloodNow(); });
		}));
}

ATN_TctGameMode::ATN_TctGameMode()
{
	GameStateClass = ATN_TctGameState::StaticClass();
	// Morir es definitivo dentro de la ronda: sin DBNO ni rescate (el tótem sí salva).
	bAllowRevive = false;

	// Los mismos Blueprints que el resto de modos: la clase C++ sirve tal cual como ?game=Tct sin un BP propio.
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

// ─────────────────────────────────────────────────────────────────────────────
// Arena
// ─────────────────────────────────────────────────────────────────────────────

void ATN_TctGameMode::StartPlay()
{
	// StartPlay va antes del BeginPlay de los actores: la arena ya está cargada cuando la base reparte a las tortugas.
	bArenaReady = SetUpArena();
	Super::StartPlay();
}

bool ATN_TctGameMode::HasDefaultArena()
{
	return ATN_TctArena::VariantExists(GetDefault<ATN_TctGameMode>()->DefaultArenaVariant);
}

ATN_TctGameState* ATN_TctGameMode::GetTctState() const
{
	return GetGameState<ATN_TctGameState>();
}

bool ATN_TctGameMode::SetUpArena()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FName Wanted = DefaultArenaVariant;
	const FString ArenaOption = UGameplayStatics::ParseOption(OptionsString, TEXT("Arena"));
	if (!ArenaOption.IsEmpty())
	{
		if (ATN_TctArena::VariantExists(FName(*ArenaOption)))
		{
			Wanted = FName(*ArenaOption);
		}
		else
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[TcT] ?Arena=%s no existe en Scripts/terrain_volumes/Variants: se juega en %s."),
				*ArenaOption, *DefaultArenaVariant.ToString());
		}
	}

	Arena = ATN_TctArena::Find(World);
	if (!Arena)
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("TctArena");
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Arena = World->SpawnActor<ATN_TctArena>(ATN_TctArena::StaticClass(), FTransform::Identity, Params);
		UE_LOG(LogTortunabo, Log, TEXT("[TcT] El mapa no trae ATN_TctArena: se crea una en el origen."));
	}
	if (!Arena)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[TcT] No se ha podido crear la arena."));
		return false;
	}
	if (!ATN_TctArena::VariantExists(Wanted))
	{
		UE_LOG(LogTortunabo, Error, TEXT("[TcT] La arena %s no está en Scripts/terrain_volumes/Variants (el modo solo se juega sin cocinar): no hay partida."),
			*Wanted.ToString());
		return false;
	}
	Arena->ServerSetArenaVariant(Wanted);
	if (!Arena->Survey(SurveySpacing))
	{
		return false;
	}

	const FBox& Box = Arena->GetGroundBox();
	ArenaBounds.Min = Box.Min;
	ArenaBounds.Max = Box.Max;
	ArenaBounds.OutMargin = OutOfBoundsMargin;
	ArenaBounds.WadeDepth = WadeDepth;
	BuildFloodPlan();
	CreateSpawnPoints();
	CreateItemPads();
	HoldWater(FloodPlan.BaseZ);
	// El decorado vivo (#829): la semilla de esta partida y los sitios libres, que cada máquina usa para repartir lo mismo.
	if (SceneryMatchSeed == 0)
	{
		const int64 Ticks = FDateTime::UtcNow().GetTicks();
		SceneryMatchSeed = static_cast<uint32>(Ticks ^ (Ticks >> 32)) | 1u;
	}
	Arena->ServerSetScenery(SceneryMatchSeed, MakeSceneryKeepOut());
	return true;
}

TArray<FIntVector> ATN_TctGameMode::MakeSceneryKeepOut() const
{
	TArray<FIntVector> Zones;
	for (const APlayerStart* Start : SpawnPoints)
	{
		if (IsValid(Start))
		{
			Zones.Add(FIntVector(FMath::RoundToInt(Start->GetActorLocation().X), FMath::RoundToInt(Start->GetActorLocation().Y), FMath::RoundToInt(SceneryKeepOutSpawn)));
		}
	}
	for (const ATN_TctItemPad* Pad : ItemPads)
	{
		if (IsValid(Pad))
		{
			Zones.Add(FIntVector(FMath::RoundToInt(Pad->GetActorLocation().X), FMath::RoundToInt(Pad->GetActorLocation().Y), FMath::RoundToInt(SceneryKeepOutPad)));
		}
	}
	return Zones;
}

void ATN_TctGameMode::BuildFloodPlan()
{
	FloodPlan = FTNTctFloodPlan();
	FloodPlan.BaseZ = Arena ? Arena->GetBaseWaterZ() : 0.f;
	const TArray<float>& Heights = Arena ? Arena->GetSurveyHeights() : TArray<float>();
	FloodPlan.Levels = TNTctRules::ComputeFloodLevels(Heights, FloodMaxSteps, FloodKeepFraction, FloodTierTolerance, FloodMargin);
	float Top = FloodPlan.BaseZ;
	for (const float Height : Heights)
	{
		Top = FMath::Max(Top, Height);
	}
	FloodPlan.SuddenDeathZ = Top + SuddenDeathMargin;
	FloodPlan.StartDelay = FloodStartDelay;
	FloodPlan.StepSeconds = FloodStepSeconds;
	FloodPlan.RiseSeconds = FMath::Min(FloodRiseSeconds, FloodStepSeconds);
	FloodPlan.SuddenDeathRiseSeconds = SuddenDeathRiseSeconds;

	FString LevelsText;
	for (const float Level : FloodPlan.Levels)
	{
		LevelsText += FString::Printf(TEXT(" %.0f"), Level);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] Agua: mar a %.0f, escalones [%s ] cada %.0f s desde %.0f s, muerte súbita hasta %.0f a los %.0f s."),
		FloodPlan.BaseZ, *LevelsText, FloodPlan.StepSeconds, FloodPlan.StartDelay, FloodPlan.SuddenDeathZ,
		TNTctRules::StepStartSeconds(FloodPlan, FloodPlan.Levels.Num()));
}

void ATN_TctGameMode::CreateSpawnPoints()
{
	for (APlayerStart* Old : SpawnPoints)
	{
		if (IsValid(Old))
		{
			Old->Destroy();
		}
	}
	SpawnPoints.Reset();
	if (!Arena)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FTransform& Spawn : Arena->PickSpawnTransforms(TNTctRules::MaxPlayers, SpawnLift))
	{
		if (APlayerStart* Start = GetWorld()->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), Spawn, Params))
		{
			SpawnPoints.Add(Start);
		}
	}
	if (SpawnPoints.Num() < TNTctRules::MaxPlayers)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[TcT] Solo %d sitios de salida en la arena (se esperaban %d)."), SpawnPoints.Num(), TNTctRules::MaxPlayers);
	}
}

AActor* ATN_TctGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	TArray<AActor*> Starts;
	for (APlayerStart* Start : SpawnPoints)
	{
		if (IsValid(Start))
		{
			Starts.Add(Start);
		}
	}
	if (Starts.Num() == 0)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}
	AActor* Start = TN_PickSpreadPlayerStart(GetWorld(), Starts, Player, DefaultPawnClass, TEXT("TcT"));
	return Start ? Start : Starts[0];
}

// ─────────────────────────────────────────────────────────────────────────────
// Jugadoras
// ─────────────────────────────────────────────────────────────────────────────

void ATN_TctGameMode::PostLogin(APlayerController* NewPlayer)
{
	// Antes de Super: la base puede arrancar la primera ronda con esta jugadora (la última que faltaba), y esa no se queda fuera.
	const bool bJoinsLiveRound = bRoundLive;
	Super::PostLogin(NewPlayer);
	if (bJoinsLiveRound)
	{
		// Reconexión o entrada tardía con la ronda en juego: espera como fantasma a la siguiente.
		SitOutRound(NewPlayer);
	}
	else if (!bRoundLive)
	{
		FreezePlayer(NewPlayer);
	}
}

void ATN_TctGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Reconexión: AGameMode::PostLogin (FindInactivePlayer) ya le ha devuelto su PlayerState, con el PlayerId de antes. Deja de
	// contar como ida antes de que la base pueda arrancar la ronda, para que GatherFighters y GetPlayingControllers la incluyan.
	if (const APlayerState* PS = NewPlayer ? NewPlayer->PlayerState.Get() : nullptr)
	{
		LeftPlayerIds.Remove(PS->GetPlayerId());
	}
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
	if (!bRoundLive)
	{
		FreezePlayer(NewPlayer);
	}
}

void ATN_TctGameMode::Logout(AController* Exiting)
{
	if (const APlayerState* ExitingPS = Exiting ? Exiting->PlayerState.Get() : nullptr)
	{
		LeftPlayerIds.Add(ExitingPS->GetPlayerId());
	}
	// La base vuelve a mirar la ronda (UpdateRoundProgressAndMaybeFinish) sin la que se va.
	Super::Logout(Exiting);
	if (bMatchStarted && !bRoundLive && !bMatchOver && !bLeaving)
	{
		// Entre rondas: si ya solo queda una (o ninguna), la partida se acaba ahí.
		ContinueOrFinish();
	}
}

void ATN_TctGameMode::MarkPlayerDead(APlayerController* PlayerController)
{
	if (!bRoundLive || bMatchOver)
	{
		return;
	}
	Super::MarkPlayerDead(PlayerController);
}

TArray<FTNTctFighter> ATN_TctGameMode::GatherFighters() const
{
	TArray<FTNTctFighter> Fighters;
	if (!GameState)
	{
		return Fighters;
	}
	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		const ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS);
		if (!PS)
		{
			continue;
		}
		FTNTctFighter& Fighter = Fighters.AddDefaulted_GetRef();
		Fighter.Id = PS->GetPlayerId();
		Fighter.bConnected = !PS->IsInactive() && !LeftPlayerIds.Contains(Fighter.Id);
		Fighter.bAlive = PS->bIsAlive;
		Fighter.Wins = PS->RoundWins;
	}
	return Fighters;
}

TArray<APlayerController*> ATN_TctGameMode::GetPlayingControllers() const
{
	TArray<APlayerController*> Controllers;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		const APlayerState* PS = PC ? PC->PlayerState.Get() : nullptr;
		if (PS && !LeftPlayerIds.Contains(PS->GetPlayerId()))
		{
			Controllers.Add(PC);
		}
	}
	// Siempre en el mismo orden (el de llegada): cada una sabe cuál es su sitio y la rotación de salidas es estable.
	Controllers.Sort([](const APlayerController& A, const APlayerController& B)
	{
		return A.PlayerState->GetPlayerId() < B.PlayerState->GetPlayerId();
	});
	return Controllers;
}

ATN_CoopPlayerState* ATN_TctGameMode::FindPlayerState(int32 PlayerId) const
{
	if (!GameState)
	{
		return nullptr;
	}
	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS);
		if (PS && PS->GetPlayerId() == PlayerId)
		{
			return PS;
		}
	}
	return nullptr;
}

void ATN_TctGameMode::FreezePlayer(APlayerController* PlayerController) const
{
	if (!PlayerController)
	{
		return;
	}
	PlayerController->ClientIgnoreMoveInput(true);
	ACharacter* Character = Cast<ACharacter>(PlayerController->GetPawn());
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (Move && Move->MovementMode != MOVE_None)
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}
}

void ATN_TctGameMode::UnfreezePlayers() const
{
	for (APlayerController* PC : GetPlayingControllers())
	{
		ACharacter* Character = Cast<ACharacter>(PC->GetPawn());
		UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
		if (Move && Move->MovementMode == MOVE_None)
		{
			Move->SetMovementMode(MOVE_Falling);
		}
		if (AMP_GamePlayerController* TNPC = Cast<AMP_GamePlayerController>(PC))
		{
			TNPC->ClientRestorePlayerInput();
			// Los Client RPC no corren en el servidor escucha: el anfitrión, directamente.
			if (TNPC->IsLocalController())
			{
				TNPC->ForceRestoreInput();
			}
		}
		else
		{
			PC->ResetIgnoreInputFlags();
			PC->ClientIgnoreMoveInput(false);
		}
	}
}

void ATN_TctGameMode::SitOutRound(APlayerController* PlayerController)
{
	ATN_CoopPlayerState* PS = PlayerController ? PlayerController->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
	if (!PS)
	{
		return;
	}
	PS->bIsAlive = false;
	PS->bIsEliminated = true;
	PS->bHasFinishedRun = true;
	PS->ForceNetUpdate();
	if (APawn* Pawn = PlayerController->GetPawn())
	{
		PlayerController->UnPossess();
		Pawn->Destroy();
	}
	MovePlayerToSpectator(PlayerController);
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] '%s' entra con la ronda %d en juego: fantasma hasta la siguiente."),
		*PS->GetPlayerName(), CurrentRound);
}

void ATN_TctGameMode::PlaceForRound(APlayerController* PlayerController, const FTransform& Spawn)
{
	ATN_CoopPlayerState* PS = PlayerController ? PlayerController->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
	if (!PS)
	{
		return;
	}
	const int32 Id = PS->GetPlayerId();
	const bool bWasOut = !PS->bIsAlive || PlayerController->GetStateName() == NAME_Spectating || !PlayerController->GetPawn();
	PS->ResetForNewRace();
	PS->ForceNetUpdate();

	if (bWasOut)
	{
		// Eliminada en la ronda anterior (o sin tortuga): tortuga nueva en su sitio. El cuerpo de la anterior se va.
		if (const TWeakObjectPtr<APawn>* DeadPawn = DeadPlayerPawns.Find(Id))
		{
			if (DeadPawn->IsValid())
			{
				DeadPawn->Get()->Destroy();
			}
			DeadPlayerPawns.Remove(Id);
		}
		if (APawn* OldPawn = PlayerController->GetPawn())
		{
			PlayerController->UnPossess();
			OldPawn->Destroy();
		}
		PS->SetIsSpectator(false);
		PS->SetIsOnlyASpectator(false);
		PlayerController->ChangeState(NAME_Playing);
		PlayerController->ClientGotoState(NAME_Playing);
		RestartPlayerAtTransform(PlayerController, Spawn);
		if (APawn* NewPawn = PlayerController->GetPawn())
		{
			NewPawn->EnableInput(PlayerController);
			PlayerController->SetViewTarget(NewPawn);
		}
	}
	else if (ACharacter* Character = Cast<ACharacter>(PlayerController->GetPawn()))
	{
		// Teletransporte limpio: fuera del caparazón, del derribo, de lo que lleve y de quien la lleve.
		TNBeach::RelocateTurtle(Character, Spawn);
	}
	// Cada ronda se empieza con las manos vacías y sin lastre.
	ResetItemsForRound(PlayerController->GetPawn());
	PlayerController->ClientSetRotation(Spawn.Rotator(), true);
	FreezePlayer(PlayerController);
}

void ATN_TctGameMode::ResetMatchScores() const
{
	if (!GameState)
	{
		return;
	}
	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		if (ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS))
		{
			PS->RoundWins = 0;
			PS->RaceShellHalves = 0;
			PS->TeamIndex = -1;
			PS->ForceNetUpdate();
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Pantalla de la campeona
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_TctGameMode::HandleChampionChoice(ETNBeachChampionChoice Choice)
{
	if (!HasAuthority() || !CanChooseChampion())
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[TcT] %s: solo en la pantalla de la campeona."), *UEnum::GetValueAsString(Choice));
		return false;
	}
	switch (Choice)
	{
	case ETNBeachChampionChoice::PlayAgain:
		PlayAgain();
		return true;
	case ETNBeachChampionChoice::ChangeMode:
		// Como en la carrera: «el otro modo» es el cooperativo; en el lobby se sale a él al ponerse listas.
		if (UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance()))
		{
			GI->SelectedProcMode = ETNProcGameMode::Coop;
		}
		UE_LOG(LogTortunabo, Log, TEXT("[TcT] Cambiar de modo: vuelta al lobby con el cooperativo elegido."));
		LeaveAfterDelay([this]() { FinishRoundAndReturnToLobby(); });
		return true;
	case ETNBeachChampionChoice::Quit:
		UE_LOG(LogTortunabo, Log, TEXT("[TcT] Salir: el anfitrión vuelve al menú y la partida se cierra."));
		LeaveAfterDelay([this]()
		{
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
				if (Pawn)
				{
					Pawn->Destroy();
				}
			}
			GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance()))
				{
					GI->HandleReturnToMenu();
				}
			}));
		});
		return true;
	default:
		return false;
	}
}

void ATN_TctGameMode::LeaveAfterDelay(TFunction<void()> Action)
{
	bLeaving = true;
	CancelRoundTimers();
	// Con CountdownValue = 1 en Results se cierra el huevo en todas las pantallas y la música se funde.
	if (ATN_TctGameState* State = GetTctState())
	{
		State->CountdownValue = 1;
	}
	GetWorldTimerManager().SetTimer(LeaveHandle, FTimerDelegate::CreateWeakLambda(this, [DeferredAction = MoveTemp(Action)]()
	{
		DeferredAction();
	}), ChampionLeaveDelaySeconds, false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pruebas
// ─────────────────────────────────────────────────────────────────────────────

void ATN_TctGameMode::DebugEliminate(int32 PlayerIndex)
{
	const TArray<APlayerController*> Controllers = GetPlayingControllers();
	if (!Controllers.IsValidIndex(PlayerIndex))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[TcT] TN.Tct.Eliminate: no hay jugadora %d (hay %d)."), PlayerIndex, Controllers.Num());
		return;
	}
	MarkPlayerDead(Controllers[PlayerIndex]);
}

void ATN_TctGameMode::DebugWinRound(int32 PlayerIndex)
{
	const TArray<APlayerController*> Controllers = GetPlayingControllers();
	if (!bRoundLive || !Controllers.IsValidIndex(PlayerIndex))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[TcT] TN.Tct.WinRound: sin ronda en juego o sin jugadora %d."), PlayerIndex);
		return;
	}
	EndRound(Controllers[PlayerIndex]->GetPlayerState<ATN_CoopPlayerState>());
}

void ATN_TctGameMode::DebugFloodNow()
{
	ATN_TctGameState* State = GetTctState();
	if (!bRoundLive || !State || State->Flood.StartServerTime < 0.f)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[TcT] TN.Tct.Flood: sin ronda en juego."));
		return;
	}
	const float ToNext = State->GetSecondsToNextRise();
	if (ToNext <= 0.f)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[TcT] TN.Tct.Flood: el agua ya no sube más."));
		return;
	}
	// Adelanta el reloj del agua (la hora de salida) lo que falta para la siguiente subida.
	State->Flood.StartServerTime -= ToNext;
	State->ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] TN.Tct.Flood: el agua sube ya (adelantada %.1f s)."), ToNext);
}
