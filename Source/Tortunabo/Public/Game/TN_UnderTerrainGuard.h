#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TN_UnderTerrainGuard.generated.h"

class ATortugaCharacter;
struct FHitResult;

/**
 * Red de seguridad bajo el terreno (GDD §9, #633): 10 veces por segundo, en el servidor, comprueba si una tortuga se ha
 * hundido bajo el terreno y la recoloca en la superficie. Lo común lo comparten la carrera de la playa
 * (ATN_BeachRaceGameMode::GuardUnderSand, que conoce la arena de su generador) y el resto de modos
 * (UTN_UnderTerrainGuardComponent, que la busca con trazas): qué mueve de verdad a la tortuga (BodyProbe) y cuándo una
 * mirada bajo el terreno cuenta (RegisterLook: dos seguidas, o una muy honda).
 */
namespace TNUnderTerrain
{
	/** Cada cuánto se mira (s): 10 veces por segundo. */
	constexpr float WatchInterval = 0.1f;

	/** Una mirada a más de esta hondura (cm) bajo el terreno rescata ya, sin esperar a confirmarla en la siguiente. */
	constexpr double DeepDepth = 400.0;

	/**
	 * @brief Una mirada: Depth cm bajo el terreno (negativo, encima) con Margin de holgura. Lleva en InOutStrikes las miradas
	 * seguidas por debajo (una mirada por encima lo pone a 0).
	 * @return true si hay que rescatar: confirmado en dos miradas seguidas (0,1 s; un fotograma de la física no cuenta) o
	 * ya a más de Deep.
	 */
	inline bool RegisterLook(int32& InOutStrikes, double Depth, double Margin, double Deep = DeepDepth)
	{
		if (Depth <= Margin)
		{
			InOutStrikes = 0;
			return false;
		}
		++InOutStrikes;
		return InOutStrikes >= 2 || Depth > Deep;
	}

	/**
	 * Lo que mueve de verdad a la tortuga y su punto más bajo: la caja de la bola del caparazón (enganchada en esta máquina),
	 * el cuerpo raíz del ragdoll o los pies de la cápsula. OutDriver, si se pide (solo para el registro: no se forma texto en
	 * cada mirada), dice qué la mueve.
	 */
	TORTUNABO_API FVector BodyProbe(const ATortugaCharacter& Turtle, FVector& OutVelocity, FString* OutDriver = nullptr);
}

/** Lo que la red de seguridad lleva visto de cada tortuga. */
struct FTNUnderTerrainWatch
{
	/** Miradas seguidas bajo el terreno (TNUnderTerrain::RegisterLook). */
	int32 Strikes = 0;
	/** Último sitio seguro (de pie en el suelo, fuera del agua) y cuándo (hora del mundo; < 0 si aún no hay). */
	FVector SafeLocation = FVector::ZeroVector;
	FRotator SafeRotation = FRotator::ZeroRotator;
	float SafeTime = -1.f;
	/** Último rescate: dónde estaba y cuándo (< 0 si no hay). */
	FVector LastRescueAt = FVector::ZeroVector;
	float LastRescueTime = -1.f;
};

/**
 * @brief Red de seguridad bajo el terreno para los modos sin generador de arena: Coop (ATN_ProcMapGameMode) y Clásico
 * (ATN_RunGameMode). La lleva ATN_RunGameMode; la carrera de la playa la apaga porque tiene la suya.
 *
 * Una tortuga está bajo el terreno si lo primero que la para bajando desde SurfaceSearchUp por encima de su punto más bajo
 * queda más de Margin por encima y, además, está dentro de esa geometría: mirando hacia arriba desde ella no hay otra cara
 * por debajo de esa superficie (bajo un puente, una cornisa, un árbol o en una cueva se ve su cara de abajo: aire libre, no
 * se toca, haya suelo debajo o una sima). No se mira a quien nada, está muerta o derribada esperando rescate, ha
 * terminado, va en brazos de otra, tiene el movimiento parado (esperando la ronda) o está dentro de una zona de muerte
 * (ATN_DeathZoneVolume): esas muertes son legítimas. Si cae al vacío sin nada encima, tampoco: es una caída.
 *
 * Al confirmarlo la pone de pie (TNBeach::RelocateTurtle: la suelta de lo que la tenga, sin caída ni daño) en la superficie
 * más cercana por encima de ella o, si no cabe, alrededor (hasta RingSearchRadius); si vuelve a hundirse en
 * RepeatSeconds, en su último sitio seguro. Lo apunta en el registro como «[Red de seguridad]».
 */
