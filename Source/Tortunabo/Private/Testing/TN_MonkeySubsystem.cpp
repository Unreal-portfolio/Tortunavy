#include "Testing/TN_MonkeySubsystem.h"

#include "Core/TN_Log.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/CommandLine.h"
#include "Testing/TN_MonkeyComponent.h"
#include "Testing/TN_MonkeyNetStart.h"

namespace TNMonkeySubsystemDetail
{
	/** Sesión en curso (para el gancho de error fatal). */
	UTN_MonkeySubsystem* GActive = nullptr;
	bool bCrashHookInstalled = false;

	void OnFatal()
	{
		if (GActive)
		{
			GActive->WriteCrashReport();
		}
	}

	/** El mundo de juego donde se escribe el comando. */
	UTN_MonkeySubsystem* FromWorld(UWorld* World)
	{
		return World ? World->GetSubsystem<UTN_MonkeySubsystem>() : nullptr;
	}

	void RunCommand(const TArray<FString>& Args, UWorld* World)
	{
		UTN_MonkeySubsystem* Monkey = FromWorld(World);
		if (!Monkey)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Monkey] TN.Monkey: solo en un mundo de juego."));
			return;
		}
		if (Args.Num() > 0 && Args[0].Equals(TEXT("stop"), ESearchCase::IgnoreCase))
		{
			Monkey->StopSession(TEXT("parado a mano"));
			return;
		}
		if (Args.Num() < 1 || !Args[0].IsNumeric())
		{
			UE_LOG(LogTortunabo, Display, TEXT("[Monkey] Uso: TN.Monkey <segundos> [semilla=1] [jugadores=1] | TN.Monkey stop. Informe en Saved/Monkey/<fecha>.json."));
			return;
		}
		FTNMonkeyConfig Config;
		Config.Seconds = FMath::Clamp(FCString::Atof(*Args[0]), 1.f, 3600.f);
		Config.Seed = Args.IsValidIndex(1) ? FCString::Atoi(*Args[1]) : 1;
		Config.Players = Args.IsValidIndex(2) ? FMath::Clamp(FCString::Atoi(*Args[2]), 1, 8) : 1;
		Monkey->StartSession(Config);
	}

	static FAutoConsoleCommandWithWorldAndArgs CmdMonkey(TEXT("TN.Monkey"),
		TEXT("Monkey test: entrada aleatoria reproducible en cada jugador local. TN.Monkey <segundos> [semilla=1] [jugadores=1] | TN.Monkey stop. Informe en Saved/Monkey/."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunCommand), ECVF_Cheat);
}

bool UTN_MonkeySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
#endif
}

void UTN_MonkeySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
#if !UE_BUILD_SHIPPING
	// -TNMonkey=<segundos>:<semilla>  [-TNMonkeyPlayers=N] [-TNMonkeyWarmup=s] [-TNMonkeyOut=ruta] [-TNQuitWhenDone]
	const FString Spec = TNTestReport::CommandLineValue(TEXT("-TNMonkey"));
	if (Spec.IsEmpty())
	{
		return;
	}
	// [-TNMonkeyNet=client|server|any]: un cliente pasa antes por el mapa por defecto sin jugadores; el monkey espera a su mundo.
	const TNMonkey::ENetFilter NetFilter =
		TNMonkey::ResolveNetFilter(TNTestReport::CommandLineValue(TEXT("-TNMonkeyNet")), TNMonkey::FirstUrlToken(FCommandLine::Get()));
	if (!TNMonkey::ShouldStartIn(NetFilter, InWorld.GetNetMode()))
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Monkey] -TNMonkey espera a un mundo «%s»; %s es %s: no arranca aquí."), TNMonkey::NetFilterName(NetFilter),
			*InWorld.GetMapName(), *TNTestReport::NetModeName(&InWorld));
		return;
	}
	FString SecondsText;
	FString SeedText;
	if (!Spec.Split(TEXT(":"), &SecondsText, &SeedText))
	{
		SecondsText = Spec;
		SeedText = TEXT("1");
	}
	FTNMonkeyConfig NewConfig;
	NewConfig.Seconds = FMath::Clamp(FCString::Atof(*SecondsText), 1.f, 3600.f);
	NewConfig.Seed = FCString::Atoi(*SeedText);
	const FString Players = TNTestReport::CommandLineValue(TEXT("-TNMonkeyPlayers"));
	NewConfig.Players = Players.IsEmpty() ? 1 : FMath::Clamp(FCString::Atoi(*Players), 1, 8);
	const FString Warmup = TNTestReport::CommandLineValue(TEXT("-TNMonkeyWarmup"));
	NewConfig.WarmupSeconds = Warmup.IsEmpty() ? 10.f : FMath::Max(0.f, FCString::Atof(*Warmup));
	NewConfig.OutPath = TNTestReport::CommandLineValue(TEXT("-TNMonkeyOut"));
	NewConfig.bQuitWhenDone = FParse::Param(FCommandLine::Get(), TEXT("TNQuitWhenDone"));
	StartSession(NewConfig);
