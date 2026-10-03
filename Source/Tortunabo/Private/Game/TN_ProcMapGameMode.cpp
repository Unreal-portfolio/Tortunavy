#include "Game/TN_ProcMapGameMode.h"
#include "Game/TN_ProcMapGameState.h"
#include "Core/TN_Log.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_GameModeSpawnUtils.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Player/MP_GamePlayerController.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TN_StaminaComponent.h"
#include "World/TN_RescuePickup.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "World/ProcMap/TN_ProcMapTypes.h"
#include "World/ProcMap/TN_ProcEggNest.h"
#include "World/ProcMap/TN_ProcStartStructure.h"
#include "World/ProcMap/TN_PathStorm.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Misc/DateTime.h"

namespace TNProcMapGameModeDetail
{
	TAutoConsoleVariable<int32> CVarProcStartStyle(TEXT("TN.Proc.StartStyle"), -1,
		TEXT("Salida del mapa procedural: -1 = lo del lobby (por defecto), 0 = puerta doble, 1 = huevos. Vale desde la siguiente generación del mapa."));

	/** Con estructura de salida, un PlayerStart está ocupado si hay otro peón a menos de esto (los sitios de la sala distan ~2 m). */
	constexpr double StructureStartTakenRadius = 80.0;

	/**
	 * Salida de un mapa sin generador: el PlayerStart con la etiqueta MapVariantStart o, si no hay, el primero (el que
	 * ATN_MapVariantLoader lleva a la salida de la variante).
	 */
	bool FindLevelStartTransform(UWorld* World, FTransform& OutTransform)
	{
		if (!World)
		{
			return false;
		}
		const APlayerStart* First = nullptr;
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			if (It->ActorHasTag(TEXT("MapVariantStart")))
			{
				First = *It;
				break;
			}
			First = First ? First : *It;
		}
		if (First)
		{
			OutTransform = First->GetActorTransform();
		}
		return First != nullptr;
	}
}

ATN_ProcMapGameMode::ATN_ProcMapGameMode()
{
	GameStateClass = ATN_ProcMapGameState::StaticClass();
	GeneratorClass = ATN_ProcMapGenerator::StaticClass();
	PathStormClass = ATN_PathStorm::StaticClass();

	// Los mismos Blueprints que BP_RunGameMode: así la clase C++ ya sirve como
	// GameMode Override de LVL_ProcMap aunque no exista un BP propio.
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
	static ConstructorHelpers::FClassFinder<ATN_RescuePickup> RescueBP(TEXT("/Game/Blueprints/Gameplay/Items/BP_RescuePickUp"));
	if (RescueBP.Succeeded())
	{
		RescuePickupClass = RescueBP.Class;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Arranque
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcMapGameMode::BeginPlay()
{
	ResolveModeAndDifficulty();
	EnsureGenerator();

	// La primera ronda se genera antes de que la base spawnee a nadie: así los
	// PlayerStart del mapa ya existen cuando los jugadores llegan del lobby.
	CurrentRound = 1;
	GenerateRoundMap();

	Super::BeginPlay();

	const FString Goal = Mode == ETNProcGameMode::Coop
		? FString::Printf(TEXT("%d ronda(s)"), CoopRounds)
		: FString::Printf(TEXT("gana quien llegue a %d rondas"), WinsToWinMatch);
	UE_LOG(LogTortunabo, Log, TEXT("[ProcMapGameMode] Modo %s · dificultad %s · %s"),
		*UEnum::GetValueAsString(Mode), *UEnum::GetValueAsString(Difficulty), *Goal);
	SyncGameState();
}

void ATN_ProcMapGameMode::ResolveModeAndDifficulty()
{
	Mode = ModeWithoutLobby;
	Difficulty = DifficultyWithoutLobby;

	// Lo elegido en el lobby (Clásico y Supervivencia nunca llegan aquí: viajan a LVL_Run).
	if (const UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance()))
	{
		if (GI->SelectedProcMode != ETNProcGameMode::Classic && GI->SelectedProcMode != ETNProcGameMode::Survival
			&& GI->SelectedProcMode != ETNProcGameMode::Count)
		{
			Mode = GI->SelectedProcMode;
			Difficulty = GI->SelectedProcDifficulty;
		}
	}

	// Opciones de URL para probar sin lobby: open LVL_ProcMap?ProcMode=Race?ProcDifficulty=Hard?ProcSeed=42
	// (?ProcMode=Survival: el mapa de Supervivencia, #273; Fácil/Normal/Difícil = dificultad 1/3/5).
	const FString ModeOption = UGameplayStatics::ParseOption(OptionsString, TEXT("ProcMode"));
	if (ModeOption.Equals(TEXT("Coop"), ESearchCase::IgnoreCase)) { Mode = ETNProcGameMode::Coop; }
	else if (ModeOption.Equals(TEXT("Race"), ESearchCase::IgnoreCase)) { Mode = ETNProcGameMode::Race; }
	else if (ModeOption.Equals(TEXT("2v2"), ESearchCase::IgnoreCase) || ModeOption.Equals(TEXT("TwoVsTwo"), ESearchCase::IgnoreCase)) { Mode = ETNProcGameMode::TwoVsTwo; }
	else if (ModeOption.Equals(TEXT("Survival"), ESearchCase::IgnoreCase)) { Mode = ETNProcGameMode::Survival; }

	const FString DifficultyOption = UGameplayStatics::ParseOption(OptionsString, TEXT("ProcDifficulty"));
	if (DifficultyOption.Equals(TEXT("Easy"), ESearchCase::IgnoreCase)) { Difficulty = ETNProcDifficulty::Easy; }
	else if (DifficultyOption.Equals(TEXT("Normal"), ESearchCase::IgnoreCase)) { Difficulty = ETNProcDifficulty::Normal; }
	else if (DifficultyOption.Equals(TEXT("Hard"), ESearchCase::IgnoreCase)) { Difficulty = ETNProcDifficulty::Hard; }

	const FString SeedOption = UGameplayStatics::ParseOption(OptionsString, TEXT("ProcSeed"));
	UrlSeed = SeedOption.IsEmpty() ? 0 : FCString::Atoi(*SeedOption);

	if (Mode == ETNProcGameMode::Classic || Mode == ETNProcGameMode::Count)
	{
		Mode = ETNProcGameMode::Coop;
	}
	if (Difficulty == ETNProcDifficulty::Count)
	{
		Difficulty = ETNProcDifficulty::Normal;
	}
}

void ATN_ProcMapGameMode::ResolveStartStyle()
{
	using namespace TNProcMapGameModeDetail;
	// Igual que se pusieron listos en el lobby (lo guarda ATN_HQGameMode en la GameInstance y en la URL del viaje); sin
	// lobby, la puerta doble. Para probar, la consola manda sobre todo.
	ETNMatchStartStyle Resolved = ETNMatchStartStyle::Gate;
	const TCHAR* From = TEXT("por defecto");
	if (const UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance()))
	{
		Resolved = GI->PendingStartStyle;
		From = TEXT("GameInstance");
	}
	const FString StartOption = UGameplayStatics::ParseOption(OptionsString, TEXT("ProcStart"));
	if (StartOption.Equals(TEXT("Gate"), ESearchCase::IgnoreCase))
	{
		Resolved = ETNMatchStartStyle::Gate;
		From = TEXT("URL");
	}
	else if (StartOption.Equals(TEXT("Eggs"), ESearchCase::IgnoreCase))
	{
		Resolved = ETNMatchStartStyle::Eggs;
		From = TEXT("URL");
	}
	const int32 Forced = CVarProcStartStyle.GetValueOnGameThread();
	if (Forced == 0 || Forced == 1)
	{
		Resolved = Forced == 1 ? ETNMatchStartStyle::Eggs : ETNMatchStartStyle::Gate;
		From = TEXT("TN.Proc.StartStyle");
	}
	StartStyle = Resolved;
	UE_LOG(LogTortunabo, Log, TEXT("[ProcMap] Salida: %s (%s)"), StartStyle == ETNMatchStartStyle::Eggs ? TEXT("huevos") : TEXT("puerta doble"), From);
}

