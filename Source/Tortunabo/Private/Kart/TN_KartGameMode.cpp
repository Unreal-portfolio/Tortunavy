#include "Kart/TN_KartGameMode.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kart/TN_KartAIController.h"
#include "Kart/TN_KartBuggy.h"
#include "Vehicles/TN_Buggy.h"
#include "Kart/TN_KartItemComponent.h"
#include "Kart/TN_KartGameState.h"
#include "Kart/TN_KartPlayerController.h"
#include "Kart/TN_KartTrack.h"
#include "Kismet/GameplayStatics.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Rally/TN_RallyAIController.h"
#include "Rally/TN_RallyLogic.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "World/ProcMap/TN_ProcMapTypes.h"
#include "World/ProcMap/TN_ProcMapLayout.h"
#include "World/ProcMap/TN_ProcTraversalActors.h"
#include "Components/SkeletalMeshComponent.h"
#include "TimerManager.h"

namespace TNKartMode
{
	TAutoConsoleVariable<int32> CVarKartBots(TEXT("TN.Kart.Bots"), -1,
		TEXT("Karts: bots de la parrilla (-1 = los que falten hasta MinKarts karts; ?Bots= en la URL manda). Vale para la próxima partida."));

	TAutoConsoleVariable<int32> CVarKartProbeArc(TEXT("TN.Kart.ProbeArc"), 0,
		TEXT("Karts (diagnóstico, LogTNRally Verbose): perfil del suelo a lo ancho del camino alrededor de este arco (m) al empezar."));

	TAutoConsoleVariable<int32> CVarKartSeats(TEXT("TN.Kart.Seats"), 0,
		TEXT("Karts: tortugas por kart (1 o 2; 0 = las de por defecto; ?Seats= en la URL manda). Vale para la próxima partida."));

	const TCHAR* const DefaultSettingsPath = TEXT("/Game/ProcMap/DA_ProcMapSettings.DA_ProcMapSettings");
	const TCHAR* const DefaultLobbyPath = TEXT("/Game/Maps/Lobby/LVL_Lobby");
	/** Cada cuánto se mira si todas tienen el mapa (s). */
	constexpr double ReadyCheckIntervalSeconds = 0.5;

	int32 DifficultyIndex(ETNProcDifficulty Difficulty)
	{
		return FMath::Clamp(static_cast<int32>(Difficulty), 0, 2);
	}
}

ATN_KartGameMode::ATN_KartGameMode()
{
	GameStateClass = ATN_KartGameState::StaticClass();
	PlayerControllerClass = ATN_KartPlayerController::StaticClass();
	AIControllerClass = ATN_KartAIController::StaticClass();
	// El buggy de SkiTemplar con los objetos, la artillera que se inclina y la torreta que sigue a la cámara.
	VehicleClass = TSoftClassPtr<APawn>(ATN_KartBuggy::StaticClass());
	// Sin variante del manifest: la pista sale del mapa generado (ATN_KartGameState::PrepareTrack).
	DefaultVariant = NAME_None;
	// Del lobby se llega y al lobby se vuelve sin cortar la conexión.
	bUseSeamlessTravel = true;
	// Pistas de varios kilómetros con cajas y conchas: más margen para llegar tras la primera que en el Rally.
	FinishGraceSeconds = 45.f;
	MapSettings = TSoftObjectPtr<UTN_ProcMapSettings>(FSoftObjectPath(TNKartMode::DefaultSettingsPath));
}

