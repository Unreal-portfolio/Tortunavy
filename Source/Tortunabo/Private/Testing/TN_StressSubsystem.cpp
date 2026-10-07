#include "Testing/TN_StressSubsystem.h"

#include "Core/TN_Log.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Player/TortugaCharacter.h"
#include "RHI.h"
#include "RenderTimer.h"
#include "Testing/TN_CpuCoreProbe.h"
#include "Testing/TN_MonkeyPlan.h"
#include "Testing/TN_MonkeySubsystem.h"
#include "Testing/TN_StressChaos.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachTypes.h"
#include "TN_StressEnemies.h"

namespace TNStressDetail
{
	/** Segundos al principio de cada fase que no cuentan en el promedio (el propio parón de crear los actores). */
	constexpr double SettleSeconds = 1.5;

	/** Reparto de los fotogramas entre núcleos de eficiencia (E) y de rendimiento (P): porcentaje en E, mediana en cada uno y picos en E. */
	void WriteCoreSplit(FJsonObject& Item, const TArray<float>& FrameMs, const TArray<uint8>& OnECore, float SpikeThresholdMs)
	{
		if (FrameMs.Num() == 0 || FrameMs.Num() != OnECore.Num())
		{
			return;
		}
		TArray<float> OnE;
		TArray<float> OnP;
		int32 Spikes = 0;
		int32 SpikesOnE = 0;
		for (int32 Index = 0; Index < FrameMs.Num(); ++Index)
		{
			const bool bE = OnECore[Index] != 0;
			(bE ? OnE : OnP).Add(FrameMs[Index]);
			if (FrameMs[Index] > SpikeThresholdMs)
			{
				++Spikes;
				SpikesOnE += bE ? 1 : 0;
			}
		}
		Item.SetNumberField(TEXT("frames_on_efficiency_core_pct"), 100.0 * OnE.Num() / FrameMs.Num());
		Item.SetNumberField(TEXT("frame_p50_ms_on_efficiency_core"), TNMonkey::Summarize(OnE).P50);
		Item.SetNumberField(TEXT("frame_p50_ms_on_performance_core"), TNMonkey::Summarize(OnP).P50);
		Item.SetNumberField(TEXT("spikes_on_efficiency_core_pct"), Spikes > 0 ? 100.0 * SpikesOnE / Spikes : 0.0);
	}

	/** Clases con Tick que salen en el informe de cada fase (actores y componentes juntos, de más a menos). */
	constexpr int32 TopTickingClasses = 30;

	UTN_StressSubsystem* FromWorld(UWorld* World)
	{
		return World ? World->GetSubsystem<UTN_StressSubsystem>() : nullptr;
	}

	void RunCommand(const TArray<FString>& Args, UWorld* World)
	{
		UTN_StressSubsystem* Stress = FromWorld(World);
		if (!Stress)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Estrés] TN.Stress: solo en un mundo de juego."));
			return;
		}
		if (Args.Num() > 0 && Args[0].Equals(TEXT("stop"), ESearchCase::IgnoreCase))
		{
			if (UTN_StressChaosSubsystem* Chaos = World->GetSubsystem<UTN_StressChaosSubsystem>())
			{
				Chaos->StopChaos(TEXT("parado a mano"));
			}
			Stress->StopSession(TEXT("parado a mano"));
			return;
		}
		// caos (TN_StressChaos.h): lo lleva su propio subsistema; segundos = por fase (20 por defecto).
		if (Args.Num() > 0 && TNChaos::IsChaosName(Args[0]))
		{
			if (UTN_StressChaosSubsystem* Chaos = World->GetSubsystem<UTN_StressChaosSubsystem>())
			{
				Chaos->StartChaos(Args.IsValidIndex(1) ? FMath::Clamp(FCString::Atof(*Args[1]), 5.f, 300.f) : 20.f, 0.f, 1.f, false);
			}
			return;
		}
		TNStress::FScenario Scenario;
		if (Args.Num() < 1 || !TNStress::Parse(Args[0], Scenario))
		{
			UE_LOG(LogTortunabo, Display, TEXT("[Estrés] Uso: TN.Stress <light|heavy|tortugas8|control> [segundos=60] | TN.Stress caos [segundos por fase=20] | TN.Stress stop. light = 50 enemigos, 100 lanzables, 20 cajas; heavy = 200/500/100; tortugas8 = 8 tortugas."));
			return;
		}
		const float Total = Args.IsValidIndex(1) ? FMath::Clamp(FCString::Atof(*Args[1]), 6.f, 600.f) : 60.f;
		Stress->StartSession(Scenario, 0.f, Total, true, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs CmdStress(TEXT("TN.Stress"),
		TEXT("Prueba de estrés: TN.Stress <light|heavy|tortugas8|control> [segundos=60] | TN.Stress caos [segundos por fase=20] | TN.Stress stop. Informe en Saved/Stress/."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunCommand), ECVF_Cheat);
}

