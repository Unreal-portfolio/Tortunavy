#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Testing/TN_StressChaosPlan.h"
#include "Testing/TN_TestLogSink.h"
#include "TN_StressChaos.generated.h"

class AActor;
class APlayerController;
class ULocalPlayer;
class ATortugaCharacter;
class FJsonObject;
class UDataTable;

/**
 * Escenario de estrés «caos» (TN.Stress caos [segundos por fase=20], o -TNStress=caos [-TNStressSeconds=<por fase>]
 * [-TNStressWarmup=20] [-TNChaosEnemies=1] [-TNQuitWhenDone]; no Shipping). El peor caso de juego real en una sola máquina:
 *
 * - En el anfitrión (o en una partida sin red), cuatro tortugas locales (las que faltan entran como jugadores extra, con la
 *   pantalla partida apagada para que la GPU pinte una sola vista, como en el PC de cada jugador) juegan solas por los mismos
 *   caminos que la entrada real: Move, ToggleShell, TryInteract (coger y lanzar a la compañera) y TryUseEquippedItem, que
 *   acaban en los RPC de servidor de siempre. Nada de teletransportes para las acciones.
 * - En un cliente (-TNStress=caos en un mundo NM_Client), su tortuga juega igual (sin crear nada): su entrada viaja por red y
 *   las correcciones del servidor se cuentan en su propio informe.
 *
 * Fases de TNChaos::BuildTimeline. Mide por fase el fotograma (p50/p95/p99), los hilos de juego, render y RHI, la GPU, la RAM
 * del proceso (media, máxima y al acabar), la VRAM del proceso (DXGI) y la de texturas (RHI), KB/s por conexión, correcciones
 * de red, actores y acciones hechas. Informe en Saved/Stress/caos_<fecha>.json (caos_cliente_<fecha>.json en el cliente).
 */
UCLASS()
class TORTUNABO_API UTN_StressChaosSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UTN_StressChaosSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return bActive && !IsTemplate(); }

	/** Empieza el caos. PhaseSeconds = duración de cada fase; Warmup = espera antes de medir (salida de la carrera, mapa). */
	bool StartChaos(float PhaseSeconds, float WarmupSeconds, float EnemyScale, bool bQuit);
	void StopChaos(const TCHAR* Reason);
	bool IsRunning() const { return bActive; }

