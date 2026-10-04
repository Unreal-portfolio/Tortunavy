// Efectos de las ruedas del buggy del Rally (#301): polvo de arena según la velocidad y más denso al derrapar,
// salpicaduras al vadear, marcas de neumático en el suelo al derrapar y golpe de aterrizaje (sonido y polvo). Cosmético y
// local en cada máquina con pantalla: lee el estado que ve del buggy (velocidad, giro, posición de las ruedas) y traza
// el suelo bajo cada rueda; no replica nada ni existe en un servidor dedicado. Partículas y marcas con el sistema ligero
// del proyecto (TNAmbientFX). Reglas puras en TNBuggyFX (tests Tortunabo.Rally.BuggyFX.*).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TN_BuggyDustComponent.generated.h"

class ATN_Buggy;
class USoundBase;

namespace TNBuggyDust
{
	// Emisores, marcas y memoria del fotograma anterior (Private/Vehicles/TN_BuggyDustComponent.cpp).
	struct FState;
	struct FWheelProbe;
}

namespace TNBuggyFX
{
	/** Por debajo de esta velocidad (cm/s) las ruedas no levantan polvo. */
	inline constexpr float MinDustSpeedCms = 200.f;
	/** Velocidad (cm/s) con el polvo de rodar a tope. */
	inline constexpr float FullDustSpeedCms = 1600.f;
	/** Partículas de polvo por segundo y rueda trasera rodando a tope, y las que suma un derrape entero. */
	inline constexpr float RollDustRate = 26.f;
	inline constexpr float SkidDustRate = 46.f;
	/** Derrape (0..1, el del sonido) desde el que marcan las ruedas traseras y las delanteras. */
	inline constexpr float RearMarkSkid = 0.25f;
	inline constexpr float FrontMarkSkid = 0.6f;
	/** Velocidad de caída (cm/s) desde la que suena el aterrizaje y a la que suena entero. */
	inline constexpr float MinLandingFallCms = 300.f;
	inline constexpr float FullLandingFallCms = 1300.f;
	/** Salpicadura al vadear: desde esta velocidad (cm/s), a tope en FullSplashSpeedCms, con SplashRate gotas/s por rueda. */
	inline constexpr float MinSplashSpeedCms = 100.f;
	inline constexpr float FullSplashSpeedCms = 1200.f;
	inline constexpr float SplashRate = 60.f;

	/** Partículas de polvo por segundo de una rueda trasera a SpeedCms con el derrape Skid01. */
	TORTUNABO_API float DustRate(float SpeedCms, float Skid01);

	/** Una rueda deja marca: las traseras con un derrape moderado, las delanteras solo con uno fuerte. */
	TORTUNABO_API bool LeavesMark(float Skid01, bool bRearWheel);

	/** Volumen del golpe al aterrizar (0..1) por la velocidad de caída justo antes. */
	TORTUNABO_API float LandingVolume(float FallSpeedCms);

	/** La rueda (con el borde de abajo en WheelBottomZ) va por el agua. */
	TORTUNABO_API bool IsWading(float WheelBottomZ, bool bHasWater, double WaterZ);

	/** Gotas por segundo de una rueda que vadea a SpeedCms. */
	TORTUNABO_API float SplashRateAt(float SpeedCms);
}

UCLASS(ClassGroup = (Rally), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_BuggyDustComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_BuggyDustComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Golpe de las ruedas al aterrizar de un salto. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Efectos")
	TObjectPtr<USoundBase> LandSound;

	/** Más lejos de la cámara local (cm) no se levanta polvo ni se marcan las ruedas (lo que ya vuela acaba su vida). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Efectos", meta = (ClampMin = "1000"))
	float MaxViewDistanceCm = 9000.f;

	/** Cantidad de polvo y salpicaduras (1 = normal). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Efectos", meta = (ClampMin = "0", ClampMax = "4"))
	float Amount = 1.f;

	/** Segundos que duran las marcas de neumático en el suelo (el último segundo se estrechan hasta desaparecer). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Efectos", meta = (ClampMin = "1"))
	float MarkSeconds = 8.f;

	/** Partículas lanzadas y tramos de marca puestos desde que empezó (para las pruebas sin editor). */
	int32 GetParticlesSpawned() const { return ParticlesSpawned; }
	int32 GetMarksPlaced() const { return MarksPlaced; }
	int32 GetLandings() const { return Landings; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	ATN_Buggy* GetBuggy() const;
	/** Suelo bajo cada rueda, polvo, salpicaduras, marcas y aterrizaje de este fotograma. */
	void UpdateWheels(ATN_Buggy& Buggy, float Dt);
	/** Polvo o salpicadura y marca de la rueda Index (Skid01: derrape del buggy). */
	void UpdateWheel(int32 Index, const TNBuggyDust::FWheelProbe& Probe, float SpeedCms, float Skid01, const FVector& Back, float Dt);
	/** Golpe y polvo al volver al suelo tras un salto. */
	void UpdateLanding(const ATN_Buggy& Buggy, const TNBuggyDust::FWheelProbe* Probes, bool bAnyContact, float Dt);

	TSharedPtr<TNBuggyDust::FState> State;
	int32 ParticlesSpawned = 0;
	int32 MarksPlaced = 0;
	int32 Landings = 0;
};