bool UTN_StressSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
#endif
}

void UTN_StressSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
#if !UE_BUILD_SHIPPING
	// -TNStress=<escenario> [-TNStressSeconds=60] [-TNStressWarmup=10] [-TNStressNoMonkey] [-TNQuitWhenDone]
	const FString Name = TNTestReport::CommandLineValue(TEXT("-TNStress"));
	TNStress::FScenario Parsed;
	if (Name.IsEmpty() || !TNStress::Parse(Name, Parsed))
	{
		return;
	}
	const FString Seconds = TNTestReport::CommandLineValue(TEXT("-TNStressSeconds"));
	const FString Warmup = TNTestReport::CommandLineValue(TEXT("-TNStressWarmup"));
	StartSession(Parsed, Warmup.IsEmpty() ? 10.f : FCString::Atof(*Warmup), Seconds.IsEmpty() ? 60.f : FMath::Clamp(FCString::Atof(*Seconds), 6.f, 600.f),
		!FParse::Param(FCommandLine::Get(), TEXT("TNStressNoMonkey")), FParse::Param(FCommandLine::Get(), TEXT("TNQuitWhenDone")));
#endif
}

void UTN_StressSubsystem::Deinitialize()
{
	if (bActive)
	{
		Finish(TEXT("el mundo se ha cerrado"));
	}
	Super::Deinitialize();
}

bool UTN_StressSubsystem::StartSession(const TNStress::FScenario& InScenario, float InWarmup, float InTotal, bool bInDriveTurtles, bool bQuit)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	UWorld* World = GetWorld();
	if (bActive || !World || World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Estrés] No se puede empezar (ya hay una prueba o este mundo no tiene autoridad)."));
		return false;
	}
	Scenario = InScenario;
	WarmupSeconds = InWarmup;
	TotalSeconds = InTotal;
	bDriveTurtles = bInDriveTurtles;
	bQuitWhenDone = bQuit;
	bActive = true;
	bMeasuring = false;
	CurrentPhase = INDEX_NONE;
	Spawned.Reset();
	Phases.Reset();
	for (const TNStress::FPhase& Plan : TNStress::BuildTimeline(Scenario, TotalSeconds))
	{
		FPhaseData& Data = Phases.AddDefaulted_GetRef();
		Data.Plan = Plan;
	}
	Stream.Initialize(4242);
	// Sin ventana visible ni audio (-nullrhi -nosound), Windows baja la QoS del proceso y en CPU híbridas manda el hilo de juego
	// a núcleos de eficiencia a ratos: picos de 1,6-2 veces que no son del juego. -TNStressDefaultQoS mide sin corregirlo.
	bHighQoSRequested = !FParse::Param(FCommandLine::Get(), TEXT("TNStressDefaultQoS"));
	bHighQoSApplied = bHighQoSRequested && TNCpuCore::RequestHighQoS();
	bPerformanceCoresApplied = bHighQoSRequested && TNCpuCore::PreferPerformanceCores();
	bLastFrameOnECore = false;
	SessionStart = FPlatformTime::Seconds();
	MemoryStartMB = TNTestReport::UsedPhysicalMB();

	// Sin tope de fotogramas: el tiempo entre fotogramas es entonces lo que cuesta el juego (sin esperas de sincronía).
	if (IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
	{
		SavedMaxFps = MaxFps->GetInt();
		MaxFps->Set(0, ECVF_SetByConsole);
		bMaxFpsChanged = true;
	}
	if (bDriveTurtles || Scenario.Turtles > 1)
	{
		if (UTN_MonkeySubsystem* Monkey = World->GetSubsystem<UTN_MonkeySubsystem>())
		{
			FTNMonkeyConfig Config;
			Config.Seconds = WarmupSeconds + TotalSeconds + 5.f;
			Config.Seed = 7;
			Config.Players = FMath::Max(1, Scenario.Turtles);
			Config.WarmupSeconds = 0.f;
			MonkeyReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Stress"), FString::Printf(TEXT("monkey_%s.json"), *Scenario.Name));
			Config.OutPath = MonkeyReportPath;
			if (!bDriveTurtles)
			{
				// Solo las tortugas extra hacen falta: el mono las mueve igual (la referencia sin mono no compara con ellas).
			}
			Monkey->StartSession(Config);
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Estrés] %s: %d enemigos, %d tortugas; %d fases en %.0f s (espera %.0f s)."), *Scenario.Name, Scenario.Enemies,
		Scenario.Turtles, Phases.Num(), TotalSeconds, WarmupSeconds);
	return true;
#endif
}

