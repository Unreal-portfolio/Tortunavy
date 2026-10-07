#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Testing/TN_StressScenarios.h"
#include "Testing/TN_TestReport.h"
#include "TN_StressSubsystem.generated.h"

class AActor;

/**
 * Pruebas de estrés (TN.Stress <light|heavy|tortugas8|stop>, o -TNStress=<escenario> en la línea de órdenes; no Shipping).
 * Crea en el mapa actual cangrejos (arrastrador, subterráneo y de patrulla) y zonas de gaviotas, en fases de la misma
 * duración (una de referencia y una por grupo, 60 s en total), con las tortugas jugando (monkey). Mide por fase los ms de
 * fotograma (medio, p95, p99 y máximo), memoria, actores con Tick y KB/s de red por conexión, y escribe Saved/Stress/<fecha>.json.
 * El coste de cada grupo sale de restar cada fase a la anterior. Docs/Estres-Monkey-2026-09-29.md.
 */
UCLASS()
class TORTUNABO_API UTN_StressSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UTN_StressSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return bActive && !IsTemplate(); }

	/**
	 * Empieza un escenario. Warmup = segundos de espera antes de medir (la salida de la carrera); Total = duración de la
	 * medición; bDriveTurtles = las tortugas juegan solas (monkey).
	 */
	bool StartSession(const TNStress::FScenario& InScenario, float WarmupSeconds, float TotalSeconds, bool bDriveTurtles, bool bQuit);
	void StopSession(const TCHAR* Reason);

private:
	struct FPhaseData
	{
		TNStress::FPhase Plan;
		int32 Spawned = 0;
		TArray<float> FrameMs;
		/** Instante (ms desde el inicio de la fase) en que acaba cada fotograma de FrameMs, para hallar el periodo de los picos. */
		TArray<float> FrameStampMs;
		TArray<float> GameThreadMs;
		TArray<float> RenderThreadMs;
		TArray<float> GpuMs;
		/** 1 si el hilo de juego estaba en un núcleo de eficiencia (CPU híbrida) al empezar o al acabar el fotograma de FrameMs. */
		TArray<uint8> FrameOnECore;
		float HitchMs = 0.f;
		/** Grupos repartidos (TNStress::IsSpreadGroup): los que faltan por intentar, cuántos se han intentado y el mayor tiempo de creación en un fotograma. */
		int32 PendingSpawn = 0;
		int32 Attempted = 0;
		float SpawnMaxFrameMs = 0.f;
		int32 SpawnFrames = 0;
		double MemoryMB = 0.0;
		int32 Actors = 0;
		int32 ReplicatedActors = 0;
		int32 TickingActors = 0;
		int32 TickingComponents = 0;
		int32 Connections = 0;
		double NetInKBsSum = 0.0;
		double NetOutKBsSum = 0.0;
		double NetMaxOutKBs = 0.0;
		int32 NetSamples = 0;
		TArray<TPair<FString, int32>> TopTicking;
	};

	void BeginPhase(int32 Index);
	void EndPhase(int32 Index);
	void SampleFrame(FPhaseData& Phase);
	void SampleNet(FPhaseData& Phase);
	bool SpawnEnemy(TNStress::EGroup Group, int32 Index);
	/** Crea los pendientes de un grupo repartido sin pasar de TNStress::SPAWN_BUDGET_MS en este fotograma. */
	void SpawnPending(FPhaseData& Phase);
	FVector PickSpot(float MinRadius, float MaxRadius);
	double GroundAt(const FVector& At, double Fallback) const;
	void Finish(const TCHAR* Reason);
	TSharedRef<FJsonObject> BuildReport(const TCHAR* Reason) const;

	bool bActive = false;
	bool bQuitWhenDone = false;
	bool bDriveTurtles = true;
	bool bMeasuring = false;
	TNStress::FScenario Scenario;
	TArray<FPhaseData> Phases;
	int32 CurrentPhase = INDEX_NONE;
	float WarmupSeconds = 10.f;
	float TotalSeconds = 60.f;
	double SessionStart = 0.0;
	double MeasureStart = 0.0;
	double PhaseStart = 0.0;
	double LastFrame = 0.0;
	double LastNetSample = 0.0;
	double MemoryStartMB = 0.0;
	FString MonkeyReportPath;
	FRandomStream Stream;
	FVector CenterAtStart = FVector::ZeroVector;
	TArray<TWeakObjectPtr<AActor>> Spawned;
	int32 SavedMaxFps = 0;
	bool bMaxFpsChanged = false;
	/** QoS alta pedida a Windows al empezar (sin -TNStressDefaultQoS) y si la aceptó; ver TN_CpuCoreProbe.h. */
	bool bHighQoSRequested = false;
	bool bHighQoSApplied = false;
	bool bPerformanceCoresApplied = false;
	bool bLastFrameOnECore = false;
};
