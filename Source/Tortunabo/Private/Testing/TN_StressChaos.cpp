#include "Testing/TN_StressChaos.h"

#include "Core/TN_InventoryTypes.h"
#include "Core/TN_Log.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Player/TortugaCharacter.h"
#include "RHI.h"
#include "RHIStats.h"
#include "RenderTimer.h"
#include "Scalability.h"
#include "Testing/TN_CpuCoreProbe.h"
#include "Testing/TN_MonkeyPlan.h"
#include "Testing/TN_TestReport.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachTypes.h"
#include "World/TN_InkProjectile.h"
#include "World/TN_ThrowableItemActor.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <dxgi1_4.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace TNChaosDetail
{
	/** Segundos al principio de cada fase que no cuentan en los percentiles (el parón de crear lo de la fase). */
	constexpr double SettleSeconds = 1.5;
	/** Cada cuánto se mide lo lento (memoria, VRAM, red). */
	constexpr double SlowSampleSeconds = 0.5;
	constexpr int32 TopTickingClasses = 20;
	const TCHAR* const CatalogPath = TEXT("/Game/Blueprints/Gameplay/Items/DT_Items.DT_Items");

	double ToMB(uint64 Bytes) { return static_cast<double>(Bytes) / (1024.0 * 1024.0); }

#if PLATFORM_WINDOWS
	/**
	 * VRAM que usa este proceso (DXGI QueryVideoMemoryInfo, segmento local) en el adaptador con más memoria dedicada. Se carga
	 * dxgi.dll a mano para no añadir dependencias de enlace al módulo. -1 si no se puede leer.
	 */
	double ProcessVramMB(double* OutBudgetMB = nullptr)
	{
		using FCreateFactory = HRESULT(WINAPI*)(REFIID, void**);
		static FCreateFactory CreateFactory = nullptr;
		static bool bTried = false;
		if (!bTried)
		{
			bTried = true;
			if (HMODULE Dxgi = ::LoadLibraryW(L"dxgi.dll"))
			{
				CreateFactory = reinterpret_cast<FCreateFactory>(reinterpret_cast<void*>(::GetProcAddress(Dxgi, "CreateDXGIFactory1")));
			}
		}
		if (!CreateFactory)
		{
			return -1.0;
		}
		IDXGIFactory1* Factory = nullptr;
		if (FAILED(CreateFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&Factory))) || !Factory)
		{
			return -1.0;
		}
		double Best = -1.0;
		SIZE_T BestDedicated = 0;
		IDXGIAdapter1* Adapter = nullptr;
		for (UINT Index = 0; Factory->EnumAdapters1(Index, &Adapter) != DXGI_ERROR_NOT_FOUND; ++Index)
		{
			DXGI_ADAPTER_DESC1 Desc;
			IDXGIAdapter3* Adapter3 = nullptr;
			if (SUCCEEDED(Adapter->GetDesc1(&Desc)) && Desc.DedicatedVideoMemory >= BestDedicated
				&& SUCCEEDED(Adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&Adapter3))) && Adapter3)
			{
				DXGI_QUERY_VIDEO_MEMORY_INFO Info;
				if (SUCCEEDED(Adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &Info)))
				{
					BestDedicated = Desc.DedicatedVideoMemory;
					Best = ToMB(Info.CurrentUsage);
					if (OutBudgetMB)
					{
						*OutBudgetMB = ToMB(Info.Budget);
					}
				}
				Adapter3->Release();
			}
			Adapter->Release();
			Adapter = nullptr;
		}
		Factory->Release();
		return Best;
	}
#else
	double ProcessVramMB(double* OutBudgetMB = nullptr) { return -1.0; }
#endif

	/** Memoria de texturas según el RHI (streaming + no streaming), MB; -1 sin RHI. */
	double TextureMemoryMB()
	{
		if (!GDynamicRHI || !FApp::CanEverRender())
		{
			return -1.0;
		}
		FTextureMemoryStats Stats;
		RHIGetTextureMemoryStats(Stats);
		return ToMB(Stats.StreamingMemorySize + Stats.NonStreamingMemorySize);
	}

	void AddSummary(FJsonObject& Item, const TCHAR* Prefix, const TArray<float>& Values)
	{
		const TNMonkey::FFrameSummary Summary = TNMonkey::Summarize(Values);
		Item.SetNumberField(FString::Printf(TEXT("%s_avg_ms"), Prefix), Summary.Average);
		Item.SetNumberField(FString::Printf(TEXT("%s_p50_ms"), Prefix), Summary.P50);
		Item.SetNumberField(FString::Printf(TEXT("%s_p95_ms"), Prefix), Summary.P95);
		Item.SetNumberField(FString::Printf(TEXT("%s_p99_ms"), Prefix), Summary.P99);
		Item.SetNumberField(FString::Printf(TEXT("%s_max_ms"), Prefix), Summary.Max);
	}
}