private:
	/** Contadores de acciones de una fase (o de todo el caos). */
	struct FActions
	{
		int32 Grabs = 0;
		int32 Throws = 0;
		int32 BallEntries = 0;
		int32 ItemsUsed = 0;
		int32 ItemsGiven = 0;
	};

	/** Lo que hace una tortuga y en qué punto va. */
	struct FDriver
	{
		TWeakObjectPtr<APlayerController> Controller;
		int32 Index = 0;
		TNChaos::ETask Task = TNChaos::ETask::Wander;
		/** Paso dentro de la tarea (cada tarea define los suyos). */
		int32 Stage = 0;
		float TaskClock = 0.f;
		float StageClock = 0.f;
		float ItemClock = 0.f;
		FVector2D MoveInput = FVector2D::ZeroVector;
		float WanderYaw = 0.f;
		TWeakObjectPtr<ATortugaCharacter> Partner;
		/** Cebo de coger y lanzar: se queda en su bola hasta que la cogen (lo pone el portador). */
		bool bBait = false;
		bool bWasInShell = false;
		bool bWasCarrying = false;
		/** Espacia las pulsaciones del caparazón (InputShell). */
		TNChaos::FShellGate Shell;
	};

	struct FPhaseData
	{
		TNChaos::FPhase Plan;
		TArray<float> FrameMs;
		TArray<float> GameThreadMs;
		TArray<float> RenderThreadMs;
		TArray<float> RhiThreadMs;
		TArray<float> GpuMs;
		float SpawnHitchMs = 0.f;
		double MemoryEndMB = 0.0;
		/** Memoria comprometida del proceso (privada): no baja cuando Windows recorta el conjunto de trabajo. */
		double CommitEndMB = 0.0;
		double CommitMaxMB = 0.0;
		double MemoryMaxMB = 0.0;
		double MemorySumMB = 0.0;
		double VramMaxMB = 0.0;
		double VramEndMB = 0.0;
		double TextureMemoryMB = 0.0;
		int32 MemorySamples = 0;
		int32 Actors = 0;
		int32 ReplicatedActors = 0;
		int32 TickingActors = 0;
		int32 TickingComponents = 0;
		int32 Connections = 0;
		double NetOutKBsSum = 0.0;
		double NetInKBsSum = 0.0;
		double NetMaxOutKBs = 0.0;
		int32 NetSamples = 0;
		int32 CorrectionsAtStart = 0;
		int32 Corrections = 0;
		int32 Created = 0;
		FActions Actions;
		TArray<TPair<FString, int32>> TopTicking;
	};

	// ── Fases, medida e informe (TN_StressChaos.cpp) ──
	void BeginPhase(int32 Index);
	void EndPhase(int32 Index);
	void SampleFrame(FPhaseData& Phase);
	void SampleSlow(FPhaseData& Phase);
	void Finish(const TCHAR* Reason);
	TSharedRef<FJsonObject> BuildReport(const TCHAR* Reason) const;
	void EnsureLocalPlayers();
	/** Deshace lo que el escenario cambió en la partida: jugadores extra, pantalla partida y tormenta del cooperativo. */
	void RestoreWorld();
	FVector TurtlesCenter() const;
	double GroundAt(const FVector& At, double Fallback) const;
	FVector PickSpot(const FVector& Center, float MinRadius, float MaxRadius);
	int32 SpawnEnemies(int32 Crabs, int32 Gulls, int32 Tanks);

	// ── Tortugas (TN_StressChaosDriver.cpp) ──
	void SyncDrivers();
	void TickDrivers(float DeltaTime);
	void TickDriver(FDriver& Driver, ATortugaCharacter* Turtle, float DeltaTime);
	void BeginTask(FDriver& Driver, ATortugaCharacter* Turtle);
	void EndTask(FDriver& Driver, ATortugaCharacter* Turtle);
	bool TickWander(FDriver& Driver, ATortugaCharacter* Turtle, float DeltaTime);
	bool TickCarry(FDriver& Driver, ATortugaCharacter* Turtle, float DeltaTime);
	bool TickBall(FDriver& Driver, ATortugaCharacter* Turtle, float DeltaTime);
	bool TickItems(FDriver& Driver, ATortugaCharacter* Turtle, float DeltaTime);
	void TickItemBurst(FDriver& Driver, ATortugaCharacter* Turtle, float DeltaTime);
	void TrackTransitions(FDriver& Driver, ATortugaCharacter* Turtle);
	bool GiveBurstItem(ATortugaCharacter* Turtle);
	FDriver* FindFreePartner(const FDriver& For);
	FActions& CurrentActions();

	/** Entrada de la tortuga: las mismas funciones que llaman los Input Actions (amigo de ATortugaCharacter). */
	static void InputMove(ATortugaCharacter* Turtle, const FVector2D& Value);
	static void InputShell(FDriver& Driver, ATortugaCharacter* Turtle);
	static void InputInteract(ATortugaCharacter* Turtle);
	static void InputUseItem(ATortugaCharacter* Turtle);
	static void InputJump(ATortugaCharacter* Turtle);
	static void InputSprint(ATortugaCharacter* Turtle, bool bOn);
	static void Aim(FDriver& Driver, float Yaw);
	static void AimAt(FDriver& Driver, const ATortugaCharacter* Turtle, const FVector& Target);

	bool bActive = false;
	bool bMeasuring = false;
	bool bQuitWhenDone = false;
	/** Cliente: solo mueve su tortuga (no crea nada ni da objetos). */
	bool bClientOnly = false;
	/** -TNChaosVerbose: una línea por tarea que empieza (para ver qué hace cada tortuga). */
	bool bVerbose = false;
	TNChaos::FConfig Config;
	TArray<FPhaseData> Phases;
	int32 CurrentPhase = INDEX_NONE;
	float WarmupSeconds = 20.f;
	double SessionStart = 0.0;
	double MeasureStart = 0.0;
	double PhaseStart = 0.0;
	double LastFrame = 0.0;
	double LastSlowSample = 0.0;
	double MemoryStartMB = 0.0;
	double CommitStartMB = 0.0;
	/** Presupuesto de VRAM que Windows da al proceso (DXGI), última muestra. */
	double VramBudgetMB = -1.0;
	FRandomStream Stream;
	TArray<FDriver> Drivers;
	TArray<TWeakObjectPtr<AActor>> Spawned;
	FActions Totals;
	FTNTestLogSink Sink;
	bool bSinkAttached = false;
	int32 SavedNetShowCorrections = 0;
	int32 SavedMaxFps = 0;
	bool bCVarsChanged = false;
	bool bSplitscreenForcedOff = false;
	/** Pantalla de antes de EnsureLocalPlayers, para dejarla como estaba en Finish. */
	bool bSavedForceDisableSplitscreen = false;
	int32 SavedMaxSplitscreenPlayers = 0;
	/** Jugadores locales que ha creado el escenario (Finish los quita). */
	TArray<TWeakObjectPtr<ULocalPlayer>> CreatedLocalPlayers;
	float SavedNetCorrectionLifetime = 4.f;
	bool bHighQoSApplied = false;
	/** Objetos de DT_Items que se lanzan (Throwable e InkThrower), cargados al empezar. */
	TArray<FName> CatalogThrowables;
	UPROPERTY(Transient)
	TObjectPtr<const UDataTable> Catalog = nullptr;
};