void ATN_KartGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	// La dificultad del lobby (como en el cooperativo); ?ProcDifficulty= manda para probar sin lobby.
	const UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance());
	if (GI)
	{
		Difficulty = GI->SelectedProcDifficulty;
		ExpectedHumans = FMath::Max(1, GI->PendingTravelPlayerCount);
	}
	const FString DifficultyOption = UGameplayStatics::ParseOption(Options, TEXT("ProcDifficulty"));
	if (DifficultyOption.Equals(TEXT("Easy"), ESearchCase::IgnoreCase)) { Difficulty = ETNProcDifficulty::Easy; }
	else if (DifficultyOption.Equals(TEXT("Normal"), ESearchCase::IgnoreCase)) { Difficulty = ETNProcDifficulty::Normal; }
	else if (DifficultyOption.Equals(TEXT("Hard"), ESearchCase::IgnoreCase)) { Difficulty = ETNProcDifficulty::Hard; }
	if (Difficulty == ETNProcDifficulty::Count)
	{
		Difficulty = ETNProcDifficulty::Normal;
	}
	UrlSeed = UGameplayStatics::GetIntOption(Options, TEXT("ProcSeed"), 0);
	bReturnToLobbyAfterResults = !UGameplayStatics::HasOption(Options, TEXT("Races"));

	// Plazas y bots: lo que no diga la URL lo pone el modo (la carrera del Rally lo lee de las opciones).
	FString KartOptions = Options;
	int32 KartSeats = FMath::Clamp(DefaultSeats, 1, 2);
	if (UGameplayStatics::HasOption(Options, TEXT("Seats")))
	{
		KartSeats = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Seats"), KartSeats), 1, 2);
	}
	else
	{
		// Lo que eligió el anfitrión con el general; TN.Kart.Seats manda para probar.
		KartSeats = GI ? FMath::Clamp(GI->SelectedKartSeats, 1, 2) : KartSeats;
		const int32 Forced = TNKartMode::CVarKartSeats.GetValueOnGameThread();
		KartSeats = Forced == 1 || Forced == 2 ? Forced : KartSeats;
		KartOptions += FString::Printf(TEXT("?Seats=%d"), KartSeats);
	}
	if (!UGameplayStatics::HasOption(Options, TEXT("Bots")))
	{
		const int32 Forced = TNKartMode::CVarKartBots.GetValueOnGameThread();
		const int32 HumanKarts = FMath::DivideAndRoundUp(ExpectedHumans, KartSeats);
		const int32 KartBots = Forced >= 0 ? FMath::Min(Forced, TNRally::MaxGridSlots)
			: FMath::Clamp(MinKarts - HumanKarts, 0, TNRally::MaxGridSlots - HumanKarts);
		KartOptions += FString::Printf(TEXT("?Bots=%d"), KartBots);
	}
	UE_LOG(LogTNRally, Log, TEXT("[Karts] Karts en el mapa del cooperativo: dificultad %s, semilla %s, %d tortuga(s) esperada(s), %d por kart."),
		*UEnum::GetValueAsString(Difficulty), UrlSeed != 0 ? *FString::FromInt(UrlSeed) : TEXT("aleatoria"), ExpectedHumans, KartSeats);
	Super::InitGame(MapName, KartOptions, ErrorMessage);
}

ATN_KartGameState* ATN_KartGameMode::GetKartState() const
{
	return GetGameState<ATN_KartGameState>();
}

ATN_ProcMapGenerator* ATN_KartGameMode::EnsureGenerator()
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
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Generator = GetWorld()->SpawnActor<ATN_ProcMapGenerator>(ATN_ProcMapGenerator::StaticClass(), FTransform::Identity, Params);
		UE_LOG(LogTNRally, Log, TEXT("[Karts] El nivel no tenía generador: creado %s."), *GetNameSafe(Generator));
	}
	if (Generator)
	{
		Generator->SetSettingsIfMissing(MapSettings.LoadSynchronous());
	}
	return Generator;
}

ATN_ProcMapGenerator* ATN_KartGameMode::GenerateMap()
{
	ATN_ProcMapGenerator* Map = EnsureGenerator();
	if (!Map)
	{
		return nullptr;
	}
	// El mismo mapa del cooperativo con el camino hecho para el kart (el generador lo sabe por el modo, que se replica).
	MapSeed = UrlSeed != 0 ? UrlSeed : (FixedSeed != 0 ? FixedSeed : FMath::RandRange(1, MAX_int32 - 1));
	Map->ServerGenerate(MapSeed, ETNProcGameMode::Karts, Difficulty);
	WaitStartTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogTNRally, Log, TEXT("[Karts] Mapa %d generado (semilla %d, %s)."), Map->GetBuiltGeneration(), MapSeed,
		*UEnum::GetValueAsString(Difficulty));
	return Map->IsMapReady() ? Map : nullptr;
}