void UTN_StressSubsystem::StopSession(const TCHAR* Reason)
{
	if (bActive)
	{
		Finish(Reason);
	}
}

double UTN_StressSubsystem::GroundAt(const FVector& At, double Fallback) const
{
	UWorld* World = GetWorld();
	FHitResult Hit;
	if (World && World->LineTraceSingleByChannel(Hit, At + FVector(0.0, 0.0, 3000.0), At - FVector(0.0, 0.0, 6000.0), ECC_WorldStatic))
	{
		return Hit.ImpactPoint.Z;
	}
	return Fallback;
}

FVector UTN_StressSubsystem::PickSpot(float MinRadius, float MaxRadius)
{
	const double Angle = Stream.FRandRange(0.f, 2.f * UE_PI);
	const double Radius = Stream.FRandRange(MinRadius, MaxRadius);
	FVector At = CenterAtStart + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * Radius;
	At.Z = GroundAt(At, CenterAtStart.Z - 90.0);
	return At;
}

bool UTN_StressSubsystem::SpawnEnemy(TNStress::EGroup Group, int32 Index)
{
	const FVector At = PickSpot(1500.f, 14000.f);
	const FRotator Facing(0.0, Stream.FRandRange(0.f, 360.f), 0.0);
	AActor* Enemy = nullptr;
	if (Group == TNStress::EGroup::Patrols)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		Enemy = GetWorld()->SpawnActor<ATN_CrabActor>(TNStressEnemies::PatrolCrabClass(), At, Facing, Params);
	}
	else
	{
		FTNBeachElementSpec Spec;
		Spec.Seed = Stream.RandRange(1, 1000000);
		Spec.SizeScale = 1.f;
		Spec.Element = Group == TNStress::EGroup::Gulls ? ETNBeachElement::GullZone
			: (Index % 2 == 0 ? ETNBeachElement::DragCrab : ETNBeachElement::BurrowCrab);
		Enemy = ATN_BeachElement::SpawnElement(GetWorld(), FTransform(Facing, At), Spec);
	}
	if (Enemy)
	{
		Spawned.Add(Enemy);
	}
	return Enemy != nullptr;
}

