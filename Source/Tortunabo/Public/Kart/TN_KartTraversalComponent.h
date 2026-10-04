// Géiseres, cascadas y agua para el kart (#293). En el mapa de los karts los desniveles grandes se suben en géiser y se
// bajan por la cascada (el tobogán del cooperativo) y hay canales de agua abierta en el camino:
//  - Géiser (ATN_ProcGeyser): al pasar por su boca, el kart sale despedido en parábola hasta la cima, como las tortugas.
//  - Cascada (ATN_ProcSlideZone): empujón ladera abajo y el kart se mantiene derecho hasta la poza del pie.
//  - Agua (el mar a la cota del mapa y las pozas de las cascadas): pliega las ruedas hacia abajo y flota como una balsa;
//    el acelerador y la dirección lo mueven por el agua y, al llegar a la orilla, sube la rampa sobre sus ruedas.
// La física la aplican el servidor y la conductora local (los que simulan el chasis); los efectos (ruedas plegadas,
// salpicaduras y estela, partículas de TNAmbientFX) son locales en cada máquina con pantalla. Lógica pura en TNKart.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TN_KartTraversalComponent.generated.h"

class ATN_Buggy;
class ATN_ProcGeyser;
class ATN_ProcMapGenerator;
class ATN_ProcSlideZone;
class UStaticMeshComponent;

namespace TNKart
{
	/**
	 * Velocidad de salida del géiser para caer en Target pasando por un ápice ApexExtra por encima del punto más alto, con
	 * la gravedad GravityCms2 (positiva). La misma parábola que lanza a las tortugas (ATN_ProcGeyser::Launch).
	 */
	TORTUNABO_API FVector GeyserLaunchVelocity(const FVector& Start, const FVector& Target, float ApexExtra, float GravityCms2);

	/** Segundos de vuelo de esa parábola, del géiser al destino. */
	TORTUNABO_API float GeyserFlightSeconds(const FVector& Start, const FVector& Target, float ApexExtra, float GravityCms2);

	/**
	 * Aceleración vertical de la flotación (cm/s²): la que sostiene el kart con su línea de flotación a ras de agua.
	 * Submersion > 0: la línea de flotación está bajo el agua (empuja más); VerticalSpeed amortigua el rebote.
	 */
	TORTUNABO_API float BuoyancyAccel(float SubmersionCm, float VerticalSpeedCms, float GravityCms2);

	/** Ruedas plegadas en [0, 1]: van hacia Target a RatePerSecond. */
	TORTUNABO_API float AdvanceFold(float Fold01, bool bTarget, float RatePerSecond, float DeltaSeconds);
}

UCLASS(ClassGroup = (Karts), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_KartTraversalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_KartTraversalComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Flota en el agua en esta máquina. */
	UFUNCTION(BlueprintPure, Category = "Karts|Agua")
	bool IsFloating() const { return bFloating; }

	/** Ruedas plegadas (0 = en el suelo, 1 = plegadas del todo). */
	UFUNCTION(BlueprintPure, Category = "Karts|Agua")
	float GetFold01() const { return Fold01; }

	/** Línea de flotación: el origen del kart flota esto por encima del agua (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Agua")
	float FloatLineCm = 15.f;

	/** Profundidad mínima para flotar (cm): en la orilla, el kart rueda. */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Agua")
	float MinWaterDepthCm = 70.f;

	/** Empuje del acelerador en el agua (cm/s²) y velocidad máxima flotando (cm/s, unos 30 km/h). */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Agua")
	float PaddleAccelCms2 = 900.f;

	UPROPERTY(EditDefaultsOnly, Category = "Karts|Agua")
	float MaxFloatSpeedCms = 850.f;

	/** Giro máximo en el agua (grados por segundo). */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Agua")
	float FloatYawDegPerSecond = 70.f;

	/** Radio de la boca del géiser que lanza al kart (cm) y espera entre dos lanzamientos (s). */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Géiser")
	float GeyserRadiusCm = 380.f;

	UPROPERTY(EditDefaultsOnly, Category = "Karts|Géiser")
	float GeyserCooldownSeconds = 2.5f;

	/** Empujón ladera abajo en la cascada (cm/s²). */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Cascada")
	float SlideAccelCms2 = 450.f;

protected:
	virtual void BeginPlay() override;

private:
	ATN_Buggy* GetKart() const;
	/** Generador, géiseres y toboganes del nivel (locales en cada máquina; se buscan cuando hay mapa). */
	void CacheMapActors();
	/** Cota del agua bajo Location (el mar o la poza de una cascada); false si ahí no hay agua bastante honda. */
	bool FindWaterSurface(const FVector& Location, float& OutSurfaceZ) const;

	/** Servidor y conductora local. */
	void ApplyRaft(float DeltaSeconds, float SurfaceZ);
	void TryGeyserLaunch();
	void ApplySlide(float DeltaSeconds);
	void LevelAfterGeyser(float DeltaSeconds);
	/**
	 * El vuelo del géiser, guiado: el kart sigue la parábola hasta el destino (sin el freno del aire ni las fuerzas del buggy
	 * en el aire, que lo dejaban corto contra la pared del escalón).
	 */
	void GuideGeyserFlight();
	/** Máquinas con pantalla: ruedas plegadas, salpicaduras al entrar y estela. */
	void UpdateWaterVisuals(float DeltaSeconds, bool bWasFloating, float SurfaceZ);
	void FoldTires();

	TWeakObjectPtr<ATN_ProcMapGenerator> Generator;
	TArray<TWeakObjectPtr<ATN_ProcGeyser>> Geysers;
	TArray<TWeakObjectPtr<ATN_ProcSlideZone>> Slides;
	int32 CachedGeneration = 0;
	double LastGeyserLaunch = -1000.0;
	double LastSlideLog = -1000.0;
	/** Vuelo del géiser en curso: salida, velocidad inicial, gravedad y duración (0 = sin vuelo). */
	FVector FlightOrigin = FVector::ZeroVector;
	FVector FlightVelocity = FVector::ZeroVector;
	float FlightGravity = 980.f;
	float FlightSeconds = 0.f;

	bool bFloating = false;
	float Fold01 = 0.f;
	bool bFxBuilt = false;

	/** Neumáticos del buggy (Tire_*) y su transformación sin plegar (la que les pone el buggy) y la última escrita. */
	TArray<TWeakObjectPtr<UStaticMeshComponent>> Tires;
	TArray<FTransform> TireBase;
	TArray<FTransform> TireWritten;
};