void ATN_ProcMapGameMode::EnsureGenerator()
{
	if (!Generator)
	{
		for (TActorIterator<ATN_ProcMapGenerator> It(GetWorld()); It; ++It)
		{
			Generator = *It;
			break;
		}
	}
	if (!Generator)
	{
		UClass* Class = GeneratorClass ? GeneratorClass.Get() : ATN_ProcMapGenerator::StaticClass();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Generator = GetWorld()->SpawnActor<ATN_ProcMapGenerator>(Class, FTransform::Identity, Params);
		UE_LOG(LogTortunabo, Log, TEXT("[ProcMapGameMode] El nivel no tenía generador: creado %s."), *GetNameSafe(Generator));
	}
	if (Generator)
	{
		Generator->SetSettingsIfMissing(MapSettings);
	}
}

void ATN_ProcMapGameMode::GenerateRoundMap()
{
	if (!Generator)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[ProcMapGameMode] Sin generador: no hay mapa que jugar."));
		return;
	}

	// La estructura de salida (puerta doble o huevos) la pone el generador con el mapa; se vuelve a mirar cada ronda
	// para que TN.Proc.StartStyle valga sin reiniciar.
	ResolveStartStyle();
	Generator->SetStartStructureStyle(StartStyle);

	const int32 BaseSeed = UrlSeed != 0 ? UrlSeed : FixedSeed;
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		int32 Seed = 0;
		if (BaseSeed != 0 && Attempt == 0)
		{
			Seed = BaseSeed + (CurrentRound - 1);
		}
		else
		{
			const uint64 Ticks = static_cast<uint64>(FDateTime::UtcNow().GetTicks());
			const uint32 Mixed = static_cast<uint32>(Ticks) ^ static_cast<uint32>(Ticks >> 32)
				^ (static_cast<uint32>(FMath::Rand()) << 8)
				^ (static_cast<uint32>(CurrentRound + Attempt) * 0x9E3779B9u);
			Seed = static_cast<int32>(Mixed & 0x7FFFFFFFu);
		}

		// En el servidor la construcción es síncrona: al volver ya sabemos si hay mapa.
		Generator->ServerGenerate(Seed, Mode, Difficulty);
		if (Generator->IsMapReady())
		{
			break;
		}
		UE_LOG(LogTortunabo, Warning, TEXT("[ProcMapGameMode] La semilla %d no dio mapa, probando otra."), Seed);
	}

	NestReachedByPlayer.Reset();
	TeamBestNest = -1;
	bWaitingForMap = true;
	WaitStartTime = GetWorld()->GetTimeSeconds();
	GetWorldTimerManager().SetTimer(MapReadyPollHandle, this, &ATN_ProcMapGameMode::PollMapReady, 0.25f, true);
	SyncGameState();
}

void ATN_ProcMapGameMode::OnWaitingTimeout()
{
	// La base avisa cuando han llegado todos del lobby (o venció la espera). La
	// ronda arranca cuando además todos tienen el mapa construido.
	if (bMatchStarted)
	{
		return;
	}
	bPlayersArrived = true;
	GetWorldTimerManager().ClearTimer(WaitingTimeoutTimerHandle);
	PollMapReady();
}

void ATN_ProcMapGameMode::NotifyClientMapReady(APlayerController* PlayerController, int32 Generation)
{
	if (!PlayerController)
	{
		return;
	}
	int32& Ready = ClientReadyGeneration.FindOrAdd(PlayerController);
	Ready = FMath::Max(Ready, Generation);
	UE_LOG(LogTortunabo, Log, TEXT("[ProcMapGameMode] %s tiene el mapa (generación %d)."), *GetNameSafe(PlayerController), Generation);
}

void ATN_ProcMapGameMode::PollMapReady()
{
	if (!bWaitingForMap)
	{
		GetWorldTimerManager().ClearTimer(MapReadyPollHandle);
		return;
	}

	FreezeWaitingPlayers();
	if (!bPlayersArrived || !Generator)
	{
		return;
	}

	const int32 Wanted = Generator->GetRequestedGeneration();
	int32 ReadyCount = 0;
	int32 Total = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		++Total;
		if (PC->IsLocalController())
		{
			// El host comparte el mapa del servidor.
			ReadyCount += (Generator->IsMapReady() && Generator->GetBuiltGeneration() == Wanted) ? 1 : 0;
			continue;
		}
		const int32* Generation = ClientReadyGeneration.Find(PC);
		ReadyCount += (Generation && *Generation >= Wanted) ? 1 : 0;
	}
	if (ATN_ProcMapGameState* GS = GetProcGameState())
	{
		GS->ReadyPlayers = ReadyCount;
	}

	const float Waited = GetWorld()->GetTimeSeconds() - WaitStartTime;
	if (Waited < MinPreRoundSeconds)
	{
		return;
	}
	const bool bAllReady = ReadyCount >= Total;
	if (!bAllReady && Waited < MapReadyTimeoutSeconds)
	{
		return;
	}
	if (!bAllReady)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[ProcMapGameMode] %d/%d jugadores con el mapa tras %.0fs: se arranca igualmente."),
			ReadyCount, Total, Waited);
	}
	BeginRoundPlay();
}

