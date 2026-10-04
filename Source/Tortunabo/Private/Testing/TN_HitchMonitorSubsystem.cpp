#include "Testing/TN_HitchMonitorSubsystem.h"

#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "RHI.h"
#include "RenderTimer.h"

namespace TNHitchMonitorDetail
{
	/** Umbral por defecto de -TNHitchLog sin valor: el del criterio de #152. */
	constexpr float DefaultThresholdMs = 50.f;
	/** Segundos sin mirar tras empezar el mundo: la carga y el primer fotograma no son tirones. */
	constexpr double WarmupSeconds = 5.0;

	TAutoConsoleVariable<float> CVarThresholdMs(
		TEXT("TN.HitchLog.ThresholdMs"),
		0.f,
		TEXT("Registro de tirones (#152): cada fotograma de más de estos ms deja una línea [Tirón] en el log y un marcador en Insights. ")
		TEXT("0 = apagado. -TNHitchLog[=ms] en la línea de órdenes lo enciende (50 ms sin valor)."),
		ECVF_Default);

	void ApplyCommandLineOnce()
	{
		static bool bApplied = false;
		if (bApplied)
		{
			return;
		}
		bApplied = true;
		float Ms = 0.f;
		if (FParse::Value(FCommandLine::Get(), TEXT("-TNHitchLog="), Ms) && Ms > 0.f)
		{
			CVarThresholdMs->Set(Ms, ECVF_SetByCommandline);
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("TNHitchLog")))
		{
			CVarThresholdMs->Set(DefaultThresholdMs, ECVF_SetByCommandline);
		}
	}
}

bool UTN_HitchMonitorSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
#endif
}

void UTN_HitchMonitorSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	TNHitchMonitorDetail::ApplyCommandLineOnce();
	IgnoreUntil = FPlatformTime::Seconds() + TNHitchMonitorDetail::WarmupSeconds;
}

bool UTN_HitchMonitorSubsystem::IsTickable() const
{
	return !IsTemplate() && TNHitchMonitorDetail::CVarThresholdMs.GetValueOnGameThread() > 0.f;
}

void UTN_HitchMonitorSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const double Now = FPlatformTime::Seconds();
	if (Now < IgnoreUntil)
	{
		return;
	}
	// Tiempo real del fotograma (sin dilatación ni pausa): lo que nota quien juega.
	const float FrameMs = static_cast<float>(FApp::GetDeltaTime() * 1000.0);
	if (FrameMs > TNHitchMonitorDetail::CVarThresholdMs.GetValueOnGameThread())
	{
		ReportHitch(FrameMs, Now);
	}
}

void UTN_HitchMonitorSubsystem::ReportHitch(float FrameMs, double Now)
{
	// Los tiempos de hilo que publica el motor son los del último fotograma medido (la GPU va uno o dos por detrás).
	const float GameMs = static_cast<float>(FPlatformTime::ToMilliseconds(GGameThreadTime));
	const float RenderMs = static_cast<float>(FPlatformTime::ToMilliseconds(GRenderThreadTime));
	const float RhiMs = static_cast<float>(FPlatformTime::ToMilliseconds(GRHIThreadTime));
	const float GpuMs = static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
	const TNHitch::EBound Bound = TNHitch::Classify(FrameMs, GameMs, RenderMs, RhiMs, GpuMs);
	Tracker.Add(Now);

	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	const int32 Players = GameState ? GameState->PlayerArray.Num() : 0;
	const bool bForeground = FPlatformApplicationMisc::IsThisApplicationForeground();
	const double SinceLast = Tracker.LastIntervalSeconds();
	const double Median = Tracker.MedianIntervalSeconds();

	UE_LOG(LogTortunabo, Warning,
		TEXT("[Tirón] %.0f ms (juego %.1f, render %.1f, RHI %.1f, GPU %.1f ms: %s) · %d jugadores · ventana %s · n.º %d, a %.1f s del anterior (mediana %.1f s)"),
		FrameMs, GameMs, RenderMs, RhiMs, GpuMs, TNHitch::BoundName(Bound), Players, bForeground ? TEXT("con foco") : TEXT("sin foco"),
		Tracker.Total(), SinceLast, Median);
	TRACE_BOOKMARK(TEXT("Tirón %d ms"), FMath::RoundToInt(FrameMs));
}
