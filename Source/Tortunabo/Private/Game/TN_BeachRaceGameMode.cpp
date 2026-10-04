#include "Game/TN_BeachRaceGameMode.h"
#include "Game/TN_BeachRaceDecisions.h"
#include "Game/TN_BeachRaceGameState.h"
#include "Game/TN_BeachRoundSyncComponent.h"
#include "Game/TN_UnderTerrainGuard.h"
#include "Core/TN_Log.h"
#include "Core/TN_CoopGameState.h"
#include "Core/TN_CoopPlayerState.h"
#include "Lobby/TN_LobbyMission.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Player/MP_GamePlayerController.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_ShellBody.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/Beach/TN_BeachSandWorm.h"
#include "World/Beach/TN_BeachStorm.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/Beach/TN_BeachStunComponent.h"
#include "World/Beach/TN_BeachTypes.h"
#include "World/Beach/TN_RaceItemComponent.h"
#include "World/TN_ConchPickup.h"
#include "World/TN_DeathZoneVolume.h"
#include "World/TN_PickupInteractableBase.h"
#include "World/TN_StormVolume.h"
#include "World/TN_ThrowableItemActor.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace TNBeachRaceGameModeDetail
{
	/** Junta en Out los actores de TActorClass que quedan sueltos y no son de ninguna ronda (TNBeachRaceRules::ShouldClearRoundLeftover). */
	template <typename TActorClass>
	void GatherRoundLeftovers(UWorld* World, TArray<AActor*>& Out)
	{
		for (TActorIterator<TActorClass> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (TNBeachRaceRules::ShouldClearRoundLeftover(Actor->IsNetStartupActor(), !IsValid(Actor) || Actor->IsActorBeingDestroyed()))
			{
				Out.Add(Actor);
			}
		}
	}

	/**
	 * CountdownValue durante la pantalla del campeón (no hay cuenta: se espera al anfitrión). Por encima de 1 para que
	 * la música de fin de partida no se funda (el director funde en el último segundo) ni se cierre el huevo de la
	 * pantalla de carga (se cierra con CountdownValue == 1 en Results). Al elegir se pone a 1 y ahí sí se funden.
	 */
	constexpr int32 ChampionHoldCountdown = 99;

	/** Velocidad media de la carrera (cm/s) para estimar los minutos de la ronda (Docs/Modo_Carrera.md: ~4 m/s). */
	constexpr double AverageRaceSpeed = 400.0;

	/** Cada cuánto se mira la meta, el vacío y los sitios seguros durante la carrera. */
	constexpr float WatchInterval = 0.1f;

	/** Sitios seguros que se recuerdan por tortuga (con SafeSpotSampleSeconds = 0,5: los últimos 6 s). */
	constexpr int32 MaxSafeSpots = 12;

	TAutoConsoleVariable<int32> CVarSafetyNet(TEXT("TN.Race.SafetyNet"), 1,
		TEXT("Carrera en la playa (servidor): 1 = red de seguridad: una tortuga bajo la arena o cayendo sin suelo vuelve encima en una bola corta, con aviso «[Carrera] Red de seguridad» en el registro; 0 = apagada (para comparar)."));

	/** A menos de esto (cm) del filo del acantilado no se mira la red: la pared está socavada y ahí se cae al agua de meta. */
	constexpr double CliffSkipDistance = 1000.0;

	/** Margen de más (cm) en las trincheras (el canal cavado y sus caballones). */
	constexpr double TrenchExtraMargin = 100.0;

	/** Rescates de la red de seguridad en SafetyNetRepeatSeconds a partir de los que se avisa de un bucle (en el registro). */
	constexpr int32 RescueLoopWarnCount = 4;

	/** Hasta dónde (cm) se busca arena abierta alrededor de su último sitio seguro (nivel 1) y lejos de los hoyos (nivel 2). */
	constexpr float RescueNearSearch = 400.f;
	constexpr float RescueFarSearch = 3000.f;

	/** El estado de la tortuga para el registro de la red de seguridad. */
	FString DescribeTurtle(const ATortugaCharacter& Turtle)
	{
		TArray<FString> Parts;
		const UCharacterMovementComponent* Move = Turtle.GetCharacterMovement();
		Parts.Add(Move ? Move->GetMovementName() : FString(TEXT("sin movimiento")));
		if (const UTN_ShellComponent* Shell = Turtle.GetShellComponent(); Shell && Shell->IsInShell())
		{
			Parts.Add(Shell->HasLocalBody() ? TEXT("en bola con caja física") : TEXT("en el caparazón sin caja"));
		}
		if (Turtle.IsKnockedDown())
		{
			const USkeletalMeshComponent* Mesh = Turtle.GetMesh();
			Parts.Add(Mesh && Mesh->IsSimulatingPhysics() ? TEXT("derribada en ragdoll") : TEXT("derribada"));
		}
		if (TNBeach::IsTurtleStunned(&Turtle))
		{
			Parts.Add(TEXT("aturdida"));
		}
		if (Turtle.IsDiving())
		{
			const UTN_TurtleMovementComponent* TurtleMove = Turtle.GetTurtleMovement();
			Parts.Add(TurtleMove && TurtleMove->IsOnBelly() ? TEXT("panzazo, sobre la tripa") : TEXT("panzazo"));
		}
		if (ATN_BeachEnemy::IsTurtleHeld(&Turtle))
		{
			Parts.Add(TEXT("sujeta"));
		}
		if (const UTN_CarryComponent* Carry = Turtle.GetCarryComponent())
		{
			if (Carry->IsBeingCarried())
			{
				Parts.Add(TEXT("en brazos de otra"));
			}
			if (Carry->IsCarrying())
			{
				Parts.Add(TEXT("llevando a otra"));
			}
		}
		if (Turtle.IsFallImmune())
		{
			Parts.Add(TEXT("caída inmune (lanzada)"));
		}
		const UPrimitiveComponent* Base = Turtle.GetMovementBase();
		Parts.Add(FString::Printf(TEXT("base %s%s"), Base ? *Base->GetName() : TEXT("ninguna"),
			Base && Base->GetOwner() ? *FString::Printf(TEXT(" de %s"), *Base->GetOwner()->GetName()) : TEXT("")));
		const UCapsuleComponent* Capsule = Turtle.GetCapsuleComponent();
		if (Capsule)
		{
			Parts.Add(FString::Printf(TEXT("cápsula %.0f cm%s"), Capsule->GetUnscaledCapsuleHalfHeight(),
				Capsule->GetCollisionEnabled() == ECollisionEnabled::NoCollision ? TEXT(" sin colisión") : TEXT("")));
		}
		return FString::Join(Parts, TEXT(", "));
	}

	ATN_BeachRaceGameMode* FindGameMode(const UWorld* World)
	{
		return World ? World->GetAuthGameMode<ATN_BeachRaceGameMode>() : nullptr;
	}

	int32 IntArg(const TArray<FString>& Args, int32 Index, int32 Default)
	{
		return Args.IsValidIndex(Index) ? FCString::Atoi(*Args[Index]) : Default;
	}

	float FloatArg(const TArray<FString>& Args, int32 Index, float Default)
	{
		return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : Default;
	}

	/** Consola: corre Action con el GameMode de la playa del anfitrión o avisa de que aquí no hay carrera. */
	void WithGameMode(const UWorld* World, const TCHAR* Command, TFunctionRef<void(ATN_BeachRaceGameMode&)> Action)
	{
		if (ATN_BeachRaceGameMode* GM = FindGameMode(World))
		{
			Action(*GM);
			return;
		}
		UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] %s: solo en el anfitrión y en la playa (LVL_BeachRace)."), Command);
	}

	FAutoConsoleCommandWithWorldAndArgs CmdWinRound(TEXT("TN.Race.WinRound"),
		TEXT("Carrera en la playa: el jugador N (0 = el primero, normalmente el anfitrión) toca el agua como si llegara: la primera gana la ronda y arranca la cuenta atrás de 10 s; las siguientes, media concha. TN.Race.WinRound [jugador = 0] [puesto]: con puesto (1-8), su pantalla «Has quedado X.º» enseña ese puesto y su premio."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			WithGameMode(World, TEXT("TN.Race.WinRound"), [&Args](ATN_BeachRaceGameMode& GM) { GM.DebugWinRound(IntArg(Args, 0, 0), IntArg(Args, 1, 0)); });
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdNextRound(TEXT("TN.Race.NextRound"),
		TEXT("Carrera en la playa (anfitrión): salta a la ronda siguiente. En plena carrera la cierra ya con el recuento; en el recuento o en el título del sprint, sigue sin esperar (huevo negro y «RONDA N»)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			WithGameMode(World, TEXT("TN.Race.NextRound"), [](ATN_BeachRaceGameMode& GM) { GM.DebugNextRound(); });
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdChampion(TEXT("TN.Race.Champion"),
		TEXT("Carrera en la playa: el jugador N llega a las conchas del campeón y se pasa directamente a su pantalla."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			WithGameMode(World, TEXT("TN.Race.Champion"), [&Args](ATN_BeachRaceGameMode& GM) { GM.DebugChampion(IntArg(Args, 0, 0)); });
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdSprint(TEXT("TN.Race.Sprint"),
		TEXT("Carrera en la playa: empate forzado a tres conchas y sprint final. TN.Race.Sprint [jugador] [jugador]... (por defecto 0 y 1; con uno solo también, para probar)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			WithGameMode(World, TEXT("TN.Race.Sprint"), [&Args](ATN_BeachRaceGameMode& GM)
			{
				TArray<int32> Indices;
				for (const FString& Arg : Args)
				{
					Indices.AddUnique(FCString::Atoi(*Arg));
				}
				if (Indices.Num() == 0)
				{
					Indices = { 0, 1 };
				}
				GM.DebugSprint(Indices);
			});
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdStun(TEXT("TN.Race.Stun"),
		TEXT("Carrera en la playa: aturde al jugador. TN.Race.Stun [segundos = 3] [jugador = 0]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			WithGameMode(World, TEXT("TN.Race.Stun"), [&Args](ATN_BeachRaceGameMode& GM) { GM.DebugStun(IntArg(Args, 1, 0), FloatArg(Args, 0, 3.f)); });
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdKill(TEXT("TN.Race.Kill"),
		TEXT("Carrera en la playa: pasa al jugador N por la ruta de muerte (aquí aturde; en una zona de muerte, vuelve a un sitio seguro)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			WithGameMode(World, TEXT("TN.Race.Kill"), [&Args](ATN_BeachRaceGameMode& GM) { GM.DebugKill(IntArg(Args, 0, 0)); });
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdVoid(TEXT("TN.Race.Void"),
		TEXT("Carrera en la playa: tira al jugador N al vacío (vuelve a su último sitio seguro, aturdido)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			WithGameMode(World, TEXT("TN.Race.Void"), [&Args](ATN_BeachRaceGameMode& GM) { GM.DebugVoid(IntArg(Args, 0, 0)); });
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdBury(TEXT("TN.Race.Bury"),
		TEXT("Carrera en la playa: mete a la tortuga bajo la arena donde está para probar la red de seguridad (vuelve encima en una bola corta, con aviso en el registro). TN.Race.Bury [metros = 3] [jugador = 0]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			WithGameMode(World, TEXT("TN.Race.Bury"), [&Args](ATN_BeachRaceGameMode& GM) { GM.DebugBury(IntArg(Args, 1, 0), FloatArg(Args, 0, 3.f)); });
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdTimeLeft(TEXT("TN.Race.TimeLeft"),
		TEXT("Carrera en la playa (anfitrión): deja N segundos de tiempo de ronda si nadie ha llegado al agua (reloj del último minuto, avisos a los 60 y 30 s y «¡TIEMPO!»). TN.Race.TimeLeft [segundos = 65]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			WithGameMode(World, TEXT("TN.Race.TimeLeft"), [&Args](ATN_BeachRaceGameMode& GM) { GM.DebugSetRoundTimeLeft(FloatArg(Args, 0, 65.f)); });
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdPlayAgain(TEXT("TN.Race.PlayAgain"),
		TEXT("Carrera en la playa, pantalla del campeón: Volver a jugar."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ATN_BeachRaceGameMode::RequestChampionChoice(World, ETNBeachChampionChoice::PlayAgain);
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdChangeMode(TEXT("TN.Race.ChangeMode"),
		TEXT("Carrera en la playa, pantalla del campeón: Cambiar de modo (vuelve al lobby con el cooperativo elegido)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ATN_BeachRaceGameMode::RequestChampionChoice(World, ETNBeachChampionChoice::ChangeMode);
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdMenu(TEXT("TN.Race.Menu"),
		TEXT("Carrera en la playa, pantalla del campeón: Salir al menú principal."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ATN_BeachRaceGameMode::RequestChampionChoice(World, ETNBeachChampionChoice::Quit);
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdMode(TEXT("TN.Mode"),
		TEXT("Modo de la próxima partida que salga del lobby (en el anfitrión): TN.Mode Coop|Race. Sin argumento, dice el actual."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UMP_GameInstance* GI = World ? Cast<UMP_GameInstance>(World->GetGameInstance()) : nullptr;
			if (!GI)
			{
				return;
			}
			// Por TNLobbyMission, como el general: en el lobby se ven al momento su pizarra y los selectores.
			if (Args.Num() > 0)
			{
				if (Args[0].Equals(TEXT("Coop"), ESearchCase::IgnoreCase) || Args[0].Equals(TEXT("Cooperativo"), ESearchCase::IgnoreCase))
				{
					TNLobbyMission::SetMode(World, ETNProcGameMode::Coop);
				}
				else if (Args[0].Equals(TEXT("Race"), ESearchCase::IgnoreCase) || Args[0].Equals(TEXT("Carrera"), ESearchCase::IgnoreCase))
				{
					TNLobbyMission::SetMode(World, ETNProcGameMode::Race);
				}
			}
			UE_LOG(LogTortunabo, Log, TEXT("[Modo] Próxima partida: %s"), *UEnum::GetValueAsString(GI->SelectedProcMode));
		}));
}

ATN_BeachRaceGameMode::ATN_BeachRaceGameMode()
{
	GameStateClass = ATN_BeachRaceGameState::StaticClass();
	GeneratorClass = ATN_BeachRaceGenerator::StaticClass();
	StormClass = ATN_BeachStorm::StaticClass();
	// La red de seguridad bajo el terreno de ATN_RunGameMode (#633) no: la playa tiene la suya, que conoce la arena del
	// generador, las pozas y el acantilado (GuardUnderSand).
	if (UnderTerrainGuard)
	{
		UnderTerrainGuard->SetGuardEnabled(false);
	}

	// Los mismos Blueprints que el mapa procedural: la clase C++ sirve tal cual como GameMode de LVL_BeachRace.
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
	// Sin RescuePickupClass: en la playa nadie muere, así que no hay rescates.
}

// ─────────────────────────────────────────────────────────────────────────────
// Arranque
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachRaceGameMode::BeginPlay()
{
	ResolveUrlOptions();
	EnsureGenerator();

	// La primera ronda se reparte antes de que la base cree a nadie: así la salida ya tiene sus sitios cuando los
	// jugadores llegan del lobby. La cuenta atrás de esta ronda es el huevo de la pantalla de carga.
	CurrentRound = 1;
	bShowPreRaceCountdown = false;
	PrepareRound(false);

	Super::BeginPlay();

	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Playa · gana quien llegue a %d conchas · generador %s"), WinsToWinMatch, *GetNameSafe(Generator));
	SyncGameState();
}

void ATN_BeachRaceGameMode::ResolveUrlOptions()
{
	// Para probar sin lobby: open LVL_BeachRace?BeachSeed=42?BeachWins=1
	const FString SeedOption = UGameplayStatics::ParseOption(OptionsString, TEXT("BeachSeed"));
	UrlSeed = SeedOption.IsEmpty() ? 0 : FCString::Atoi(*SeedOption);
	const FString WinsOption = UGameplayStatics::ParseOption(OptionsString, TEXT("BeachWins"));
	if (!WinsOption.IsEmpty())
	{
		WinsToWinMatch = FMath::Max(1, FCString::Atoi(*WinsOption));
	}
}

void ATN_BeachRaceGameMode::EnsureGenerator()
{
	if (!Generator)
	{
		for (TActorIterator<ATN_BeachRaceGenerator> It(GetWorld()); It; ++It)
		{
			Generator = *It;
			break;
		}
	}
	if (!Generator)
	{
		UClass* Class = GeneratorClass ? GeneratorClass.Get() : ATN_BeachRaceGenerator::StaticClass();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Generator = GetWorld()->SpawnActor<ATN_BeachRaceGenerator>(Class, FTransform::Identity, Params);
		UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] El nivel no tenía generador de la playa: creado %s."), *GetNameSafe(Generator));
	}
}

void ATN_BeachRaceGameMode::OnWaitingTimeout()
{
	// La base avisa cuando han llegado todos del lobby (o venció la espera). La carrera arranca cuando además la ronda
	// está repartida y las tortugas colocadas en la salida.
	if (bMatchStarted)
	{
		return;
	}
	bPlayersArrived = true;
	GetWorldTimerManager().ClearTimer(WaitingTimeoutTimerHandle);
	PollRoundReady();
}

void ATN_BeachRaceGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	if (!NewPlayer)
	{
		return;
	}
	// El aviso de «ronda montada» de este cliente (UTN_BeachRoundSyncComponent), cuanto antes para que ya le haya llegado.
	UTN_BeachRoundSyncComponent::FindOrAddOn(NewPlayer);
	if (bSprint)
	{
		// En el sprint final solo corren las finalistas: quien entra ahora lo mira.
		SendOutOfSprint(NewPlayer);
		return;
	}
	if (bRoundActive)
	{
		// Llega con la ronda en marcha (reconexión): sale desde la salida, como todas.
		if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(NewPlayer->GetPawn()))
		{
			const float HalfHeight = Turtle->GetCapsuleComponent() ? Turtle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : GetDefaultHalfHeight();
			const FTransform Start = PutOnFloor(GetStartTransformFor(GetStartSlot(NewPlayer)), HalfHeight);
			TeleportTurtle(Turtle, Start);
			NewPlayer->ClientSetRotation(Start.Rotator(), true);
		}
	}
	else
	{
		FreezePlayers();
	}
}

void ATN_BeachRaceGameMode::Logout(AController* Exiting)
{
	if (APlayerController* PC = Cast<APlayerController>(Exiting))
	{
		FrozenControllers.Remove(PC);
		SafeSpots.Remove(PC);
		UnderSandWatch.Remove(PC);
		ReleaseCarry(Cast<ATortugaCharacter>(PC->GetPawn()));
	}
	// La base limpia lo suyo y, con la partida en marcha, mira si la ronda ha acabado (UpdateRoundProgressAndMaybeFinish:
	// si ya no queda nadie corriendo en la cuenta atrás, «¡TIEMPO!»). Si se va quien llegó primera, conserva su concha
	// mientras exista su PlayerState; el podio y el campeón, igual (luego, null).
	Super::Logout(Exiting);
	if (bRoundActive && !bSprint && Arrivals.Num() > 0 && AreAllRacersIn(Exiting))
	{
		// Se va la última que corría durante la cuenta atrás: ya no queda nadie por llegar.
		FinishTimeUp(ETNBeachRoundEnd::AllIn);
	}
	if ((bSprint || GetRacePhase() == ETNBeachRacePhase::SprintIntro) && Exiting)
	{
		SprintFinalists.RemoveAll([Exiting](const TWeakObjectPtr<APlayerController>& Weak) { return !Weak.IsValid() || Weak.Get() == Exiting; });
		CheckSprintForfeit();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Ronda: preparación, cuenta atrás y carrera
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachRaceGameMode::PrepareRound(bool bCleanup)
{
	CancelRoundTimers();
	bRoundActive = false;
	Arrivals.Reset();
	ResetRoundGameState();
	if (bCleanup)
	{
		CleanupRoundActors();
	}

	const int32 BaseSeed = UrlSeed != 0 ? UrlSeed : FixedSeed;
	if (BaseSeed != 0)
	{
		RoundSeed = BaseSeed + (CurrentRound - 1);
	}
	else
	{
		const uint64 Ticks = static_cast<uint64>(FDateTime::UtcNow().GetTicks());
		const uint32 Mixed = static_cast<uint32>(Ticks) ^ static_cast<uint32>(Ticks >> 32)
			^ (static_cast<uint32>(FMath::Rand()) << 8)
			^ (static_cast<uint32>(CurrentRound) * 0x9E3779B9u);
		RoundSeed = static_cast<int32>(Mixed & 0x7FFFFFFFu);
	}

	// El terreno es fijo: el generador solo vuelve a repartir decorado, trampas y enemigos.
	if (Generator)
	{
		// La dificultad del reparto, la que eligió el general (va replicada con la ronda: todas las máquinas reparten igual).
		// Sin misión de carrera (el nivel abierto a mano), la del generador (su propiedad o TN.Race.Difficulty).
		const UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance());
		if (GI && GI->SelectedProcMode == ETNProcGameMode::Race)
		{
			Generator->Difficulty = GI->SelectedProcDifficulty;
		}
		Generator->GenerateRound(RoundSeed);
		// El nido de huevos, en la salida de siempre o, en el sprint final, en la línea del sprint (vuelve solo a la salida
		// con la ronda siguiente).
		Generator->SetStartEggsAtSprint(bSprint);
	}
	else
	{
		UE_LOG(LogTortunabo, Error, TEXT("[Carrera] Sin generador: la ronda %d se corre sin reparto ni meta."), CurrentRound);
	}

	bPreparingRound = true;
	PrepStartTime = GetWorld()->GetTimeSeconds();
	ClientWaitStartTime = -1.f;
	UnderSandWatch.Reset();
	// Cada cliente dirá cuándo tiene montada esta ronda (UTN_BeachRoundSyncComponent): la salida lo espera.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		UTN_BeachRoundSyncComponent::FindOrAddOn(It->Get());
	}
	SetRacePhase(ETNBeachRacePhase::Waiting);
	GetWorldTimerManager().SetTimer(PrepPollHandle, this, &ATN_BeachRaceGameMode::PollRoundReady, 0.25f, true);
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Preparando la ronda %d (semilla %d)."), CurrentRound, RoundSeed);
	SyncGameState();
}

void ATN_BeachRaceGameMode::PollRoundReady()
{
	if (!bPreparingRound)
	{
		GetWorldTimerManager().ClearTimer(PrepPollHandle);
		return;
	}

	FreezePlayers();
	if (!bPlayersArrived)
	{
		return;
	}
	const float Waited = GetWorld()->GetTimeSeconds() - PrepStartTime;
	if (Waited < MinPreRoundSeconds)
	{
		return;
	}
	const bool bReady = !Generator || Generator->IsRoundReady();
	if (!bReady && Waited < RoundReadyTimeoutSeconds)
	{
		return;
	}
	if (!bReady)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] El generador no tiene lista la ronda %d tras %.0f s: se corre igual."), CurrentRound, Waited);
	}
	// Cada cliente que corre, con su parte montada: los asientos del terreno y el decorado local (con su colisión) los monta
	// cada máquina por su cuenta. Soltarlas antes daba correcciones, tortugas metidas en el decorado o bajo la arena.
	if (bReady && Generator)
	{
		FString Waiting;
		if (!AreClientsRoundReady(&Waiting))
		{
			const float Now = GetWorld()->GetTimeSeconds();
			if (ClientWaitStartTime < 0.f)
			{
				ClientWaitStartTime = Now;
				UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Ronda %d lista en el servidor: esperando a que %s la monten (como mucho %.0f s)."), CurrentRound,
					*Waiting, ClientRoundReadyTimeoutSeconds);
			}
			if (Now - ClientWaitStartTime < ClientRoundReadyTimeoutSeconds)
			{
				return;
			}
			UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] Ronda %d: %s no ha dicho que la tenga montada tras %.0f s: se corre igual."), CurrentRound, *Waiting,
				Now - ClientWaitStartTime);
		}
		else if (ClientWaitStartTime >= 0.f)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Ronda %d montada en todos los clientes (%.1f s de espera)."), CurrentRound,
				GetWorld()->GetTimeSeconds() - ClientWaitStartTime);
		}
	}

	bPreparingRound = false;
	GetWorldTimerManager().ClearTimer(PrepPollHandle);

	if (CurrentRound == 1)
	{
		// Las conchas son de esta partida (el PlayerState viaja desde el lobby o de una carrera anterior).
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
		LastRoundWonByPlayer.Reset();
	}

	if (bSprint)
	{
		// Sprint final: solo las finalistas, dentro de los huevos del nido en la línea del sprint.
		if (!PlaceSprintFinalists())
		{
			return;
		}
	}
	else
	{
		PlacePlayersAtStart();
	}

	if (bShowPreRaceCountdown && PreRaceCountdownSeconds > 0.f)
	{
		BeginPhaseClock(PreRaceCountdownSeconds);
		GetWorldTimerManager().SetTimer(PhaseEndHandle, this, &ATN_BeachRaceGameMode::BeginRace, PreRaceCountdownSeconds, false);
	}
	else
	{
		BeginRace();
	}
}

void ATN_BeachRaceGameMode::BeginRace()
{
	StopPhaseClock();
	GetWorldTimerManager().ClearTimer(PhaseEndHandle);
	if (bSprint)
	{
		// Sprint: la finalista que se haya quedado sin tortuga vuelve a su huevo.
		for (int32 Index = 0; Index < SprintFinalists.Num(); ++Index)
		{
			APlayerController* PC = SprintFinalists[Index].Get();
			if (PC && !PC->GetPawn())
			{
				PlaceSprintFinalist(PC, Index);
			}
		}
	}
	UnfreezePlayers();
	// Salida con huevos: con las tortugas ya sueltas, las tapas saltan y cada una sale lanzada hacia el mar, corriendo
	// (en el sprint final, desde el nido en la línea del sprint).
	if (Generator)
	{
		Generator->OpenStartEggs();
	}

	if (!bMatchStarted)
	{
		// Primera ronda: el arranque de la base (InProgress y cronómetro). Rompe el huevo con «¡ADELANTE!».
		Super::OnWaitingTimeout();
	}
	else
	{
		// Rondas siguientes: sin huevo, el rótulo «¡ADELANTE!» sale solo al volver a InProgress.
		MatchStartServerTime = GetWorld()->GetTimeSeconds();
		SetFlowState(ETNMatchFlowState::InProgress);
	}

	bRoundActive = true;
	SafeSpots.Reset();
	UnderSandWatch.Reset();
	NextSafeSampleTime = 0.f;
	LowestGroundZ = CourseOrigin.Z;
	SetRacePhase(ETNBeachRacePhase::Racing);
	StartStorm();

	GetWorldTimerManager().SetTimer(WatchHandle, this, &ATN_BeachRaceGameMode::WatchRacers, TNBeachRaceGameModeDetail::WatchInterval, true);
	GetWorldTimerManager().ClearTimer(RoundTimeLimitHandle);
	const float TimeLimit = bSprint ? SprintTimeLimitSeconds : RoundTimeLimitSeconds;
	RoundClockEndTime = -1.f;
	RoundTimeWarningsLogged = 0;
	if (TimeLimit > 0.f)
	{
		GetWorldTimerManager().SetTimer(RoundTimeLimitHandle, this, &ATN_BeachRaceGameMode::OnRoundTimeLimit, TimeLimit, false);
		RoundClockEndTime = GetWorld()->GetTimeSeconds() + TimeLimit;
	}
	// El tiempo de la ronda, replicado en hora del servidor: el HUD lo enseña en el último minuto (con aviso a los 60 y a los
	// 30 s) y la cinta del «¡TIEMPO!» dice el motivo (RoundEndReason) si se agota sin nadie en el agua.
	if (ATN_BeachRaceGameState* BeachState = GetBeachGameState())
	{
		BeachState->RoundEndReason = ETNBeachRoundEnd::None;
		BeachState->RoundTimeLimitSeconds = FMath::Max(0.f, TimeLimit);
		BeachState->RoundEndServerTime = TimeLimit > 0.f ? static_cast<float>(BeachState->GetServerWorldTimeSeconds()) + TimeLimit : 0.f;
		BeachState->ForceNetUpdate();
	}

	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] ═══ Ronda %d en marcha%s · semilla %d · tiempo de ronda %s ═══"), CurrentRound, bSprint ? TEXT(" (sprint final)") : TEXT(""),
		RoundSeed, TimeLimit > 0.f ? *FString::Printf(TEXT("%d:%02d"), FMath::FloorToInt(TimeLimit / 60.f), FMath::FloorToInt(FMath::Fmod(TimeLimit, 60.f))) : TEXT("sin límite"));
	SyncGameState();
}

void ATN_BeachRaceGameMode::WatchRacers()
{
	// También con «¡TIEMPO!» en pantalla y mientras se ve el chapuzón de las que han llegado: quien salte entonces del
	// acantilado tampoco se hace bola, y las que llegan pasan a espectadoras al acabar su margen.
	const bool bPendingArrivals = Arrivals.ContainsByPredicate([](const FTNBeachArrival& Arrival) { return !Arrival.bSettled; });
	if (!bRoundActive && !bTimeUp && !bPendingArrivals)
	{
		GetWorldTimerManager().ClearTimer(WatchHandle);
		return;
	}
	SettleArrivals(false);
	const float Now = GetWorld()->GetTimeSeconds();
	LogRoundTimeWarnings(Now);
	const bool bSample = Now >= NextSafeSampleTime;
	if (bSample)
	{
		NextSafeSampleTime = Now + SafeSpotSampleSeconds;
	}

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		const ATN_CoopPlayerState* PS = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
		if (!PS || PS->bHasFinishedRun || PS->IsOnlyASpectator())
		{
			continue;
		}
		APawn* Pawn = PC->GetPawn();
		if (!Pawn)
		{
			// Sin peón en plena carrera: el motor lo ha destruido por debajo del KillZ del nivel.
			if (bRoundActive && !HasArrived(PC))
			{
				RescueTurtle(PC, TEXT("fuera del mundo"));
			}
			continue;
		}
		const ACharacter* Character = Cast<ACharacter>(Pawn);
		if (Character && ATN_BeachSandWorm::IsBeingEaten(Character))
		{
			// En la boca de un gusano de arena (se acabó la cuenta): nada la mueve ni la rescata hasta la ronda siguiente.
			continue;
		}
		GuardCliffJump(Pawn);
		if (!bRoundActive || HasArrived(PC))
		{
			// Ya en el agua (viendo su chapuzón) o con la ronda cerrada: nadie más llega ni se rescata hasta el recuento.
			continue;
		}
		const FVector Location = Pawn->GetActorLocation();
		if (Generator && Generator->IsFinishWater(Location))
		{
			// La primera que toca el agua gana la ronda y arranca la cuenta atrás; las demás, media concha.
			MarkPlayerFinished(PC);
			continue;
		}
		if (Location.Z < GetVoidZ())
		{
			RescueTurtle(PC, TEXT("vacío"));
			continue;
		}
		// Red de seguridad: bajo la arena o cayendo sin suelo, vuelve encima (mucho antes que el vacío, 150 m más abajo).
		if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Pawn))
		{
			GuardUnderSand(PC, Turtle, Now);
		}
		if (bSample)
		{
			SampleSafeSpot(PC, Cast<ACharacter>(Pawn), Now);
		}
	}
}

void ATN_BeachRaceGameMode::OnRoundTimeLimit()
{
	if (!bRoundActive)
	{
		return;
	}
	// Nadie ha llegado al agua en el límite: gana quien esté más cerca del mar (en el sprint, entre las finalistas). En el
	// sprint, si ninguna finalista tiene tortuga (caída bajo KillZ, esperando su huevo), la primera que quede: antes no ganaba
	// nadie y, con dos finalistas aún conectadas, CheckSprintForfeit dejaba la partida parada sin terminar (#56).
	TArray<APlayerController*> Controllers;
	TArray<TNBeachRaceRules::FTimeLimitCandidate> Candidates;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		const ATN_CoopPlayerState* PS = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		TNBeachRaceRules::FTimeLimitCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Controllers.Add(PC);
		Candidate.bEligible = PS && !PS->IsOnlyASpectator() && (!bSprint || IsSprintFinalist(PC));
		Candidate.bHasPawn = Pawn != nullptr;
		Candidate.Progress = Pawn ? GetCourseProgress(Pawn) : 0.f;
	}
	const int32 BestIndex = TNBeachRaceRules::PickTimeLimitWinner(Candidates, bSprint);
	APlayerController* Best = Controllers.IsValidIndex(BestIndex) ? Controllers[BestIndex] : nullptr;
	ATN_CoopPlayerState* BestState = Best ? Best->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
	const float LimitMinutes = (bSprint ? SprintTimeLimitSeconds : RoundTimeLimitSeconds) / 60.f;
	ATN_BeachRaceGameState* BeachState = GetBeachGameState();
	if (bSprint)
	{
		// Sprint final: «¡TIEMPO!» un momento, con el motivo en su cinta (nadie ha llegado al agua), y luego el podio de la
		// finalista más cerca del mar. Antes se saltaba al podio sin decir por qué.
		bRoundActive = false;
		StopRoundClock();
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Sprint final: se acaban los %.1f min sin nadie en el agua; gana %s, la más cerca del mar."), LimitMinutes,
			BestState ? *BestState->GetPlayerName() : TEXT("nadie"));
		if (!BestState)
		{
			CheckSprintForfeit();
			return;
		}
		FTNBeachArrival& Arrival = Arrivals.AddDefaulted_GetRef();
		Arrival.Controller = Best;
		Arrival.State = BestState;
		Arrival.Name = BestState->GetPlayerName();
		Arrival.bSettled = true;
		if (BeachState)
		{
			BeachState->RoundWinner = BestState;
			BeachState->FinishCountdown = ETNBeachFinishCountdown::TimeUp;
			BeachState->RoundEndReason = ETNBeachRoundEnd::SprintTimeLimit;
			BeachState->NotifyRacePhaseChanged();
			BeachState->ForceNetUpdate();
		}
		FreezePlayers();
		StopStorm(false);
		GetWorldTimerManager().SetTimer(SprintWinHandle, this, &ATN_BeachRaceGameMode::CompleteSprintWin, FMath::Max(0.05f, TimeUpHoldSeconds), false);
		return;
	}
	if (BestState && Arrivals.Num() == 0)
	{
		// Se lleva la concha entera sin haber llegado: sin chapuzón ni meta de la base.
		FTNBeachArrival& Arrival = Arrivals.AddDefaulted_GetRef();
		Arrival.Controller = Best;
		Arrival.State = BestState;
		Arrival.Name = BestState->GetPlayerName();
		Arrival.Halves = 2;
		Arrival.bSettled = true;
	}
	if (BeachState)
	{
		// La cinta del «¡TIEMPO!» dice que nadie ha llegado y para quién es la concha (antes enseñaba «¡La primera ya está en el
		// agua!» sin nadie en ella).
		BeachState->RoundWinner = BestState;
	}
	const FString Outcome = BestState ? FString::Printf(TEXT("gana %s, la más cerca del mar"), *BestState->GetPlayerName()) : FString(TEXT("nadie gana"));
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] ¡Tiempo! Se acaban los %.1f min de la ronda %d sin nadie en el agua: %s."), LimitMinutes, CurrentRound, *Outcome);
	FinishTimeUp(ETNBeachRoundEnd::TimeLimit);
}

void ATN_BeachRaceGameMode::StopRoundClock()
{
	GetWorldTimerManager().ClearTimer(RoundTimeLimitHandle);
	RoundClockEndTime = -1.f;
	ATN_BeachRaceGameState* BeachState = GetBeachGameState();
	if (BeachState && BeachState->RoundEndServerTime != 0.f)
	{
		BeachState->RoundEndServerTime = 0.f;
		BeachState->ForceNetUpdate();
	}
}

void ATN_BeachRaceGameMode::LogRoundTimeWarnings(float Now)
{
	if (!bRoundActive || RoundClockEndTime < 0.f)
	{
		return;
	}
	const float Left = RoundClockEndTime - Now;
	const int32 Due = Left <= 30.f ? 2 : (Left <= 60.f ? 1 : 0);
	if (Due <= RoundTimeWarningsLogged)
	{
		return;
	}
	RoundTimeWarningsLogged = Due;
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Ronda %d%s: %s y nadie ha llegado al agua (al acabarse, la concha es para la más cerca del mar)."), CurrentRound,
		bSprint ? TEXT(" (sprint final)") : TEXT(""), Due == 2 ? TEXT("quedan 30 s") : TEXT("queda 1 minuto"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Meta y «muerte»
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachRaceGameMode::MarkPlayerFinished(APlayerController* PlayerController)
{
	if (!HasAuthority() || !PlayerController || !bRoundActive || HasArrived(PlayerController))
	{
		return;
	}
	ATN_CoopPlayerState* PS = PlayerController->GetPlayerState<ATN_CoopPlayerState>();
	if (!PS || PS->bHasFinishedRun || !PS->bIsAlive || PS->IsOnlyASpectator() || (bSprint && !IsSprintFinalist(PlayerController)))
	{
		return;
	}

	// El puesto se decide en el instante del contacto: la primera se lleva la concha entera; las de la cuenta atrás, media.
	const bool bFirst = Arrivals.Num() == 0;
	const float Now = GetWorld()->GetTimeSeconds();
	FTNBeachArrival& Arrival = Arrivals.AddDefaulted_GetRef();
	Arrival.Controller = PlayerController;
	Arrival.State = PS;
	Arrival.Name = PS->GetPlayerName();
	Arrival.Halves = bFirst ? 2 : 1;
	// Se queda a la vista dentro del agua con su chapuzón (en todas las máquinas) y luego pasa a espectadora (WatchRacers).
	Arrival.HoldEnd = Now + FinishSplashHoldSeconds;
	Arrival.ArrivedAt = Now;
	const FString ArrivalName = Arrival.Name;
	// El puesto, replicado: en la pantalla de quien llega se cierra el huevo negro con «Has quedado X.º» y su premio
	// (UTN_RaceScreensSubsystem). TN.Race.WinRound puede pedir otro para ver los premios con pocos jugadores.
	const int32 Place = DebugForcedPlace > 0 ? DebugForcedPlace : Arrivals.Num();

	// Al agua como tortuga, no como bola: fuera del caparazón, del aturdimiento y de quien la llevara.
	if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PlayerController->GetPawn()))
	{
		ReleaseCarry(Turtle);
		if (UTN_BeachStunComponent* Stun = UTN_BeachStunComponent::FindOn(Turtle))
		{
			Stun->EndStun(true);
		}
		if (UTN_ShellComponent* Shell = Turtle->GetShellComponent())
		{
			Shell->SetExitLocked(false);
			Shell->ForceExitShell();
		}
		// Lo que promete TN_RaceItemComponent.h: al llegar se quitan turbo y protector (no derriba a las que entran).
		if (UTN_RaceItemComponent* Effects = UTN_RaceItemComponent::FindOn(Turtle))
		{
			Effects->CancelEffects();
		}
	}

	ATN_BeachRaceGameState* BeachState = GetBeachGameState();
	if (BeachState)
	{
		BeachState->AddRoundArrival(PS, Place);
	}
	if (bSprint)
	{
		// Sprint final: la primera en el agua es campeona, sin cuenta atrás. El podio sale tras su chapuzón (debajo de su
		// pantalla del puesto, que se abre ya sobre él).
		bRoundActive = false;
		StopRoundClock();
		if (BeachState)
		{
			BeachState->RoundWinner = PS;
			BeachState->NotifyRacePhaseChanged();
			BeachState->ForceNetUpdate();
		}
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] ═══ %s gana el sprint final ═══"), *ArrivalName);
		GetWorldTimerManager().SetTimer(SprintWinHandle, this, &ATN_BeachRaceGameMode::CompleteSprintWin, FMath::Max(0.05f, FinishSplashHoldSeconds), false);
		return;
	}

	if (bFirst)
	{
		// La primera: gana la ronda y arranca la cuenta atrás para todas (la tormenta y todo lo demás siguen). El tiempo de la
		// ronda deja de contar (y su reloj se quita del HUD): manda la cuenta de 10 s.
		StopRoundClock();
		if (BeachState)
		{
			BeachState->RoundWinner = PS;
			BeachState->FinishCountdown = ETNBeachFinishCountdown::Counting;
			BeachState->FinishCountdownSeconds = FinishCountdownSeconds;
			BeachState->FinishCountdownEndTime = static_cast<float>(BeachState->GetServerWorldTimeSeconds()) + FinishCountdownSeconds;
		}
		GetWorldTimerManager().SetTimer(FinishCountdownHandle, this, &ATN_BeachRaceGameMode::OnFinishCountdownEnd, FinishCountdownSeconds, false);
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s toca el agua de meta: gana la ronda %d. Cuenta atrás de %.0f s: media concha para quien llegue."),
			*ArrivalName, CurrentRound, FinishCountdownSeconds);
	}
	else
	{
		if (BeachState)
		{
			BeachState->RoundHalfShells.Add(PS);
		}
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s llega en la cuenta atrás: media concha (%d.ª)."), *ArrivalName, Arrivals.Num());
	}
	if (BeachState)
	{
		BeachState->NotifyRacePhaseChanged();
		BeachState->ForceNetUpdate();
	}
	if (AreAllRacersIn())
	{
		FinishTimeUp(ETNBeachRoundEnd::AllIn);
	}
}

bool ATN_BeachRaceGameMode::HasArrived(const AController* Controller) const
{
	return Controller && Arrivals.ContainsByPredicate([Controller](const FTNBeachArrival& Arrival) { return Arrival.Controller.Get() == Controller; });
}

bool ATN_BeachRaceGameMode::AreAllRacersIn(const AController* Ignore) const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const ATN_CoopPlayerState* PS = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
		if (!PS || PC == Ignore || HasArrived(PC) || PS->bHasFinishedRun || PS->IsOnlyASpectator() || (bSprint && !IsSprintFinalist(PC)))
		{
			continue;
		}
		return false;
	}
	return true;
}

