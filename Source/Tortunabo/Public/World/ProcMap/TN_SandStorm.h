#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_SandStorm.generated.h"

class ACharacter;
class UPostProcessComponent;
class UExponentialHeightFogComponent;
class USoundBase;

/** Lo que el servidor decide de la tormenta de arena; con esto cada máquina calcula lo mismo (TN_SandStormRules.h). */
USTRUCT()
struct FTNSandStormCycle
{
	GENERATED_BODY()

	/** Semilla de las tormentas (cuándo, hacia dónde sopla y sus ráfagas). */
	UPROPERTY()
	int32 Seed = 0;

	/** Segundos del servidor (AGameStateBase::GetServerWorldTimeSeconds) a los que empezó el ciclo. */
	UPROPERTY()
	double StartServerTime = 0.0;

	UPROPERTY()
	bool bRunning = false;
};

/**
 * Tormenta de arena periódica del coop (#790). Cada cierto tiempo llega una tormenta que dura unos segundos: cierra la
 * niebla y tiñe la imagen de arena (por código, sin assets), frena un poco a las tortugas y la empujan ráfagas de hasta
 * 8 m/s. Sin daño ni muerte. Dentro de un búnker (ATN_BeachShelterVolume) ni frena ni empuja. No sustituye a la tormenta
 * de bañistas (ATN_PathStorm): conviven, y si el jugador está dentro de aquella, esta no toca la niebla.
 *
 * Red: el servidor fija el ciclo (semilla y hora de inicio, Cycle replicado) y todas las máquinas calculan la tormenta
 * con la hora del servidor, así la ven a la vez. El empuje y el freno se aplican donde se simula el movimiento de cada
 * tortuga (servidor y su cliente dueño), como las corrientes del ProcMap. El efecto visual se atenúa con el ajuste de
 * accesibilidad «Efectos del clima» (Ajustes > Juego).
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_SandStorm : public AActor
{
	GENERATED_BODY()

public:
	ATN_SandStorm();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Servidor: arranca el ciclo de tormentas con esta semilla desde ahora. */
	void StartCycle(int32 InSeed);

	/** Servidor: para el ciclo (entre rondas y al acabar la partida). */
	void StopCycle();

	/** Servidor (pruebas, comando TNSandStorm): adelanta el ciclo para que la siguiente tormenta empiece ya. */
	void DebugStartNow();

	/** Fuerza de la tormenta ahora (0..1) en esta máquina. */
	UFUNCTION(BlueprintPure, Category = "SandStorm")
	float GetIntensity() const { return CurrentIntensity; }

	UFUNCTION(BlueprintPure, Category = "SandStorm")
	bool IsStormActive() const { return CurrentIntensity > 0.f; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SandStorm")
	TObjectPtr<USceneComponent> Root;

	/** Tinte de arena, menos color y viñeta (sin límites: toda la pantalla). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SandStorm")
	TObjectPtr<UPostProcessComponent> PostProcess;

	/** Sonido de cada ráfaga (2D, en cada máquina con pantalla). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SandStorm|FX")
	TObjectPtr<USoundBase> GustSound;

private:
	UFUNCTION()
	void OnRep_Cycle();

	UPROPERTY(ReplicatedUsing = OnRep_Cycle)
	FTNSandStormCycle Cycle;

	/** Hora del servidor en esta máquina. */
	double ServerNow() const;

	/** Empuje y freno a las tortugas que simula esta máquina. */
	void ApplyToTurtles(float DeltaSeconds, float Intensity, float Gust, const FVector2D& Wind);
	void ClearSpeedCaps();

	/** Niebla, tinte y sonido de esta máquina (no en un servidor dedicado). */
	void ApplyLocalLook(float Intensity, float Gust);
	void RestoreFog();
	/** La niebla la lleva ahora la tormenta de bañistas (el jugador está dentro de ella). */
	bool PathStormOwnsFog(const FVector& ViewLocation) const;

	float CurrentIntensity = 0.f;
	float LastGust = 0.f;

	/** Tortugas con el freno de la tormenta puesto en esta máquina y el tope aplicado. */
	TMap<TWeakObjectPtr<ACharacter>, float> AppliedCaps;

	TWeakObjectPtr<UExponentialHeightFogComponent> Fog;
	bool bFogCached = false;
	bool bFogApplied = false;
	float FogDensity0 = 0.f;
	float FogFalloff0 = 0.f;
	float FogStart0 = 0.f;
	float FogOpacity0 = 1.f;
	FLinearColor FogColor0 = FLinearColor::Black;
};
