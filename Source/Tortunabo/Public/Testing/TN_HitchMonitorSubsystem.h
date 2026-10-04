#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Testing/TN_HitchTracker.h"
#include "TN_HitchMonitorSubsystem.generated.h"

/**
 * Registro de tirones (#152; no existe en Shipping). Con TN.HitchLog.ThresholdMs > 0 (o -TNHitchLog[=ms] en la línea de
 * órdenes; sin valor, 50 ms), cada fotograma más largo que el umbral deja en el log una línea «[Tirón]» con su duración,
 * los tiempos de hilo de juego, render, RHI y GPU, a qué se debe, los jugadores, si la ventana tenía el foco y cada cuánto se
 * repiten, y un marcador «Tirón N ms» en la traza de Insights (canal bookmark). Así el tirón queda capturado aunque nadie
 * llegue a pulsar nada. Docs/Analisis/2026-10-03-Tirones-lobby.md.
 */
UCLASS()
class TORTUNABO_API UTN_HitchMonitorSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UTN_HitchMonitorSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override;

private:
	void ReportHitch(float FrameMs, double Now);

	TNHitch::FTracker Tracker;
	/** Hasta cuándo no se mira (la carga del mapa no cuenta como tirón). */
	double IgnoreUntil = 0.0;
};