void UTN_StressSubsystem::SpawnPending(FPhaseData& Phase)
{
	if (Phase.PendingSpawn <= 0)
	{
		return;
	}
	const double Start = FPlatformTime::Seconds();
	int32 MadeThisFrame = 0;
	while (TNStress::ShouldSpawnMore(Phase.PendingSpawn, MadeThisFrame, (FPlatformTime::Seconds() - Start) * 1000.0))
	{
		const bool bMade = SpawnEnemy(Phase.Plan.Group, Phase.Attempted);
		Phase.Spawned += bMade ? 1 : 0;
		++Phase.Attempted;
		--Phase.PendingSpawn;
		++MadeThisFrame;
	}
	Phase.SpawnMaxFrameMs = FMath::Max(Phase.SpawnMaxFrameMs, static_cast<float>((FPlatformTime::Seconds() - Start) * 1000.0));
	++Phase.SpawnFrames;
	if (Phase.PendingSpawn <= 0)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Estrés] «%s»: %d de %d creados en %d fotogramas (máx. %.1f ms de creación en uno)."),
			TNStress::GroupName(Phase.Plan.Group), Phase.Spawned, Phase.Plan.Count, Phase.SpawnFrames, Phase.SpawnMaxFrameMs);
	}
}

void UTN_StressSubsystem::BeginPhase(int32 Index)
{
	CurrentPhase = Index;
	PhaseStart = FPlatformTime::Seconds();
	LastFrame = 0.0;
	FPhaseData& Phase = Phases[Index];
	// Centro: la tortuga del primer jugador (las demás salen a su lado).
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			CenterAtStart = Pawn->GetActorLocation();
		}
	}
	if (TNStress::IsSpreadGroup(Phase.Plan.Group))
	{
		// Repartidos en varios fotogramas (SpawnPending en cada Tick).
		Phase.PendingSpawn = Phase.Plan.Count;
		UE_LOG(LogTortunabo, Log, TEXT("[Estrés] Fase %d/%d «%s»: %d por crear (%.0f ms por fotograma)."), Index + 1, Phases.Num(),
			TNStress::GroupName(Phase.Plan.Group), Phase.Plan.Count, TNStress::SPAWN_BUDGET_MS);
		SpawnPending(Phase);
	}
}

void UTN_StressSubsystem::SampleFrame(FPhaseData& Phase)
{
	const double Now = FPlatformTime::Seconds();
	const bool bOnECore = TNCpuCore::IsOnEfficiencyCore();
	if (LastFrame > 0.0)
	{
		const float Ms = static_cast<float>((Now - LastFrame) * 1000.0);
		if (Now - PhaseStart < TNStressDetail::SettleSeconds)
		{
			Phase.HitchMs = FMath::Max(Phase.HitchMs, Ms);
		}
		else if (Phase.FrameMs.Num() < 200000)
		{
			Phase.FrameMs.Add(Ms);
			Phase.FrameStampMs.Add(static_cast<float>((Now - PhaseStart) * 1000.0));
			Phase.GameThreadMs.Add(static_cast<float>(FPlatformTime::ToMilliseconds(GGameThreadTime)));
			Phase.RenderThreadMs.Add(static_cast<float>(FPlatformTime::ToMilliseconds(GRenderThreadTime)));
			Phase.GpuMs.Add(static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles())));
			Phase.FrameOnECore.Add((bOnECore || bLastFrameOnECore) ? 1 : 0);
		}
	}
	LastFrame = Now;
	bLastFrameOnECore = bOnECore;
}

void UTN_StressSubsystem::SampleNet(FPhaseData& Phase)
{
	const UNetDriver* Driver = GetWorld()->GetNetDriver();
	if (!Driver || Driver->ClientConnections.Num() == 0)
	{
		return;
	}
	double In = 0.0;
	double Out = 0.0;
	for (const UNetConnection* Connection : Driver->ClientConnections)
	{
		if (Connection)
		{
			In += Connection->InBytesPerSecond;
			Out += Connection->OutBytesPerSecond;
		}
	}
	const double Count = static_cast<double>(Driver->ClientConnections.Num());
	Phase.Connections = Driver->ClientConnections.Num();
	Phase.NetInKBsSum += In / Count / 1024.0;
	Phase.NetOutKBsSum += Out / Count / 1024.0;
	Phase.NetMaxOutKBs = FMath::Max(Phase.NetMaxOutKBs, Out / Count / 1024.0);
	++Phase.NetSamples;
}