void ATN_BeachRaceGameMode::SettleArrivals(bool bAll)
{
	const float Now = GetWorld()->GetTimeSeconds();
	for (FTNBeachArrival& Arrival : Arrivals)
	{
		if (Arrival.bSettled || (!bAll && Now < Arrival.HoldEnd))
		{
			continue;
		}
		Arrival.bSettled = true;
		APlayerController* PC = Arrival.Controller.Get();
		const ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(Arrival.State.Get());
		if (!PC || !PS || PS->bHasFinishedRun)
		{
			continue;
		}
		// La meta de la base: puesto y puntos (la música de llegar sale de ahí), la oculta y la pasa a espectadora por la
		// vía normal (MovePlayerToSpectator: el fantasma que sigue a las demás). La ronda la cierra la cuenta atrás.
		bSuppressRoundCheck = true;
		Super::MarkPlayerFinished(PC);
		bSuppressRoundCheck = false;
	}
}

void ATN_BeachRaceGameMode::OnFinishCountdownEnd()
{
	// La cuenta ha llegado a 0: «¡TIEMPO!» y, para las que no han llegado, los gusanos de arena.
	FinishTimeUp(ETNBeachRoundEnd::Countdown);
}

void ATN_BeachRaceGameMode::FinishTimeUp(ETNBeachRoundEnd Reason)
{
	if (!bRoundActive || bTimeUp)
	{
		return;
	}
	const bool bAllIn = Reason == ETNBeachRoundEnd::AllIn;
	// Nadie más llega: «¡TIEMPO!» (o «¡todas en el agua!») en todas las pantallas, todas quietas y, enseguida, el recuento.
	bRoundActive = false;
	bTimeUp = true;
	GetWorldTimerManager().ClearTimer(FinishCountdownHandle);
	StopRoundClock();
	if (ATN_BeachRaceGameState* BeachState = GetBeachGameState())
	{
		BeachState->FinishCountdown = bAllIn ? ETNBeachFinishCountdown::AllIn : ETNBeachFinishCountdown::TimeUp;
		BeachState->RoundEndReason = Reason;
		BeachState->NotifyRacePhaseChanged();
		BeachState->ForceNetUpdate();
	}
	// Al llegar la cuenta a 0, a cada una que no ha llegado le sale un gusano de arena que se la come (antes de dejarlas
	// quietas: el gusano la saca del caparazón, del derribo y de quien la llevara). El recuento espera al bocado. Con el tiempo
	// de la ronda agotado, no: nadie ha llegado y la concha es para la más cerca del mar.
	const int32 Eaten = Reason == ETNBeachRoundEnd::Countdown && !bSprint ? FeedSandWorms() : 0;
	FreezePlayers();
	StopStorm(false);
	float Hold = Eaten > 0 ? FMath::Max(TimeUpHoldSeconds, ATN_BeachSandWorm::EatSeconds + SandWormMarginSeconds) : TimeUpHoldSeconds;
	// Quien acaba de llegar está viendo su pantalla del puesto (huevo negro y «Has quedado X.º»): el recuento no la corta, y
	// así su huevo se abre directamente sobre él (con todas en el agua, la última acaba de llegar).
	const float Now = GetWorld()->GetTimeSeconds();
	for (const FTNBeachArrival& Arrival : Arrivals)
	{
		if (Arrival.ArrivedAt > 0.f)
		{
			Hold = FMath::Max(Hold, Arrival.ArrivedAt + ArrivalScreenHoldSeconds - Now);
		}
	}
	if (Reason == ETNBeachRoundEnd::TimeLimit)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] ¡Tiempo de ronda agotado! Ronda %d: nadie en el agua; concha para %s, la más cerca del mar."), CurrentRound,
			Arrivals.Num() > 0 ? *Arrivals[0].Name : TEXT("nadie"));
	}
	else
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s Ronda %d: %d en el agua%s."), bAllIn ? TEXT("¡Todas en el agua!") : TEXT("¡Tiempo!"), CurrentRound, Arrivals.Num(),
			Eaten > 0 ? *FString::Printf(TEXT(" y %d para los gusanos de arena"), Eaten) : TEXT(""));
	}
	GetWorldTimerManager().SetTimer(TimeUpHandle, this, &ATN_BeachRaceGameMode::CloseRoundAfterTimeUp, Hold, false);
}