// ─────────────────────────────────────────────────────────────────────────────
// Ronda
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcMapGameMode::BeginRoundPlay()
{
	bWaitingForMap = false;
	GetWorldTimerManager().ClearTimer(MapReadyPollHandle);

	if (Mode == ETNProcGameMode::TwoVsTwo && CountConnectedPlayers() != 4)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[ProcMapGameMode] 2vs2 exige 4 jugadores (hay %d): se juega como Carrera."), CountConnectedPlayers());
		Mode = ETNProcGameMode::Race;
	}

	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		if (ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS))
		{
			if (CurrentRound == 1)
			{
				PS->RoundWins = 0;
			}
			PS->TeamIndex = -1;
		}
	}
	if (Mode == ETNProcGameMode::TwoVsTwo)
	{
		AssignTwoVsTwoTeams();
	}

	PlacePlayersAtStart();

	if (!bMatchStarted)
	{
		// Primera ronda: el arranque de la base (InProgress + cronómetro).
		Super::OnWaitingTimeout();
	}
	else
	{
		MatchStartServerTime = GetWorld()->GetTimeSeconds();
		SetFlowState(ETNMatchFlowState::InProgress);
	}

	bRoundActive = true;
	StartStormIfNeeded();

	// La salida se abre con el «¡ADELANTE!» de la pantalla de carga: gira la puerta 2 o se rompen los huevos.
	GetWorldTimerManager().ClearTimer(StartStructureOpenHandle);
	if (Generator && Generator->GetStartStructure())
	{
		GetWorldTimerManager().SetTimer(StartStructureOpenHandle, this, &ATN_ProcMapGameMode::OpenStartStructure,
			FMath::Max(0.01f, StartStructureOpenDelaySeconds), false);
	}

	GetWorldTimerManager().ClearTimer(RoundTimeLimitHandle);
	if (Mode != ETNProcGameMode::Coop && CompetitiveRoundTimeLimitSeconds > 0.f)
	{
		GetWorldTimerManager().SetTimer(RoundTimeLimitHandle, this, &ATN_ProcMapGameMode::OnRoundTimeLimit, CompetitiveRoundTimeLimitSeconds, false);
	}

	UE_LOG(LogTortunabo, Log, TEXT("[ProcMapGameMode] ═══ Ronda %d en marcha · %s · semilla %d · ~%.0f min ═══"),
		CurrentRound, *UEnum::GetValueAsString(Mode),
		Generator ? Generator->GetNetConfig().Seed : 0,
		Generator ? Generator->EstimateTraversalMinutes() : 0.f);
	SyncGameState();
}

void ATN_ProcMapGameMode::PlacePlayersAtStart()
{
	if (!Generator)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}

		APawn* Pawn = PC->GetPawn();
		const bool bSpectating = PC->GetStateName() == NAME_Spectating || (PC->PlayerState && PC->PlayerState->IsOnlyASpectator());
		if (!Pawn || bSpectating)
		{
			RespawnControllerFresh(PC);
			Pawn = PC->GetPawn();
		}
		if (!Pawn)
		{
			continue;
		}

		// Cada uno en su sitio de la estructura de salida (sala o huevo), de pie con su cápsula; si no, el anillo del claro.
		const FTransform Start = GetRoundStartTransform(PC, Pawn);
		ACharacter* Character = Cast<ACharacter>(Pawn);
		UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
		if (Move)
		{
			Move->StopMovementImmediately();
		}
		Pawn->SetActorLocationAndRotation(Start.GetLocation(), Start.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->SetActorHiddenInGame(false);
		Pawn->SetActorEnableCollision(true);
		PC->ClientSetRotation(Start.Rotator(), true);
		if (Move)
		{
			Move->SetMovementMode(MOVE_Falling);
		}
	}

	UnfreezeAllPlayers();
}

FTransform ATN_ProcMapGameMode::GetRoundStartTransform(const AController* Controller, const APawn* Pawn) const
{
	const int32 Slot = GetPlayerSlot(Controller);
	if (const ATN_ProcStartStructure* Structure = Generator ? Generator->GetStartStructure() : nullptr)
	{
		const ACharacter* Character = Cast<ACharacter>(Pawn);
		const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
		const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : ATN_ProcStartStructure::DefaultSpawnHalfHeight;
		FTransform InStructure;
		if (Structure->GetSpawnTransform(Slot, HalfHeight, InStructure))
		{
			return InStructure;
		}
	}
	return Generator ? Generator->GetStartTransform(Slot) : FTransform::Identity;
}

void ATN_ProcMapGameMode::OpenStartStructure()
{
	if (!bRoundActive)
	{
		return;
	}
	if (ATN_ProcStartStructure* Structure = Generator ? Generator->GetStartStructure() : nullptr)
	{
		Structure->Open();
	}
}

void ATN_ProcMapGameMode::RespawnControllerFresh(APlayerController* PlayerController)
{
	if (APawn* OldPawn = PlayerController->GetPawn())
	{
		PlayerController->UnPossess();
		OldPawn->Destroy();
	}
	if (APlayerState* PS = PlayerController->PlayerState)
	{
		PS->SetIsSpectator(false);
		PS->SetIsOnlyASpectator(false);
	}
	PlayerController->ChangeState(NAME_Playing);
	PlayerController->ClientGotoState(NAME_Playing);

	RestartPlayer(PlayerController);

	if (APawn* NewPawn = PlayerController->GetPawn())
	{
		NewPawn->EnableInput(PlayerController);
		PlayerController->SetViewTarget(NewPawn);
	}
}

void ATN_ProcMapGameMode::FreezeWaitingPlayers()
{
	// Mientras alguien no tiene el mapa, nadie se mueve: servidor (el pawn) y
	// cliente (el input). Si el pawn cambia, se vuelve a avisar al cliente porque
	// ClientRestart limpia los flags de input.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		APawn* Pawn = PC->GetPawn();
		const TWeakObjectPtr<APawn>* Frozen = FrozenControllers.Find(PC);
		if (!Frozen || Frozen->Get() != Pawn)
		{
			PC->ClientIgnoreMoveInput(true);
			FrozenControllers.Add(PC, Pawn);
		}
		if (ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			UCharacterMovementComponent* Move = Character->GetCharacterMovement();
			if (Move && Move->MovementMode != MOVE_None)
			{
				Move->DisableMovement();
			}
		}
	}
}