void UTN_StressSubsystem::EndPhase(int32 Index)
{
	FPhaseData& Phase = Phases[Index];
	UWorld* World = GetWorld();
	Phase.MemoryMB = TNTestReport::UsedPhysicalMB();
	TMap<FString, int32> ByClass;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const AActor* Actor = *It;
		++Phase.Actors;
		Phase.ReplicatedActors += Actor->GetIsReplicated() ? 1 : 0;
		if (Actor->PrimaryActorTick.IsTickFunctionEnabled())
		{
			++Phase.TickingActors;
			++ByClass.FindOrAdd(Actor->GetClass()->GetName());
		}
		for (const UActorComponent* Component : Actor->GetComponents())
		{
			if (Component && Component->PrimaryComponentTick.IsTickFunctionEnabled())
			{
				++Phase.TickingComponents;
				++ByClass.FindOrAdd(Component->GetClass()->GetName() + TEXT(" (componente)"));
			}
		}
	}
	for (const TPair<FString, int32>& Pair : ByClass)
	{
		Phase.TopTicking.Add(TPair<FString, int32>(Pair.Key, Pair.Value));
	}
	Phase.TopTicking.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) { return A.Value > B.Value; });
	if (Phase.TopTicking.Num() > TNStressDetail::TopTickingClasses)
	{
		Phase.TopTicking.SetNum(TNStressDetail::TopTickingClasses);
	}
}

void UTN_StressSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bActive)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (!bMeasuring)
	{
		if (Now - SessionStart < WarmupSeconds)
		{
			return;
		}
		bMeasuring = true;
		MeasureStart = Now;
		LastNetSample = Now;
		BeginPhase(0);
		return;
	}
	const double T = Now - MeasureStart;
	// Cambio de fase.
	while (CurrentPhase != INDEX_NONE && CurrentPhase < Phases.Num() && T >= Phases[CurrentPhase].Plan.End)
	{
		EndPhase(CurrentPhase);
		if (CurrentPhase + 1 >= Phases.Num())
		{
			Finish(TEXT("escenario completo"));
			return;
		}
		BeginPhase(CurrentPhase + 1);
	}
	if (CurrentPhase == INDEX_NONE || !Phases.IsValidIndex(CurrentPhase))
	{
		return;
	}
	FPhaseData& Phase = Phases[CurrentPhase];
	SampleFrame(Phase);
	SpawnPending(Phase);
	if (Now - LastNetSample >= 1.0)
	{
		LastNetSample = Now;
		SampleNet(Phase);
	}
}