bool UTN_StressChaosSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
#endif
}

void UTN_StressChaosSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
#if !UE_BUILD_SHIPPING
	// -TNStress=caos [-TNStressSeconds=<por fase>] [-TNStressWarmup=20] [-TNChaosEnemies=1] [-TNQuitWhenDone]. En un cliente solo
	// arranca en el mundo conectado (no en el menú del que sale antes de entrar en la partida).
	// Una vez por proceso: si la partida acaba y se viaja (al lobby, por ejemplo), no se vuelve a empezar allí.
	static bool bStartedFromCommandLine = false;
	if (bStartedFromCommandLine || !TNChaos::IsChaosName(TNTestReport::CommandLineValue(TEXT("-TNStress"))))
	{
		return;
	}
	const FString MapName = InWorld.GetMapName();
	if (MapName.Contains(TEXT("LVL_Menu")) || MapName.Contains(TEXT("Entry")))
	{
		return;
	}
	const FString Seconds = TNTestReport::CommandLineValue(TEXT("-TNStressSeconds"));
	const FString Warmup = TNTestReport::CommandLineValue(TEXT("-TNStressWarmup"));
	const FString Enemies = TNTestReport::CommandLineValue(TEXT("-TNChaosEnemies"));
	bStartedFromCommandLine = true;
	StartChaos(Seconds.IsEmpty() ? 20.f : FMath::Clamp(FCString::Atof(*Seconds), 5.f, 300.f), Warmup.IsEmpty() ? 20.f : FCString::Atof(*Warmup),
		Enemies.IsEmpty() ? 1.f : FMath::Clamp(FCString::Atof(*Enemies), 0.f, 10.f), FParse::Param(FCommandLine::Get(), TEXT("TNQuitWhenDone")));
#endif
}

void UTN_StressChaosSubsystem::Deinitialize()
{
	if (bActive)
	{
		Finish(TEXT("el mundo se ha cerrado"));
	}
	Super::Deinitialize();
}