void ATN_KartGameMode::ConfigureBot(ATN_RallyAIController& Pilot)
{
	const int32 Index = TNKartMode::DifficultyIndex(Difficulty);
	// Un poco de variedad entre bots: ±4 km/h alrededor de la velocidad de la dificultad.
	const float Spread = static_cast<float>((NextBotOrdinal++ % 3) - 1) * 4.f;
	Pilot.MaxSpeedKmh = static_cast<float>(BotMaxSpeedKmh[Index]) + Spread;
	Pilot.SpecialFireChance = FMath::Clamp(static_cast<float>(BotSpecialFireChance[Index]), 0.f, 1.f);
	Pilot.FireIntervalSeconds = FMath::Max(0.2f, static_cast<float>(BotFireIntervalSeconds[Index]));
	Pilot.FireRangeCm = FMath::Max(500.f, static_cast<float>(BotFireRangeCm[Index]));
}

void ATN_KartGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Nadie se sienta hasta que todas tienen el mapa y el suelo en su máquina: así salen a la vez y ningún kart cae.
	if (!bPlayersReleased && NewPlayer)
	{
		WaitingPlayers.AddUnique(NewPlayer);
		UE_LOG(LogTNRally, Log, TEXT("[Karts] %s espera a que todas tengan el mapa."), *GetNameSafe(NewPlayer));
		return;
	}
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void ATN_KartGameMode::NotifyClientTrackReady(APlayerController* Player, int32 Generation)
{
	if (!Player)
	{
		return;
	}
	ClientTrackGeneration.Add(Player, Generation);
	UE_LOG(LogTNRally, Log, TEXT("[Karts] %s tiene la pista y el suelo de la generación %d."), *GetNameSafe(Player), Generation);
}

bool ATN_KartGameMode::AreAllPlayersReady() const
{
	const ATN_KartGameState* KartState = GetKartState();
	if (!KartState || !KartState->IsLocalTrackPlayable())
	{
		return false;
	}
	int32 Connected = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Player = It->Get();
		if (!Player)
		{
			continue;
		}
		++Connected;
		// El anfitrión usa la pista del servidor.
		if (Player->IsLocalController())
		{
			continue;
		}
		const int32* Built = ClientTrackGeneration.Find(It->Get());
		if (!Built || *Built != KartState->MapGeneration)
		{
			return false;
		}
	}
	return Connected >= ExpectedHumans;
}

void ATN_KartGameMode::ReleaseWaitingPlayers(const TCHAR* Why)
{
	bPlayersReleased = true;
	TArray<TWeakObjectPtr<APlayerController>> Waiting = MoveTemp(WaitingPlayers);
	WaitingPlayers.Reset();
	UE_LOG(LogTNRally, Log, TEXT("[Karts] %d tortuga(s) a los karts: %s."), Waiting.Num(), Why);
	for (const TWeakObjectPtr<APlayerController>& Player : Waiting)
	{
		if (Player.IsValid())
		{
			Super::HandleStartingNewPlayer_Implementation(Player.Get());
		}
	}
}

void ATN_KartGameMode::Tick(float DeltaSeconds)
{
	const UWorld* World = GetWorld();
	const ATN_KartGameState* KartState = GetKartState();
	if (World && KartState && !bPlayersReleased && HasActorBegunPlay() && World->GetTimeSeconds() >= NextReadyCheckTime)
	{
		NextReadyCheckTime = World->GetTimeSeconds() + TNKartMode::ReadyCheckIntervalSeconds;
		if (AreAllPlayersReady())
		{
			ReleaseWaitingPlayers(TEXT("todas tienen el mapa"));
		}
		else if (WaitStartTime >= 0.0 && World->GetTimeSeconds() - WaitStartTime > PlayersReadyTimeoutSeconds)
		{
			ReleaseWaitingPlayers(TEXT("tope de espera"));
		}
	}
	// Fin de los resultados: al lobby, antes de que el Rally empiece otra carrera en el mismo mapa.
	if (bReturnToLobbyAfterResults && KartState && KartState->Phase == ETNRallyPhase::Results
		&& KartState->GetServerWorldTimeSeconds() >= KartState->PhaseEndServerTime - LobbyTravelLeadSeconds)
	{
		ReturnToLobbyNow();
	}
	if (!bReturning)
	{
		Super::Tick(DeltaSeconds);
		if (UE_LOG_ACTIVE(LogTNRally, Verbose))
		{
			LogStartDiagnostics();
		}
	}
}

