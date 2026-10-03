#include "UI/Race/TN_RaceScreens.h"
#include "TN_RaceArrivalArt.h"
#include "TN_RaceArt.h"
#include "../../Audio/TN_MatchMusicSubsystem.h"
#include "Audio/TN_MusicSynthComponent.h"
#include "Blueprint/UserWidget.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "UI/HUD/TN_GhostHatchWidget.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Stats/Stats.h"
#include "VR/TN_VRMode.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto
// del bloque.
namespace TNRaceScreensDetail
{
	/** Capas de la pantalla: por encima del HUD (4, 5 y 10) y por debajo de las ruedas (30) y los menús (40). */
	constexpr int32 RoundClockZOrder = 14;
	constexpr int32 CountdownZOrder = 15;
	constexpr int32 TallyZOrder = 20;
	constexpr int32 ChampionZOrder = 21;
	constexpr int32 SprintZOrder = 22;

	/**
	 * La música de victoria del podio se pide pasado este tiempo de la fase: para entonces el director de la música de
	 * fin de partida (UTN_MatchMusicSubsystem, Results) ya ha fijado su resultado (1,6 s) y no la pisa con la derrota.
	 */
	constexpr float VictoryDelay = 2.2f;

	/** Segundos que dura la vista previa del recuento (con la cuenta atrás de mentira). */
	constexpr float PreviewTallySeconds = 8.f;

	/** Vistas previas del título del sprint y de la cuenta atrás tras la primera en el agua (con su «¡TIEMPO!»). */
	constexpr float PreviewSprintSeconds = 7.f;
	constexpr float PreviewCountdownSeconds = 10.f;
	constexpr float PreviewTimeUpSeconds = 1.8f;

	/** El reloj de la ronda sale cuando quedan estos segundos (el último minuto; con medio segundo de margen por la red). */
	constexpr float RoundClockShowSeconds = 60.5f;

	/**
	 * Cáscara de la llegada al agua: lo que tarda en cerrarse (rápido, según se zambulle), lo que se espera el puesto del
	 * servidor (si no llega, no era una llegada), lo más que puede quedarse cerrada y lo que se espera tapado al recuento
	 * cuando ya no queda nadie corriendo (el servidor lo retrasa hasta que acaba la pantalla del puesto).
	 */
	constexpr float ArrivalCloseSeconds = 0.28f;
	constexpr float ArrivalConfirmSeconds = 1.6f;
	constexpr float ArrivalMaxHoldSeconds = 14.f;
	constexpr float ArrivalTallyWaitSeconds = 2.5f;

	/** Cáscara del paso entre rondas: cierre y lo más que puede quedarse cerrada (la preparación espera como mucho ~35 s). */
	constexpr float RoundIntroCloseSeconds = 0.32f;
	constexpr float RoundIntroMaxSeconds = 50.f;

	/** Vista previa del paso entre rondas: segundos de «Colocando la playa…» antes del 3, 2, 1 (uno por número). */
	constexpr float PreviewIntroPrepSeconds = 1.8f;

	/** Cada cuánto se busca el generador de la playa mientras no hay (fuera de la playa no existe). */
	constexpr float GeneratorLookupSeconds = 2.f;

	/** Las dos medias conchas del recuento (para dibujarlas de antemano con el resto del arte). */
	UTexture2D* HalfShellA() { return TNRaceArt::HalfShell(false); }
	UTexture2D* HalfShellB() { return TNRaceArt::HalfShell(true); }

	APlayerController* FindLocalController(UWorld& World)
	{
		for (FConstPlayerControllerIterator It = World.GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (Controller && Controller->IsLocalController()) { return Controller; }
		}
		return nullptr;
	}

	UTN_RaceScreensSubsystem* ScreensOf(UWorld* World)
	{
		UTN_RaceScreensSubsystem* Screens = World ? World->GetSubsystem<UTN_RaceScreensSubsystem>() : nullptr;
		if (!Screens) { UE_LOG(LogTortunabo, Display, TEXT("[Carrera] Aquí no hay pantallas de la carrera (servidor dedicado o sin mundo de juego).")); }
		return Screens;
	}

	int32 IntArg(const TArray<FString>& Args, int32 Index, int32 Default)
	{
		return Args.IsValidIndex(Index) ? FCString::Atoi(*Args[Index]) : Default;
	}

	void RunTally(const TArray<FString>& Args, UWorld* World)
	{
		if (UTN_RaceScreensSubsystem* Screens = ScreensOf(World))
		{
			Screens->StartTallyPreview(IntArg(Args, 0, 0), IntArg(Args, 1, 4), IntArg(Args, 2, 0) != 0, IntArg(Args, 3, 1));
		}
	}

	void RunPodium(const TArray<FString>& Args, UWorld* World)
	{
		if (UTN_RaceScreensSubsystem* Screens = ScreensOf(World)) { Screens->StartPodiumPreview(IntArg(Args, 0, 3)); }
	}

	void RunSprintPreview(const TArray<FString>& Args, UWorld* World)
	{
		if (UTN_RaceScreensSubsystem* Screens = ScreensOf(World)) { Screens->StartSprintPreview(IntArg(Args, 0, 2)); }
	}

	void RunCountdownPreview(UWorld* World)
	{
		if (UTN_RaceScreensSubsystem* Screens = ScreensOf(World)) { Screens->StartCountdownPreview(); }
	}

	void RunClockPreview(const TArray<FString>& Args, UWorld* World)
	{
		const float Start = Args.IsValidIndex(0) ? FCString::Atof(*Args[0]) : 62.f;
		if (UTN_RaceScreensSubsystem* Screens = ScreensOf(World)) { Screens->StartClockPreview(Start); }
	}

	void RunPreviewOff(UWorld* World)
	{
		if (UTN_RaceScreensSubsystem* Screens = ScreensOf(World)) { Screens->StopPreview(); }
	}

	void RunArrivalPreview(const TArray<FString>& Args, UWorld* World)
	{
		if (UTN_RaceScreensSubsystem* Screens = ScreensOf(World)) { Screens->StartArrivalPreview(IntArg(Args, 0, 1), IntArg(Args, 1, 0) != 0); }
	}

	void RunRoundPreview(const TArray<FString>& Args, UWorld* World)
	{
		if (UTN_RaceScreensSubsystem* Screens = ScreensOf(World)) { Screens->StartRoundIntroPreview(IntArg(Args, 0, 2), IntArg(Args, 1, 0) != 0); }
	}