bool UTN_StressChaosSubsystem::StartChaos(float PhaseSeconds, float InWarmup, float EnemyScale, bool bQuit)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	UWorld* World = GetWorld();
	if (bActive || !World)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Estrés] caos: ya hay uno en marcha o no hay mundo."));
		return false;
	}
	bClientOnly = World->GetNetMode() == NM_Client;
	Config = TNChaos::FConfig();
	Config.PhaseSeconds = PhaseSeconds;
	Config.EnemyScale = EnemyScale;
	WarmupSeconds = FMath::Max(0.f, InWarmup);
	bQuitWhenDone = bQuit;
	bVerbose = FParse::Param(FCommandLine::Get(), TEXT("TNChaosVerbose"));
	Phases.Reset();
	for (const TNChaos::FPhase& Plan : TNChaos::BuildTimeline(Config))
	{
		Phases.AddDefaulted_GetRef().Plan = Plan;
	}
	Drivers.Reset();
	Spawned.Reset();
	Totals = FActions();
	CurrentPhase = INDEX_NONE;
	bMeasuring = false;
	bActive = true;
	Stream.Initialize(bClientOnly ? 5851 : 585);
	SessionStart = FPlatformTime::Seconds();
	MemoryStartMB = TNTestReport::UsedPhysicalMB();
	CommitStartMB = TNChaosDetail::ToMB(FPlatformMemory::GetStats().UsedVirtual);

	if (GLog && !bSinkAttached)
	{
		GLog->AddOutputDevice(&Sink);
		bSinkAttached = true;
	}
	IConsoleManager& Console = IConsoleManager::Get();
	if (IConsoleVariable* Corrections = Console.FindConsoleVariable(TEXT("p.NetShowCorrections")))
	{
		SavedNetShowCorrections = Corrections->GetInt();
		Corrections->Set(1, ECVF_SetByConsole);
	}
	// Las correcciones se cuentan por el registro; sus cápsulas de depuración duran un fotograma, no 4 s (no cargan el render).
	if (IConsoleVariable* CorrectionLifetime = Console.FindConsoleVariable(TEXT("p.NetCorrectionLifetime")))
	{
		SavedNetCorrectionLifetime = CorrectionLifetime->GetFloat();
		CorrectionLifetime->Set(0.f, ECVF_SetByConsole);
	}
	// Sin tope de fotogramas: el tiempo entre fotogramas es lo que cuesta el juego.
	if (IConsoleVariable* MaxFps = Console.FindConsoleVariable(TEXT("t.MaxFPS")))
	{
		SavedMaxFps = MaxFps->GetInt();
		MaxFps->Set(0, ECVF_SetByConsole);
	}
	bCVarsChanged = true;
	bHighQoSApplied = !FParse::Param(FCommandLine::Get(), TEXT("TNStressDefaultQoS")) && TNCpuCore::RequestHighQoS();
	if (bHighQoSApplied)
	{
		TNCpuCore::PreferPerformanceCores();
	}

	if (!bClientOnly)
	{
		EnsureLocalPlayers();
		Catalog = LoadObject<UDataTable>(nullptr, TNChaosDetail::CatalogPath);
		CatalogThrowables.Reset();
		if (Catalog)
		{
			for (const TPair<FName, uint8*>& Row : Catalog->GetRowMap())
			{
				const FTN_InventoryItem* Item = reinterpret_cast<const FTN_InventoryItem*>(Row.Value);
				const bool bThrowable = Item->UseType == ETN_ItemUseType::Throwable && Item->ThrowableData.ActorClass;
				const bool bInk = Item->UseType == ETN_ItemUseType::InkThrower && Item->InkData.ProjectileClass;
				if (bThrowable || bInk)
				{
					CatalogThrowables.Add(Row.Key);
				}
			}
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Estrés] caos (%s): %d fases de %.0f s, espera %.0f s, enemigos x%.1f, %d lanzables de DT_Items."),
		bClientOnly ? TEXT("cliente") : TEXT("anfitrión"), Phases.Num(), Config.PhaseSeconds, WarmupSeconds, Config.EnemyScale, CatalogThrowables.Num());
	return true;
#endif
}

void UTN_StressChaosSubsystem::StopChaos(const TCHAR* Reason)
{
	if (bActive)
	{
		Finish(Reason);
	}
}

void UTN_StressChaosSubsystem::EnsureLocalPlayers()
{
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (!GameInstance)
	{
		return;
	}
	UGameViewportClient* Viewport = World->GetGameViewport();
	CreatedLocalPlayers.Reset();
	if (Viewport)
	{
		bSavedForceDisableSplitscreen = Viewport->IsSplitscreenForceDisabled();
		SavedMaxSplitscreenPlayers = Viewport->MaxSplitscreenPlayers;
		Viewport->MaxSplitscreenPlayers = FMath::Max(Viewport->MaxSplitscreenPlayers, Config.Turtles);
		// Cada jugador tiene su PC: la GPU pinta una sola vista, la del primero.
		Viewport->SetForceDisableSplitscreen(true);
		bSplitscreenForcedOff = true;
	}
	while (GameInstance->GetNumLocalPlayers() < Config.Turtles)
	{
		APlayerController* Created = UGameplayStatics::CreatePlayer(World, -1, true);
		if (!Created)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Estrés] caos: no se ha podido crear el jugador local %d."), GameInstance->GetNumLocalPlayers());
			break;
		}
		CreatedLocalPlayers.Add(Created->GetLocalPlayer());
	}
}

void UTN_StressChaosSubsystem::RestoreWorld()
{
	UWorld* World = GetWorld();
	// Los jugadores extra salen con su tortuga: la partida vuelve a los jugadores que tenía.
	for (const TWeakObjectPtr<ULocalPlayer>& Weak : CreatedLocalPlayers)
	{
		ULocalPlayer* Player = Weak.Get();
		if (!Player)
		{
			continue;
		}
		// Viven en la GameInstance: si no se quitan, pasarían al mapa siguiente.
		if (APlayerController* PC = World ? Player->GetPlayerController(World) : nullptr)
		{
			UGameplayStatics::RemovePlayer(PC, true);
		}
		else if (UGameInstance* GameInstance = Player->GetGameInstance())
		{
			GameInstance->RemoveLocalPlayer(Player);
		}
	}
	CreatedLocalPlayers.Reset();
	if (bSplitscreenForcedOff)
	{
		if (UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr)
		{
			Viewport->MaxSplitscreenPlayers = SavedMaxSplitscreenPlayers;
			Viewport->SetForceDisableSplitscreen(bSavedForceDisableSplitscreen);
		}
		bSplitscreenForcedOff = false;
	}
}

