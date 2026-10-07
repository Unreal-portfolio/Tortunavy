#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_AirdropPoint.generated.h"

class UBillboardComponent;
class USceneComponent;

/**
 * Punto de airdrop (#860): un sitio del mapa, colocado a mano, donde puede caer la caja de suministros. El gestor del
 * servidor (UTN_AirdropSubsystem) elige uno libre al azar cada vez que toca un airdrop. Solo existe en el servidor (no se
 * replica); en el editor se ve como un icono. Se pone en el suelo: la caja aterriza en el suelo que haya bajo él.
 */
UCLASS()
class TORTUNABO_API ATN_AirdropPoint : public AActor
{
	GENERATED_BODY()

public:
	ATN_AirdropPoint();

	/** Si el gestor puede elegirlo (para apagar un punto sin borrarlo). */
	bool IsEnabled() const { return bEnabled; }

	/** Altura (cm) desde la que cae la caja en este punto (0 = la del gestor). */
	float GetDropHeight() const { return DropHeight; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Airdrop")
	TObjectPtr<USceneComponent> SceneRoot;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category = "Airdrop")
	TObjectPtr<UBillboardComponent> Icon;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airdrop")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airdrop", meta = (ClampMin = "0.0"))
	float DropHeight = 0.f;
};
