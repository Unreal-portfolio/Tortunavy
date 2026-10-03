// Caja de munición especial del Rally: la recoge el buggy que pasa (servidor), da una carga según su puesto
// (TNRally::AmmoWeightsForPlace) y reaparece a los 3 s.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_RallyAmmoBox.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class URotatingMovementComponent;

UCLASS(Blueprintable)
class TORTUNABO_API ATN_RallyAmmoBox : public AActor
{
	GENERATED_BODY()

public:
	ATN_RallyAmmoBox();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Servidor: si está disponible y Vehicle es un buggy del Rally, le da la munición y se oculta RespawnSeconds. */
	bool TryCollect(AActor* Vehicle);

	UFUNCTION(BlueprintPure, Category = "Rally")
	bool IsAvailable() const { return bAvailable; }

	/** Radio de recogida (cm) que usa el GameMode para el paso cercano (no depende de los solapamientos del buggy). */
	UPROPERTY(EditAnywhere, Category = "Rally")
	float PickupRadiusCm = 250.f;

	UPROPERTY(EditAnywhere, Category = "Rally", meta = (ClampMin = "0.1"))
	float RespawnSeconds = 3.f;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Rally")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category = "Rally")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Rally")
	TObjectPtr<URotatingMovementComponent> Spin;

	UPROPERTY(ReplicatedUsing = OnRep_Available)
	bool bAvailable = true;

	UFUNCTION()
	void OnRep_Available();

private:
	void Reactivate();

	FTimerHandle ReactivateHandle;
};