void ATN_ProcMapGameMode::UnfreezeAllPlayers()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		if (AMP_GamePlayerController* TNPC = Cast<AMP_GamePlayerController>(PC))
		{
			TNPC->ClientRestorePlayerInput();
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
	FrozenControllers.Reset();
}

void ATN_ProcMapGameMode::StartStormIfNeeded()
{
	if (!Generator)
	{
		return;
	}
	const FTNProcMapProfile& Profile = Generator->GetActiveProfile();
	if (Mode != ETNProcGameMode::Coop || Profile.StormSpeed <= 0.f)
	{
		if (Storm)
		{
			Storm->StopStorm();
		}
		return;
	}

	if (!Storm)
	{
		UClass* StormClass = PathStormClass ? PathStormClass.Get() : ATN_PathStorm::StaticClass();
		if (const UTN_ProcMapSettings* Settings = Generator->GetSettings())
		{
			if (Settings->PathStormClass && Settings->PathStormClass->IsChildOf(ATN_PathStorm::StaticClass()))
			{
				StormClass = Settings->PathStormClass.Get();
			}
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Storm = GetWorld()->SpawnActor<ATN_PathStorm>(StormClass, FTransform::Identity, Params);
	}
	if (Storm)
	{
		// Nunca más rápida que una tortuga andando: si no, no hay forma de escapar de ella.
		const float WalkSpeed = GetTurtleWalkSpeed();
		const float Speed = WalkSpeed > 0.f ? FMath::Min(Profile.StormSpeed, WalkSpeed) : Profile.StormSpeed;
		Storm->StartStorm(Generator, Speed, Profile.StormGraceSeconds);
	}
}

float ATN_ProcMapGameMode::GetTurtleWalkSpeed() const
{
	// La más lenta de las tortugas en juego; si aún no hay ninguna, la del peón por defecto.
	float Walk = TNumericLimits<float>::Max();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (const UTN_StaminaComponent* Stamina = Pawn ? Pawn->FindComponentByClass<UTN_StaminaComponent>() : nullptr)
		{
			Walk = FMath::Min(Walk, Stamina->GetWalkSpeed());
		}
	}
	if (Walk == TNumericLimits<float>::Max() && DefaultPawnClass)
	{
		if (const ATortugaCharacter* Cdo = Cast<ATortugaCharacter>(DefaultPawnClass->GetDefaultObject()))
		{
			if (const UTN_StaminaComponent* Stamina = Cdo->FindComponentByClass<UTN_StaminaComponent>())
			{
				Walk = Stamina->GetWalkSpeed();
			}
		}
	}
	return Walk == TNumericLimits<float>::Max() ? 0.f : Walk;
}

void ATN_ProcMapGameMode::AssignTwoVsTwoTeams()
{
	TArray<ATN_CoopPlayerState*> Players;
	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		if (ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS))
		{
			Players.Add(PS);
		}
	}
	if (Players.Num() != 4)
	{
		return;
	}
	Players.Sort([](const ATN_CoopPlayerState& A, const ATN_CoopPlayerState& B) { return A.GetPlayerId() < B.GetPlayerId(); });

	// Rotación de parejas: AB|CD, AC|BD, AD|BC y vuelta a empezar.
	static const int32 Pairings[3][4] = { { 0, 0, 1, 1 }, { 0, 1, 0, 1 }, { 0, 1, 1, 0 } };
	const int32* Pairing = Pairings[(CurrentRound - 1) % 3];
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Players[Index]->TeamIndex = Pairing[Index];
	}
	UE_LOG(LogTortunabo, Log, TEXT("[ProcMapGameMode] Parejas de la ronda %d: %s  contra  %s"), CurrentRound,
		*DescribePlayers(GetTeamMembers(0)), *DescribePlayers(GetTeamMembers(1)));
}

// ─────────────────────────────────────────────────────────────────────────────
// Meta, muerte y reaparición
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcMapGameMode::MarkPlayerFinished(APlayerController* PlayerController)
{
	if (!HasAuthority() || !PlayerController || !bRoundActive || PendingRespawns.Contains(PlayerController))
	{
		return;
	}
	ATN_CoopPlayerState* PS = PlayerController->GetPlayerState<ATN_CoopPlayerState>();
	if (!PS || PS->bHasFinishedRun || !PS->bIsAlive)
	{
		return;
	}

	ReleaseCarry(Cast<ATortugaCharacter>(PlayerController->GetPawn()));

	// La base asigna puesto y puntos y pasa al jugador a espectador; quién gana la
	// ronda lo decide el modo a continuación.
	bSuppressRoundCheck = true;
	Super::MarkPlayerFinished(PlayerController);
	bSuppressRoundCheck = false;
	if (!PS->bHasFinishedRun)
	{
		return;
	}

	if (Mode == ETNProcGameMode::Race)
	{
		EndRound({ PlayerController }, FString::Printf(TEXT("¡%s gana la ronda!"), *PS->GetPlayerName()));
		return;
	}
	if (Mode == ETNProcGameMode::TwoVsTwo)
	{
		const TArray<APlayerController*> Team = GetTeamMembers(PS->TeamIndex);
		bool bWholeTeam = Team.Num() > 0;
		for (const APlayerController* Member : Team)
		{
			const ATN_CoopPlayerState* MemberPS = Member->GetPlayerState<ATN_CoopPlayerState>();
			bWholeTeam &= MemberPS && MemberPS->bHasFinishedRun && !MemberPS->bIsEliminated;
		}
		if (bWholeTeam)
		{
			EndRound(Team, FString::Printf(TEXT("¡Gana la pareja %s!"), *DescribePlayers(Team)));
			return;
		}
	}
	UpdateRoundProgressAndMaybeFinish();
}

void ATN_ProcMapGameMode::MarkPlayerDead(APlayerController* PlayerController)
{
	if (!HasAuthority() || !PlayerController)
	{
		return;
	}
	// Entre rondas o generando no muere nadie (p. ej. una zona de muerte del mapa viejo).
	if (!bRoundActive || PendingRespawns.Contains(PlayerController))
	{
		return;
	}
	const ATN_CoopPlayerState* PS = PlayerController->GetPlayerState<ATN_CoopPlayerState>();
	if (!PS || !PS->bIsAlive || PS->bHasFinishedRun)
	{
		return;
	}

	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PlayerController->GetPawn());
	FTransform RespawnAt;
	if (Turtle && FindRespawnTransform(PlayerController, RespawnAt))
	{
		BeginRespawn(PlayerController, Turtle);
		return;
	}

	// Sin pila válida (la tormenta las ha pasado todas): muerte normal con rescate.
	ReleaseCarry(Turtle);
	Super::MarkPlayerDead(PlayerController);
}

