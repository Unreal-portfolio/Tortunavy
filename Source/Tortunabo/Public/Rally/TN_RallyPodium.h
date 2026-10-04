// Podio del Rally (#306): una losa que flota sobre la meta (no estorba a los que siguen corriendo) con tres escalones. El
// servidor lo crea al tener pista y aparca en él cada buggy que llega (ATN_RallyGameMode::ParkFinishedTeams): los tres
// primeros en los escalones y el resto en fila detrás, con sus tortugas sentadas. Se replica para que los clientes lo vean y
// la cámara de llegada (UTN_RallyCameraDirector) sepa dónde mirar.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rally/TN_RallyCameraLogic.h"
#include "TN_RallyPodium.generated.h"

class UStaticMeshComponent;

UCLASS(NotBlueprintable)
class TORTUNABO_API ATN_RallyPodium : public AActor
{
	GENERATED_BODY()

public:
	ATN_RallyPodium();

	virtual void BeginPlay() override;

	/** Servidor: crea el podio sobre el centro de la meta mirando contra el sentido de la carrera. */
	static ATN_RallyPodium* SpawnAtFinish(UWorld* World, const FVector& FinishCenter, const FVector& RaceForward);

	/** El podio de este mundo (nullptr si aún no ha llegado al cliente). */
	static ATN_RallyPodium* Find(const UWorld* World);

	/** Marco del podio (origen en la cara de arriba de la losa, Facing hacia donde miran los buggies). */
	TNRallyCamera::FPodiumFrame GetFrame() const;

	/** Dónde aparcar al FinishOrder-ésimo en llegar: sobre su escalón o su hueco, mirando a la cámara del podio. */
	FTransform GetSlotTransform(int32 FinishOrder, double OriginAboveBottomCm) const;

private:
	UPROPERTY(VisibleAnywhere, Category = "Rally")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Rally")
	TObjectPtr<UStaticMeshComponent> Slab;

	UPROPERTY(VisibleAnywhere, Category = "Rally")
	TArray<TObjectPtr<UStaticMeshComponent>> Steps;
};