int32 ATN_BeachRaceGameMode::FeedSandWorms()
{
	// Las que aún corrían (ni en el agua, ni espectadoras). Nadie muere: se quedan en el gusano, ocultas, hasta la ronda
	// siguiente, que las vuelve a crear en la salida (CleanupRoundActors y PlacePlayersAtStart).
	int32 Eaten = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		const ATN_CoopPlayerState* PS = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
		ATortugaCharacter* Turtle = PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr;
		if (!PS || !Turtle || HasArrived(PC) || PS->bHasFinishedRun || PS->IsOnlyASpectator())
		{
			continue;
		}
		// A la boca como tortuga: fuera de quien la llevara (o de la que llevaba), del mareo, del derribo y del caparazón.
		ReleaseCarry(Turtle);
		if (UTN_BeachStunComponent* Stun = UTN_BeachStunComponent::FindOn(Turtle))
		{
			Stun->EndStun(true);
		}
		if (Turtle->IsKnockedDown())
		{
			Turtle->RecoverFromKnockdownSilently();
		}
		if (UTN_ShellComponent* Shell = Turtle->GetShellComponent())
		{
			Shell->SetExitLocked(false);
			Shell->ForceExitShell();
		}
		if (ATN_BeachSandWorm::EatTurtle(Turtle))
		{
			++Eaten;
			UE_LOG(LogTortunabo, Log, TEXT("[Carrera] A %s se la come un gusano de arena."), *PS->GetPlayerName());
		}
	}
	return Eaten;
}