void ATN_ProcMapGameMode::NotifyEggNestReached(APlayerController* PlayerController, ATN_ProcEggNest* Nest)
{
	if (!bRoundActive || !PlayerController || !Nest)
	{
		return;
	}
	const ATN_CoopPlayerState* PS = PlayerController->GetPlayerState<ATN_CoopPlayerState>();
	if (!PS || !PS->bIsAlive || PS->bHasFinishedRun)
	{
		return;
	}

	int32& PlayerBest = NestReachedByPlayer.FindOrAdd(PS->GetPlayerId(), -1);
	const bool bNewForPlayer = Nest->GetNestOrder() > PlayerBest;
	PlayerBest = FMath::Max(PlayerBest, Nest->GetNestOrder());
	if (Mode == ETNProcGameMode::Coop)
	{
		TeamBestNest = FMath::Max(TeamBestNest, Nest->GetNestOrder());
	}
	if (!Nest->IsActivated())
	{
		Nest->MarkActivated();
	}
	if (bNewForPlayer)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[ProcMapGameMode] %s alcanza la pila de huevos %d."), *PS->GetPlayerName(), Nest->GetNestOrder());
	}
}

bool ATN_ProcMapGameMode::FindRespawnTransform(APlayerController* PlayerController, FTransform& OutTransform) const
{
	// Con generador, sus pilas y solo con el mapa construido (entre rondas se rehacen). Sin él (un mapa fijo con el
	// bloque placements del manifest, #652), las pilas que haya en el mundo.
	if (!PlayerController || (Generator && !Generator->IsMapReady()))
	{
		return false;
	}
	const ATN_CoopPlayerState* PS = PlayerController->GetPlayerState<ATN_CoopPlayerState>();
	if (!PS)
	{
		return false;
	}

	int32 ReachedOrder = -1;
	if (Mode == ETNProcGameMode::Coop)
	{
		ReachedOrder = TeamBestNest;
	}
	else if (const int32* PlayerBest = NestReachedByPlayer.Find(PS->GetPlayerId()))
	{
		ReachedOrder = *PlayerBest;
	}
	// La pila 0 del generador está siempre en la salida: cuenta como alcanzada aunque nadie haya pasado junto a ella
	// (#524). La primera de un mapa fijo está lejos de la salida: hasta alcanzarla se reaparece en la salida.
	if (Generator)
	{
		ReachedOrder = FMath::Max(ReachedOrder, 0);
	}

	const bool bStorm = Storm && Storm->IsStormActive();
	const float MinProgress = bStorm ? Storm->GetFrontProgress() + StormRespawnMargin : -TNumericLimits<float>::Max();
	const int32 Slot = GetPlayerSlot(PlayerController);

	// La pila alcanzada más lejana que siga por delante de la tormenta.
	TArray<ATN_ProcEggNest*> Nests;
	if (Generator)
	{
		for (const TWeakObjectPtr<ATN_ProcEggNest>& WeakNest : Generator->GetEggNests())
		{
			if (ATN_ProcEggNest* Nest = WeakNest.Get())
			{
				Nests.Add(Nest);
			}
		}
	}
	else
	{
		ATN_ProcEggNest::GatherWorldNests(GetWorld(), Nests);
	}
	if (const ATN_ProcEggNest* Best = ATN_ProcEggNest::PickRespawnNest(Nests, ReachedOrder, MinProgress))
	{
		OutTransform = Best->GetRespawnTransform(Slot);
		return true;
	}

	// Sin pila: la salida, mientras la tormenta no la haya alcanzado.
	if (MinProgress > 0.f)
	{
		return false;
	}
	if (Generator)
	{
		OutTransform = Generator->GetStartTransform(Slot);
		return true;
	}
	return TNProcMapGameModeDetail::FindLevelStartTransform(GetWorld(), OutTransform);
}

void ATN_ProcMapGameMode::BeginRespawn(APlayerController* PlayerController, ATortugaCharacter* Turtle)
{
	ReleaseCarry(Turtle);
	if (Turtle->IsKnockedDown())
	{
		Turtle->RecoverFromKnockdownSilently();
	}
	if (UTN_ShellComponent* Shell = Turtle->GetShellComponent())
	{
		Shell->ForceExitShell();
	}
	if (ATN_CoopPlayerState* PS = PlayerController->GetPlayerState<ATN_CoopPlayerState>())
	{
		PS->bIsDBNO = false;
		PS->DBNOBleedoutTimeRemaining = -1.f;
		PS->DeathZoneTimeRemaining = -1.f;
	}
	DBNOPlayers.Remove(PlayerController);

	if (UCharacterMovementComponent* Move = Turtle->GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}
	Turtle->SetActorHiddenInGame(true);
	Turtle->SetActorEnableCollision(false);
	PlayerController->ClientIgnoreMoveInput(true);

	FTimerHandle& Handle = PendingRespawns.FindOrAdd(PlayerController);
	GetWorldTimerManager().SetTimer(Handle,
		FTimerDelegate::CreateUObject(this, &ATN_ProcMapGameMode::FinishRespawn, TWeakObjectPtr<APlayerController>(PlayerController)),
		FMath::Max(0.05f, RespawnDelaySeconds), false);

	UE_LOG(LogTortunabo, Log, TEXT("[ProcMapGameMode] %s cae: reaparece en %.1fs."), *GetNameSafe(PlayerController), RespawnDelaySeconds);
}