double UTN_StressChaosSubsystem::GroundAt(const FVector& At, double Fallback) const
{
	FHitResult Hit;
	const UWorld* World = GetWorld();
	if (World && World->LineTraceSingleByChannel(Hit, At + FVector(0.0, 0.0, 3000.0), At - FVector(0.0, 0.0, 6000.0), ECC_WorldStatic))
	{
		return Hit.ImpactPoint.Z;
	}
	return Fallback;
}

FVector UTN_StressChaosSubsystem::PickSpot(const FVector& Center, float MinRadius, float MaxRadius)
{
	const double Angle = Stream.FRandRange(0.f, 2.f * UE_PI);
	const double Radius = Stream.FRandRange(MinRadius, MaxRadius);
	FVector At = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * Radius;
	At.Z = GroundAt(At, Center.Z - 90.0);
	return At;
}

FVector UTN_StressChaosSubsystem::TurtlesCenter() const
{
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (TActorIterator<ATortugaCharacter> It(GetWorld()); It; ++It)
	{
		Sum += It->GetActorLocation();
		++Count;
	}
	return Count > 0 ? Sum / Count : FVector::ZeroVector;
}

int32 UTN_StressChaosSubsystem::SpawnEnemies(int32 Crabs, int32 Gulls, int32 Tanks)
{
	UWorld* World = GetWorld();
	const FVector Center = TurtlesCenter();
	int32 Made = 0;
	auto Spawn = [&](ETNBeachElement Element, float Extent)
	{
		FTNBeachElementSpec Spec;
		Spec.Element = Element;
		Spec.Seed = Stream.RandRange(1, 1000000);
		Spec.SizeScale = 1.f;
		Spec.Extent = Extent;
		// Cerca de las tortugas (8-40 m): que las vean y las persigan.
		const FVector At = PickSpot(Center, 800.f, 4000.f);
		if (ATN_BeachElement* Spawned1 = ATN_BeachElement::SpawnElement(World, FTransform(FRotator(0.0, Stream.FRandRange(0.f, 360.f), 0.0), At), Spec))
		{
			Spawned.Add(Spawned1);
			++Made;
		}
	};
	for (int32 Index = 0; Index < Crabs; ++Index)
	{
		const bool bHermit = Index % 2 == 1;
		Spawn(bHermit ? ETNBeachElement::HermitCrab : ETNBeachElement::GiantCrab, bHermit ? 3000.f : 0.f);
	}
	for (int32 Index = 0; Index < Gulls; ++Index)
	{
		Spawn(ETNBeachElement::GullZone, 0.f);
	}
	for (int32 Index = 0; Index < Tanks; ++Index)
	{
		Spawn(ETNBeachElement::ToyTank, 2400.f);
	}
	return Made;
}