	FAutoConsoleCommandWithWorldAndArgs CmdArrivalPreview(TEXT("TN.Race.ArrivalPreview"),
		TEXT("Vista previa de la llegada al agua: el huevo negro se cierra, «Has quedado X.º» con su premio y su mensaje, y se rompe. TN.Race.ArrivalPreview [puesto 1-8 = 1] [1 = sprint final]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunArrivalPreview));

	FAutoConsoleCommandWithWorldAndArgs CmdRoundPreview(TEXT("TN.Race.RoundPreview"),
		TEXT("Vista previa del paso entre rondas: el huevo negro, «RONDA N» (o «SPRINT FINAL»), «Colocando la playa…», 3, 2, 1 y se rompe. TN.Race.RoundPreview [ronda = 2] [1 = sprint final]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunRoundPreview));

	FAutoConsoleCommandWithWorldAndArgs CmdTally(TEXT("TN.Race.Tally"),
		TEXT("Vista previa del recuento de conchas: TN.Race.Tally [ganador 0-7, -1 = nadie] [jugadores 1-8] [1 = la concha que corona y luego el podio] [medias conchas = 1]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunTally));

	FAutoConsoleCommandWithWorldAndArgs CmdSprintPreview(TEXT("TN.Race.SprintPreview"),
		TEXT("Vista previa del título del sprint final (fanfarria, caras con «VS» y confeti): TN.Race.SprintPreview [finalistas 2-8]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSprintPreview));

	FAutoConsoleCommandWithWorld CmdCountdownPreview(TEXT("TN.Race.CountdownPreview"),
		TEXT("Vista previa de la cuenta atrás de 10 s tras la primera tortuga en el agua (con su «¡TIEMPO!»)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunCountdownPreview));

	FAutoConsoleCommandWithWorldAndArgs CmdClockPreview(TEXT("TN.Race.ClockPreview"),
		TEXT("Vista previa del reloj del último minuto de la ronda (avisos a los 60 y a los 30 s) y del «¡TIEMPO!» sin nadie en el agua: TN.Race.ClockPreview [segundos = 62]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunClockPreview));

	FAutoConsoleCommandWithWorldAndArgs CmdPodium(TEXT("TN.Race.Podium"),
		TEXT("Vista previa de la pantalla del campeón con el podio animado: TN.Race.Podium [jugadores 1-3]. Cualquier botón la cierra."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunPodium));

	FAutoConsoleCommandWithWorld CmdPreviewOff(TEXT("TN.Race.PreviewOff"),
		TEXT("Cierra la vista previa del recuento, del podio, del sprint o de la cuenta atrás."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunPreviewOff));
}

// ─────────────────────────────────────────────────────────────────────────────
// Subsistema
// ─────────────────────────────────────────────────────────────────────────────

bool UTN_RaceScreensSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

bool UTN_RaceScreensSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UTN_RaceScreensSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTN_RaceScreensSubsystem, STATGROUP_Tickables);
}

void UTN_RaceScreensSubsystem::Deinitialize()
{
	HideCurtain();
	HideAll();
	Super::Deinitialize();
}

void UTN_RaceScreensSubsystem::Tick(float DeltaTime)
{
	using namespace TNRaceScreensDetail;
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown) { return; }
	APlayerController* PC = FindLocalController(*World);
	if (!PC) { return; }
	if (bPreview)
	{
		TickPreview(DeltaTime, PC);
		return;
	}
	const ATN_BeachRaceGameState* State = World->GetGameState<ATN_BeachRaceGameState>();
	if (!State)
	{
		if (Tally || ChampionScreen || CountdownScreen || SprintScreen || RoundClock) { HideAll(); }
		if (Curtain || ArrivalScreen || RoundIntro) { HideCurtain(); }
		bHasPhase = false;
		return;
	}
	TickMatch(DeltaTime, PC, *State);
}

void UTN_RaceScreensSubsystem::TickMatch(float DeltaTime, APlayerController* PC, const ATN_BeachRaceGameState& State)
{
	using namespace TNRaceScreensDetail;
	TrackWins(DeltaTime, State);
	if (State.RacePhase == ETNBeachRacePhase::Waiting || State.RacePhase == ETNBeachRacePhase::Racing) { WarmArt(State); }
	const ETNBeachRacePhase Phase = State.RacePhase;
	const int32 Round = State.CurrentRound;
	if (!bHasPhase || Phase != ShownPhase || (Phase == ETNBeachRacePhase::RoundResults && Round != ShownRound))
	{
		const bool bHadPhase = bHasPhase;
		const ETNBeachRacePhase PreviousPhase = ShownPhase;
		bHasPhase = true;
		ShownPhase = Phase;
		ShownRound = Round;
		PhaseClock = 0.f;
		if (Phase == ETNBeachRacePhase::RoundResults)
		{
			ShowTally(PC, BuildTally(State, PC));
			LandedRound = Round;
		}
		else if (Phase == ETNBeachRacePhase::SprintIntro)
		{
			// Empate en lo más alto: el recuento se va por debajo mientras entra el título del sprint final.
			ShowSprint(PC, BuildSprint(State, PC));
		}
		else if (Phase == ETNBeachRacePhase::Champion)
		{
			// Sin recuento de esta ronda (se saltó directamente al campeón): primero la concha que le corona. Tras el sprint
			// final no: la ganadora va directa al podio.
			if (LandedRound != Round && !State.bSprintFinal && State.Champion)
			{
				ShowTally(PC, BuildTally(State, PC, State.Champion.Get()));
				LandedRound = Round;
			}
			if (IsValid(SprintScreen)) { SprintScreen->Dismiss(); }
			SprintScreen = nullptr;
		}
		else
		{
			// Ronda nueva (o partida nueva tras «Volver a jugar», o el sprint que se prepara): ningún recuento pendiente.
			HideAll();
			LandedRound = -1;
			if (Phase == ETNBeachRacePhase::Waiting)
			{
				// La llegada al agua se vuelve a mirar en la ronda nueva y, si se viene del recuento, del título del sprint o del
				// podio (no del viaje: esa la tapa el huevo de la pantalla de carga), la cáscara se cierra con «RONDA N».
				bArrivalHandled = false;
				const bool bBetweenRounds = bHadPhase && (PreviousPhase == ETNBeachRacePhase::RoundResults
					|| PreviousPhase == ETNBeachRacePhase::SprintIntro || PreviousPhase == ETNBeachRacePhase::Champion);
				if (bBetweenRounds)
				{
					IntroQuip = FMath::RandRange(0, FMath::Max(0, UTN_RaceRoundIntroWidget::NumQuips() - 1));
					StartRoundIntro(PC, BuildRoundIntro(State));
				}
			}
		}
	}
	PhaseClock += DeltaTime;
	TickCountdown(PC, State);
	TickRoundClock(PC, State);
	TickArrival(DeltaTime, PC, State);
	if (CurtainUse == ETNRaceCurtainUse::RoundIntro)
	{
		AdvanceRoundIntro(DeltaTime, &State);
	}

	if (Phase == ETNBeachRacePhase::RoundResults && Tally)
	{
		Tally->SetSecondsLeft(State.PhaseSecondsLeft);
		// La ganadora puede llegar por red un poco después que la fase.
		const APlayerState* Winner = State.RoundWinner.Get();
		Tally->UpdateWinner(Winner ? TallyPlayerIds.IndexOfByKey(Winner->GetPlayerId()) : INDEX_NONE);
	}
	else if (Phase == ETNBeachRacePhase::SprintIntro)
	{
		if (SprintScreen) { SprintScreen->SetSecondsLeft(State.PhaseSecondsLeft); }
	}
	else if (Phase == ETNBeachRacePhase::Champion)
	{
		if (Tally) { Tally->SetSecondsLeft(0.f); }
		if (!ChampionScreen && (!Tally || Tally->IsSequenceDone() || Tally->IsDismissing()))
		{
			ShowChampion(PC, BuildChampion(State, PC));
		}
		if (ChampionScreen)
		{
			ChampionScreen->SetSecondsLeft(State.PhaseSecondsLeft);
			if (!bVictoryPlaying && PhaseClock >= VictoryDelay) { PlayVictory(true); }
		}
	}
}

void UTN_RaceScreensSubsystem::TickCountdown(APlayerController* PC, const ATN_BeachRaceGameState& State)
{
	using namespace TNRaceScreensDetail;
	const bool bWanted = State.RacePhase == ETNBeachRacePhase::Racing && State.FinishCountdown != ETNBeachFinishCountdown::None;
	if (!bWanted)
	{
		if (IsValid(CountdownScreen)) { CountdownScreen->Dismiss(); }
		CountdownScreen = nullptr;
		return;
	}
	if (!CountdownScreen || CountdownScreen->IsDismissing())
	{
		CountdownScreen = CreateWidget<UTN_RaceFinishCountdownWidget>(PC, UTN_RaceFinishCountdownWidget::StaticClass());
		if (!CountdownScreen) { return; }
		TNVR::AddToFullScreen(CountdownScreen, CountdownZOrder);
	}
	FTNRaceCountdownView View;
	View.State = State.FinishCountdown;
	View.Reason = State.RoundEndReason;
	View.SecondsLeft = State.GetFinishCountdownLeft();
	View.TotalSeconds = State.FinishCountdownSeconds;
	const APlayerState* Leader = State.RoundWinner.Get();
	if (const ATN_CoopPlayerState* LeaderState = Cast<ATN_CoopPlayerState>(Leader))
	{
		const FTNRaceTallyRow LeaderRow = RowOf(*LeaderState, PC);
		View.LeaderName = LeaderRow.Name;
		View.LeaderLook = LeaderRow.Look;
	}
	// Lo que le toca a quien mira: la entera, una media, correr a por ella o solo mirar (ya no corre).
	const APlayerState* Own = PC ? PC->PlayerState.Get() : nullptr;
	if (Own && Own == Leader)
	{
		View.LocalStatus = 1;
	}
	else if (Own && State.RoundHalfShells.Contains(Own))
	{
		View.LocalStatus = 2;
	}
	else
	{
		const ATN_CoopPlayerState* OwnState = Cast<ATN_CoopPlayerState>(Own);
		View.LocalStatus = (!OwnState || OwnState->bHasFinishedRun || OwnState->IsOnlyASpectator()) ? 3 : 0;
	}
	CountdownScreen->SetView(View);
}

void UTN_RaceScreensSubsystem::TickRoundClock(APlayerController* PC, const ATN_BeachRaceGameState& State)
{
	using namespace TNRaceScreensDetail;
	// Solo mientras el tiempo de la ronda cuenta (nadie ha llegado al agua: después manda la cuenta de 10 s) y en el último
	// minuto; al acabarse, el «¡TIEMPO!» lo enseña la cuenta atrás con su motivo.
	const float Left = State.GetRoundTimeLeft();
	if (Left < 0.f || Left > RoundClockShowSeconds)
	{
		if (IsValid(RoundClock)) { RoundClock->Dismiss(); }
		RoundClock = nullptr;
		return;
	}
	FTNRaceRoundClockView View;
	View.SecondsLeft = Left;
	View.bSprint = State.bSprintFinal;
	ShowRoundClock(PC, View);
}

void UTN_RaceScreensSubsystem::ShowRoundClock(APlayerController* PC, const FTNRaceRoundClockView& View)
{
	using namespace TNRaceScreensDetail;
	if (!RoundClock || RoundClock->IsDismissing())
	{
		RoundClock = CreateWidget<UTN_RaceRoundClockWidget>(PC, UTN_RaceRoundClockWidget::StaticClass());
		if (!RoundClock) { return; }
		TNVR::AddToFullScreen(RoundClock, RoundClockZOrder);
	}
	RoundClock->SetView(View);
}

void UTN_RaceScreensSubsystem::TrackWins(float DeltaTime, const ATN_BeachRaceGameState& State)
{
	const bool bRoundPhase = State.RacePhase == ETNBeachRacePhase::Waiting || State.RacePhase == ETNBeachRacePhase::Racing;
	if (!bRoundPhase)
	{
		bTrackingRound = false;
		return;
	}
	if (!bTrackingRound)
	{
		bTrackingRound = true;
		HalvesAtRoundStart.Reset();
		RacingClock = 0.f;
	}
	if (State.RacePhase == ETNBeachRacePhase::Racing) { RacingClock += DeltaTime; }
	// Mientras se prepara la ronda y en sus primeros segundos se sigue apuntando (por si algo llega tarde); después, solo
	// los jugadores nuevos: las conchas de la ronda nunca entran aquí aunque RaceShellHalves llegue antes que el recuento.
	const bool bSettling = State.RacePhase == ETNBeachRacePhase::Waiting || RacingClock < 4.f;
	for (const TObjectPtr<APlayerState>& Base : State.PlayerArray)
	{
		const ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(Base);
		if (!PS) { continue; }
		const int32 Id = PS->GetPlayerId();
		if (bSettling || !HalvesAtRoundStart.Contains(Id)) { HalvesAtRoundStart.Add(Id, PS->RaceShellHalves); }
	}
}

void UTN_RaceScreensSubsystem::WarmArt(const ATN_BeachRaceGameState& State)
{
	using FMakeTexture = UTexture2D* (*)();
	static const FMakeTexture Fixed[] = { &TNRaceArt::TallyBackdrop, &TNRaceArt::ShellSocket, &TNRaceArt::SoftGlow, &TNRaceArt::Sparkle,
		&TNRaceArt::Crown, &TNRaceArt::SkyGradient, &TNRaceArt::SunGlow, &TNRaceArt::Cloud, &TNRaceArt::SidePanel,
		&TNRaceScreensDetail::HalfShellA, &TNRaceScreensDetail::HalfShellB };
	if (WarmStatic < static_cast<int32>(UE_ARRAY_COUNT(Fixed)))
	{
		Fixed[WarmStatic++]();
		return;
	}
	// Los premios de la pantalla del puesto (la llegada al agua no puede esperar a dibujarlos).
	if (WarmPrize < static_cast<int32>(TNRaceArrivalArt::EPrize::Count))
	{
		TNRaceArrivalArt::PrizeTexture(static_cast<TNRaceArrivalArt::EPrize>(WarmPrize++));
		return;
	}
	// Caras de cada jugador con su piel: feliz, con ojos de estrella y mareada (en caché: las ya hechas no cuestan).
	const int32 Count = State.PlayerArray.Num() * 3;
	if (Count == 0) { return; }
	WarmCursor = (WarmCursor + 1) % Count;
	if (const ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(State.PlayerArray[WarmCursor / 3]))
	{
		static const ETNTurtleFace Faces[] = { ETNTurtleFace::Happy, ETNTurtleFace::Win, ETNTurtleFace::Down };
		TNRaceArt::TurtleFaceFor(this, RowOf(*PS, nullptr).Look, Faces[WarmCursor % 3]);
	}
}

FTNRaceTallyRow UTN_RaceScreensSubsystem::RowOf(const ATN_CoopPlayerState& PS, const APlayerController* PC) const
{
	FTNRaceTallyRow Row;
	Row.Name = PS.GetPlayerName();
	Row.Look.HelmetId = PS.EquippedHelmetId;
	Row.Look.ShellId = PS.EquippedShellId;
	Row.Look.SkinId = PS.EquippedSkinId;
	Row.Look.EyesId = PS.EquippedEyesId;
	Row.bLocal = PC && PC->PlayerState.Get() == &PS;
	Row.HalvesBefore = PS.RaceShellHalves;
	return Row;
}

FTNRaceTallySetup UTN_RaceScreensSubsystem::BuildTally(const ATN_BeachRaceGameState& State, const APlayerController* PC, const APlayerState* ChampionOnly)
{
	FTNRaceTallySetup Setup;
	TallyPlayerIds.Reset();
	const APlayerState* Winner = ChampionOnly ? ChampionOnly : State.RoundWinner.Get();
	// Columnas en el orden de entrada a la partida (siempre el mismo): cada uno se encuentra en su sitio.
	TArray<const ATN_CoopPlayerState*> Players;
	for (const TObjectPtr<APlayerState>& Base : State.PlayerArray)
	{
		const ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(Base);
		if (PS && (!PS->IsInactive() || PS == Winner || State.RoundHalfShells.Contains(PS))) { Players.Add(PS); }
	}
	Players.Sort([](const ATN_CoopPlayerState& A, const ATN_CoopPlayerState& B) { return A.GetPlayerId() < B.GetPlayerId(); });
	int32 MostHalves = 0;
	for (int32 i = 0; i < Players.Num(); ++i)
	{
		const ATN_CoopPlayerState& PS = *Players[i];
		FTNRaceTallyRow Row = RowOf(PS, PC);
		const bool bWon = &PS == Winner;
		const bool bHalf = !ChampionOnly && !bWon && State.RoundHalfShells.Contains(&PS);
		Row.HalvesGained = bWon ? 2 : (bHalf ? 1 : 0);
		// Las conchas de esta ronda llegan desde las que tenía al empezarla (si no se apuntaron, las de ahora menos las
		// ganadas); en el recuento de la concha que corona, desde las de ahora menos esa.
		const int32* AtStart = ChampionOnly ? nullptr : HalvesAtRoundStart.Find(PS.GetPlayerId());
		Row.HalvesBefore = AtStart ? *AtStart : FMath::Max(0, PS.RaceShellHalves - Row.HalvesGained);
		if (bWon) { Setup.WinnerRow = i; }
		MostHalves = FMath::Max(MostHalves, Row.HalvesBefore + Row.HalvesGained);
		Setup.Rows.Add(Row);
		TallyPlayerIds.Add(PS.GetPlayerId());
	}
	const int32 Target = FMath::Max(1, State.RoundTarget);
	Setup.Target = FMath::Max(Target, (MostHalves + 1) / 2);
	Setup.Round = FMath::Max(1, State.CurrentRound);
	Setup.bTimeLimit = !ChampionOnly && State.RoundEndReason == ETNBeachRoundEnd::TimeLimit;
	DecideVerdict(Setup, Target * 2);
	if (ChampionOnly && Setup.Rows.IsValidIndex(Setup.WinnerRow))
	{
		// El campeón lo ha decidido el servidor (p. ej. TN.Race.Champion): a él le baja la corona, sin sprint.
		Setup.ChampionRow = Setup.WinnerRow;
		Setup.SprintRows.Reset();
	}
	return Setup;
}

void UTN_RaceScreensSubsystem::DecideVerdict(FTNRaceTallySetup& Setup, int32 TargetHalves)
{
	// Lo mismo que decide el servidor tras el recuento: la única en lo más alto con las conchas del campeón es campeona;
	// si hay empate ahí arriba, sprint final entre las empatadas.
	int32 Top = 0;
	for (const FTNRaceTallyRow& Row : Setup.Rows)
	{
		Top = FMath::Max(Top, Row.HalvesBefore + Row.HalvesGained);
	}
	Setup.ChampionRow = INDEX_NONE;
	Setup.SprintRows.Reset();
	if (Top < TargetHalves)
	{
		return;
	}
	for (int32 i = 0; i < Setup.Rows.Num(); ++i)
	{
		if (Setup.Rows[i].HalvesBefore + Setup.Rows[i].HalvesGained == Top) { Setup.SprintRows.Add(i); }
	}
	if (Setup.SprintRows.Num() == 1)
	{
		Setup.ChampionRow = Setup.SprintRows[0];
		Setup.SprintRows.Reset();
	}
}

FTNRaceChampionSetup UTN_RaceScreensSubsystem::BuildChampion(const ATN_BeachRaceGameState& State, const APlayerController* PC) const
{
	FTNRaceChampionSetup Setup;
	Setup.Target = FMath::Max(1, State.RoundTarget);
	Setup.bSprintWin = State.bSprintFinal;
	for (const TObjectPtr<APlayerState>& Base : State.Podium)
	{
		if (const ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(Base)) { Setup.Podium.Add(RowOf(*PS, PC)); }
		if (Setup.Podium.Num() == 3) { break; }
	}
	if (Setup.Podium.Num() == 0)
	{
		// Sin podio replicado todavía: el campeón y, detrás, por conchas.
		TArray<const ATN_CoopPlayerState*> Players;
		for (const TObjectPtr<APlayerState>& Base : State.PlayerArray)
		{
			if (const ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(Base)) { Players.Add(PS); }
		}
		const APlayerState* ChampionState = State.Champion.Get();
		Players.Sort([ChampionState](const ATN_CoopPlayerState& A, const ATN_CoopPlayerState& B)
		{
			if ((&A == ChampionState) != (&B == ChampionState)) { return &A == ChampionState; }
			return A.RaceShellHalves > B.RaceShellHalves;
		});
		for (int32 i = 0; i < Players.Num() && i < 3; ++i) { Setup.Podium.Add(RowOf(*Players[i], PC)); }
	}
	return Setup;
}

FTNRaceSprintSetup UTN_RaceScreensSubsystem::BuildSprint(const ATN_BeachRaceGameState& State, const APlayerController* PC) const
{
	FTNRaceSprintSetup Setup;
	Setup.TieHalves = 0;
	for (const TObjectPtr<APlayerState>& Base : State.SprintFinalists)
	{
		const ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(Base);
		if (!PS) { continue; }
		Setup.Finalists.Add(RowOf(*PS, PC));
		Setup.TieHalves = FMath::Max(Setup.TieHalves, PS->RaceShellHalves);
		Setup.bLocalFinalist |= PC && PC->PlayerState.Get() == PS;
	}
	if (Setup.TieHalves == 0) { Setup.TieHalves = FMath::Max(1, State.RoundTarget) * 2; }
	return Setup;
}

void UTN_RaceScreensSubsystem::ShowTally(APlayerController* PC, const FTNRaceTallySetup& Setup)
{
	using namespace TNRaceScreensDetail;
	if (!Tally || Tally->IsDismissing())
	{
		Tally = CreateWidget<UTN_RaceTallyWidget>(PC, UTN_RaceTallyWidget::StaticClass());
		if (!Tally) { return; }
		TNVR::AddToFullScreen(Tally, TallyZOrder);
	}
	Tally->Setup(Setup);
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Recuento: ronda %d, %d jugadores, ganador en la columna %d%s."), Setup.Round, Setup.Rows.Num(), Setup.WinnerRow,
		Setup.bPreview ? TEXT(" (vista previa)") : TEXT(""));
}

void UTN_RaceScreensSubsystem::ShowChampion(APlayerController* PC, const FTNRaceChampionSetup& Setup)
{
	using namespace TNRaceScreensDetail;
	if (!ChampionScreen)
	{
		ChampionScreen = CreateWidget<UTN_RaceChampionWidget>(PC, UTN_RaceChampionWidget::StaticClass());
		if (!ChampionScreen) { return; }
		TNVR::AddToFullScreen(ChampionScreen, ChampionZOrder);
	}
	ChampionScreen->Setup(Setup);
	if (Setup.bPreview)
	{
		TWeakObjectPtr<UTN_RaceScreensSubsystem> WeakThis(this);
		ChampionScreen->OnPreviewClosed = [WeakThis]()
		{
			if (UTN_RaceScreensSubsystem* Screens = WeakThis.Get()) { Screens->StopPreview(); }
		};
	}
	// El recuento se va con un fundido por debajo mientras entra el podio.
	if (Tally)
	{
		Tally->Dismiss();
		Tally = nullptr;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Pantalla del campeón: %s%s."), Setup.Podium.Num() > 0 ? *Setup.Podium[0].Name : TEXT("nadie"),
		Setup.bPreview ? TEXT(" (vista previa)") : TEXT(""));
}

void UTN_RaceScreensSubsystem::ShowSprint(APlayerController* PC, const FTNRaceSprintSetup& Setup)
{
	using namespace TNRaceScreensDetail;
	if (!SprintScreen || SprintScreen->IsDismissing())
	{
		SprintScreen = CreateWidget<UTN_RaceSprintWidget>(PC, UTN_RaceSprintWidget::StaticClass());
		if (!SprintScreen) { return; }
		TNVR::AddToFullScreen(SprintScreen, SprintZOrder);
	}
	SprintScreen->Setup(Setup);
	// El recuento y la cuenta atrás se van con un fundido por debajo mientras entra el título.
	if (Tally)
	{
		Tally->Dismiss();
		Tally = nullptr;
	}
	if (CountdownScreen)
	{
		CountdownScreen->Dismiss();
		CountdownScreen = nullptr;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Sprint final: %d finalistas%s."), Setup.Finalists.Num(), Setup.bPreview ? TEXT(" (vista previa)") : TEXT(""));
}

void UTN_RaceScreensSubsystem::HideAll()
{
	if (IsValid(Tally)) { Tally->Dismiss(); }
	Tally = nullptr;
	if (IsValid(ChampionScreen)) { ChampionScreen->RemoveFromParent(); }
	ChampionScreen = nullptr;
	if (IsValid(CountdownScreen)) { CountdownScreen->Dismiss(); }
	CountdownScreen = nullptr;
	if (IsValid(SprintScreen)) { SprintScreen->Dismiss(); }
	SprintScreen = nullptr;
	if (IsValid(RoundClock)) { RoundClock->Dismiss(); }
	RoundClock = nullptr;
	// La música de victoria del podio la apaga el director al cambiar el flujo (ronda nueva o viaje); la de la vista
	// previa, StopPreview.
	bVictoryPlaying = false;
}

void UTN_RaceScreensSubsystem::PlayVictory(bool bOn)
{
	bVictoryPlaying = bOn;
	UWorld* World = GetWorld();
	if (UTN_MatchMusicSubsystem* Music = World ? World->GetSubsystem<UTN_MatchMusicSubsystem>() : nullptr)
	{
		// La misma pista que la de ganar la carrera, para todos: es la fiesta de la campeona.
		Music->DebugPlayTrack(bOn ? ETNMusicTrack::Victory : ETNMusicTrack::None);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Cáscara oscura: llegada al agua y paso entre rondas
// ─────────────────────────────────────────────────────────────────────────────

void UTN_RaceScreensSubsystem::TickArrival(float DeltaTime, APlayerController* PC, const ATN_BeachRaceGameState& State)
{
	if (CurtainUse == ETNRaceCurtainUse::Arrival)
	{
		AdvanceArrival(DeltaTime, PC, &State);
		return;
	}
	if (CurtainUse != ETNRaceCurtainUse::None || bArrivalHandled || State.RacePhase != ETNBeachRacePhase::Racing)
	{
		return;
	}
	// En cuanto la tortuga entra en el agua de meta (sin esperar a la red: el puesto llega después) o, si no se ha visto aquí (la
	// bola del caparazón, TN.Race.WinRound), en cuanto llega el puesto del servidor.
	const int32 Place = State.GetArrivalPlace(PC ? PC->PlayerState.Get() : nullptr);
	if (Place > 0 || IsLocalTurtleInFinishWater(PC, State))
	{
		StartArrival(PC, Place);
	}
}

bool UTN_RaceScreensSubsystem::IsLocalTurtleInFinishWater(const APlayerController* PC, const ATN_BeachRaceGameState& State)
{
	using namespace TNRaceScreensDetail;
	const ACharacter* Turtle = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	const ATN_CoopPlayerState* OwnState = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
	if (!Turtle || !OwnState || Turtle->IsHidden() || !Turtle->IsLocallyControlled() || OwnState->bHasFinishedRun || OwnState->IsOnlyASpectator())
	{
		return false;
	}
	// Con la ronda cerrada («¡TIEMPO!» o todas dentro) ya no se llega; en el sprint, solo la primera finalista.
	if (State.FinishCountdown == ETNBeachFinishCountdown::TimeUp || State.FinishCountdown == ETNBeachFinishCountdown::AllIn)
	{
		return false;
	}
	if (State.bSprintFinal && (State.RoundWinner || !State.SprintFinalists.Contains(OwnState)))
	{
		return false;
	}
	UWorld* World = GetWorld();
	ATN_BeachRaceGenerator* Generator = BeachGenerator.Get();
	if (!Generator && World && World->GetTimeSeconds() >= NextGeneratorLookup)
	{
		NextGeneratorLookup = World->GetTimeSeconds() + GeneratorLookupSeconds;
		Generator = ATN_BeachRaceGenerator::Find(World);
		BeachGenerator = Generator;
	}
	if (!Generator)
	{
		return false;
	}
	// Lo mismo que mira el servidor (el centro de la tortuga en el agua de meta, ATN_BeachRaceGameMode::WatchRacers): en la
	// zambullida llega unas centésimas después que los pies y así una tortuga que solo chapotea en la orilla del agua no
	// cierra la cáscara sin llegar.
	return Generator->IsFinishWater(Turtle->GetActorLocation());
}

void UTN_RaceScreensSubsystem::StartArrival(APlayerController* PC, int32 ConfirmedPlace)
{
	using namespace TNRaceScreensDetail;
	// La vista previa no gasta la llegada de verdad de la ronda en curso.
	if (!bPreview)
	{
		bArrivalHandled = true;
	}
	HideCurtain();
	Curtain = UTN_GhostHatchWidget::ShowCurtain(PC, ArrivalCloseSeconds, ArrivalMaxHoldSeconds);
	if (!Curtain)
	{
		return;
	}
	CurtainUse = ETNRaceCurtainUse::Arrival;
	CurtainClock = 0.f;
	bArrivalConfirmed = ConfirmedPlace > 0;
	ArrivalPlace = ConfirmedPlace;
	ArrivalScreen = CreateWidget<UTN_RaceArrivalWidget>(PC, UTN_RaceArrivalWidget::StaticClass());
	if (ArrivalScreen)
	{
		TNVR::AddToScreen(ArrivalScreen, UTN_GhostHatchWidget::ViewportZOrder + 1);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Llegada al agua: se cierra el huevo negro (%s)."),
		bArrivalConfirmed ? *FString::Printf(TEXT("puesto %d"), ArrivalPlace) : TEXT("a la espera del puesto del servidor"));
}

void UTN_RaceScreensSubsystem::AdvanceArrival(float DeltaTime, APlayerController* PC, const ATN_BeachRaceGameState* State)
{
	using namespace TNRaceScreensDetail;
	CurtainClock += DeltaTime;
	if (!IsValid(Curtain) || Curtain->IsOpening())
	{
		// Algo la ha quitado (o se ha roto sola tras la espera máxima): fuera lo de encima.
		OpenCurtain(true);
		return;
	}
	if (!bArrivalConfirmed)
	{
		const int32 Place = State ? State->GetArrivalPlace(PC ? PC->PlayerState.Get() : nullptr) : 0;
		if (Place > 0)
		{
			bArrivalConfirmed = true;
			ArrivalPlace = Place;
		}
		else
		{
			// El servidor no la ha contado (la ronda se cerraba justo, o no era el agua de meta): la cáscara se funde.
			if (CurtainClock >= ArrivalConfirmSeconds || (State && State->RacePhase != ETNBeachRacePhase::Racing))
			{
				UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Llegada al agua sin puesto del servidor en %.1f s: se abre el huevo negro."), CurtainClock);
				OpenCurtain(false);
			}
			return;
		}
	}
	if (!ArrivalScreen)
	{
		// Sin pantalla del puesto (no se pudo crear): la cáscara se abre a su hora.
		if (CurtainClock >= ArrivalCloseSeconds + UTN_RaceArrivalWidget::ContentSeconds) { OpenCurtain(true); }
		return;
	}
	if (!ArrivalScreen->IsPlaying())
	{
		// Con la cáscara ya cerrada (nunca se ve a la tortuga ponerse de pie), el puesto.
		if (Curtain->IsClosed())
		{
			FTNRaceArrivalSetup Setup;
			Setup.Place = ArrivalPlace;
			Setup.Round = State ? FMath::Max(1, State->CurrentRound) : (bPreviewSprint ? 4 : 2);
			Setup.bSprint = State ? State->bSprintFinal : bPreviewSprint;
			Setup.bPreview = State == nullptr;
			ArrivalScreen->Play(Setup);
			UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Pantalla del puesto: %d.º%s."), Setup.Place, Setup.bPreview ? TEXT(" (vista previa)") : TEXT(""));
		}
		return;
	}
	if (!ArrivalScreen->IsDone())
	{
		return;
	}
	// Ya no queda nadie corriendo (todas en el agua): el recuento sale enseguida (el servidor lo ha retrasado hasta que acabe
	// esta pantalla), así que se espera tapado y la cáscara se rompe directamente sobre él.
	const bool bTallyComing = State && State->RacePhase == ETNBeachRacePhase::Racing && State->FinishCountdown == ETNBeachFinishCountdown::AllIn;
	if (bTallyComing && ArrivalScreen->GetTimeSinceDone() < ArrivalTallyWaitSeconds)
	{
		return;
	}
	// Si no, a la vista del fantasma espectador (quedan tortugas corriendo), del gusano o del recuento o el podio.
	OpenCurtain(true);
}

void UTN_RaceScreensSubsystem::StartRoundIntro(APlayerController* PC, const FTNRaceRoundIntroSetup& Setup)
{
	using namespace TNRaceScreensDetail;
	HideCurtain();
	Curtain = UTN_GhostHatchWidget::ShowCurtain(PC, RoundIntroCloseSeconds, RoundIntroMaxSeconds);
	if (!Curtain)
	{
		return;
	}
	CurtainUse = ETNRaceCurtainUse::RoundIntro;
	CurtainClock = 0.f;
	IntroCountLeft = -1.f;
	IntroLastReplicated = -1.f;
	IntroShownNumber = 0;
	bIntroSawPrep = false;
	RoundIntro = CreateWidget<UTN_RaceRoundIntroWidget>(PC, UTN_RaceRoundIntroWidget::StaticClass());
	if (RoundIntro)
	{
		RoundIntro->Setup(Setup);
		TNVR::AddToFullScreen(RoundIntro, UTN_GhostHatchWidget::ViewportZOrder + 1);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] Paso entre rondas: se cierra el huevo negro con «%s»%s."),
		Setup.bSprint ? TEXT("SPRINT FINAL") : *FString::Printf(TEXT("RONDA %d"), Setup.Round), Setup.bPreview ? TEXT(" (vista previa)") : TEXT(""));
}

FTNRaceRoundIntroSetup UTN_RaceScreensSubsystem::BuildRoundIntro(const ATN_BeachRaceGameState& State) const
{
	FTNRaceRoundIntroSetup Setup;
	Setup.Round = FMath::Max(1, State.CurrentRound);
	Setup.bSprint = State.bSprintFinal;
	Setup.Quip = IntroQuip;
	// Bola de partido: la concha entera de esta ronda coronaría a alguien.
	const int32 TargetHalves = FMath::Max(1, State.RoundTarget) * 2;
	for (const TObjectPtr<APlayerState>& Base : State.PlayerArray)
	{
		if (ATN_BeachRaceGameState::GetShellHalves(Base) + 2 >= TargetHalves)
		{
			Setup.bMatchPoint = true;
			break;
		}
	}
	return Setup;
}

void UTN_RaceScreensSubsystem::AdvanceRoundIntro(float DeltaTime, const ATN_BeachRaceGameState* State)
{
	using namespace TNRaceScreensDetail;
	CurtainClock += DeltaTime;
	if (!IsValid(Curtain) || Curtain->IsOpening())
	{
		OpenCurtain(true);
		return;
	}
	if (RoundIntro && !RoundIntro->IsPlaying())
	{
		// La ronda puede llegar por red un poco después que la fase: el título se corrige hasta que entra.
		if (State) { RoundIntro->Setup(BuildRoundIntro(*State)); }
		if (Curtain->IsClosed()) { RoundIntro->Play(); }
	}
	int32 Number = 0;
	if (State)
	{
		// ¡A correr! (o la partida se ha ido por otro lado, p. ej. el sprint sin rival): se rompe la cáscara.
		if (State->RacePhase != ETNBeachRacePhase::Waiting || CurtainClock >= RoundIntroMaxSeconds)
		{
			OpenCurtain(true);
			return;
		}
		// Con las tortugas ya en sus huevos empieza la cuenta de salida (PhaseSecondsLeft, a 4 Hz): entre valores replicados
		// se descuenta aquí (sin llegar a 0: la cáscara se rompe con la salida, no con el reloj de esta máquina).
		const float Replicated = State->PhaseSecondsLeft;
		if (Replicated <= 0.f)
		{
			bIntroSawPrep = true;
		}
		else if (bIntroSawPrep)
		{
			if (IntroCountLeft < 0.f || Replicated != IntroLastReplicated)
			{
				IntroCountLeft = Replicated;
			}
			else
			{
				IntroCountLeft = FMath::Max(0.01f, IntroCountLeft - DeltaTime);
			}
			IntroLastReplicated = Replicated;
			Number = FMath::CeilToInt(IntroCountLeft);
		}
	}
	else
	{
		// Vista previa: «Colocando la playa…» un rato, 3, 2, 1 (uno por segundo) y se rompe.
		const float Counting = PreviewClock - PreviewIntroPrepSeconds;
		if (Counting >= 3.f)
		{
			OpenCurtain(true);
			return;
		}
		if (Counting >= 0.f) { Number = 3 - FMath::FloorToInt(Counting); }
	}
	// Un «pum» de la cáscara por cada número nuevo (solo hacia abajo).
	if (Number >= 1 && (IntroShownNumber == 0 || Number < IntroShownNumber))
	{
		IntroShownNumber = Number;
		Curtain->Knock();
		if (RoundIntro) { RoundIntro->ShowCount(Number); }
	}
}

void UTN_RaceScreensSubsystem::OpenCurtain(bool bBurst)
{
	if (IsValid(Curtain)) { Curtain->Open(bBurst); }
	// La cáscara se quita sola al acabar de abrirse; lo de encima se va ya.
	Curtain = nullptr;
	if (IsValid(ArrivalScreen)) { ArrivalScreen->Dismiss(); }
	ArrivalScreen = nullptr;
	if (IsValid(RoundIntro)) { RoundIntro->Dismiss(); }
	RoundIntro = nullptr;
	CurtainUse = ETNRaceCurtainUse::None;
}

void UTN_RaceScreensSubsystem::HideCurtain()
{
	if (IsValid(Curtain)) { Curtain->RemoveFromParent(); }
	Curtain = nullptr;
	if (IsValid(ArrivalScreen)) { ArrivalScreen->RemoveFromParent(); }
	ArrivalScreen = nullptr;
	if (IsValid(RoundIntro)) { RoundIntro->RemoveFromParent(); }
	RoundIntro = nullptr;
	CurtainUse = ETNRaceCurtainUse::None;
}

// ─────────────────────────────────────────────────────────────────────────────
// Vista previa por consola
// ─────────────────────────────────────────────────────────────────────────────

void UTN_RaceScreensSubsystem::BuildPreviewRows(APlayerController* PC, int32 NumPlayers, TArray<FTNRaceTallyRow>& OutRows) const
{
	OutRows.Reset();
	// Hasta ocho columnas (partidas de ocho: el recuento y el sprint se encogen para caber).
	const int32 Count = FMath::Clamp(NumPlayers, 1, 8);
	static const TCHAR* FakeNames[] = { TEXT("Maximiliano de la Cruz Montoya"), TEXT("Coral"), TEXT("Perla"), TEXT("Marea"), TEXT("Alga") };
	// Medias conchas de mentira: una entera, media, dos, una y media, nada, dos y media, media y una.
	static const int32 FakeHalves[] = { 2, 1, 4, 3, 0, 5, 1, 2 };
	const UMP_GameInstance* GI = PC ? Cast<UMP_GameInstance>(UGameplayStatics::GetGameInstance(PC)) : nullptr;
	const TArray<FName> Skins = GI ? GI->GetCosmeticCatalog(ETNCosmeticCategory::Body) : TArray<FName>();
	const TArray<FName> Shells = GI ? GI->GetCosmeticCatalog(ETNCosmeticCategory::Shell) : TArray<FName>();
	const TArray<FName> Helmets = GI ? GI->GetCosmeticCatalog(ETNCosmeticCategory::Helmet) : TArray<FName>();
	const TArray<FName> Eyes = GI ? GI->GetCosmeticCatalog(ETNCosmeticCategory::Eyes) : TArray<FName>();
	auto Pick = [](const TArray<FName>& List, int32 Index) { return List.Num() > 0 ? List[Index % List.Num()] : NAME_None; };
	for (int32 i = 0; i < Count; ++i)
	{
		FTNRaceTallyRow Row;
		Row.HalvesBefore = FakeHalves[i];
		if (i == 0)
		{
			// Tu tortuga, con tu aspecto de verdad.
			const ATN_CoopPlayerState* Own = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
			if (Own)
			{
				Row = RowOf(*Own, PC);
				Row.HalvesBefore = FakeHalves[i];
			}
			else if (GI)
			{
				Row.Look.HelmetId = GI->GetEquippedHelmetIdFor(PC);
				Row.Look.ShellId = GI->GetEquippedShellIdFor(PC);
				Row.Look.SkinId = GI->GetEquippedSkinIdFor(PC);
				Row.Look.EyesId = GI->GetEquippedEyesIdFor(PC);
			}
			if (Row.Name.IsEmpty()) { Row.Name = TEXT("Tú"); }
			Row.bLocal = true;
		}
		else
		{
			Row.Name = FakeNames[(i - 1) % 5];
			Row.Look.SkinId = Pick(Skins, i * 2 + 1);
			Row.Look.ShellId = (i % 2 == 0) ? Pick(Shells, i) : NAME_None;
			Row.Look.HelmetId = Pick(Helmets, i * 3);
			Row.Look.EyesId = (i == 2) ? Pick(Eyes, i) : NAME_None;
		}
		OutRows.Add(Row);
	}
}

void UTN_RaceScreensSubsystem::StartTallyPreview(int32 WinnerRow, int32 NumPlayers, bool bChampionRound, int32 NumHalves)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? TNRaceScreensDetail::FindLocalController(*World) : nullptr;
	if (!PC) { return; }
	StopPreview();
	bPreview = true;
	PreviewKind = 0;
	PreviewClock = 0.f;
	BuildPreviewRows(PC, NumPlayers, PreviewRows);
	FTNRaceTallySetup Setup;
	Setup.Rows = PreviewRows;
	Setup.Target = 3;
	Setup.Round = bChampionRound ? 5 : 2;
	Setup.bPreview = true;
	Setup.WinnerRow = PreviewRows.IsValidIndex(WinnerRow) ? WinnerRow : INDEX_NONE;
	const int32 TargetHalves = Setup.Target * 2;
	if (Setup.Rows.IsValidIndex(Setup.WinnerRow))
	{
		// La entera para la ganadora (con la final, justo la que corona).
		FTNRaceTallyRow& Winner = Setup.Rows[Setup.WinnerRow];
		Winner.HalvesGained = 2;
		Winner.HalvesBefore = bChampionRound ? TargetHalves - 2 : FMath::Min(Winner.HalvesBefore, TargetHalves - 3);
	}
	// Medias para las columnas siguientes (sin coronar a nadie más que a la ganadora).
	int32 Given = 0;
	for (int32 k = 1; k < Setup.Rows.Num() && Given < FMath::Max(0, NumHalves); ++k)
	{
		const int32 Index = (FMath::Max(0, Setup.WinnerRow) + k) % Setup.Rows.Num();
		if (Index == Setup.WinnerRow) { continue; }
		FTNRaceTallyRow& Row = Setup.Rows[Index];
		Row.HalvesGained = 1;
		Row.HalvesBefore = FMath::Min(Row.HalvesBefore, TargetHalves - 2);
		++Given;
	}
	for (FTNRaceTallyRow& Row : Setup.Rows)
	{
		if (Row.HalvesGained == 0) { Row.HalvesBefore = FMath::Min(Row.HalvesBefore, TargetHalves - 1); }
	}
	DecideVerdict(Setup, TargetHalves);
	PreviewRows = Setup.Rows;
	PreviewWinner = Setup.WinnerRow;
	bPreviewChampionAfter = Setup.Rows.IsValidIndex(Setup.ChampionRow);
	ShowTally(PC, Setup);
}

void UTN_RaceScreensSubsystem::StartSprintPreview(int32 NumPlayers)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? TNRaceScreensDetail::FindLocalController(*World) : nullptr;
	if (!PC) { return; }
	StopPreview();
	bPreview = true;
	PreviewKind = 1;
	PreviewClock = 0.f;
	BuildPreviewRows(PC, FMath::Clamp(NumPlayers, 2, 8), PreviewRows);
	FTNRaceSprintSetup Setup;
	Setup.Finalists = PreviewRows;
	Setup.TieHalves = 6;
	Setup.bLocalFinalist = true;
	Setup.bPreview = true;
	ShowSprint(PC, Setup);
}

void UTN_RaceScreensSubsystem::StartCountdownPreview()
{
	using namespace TNRaceScreensDetail;
	UWorld* World = GetWorld();
	APlayerController* PC = World ? FindLocalController(*World) : nullptr;
	if (!PC) { return; }
	StopPreview();
	bPreview = true;
	PreviewKind = 2;
	PreviewClock = 0.f;
	BuildPreviewRows(PC, 2, PreviewRows);
	CountdownScreen = CreateWidget<UTN_RaceFinishCountdownWidget>(PC, UTN_RaceFinishCountdownWidget::StaticClass());
	if (CountdownScreen) { TNVR::AddToFullScreen(CountdownScreen, CountdownZOrder); }
}

void UTN_RaceScreensSubsystem::StartClockPreview(float StartSeconds)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? TNRaceScreensDetail::FindLocalController(*World) : nullptr;
	if (!PC) { return; }
	StopPreview();
	bPreview = true;
	PreviewKind = 3;
	PreviewClock = 0.f;
	PreviewClockStart = FMath::Clamp(StartSeconds, 1.f, 120.f);
	BuildPreviewRows(PC, 2, PreviewRows);
}

void UTN_RaceScreensSubsystem::StartArrivalPreview(int32 Place, bool bSprint)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? TNRaceScreensDetail::FindLocalController(*World) : nullptr;
	if (!PC) { return; }
	StopPreview();
	bPreview = true;
	PreviewKind = 4;
	PreviewClock = 0.f;
	bPreviewSprint = bSprint;
	StartArrival(PC, FMath::Clamp(Place, 1, 8));
}

void UTN_RaceScreensSubsystem::StartRoundIntroPreview(int32 Round, bool bSprint)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? TNRaceScreensDetail::FindLocalController(*World) : nullptr;
	if (!PC) { return; }
	StopPreview();
	bPreview = true;
	PreviewKind = 5;
	PreviewClock = 0.f;
	FTNRaceRoundIntroSetup Setup;
	Setup.Round = FMath::Max(1, Round);
	Setup.bSprint = bSprint;
	Setup.bMatchPoint = !bSprint && Setup.Round >= 4;
	Setup.Quip = FMath::RandRange(0, FMath::Max(0, UTN_RaceRoundIntroWidget::NumQuips() - 1));
	Setup.bPreview = true;
	StartRoundIntro(PC, Setup);
}

void UTN_RaceScreensSubsystem::StartPodiumPreview(int32 NumPlayers)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? TNRaceScreensDetail::FindLocalController(*World) : nullptr;
	if (!PC) { return; }
	StopPreview();
	bPreview = true;
	bPreviewChampionAfter = false;
	PreviewClock = 0.f;
	BuildPreviewRows(PC, FMath::Clamp(NumPlayers, 1, 3), PreviewRows);
	FTNRaceChampionSetup Setup;
	Setup.Podium = PreviewRows;
	Setup.Target = 3;
	Setup.bPreview = true;
	ShowChampion(PC, Setup);
	PlayVictory(true);
}

void UTN_RaceScreensSubsystem::StopPreview()
{
	if (!bPreview) { return; }
	bPreview = false;
	PreviewKind = 0;
	const bool bWasPlaying = bVictoryPlaying;
	HideCurtain();
	HideAll();
	if (bWasPlaying) { PlayVictory(false); }
	bVictoryPlaying = false;
	bHasPhase = false;
}

void UTN_RaceScreensSubsystem::TickPreview(float DeltaTime, APlayerController* PC)
{
	using namespace TNRaceScreensDetail;
	PreviewClock += DeltaTime;
	if (PreviewKind == 4 || PreviewKind == 5)
	{
		// Llegada al agua o paso entre rondas: la cáscara hace su guion y, un momento después de romperse, se acaba.
		if (CurtainUse == ETNRaceCurtainUse::Arrival)
		{
			AdvanceArrival(DeltaTime, PC, nullptr);
		}
		else if (CurtainUse == ETNRaceCurtainUse::RoundIntro)
		{
			AdvanceRoundIntro(DeltaTime, nullptr);
		}
		else
		{
			// Ya se ha roto (se quita sola al acabar de abrirse, porque ya no se apunta aquí): fin de la vista previa.
			StopPreview();
		}
		return;
	}
	if (PreviewKind == 1)
	{
		// Título del sprint: con su cuenta de mentira y se cierra solo.
		if (SprintScreen) { SprintScreen->SetSecondsLeft(FMath::Max(0.f, PreviewSprintSeconds - 1.f - PreviewClock)); }
		if (PreviewClock >= PreviewSprintSeconds) { StopPreview(); }
		return;
	}
	if (PreviewKind == 3)
	{
		// Reloj del último minuto (aparece a los 60 s) y, a 0, el «¡TIEMPO!» de la ronda sin nadie en el agua: la concha
		// para la de la columna 1, la más cerca del mar.
		const float Left = PreviewClockStart - PreviewClock;
		if (Left > 0.f)
		{
			if (Left <= RoundClockShowSeconds)
			{
				FTNRaceRoundClockView View;
				View.SecondsLeft = Left;
				View.bPreview = true;
				ShowRoundClock(PC, View);
			}
			return;
		}
		if (IsValid(RoundClock)) { RoundClock->Dismiss(); }
		RoundClock = nullptr;
		if (!CountdownScreen || CountdownScreen->IsDismissing())
		{
			CountdownScreen = CreateWidget<UTN_RaceFinishCountdownWidget>(PC, UTN_RaceFinishCountdownWidget::StaticClass());
			if (CountdownScreen) { TNVR::AddToFullScreen(CountdownScreen, CountdownZOrder); }
		}
		if (CountdownScreen)
		{
			FTNRaceCountdownView View;
			View.State = ETNBeachFinishCountdown::TimeUp;
			View.Reason = ETNBeachRoundEnd::TimeLimit;
			if (PreviewRows.IsValidIndex(1))
			{
				View.LeaderName = PreviewRows[1].Name;
				View.LeaderLook = PreviewRows[1].Look;
			}
			View.LocalStatus = 3;
			View.bPreview = true;
			CountdownScreen->SetView(View);
		}
		if (-Left >= PreviewTimeUpSeconds + 0.6f) { StopPreview(); }
		return;
	}
	if (PreviewKind == 2)
	{
		// Cuenta atrás: diez segundos contando y el «¡TIEMPO!»; la de la columna 1 es la primera y tú aún corres.
		if (CountdownScreen)
		{
			FTNRaceCountdownView View;
			View.SecondsLeft = FMath::Max(0.f, PreviewCountdownSeconds - PreviewClock);
			View.TotalSeconds = PreviewCountdownSeconds;
			View.State = View.SecondsLeft > 0.f ? ETNBeachFinishCountdown::Counting : ETNBeachFinishCountdown::TimeUp;
			if (PreviewRows.IsValidIndex(1))
			{
				View.LeaderName = PreviewRows[1].Name;
				View.LeaderLook = PreviewRows[1].Look;
			}
			View.LocalStatus = 0;
			View.bPreview = true;
			CountdownScreen->SetView(View);
		}
		if (PreviewClock >= PreviewCountdownSeconds + PreviewTimeUpSeconds) { StopPreview(); }
		return;
	}
	if (Tally && !ChampionScreen)
	{
		Tally->SetSecondsLeft(FMath::Max(0.f, PreviewTallySeconds - PreviewClock));
	}
	if (bPreviewChampionAfter)
	{
		// Tras la concha que corona, el podio: el ganador primero y el resto por conchas.
		if (!ChampionScreen && Tally && Tally->IsSequenceDone())
		{
			FTNRaceChampionSetup Setup;
			Setup.Target = 3;
			Setup.bPreview = true;
			TArray<FTNRaceTallyRow> Ordered = PreviewRows;
			if (Ordered.IsValidIndex(PreviewWinner))
			{
				Ordered[PreviewWinner].HalvesBefore = Setup.Target * 2;
				const FTNRaceTallyRow Winner = Ordered[PreviewWinner];
				Ordered.RemoveAt(PreviewWinner);
				Ordered.StableSort([](const FTNRaceTallyRow& A, const FTNRaceTallyRow& B) { return A.HalvesBefore + A.HalvesGained > B.HalvesBefore + B.HalvesGained; });
				Ordered.Insert(Winner, 0);
			}
			Ordered.SetNum(FMath::Min(Ordered.Num(), 3));
			Setup.Podium = Ordered;
			ShowChampion(PC, Setup);
			PlayVictory(true);
		}
		return;
	}
	if (!ChampionScreen && PreviewClock >= PreviewTallySeconds) { StopPreview(); }
}