void ATN_ProcMapGameMode::FinishRespawn(TWeakObjectPtr<APlayerController> WeakPC)
{
	PendingRespawns.Remove(WeakPC);
	APlayerController* PC = WeakPC.Get();
	if (!PC || !bRoundActive)
	{
		return;
	}
	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PC->GetPawn());
	ATN_CoopPlayerState* PS = PC->GetPlayerState<ATN_CoopPlayerState>();
	if (!Turtle || !PS)
	{
		return;
	}

	// Se recalcula: la tormenta ha podido pasar la pila durante la espera.
	FTransform At;
	if (!FindRespawnTransform(PC, At))
	{
		Turtle->SetActorHiddenInGame(false);
		Turtle->SetActorEnableCollision(true);
		PC->ClientIgnoreMoveInput(false);
		UE_LOG(LogTortunabo, Log, TEXT("[ProcMapGameMode] %s: la tormenta ya pasó su pila → eliminado."), *GetNameSafe(PC));
		Super::MarkPlayerDead(PC);
		return;
	}

	// Primero a la pila y después visible y con colisión: con la colisión puesta donde cayó, el volumen de muerte
	// la volvía a matar y el teletransporte la soltaba invisible y sin colisión (#519).
	GrantReviveImmunity(PC);
	Turtle->SetActorLocationAndRotation(At.GetLocation(), At.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	Turtle->SetActorHiddenInGame(false);
	Turtle->SetActorEnableCollision(true);
	PC->ClientIgnoreMoveInput(false);
	// Vuelve con la stamina entera y sin agotamiento.
	if (UTN_StaminaComponent* Stamina = Turtle->GetStaminaComponent())
	{
		Stamina->RestoreStaminaToFull();
	}
	if (UCharacterMovementComponent* Move = Turtle->GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->SetMovementMode(MOVE_Falling);
	}
	PC->ClientSetRotation(At.Rotator(), true);
	PS->DeathZoneTimeRemaining = -1.f;
}

ETNLateJoinPolicy ATN_ProcMapGameMode::GetLateJoinPolicy() const
{
	return Mode == ETNProcGameMode::Coop ? ETNLateJoinPolicy::ResumeOnPath : ETNLateJoinPolicy::SpectateUntilNextRound;
}

bool ATN_ProcMapGameMode::PlaceMidMatchJoiner(APlayerController* PlayerController)
{
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	FTransform At;
	if (!Pawn || !FindRespawnTransform(PlayerController, At))
	{
		return false;
	}

	// Como una reaparición en la pila: sale ya colocado, con la inmunidad breve y cayendo (sin quedarse sin movimiento).
	Pawn->SetActorLocationAndRotation(At.GetLocation(), At.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	if (UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr)
	{
		Move->StopMovementImmediately();
		Move->SetMovementMode(MOVE_Falling);
	}
	PlayerController->ClientSetRotation(At.Rotator(), true);
	GrantReviveImmunity(PlayerController);
	UE_LOG(LogTortunabo, Log, TEXT("[Join] %s entra a mitad de ronda en %s (pila del equipo %d)."),
		*GetNameSafe(PlayerController), *At.GetLocation().ToCompactString(), TeamBestNest);
	return true;
}

void ATN_ProcMapGameMode::ReleaseCarry(ATortugaCharacter* Turtle) const
{
	UTN_CarryComponent* Carry = Turtle ? Turtle->GetCarryComponent() : nullptr;
	if (!Carry)
	{
		return;
	}
	if (Carry->IsCarrying())
	{
		Carry->ForceRelease(false);
	}
	if (ATortugaCharacter* Carrier = Carry->GetCarrier())
	{
		if (UTN_CarryComponent* CarrierCarry = Carrier->GetCarryComponent())
		{
			CarrierCarry->ForceRelease(false);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Fin de ronda y de partida
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcMapGameMode::UpdateRoundProgressAndMaybeFinish()
{
	if (bSuppressRoundCheck || !bRoundActive)
	{
		return;
	}

	// Última (o única) ronda del Coop: el flujo de siempre, resultados y lobby.
	if (Mode == ETNProcGameMode::Coop && CurrentRound >= CoopRounds)
	{
		Super::UpdateRoundProgressAndMaybeFinish();
		if (GetWorldTimerManager().IsTimerActive(ResultsTimerHandle))
		{
			bRoundActive = false;
			bMatchOver = true;
			GetWorldTimerManager().ClearTimer(RoundTimeLimitHandle);
			if (Storm)
			{
				Storm->StopStorm();
			}
			SyncGameState();
		}
		return;
	}

	int32 Total = 0;
	int32 Resolved = 0;
	int32 Alive = 0;
	TArray<APlayerController*> Finishers;
	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		const ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS);
		if (!PS)
		{
			continue;
		}
		++Total;
		Alive += PS->bIsAlive ? 1 : 0;
		Resolved += PS->bHasFinishedRun ? 1 : 0;
		if (PS->bHasFinishedRun && !PS->bIsEliminated)
		{
			if (APlayerController* PC = PS->GetPlayerController())
			{
				Finishers.Add(PC);
			}
		}
	}
	if (ATN_CoopGameState* GS = GetGameState<ATN_CoopGameState>())
	{
		GS->FinishedPlayers = Resolved;
		GS->ExpectedPlayers = Total;
	}
	if (Total == 0 || (Resolved < Total && Alive > 0))
	{
		return;
	}

	if (Mode == ETNProcGameMode::Coop)
	{
		EndRound(Finishers, FString::Printf(TEXT("Ronda superada: %d de %d en la meta"), Finishers.Num(), Total));
	}
	else
	{
		EndRound({}, TEXT("Nadie gana la ronda"));
	}
}

void ATN_ProcMapGameMode::OnRoundTimeLimit()
{
	if (!bRoundActive)
	{
		return;
	}

	if (Mode == ETNProcGameMode::Race)
	{
		APlayerController* Best = nullptr;
		float BestProgress = -1.f;
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			const float Progress = GetPlayerProgress(PC);
			if (PC && Progress > BestProgress)
			{
				BestProgress = Progress;
				Best = PC;
			}
		}
		TArray<APlayerController*> Winners;
		if (Best)
		{
			Winners.Add(Best);
		}
		EndRound(Winners, FString::Printf(TEXT("Tiempo: gana %s, el más adelantado"), *DescribePlayers(Winners)));
		return;
	}

	if (Mode == ETNProcGameMode::TwoVsTwo)
	{
		float TeamProgress[2] = { 0.f, 0.f };
		for (int32 Team = 0; Team < 2; ++Team)
		{
			for (const APlayerController* Member : GetTeamMembers(Team))
			{
				TeamProgress[Team] += GetPlayerProgress(Member);
			}
		}
		const int32 WinnerTeam = TeamProgress[1] > TeamProgress[0] ? 1 : 0;
		const TArray<APlayerController*> Winners = GetTeamMembers(WinnerTeam);
		EndRound(Winners, FString::Printf(TEXT("Tiempo: gana la pareja %s"), *DescribePlayers(Winners)));
		return;
	}

	EndRound({}, TEXT("Tiempo agotado"));
}

void ATN_ProcMapGameMode::EndRound(const TArray<APlayerController*>& Winners, const FString& ResultText)
{
	if (!bRoundActive)
	{
		return;
	}
	bRoundActive = false;
	GetWorldTimerManager().ClearTimer(RoundTimeLimitHandle);
	for (TPair<TWeakObjectPtr<APlayerController>, FTimerHandle>& Pending : PendingRespawns)
	{
		GetWorldTimerManager().ClearTimer(Pending.Value);
	}
	PendingRespawns.Reset();
	if (Storm)
	{
		Storm->StopStorm();
	}

	for (APlayerController* Winner : Winners)
	{
		if (ATN_CoopPlayerState* PS = Winner ? Winner->GetPlayerState<ATN_CoopPlayerState>() : nullptr)
		{
			++PS->RoundWins;
		}
	}
	if (ATN_ProcMapGameState* GS = GetProcGameState())
	{
		GS->RoundResultText = ResultText;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[ProcMapGameMode] Ronda %d terminada: %s"), CurrentRound, *ResultText);

	bool bMatchDone = false;
	if (Mode == ETNProcGameMode::Coop)
	{
		bMatchDone = CurrentRound >= CoopRounds;
	}
	else
	{
		for (APlayerState* BasePS : GameState->PlayerArray)
		{
			const ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS);
			bMatchDone |= PS && PS->RoundWins >= WinsToWinMatch;
		}
	}
	if (bMatchDone)
	{
		EnterFinalResults();
		return;
	}

	// Entre rondas: cuenta atrás con el resultado a la vista y mapa nuevo al acabar.
	SetFlowState(ETNMatchFlowState::Countdown);
	ResultsCountdownValue = FMath::CeilToInt(BetweenRoundsSeconds);
	if (ATN_CoopGameState* GS = GetGameState<ATN_CoopGameState>())
	{
		GS->CountdownValue = ResultsCountdownValue;
	}
	GetWorldTimerManager().SetTimer(ResultsCountdownTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]() { TickResultsCountdown(); }), 1.f, true);
	GetWorldTimerManager().SetTimer(BetweenRoundsHandle, this, &ATN_ProcMapGameMode::StartNextRound, BetweenRoundsSeconds, false);
	SyncGameState();
}