#endif
}

void UTN_MonkeySubsystem::Deinitialize()
{
	if (State != EState::Idle)
	{
		Finish(TEXT("el mundo se ha cerrado"), false);
	}
	Super::Deinitialize();
}

bool UTN_MonkeySubsystem::StartSession(const FTNMonkeyConfig& InConfig)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	UWorld* World = GetWorld();
	if (State != EState::Idle || !World)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Monkey] Ya hay una sesión en marcha (TN.Monkey stop la para)."));
		return false;
	}
	Config = InConfig;
	State = EState::Warmup;
	WarmupEnd = FPlatformTime::Seconds() + Config.WarmupSeconds;
	UE_LOG(LogTortunabo, Log, TEXT("[Monkey] Sesión de %.0f s, semilla %d, %d jugadores, calentamiento %.0f s."), Config.Seconds, Config.Seed, Config.Players,
		Config.WarmupSeconds);

	// Jugadores locales que faltan: entran como cualquier otro (mismo modo de juego, misma salida).
	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		// El motor limita a 4 los jugadores locales (pantalla partida); en headless no se dibuja nada, así que se sube el tope.
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->MaxSplitscreenPlayers < Config.Players)
		{
			GEngine->GameViewport->MaxSplitscreenPlayers = Config.Players;
		}
		while (GameInstance->GetNumLocalPlayers() < Config.Players)
		{
			if (!UGameplayStatics::CreatePlayer(World, -1, true))
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[Monkey] No se ha podido crear el jugador local %d."), GameInstance->GetNumLocalPlayers());
				break;
			}
		}
	}
	return true;
#endif
}

void UTN_MonkeySubsystem::BeginRunning()
{
	State = EState::Running;
	RunStart = FPlatformTime::Seconds();
	LastScan = 0.0;
	Frames.Reset();
	MemoryStartMB = TNTestReport::UsedPhysicalMB();
	SessionNetMode = TNTestReport::NetModeName(GetWorld());

	if (!bSinkAttached && GLog)
	{
		GLog->AddOutputDevice(&Sink);
		bSinkAttached = true;
	}
	// Correcciones de red: el motor las escribe (LogNetPlayerMovement) cuando el servidor corrige a un cliente.
	if (IConsoleVariable* Corrections = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetShowCorrections")))
	{
		SavedNetShowCorrections = Corrections->GetInt();
		Corrections->Set(1, ECVF_SetByConsole);
		bCVarsChanged = true;
	}
	TNMonkeySubsystemDetail::GActive = this;
	if (!TNMonkeySubsystemDetail::bCrashHookInstalled)
	{
		TNMonkeySubsystemDetail::bCrashHookInstalled = true;
		FCoreDelegates::OnHandleSystemError.AddStatic(&TNMonkeySubsystemDetail::OnFatal);
	}
	AttachComponents();
	UE_LOG(LogTortunabo, Log, TEXT("[Monkey] En marcha: %d componentes."), Monkeys.Num());
}

void UTN_MonkeySubsystem::AttachComponents()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	int32 Index = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController())
		{
			continue;
		}
		bool bHas = false;
		for (const TObjectPtr<UTN_MonkeyComponent>& Existing : Monkeys)
		{
			if (Existing && Existing->GetOwner() == PC)
			{
				bHas = true;
				break;
			}
		}
		if (!bHas)
		{
			UTN_MonkeyComponent* Monkey = NewObject<UTN_MonkeyComponent>(PC, NAME_None, RF_Transient);
			Monkey->Configure(Config.Seed, Index);
			Monkey->RegisterComponent();
			Monkeys.Add(Monkey);
		}
		++Index;
	}
}

void UTN_MonkeySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const double Now = FPlatformTime::Seconds();
	if (State == EState::Warmup)
	{
		if (Now >= WarmupEnd)
		{
			BeginRunning();
		}
		return;
	}
	if (State != EState::Running)
	{
		return;
	}
	Frames.Tick();
	if (Now - LastScan >= 1.0)
	{
		LastScan = Now;
		AttachComponents();
	}
	if (Now - RunStart >= Config.Seconds)
	{
		Finish(TEXT("tiempo cumplido"), false);
	}
}

void UTN_MonkeySubsystem::StopSession(const TCHAR* Reason)
{
	if (State != EState::Idle)
	{
		Finish(Reason, false);
	}
}