TSharedRef<FJsonObject> UTN_StressSubsystem::BuildReport(const TCHAR* Reason) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	const UWorld* World = GetWorld();
	Root->SetStringField(TEXT("tool"), TEXT("TN.Stress"));
	Root->SetStringField(TEXT("scenario"), Scenario.Name);
	Root->SetStringField(TEXT("map"), World ? World->GetMapName() : FString());
	Root->SetStringField(TEXT("build"), TNTestReport::BuildConfigName());
	Root->SetStringField(TEXT("net_mode"), TNTestReport::NetModeName(World));
	Root->SetStringField(TEXT("end_reason"), Reason);
	Root->SetBoolField(TEXT("rendering"), FApp::CanEverRender());
	Root->SetNumberField(TEXT("enemies_requested"), Scenario.Enemies);
	Root->SetNumberField(TEXT("turtles"), Scenario.Turtles);
	Root->SetNumberField(TEXT("measured_seconds"), TotalSeconds);
	Root->SetNumberField(TEXT("memory_start_mb"), MemoryStartMB);
	Root->SetNumberField(TEXT("memory_peak_mb"), TNTestReport::PeakPhysicalMB());
	Root->SetStringField(TEXT("monkey_report"), MonkeyReportPath);
	Root->SetBoolField(TEXT("cpu_hybrid"), TNCpuCore::IsHybrid());
	Root->SetBoolField(TEXT("high_qos_requested"), bHighQoSRequested);
	Root->SetBoolField(TEXT("high_qos_applied"), bHighQoSApplied);
	Root->SetBoolField(TEXT("performance_cores_only"), bPerformanceCoresApplied);

	TArray<TSharedPtr<FJsonValue>> PhasesJson;
	TArray<TSharedPtr<FJsonValue>> CostsJson;
	TArray<TNMonkey::FFrameSummary> Summaries;
	for (int32 Index = 0; Index < Phases.Num(); ++Index)
	{
		const FPhaseData& Phase = Phases[Index];
		const TNMonkey::FFrameSummary Frame = TNMonkey::Summarize(Phase.FrameMs);
		Summaries.Add(Frame);
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("group"), TNStress::GroupName(Phase.Plan.Group));
		Item->SetNumberField(TEXT("requested"), Phase.Plan.Count);
		Item->SetNumberField(TEXT("spawned"), Phase.Spawned);
		Item->SetNumberField(TEXT("frames"), Frame.Frames);
		Item->SetNumberField(TEXT("frame_avg_ms"), Frame.Average);
		Item->SetNumberField(TEXT("frame_p50_ms"), Frame.P50);
		Item->SetNumberField(TEXT("frame_p95_ms"), Frame.P95);
		Item->SetNumberField(TEXT("frame_p99_ms"), Frame.P99);
		Item->SetNumberField(TEXT("frame_max_ms"), Frame.Max);
		Item->SetNumberField(TEXT("spawn_hitch_ms"), Phase.HitchMs);
		Item->SetNumberField(TEXT("spawn_frames"), Phase.SpawnFrames);
		Item->SetNumberField(TEXT("spawn_max_frame_ms"), Phase.SpawnMaxFrameMs);
		int32 Spikes = 0;
		float SpikePeriod = 0.f;
		TNMonkey::FindSpikePeriod(Phase.FrameMs, Phase.FrameStampMs, 1.8f, Spikes, SpikePeriod);
		Item->SetNumberField(TEXT("spikes_over_1_8x_median"), Spikes);
		Item->SetNumberField(TEXT("spike_median_period_ms"), SpikePeriod);
		TNStressDetail::WriteCoreSplit(*Item, Phase.FrameMs, Phase.FrameOnECore, Frame.P50 * 1.8f);
		Item->SetNumberField(TEXT("game_thread_avg_ms"), TNMonkey::Summarize(Phase.GameThreadMs).Average);
		Item->SetNumberField(TEXT("render_thread_avg_ms"), TNMonkey::Summarize(Phase.RenderThreadMs).Average);
		Item->SetNumberField(TEXT("gpu_avg_ms"), TNMonkey::Summarize(Phase.GpuMs).Average);
		Item->SetNumberField(TEXT("memory_mb"), Phase.MemoryMB);
		Item->SetNumberField(TEXT("actors"), Phase.Actors);
		Item->SetNumberField(TEXT("replicated_actors"), Phase.ReplicatedActors);
		Item->SetNumberField(TEXT("ticking_actors"), Phase.TickingActors);
		Item->SetNumberField(TEXT("ticking_components"), Phase.TickingComponents);
		Item->SetNumberField(TEXT("connections"), Phase.Connections);
		Item->SetNumberField(TEXT("net_out_kb_s_per_connection_avg"), Phase.NetSamples > 0 ? Phase.NetOutKBsSum / Phase.NetSamples : 0.0);
		Item->SetNumberField(TEXT("net_out_kb_s_per_connection_max"), Phase.NetMaxOutKBs);
		Item->SetNumberField(TEXT("net_in_kb_s_per_connection_avg"), Phase.NetSamples > 0 ? Phase.NetInKBsSum / Phase.NetSamples : 0.0);
		TArray<TSharedPtr<FJsonValue>> Top;
		for (const TPair<FString, int32>& Pair : Phase.TopTicking)
		{
			TSharedRef<FJsonObject> Class = MakeShared<FJsonObject>();
			Class->SetStringField(TEXT("class"), Pair.Key);
			Class->SetNumberField(TEXT("ticking"), Pair.Value);
			Top.Add(MakeShared<FJsonValueObject>(Class));
		}
		Item->SetArrayField(TEXT("top_ticking_classes"), Top);
		PhasesJson.Add(MakeShared<FJsonValueObject>(Item));

		// Coste marginal del grupo: esta fase menos la anterior.
		if (Index > 0 && Phase.Spawned > 0 && Summaries[Index - 1].Frames > 0 && Frame.Frames > 0)
		{
			const float DeltaMs = Frame.Average - Summaries[Index - 1].Average;
			const FPhaseData& Prev = Phases[Index - 1];
			const double NetDelta = (Phase.NetSamples > 0 ? Phase.NetOutKBsSum / Phase.NetSamples : 0.0) - (Prev.NetSamples > 0 ? Prev.NetOutKBsSum / Prev.NetSamples : 0.0);
			TSharedRef<FJsonObject> Cost = MakeShared<FJsonObject>();
			Cost->SetStringField(TEXT("group"), TNStress::GroupName(Phase.Plan.Group));
			const int32 Entities = Phase.Spawned;
			Cost->SetNumberField(TEXT("spawned"), Phase.Spawned);
			Cost->SetNumberField(TEXT("entities_alive"), Entities);
			Cost->SetNumberField(TEXT("delta_frame_avg_ms"), DeltaMs);
			Cost->SetNumberField(TEXT("delta_frame_p95_ms"), Frame.P95 - Summaries[Index - 1].P95);
			Cost->SetNumberField(TEXT("ms_per_entity"), DeltaMs / Entities);
			Cost->SetNumberField(TEXT("delta_memory_mb"), Phase.MemoryMB - Prev.MemoryMB);
			Cost->SetNumberField(TEXT("delta_ticking_actors"), Phase.TickingActors - Prev.TickingActors);
			Cost->SetNumberField(TEXT("delta_net_out_kb_s_per_connection"), NetDelta);
			Cost->SetNumberField(TEXT("spawn_hitch_ms"), Phase.HitchMs);
			CostsJson.Add(MakeShared<FJsonValueObject>(Cost));
		}
	}
	Root->SetArrayField(TEXT("phases"), PhasesJson);
	Root->SetArrayField(TEXT("marginal_costs"), CostsJson);
	return Root;
}

