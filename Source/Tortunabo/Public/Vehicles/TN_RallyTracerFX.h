// Trazador local del disparo de la artillera (#333): en la máquina de un cliente, en cuanto pulsa, sale de la boca una
// estela corta con el color y la balística de la munición, sin esperar al proyectil del servidor (que llega con el ping).
// Cosmético: no se replica, no choca y vive una fracción de segundo.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rally/TN_RallyVehicle.h"
#include "TN_RallyTracerFX.generated.h"

class UStaticMeshComponent;

UCLASS(Transient, NotPlaceable)
class TORTUNABO_API ATN_RallyTracerFX : public AActor
{
	GENERATED_BODY()

public:
	ATN_RallyTracerFX();

	/**
	 * Crea el trazador en esta máquina: sale de Muzzle con Velocity (cm/s) y cae con GravityZ (cm/s², negativa). Nada en
	 * un servidor dedicado ni en una máquina que no pinta.
	 */
	static ATN_RallyTracerFX* Spawn(UWorld* World, ETNRallyAmmo Ammo, const FVector& Muzzle, const FVector& Velocity, float GravityZ);

	virtual void Tick(float DeltaSeconds) override;

private:
	void ApplyPose();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	FVector Velocity = FVector::ZeroVector;
	float GravityZ = 0.f;
	float Age = 0.f;
};