UCLASS(ClassGroup = (Tortunabo), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_UnderTerrainGuardComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_UnderTerrainGuardComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Enciende o apaga la red (antes de BeginPlay; la carrera de la playa la apaga en su constructor). */
	void SetGuardEnabled(bool bInEnabled) { bGuardEnabled = bInEnabled; }

	UFUNCTION(BlueprintPure, Category = "Safety")
	bool IsGuardEnabled() const { return bGuardEnabled; }

	/** Servidor: una mirada a todas las tortugas de los jugadores (lo que hace el temporizador a 10 Hz). */
	void WatchAll();

	/**
	 * @brief Servidor: una mirada a Turtle en el instante Now (hora del mundo).
	 * @return true si la ha recolocado en la superficie.
	 */
	bool WatchTurtle(ATortugaCharacter* Turtle, float Now);

protected:
	/** Holgura (cm) por debajo de la superficie antes de contar una mirada (la cápsula y la bola rozan y se hunden un poco). */
	UPROPERTY(EditDefaultsOnly, Category = "Safety", meta = (ClampMin = "0.0", Units = "Centimeters"))
	float Margin = 160.f;

	/** Hasta dónde (cm) por encima se busca la superficie. */
	UPROPERTY(EditDefaultsOnly, Category = "Safety", meta = (ClampMin = "100.0", Units = "Centimeters"))
	float SurfaceSearchUp = 3000.f;

	/** Si la superficie de encima no tiene sitio de pie, se busca alrededor hasta este radio (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Safety", meta = (ClampMin = "0.0", Units = "Centimeters"))
	float RingSearchRadius = 1000.f;

	/** Cada cuánto (s) se apunta el último sitio seguro. */
	UPROPERTY(EditDefaultsOnly, Category = "Safety", meta = (ClampMin = "0.1", Units = "Seconds"))
	float SafeSpotSampleSeconds = 1.f;

	/** Si se vuelve a hundir antes de esto (s) cerca del mismo sitio, va a su último sitio seguro. */
	UPROPERTY(EditDefaultsOnly, Category = "Safety", meta = (ClampMin = "0.0", Units = "Seconds"))
	float RepeatSeconds = 5.f;

private:
	/** true si ahora no se la debe tocar: muerta, en el agua, en brazos de otra, parada o fuera de juego. */
	bool ShouldSkip(const ATortugaCharacter& Turtle) const;

	/** true si Probe está dentro de una zona de muerte (o su jugador ya lleva la cuenta de una). */
	bool IsInDeathZone(const ATortugaCharacter& Turtle, const FVector& Probe) const;

	/** Lo primero que para a una tortuga bajando desde SurfaceSearchUp por encima de Probe hasta Probe; false si nada. */
	bool TraceSurfaceAbove(const ATortugaCharacter& Turtle, const FVector& Probe, FHitResult& OutHit) const;

	/**
	 * true si Probe está en aire libre bajo la superficie SurfaceZ: mirando hacia arriba se ve antes otra cara (la de abajo de
	 * un puente, una cornisa o el techo de una cueva). false si está dentro de la geometría: el trazo empieza dentro, no da con
	 * nada (cara de arriba vista por detrás) o da con la propia superficie.
	 */
	bool IsInOpenSpace(const ATortugaCharacter& Turtle, const FVector& Probe, double SurfaceZ) const;

	/** El sitio de pie más cercano por encima de Probe (en su vertical y, si no, en anillos alrededor). */
	bool FindSurfaceSpot(const ATortugaCharacter& Turtle, const FVector& Probe, FTransform& OutTransform) const;

	/** El sitio de pie más bajo por encima de Probe en la vertical de Column. */
	bool FindStandInColumn(const ATortugaCharacter& Turtle, const FVector2D& Column, double FromZ, double ToZ, FTransform& OutTransform) const;

	void SampleSafeSpot(const ATortugaCharacter& Turtle, FTNUnderTerrainWatch& Watch, float Now) const;
	void Rescue(ATortugaCharacter& Turtle, FTNUnderTerrainWatch& Watch, const FVector& Probe, double Depth, float Now);

	TMap<TWeakObjectPtr<ATortugaCharacter>, FTNUnderTerrainWatch> Watches;
	FTimerHandle WatchHandle;
	bool bGuardEnabled = true;
};