void UTN_StressChaosSubsystem::BeginPhase(int32 Index)
{
	CurrentPhase = Index;
	PhaseStart = FPlatformTime::Seconds();
	LastFrame = 0.0;
	FPhaseData& Phase = Phases[Index];
	// Región de Unreal Insights por fase (TNChaos_<fase>): TimingInsights.ExportTimerStatistics -region=TNChaos_* la exporta aparte.
	TRACE_BEGIN_REGION(*FString::Printf(TEXT("TNChaos_%s"), TNChaos::StepName(Phase.Plan.Step)));
	Phase.CorrectionsAtStart = Sink.GetNetCorrectionCount();
	if (!bClientOnly)
	{
		Phase.Created += SpawnEnemies(Phase.Plan.Crabs, Phase.Plan.Gulls, Phase.Plan.Tanks);
	}
	// Lo que estrena la fase empieza ya: se cortan las tareas que se pueden dejar (andar, objetos, bola sin empezar).
	for (FDriver& Driver : Drivers)
	{
		const bool bInterruptible = Driver.Task == TNChaos::ETask::Wander || Driver.Task == TNChaos::ETask::Items;
		if (bInterruptible && !Driver.bBait)
		{
			Driver.TaskClock = 1000.f;
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Estrés] caos fase %d/%d «%s»: %d creados."), Index + 1, Phases.Num(), TNChaos::StepName(Phase.Plan.Step), Phase.Created);
}

void UTN_StressChaosSubsystem::SampleFrame(FPhaseData& Phase)
{
	const double Now = FPlatformTime::Seconds();
	if (LastFrame > 0.0)
	{
		const float Ms = static_cast<float>((Now - LastFrame) * 1000.0);
		if (Now - PhaseStart < TNChaosDetail::SettleSeconds)
		{
			Phase.SpawnHitchMs = FMath::Max(Phase.SpawnHitchMs, Ms);
		}
		else if (Phase.FrameMs.Num() < 200000)
		{
			Phase.FrameMs.Add(Ms);
			Phase.GameThreadMs.Add(static_cast<float>(FPlatformTime::ToMilliseconds(GGameThreadTime)));
			Phase.RenderThreadMs.Add(static_cast<float>(FPlatformTime::ToMilliseconds(GRenderThreadTime)));
			Phase.RhiThreadMs.Add(static_cast<float>(FPlatformTime::ToMilliseconds(GRHIThreadTime)));
			Phase.GpuMs.Add(static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles())));
		}
	}
	LastFrame = Now;
}

void UTN_StressChaosSubsystem::SampleSlow(FPhaseData& Phase)
{
	const double Memory = TNTestReport::UsedPhysicalMB();
	Phase.CommitMaxMB = FMath::Max(Phase.CommitMaxMB, TNChaosDetail::ToMB(FPlatformMemory::GetStats().UsedVirtual));
	Phase.MemorySumMB += Memory;
	Phase.MemoryMaxMB = FMath::Max(Phase.MemoryMaxMB, Memory);
	++Phase.MemorySamples;
	if (FApp::CanEverRender())
	{
		const double Vram = TNChaosDetail::ProcessVramMB(&VramBudgetMB);
		Phase.VramMaxMB = FMath::Max(Phase.VramMaxMB, Vram);
		Phase.VramEndMB = Vram;
	}
	const UNetDriver* Driver = GetWorld()->GetNetDriver();
	if (bClientOnly || !Driver || Driver->ClientConnections.Num() == 0)
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

void UTN_StressChaosSubsystem::EndPhase(int32 Index)
{
	FPhaseData& Phase = Phases[Index];
	TRACE_END_REGION(*FString::Printf(TEXT("TNChaos_%s"), TNChaos::StepName(Phase.Plan.Step)));
	Phase.MemoryEndMB = TNTestReport::UsedPhysicalMB();
	Phase.CommitEndMB = TNChaosDetail::ToMB(FPlatformMemory::GetStats().UsedVirtual);
	Phase.TextureMemoryMB = TNChaosDetail::TextureMemoryMB();
	Phase.Corrections = Sink.GetNetCorrectionCount() - Phase.CorrectionsAtStart;
	TMap<FString, int32> ByClass;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
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
		Phase.TopTicking.Add(Pair);
	}
	Phase.TopTicking.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) { return A.Value > B.Value; });
	if (Phase.TopTicking.Num() > TNChaosDetail::TopTickingClasses)
	{
		Phase.TopTicking.SetNum(TNChaosDetail::TopTickingClasses);
	}
}

UTN_StressChaosSubsystem::FActions& UTN_StressChaosSubsystem::CurrentActions()
{
	return Phases.IsValidIndex(CurrentPhase) && bMeasuring ? Phases[CurrentPhase].Actions : Totals;
}

void UTN_StressChaosSubsystem::Tick(float DeltaTime)
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
		LastSlowSample = Now;
		SyncDrivers();
		BeginPhase(0);
		return;
	}
	const double T = Now - MeasureStart;
	while (Phases.IsValidIndex(CurrentPhase) && T >= Phases[CurrentPhase].Plan.End)
	{
		EndPhase(CurrentPhase);
		if (CurrentPhase + 1 >= Phases.Num())
		{
			Finish(TEXT("escenario completo"));
			return;
		}
		BeginPhase(CurrentPhase + 1);
	}
	if (!Phases.IsValidIndex(CurrentPhase))
	{
		return;
	}
	FPhaseData& Phase = Phases[CurrentPhase];
	SampleFrame(Phase);
	if (Now - LastSlowSample >= TNChaosDetail::SlowSampleSeconds)
	{
		LastSlowSample = Now;
		SampleSlow(Phase);
		SyncDrivers();
	}
	TickDrivers(DeltaTime);
}