void ATN_BeachRaceGameMode::CloseRoundAfterTimeUp()
{
	if (!bTimeUp)
	{
		return;
	}
	const FTNBeachArrival* First = Arrivals.Num() > 0 ? &Arrivals[0] : nullptr;
	const int32 HalfCount = FMath::Max(0, Arrivals.Num() - 1);
	const ATN_BeachRaceGameState* BeachState = GetBeachGameState();
	const bool bTimeLimit = BeachState && BeachState->RoundEndReason == ETNBeachRoundEnd::TimeLimit;
	FString Text;
	if (!First)
	{
		Text = TEXT("¡Tiempo! Nadie gana la ronda");
	}
	else if (bTimeLimit)
	{
		Text = FString::Printf(TEXT("¡Tiempo! Nadie ha llegado al agua: concha para %s, la más cerca del mar"), *First->Name);
	}
	else if (HalfCount == 0)
	{
		Text = FString::Printf(TEXT("¡%s gana la ronda!"), *First->Name);
	}
	else
	{
		Text = FString::Printf(TEXT("¡%s gana la ronda! Media concha para %d más"), *First->Name, HalfCount);
	}
	EndRound(Text);
}

void ATN_BeachRaceGameMode::MarkPlayerDead(APlayerController* PlayerController)
{
	// Todas las rutas de muerte del juego acaban aquí (zonas de muerte, tormenta, caídas largas y enemigos, vía
	// ATortugaCharacter::RequestKill): en la playa nadie muere, así que nunca se llama a la base.
	if (!HasAuthority() || !PlayerController)
	{
		return;
	}
	ATN_CoopPlayerState* PS = PlayerController->GetPlayerState<ATN_CoopPlayerState>();
	if (!PS || PS->bHasFinishedRun)
	{
		return;
	}
	PS->DeathZoneTimeRemaining = -1.f;
	if (!bRoundActive || HasArrived(PlayerController))
	{
		// Preparando, con «¡TIEMPO!», en el recuento o con el campeón (las tortugas están quietas o en la salida), o ya en
		// el agua de meta viendo su chapuzón: nada.
		return;
	}

	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PlayerController->GetPawn());
	if (!Turtle)
	{
		RescueTurtle(PlayerController, TEXT("sin tortuga"));
		return;
	}
	const FVector Location = Turtle->GetActorLocation();
	if (Generator && Generator->IsFinishWater(Location))
	{
		// Una caída larga que acaba en el agua de meta es llegar, no caerse.
		MarkPlayerFinished(PlayerController);
		return;
	}
	if (Location.Z < GetVoidZ() || IsInsideHazard(Turtle))
	{
		// Dentro de una zona de muerte o de la tormenta seguiría «muriendo»: vuelve a un sitio seguro.
		RescueTurtle(PlayerController, TEXT("zona de muerte"));
		return;
	}
	TNBeach::StunTurtle(Turtle, DeathStunSeconds);
}

void ATN_BeachRaceGameMode::UpdateRoundProgressAndMaybeFinish()
{
	if (bSuppressRoundCheck || !bRoundActive || !GameState)
	{
		return;
	}
	// Llega aquí tras una desconexión: el marcador de siempre y, si con la cuenta atrás ya no queda nadie corriendo,
	// «¡TIEMPO!» sin esperar al final. La primera en el agua la da MarkPlayerFinished.
	int32 Total = 0;
	int32 Finished = 0;
	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		const ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS);
		if (!PS)
		{
			continue;
		}
		++Total;
		if (PS->bHasFinishedRun && !PS->bIsEliminated)
		{
			++Finished;
		}
	}
	if (ATN_CoopGameState* CoopState = GetGameState<ATN_CoopGameState>())
	{
		CoopState->FinishedPlayers = Finished;
		CoopState->ExpectedPlayers = Total;
	}
	if (!bSprint && Arrivals.Num() > 0 && AreAllRacersIn())
	{
		FinishTimeUp(ETNBeachRoundEnd::AllIn);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Fin de ronda, recuento y campeón
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachRaceGameMode::EndRound(const FString& ResultText)
{
	// En juego (por si algo cierra la ronda de golpe) o con «¡TIEMPO!» en pantalla (CloseRoundAfterTimeUp).
	if (!bRoundActive && !bTimeUp)
	{
		return;
	}
	bRoundActive = false;
	bTimeUp = false;
	GetWorldTimerManager().ClearTimer(WatchHandle);
	StopRoundClock();
	GetWorldTimerManager().ClearTimer(FinishCountdownHandle);
	GetWorldTimerManager().ClearTimer(TimeUpHandle);
	StopStorm(false);

	// Conchas: entera para la primera en el agua y media para cada una de la cuenta atrás (si alguien se ha ido, conserva
	// la suya mientras exista su PlayerState).
	ATN_CoopPlayerState* WinnerState = nullptr;
	TArray<TObjectPtr<APlayerState>> HalfShells;
	for (const FTNBeachArrival& Arrival : Arrivals)
	{
		ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(Arrival.State.Get());
		if (!PS)
		{
			continue;
		}
		PS->RaceShellHalves += Arrival.Halves;
		// El PlayerState replica a 5 Hz (1 en reposo): las conchas del recuento, ya.
		PS->ForceNetUpdate();
		if (Arrival.Halves >= 2 && !WinnerState)
		{
			WinnerState = PS;
			++PS->RoundWins;
			LastRoundWonByPlayer.Add(PS->GetPlayerId(), CurrentRound);
		}
		else
		{
			HalfShells.Add(PS);
		}
	}
	if (ATN_BeachRaceGameState* BeachState = GetBeachGameState())
	{
		BeachState->RoundWinner = WinnerState;
		BeachState->RoundHalfShells = HalfShells;
		BeachState->RoundResultText = ResultText;
		BeachState->FinishCountdown = ETNBeachFinishCountdown::None;
		BeachState->FinishCountdownEndTime = 0.f;
	}

	// Recuento: todos quietos donde estén; las conchas nuevas las pinta la interfaz a partir de RoundWinner,
	// RoundHalfShells y RaceShellHalves.
	FreezePlayers();
	SetFlowState(ETNMatchFlowState::Countdown);
	SetRacePhase(ETNBeachRacePhase::RoundResults);
	BeginPhaseClock(RoundResultsSeconds);
	GetWorldTimerManager().SetTimer(PhaseEndHandle, this, &ATN_BeachRaceGameMode::AfterRoundResults, RoundResultsSeconds, false);

	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Ronda %d terminada: %s"), CurrentRound, *ResultText);
	SyncGameState();
	// El recuento sale ya en todas las máquinas (lo que venga detrás, como la cámara de espectadora, queda tapado).
	if (GameState)
	{
		GameState->ForceNetUpdate();
	}
	// Detrás del recuento, las que aún estaban en el agua pasan por la meta de la base (puesto, puntos, espectadoras).
	SettleArrivals(true);
}

void ATN_BeachRaceGameMode::AfterRoundResults()
{
	StopPhaseClock();
	// Campeona: la única en lo más alto con WinsToWinMatch conchas o más; si hay empate ahí arriba, sprint final.
	const int32 TargetHalves = WinsToWinMatch * 2;
	int32 Top = 0;
	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		Top = FMath::Max(Top, HalvesOf(BasePS));
	}
	if (Top >= TargetHalves)
	{
		TArray<ATN_CoopPlayerState*> Leaders;
		for (APlayerState* BasePS : GameState->PlayerArray)
		{
			ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS);
			if (PS && PS->RaceShellHalves == Top)
			{
				Leaders.Add(PS);
			}
		}
		if (Leaders.Num() == 1)
		{
			EnterChampion(Leaders[0]);
		}
		else
		{
			EnterSprintIntro(Leaders);
		}
		return;
	}
	StartNextRound();
}

void ATN_BeachRaceGameMode::StartNextRound()
{
	++CurrentRound;
	bShowPreRaceCountdown = true;
	ResetRoundPlayerStates();
	SetFlowState(ETNMatchFlowState::WaitingForPlayers);
	PrepareRound(true);
}

void ATN_BeachRaceGameMode::EnterChampion(ATN_CoopPlayerState* ChampionState)
{
	CancelRoundTimers();
	bRoundActive = false;
	bTimeUp = false;
	bMatchOver = true;
	// El sprint (si lo hubo) se acaba aquí; la interfaz aún mira bSprintFinal para ir directa al podio.
	bSprint = false;
	SprintFinalists.Reset();
	StopStorm(false);
	FreezePlayers();

	// Podio: el campeón y, detrás, por conchas (en medias); a igualdad, quien ganó una ronda más tarde; después, por
	// orden de llegada a la partida.
	TArray<ATN_CoopPlayerState*> Standings;
	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		if (ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS))
		{
			Standings.Add(PS);
		}
	}
	Standings.Sort([this, ChampionState](const ATN_CoopPlayerState& A, const ATN_CoopPlayerState& B)
	{
		if ((&A == ChampionState) != (&B == ChampionState))
		{
			return &A == ChampionState;
		}
		if (A.RaceShellHalves != B.RaceShellHalves)
		{
			return A.RaceShellHalves > B.RaceShellHalves;
		}
		const int32 LastA = LastRoundWonByPlayer.FindRef(A.GetPlayerId());
		const int32 LastB = LastRoundWonByPlayer.FindRef(B.GetPlayerId());
		if (LastA != LastB)
		{
			return LastA > LastB;
		}
		return A.GetPlayerId() < B.GetPlayerId();
	});

	if (ATN_BeachRaceGameState* BeachState = GetBeachGameState())
	{
		BeachState->Champion = ChampionState;
		BeachState->Podium.Reset();
		for (int32 Index = 0; Index < Standings.Num() && Index < 3; ++Index)
		{
			BeachState->Podium.Add(Standings[Index]);
		}
		// La tabla de siempre (HUD de resultados y música de fin de partida): puesto por conchas.
		BeachState->RaceResults.Reset();
		for (int32 Index = 0; Index < Standings.Num(); ++Index)
		{
			BeachState->Server_UpsertRaceResult(Standings[Index]->GetPlayerId(), Standings[Index]->GetPlayerName(),
				Index + 1, 0.f, Standings[Index]->RaceShellHalves, false);
		}
		SetFlowState(ETNMatchFlowState::Results);
		BeachState->CountdownValue = TNBeachRaceGameModeDetail::ChampionHoldCountdown;
		BeachState->PhaseSecondsLeft = 0.f;
		BeachState->FinishCountdown = ETNBeachFinishCountdown::None;
	}
	SetRacePhase(ETNBeachRacePhase::Champion);

	const int32 Halves = HalvesOf(ChampionState);
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] ═══ ¡%s es campeona con %d conchas%s tras %d rondas! Esperando al anfitrión. ═══"),
		ChampionState ? *ChampionState->GetPlayerName() : TEXT("nadie"), Halves / 2, (Halves % 2) ? TEXT(" y media") : TEXT(""), CurrentRound);
	SyncGameState();
}

// ─────────────────────────────────────────────────────────────────────────────
// Sprint final de desempate
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachRaceGameMode::EnterSprintIntro(const TArray<ATN_CoopPlayerState*>& Finalists)
{
	CancelRoundTimers();
	bRoundActive = false;
	bTimeUp = false;
	SprintFinalists.Reset();
	TArray<TObjectPtr<APlayerState>> FinalistStates;
	FString Names;
	for (ATN_CoopPlayerState* PS : Finalists)
	{
		APlayerController* PC = PS ? PS->GetPlayerController() : nullptr;
		if (!PC)
		{
			continue;
		}
		SprintFinalists.Add(PC);
		FinalistStates.Add(PS);
		Names += Names.IsEmpty() ? PS->GetPlayerName() : FString::Printf(TEXT(", %s"), *PS->GetPlayerName());
	}
	if (SprintFinalists.Num() == 0)
	{
		// Las empatadas se han ido: la partida es para quien quede con más conchas.
		CheckSprintForfeit();
		return;
	}

	// Título a pantalla completa («¡SPRINT FINAL!», las caras con «VS» y la fanfarria, en la interfaz) y todas quietas.
	StopStorm(false);
	FreezePlayers();
	SetFlowState(ETNMatchFlowState::Countdown);
	if (ATN_BeachRaceGameState* BeachState = GetBeachGameState())
	{
		BeachState->bSprintFinal = true;
		BeachState->SprintFinalists = FinalistStates;
		BeachState->Champion = nullptr;
		BeachState->FinishCountdown = ETNBeachFinishCountdown::None;
	}
	SetRacePhase(ETNBeachRacePhase::SprintIntro);
	BeginPhaseClock(SprintIntroSeconds);
	GetWorldTimerManager().SetTimer(PhaseEndHandle, this, &ATN_BeachRaceGameMode::StartSprint, SprintIntroSeconds, false);
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] ═══ ¡Empate en lo más alto! Sprint final entre %s ═══"), *Names);
	SyncGameState();
	if (GameState)
	{
		GameState->ForceNetUpdate();
	}
}

