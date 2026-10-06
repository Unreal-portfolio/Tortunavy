#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_TctJellyPad.generated.h"

class ATortugaCharacter;
class UStaticMeshComponent;

/**
 * Medusa trampolín de Todos contra Todos (#777): la planta el objeto MedusaTrampolin en el suelo delante de quien la usa y
 * dura TNTctItemTuning::JellyLifeSeconds. Cualquier tortuga que la pisa (TNTctItemRules::JellyTouches), también quien la
 * puso, bota TNTctItemTuning::JellyBounceHeight hacia arriba (TNTctItemRules::BounceSpeed con la gravedad de esa tortuga):
 * sirve para subir a las plataformas altas cuando sube el agua.
 *
 * Red: el servidor decide los botes y los da con UTN_TurtleMovementComponent::LaunchFromServer (el dueño lo estrena en su
 * propio movimiento: sin correcciones). El aplastón de la medusa al botar es un multicast cosmético.
 */
UCLASS()
class TORTUNABO_API ATN_TctJellyPad : public AActor
{
	GENERATED_BODY()

public:
	ATN_TctJellyPad();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Servidor: Planter planta una medusa en el suelo delante de ella. nullptr si no hay suelo. */
	static ATN_TctJellyPad* ServerPlant(ATortugaCharacter* Planter);

	/** Servidor: una vuelta de los botes (lo que hace su Tick; pública para las pruebas). Devuelve cuántas han botado. */
	int32 ServerBounceTurtles();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Tct")
	TObjectPtr<UStaticMeshComponent> Mesh;

private:
	/** Todas las máquinas: la medusa se aplasta y rebota. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSquash();

	/** Servidor: hasta cuándo (Age) no vuelve a botar a cada tortuga. */
	TMap<TWeakObjectPtr<ATortugaCharacter>, float> RearmUntil;

	float Age = 0.f;
	float SquashAge = -1.f;
};