void ATN_ProcMapGameMode::StartNextRound()
{
	GetWorldTimerManager().ClearTimer(ResultsCountdownTimerHandle);
	GetWorldTimerManager().ClearTimer(StartStructureOpenHandle);
	++CurrentRound;
	CleanupRoundActors();

	// Ronda nueva sobre el mismo mapa: la salida vuelve a cerrarse (si se regenera, el generador pone otra).
	if (ATN_ProcStartStructure* Structure = Generator ? Generator->GetStartStructure() : nullptr)
	{
		Structure->Close();
	}

	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		if (ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS))
		{
			PS->ResetForNewRace();
		}
	}
	// Quien miraba la ronda anterior por entrar a mitad juega esta (PlacePlayersAtStart le da pawn).
	SitOutPlayerIds.Reset();
	NextFinishRank = 1;
	if (ATN_CoopGameState* GS = GetGameState<ATN_CoopGameState>())
	{
		GS->RaceResults.Reset();
		GS->FinishedPlayers = 0;
		GS->CountdownValue = 0;
		GS->OnRaceResultsUpdated.Broadcast();
	}
	SetFlowState(ETNMatchFlowState::WaitingForPlayers);

	if (bRegenerateEachRound || !Generator || !Generator->IsMapReady())
	{
		GenerateRoundMap();
	}
	else
	{
		bWaitingForMap = true;
		WaitStartTime = GetWorld()->GetTimeSeconds();
		GetWorldTimerManager().SetTimer(MapReadyPollHandle, this, &ATN_ProcMapGameMode::PollMapReady, 0.25f, true);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[ProcMapGameMode] Preparando la ronda %d."), CurrentRound);
	SyncGameState();
}

void ATN_ProcMapGameMode::CleanupRoundActors()
{
	for (TPair<TWeakObjectPtr<APlayerController>, FTimerHandle>& Pending : PendingRespawns)
	{
		GetWorldTimerManager().ClearTimer(Pending.Value);
	}
	PendingRespawns.Reset();

	for (TPair<int32, TWeakObjectPtr<ATN_RescuePickup>>& Pickup : RescuePickups)
	{
		if (Pickup.Value.IsValid())
		{
			Pickup.Value->Destroy();
		}
	}
	RescuePickups.Reset();

	for (TPair<int32, TWeakObjectPtr<APawn>>& DeadPawn : DeadPlayerPawns)
	{
		if (DeadPawn.Value.IsValid())
		{
			DeadPawn.Value->Destroy();
		}
	}
	DeadPlayerPawns.Reset();
	DBNOPlayers.Reset();
	GetWorldTimerManager().ClearTimer(DBNOBleedoutTimerHandle);

	// Fuera todos los pawns: la ronda nueva los crea en la salida del mapa nuevo.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (APawn* Pawn = PC ? PC->GetPawn() : nullptr)
		{
			PC->UnPossess();
			Pawn->Destroy();
		}
	}
	for (TActorIterator<ATortugaCharacter> It(GetWorld()); It; ++It)
	{
		if (IsValid(*It) && !It->IsActorBeingDestroyed())
		{
			It->Destroy();
		}
	}
}

void ATN_ProcMapGameMode::EnterFinalResults()
{
	bMatchOver = true;
	bRoundActive = false;
	GetWorldTimerManager().ClearTimer(RoundTimeLimitHandle);
	if (Storm)
	{
		Storm->StopStorm();
	}

	// Carrera y 2vs2: la tabla final es la de rondas ganadas (el widget de
	// resultados de siempre la muestra con el puesto y los puntos).
	if (Mode != ETNProcGameMode::Coop)
	{
		if (ATN_CoopGameState* GS = GetGameState<ATN_CoopGameState>())
		{
			TArray<const ATN_CoopPlayerState*> Standings;
			for (APlayerState* BasePS : GameState->PlayerArray)
			{
				if (const ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS))
				{
					Standings.Add(PS);
				}
			}
			Standings.Sort([](const ATN_CoopPlayerState& A, const ATN_CoopPlayerState& B) { return A.RoundWins > B.RoundWins; });
			GS->RaceResults.Reset();
			for (int32 Index = 0; Index < Standings.Num(); ++Index)
			{
				GS->Server_UpsertRaceResult(Standings[Index]->GetPlayerId(), Standings[Index]->GetPlayerName(),
					Index + 1, 0.f, Standings[Index]->RoundWins, false);
			}
		}
	}

	SetFlowState(ETNMatchFlowState::Results);
	ResultsCountdownValue = FMath::CeilToInt(ResultsDurationSeconds);
	if (ATN_CoopGameState* GS = GetGameState<ATN_CoopGameState>())
	{
		GS->CountdownValue = ResultsCountdownValue;
	}
	GetWorldTimerManager().SetTimer(ResultsCountdownTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]() { TickResultsCountdown(); }), 1.f, true);
	GetWorldTimerManager().SetTimer(ResultsTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]() { FinishRoundAndReturnToLobby(); }), ResultsDurationSeconds, false);
	UE_LOG(LogTortunabo, Log, TEXT("[ProcMapGameMode] ═══ Partida terminada tras %d ronda(s) ═══"), CurrentRound);
	SyncGameState();
}