void ATN_BeachRaceGameMode::StartSprint()
{
	StopPhaseClock();
	bSprint = true;
	++CurrentRound;
	bShowPreRaceCountdown = true;
	ResetRoundPlayerStates();

	// Las que no corren: sin tortuga y a espectadoras por la vía normal (el fantasma que sigue a las finalistas). No se
	// limpian todos los peones como entre rondas (CleanupRoundActors), para no quitarles su vista de espectadora: solo las
	// tortugas sueltas. Las de las finalistas se cambian por otras nuevas dentro de sus huevos (PlaceSprintFinalist).
	StopStorm(true);
	SafeSpots.Reset();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (PC && !IsSprintFinalist(PC))
		{
			SendOutOfSprint(PC);
		}
	}
	for (TActorIterator<ATortugaCharacter> It(GetWorld()); It; ++It)
	{
		if (IsValid(*It) && !It->IsActorBeingDestroyed() && !It->GetController())
		{
			It->Destroy();
		}
	}

	// Reparto nuevo (trampas y enemigos enteros otra vez) con el nido de huevos en la línea del sprint; al estar listo,
	// las finalistas a sus huevos (PollRoundReady → PlaceSprintFinalists), 3, 2, 1 y los huevos se rompen.
	SetFlowState(ETNMatchFlowState::WaitingForPlayers);
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Sprint final: se prepara la playa (%d finalistas)."), SprintFinalists.Num());
	// Los peones se quedan (las finalistas se cambian al colocarlas en el nido), pero lo suelto de la ronda anterior, no.
	CleanupRoundLeftovers();
	PrepareRound(false);
}

bool ATN_BeachRaceGameMode::PlaceSprintFinalists()
{
	if (SprintFinalists.Num() == 0)
	{
		CheckSprintForfeit();
		return false;
	}
	// La línea del sprint despejada: fuera los elementos de la ronda que toquen el nido (sus dos filas de cuatro huevos) y un
	// margen alrededor, por donde caen las tortugas al salir lanzadas.
	const int32 NumSpots = FMath::Max(SprintFinalists.Num(), Generator ? Generator->GetNumStartSpots() : 1);
	TArray<FVector> Spots;
	FVector Center = FVector::ZeroVector;
	for (int32 Index = 0; Index < NumSpots; ++Index)
	{
		Spots.Add((Generator ? Generator->GetSprintStartTransform(Index) : GetStartTransformFor(Index)).GetLocation());
		Center += Spots.Last();
	}
	Center /= static_cast<double>(Spots.Num());
	double Reach = 0.0;
	for (const FVector& Spot : Spots)
	{
		Reach = FMath::Max(Reach, FVector::Dist2D(Spot, Center));
	}
	int32 Cleared = 0;
	if (Generator)
	{
		Cleared = Generator->ClearElementsAround(Center, static_cast<float>(Reach) + SprintClearMargin);
	}
	// La tormenta sale por detrás del nido y el progreso (límite de tiempo) se mide desde ahí.
	const FTransform Reference = Generator ? Generator->GetSprintStartTransform(0) : GetStartTransformFor(0);
	CourseOrigin = Center;
	CourseForward = FRotator(0.f, Reference.Rotator().Yaw, 0.f).Vector();
	LowestGroundZ = CourseOrigin.Z;

	for (int32 Index = 0; Index < SprintFinalists.Num(); ++Index)
	{
		if (APlayerController* PC = SprintFinalists[Index].Get())
		{
			PlaceSprintFinalist(PC, Index);
		}
	}
	// Quietas en sus huevos durante el 3, 2, 1.
	FreezePlayers();
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Sprint final: %d finalistas en sus huevos (%d elementos despejados alrededor del nido)."),
		SprintFinalists.Num(), Cleared);
	return true;
}

void ATN_BeachRaceGameMode::CompleteSprintWin()
{
	ATN_CoopPlayerState* Winner = Arrivals.Num() > 0 ? Cast<ATN_CoopPlayerState>(Arrivals[0].State.Get()) : nullptr;
	if (!Winner)
	{
		// Se ha ido tras llegar: la partida es para quien quede.
		CheckSprintForfeit();
		return;
	}
	EnterChampion(Winner);
}

void ATN_BeachRaceGameMode::CheckSprintForfeit()
{
	// La llaman el título, la preparación y la carrera del sprint (y Logout en ellos): nunca con la partida acabada.
	if (bMatchOver)
	{
		return;
	}
	TArray<ATN_CoopPlayerState*> Left;
	for (const TWeakObjectPtr<APlayerController>& Weak : SprintFinalists)
	{
		const APlayerController* PC = Weak.Get();
		if (ATN_CoopPlayerState* PS = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr)
		{
			Left.Add(PS);
		}
	}
	if (Left.Num() >= 2)
	{
		// Siguen dos o más: el sprint sigue.
		return;
	}
	// Solo en el sprint: en su título aún quedan las llegadas de la ronda anterior (PrepareRound las borra al empezarlo) y una
	// finalista que se iba entonces dejaba a la otra corriendo sola el sprint entero.
	if (bSprint && !bRoundActive && Arrivals.Num() > 0 && Cast<ATN_CoopPlayerState>(Arrivals[0].State.Get()))
	{
		// Ya hay ganadora del sprint (su podio sale tras el chapuzón).
		return;
	}
	ATN_CoopPlayerState* Winner = Left.Num() == 1 ? Left[0] : nullptr;
	if (!Winner)
	{
		// Sin finalistas: la de más conchas de las que quedan.
		for (APlayerState* BasePS : GameState->PlayerArray)
		{
			ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS);
			if (PS && (!Winner || PS->RaceShellHalves > Winner->RaceShellHalves))
			{
				Winner = PS;
			}
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Sprint final sin rival: la partida es para %s."), Winner ? *Winner->GetPlayerName() : TEXT("nadie"));
	if (Winner)
	{
		if (ATN_BeachRaceGameState* BeachState = GetBeachGameState())
		{
			BeachState->RoundEndReason = ETNBeachRoundEnd::Forfeit;
		}
		EnterChampion(Winner);
	}
}

bool ATN_BeachRaceGameMode::IsSprintFinalist(const AController* Controller) const
{
	return Controller && SprintFinalists.ContainsByPredicate([Controller](const TWeakObjectPtr<APlayerController>& Weak) { return Weak.Get() == Controller; });
}

void ATN_BeachRaceGameMode::PlaceSprintFinalist(APlayerController* PlayerController, int32 Index)
{
	if (!PlayerController)
	{
		return;
	}
	// Tortuga nueva (sin caparazón, mareo ni carga) dentro de su huevo del nido, en la línea del sprint.
	if (APawn* OldPawn = PlayerController->GetPawn())
	{
		ReleaseCarry(Cast<ATortugaCharacter>(OldPawn));
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
	const FTransform Spot = Generator ? Generator->GetSprintStartTransform(Index) : GetStartTransformFor(Index);
	const FTransform Start = PutOnFloor(Spot, GetDefaultHalfHeight());
	RestartPlayerAtTransform(PlayerController, Start);
	APawn* NewPawn = PlayerController->GetPawn();
	if (!NewPawn)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] Sprint final: %s no ha podido aparecer en su huevo."), *GetNameSafe(PlayerController));
		return;
	}
	NewPawn->EnableInput(PlayerController);
	NewPawn->SetActorHiddenInGame(false);
	NewPawn->SetActorEnableCollision(true);
	PlayerController->SetViewTarget(NewPawn);
	PlayerController->ClientSetRotation(Start.Rotator(), true);
}

void ATN_BeachRaceGameMode::SendOutOfSprint(APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		return;
	}
	if (APawn* Pawn = PlayerController->GetPawn())
	{
		ReleaseCarry(Cast<ATortugaCharacter>(Pawn));
		PlayerController->UnPossess();
		Pawn->Destroy();
	}
	FrozenControllers.Remove(PlayerController);
	// Ha terminado su parte: puede ir cambiando de tortuga a la que mirar (y la música no la toma por una llegada).
	if (ATN_CoopPlayerState* PS = PlayerController->GetPlayerState<ATN_CoopPlayerState>())
	{
		PS->bHasFinishedRun = true;
		PS->ForceNetUpdate();
	}
	MovePlayerToSpectator(PlayerController);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pantalla del campeón
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_BeachRaceGameMode::RequestChampionChoice(const UObject* WorldContextObject, ETNBeachChampionChoice Choice)
{
	UWorld* World = WorldContextObject && GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		return false;
	}
	if (ATN_BeachRaceGameMode* GM = TNBeachRaceGameModeDetail::FindGameMode(World))
	{
		// Anfitrión: decide por todos, y solo con la partida acabada.
		if (!GM->bMatchOver || GM->bLeaving)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] %s: solo en la pantalla del campeón."), *UEnum::GetValueAsString(Choice));
			return false;
		}
		switch (Choice)
		{
		case ETNBeachChampionChoice::PlayAgain:  GM->PlayAgain(); break;
		case ETNBeachChampionChoice::ChangeMode: GM->ChangeModeAndReturnToLobby(); break;
		case ETNBeachChampionChoice::Quit:       GM->QuitToMainMenu(); break;
		default: return false;
		}
		return true;
	}
	// Cliente: lo demás lo decide el anfitrión; salir, cada uno por su cuenta.
	if (Choice == ETNBeachChampionChoice::Quit)
	{
		if (UMP_GameInstance* GI = Cast<UMP_GameInstance>(World->GetGameInstance()))
		{
			GI->HandleReturnToMenu();
			return true;
		}
	}
	return false;
}

bool ATN_BeachRaceGameMode::CanLocalPlayerChoose(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject && GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	const ATN_BeachRaceGameMode* GM = TNBeachRaceGameModeDetail::FindGameMode(World);
	return GM && GM->bMatchOver && !GM->bLeaving;
}