void ATN_KartGameMode::LogStartDiagnostics()
{
	const ATN_KartGameState* KartState = GetKartState();
	const ATN_KartTrack* KartTrack = KartState ? KartState->GetKartTrack() : nullptr;
	const bool bCountdown = KartState && KartState->Phase == ETNRallyPhase::Countdown;
	if (!KartTrack || (KartState->Phase != ETNRallyPhase::Racing && !bCountdown))
	{
		return;
	}
	const double Now = KartState->GetServerWorldTimeSeconds();
	if (Now - KartState->StartServerTime > 20.0 || Now < NextStartLogTime)
	{
		return;
	}
	// Durante el semáforo y los primeros 3 s, cada medio segundo (salida a la vez); luego, cada segundo.
	NextStartLogTime = Now + (Now - KartState->StartServerTime < 3.0 ? 0.5 : 1.0);
	static const IConsoleVariable* ProbeVar = IConsoleManager::Get().FindConsoleVariable(TEXT("TN.Kart.ProbeArc"));
	const int32 ProbeArcM = ProbeVar ? ProbeVar->GetInt() : 0;
	if (ProbeArcM > 0 && Now - KartState->StartServerTime < 1.5)
	{
		// Perfil del suelo a lo ancho del camino cada 4 m alrededor del arco pedido (diagnóstico de atascos).
		ATN_ProcMapGenerator* Map = EnsureGenerator();
		for (double Arc = (ProbeArcM - 40) * 100.0; Arc <= (ProbeArcM + 40) * 100.0; Arc += 400.0)
		{
			const FVector OnLine = KartTrack->GetLocationAtArc(Arc);
			const FVector Dir = KartTrack->GetDirectionAtArc(Arc);
			const FVector Right(-Dir.Y, Dir.X, 0.0);
			FString Row;
			for (int32 Side = -12; Side <= 12; Side += 3)
			{
				const FVector At = OnLine + Right * (Side * 100.0);
				FHitResult Down;
				const bool bDown = GetWorld()->LineTraceSingleByChannel(Down, At + FVector(0.0, 0.0, 1500.0), At - FVector(0.0, 0.0, 3000.0), ECC_WorldStatic);
				Row += bDown ? FString::Printf(TEXT(" %+.0f%s"), Down.ImpactPoint.Z - OnLine.Z,
					Down.GetActor() && Down.GetActor() != Map ? TEXT("*") : TEXT("")) : FString(TEXT(" ?"));
			}
			UE_LOG(LogTNRally, Verbose, TEXT("[Karts] perfil arco %.0f m (línea a %.0f, terreno %.0f):%s"), Arc / 100.0, OnLine.Z,
				Map ? Map->GetTerrainHeightAt(OnLine) : 0.f, *Row);
		}
	}
	for (const FTNRallyStanding& Entry : KartState->Standings)
	{
		if (const APawn* Kart = Entry.Vehicle)
		{
			const FVector At = Kart->GetActorLocation();
			// Lo que tiene delante a media altura (qué lo para).
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(TNKartDiag), false, Kart);
			const FVector Ahead = Kart->GetActorForwardVector().GetSafeNormal2D();
			const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, At + FVector(0.0, 0.0, 15.0), At + FVector(0.0, 0.0, 15.0) + Ahead * 600.0,
				ECC_Visibility, Params);
			UE_LOG(LogTNRally, Verbose, TEXT("[Karts] %.0f s: equipo %d en (%.0f, %.0f, %.0f) a %.0f km/h, arco %.0f m, %.1f m de la línea, arriba.Z %.2f; delante: %s/%s a %.1f m."),
				Now - KartState->StartServerTime, Entry.TeamIndex, At.X, At.Y, At.Z, Kart->GetVelocity().Size() * 0.036,
				KartTrack->FindArcGlobal(At) / 100.0, FVector::Dist2D(At, KartTrack->GetLocationAtArc(KartTrack->FindArcGlobal(At))) / 100.0,
				Kart->GetActorUpVector().Z, bHit ? *GetNameSafe(Hit.GetActor()) : TEXT("-"), bHit ? *GetNameSafe(Hit.GetComponent()) : TEXT("-"),
				bHit ? Hit.Distance / 100.0 : 0.0);
		}
	}
}