void ATN_ProcMapGameMode::Logout(AController* Exiting)
{
	if (APlayerController* PC = Cast<APlayerController>(Exiting))
	{
		if (FTimerHandle* Pending = PendingRespawns.Find(PC))
		{
			GetWorldTimerManager().ClearTimer(*Pending);
			PendingRespawns.Remove(PC);
		}
		ClientReadyGeneration.Remove(PC);
		FrozenControllers.Remove(PC);
		ReleaseCarry(Cast<ATortugaCharacter>(PC->GetPawn()));
	}
	// Si en 2vs2 se va alguien, la ronda sigue con las parejas que queden y la
	// siguiente se juega como Carrera (BeginRoundPlay lo comprueba).
	Super::Logout(Exiting);
}

// ─────────────────────────────────────────────────────────────────────────────
// Utilidades
// ─────────────────────────────────────────────────────────────────────────────

AActor* ATN_ProcMapGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	using namespace TNProcMapGameModeDetail;
	// Con estructura de salida, cada jugador aparece dentro de ella: en el sitio de su slot (la sala de la puerta doble
	// o su huevo) o, si está ocupado, en el siguiente libre. Durante el viaje sin cortes los slots aún se reordenan;
	// PlacePlayersAtStart deja a cada uno en el suyo al empezar la ronda.
	if (Generator && Generator->GetStartStructure())
	{
		const int32 Slot = GetPlayerSlot(Player);
		for (int32 k = 0; k < ATN_ProcStartStructure::NumSpots; ++k)
		{
			APlayerStart* Candidate = Generator->GetStartPlayerStart((Slot + k) % ATN_ProcStartStructure::NumSpots);
			if (!Candidate)
			{
				continue;
			}
			bool bTaken = false;
			for (TActorIterator<APawn> It(GetWorld()); It && !bTaken; ++It)
			{
				bTaken = It->Controller != Player && FVector::Dist(It->GetActorLocation(), Candidate->GetActorLocation()) < StructureStartTakenRadius;
			}
			if (!bTaken)
			{
				return Candidate;
			}
		}
	}

	// Los PlayerStart del generador (etiqueta TNProcStart) mandan sobre cualquier
	// otro del nivel, incluido el de respaldo que crea la base.
	TArray<AActor*> ProcStarts;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		if (It->PlayerStartTag == FName(TEXT("TNProcStart")))
		{
			ProcStarts.Add(*It);
		}
	}
	if (ProcStarts.Num() > 0)
	{
		if (AActor* Start = TN_PickUnoccupiedPlayerStart(GetWorld(), ProcStarts, Player))
		{
			return Start;
		}
		return ProcStarts[FMath::RandHelper(ProcStarts.Num())];
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}

ATN_ProcMapGameState* ATN_ProcMapGameMode::GetProcGameState() const
{
	return GetGameState<ATN_ProcMapGameState>();
}

void ATN_ProcMapGameMode::SyncGameState() const
{
	ATN_ProcMapGameState* GS = GetProcGameState();
	if (!GS)
	{
		return;
	}
	GS->ProcMode = Mode;
	GS->ProcDifficulty = Difficulty;
	GS->CurrentRound = CurrentRound;
	GS->RoundTarget = Mode == ETNProcGameMode::Coop ? CoopRounds : WinsToWinMatch;
	GS->bRoundInProgress = bRoundActive;
	if (Generator && Generator->IsMapReady())
	{
		GS->MapSeed = Generator->GetNetConfig().Seed;
		GS->EstimatedMinutes = Generator->EstimateTraversalMinutes();
	}
	GS->NotifyRoundInfoChanged();
}

int32 ATN_ProcMapGameMode::GetPlayerSlot(const AController* Controller) const
{
	if (!Controller || !GameState)
	{
		return 0;
	}
	const int32 Index = GameState->PlayerArray.IndexOfByKey(Controller->PlayerState);
	return FMath::Max(0, Index);
}

float ATN_ProcMapGameMode::GetPlayerProgress(const APlayerController* PlayerController) const
{
	if (!PlayerController || !Generator)
	{
		return 0.f;
	}
	const ATN_CoopPlayerState* PS = PlayerController->GetPlayerState<ATN_CoopPlayerState>();
	if (PS && PS->bHasFinishedRun && !PS->bIsEliminated)
	{
		return Generator->GetMainPathLength() + 1.f;
	}
	const APawn* Pawn = PlayerController->GetPawn();
	return Pawn ? Generator->GetPathProgress(Pawn->GetActorLocation()) : 0.f;
}

TArray<APlayerController*> ATN_ProcMapGameMode::GetTeamMembers(int32 Team) const
{
	TArray<APlayerController*> Members;
	if (Team < 0 || !GameState)
	{
		return Members;
	}
	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		const ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS);
		APlayerController* PC = PS ? PS->GetPlayerController() : nullptr;
		if (PC && PS->TeamIndex == Team)
		{
			Members.Add(PC);
		}
	}
	return Members;
}

int32 ATN_ProcMapGameMode::CountConnectedPlayers() const
{
	return TN_CountConnectedCoopPlayers(GameState);
}

FString ATN_ProcMapGameMode::DescribePlayers(const TArray<APlayerController*>& Players)
{
	TArray<FString> Names;
	for (const APlayerController* PC : Players)
	{
		if (PC && PC->PlayerState)
		{
			Names.Add(PC->PlayerState->GetPlayerName());
		}
	}
	return Names.Num() > 0 ? FString::Join(Names, TEXT(" y ")) : FString(TEXT("nadie"));
}