void UTN_StressSubsystem::Finish(const TCHAR* Reason)
{
	if (bMeasuring && Phases.IsValidIndex(CurrentPhase) && Phases[CurrentPhase].Actors == 0)
	{
		EndPhase(CurrentPhase);
	}
	const TSharedRef<FJsonObject> Report = BuildReport(Reason);
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Stress"),
		Scenario.Name + TEXT("_") + FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S")) + TEXT(".json"));
	const bool bSaved = TNTestReport::Save(*Report, Path);
	UE_LOG(LogTortunabo, Log, TEXT("[Estrés] Terminado (%s). Informe %s: %s"), Reason, bSaved ? TEXT("guardado") : TEXT("NO guardado"), *Path);

	for (const TWeakObjectPtr<AActor>& Actor : Spawned)
	{
		if (Actor.IsValid())
		{
			Actor->Destroy();
		}
	}
	Spawned.Reset();
	if (UWorld* World = GetWorld())
	{
		if (UTN_MonkeySubsystem* Monkey = World->GetSubsystem<UTN_MonkeySubsystem>())
		{
			Monkey->StopSession(TEXT("acaba el estrés"));
		}
	}
	if (bMaxFpsChanged)
	{
		if (IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
		{
			MaxFps->Set(SavedMaxFps, ECVF_SetByConsole);
		}
		bMaxFpsChanged = false;
	}
	bActive = false;
	bMeasuring = false;
	if (bQuitWhenDone)
	{
		FPlatformMisc::RequestExitWithStatus(false, 0);
	}
}
