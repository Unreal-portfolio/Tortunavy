#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Testing/TN_TestLogSink.h"
#include "Testing/TN_TestReport.h"
#include "TN_MonkeySubsystem.generated.h"

class UTN_MonkeyComponent;

/** Parámetros de una sesión de monkey. */
struct FTNMonkeyConfig
{
	/** Duración de la sesión en segundos reales (sin contar el calentamiento). */
	float Seconds = 30.f;
	int32 Seed = 1;
	/** Jugadores locales que se dejan jugando (los que faltan se crean como jugadores locales extra). */
	int32 Players = 1;
	/** Segundos de espera antes de empezar (la salida de la carrera). */
	float WarmupSeconds = 0.f;
	/** Cierra el proceso al acabar (para los procesos hijos del test de automatización). */
	bool bQuitWhenDone = false;
	/** Dónde escribir el informe (vacío = Saved/Monkey/<fecha>.json). */
	FString OutPath;
};

/**
 * Monkey test (TN.Monkey <segundos> <semilla> [jugadores], o -TNMonkey=<segundos>:<semilla> en la línea de órdenes; solo
 * compilaciones que no son Shipping). Pone un UTN_MonkeyComponent en cada jugador local y, al terminar, escribe el informe
 * JSON en Saved/Monkey/<fecha>.json: acciones, distancia, atascos, caídas bajo el terreno, rescates, avisos de
 * TN.Shell.Debug, correcciones de red (p.NetShowCorrections), errores del registro y tiempos de fotograma.
 * Docs/Estres-Monkey-2026-09-29.md.
 */
UCLASS()
class TORTUNABO_API UTN_MonkeySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UTN_MonkeySubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return State != EState::Idle && !IsTemplate(); }

	/** Empieza una sesión. false si ya hay una o el mundo no sirve. */
	bool StartSession(const FTNMonkeyConfig& InConfig);

	/** Termina la sesión ya (informe incluido). */
	void StopSession(const TCHAR* Reason);

	bool IsRunning() const { return State != EState::Idle; }

	/** Semilla de la sesión en curso o de la última. */
	int32 GetSeed() const { return Config.Seed; }

	/** Ruta del último informe escrito (vacía si no hay). */
	const FString& GetLastReportPath() const { return LastReportPath; }

	/** Para el gancho de error fatal: escribe el informe con lo que haya y crashed = true. */
	void WriteCrashReport();

private:
	enum class EState : uint8 { Idle, Warmup, Running };

	void BeginRunning();
	void AttachComponents();
	void Finish(const TCHAR* Reason, bool bCrashed);
	TSharedRef<FJsonObject> BuildReport(const TCHAR* Reason, bool bCrashed) const;

	EState State = EState::Idle;
	FTNMonkeyConfig Config;
	double WarmupEnd = 0.0;
	double RunStart = 0.0;
	double LastScan = 0.0;
	double ElapsedAtFinish = 0.0;
	FString LastReportPath;

	FTNTestLogSink Sink;
	bool bSinkAttached = false;
	FTNFrameRecorder Frames;
	int32 SavedNetShowCorrections = 0;
	bool bCVarsChanged = false;
	double MemoryStartMB = 0.0;
	/** Modo de red al empezar: al cerrarse el mundo de un cliente (el servidor se ha ido) ya no tiene red. */
	FString SessionNetMode;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTN_MonkeyComponent>> Monkeys;
};
