// Piloto IA del Rally: sigue la spline de la pista con mirada adelantada, frena antes de las curvas, vuelve despacio a la
// calzada si se sale, da marcha atrás (cada vez más larga si se repite) si se atasca y dispara (ITN_RallyVehicle::AIFire) al buggy de delante cuando lo tiene a menos de 40 m. Solo en el servidor.
// La munición especial de las cajas «?» (#629) la gasta cuando le conviene (TNRally::ShouldBotFireSpecial): conchas, mortero,
// tinta y ancla al de delante, el alga al de detrás, la burbuja al rato y cualquiera pasados 8 s.
// Hereda de AController (no de AAIController) para no añadir AIModule al módulo: no usa navegación ni percepción.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Controller.h"
#include "Rally/TN_RallyVehicle.h"
#include "TN_RallyAIController.generated.h"

class ATN_RallyTrack;

UCLASS()
class TORTUNABO_API ATN_RallyAIController : public AController
{
	GENERATED_BODY()

public:
	ATN_RallyAIController();

	virtual void PostInitializeComponents() override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * No viaja con el seamless travel (vuelta al lobby o ?Restart), aunque tenga PlayerState: cada partida crea sus bots
	 * (SpawnBots) y en el lobby un bot ocupaba plaza y nunca se ponía listo (#694).
	 */
	virtual bool ShouldParticipateInSeamlessTravel() const override { return false; }

	/** Velocidad máxima en recta y mínima en la curva más cerrada (km/h). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA")
	float MaxSpeedKmh = 90.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA")
	float MinCornerSpeedKmh = 28.f;

	/**
	 * Frenada antes de las curvas (#606): mira las curvas de los siguientes BrakeProbeCm, cada BrakeProbeStepCm, y va a la
	 * velocidad desde la que llega a cada una frenando con BrakeDecelG (TNRally::ApproachSpeedKmh).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA", meta = (ClampMin = "0"))
	float BrakeProbeCm = 9000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA", meta = (ClampMin = "200"))
	float BrakeProbeStepCm = 1000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA", meta = (ClampMin = "0.05"))
	float BrakeDecelG = 0.5f;

	/**
	 * Circuitos generados (#622): con los elements del manifest, llega a cada labio de salto a su velocidad de diseño por este
	 * factor (cae en la mesa) y a cada horquilla a la velocidad de su radio con HairpinLateralG de lateral, frenando con
	 * BrakeDecelG desde BrakeProbeCm (TNRallyCircuit::FeatureSpeedLimitKmh).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA", meta = (ClampMin = "0.5", ClampMax = "1.2"))
	float JumpLipSpeedFactor = 0.9f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA", meta = (ClampMin = "0.2", ClampMax = "1.2"))
	float HairpinLateralG = 0.6f;

	/**
	 * Dirección acotada para no volcar (#606): con el ángulo de rueda del vehículo (ITN_RallyVehicle::GetMaxSteerAngleDeg, el
	 * de UTN_BuggyData en el buggy) y la batalla WheelbaseCm, como mucho MaxLateralAccelG de aceleración lateral
	 * (TNBuggy::SafeSteerFraction). Despacio gira a tope. 0,7 y no 0,9: por encima de 70 km/h el antivuelco ya no sujeta el
	 * alabeo y girar fuerte vuelca (UTN_BuggyData::AntiRollGroundRollZeroSpeedCms).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA", meta = (ClampMin = "0.1"))
	float MaxLateralAccelG = 0.7f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA", meta = (ClampMin = "50"))
	float WheelbaseCm = 303.5f;

	/** Mirada adelantada: base (cm) más segundos a la velocidad actual. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA")
	float LookAheadBaseCm = 1200.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA")
	float LookAheadSeconds = 0.6f;

	/** Ángulo al objetivo que satura la dirección (grados). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA")
	float SteerSaturationDeg = 35.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA")
	float FireRangeCm = 4000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA")
	float FireIntervalSeconds = 0.6f;

	/**
	 * Probabilidad de gastar la munición especial en cada ocasión de disparo en que le conviene (TNRally::ShouldBotFireSpecial);
	 * pasados TNRally::BotMaxHoldSeconds la gasta siempre.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA", meta = (ClampMin = "0", ClampMax = "1"))
	float SpecialFireChance = 0.3f;

private:
	ATN_RallyTrack* ResolveTrack();
	void Drive(float DeltaSeconds, ATN_RallyTrack& Track);
	/**
	 * Velocidad objetivo (km/h) por las curvas de los siguientes BrakeProbeCm (frenando a tiempo), la pendiente y los saltos y
	 * horquillas del manifest.
	 */
	float TargetSpeedKmh(const ATN_RallyTrack& Track, double SpeedCms) const;
	void TryFire(const FVector& Location, const FVector& Forward);
	/** Munición especial: true si ha disparado. Ahead y Behind, los buggies de justo delante y detrás (pueden ser nulos). */
	bool TryFireSpecial(ITN_RallyVehicle& RallyVehicle, const FVector& Location, const FVector& Forward, const APawn* Ahead,
		const APawn* Behind, double Time);

	TWeakObjectPtr<ATN_RallyTrack> CachedTrack;
	double Arc = 0.0;
	FVector LastLocation = FVector::ZeroVector;
	bool bHasArc = false;
	float SlowSeconds = 0.f;
	double ReverseUntil = 0.0;
	/** Tras una reaparición (salto) no se cuenta la lentitud: el buggy está inmóvil unos segundos. */
	double IgnoreSlowUntil = 0.0;
	/** Marchas atrás seguidas (cada una más larga) y hora de la última. */
	int32 ReverseStreak = 0;
	double LastReverseTime = -1000.0;
	double NextFireTime = 0.0;
	/** Especial que lleva y desde cuándo (s del mundo). */
	ETNRallyAmmo HeldSpecial = ETNRallyAmmo::None;
	double HeldSpecialSince = 0.0;
};