TSharedRef<FJsonObject> UTN_StressChaosSubsystem::BuildReport(const TCHAR* Reason) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	const UWorld* World = GetWorld();
	Root->SetStringField(TEXT("tool"), TEXT("TN.Stress"));
	Root->SetStringField(TEXT("scenario"), bClientOnly ? TEXT("caos_cliente") : TEXT("caos"));
	Root->SetStringField(TEXT("map"), World ? World->GetMapName() : FString());
	Root->SetStringField(TEXT("build"), TNTestReport::BuildConfigName());
	Root->SetStringField(TEXT("net_mode"), TNTestReport::NetModeName(World));
	Root->SetStringField(TEXT("end_reason"), Reason);
	Root->SetBoolField(TEXT("rendering"), FApp::CanEverRender());
	Root->SetStringField(TEXT("gpu"), GRHIAdapterName);
	Root->SetStringField(TEXT("rhi"), GDynamicRHI ? FString(GDynamicRHI->GetName()) : FString());
	if (const UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr)
	{
		FVector2D Size;
		Viewport->GetViewportSize(Size);
		Root->SetStringField(TEXT("viewport"), FString::Printf(TEXT("%.0fx%.0f"), Size.X, Size.Y));
	}
	const Scalability::FQualityLevels Quality = Scalability::GetQualityLevels();
	Root->SetStringField(TEXT("scalability"), FString::Printf(TEXT("res %.0f%%, vista %d, sombras %d, GI %d, reflejos %d, post %d, texturas %d, efectos %d, follaje %d, sombreado %d"),
		Quality.ResolutionQuality, Quality.ViewDistanceQuality, Quality.ShadowQuality, Quality.GlobalIlluminationQuality, Quality.ReflectionQuality,
		Quality.PostProcessQuality, Quality.TextureQuality, Quality.EffectsQuality, Quality.FoliageQuality, Quality.ShadingQuality));
	Root->SetNumberField(TEXT("phase_seconds"), Config.PhaseSeconds);
	Root->SetNumberField(TEXT("enemy_scale"), Config.EnemyScale);
	Root->SetNumberField(TEXT("drivers"), Drivers.Num());
	Root->SetNumberField(TEXT("memory_start_mb"), MemoryStartMB);
	Root->SetNumberField(TEXT("commit_start_mb"), CommitStartMB);
	Root->SetNumberField(TEXT("vram_budget_mb"), VramBudgetMB);
	Root->SetNumberField(TEXT("memory_peak_mb"), TNTestReport::PeakPhysicalMB());
	Root->SetBoolField(TEXT("high_qos_applied"), bHighQoSApplied);
	Root->SetBoolField(TEXT("cpu_hybrid"), TNCpuCore::IsHybrid());
	TArray<TSharedPtr<FJsonValue>> PhasesJson;
	for (const FPhaseData& Phase : Phases)
	{
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("phase"), TNChaos::StepName(Phase.Plan.Step));
		Item->SetNumberField(TEXT("created"), Phase.Created);
		Item->SetNumberField(TEXT("frames"), Phase.FrameMs.Num());
		Item->SetNumberField(TEXT("spawn_hitch_ms"), Phase.SpawnHitchMs);
		TNChaosDetail::AddSummary(*Item, TEXT("frame"), Phase.FrameMs);
		TNChaosDetail::AddSummary(*Item, TEXT("game_thread"), Phase.GameThreadMs);
		TNChaosDetail::AddSummary(*Item, TEXT("render_thread"), Phase.RenderThreadMs);
		TNChaosDetail::AddSummary(*Item, TEXT("rhi_thread"), Phase.RhiThreadMs);
		TNChaosDetail::AddSummary(*Item, TEXT("gpu"), Phase.GpuMs);
		Item->SetNumberField(TEXT("memory_avg_mb"), Phase.MemorySamples > 0 ? Phase.MemorySumMB / Phase.MemorySamples : 0.0);
		Item->SetNumberField(TEXT("memory_max_mb"), Phase.MemoryMaxMB);
		Item->SetNumberField(TEXT("memory_end_mb"), Phase.MemoryEndMB);
		Item->SetNumberField(TEXT("commit_max_mb"), Phase.CommitMaxMB);
		Item->SetNumberField(TEXT("commit_end_mb"), Phase.CommitEndMB);
		Item->SetNumberField(TEXT("vram_process_max_mb"), Phase.VramMaxMB);
		Item->SetNumberField(TEXT("vram_process_end_mb"), Phase.VramEndMB);
		Item->SetNumberField(TEXT("texture_memory_mb"), Phase.TextureMemoryMB);
		Item->SetNumberField(TEXT("actors"), Phase.Actors);
		Item->SetNumberField(TEXT("replicated_actors"), Phase.ReplicatedActors);
		Item->SetNumberField(TEXT("ticking_actors"), Phase.TickingActors);
		Item->SetNumberField(TEXT("ticking_components"), Phase.TickingComponents);
		Item->SetNumberField(TEXT("connections"), Phase.Connections);
		Item->SetNumberField(TEXT("net_out_kb_s_per_connection_avg"), Phase.NetSamples > 0 ? Phase.NetOutKBsSum / Phase.NetSamples : 0.0);
		Item->SetNumberField(TEXT("net_out_kb_s_per_connection_max"), Phase.NetMaxOutKBs);
		Item->SetNumberField(TEXT("net_in_kb_s_per_connection_avg"), Phase.NetSamples > 0 ? Phase.NetInKBsSum / Phase.NetSamples : 0.0);
		Item->SetNumberField(TEXT("net_corrections"), Phase.Corrections);
		const FActions& A = Phase.Actions;
		Item->SetNumberField(TEXT("grabs"), A.Grabs);
		Item->SetNumberField(TEXT("throws"), A.Throws);
		Item->SetNumberField(TEXT("ball_entries"), A.BallEntries);
		Item->SetNumberField(TEXT("items_given"), A.ItemsGiven);
		Item->SetNumberField(TEXT("items_used"), A.ItemsUsed);
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
	}
	Root->SetArrayField(TEXT("phases"), PhasesJson);
	Sink.WriteJson(*Root);
	return Root;
}