void ATN_BeachRaceGameMode::PlayAgain()
{
	if (!HasAuthority() || bLeaving)
	{
		return;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Volver a jugar: otra partida en la playa."));
	CancelRoundTimers();
	bMatchOver = false;
	bSprint = false;
	SprintFinalists.Reset();
	ResetMatchScores();
	CurrentRound = 1;
	bShowPreRaceCountdown = true;
	ResetRoundPlayerStates();
	SetFlowState(ETNMatchFlowState::WaitingForPlayers);
	PrepareRound(true);
}

void ATN_BeachRaceGameMode::ChangeModeAndReturnToLobby()
{
	if (!HasAuthority() || bLeaving)
	{
		return;
	}
	// Con dos modos, «el otro» de la carrera es el cooperativo; en el lobby se sale a él al ponerse listos.
	if (UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance()))
	{
		GI->SelectedProcMode = ETNProcGameMode::Coop;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Cambiar de modo: vuelta al lobby con el cooperativo elegido."));
	LeaveAfterDelay([this]()
	{
		FinishRoundAndReturnToLobby();
	});
}

void ATN_BeachRaceGameMode::QuitToMainMenu()
{
	if (!HasAuthority() || bLeaving)
	{
		return;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Salir: el anfitrión vuelve al menú y la partida se cierra."));
	LeaveAfterDelay([this]()
	{
		// Peones fuera antes de cortar la voz (como antes de cualquier viaje) y, al fotograma siguiente, al menú.
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (APawn* Pawn = PC ? PC->GetPawn() : nullptr)
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
}

void ATN_BeachRaceGameMode::LeaveAfterDelay(TFunction<void()> Action)
{
	bLeaving = true;
	CancelRoundTimers();
	// Con CountdownValue = 1 en Results se cierra el huevo en todas las pantallas y la música se funde, como al final
	// de la cuenta de los resultados de siempre.
	if (ATN_CoopGameState* CoopState = GetGameState<ATN_CoopGameState>())
	{
		CoopState->CountdownValue = 1;
	}
	GetWorldTimerManager().SetTimer(LeaveHandle, FTimerDelegate::CreateWeakLambda(this, [DeferredAction = MoveTemp(Action)]()
	{
		DeferredAction();
	}), ChampionLeaveDelaySeconds, false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Estado de la partida y de las rondas
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachRaceGameMode::ResetMatchScores()
{
	LastRoundWonByPlayer.Reset();
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
	if (ATN_BeachRaceGameState* BeachState = GetBeachGameState())
	{
		BeachState->RoundWinner = nullptr;
		BeachState->RoundHalfShells.Reset();
		BeachState->RoundArrivals.Reset();
		BeachState->Champion = nullptr;
		BeachState->Podium.Reset();
		BeachState->RoundResultText.Reset();
		BeachState->bSprintFinal = false;
		BeachState->SprintFinalists.Reset();
		BeachState->FinishCountdown = ETNBeachFinishCountdown::None;
		BeachState->RoundEndReason = ETNBeachRoundEnd::None;
		BeachState->NotifyRacePhaseChanged();
	}
}

void ATN_BeachRaceGameMode::ResetRoundGameState() const
{
	// Lo de la ronda anterior fuera (el recuento ya se vio): sin ganadora, medias, llegadas ni cuenta atrás.
	if (ATN_BeachRaceGameState* BeachState = GetBeachGameState())
	{
		BeachState->RoundWinner = nullptr;
		BeachState->RoundHalfShells.Reset();
		BeachState->RoundArrivals.Reset();
		BeachState->FinishCountdown = ETNBeachFinishCountdown::None;
		BeachState->FinishCountdownEndTime = 0.f;
		BeachState->FinishCountdownSeconds = FinishCountdownSeconds;
		BeachState->RoundEndReason = ETNBeachRoundEnd::None;
		BeachState->RoundEndServerTime = 0.f;
		BeachState->NotifyRacePhaseChanged();
	}
}

void ATN_BeachRaceGameMode::ResetRoundPlayerStates()
{
	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		if (ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS))
		{
			PS->ResetForNewRace();
			PS->ForceNetUpdate();
		}
	}
	NextFinishRank = 1;
	if (ATN_CoopGameState* CoopState = GetGameState<ATN_CoopGameState>())
	{
		CoopState->RaceResults.Reset();
		CoopState->FinishedPlayers = 0;
		CoopState->CountdownValue = 0;
		CoopState->OnRaceResultsUpdated.Broadcast();
	}
}

void ATN_BeachRaceGameMode::CancelRoundTimers()
{
	FTimerManager& Timers = GetWorldTimerManager();
	Timers.ClearTimer(PrepPollHandle);
	Timers.ClearTimer(PhaseClockHandle);
	Timers.ClearTimer(PhaseEndHandle);
	Timers.ClearTimer(WatchHandle);
	StopRoundClock();
	Timers.ClearTimer(FinishCountdownHandle);
	Timers.ClearTimer(TimeUpHandle);
	Timers.ClearTimer(SprintWinHandle);
	bPreparingRound = false;
	bTimeUp = false;
	PhaseEndTime = 0.f;
}

void ATN_BeachRaceGameMode::CleanupRoundActors()
{
	StopStorm(true);
	SafeSpots.Reset();

	// Fuera todas las tortugas: la ronda nueva las crea limpias (sin caparazón, aturdimiento ni carga) en la salida.
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
	CleanupRoundLeftovers();
}

void ATN_BeachRaceGameMode::CleanupRoundLeftovers()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}
	// Nada de lo que dejan las jugadoras pasa a la ronda siguiente (#71): los pickups que se sueltan o salen de una bola
	// parada, las conchas trampa y la reciclada que dejan al gastarse, las bolas en el aire y las cajas de objetos (el botín
	// repartido lo vuelve a quitar UTN_BeachLootSubsystem al cambiar la ronda: sus punteros son débiles y ya no existirán).
	// Se juntan antes de destruir, sin tocar la lista mientras se recorre. Una bola en el aire destruida así no suelta su
	// pickup (eso solo lo hace su temporizador de vida).
	TArray<AActor*> Leftovers;
	TNBeachRaceGameModeDetail::GatherRoundLeftovers<ATN_ThrowableItemActor>(World, Leftovers);
	TNBeachRaceGameModeDetail::GatherRoundLeftovers<ATN_PickupInteractableBase>(World, Leftovers);
	TNBeachRaceGameModeDetail::GatherRoundLeftovers<ATN_ConchPickup>(World, Leftovers);
	for (AActor* Leftover : Leftovers)
	{
		Leftover->Destroy();
	}
	if (Leftovers.Num() > 0)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Ronda %d: %d objetos sueltos quitados (pickups, bolas y conchas trampa)."), CurrentRound, Leftovers.Num());
	}
}

ETNBeachRacePhase ATN_BeachRaceGameMode::GetRacePhase() const
{
	const ATN_BeachRaceGameState* BeachState = GetBeachGameState();
	return BeachState ? BeachState->RacePhase : ETNBeachRacePhase::Waiting;
}

ATN_BeachRaceGenerator* ATN_BeachRaceGameMode::GetGenerator() const
{
	return Generator;
}

ATN_BeachRaceGameState* ATN_BeachRaceGameMode::GetBeachGameState() const
{
	return GetGameState<ATN_BeachRaceGameState>();
}

void ATN_BeachRaceGameMode::SetRacePhase(ETNBeachRacePhase NewPhase) const
{
	if (ATN_BeachRaceGameState* BeachState = GetBeachGameState())
	{
		BeachState->RacePhase = NewPhase;
		BeachState->NotifyRacePhaseChanged();
		// Con la frecuencia de réplica adaptativa, un GameState quieto baja de ritmo: la fase nueva sale ya.
		BeachState->ForceNetUpdate();
	}
}

void ATN_BeachRaceGameMode::BeginPhaseClock(float Seconds)
{
	PhaseEndTime = GetWorld()->GetTimeSeconds() + FMath::Max(0.f, Seconds);
	TickPhaseClock();
	GetWorldTimerManager().SetTimer(PhaseClockHandle, this, &ATN_BeachRaceGameMode::TickPhaseClock, 0.25f, true);
}

void ATN_BeachRaceGameMode::StopPhaseClock()
{
	GetWorldTimerManager().ClearTimer(PhaseClockHandle);
	PhaseEndTime = 0.f;
	if (ATN_BeachRaceGameState* BeachState = GetBeachGameState())
	{
		BeachState->PhaseSecondsLeft = 0.f;
		BeachState->CountdownValue = 0;
	}
}

void ATN_BeachRaceGameMode::TickPhaseClock()
{
	const float Left = FMath::Max(0.f, PhaseEndTime - GetWorld()->GetTimeSeconds());
	if (ATN_BeachRaceGameState* BeachState = GetBeachGameState())
	{
		// PhaseSecondsLeft para la interfaz de la carrera; CountdownValue para la cuenta de siempre (música y HUD).
		BeachState->PhaseSecondsLeft = Left;
		BeachState->CountdownValue = FMath::CeilToInt(Left);
	}
	if (Left <= 0.f)
	{
		GetWorldTimerManager().ClearTimer(PhaseClockHandle);
	}
}

void ATN_BeachRaceGameMode::SyncGameState() const
{
	ATN_BeachRaceGameState* BeachState = GetBeachGameState();
	if (!BeachState)
	{
		return;
	}
	BeachState->ProcMode = ETNProcGameMode::Race;
	// La dificultad de la ronda (la que eligió el general en el lobby) es la del reparto del generador.
	BeachState->ProcDifficulty = Generator ? Generator->GetRoundDifficulty() : ETNProcDifficulty::Normal;
	BeachState->CurrentRound = CurrentRound;
	BeachState->RoundTarget = WinsToWinMatch;
	BeachState->bRoundInProgress = bRoundActive;
	BeachState->MapSeed = RoundSeed;
	BeachState->EstimatedMinutes = static_cast<float>(TNBeach::CourseLength / TNBeachRaceGameModeDetail::AverageRaceSpeed / 60.0);
	BeachState->NotifyRoundInfoChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
// Tortugas: salida, congelado, sitios seguros
// ─────────────────────────────────────────────────────────────────────────────

AActor* ATN_BeachRaceGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// Cada jugador aparece en su sitio de la salida (PlayerStart creado en ejecución la primera vez que se usa).
	if (Generator && Generator->IsRoundReady())
	{
		const int32 Slot = GetStartSlot(Player);
		if (!StartPoints.IsValidIndex(Slot) || !IsValid(StartPoints[Slot]))
		{
			const FTransform Start = PutOnFloor(GetStartTransformFor(Slot), GetDefaultHalfHeight());
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APlayerStart* Point = GetWorld()->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), Start, Params);
			if (Point)
			{
				Point->PlayerStartTag = FName(TEXT("TNBeachStart"));
				if (StartPoints.Num() <= Slot)
				{
					StartPoints.SetNum(Slot + 1);
				}
				StartPoints[Slot] = Point;
			}
		}
		if (StartPoints.IsValidIndex(Slot) && IsValid(StartPoints[Slot]))
		{
			return StartPoints[Slot];
		}
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}

int32 ATN_BeachRaceGameMode::GetStartSlot(const AController* Controller) const
{
	if (!Controller || !GameState)
	{
		return 0;
	}
	const int32 Count = FMath::Max(1, GameState->PlayerArray.Num());
	const int32 Index = FMath::Max(0, GameState->PlayerArray.IndexOfByKey(Controller->PlayerState));
	// La salida es escalonada: los sitios rotan cada ronda para que nadie salga siempre delante.
	return (Index + FMath::Max(0, CurrentRound - 1)) % Count;
}

FTransform ATN_BeachRaceGameMode::GetStartTransformFor(int32 Slot) const
{
	if (Generator)
	{
		return Generator->GetStartTransform(Slot);
	}
	// Sin generador: un PlayerStart del nivel, con los sitios en fila a su derecha.
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		if (It->PlayerStartTag != FName(TEXT("TNBeachStart")))
		{
			const FTransform Base = It->GetActorTransform();
			return FTransform(Base.GetRotation(), Base.GetLocation() + Base.GetRotation().GetRightVector() * (200.0 * Slot));
		}
	}
	return FTransform::Identity;
}

FTransform ATN_BeachRaceGameMode::PutOnFloor(const FTransform& Transform, float HalfHeight) const
{
	FVector Location = Transform.GetLocation();
	const FRotator Facing(0.f, Transform.Rotator().Yaw, 0.f);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(TNBeachPutOnFloor), false);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Location + FVector(0.0, 0.0, 300.0), Location - FVector(0.0, 0.0, 1500.0), Objects, Query)
		&& Hit.ImpactNormal.Z > 0.5)
	{
		Location.Z = Hit.ImpactPoint.Z + HalfHeight + 2.0;
	}
	return FTransform(Facing, Location);
}

float ATN_BeachRaceGameMode::GetDefaultHalfHeight() const
{
	const ACharacter* Cdo = DefaultPawnClass ? Cast<ACharacter>(DefaultPawnClass->GetDefaultObject()) : nullptr;
	const UCapsuleComponent* Capsule = Cdo ? Cdo->GetCapsuleComponent() : nullptr;
	return Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.f;
}

void ATN_BeachRaceGameMode::PlacePlayersAtStart()
{
	const FTransform Reference = GetStartTransformFor(0);
	CourseOrigin = Reference.GetLocation();
	CourseForward = FRotator(0.f, Reference.Rotator().Yaw, 0.f).Vector();

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
		const ACharacter* Character = Cast<ACharacter>(Pawn);
		const float HalfHeight = Character && Character->GetCapsuleComponent()
			? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
			: GetDefaultHalfHeight();
		const FTransform Start = PutOnFloor(GetStartTransformFor(GetStartSlot(PC)), HalfHeight);
		if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Pawn))
		{
			TeleportTurtle(Turtle, Start);
		}
		else
		{
			Pawn->SetActorLocationAndRotation(Start.GetLocation(), Start.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
		}
		Pawn->SetActorHiddenInGame(false);
		Pawn->SetActorEnableCollision(true);
		PC->ClientSetRotation(Start.Rotator(), true);
	}
	// Quietos en la salida hasta que se dé la salida.
	FreezePlayers();
}

void ATN_BeachRaceGameMode::RespawnControllerFresh(APlayerController* PlayerController)
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

void ATN_BeachRaceGameMode::FreezePlayers()
{
	// Servidor (el peón) y cliente (el input). Si el peón cambia, se vuelve a avisar al cliente porque ClientRestart
	// limpia los flags de input. Las tortugas en su bola de caparazón siguen con su física.
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
				Move->StopMovementImmediately();
				Move->DisableMovement();
			}
		}
	}
}

void ATN_BeachRaceGameMode::UnfreezePlayers()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PC->GetPawn()))
		{
			const UTN_ShellComponent* Shell = Turtle->GetShellComponent();
			UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
			if (Move && Move->MovementMode == MOVE_None && !(Shell && Shell->HasLocalBody()))
			{
				Move->SetMovementMode(MOVE_Falling);
			}
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

void ATN_BeachRaceGameMode::ReleaseCarry(ATortugaCharacter* Turtle) const
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

void ATN_BeachRaceGameMode::TeleportTurtle(ATortugaCharacter* Turtle, const FTransform& Transform) const
{
	if (!Turtle)
	{
		return;
	}
	// Con caja física la tortuga sigue a la caja: fuera del caparazón y del aturdimiento (si hace falta, StunTurtle la vuelve
	// a meter después en el sitio nuevo), del derribo, de lo que lleve, de quien la lleve y del enemigo que la sujete.
	TNBeach::RelocateTurtle(Turtle, Transform);
}

void ATN_BeachRaceGameMode::RescueTurtle(APlayerController* PlayerController, const TCHAR* Reason)
{
	if (!PlayerController)
	{
		return;
	}
	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PlayerController->GetPawn());
	const bool bSpectating = PlayerController->GetStateName() == NAME_Spectating;
	if (!Turtle || bSpectating)
	{
		RespawnControllerFresh(PlayerController);
		Turtle = Cast<ATortugaCharacter>(PlayerController->GetPawn());
	}
	if (!Turtle)
	{
		return;
	}
	// Su último sitio seguro (en arena abierta, lejos de donde la red de seguridad la ha tenido que sacar hace poco y, si
	// queda dentro de la tormenta, por delante del frente).
	TArray<FVector> BadSpots;
	if (const FTNBeachUnderSandWatch* Watch = UnderSandWatch.Find(PlayerController))
	{
		for (const FTNBeachRescueMark& Mark : Watch->Rescues)
		{
			BadSpots.Add(Mark.Where);
		}
	}
	FString Where;
	const FTransform Safe = ResolveRescueTarget(PlayerController, Turtle, Turtle->GetActorLocation(), 1, BadSpots, Where);
	TNBeach::ReleaseTurtle(Turtle, TNBeach::ETNBeachMover::SafetyNet);
	TNBeach::ReleaseTurtle(Turtle, TNBeach::ETNBeachMover::StormKick);
	TeleportTurtle(Turtle, Safe);
	PlayerController->ClientSetRotation(Safe.Rotator(), true);
	if (ATN_CoopPlayerState* PS = PlayerController->GetPlayerState<ATN_CoopPlayerState>())
	{
		PS->DeathZoneTimeRemaining = -1.f;
	}
	TNBeach::StunTurtle(Turtle, RescueStunSeconds);
	TNBeach::GrantStormGrace(Turtle, RescueStunSeconds + SafetyNetStormGraceSeconds);
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s (%s): vuelve a %s, aturdida."), *GetNameSafe(PlayerController), Reason, *Where);
}

FTransform ATN_BeachRaceGameMode::FindSafeTransform(APlayerController* PlayerController)
{
	TArray<FTNBeachSafeSpot>* Trail = SafeSpots.Find(PlayerController);
	if (Trail && Trail->Num() > 0)
	{
		// El más nuevo con cierta antigüedad (el de justo antes de caer suele estar en el borde); si no, el más viejo.
		const float Now = GetWorld()->GetTimeSeconds();
		int32 Pick = 0;
		for (int32 Index = Trail->Num() - 1; Index >= 0; --Index)
		{
			if (Now - (*Trail)[Index].Time >= SafeSpotMinAgeSeconds)
			{
				Pick = Index;
				break;
			}
		}
		const FTNBeachSafeSpot Spot = (*Trail)[Pick];
		// Lo de después (más cerca de donde cayó) se olvida.
		Trail->SetNum(Pick + 1);
		return FTransform(Spot.Rotation, Spot.Location);
	}
	return GetStartTransformFor(GetStartSlot(PlayerController));
}

void ATN_BeachRaceGameMode::SampleSafeSpot(APlayerController* PlayerController, const ACharacter* Character, float Now)
{
	const UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Move || !Move->IsMovingOnGround() || !Move->CurrentFloor.IsWalkableFloor() || TNBeach::IsTurtleStunned(Character))
	{
		return;
	}
	if (const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Character))
	{
		if (Turtle->IsInShell() || Turtle->IsKnockedDown())
		{
			return;
		}
	}
	if (IsInsideHazard(Character))
	{
		return;
	}
	// Ni junto a un sitio donde la red de seguridad la ha tenido que sacar hace poco (el borde de un hoyo que la vuelve a
	// tragar).
	const FVector Here = Character->GetActorLocation();
	if (const FTNBeachUnderSandWatch* Watch = UnderSandWatch.Find(PlayerController))
	{
		const double BadSq = FMath::Square(static_cast<double>(SafetyNetBadSpotRadius));
		for (const FTNBeachRescueMark& Mark : Watch->Rescues)
		{
			if (Now - Mark.Time <= SafetyNetBadSpotSeconds && FVector::DistSquared2D(Here, Mark.Where) < BadSq)
			{
				return;
			}
		}
	}
	FTNBeachSafeSpot Spot;
	Spot.Location = Here;
	Spot.Rotation = FRotator(0.f, Character->GetActorRotation().Yaw, 0.f);
	Spot.Time = Now;
	TArray<FTNBeachSafeSpot>& Trail = SafeSpots.FindOrAdd(PlayerController);
	Trail.Add(Spot);
	if (Trail.Num() > TNBeachRaceGameModeDetail::MaxSafeSpots)
	{
		Trail.RemoveAt(0);
	}
	LowestGroundZ = FMath::Min(LowestGroundZ, Spot.Location.Z);
}