TSharedRef<FJsonObject> UTN_MonkeySubsystem::BuildReport(const TCHAR* Reason, bool bCrashed) const
{
	using TNMonkey::EAction;
	/** Centímetros por metro: el informe da las distancias en m. */
	constexpr double CmPerMeter = 100.0;
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	const UWorld* World = GetWorld();
	Root->SetStringField(TEXT("tool"), TEXT("TN.Monkey"));
	Root->SetStringField(TEXT("date"), FDateTime::Now().ToString());
	Root->SetStringField(TEXT("map"), World ? World->GetMapName() : FString());
	Root->SetStringField(TEXT("build"), TNTestReport::BuildConfigName());
	Root->SetStringField(TEXT("net_mode"), SessionNetMode.IsEmpty() ? TNTestReport::NetModeName(World) : SessionNetMode);
	Root->SetStringField(TEXT("end_reason"), Reason);
	Root->SetBoolField(TEXT("crashed"), bCrashed);
	Root->SetNumberField(TEXT("seed"), Config.Seed);
	Root->SetNumberField(TEXT("requested_seconds"), Config.Seconds);
	const double Elapsed = State == EState::Running ? FPlatformTime::Seconds() - RunStart : ElapsedAtFinish;
	Root->SetNumberField(TEXT("elapsed_seconds"), Elapsed);
	Root->SetNumberField(TEXT("players"), Monkeys.Num());

	int32 TotalActions[static_cast<int32>(EAction::Count)] = {};
	int32 Stuck = 0;
	int32 UnderTerrain = 0;
	int32 Falls = 0;
	int32 NoPawn = 0;
	TArray<TSharedPtr<FJsonValue>> PlayersJson;
	for (const TObjectPtr<UTN_MonkeyComponent>& Monkey : Monkeys)
	{
		if (!Monkey)
		{
			continue;
		}
		const FTNMonkeyPlayerStats& Stats = Monkey->GetStats();
		TSharedRef<FJsonObject> Player = MakeShared<FJsonObject>();
		Player->SetNumberField(TEXT("index"), Stats.PlayerIndex);
		TSharedRef<FJsonObject> Actions = MakeShared<FJsonObject>();
		for (int32 Index = 0; Index < static_cast<int32>(EAction::Count); ++Index)
		{
			Actions->SetNumberField(TNMonkey::ActionName(static_cast<EAction>(Index)), Stats.ActionCounts[Index]);
			TotalActions[Index] += Stats.ActionCounts[Index];
		}
		Player->SetObjectField(TEXT("actions"), Actions);
		Player->SetNumberField(TEXT("pause_skipped_no_ui"), Stats.PauseSkipped);
		Player->SetNumberField(TEXT("distance_m"), Stats.DistanceCm / CmPerMeter);
		Player->SetNumberField(TEXT("min_feet_z_m"), Stats.MinZ == TNumericLimits<double>::Max() ? 0.0 : Stats.MinZ / CmPerMeter);
		Player->SetNumberField(TEXT("max_depth_under_ground_cm"), Stats.MaxDepthUnderGround < -1e9 ? 0.0 : Stats.MaxDepthUnderGround);
		Player->SetNumberField(TEXT("pawn_changes"), Stats.PawnChanges);
		TArray<TSharedPtr<FJsonValue>> EventsJson;
		for (const FTNMonkeyEvent& Event : Stats.Events)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("kind"), Event.Kind);
			Item->SetNumberField(TEXT("t"), Event.Time);
			Item->SetStringField(TEXT("location_m"), FString::Printf(TEXT("%.1f, %.1f, %.1f"), Event.Location.X / CmPerMeter, Event.Location.Y / CmPerMeter, Event.Location.Z / CmPerMeter));
			Item->SetStringField(TEXT("detail"), Event.Detail);
			EventsJson.Add(MakeShared<FJsonValueObject>(Item));
			Stuck += Event.Kind == TEXT("stuck") ? 1 : 0;
			UnderTerrain += Event.Kind == TEXT("under_terrain") ? 1 : 0;
			Falls += Event.Kind == TEXT("unrescued_fall") ? 1 : 0;
			NoPawn += Event.Kind == TEXT("no_pawn") ? 1 : 0;
		}
		Player->SetArrayField(TEXT("events"), EventsJson);
		PlayersJson.Add(MakeShared<FJsonValueObject>(Player));
	}
	TSharedRef<FJsonObject> ActionsTotal = MakeShared<FJsonObject>();
	for (int32 Index = 0; Index < static_cast<int32>(EAction::Count); ++Index)
	{
		ActionsTotal->SetNumberField(TNMonkey::ActionName(static_cast<EAction>(Index)), TotalActions[Index]);
	}
	Root->SetObjectField(TEXT("actions_total"), ActionsTotal);
	Root->SetArrayField(TEXT("per_player"), PlayersJson);
	Root->SetNumberField(TEXT("stuck_events"), Stuck);
	Root->SetNumberField(TEXT("under_terrain_events"), UnderTerrain);
	Root->SetNumberField(TEXT("unrescued_falls"), Falls);
	Root->SetNumberField(TEXT("no_pawn_events"), NoPawn);
	Sink.WriteJson(*Root);

	const TNMonkey::FFrameSummary Frame = TNMonkey::Summarize(Frames.GetSamples());
	TSharedRef<FJsonObject> FrameJson = MakeShared<FJsonObject>();
	FrameJson->SetNumberField(TEXT("frames"), Frame.Frames);
	FrameJson->SetNumberField(TEXT("avg_ms"), Frame.Average);
	FrameJson->SetNumberField(TEXT("p50_ms"), Frame.P50);
	FrameJson->SetNumberField(TEXT("p95_ms"), Frame.P95);
	FrameJson->SetNumberField(TEXT("p99_ms"), Frame.P99);
	FrameJson->SetNumberField(TEXT("max_ms"), Frame.Max);
	Root->SetObjectField(TEXT("frame_time"), FrameJson);
	Root->SetNumberField(TEXT("memory_start_mb"), MemoryStartMB);
	Root->SetNumberField(TEXT("memory_end_mb"), TNTestReport::UsedPhysicalMB());

	// Veredicto: lo que hace fallar la prueba automática.
	TArray<TSharedPtr<FJsonValue>> Failures;
	if (bCrashed)
	{
		Failures.Add(MakeShared<FJsonValueString>(TEXT("el proceso ha caído (assert o error fatal)")));
	}
	if (Sink.GetEnsureCount() > 0)
	{
		Failures.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%d asserts o ensures"), Sink.GetEnsureCount())));
	}
	if (Falls > 0)
	{
		Failures.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%d caídas sin rescatar"), Falls)));
	}
	if (Monkeys.Num() == 0 && !bCrashed)
	{
		Failures.Add(MakeShared<FJsonValueString>(TEXT("ningún jugador local al que darle entrada")));
	}
	Root->SetBoolField(TEXT("passed"), Failures.Num() == 0);
	Root->SetArrayField(TEXT("failures"), Failures);
	return Root;
}

