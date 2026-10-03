// Piloto IA del Rally: sigue la spline de la pista con mirada adelantada, frena antes de las curvas, vuelve despacio a la
// calzada si se sale, da marcha atrás (cada vez más larga si se repite) si se atasca y dispara (ITN_RallyVehicle::AIFire) al buggy de delante cuando lo tiene a menos de 40 m. Solo en el servidor.
// Hereda de AController (no de AAIController) para no añadir AIModule al módulo: no usa navegación ni percepción.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Controller.h"
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

	/** Velocidad máxima en recta y mínima en la curva más cerrada (km/h). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA")
	float MaxSpeedKmh = 90.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA")
	float MinCornerSpeedKmh = 35.f;

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

	/** Probabilidad de gastar la munición especial en cada disparo si la tiene. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|IA", meta = (ClampMin = "0", ClampMax = "1"))
	float SpecialFireChance = 0.3f;

private:
	ATN_RallyTrack* ResolveTrack();
	void Drive(float DeltaSeconds, ATN_RallyTrack& Track);
	void TryFire(const FVector& Location, const FVector& Forward);

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
};