void ATN_KartGameMode::Logout(AController* Exiting)
{
	APlayerController* Player = Cast<APlayerController>(Exiting);
	WaitingPlayers.Remove(Player);
	ClientTrackGeneration.Remove(Player);
	Super::Logout(Exiting);
}

void ATN_KartGameMode::ReturnToLobbyNow()
{
	UWorld* World = GetWorld();
	if (bReturning || !World || World->IsInSeamlessTravel())
	{
		return;
	}
	bReturning = true;
	// Peones fuera antes del viaje (karts, peones de artillera y espectadores): al lobby no llega nada de los karts.
	TArray<APawn*> Pawns;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		Pawns.Add(*It);
	}
	for (APawn* Pawn : Pawns)
	{
		if (IsValid(Pawn))
		{
			Pawn->Destroy();
		}
	}
	// Al lobby del que se salió (lo apunta ATN_HQGameMode); «?game=» quita el alias de los karts de la URL.
	const UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance());
	const FString Lobby = GI && !GI->LobbyReturnMapPath.IsEmpty() ? GI->LobbyReturnMapPath : FString(TNKartMode::DefaultLobbyPath);
	const FString TravelURL = Lobby + TEXT("?game=");
	UE_LOG(LogTNRally, Log, TEXT("[Karts] Vuelta al lobby: %s"), *TravelURL);
	World->ServerTravel(TravelURL);
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs GTNKartGiveItemCommand(
	TEXT("TN.Kart.GiveItem"),
	TEXT("Karts (servidor o partida sola): da un objeto al kart del jugador local, sin ruleta. TN.Kart.GiveItem Coco|TripleCoco|Concha|ConchaGuiada|Alga|Tinta|Estrella."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
		const ATN_RallyGameState* RallyState = World ? World->GetGameState<ATN_RallyGameState>() : nullptr;
		const FTNRallyStanding* Mine = RallyState && Player ? RallyState->FindStandingForPlayer(Player->PlayerState) : nullptr;
		UTN_KartItemComponent* Items = Mine && Mine->Vehicle ? Mine->Vehicle->FindComponentByClass<UTN_KartItemComponent>() : nullptr;
		if (!Items || !Items->GetOwner()->HasAuthority() || Args.Num() < 1)
		{
			UE_LOG(LogTNRally, Display, TEXT("TN.Kart.GiveItem: hace falta un kart propio en el servidor y el nombre del objeto."));
			return;
		}
		const int64 Value = StaticEnum<ETNKartItem>()->GetValueByNameString(Args[0]);
		if (Value == INDEX_NONE || Value <= 0 || Value >= static_cast<int64>(ETNKartItem::Count))
		{
			UE_LOG(LogTNRally, Display, TEXT("TN.Kart.GiveItem: no existe el objeto '%s'."), *Args[0]);
			return;
		}
		Items->GiveItem(static_cast<ETNKartItem>(Value), true);
		UE_LOG(LogTNRally, Display, TEXT("TN.Kart.GiveItem: %s."), *Args[0]);
	}));

