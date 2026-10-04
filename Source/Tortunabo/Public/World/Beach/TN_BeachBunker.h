#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachElement.h"
#include "TN_BeachBunker.generated.h"

class ATN_BeachShelterVolume;
class UProceduralMeshComponent;
class UStaticMeshComponent;

/**
 * Búnker refugio de la carrera (ETNBeachElement::Bunker, #689, Excel_DayT «Búnker»: neutral e indestructible). Planta
 * rectangular de hormigón con techo de losa, tronera hacia el mar (+X) y una puerta por detrás (-X) por la que cabe una
 * tortuga. Dentro hay un refugio (ATN_BeachShelterVolume, local en cada máquina): la tormenta no la ralentiza ni la
 * empuja y los enemigos no la marcan. Sin curación. Paredes y techo con colisión convexa.
 */
UCLASS()
class TORTUNABO_API ATN_BeachBunker : public ATN_BeachElement
{
	GENERATED_BODY()

public:
	ATN_BeachBunker();

	/** Semiejes del interior (cm) y alto libre. */
	FVector GetInteriorHalf() const { return InteriorHalf; }
	float GetDoorHalfWidth() const { return DoorHalfWidth; }
	float GetDoorHeight() const { return DoorHeight; }

protected:
	virtual void ApplySpec() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, Category = "Búnker")
	TObjectPtr<UStaticMeshComponent> BunkerMesh;

	UPROPERTY(VisibleAnywhere, Category = "Búnker")
	TObjectPtr<UProceduralMeshComponent> BunkerCollision;

	UPROPERTY(Transient)
	TObjectPtr<ATN_BeachShelterVolume> Shelter;

private:
	FVector InteriorHalf = FVector(290.0, 215.0, 240.0);
	float DoorHalfWidth = 95.f;
	float DoorHeight = 190.f;
};