void UTN_StressChaosSubsystem::Finish(const TCHAR* Reason)
{
	if (bMeasuring && Phases.IsValidIndex(CurrentPhase) && Phases[CurrentPhase].Actors == 0)
	{
		EndPhase(CurrentPhase);
	}
	const FString Name = bClientOnly ? TEXT("caos_cliente_") : TEXT("caos_");
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Stress"), Name + FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S")) + TEXT(".json"));
	const bool bSaved = TNTestReport::Save(*BuildReport(Reason), Path);
	UE_LOG(LogTortunabo, Log, TEXT("[Estrés] Terminado (%s). Informe %s: %s"), Reason, bSaved ? TEXT("guardado") : TEXT("NO guardado"), *Path);

	for (FDriver& Driver : Drivers)
	{
		if (APlayerController* PC = Driver.Controller.Get())
		{
			if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PC->GetPawn()))
			{
				InputSprint(Turtle, false);
			}
		}
	}
	Drivers.Reset();
	for (const TWeakObjectPtr<AActor>& Actor : Spawned)
	{
		if (Actor.IsValid())
		{
			Actor->Destroy();
		}
	}
	Spawned.Reset();
	RestoreWorld();
	if (bSinkAttached && GLog)
	{
		GLog->RemoveOutputDevice(&Sink);
		bSinkAttached = false;
	}
	if (bCVarsChanged)
	{
		IConsoleManager& Console = IConsoleManager::Get();
		if (IConsoleVariable* Corrections = Console.FindConsoleVariable(TEXT("p.NetShowCorrections")))
		{
			Corrections->Set(SavedNetShowCorrections, ECVF_SetByConsole);
		}
		if (IConsoleVariable* CorrectionLifetime = Console.FindConsoleVariable(TEXT("p.NetCorrectionLifetime")))
		{
			CorrectionLifetime->Set(SavedNetCorrectionLifetime, ECVF_SetByConsole);
		}
		if (IConsoleVariable* MaxFps = Console.FindConsoleVariable(TEXT("t.MaxFPS")))
		{
			MaxFps->Set(SavedMaxFps, ECVF_SetByConsole);
		}
		bCVarsChanged = false;
	}
	bActive = false;
	bMeasuring = false;
	if (bQuitWhenDone)
	{
		FPlatformMisc::RequestExitWithStatus(false, 0);
	}
}