namespace TNKartDebug
{
	/** Coloca el kart del equipo TeamIndex (-1 = el del jugador local) junto a un géiser, en lo alto de una cascada o en el agua. */
	void PlaceKart(UWorld* World, const FString& What, int32 TeamIndex)
	{
		const ATN_RallyGameState* RallyState = World ? World->GetGameState<ATN_RallyGameState>() : nullptr;
		const APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
		ATN_Buggy* Kart = nullptr;
		for (const FTNRallyStanding& Entry : RallyState ? RallyState->Standings : TArray<FTNRallyStanding>())
		{
			const bool bMine = TeamIndex < 0 && Player && (Entry.Driver == Player->PlayerState || Entry.Gunner == Player->PlayerState);
			if (bMine || Entry.TeamIndex == TeamIndex)
			{
				Kart = Cast<ATN_Buggy>(Entry.Vehicle);
			}
		}
		if (!Kart || !Kart->HasAuthority())
		{
			UE_LOG(LogTNRally, Display, TEXT("TN.Kart.Place: no hay kart del equipo %d en el servidor."), TeamIndex);
			return;
		}
		FVector Where = FVector::ZeroVector;
		FVector Facing = Kart->GetActorForwardVector();
		float Speed = 0.f;
		bool bFound = false;
		if (What.Equals(TEXT("Geyser"), ESearchCase::IgnoreCase))
		{
			for (TActorIterator<ATN_ProcGeyser> It(World); It && !bFound; ++It)
			{
				if (!It->IsShaft())
				{
					// Encima de la boca, mirando a la cima: sale lanzado al momento.
					Where = It->GetActorLocation();
					Facing = (It->GetTarget() - Where).GetSafeNormal2D();
					bFound = true;
				}
			}
		}
		else if (What.Equals(TEXT("Cascada"), ESearchCase::IgnoreCase))
		{
			for (TActorIterator<ATN_ProcSlideZone> It(World); It && !bFound; ++It)
			{
				FVector Flow;
				if (It->GetTop(Where, Flow))
				{
					// En el labio, hacia abajo y con algo de velocidad.
					Facing = Flow.GetSafeNormal2D();
					Where -= Facing * 300.0;
					Speed = 500.f;
					bFound = true;
				}
			}
		}
		else if (What.Equals(TEXT("Agua"), ESearchCase::IgnoreCase))
		{
			TArray<FTNProcPathPoint> Points;
			for (TActorIterator<ATN_ProcMapGenerator> It(World); It; ++It)
			{
				It->GetMainPathWorld(Points);
				break;
			}
			for (int32 Index = 1; Index < Points.Num() && !bFound; ++Index)
			{
				if ((Points[Index].Flags & TNProcMap::PathFlags::Islet) != 0)
				{
					Where = Points[FMath::Min(Index + 3, Points.Num() - 1)].Location;
					Facing = Points[Index].Direction;
					bFound = true;
				}
			}
			for (TActorIterator<ATN_ProcSlideZone> It(World); It && !bFound; ++It)
			{
				float Radius = 0.f;
				bFound = It->GetPool(Where, Radius);
			}
		}
		if (!bFound)
		{
			UE_LOG(LogTNRally, Display, TEXT("TN.Kart.Place: este mapa no tiene %s."), *What);
			return;
		}
		Kart->RallyTeleport(FTransform(FRotator(0.0, Facing.Rotation().Yaw, 0.0), Where + FVector(0.0, 0.0, 50.0)), 0.f, 0.f);
		if (USkeletalMeshComponent* Chassis = Kart->GetMesh(); Chassis && Speed > 0.f)
		{
			Chassis->SetPhysicsLinearVelocity(Facing * Speed);
		}
		UE_LOG(LogTNRally, Display, TEXT("TN.Kart.Place: %s (equipo %d) en %s, en (%.0f, %.0f, %.0f)."), *Kart->GetName(), TeamIndex, *What,
			Where.X, Where.Y, Where.Z);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTNKartPlaceCommand(
	TEXT("TN.Kart.Place"),
	TEXT("Karts (servidor o partida sola, pruebas): TN.Kart.Place Geyser|Cascada|Agua [equipo, -1 = el tuyo] [segundos de espera]. Coloca el kart encima de un géiser, en lo alto de una cascada o en el agua."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogTNRally, Display, TEXT("TN.Kart.Place Geyser|Cascada|Agua [equipo] [segundos]"));
			return;
		}
		const FString What = Args[0];
		const int32 TeamIndex = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : -1;
		const float Delay = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.f;
		if (Delay <= 0.f)
		{
			TNKartDebug::PlaceKart(World, What, TeamIndex);
			return;
		}
		FTimerHandle Handle;
		TWeakObjectPtr<UWorld> WeakWorld(World);
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, What, TeamIndex]()
		{
			TNKartDebug::PlaceKart(WeakWorld.Get(), What, TeamIndex);
		}), Delay, false);
	}));
#endif