bool ATN_BeachRaceGameMode::IsInsideHazard(const APawn* Pawn) const
{
	if (!Pawn)
	{
		return false;
	}
	TArray<AActor*> Overlapping;
	Pawn->GetOverlappingActors(Overlapping, ATN_DeathZoneVolume::StaticClass());
	if (Overlapping.Num() > 0)
	{
		return true;
	}
	Pawn->GetOverlappingActors(Overlapping, ATN_StormVolume::StaticClass());
	return Overlapping.Num() > 0;
}

void ATN_BeachRaceGameMode::GuardCliffJump(APawn* Pawn) const
{
	// La bola del salto del acantilado salía de ATortugaCharacter::TickFallRules: a los AutoShellFallHeight (5 m) de caída
	// libre la tortuga se mete sola en el caparazón y cae rodando. Con la caída inmune no hay bola ni golpe al aterrizar
	// (FatalFallHeight) hasta tocar suelo o agua; la zambullida de UTN_TurtleAnimInstance la lleva de cabeza al agua.
	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Pawn);
	const UCharacterMovementComponent* Move = Turtle ? Turtle->GetCharacterMovement() : nullptr;
	if (!Generator || !Move || !Move->IsFalling() || Turtle->IsFallImmune() || Turtle->IsInShell() || TNBeach::IsTurtleStunned(Turtle))
	{
		return;
	}
	// A diez comprobaciones por segundo sobra: la bola llega tras 5 m de caída, mucho después de dejar el borde.
	if (Generator->IsCliffJumpZone(Turtle->GetActorLocation()))
	{
		Turtle->SetFallImmuneUntilLanded();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Seguridad: nunca bajo el mapa (Docs/Modo_Carrera.md)
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_BeachRaceGameMode::AreClientsRoundReady(FString* OutWaiting)
{
	const int32 Round = Generator ? Generator->GetRoundNumber() : 0;
	bool bAll = true;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		// Solo los clientes remotos: el anfitrión es el servidor y su ronda es la del generador.
		if (!PC || PC->IsLocalController() || !PC->GetNetConnection())
		{
			continue;
		}
		// En el sprint final solo corren las finalistas; las demás miran de espectadoras.
		if (bSprint && !IsSprintFinalist(PC))
		{
			continue;
		}
		const UTN_BeachRoundSyncComponent* Sync = UTN_BeachRoundSyncComponent::FindOrAddOn(PC);
		if (Sync && Sync->GetReadyRound() >= Round)
		{
			continue;
		}
		bAll = false;
		if (OutWaiting)
		{
			const APlayerState* WaitingState = PC->PlayerState;
			const FString Name = WaitingState ? WaitingState->GetPlayerName() : PC->GetName();
			*OutWaiting += OutWaiting->IsEmpty() ? Name : FString::Printf(TEXT(", %s"), *Name);
		}
	}
	return bAll;
}

void ATN_BeachRaceGameMode::GuardUnderSand(APlayerController* PlayerController, ATortugaCharacter* Turtle, float Now)
{
	if (!Generator || !Turtle || !PlayerController || !Generator->IsRoundReady() || TNBeachRaceGameModeDetail::CVarSafetyNet.GetValueOnGameThread() == 0)
	{
		return;
	}
	FTNBeachUnderSandWatch& Watch = UnderSandWatch.FindOrAdd(PlayerController);
	// La patada de la tormenta en vuelo es de la tormenta: la vigila ella y, si se hunde o no llega, la pone en su sitio.
	if (TNBeach::GetTurtleClaim(Turtle) == TNBeach::ETNBeachMover::StormKick)
	{
		Watch.Strikes = 0;
		Watch.FallNoFloorSince = -1.f;
		return;
	}
	FVector BodyVelocity = FVector::ZeroVector;
	const FVector Probe = TNUnderTerrain::BodyProbe(*Turtle, BodyVelocity);

	// Donde bajar de la arena es normal: el borde del acantilado y más allá (la pared está socavada y se cae al agua de
	// meta), las pozas (se nada) y nadando en cualquier agua.
	const UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
	const FVector Local = Generator->GetActorTransform().InverseTransformPosition(Probe);
	const bool bNearCliff = Generator->GetCliffEdgeDistance(Probe) > -TNBeachRaceGameModeDetail::CliffSkipDistance;
	const bool bInPool = TNBeachLayout::PoolAt(FVector2D(Local.X, Local.Y), 1.15) != INDEX_NONE;
	if (bNearCliff || bInPool || (Move && Move->IsSwimming()))
	{
		Watch.Strikes = 0;
		Watch.FallNoFloorSince = -1.f;
		return;
	}

	// 1) Por debajo de la arena del generador (terreno fijo con pozas, trincheras y los asientos de la ronda).
	const double Ground = Generator->GetGroundHeightAt(Probe);
	double Margin = UnderSandMargin;
	if (TNBeachLayout::TrenchCarve(Local.X, Local.Y) > 0.0)
	{
		Margin += TNBeachRaceGameModeDetail::TrenchExtraMargin;
	}
	double Depth = Ground - Probe.Z;
	bool bRescue = false;
	bool bSameSpot = true;
	FString Cause;
	if (Depth > Margin)
	{
		// La arena del generador no lo sabe todo (una depresión de la malla, lo que un elemento cava...): se confirma con la
		// malla del terreno de verdad en esa vertical. Encima de ella no está bajo el mapa (la bola rueda y sale sola).
		float TerrainZ = 0.f;
		if (Generator->TraceTerrainAt(Probe, TerrainZ))
		{
			Depth = static_cast<double>(TerrainZ) - Probe.Z;
		}
	}
	// Confirmado en dos miradas seguidas (0,1 s), o ya si está muy hondo: un fotograma de la física no cuenta (la misma regla
	// que la red de Coop y Clásico, TNUnderTerrain::RegisterLook).
	if (TNUnderTerrain::RegisterLook(Watch.Strikes, Depth, Margin))
	{
		bRescue = true;
		Cause = FString::Printf(TEXT("%.1f m bajo la arena"), Depth / 100.0);
	}
	else if (Depth <= Margin && Ground - Probe.Z > Margin && Now >= Watch.NextMismatchLog)
	{
		Watch.NextMismatchLog = Now + 10.f;
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Red de seguridad: %s %.1f m bajo la arena del generador en (%.1f, %.1f, %.1f) m, pero encima de la malla del terreno: no se rescata."),
			*GetNameSafe(Turtle), (Ground - Probe.Z) / 100.0, Probe.X / 100.0, Probe.Y / 100.0, Probe.Z / 100.0);
	}

	// 2) Cayendo (la cápsula, sin bola ni ragdoll) sin colisión de suelo debajo: ahí se atravesaría el mapa. Al último sitio
	// seguro (en ese punto no hay suelo que valga).
	const bool bFreeFall = Move && Move->IsFalling() && !Turtle->IsInShell() && !Turtle->IsKnockedDown() && BodyVelocity.Z < -200.0;
	if (!bRescue && bFreeFall && Depth <= Margin)
	{
		if (Watch.FallNoFloorSince < 0.f)
		{
			Watch.FallNoFloorSince = Now;
		}
		if (Now - Watch.FallNoFloorSince >= NoFloorFallSeconds && !HasFloorCollisionAt(Turtle, Probe))
		{
			bRescue = true;
			bSameSpot = false;
			Cause = TEXT("cayendo sin colisión de suelo debajo");
		}
	}
	else if (!bFreeFall)
	{
		Watch.FallNoFloorSince = -1.f;
	}

	if (!bRescue)
	{
		return;
	}
	Watch.Strikes = 0;
	Watch.FallNoFloorSince = -1.f;
	FString Driver;
	TNUnderTerrain::BodyProbe(*Turtle, BodyVelocity, &Driver);
	UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] Red de seguridad: %s %s en (%.1f, %.1f, %.1f) m (arena a %.1f m) · %s · velocidad %.0f cm/s (Z %.0f) · la movía %s."),
		*GetNameSafe(Turtle), *Cause, Probe.X / 100.0, Probe.Y / 100.0, Probe.Z / 100.0, Ground / 100.0, *TNBeachRaceGameModeDetail::DescribeTurtle(*Turtle),
		BodyVelocity.Size(), BodyVelocity.Z, *Driver);
	RescueFromUnderSand(PlayerController, Turtle, Probe, Cause, bSameSpot);
}

void ATN_BeachRaceGameMode::RescueFromUnderSand(APlayerController* PlayerController, ATortugaCharacter* Turtle, const FVector& Probe, const FString& Cause,
	bool bSameSpot)
{
	if (!PlayerController || !Turtle)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	FTNBeachUnderSandWatch& Watch = UnderSandWatch.FindOrAdd(PlayerController);
	// Rescates recientes: cuántos van en SafetyNetRepeatSeconds y si vuelve a ser el mismo hoyo. Cada sitio de rescate es
	// un sitio malo SafetyNetBadSpotSeconds (ni se vuelve a él ni se apunta como seguro).
	Watch.Rescues.RemoveAll([Now, this](const FTNBeachRescueMark& Mark) { return Now - Mark.Time > FMath::Max(SafetyNetBadSpotSeconds, SafetyNetRepeatSeconds); });
	const double BadSq = FMath::Square(static_cast<double>(SafetyNetBadSpotRadius));
	int32 Recent = 0;
	bool bSameHole = false;
	for (const FTNBeachRescueMark& Mark : Watch.Rescues)
	{
		if (Now - Mark.Time <= SafetyNetRepeatSeconds)
		{
			++Recent;
			bSameHole |= FVector::DistSquared2D(Mark.Where, Probe) < BadSq;
		}
	}
	FTNBeachRescueMark& NewMark = Watch.Rescues.AddDefaulted_GetRef();
	NewMark.Where = Probe;
	NewMark.Time = Now;
	TArray<FVector> BadSpots;
	for (const FTNBeachRescueMark& Mark : Watch.Rescues)
	{
		BadSpots.Add(Mark.Where);
	}
	ForgetSafeSpotsNear(PlayerController, BadSpots, SafetyNetBadSpotRadius);

	// Nivel: 0, encima de la arena en ese mismo punto (sin perder lo avanzado); 1 (la segunda vez en poco, el mismo hoyo o
	// cayendo sin suelo), su último sitio seguro lejos de los hoyos; 2 (de la tercera en adelante), arena abierta lejos de
	// todos ellos. Así nunca vuelve al borde del hoyo que la acaba de tragar.
	const int32 Level = Recent >= 2 ? 2 : ((Recent == 1 || bSameHole || !bSameSpot) ? 1 : 0);
	FString Where;
	const FTransform Target = ResolveRescueTarget(PlayerController, Turtle, Probe, Level, BadSpots, Where);
	if (Recent + 1 >= TNBeachRaceGameModeDetail::RescueLoopWarnCount && !Watch.bLoopReported)
	{
		Watch.bLoopReported = true;
		UE_LOG(LogTortunabo, Error, TEXT("[Carrera] Red de seguridad: %s rescatada %d veces en %.0f s cerca de (%.1f, %.1f) m: algo la sigue hundiendo ahí (ver las líneas de «la movía» de arriba)."),
			*GetNameSafe(Turtle), Recent + 1, SafetyNetRepeatSeconds, Probe.X / 100.0, Probe.Y / 100.0);
	}

	// Suelta de lo que la tenga: un enemigo (boca, pico), otra tortuga, el caparazón, el aturdimiento y el derribo.
	TNBeach::ReleaseTurtle(Turtle, TNBeach::ETNBeachMover::SafetyNet);
	TeleportTurtle(Turtle, Target);
	if (Level > 0)
	{
		// En otro sitio: la cámara, detrás de ella (en el mismo punto se deja como estaba).
		PlayerController->ClientSetRotation(Target.Rotator(), true);
	}
	if (ATN_CoopPlayerState* PS = PlayerController->GetPlayerState<ATN_CoopPlayerState>())
	{
		PS->DeathZoneTimeRemaining = -1.f;
	}
	// De pie (la corrección del movimiento lleva al dueño la posición nueva); con SafetyNetStunSeconds, una bola corta que
	// nace encima de la arena. Después, un momento reservada (nada la relanza) y sin patadas de la tormenta.
	if (SafetyNetStunSeconds > 0.f)
	{
		TNBeach::StunTurtle(Turtle, SafetyNetStunSeconds);
	}
	TNBeach::ClaimTurtle(Turtle, TNBeach::ETNBeachMover::SafetyNet, SafetyNetGraceSeconds);
	TNBeach::GrantStormGrace(Turtle, SafetyNetStormGraceSeconds);
	Turtle->ForceNetUpdate();
	const FVector At = Target.GetLocation();
	UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] Red de seguridad: %s vuelve %s (%.1f, %.1f, %.1f) m tras %s (rescate %d en %.0f s, nivel %d)%s."), *GetNameSafe(Turtle),
		*Where, At.X / 100.0, At.Y / 100.0, At.Z / 100.0, *Cause, Recent + 1, SafetyNetRepeatSeconds, Level,
		SafetyNetStunSeconds > 0.f ? *FString::Printf(TEXT(", en bola %.1f s"), SafetyNetStunSeconds) : TEXT(""));
}

