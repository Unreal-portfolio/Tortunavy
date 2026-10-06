#pragma once

#include "CoreMinimal.h"
#include "Game/TN_BeachRaceGameState.h"
#include "Game/TN_TctRules.h"
#include "TN_TctGameState.generated.h"

/**
 * Subida del agua de la ronda en curso, replicada una vez por ronda: cada máquina calcula la altura del agua con la hora del
 * servidor (TNTctRules::WaterZAt), sin replicar la altura en cada fotograma.
 */
USTRUCT(BlueprintType)
struct FTNTctFloodState
{
	GENERATED_BODY()

	/** Hora del servidor (GetServerWorldTimeSeconds) de la salida de la ronda; < 0 si el agua no sube (se queda en HoldZ). */
	UPROPERTY(BlueprintReadOnly, Category = "Tct")
	float StartServerTime = -1.f;

	/**
	 * Altura del agua cuando no sube (preparación, recuento, podio). Hasta que el servidor la fija, muy abajo: el mar de la arena
	 * (ATN_TctArena) se queda en el de la variante y no tapa los primeros anillos mientras llega la réplica.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Tct")
	float HoldZ = -1.e6f;

	UPROPERTY(BlueprintReadOnly, Category = "Tct")
	float BaseZ = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Tct")
	TArray<float> Levels;

	UPROPERTY(BlueprintReadOnly, Category = "Tct")
	float SuddenDeathZ = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Tct")
	float StartDelay = TNTctFloodDefaults::StartDelay;

	UPROPERTY(BlueprintReadOnly, Category = "Tct")
	float StepSeconds = TNTctFloodDefaults::StepSeconds;

	UPROPERTY(BlueprintReadOnly, Category = "Tct")
	float RiseSeconds = TNTctFloodDefaults::RiseSeconds;

	UPROPERTY(BlueprintReadOnly, Category = "Tct")
	float SuddenDeathRiseSeconds = TNTctFloodDefaults::SuddenDeathRiseSeconds;

	/** El plan para TNTctRules. */
	FTNTctFloodPlan ToPlan() const;
};

/**
 * GameState de Todos contra Todos (ATN_TctGameMode, #651). Es el de la carrera en la playa (ATN_BeachRaceGameState) para
 * reutilizar sus pantallas tal cual (UTN_RaceScreensSubsystem): el «RONDA N» y el 3, 2, 1 entre rondas, el recuento de
 * conchas (una entera por ronda ganada, RaceShellHalves) y la pantalla de la campeona con el podio. ProcMode = FreeForAll,
 * RoundTarget = rondas para ganar la partida. Añade la subida del agua y cuántas quedan en pie.
 */
UCLASS()
class TORTUNABO_API ATN_TctGameState : public ATN_BeachRaceGameState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Tct")
	FTNTctFloodState Flood;

	/** Tortugas en pie en la ronda en curso (para el HUD). */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Tct")
	int32 FightersAlive = 0;

	/** Altura del agua ahora mismo, en cualquier máquina. */
	UFUNCTION(BlueprintPure, Category = "Tct")
	float GetWaterZ() const;

	/** Segundos hasta que el agua empiece a subir otra vez (escalón o muerte súbita); -1 si ya no sube más o no hay ronda. */
	UFUNCTION(BlueprintPure, Category = "Tct")
	float GetSecondsToNextRise() const;

	/**
	 * Cualquier máquina (#831): la próxima subida del agua, con su cuenta atrás, el tramo y la altura a la que llegará, y si
	 * el agua sube ahora. false (y el resto vacío) entre rondas o sin plan.
	 */
	bool GetNextRise(FTNTctNextRise& OutNext) const;

	/** Segundos de la ronda en curso desde la salida (-1 si el agua no está subiendo: preparación, recuento o podio). */
	float GetFloodElapsed() const;
};