void UTN_MonkeySubsystem::Finish(const TCHAR* Reason, bool bCrashed)
{
	ElapsedAtFinish = State == EState::Running ? FPlatformTime::Seconds() - RunStart : 0.0;
	for (const TObjectPtr<UTN_MonkeyComponent>& Monkey : Monkeys)
	{
		if (Monkey)
		{
			Monkey->ReleaseAll();
		}
	}
	// El informe se construye con los componentes vivos.
	const TSharedRef<FJsonObject> Report = BuildReport(Reason, bCrashed);
	const FString Path = Config.OutPath.IsEmpty() ? TNTestReport::DefaultPath(TEXT("Monkey")) : Config.OutPath;
	const bool bSaved = TNTestReport::Save(*Report, Path);
	LastReportPath = Path;
	bool bPassed = false;
	Report->TryGetBoolField(TEXT("passed"), bPassed);
	UE_LOG(LogTortunabo, Log, TEXT("[Monkey] Sesión terminada (%s): %s. Informe %s: %s"), Reason, bPassed ? TEXT("sin fallos") : TEXT("CON FALLOS"),
		bSaved ? TEXT("guardado") : TEXT("NO guardado"), *Path);

	if (bSinkAttached && GLog)
	{
		GLog->RemoveOutputDevice(&Sink);
		bSinkAttached = false;
	}
	if (bCVarsChanged)
	{
		if (IConsoleVariable* Corrections = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetShowCorrections")))
		{
			Corrections->Set(SavedNetShowCorrections, ECVF_SetByConsole);
		}
		bCVarsChanged = false;
	}
	for (const TObjectPtr<UTN_MonkeyComponent>& Monkey : Monkeys)
	{
		if (Monkey)
		{
			Monkey->DestroyComponent();
		}
	}
	Monkeys.Reset();
	if (TNMonkeySubsystemDetail::GActive == this)
	{
		TNMonkeySubsystemDetail::GActive = nullptr;
	}
	State = EState::Idle;
	if (Config.bQuitWhenDone)
	{
		FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
	}
}

void UTN_MonkeySubsystem::WriteCrashReport()
{
	if (State != EState::Running)
	{
		return;
	}
	// Ya en pleno error fatal: solo escribir lo que hay, sin tocar componentes ni el mundo más de lo imprescindible.
	const FString Path = Config.OutPath.IsEmpty() ? TNTestReport::DefaultPath(TEXT("Monkey")) : Config.OutPath;
	const TSharedRef<FJsonObject> Report = BuildReport(TEXT("caída del proceso"), true);
	TNTestReport::Save(*Report, Path);
	LastReportPath = Path;
}