FTransform ATN_BeachRaceGameMode::ResolveRescueTarget(APlayerController* PlayerController, ATortugaCharacter* Turtle, const FVector& Probe, int32 Level,
	const TArray<FVector>& BadSpots, FString& OutWhere)
{
	FTransform Target;
	bool bFound = false;
	if (Level <= 0 && Turtle && FindSandSpot(Turtle, Probe, Target))
	{
		OutWhere = TEXT("encima de la arena en");
		bFound = true;
	}
	// Su último sitio seguro (FindSafeTransform olvida lo de después: solo se pide si hace falta), en arena abierta de verdad
	// (no en lo que se mueve o se rompe) y lejos de los hoyos.
	FVector LastSafe = Probe;
	if (!bFound)
	{
		LastSafe = PutOnFloor(FindSafeTransform(PlayerController), GetDefaultHalfHeight()).GetLocation();
	}
	if (!bFound && Level <= 1 && Turtle)
	{
		bFound = TNBeach::FindOpenSandSpot(Turtle, LastSafe, TNBeachRaceGameModeDetail::RescueNearSearch, Target, &BadSpots, SafetyNetBadSpotRadius);
		OutWhere = TEXT("a su último sitio seguro en");
	}
	// Arena abierta lejos de todos los hoyos, alrededor de su último sitio seguro y, si no, de donde estaba.
	if (!bFound && Turtle)
	{
		const float Avoid = SafetyNetBadSpotRadius * 1.5f;
		bFound = TNBeach::FindOpenSandSpot(Turtle, LastSafe, TNBeachRaceGameModeDetail::RescueFarSearch, Target, &BadSpots, Avoid)
			|| TNBeach::FindOpenSandSpot(Turtle, Probe, TNBeachRaceGameModeDetail::RescueFarSearch, Target, &BadSpots, Avoid);
		OutWhere = TEXT("a arena abierta lejos del hoyo en");
	}
	if (!bFound)
	{
		Target = PutOnFloor(GetStartTransformFor(GetStartSlot(PlayerController)), GetDefaultHalfHeight());
		OutWhere = TEXT("a la salida (sin sitio seguro cerca) en");
	}
	// Dentro de la tormenta (o a menos de 5 m de su frente) la patearía en seguida: por delante del frente.
	ATN_BeachStorm* BeachStorm = Storm;
	FTransform Ahead;
	if (Turtle && BeachStorm && BeachStorm->IsBehindFront(Target.GetLocation(), -500.f) && BeachStorm->FindSpotAhead(Turtle, 0.5f, Ahead))
	{
		Target = Ahead;
		OutWhere += TEXT(" (por delante de la tormenta)");
	}
	return Target;
}

void ATN_BeachRaceGameMode::ForgetSafeSpotsNear(APlayerController* PlayerController, const TArray<FVector>& BadSpots, float Radius)
{
	TArray<FTNBeachSafeSpot>* Trail = SafeSpots.Find(PlayerController);
	if (!Trail || BadSpots.Num() == 0)
	{
		return;
	}
	const double RadiusSq = FMath::Square(static_cast<double>(Radius));
	Trail->RemoveAll([&BadSpots, RadiusSq](const FTNBeachSafeSpot& Spot)
	{
		return BadSpots.ContainsByPredicate([&Spot, RadiusSq](const FVector& Bad) { return FVector::DistSquared2D(Spot.Location, Bad) < RadiusSq; });
	});
}

bool ATN_BeachRaceGameMode::FindSandSpot(const ATortugaCharacter* Turtle, const FVector& Where, FTransform& OutTransform) const
{
	UWorld* World = GetWorld();
	if (!Generator || !World || !Turtle)
	{
		return false;
	}
	const double Ground = Generator->GetGroundHeightAt(Where);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TNBeachSafetyNet), false, Turtle);
	if (const UTN_ShellComponent* Shell = Turtle->GetShellComponent())
	{
		Query.AddIgnoredActor(Shell->GetBody());
	}
	// Lo primero que para a una tortuga desde 4 m por encima de la arena (la arena, o una roca o una pasarela encima).
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, FVector(Where.X, Where.Y, Ground + 400.0), FVector(Where.X, Where.Y, Ground - 200.0), ECC_Pawn, Query)
		|| Hit.ImpactNormal.Z < 0.6)
	{
		return false;
	}
	const float HalfHeight = GetDefaultHalfHeight();
	const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
	const float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 34.f;
	const FVector Stand = Hit.ImpactPoint + FVector(0.0, 0.0, HalfHeight + 5.0);
	// La tortuga de pie tiene que caber ahí: no dentro de una roca, una muralla o un castillo (la traza no ve lo que empieza
	// por dentro).
	if (World->OverlapBlockingTestByChannel(Stand, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius, HalfHeight), Query))
	{
		return false;
	}
	OutTransform = FTransform(FRotator(0.f, Turtle->GetActorRotation().Yaw, 0.f), Stand);
	return true;
}

bool ATN_BeachRaceGameMode::HasFloorCollisionAt(const ATortugaCharacter* Turtle, const FVector& Where) const
{
	UWorld* World = GetWorld();
	if (!Generator || !World)
	{
		return true;
	}
	// En la vertical, de 1,5 m por encima de la arena del generador a 3 m por debajo: el terreno (o lo que haya encima) tiene
	// que parar a una tortuga. Si no, en ese punto falta la colisión y se atravesaría el mapa.
	const double Ground = Generator->GetGroundHeightAt(Where);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TNBeachSafetyNetFloor), false, Turtle);
	FHitResult Hit;
	return World->LineTraceSingleByChannel(Hit, FVector(Where.X, Where.Y, Ground + 150.0), FVector(Where.X, Where.Y, Ground - 300.0), ECC_Pawn, Query);
}

double ATN_BeachRaceGameMode::GetVoidZ() const
{
	double VoidZ = FMath::Min(CourseOrigin.Z, LowestGroundZ) - VoidDepth;
	// Siempre por encima del KillZ del nivel, para llegar antes de que el motor destruya a la tortuga.
	if (const AWorldSettings* Settings = GetWorldSettings())
	{
		if (Settings->bEnableWorldBoundsChecks)
		{
			VoidZ = FMath::Max(VoidZ, static_cast<double>(Settings->KillZ) + 300.0);
		}
	}
	return VoidZ;
}

float ATN_BeachRaceGameMode::GetCourseProgress(const APawn* Pawn) const
{
	return Pawn ? static_cast<float>(FVector::DotProduct(Pawn->GetActorLocation() - CourseOrigin, CourseForward)) : 0.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tormenta de bañistas (ATN_BeachStorm)
// ─────────────────────────────────────────────────────────────────────────────

UClass* ATN_BeachRaceGameMode::GetStormClass() const
{
	return StormClass.Get();
}

void ATN_BeachRaceGameMode::StartStorm()
{
	if (!StormClass)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] Sin StormClass: se corre sin tormenta."));
		return;
	}
	StopStorm(true);

	// Detrás de la salida, mirando hacia el mar: avanza por donde van las tortugas.
	const FTransform SpawnAt(CourseForward.Rotation(), CourseOrigin - CourseForward * StormSpawnBehind);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Storm = GetWorld()->SpawnActor<ATN_BeachStorm>(StormClass, SpawnAt, Params);
	if (Storm)
	{
		Storm->StartStorm();
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Tormenta de bañistas en marcha (%s)."), *GetNameSafe(Storm));
	}
}

void ATN_BeachRaceGameMode::StopStorm(bool bDestroy)
{
	if (!Storm)
	{
		return;
	}
	// Al acabar la ronda se para y se queda a la vista durante el recuento; al preparar otra, fuera.
	if (bDestroy)
	{
		Storm->Destroy();
		Storm = nullptr;
	}
	else
	{
		Storm->StopStorm();
	}
}

int32 ATN_BeachRaceGameMode::HalvesOf(const APlayerState* PlayerState)
{
	return ATN_BeachRaceGameState::GetShellHalves(PlayerState);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pruebas
// ─────────────────────────────────────────────────────────────────────────────

APlayerController* ATN_BeachRaceGameMode::GetControllerByIndex(int32 PlayerIndex) const
{
	if (!GameState || !GameState->PlayerArray.IsValidIndex(PlayerIndex))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] No hay jugador %d (hay %d)."), PlayerIndex, GameState ? GameState->PlayerArray.Num() : 0);
		return nullptr;
	}
	const APlayerState* PS = GameState->PlayerArray[PlayerIndex];
	return PS ? PS->GetPlayerController() : nullptr;
}

void ATN_BeachRaceGameMode::DebugWinRound(int32 PlayerIndex, int32 ForcedPlace)
{
	if (!bRoundActive)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] TN.Race.WinRound: no hay ronda en marcha (fase %s)."), *UEnum::GetValueAsString(GetRacePhase()));
		return;
	}
	if (APlayerController* PC = GetControllerByIndex(PlayerIndex))
	{
		// Con puesto, solo la pantalla del puesto lo enseña (las conchas van por el orden de llegada de verdad).
		DebugForcedPlace = FMath::Clamp(ForcedPlace, 0, 8);
		MarkPlayerFinished(PC);
		DebugForcedPlace = 0;
		if (ForcedPlace > 0)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[Carrera] TN.Race.WinRound: %s llega con la pantalla del %d.º puesto."), *GetNameSafe(PC), FMath::Clamp(ForcedPlace, 1, 8));
		}
	}
}

void ATN_BeachRaceGameMode::DebugNextRound()
{
	if (bMatchOver || bLeaving)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] TN.Race.NextRound: la partida ha acabado (pantalla del campeón: TN.Race.PlayAgain)."));
		return;
	}
	const ETNBeachRacePhase Phase = GetRacePhase();
	if (Phase == ETNBeachRacePhase::RoundResults)
	{
		// Sin esperar al final del recuento: la ronda siguiente (o el campeón, o el sprint).
		GetWorldTimerManager().ClearTimer(PhaseEndHandle);
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] TN.Race.NextRound: se salta el resto del recuento de la ronda %d."), CurrentRound);
		AfterRoundResults();
		return;
	}
	if (Phase == ETNBeachRacePhase::SprintIntro)
	{
		GetWorldTimerManager().ClearTimer(PhaseEndHandle);
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] TN.Race.NextRound: se salta el título del sprint final."));
		StartSprint();
		return;
	}
	if (bSprint)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] TN.Race.NextRound: en el sprint final no hay ronda siguiente (TN.Race.WinRound para ganarlo)."));
		return;
	}
	if (bRoundActive || bTimeUp)
	{
		// La ronda se cierra ya, con las conchas de quien haya llegado, y sale su recuento.
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] TN.Race.NextRound: se cierra la ronda %d (%d en el agua)."), CurrentRound, Arrivals.Num());
		EndRound(Arrivals.Num() > 0 ? FString::Printf(TEXT("¡%s gana la ronda!"), *Arrivals[0].Name) : FString(TEXT("Ronda saltada: nadie gana")));
		return;
	}
	UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] TN.Race.NextRound: la ronda %d se está preparando (fase %s); espera a que empiece."), CurrentRound,
		*UEnum::GetValueAsString(Phase));
}

void ATN_BeachRaceGameMode::DebugChampion(int32 PlayerIndex)
{
	if (bLeaving)
	{
		return;
	}
	APlayerController* PC = GetControllerByIndex(PlayerIndex);
	ATN_CoopPlayerState* PS = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
	if (!PS)
	{
		return;
	}
	PS->RaceShellHalves = FMath::Max(PS->RaceShellHalves, WinsToWinMatch * 2);
	LastRoundWonByPlayer.Add(PS->GetPlayerId(), CurrentRound);
	if (ATN_BeachRaceGameState* BeachState = GetBeachGameState())
	{
		BeachState->RoundWinner = PS;
		BeachState->RoundHalfShells.Reset();
	}
	EnterChampion(PS);
}

void ATN_BeachRaceGameMode::DebugSprint(const TArray<int32>& PlayerIndices)
{
	if (bLeaving)
	{
		return;
	}
	TArray<ATN_CoopPlayerState*> Finalists;
	for (const int32 PlayerIndex : PlayerIndices)
	{
		APlayerController* PC = GetControllerByIndex(PlayerIndex);
		if (ATN_CoopPlayerState* PS = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr)
		{
			Finalists.AddUnique(PS);
		}
	}
	if (Finalists.Num() == 0)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] TN.Race.Sprint: ninguno de esos jugadores existe."));
		return;
	}
	// Empate forzado: las elegidas, justo en las conchas del campeón; las demás, por debajo.
	const int32 TargetHalves = WinsToWinMatch * 2;
	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		if (ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS))
		{
			PS->RaceShellHalves = Finalists.Contains(PS) ? TargetHalves : FMath::Min(PS->RaceShellHalves, TargetHalves - 1);
		}
	}
	// Lo que hubiera en marcha (carrera, recuento o podio) se corta: el título del sprint sale ya.
	bMatchOver = false;
	bSprint = false;
	Arrivals.Reset();
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] TN.Race.Sprint: empate forzado entre %d."), Finalists.Num());
	EnterSprintIntro(Finalists);
}

void ATN_BeachRaceGameMode::DebugKill(int32 PlayerIndex)
{
	if (APlayerController* PC = GetControllerByIndex(PlayerIndex))
	{
		MarkPlayerDead(PC);
	}
}

void ATN_BeachRaceGameMode::DebugVoid(int32 PlayerIndex)
{
	APlayerController* PC = GetControllerByIndex(PlayerIndex);
	ATortugaCharacter* Turtle = PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr;
	if (!Turtle || !bRoundActive)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] TN.Race.Void: hace falta una tortuga en plena carrera."));
		return;
	}
	const FVector Location = Turtle->GetActorLocation();
	TeleportTurtle(Turtle, FTransform(Turtle->GetActorRotation(), FVector(Location.X, Location.Y, GetVoidZ() - 1000.0)));
}

void ATN_BeachRaceGameMode::DebugStun(int32 PlayerIndex, float Seconds)
{
	APlayerController* PC = GetControllerByIndex(PlayerIndex);
	if (ACharacter* Character = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr)
	{
		TNBeach::StunTurtle(Character, Seconds);
	}
}

void ATN_BeachRaceGameMode::DebugSetRoundTimeLeft(float Seconds)
{
	if (!bRoundActive || RoundClockEndTime < 0.f)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] TN.Race.TimeLeft: hace falta una ronda en marcha con su tiempo contando (nadie en el agua todavía)."));
		return;
	}
	const float Left = FMath::Max(1.f, Seconds);
	GetWorldTimerManager().SetTimer(RoundTimeLimitHandle, this, &ATN_BeachRaceGameMode::OnRoundTimeLimit, Left, false);
	RoundClockEndTime = GetWorld()->GetTimeSeconds() + Left;
	RoundTimeWarningsLogged = 0;
	if (ATN_BeachRaceGameState* BeachState = GetBeachGameState())
	{
		BeachState->RoundEndServerTime = static_cast<float>(BeachState->GetServerWorldTimeSeconds()) + Left;
		BeachState->ForceNetUpdate();
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] TN.Race.TimeLeft: quedan %.0f s de ronda."), Left);
}

void ATN_BeachRaceGameMode::DebugBury(int32 PlayerIndex, float Meters)
{
	APlayerController* PC = GetControllerByIndex(PlayerIndex);
	ATortugaCharacter* Turtle = PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr;
	if (!Turtle || !bRoundActive || !Generator)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] TN.Race.Bury: hace falta una tortuga en plena carrera."));
		return;
	}
	// Bajo la arena de ese punto, cayendo: la red de seguridad (GuardUnderSand) la tiene que devolver encima en seguida.
	const FVector Location = Turtle->GetActorLocation();
	const double Ground = Generator->GetGroundHeightAt(Location);
	const float HalfHeight = Turtle->GetCapsuleComponent() ? Turtle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : GetDefaultHalfHeight();
	TeleportTurtle(Turtle, FTransform(Turtle->GetActorRotation(), FVector(Location.X, Location.Y, Ground - FMath::Max(0.5f, Meters) * 100.0 + HalfHeight)));
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] TN.Race.Bury: %s, %.1f m bajo la arena."), *GetNameSafe(Turtle), FMath::Max(0.5f, Meters));
}
