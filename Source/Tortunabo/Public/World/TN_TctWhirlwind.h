#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_TctWhirlwind.generated.h"

class ATortugaCharacter;
class UStaticMeshComponent;

/**
 * Remolino de arena de Todos contra Todos (#830): lo planta el objeto Remolino en el suelo delante de quien lo usa y dura
 * TNTctItemTuning::WhirlLifeSeconds. Cada TNTctItemTuning::WhirlKickSeconds lanza al aire (TNTctItemRules::WhirlKick: arriba y
 * girando hacia fuera) a toda tortuga que esté dentro de su radio, también a quien lo plantó: sirve para despejar un paso o
 * un punto de objetos, no para quedarse al lado.
 *
 * Red: el servidor decide los lanzamientos y los da con UTN_TurtleMovementComponent::LaunchFromServer (el dueño lo estrena en
 * su propio movimiento: sin correcciones). El giro y el tamaño los pone cada máquina con la edad del actor.
 */
UCLASS()
class TORTUNABO_API ATN_TctWhirlwind : public AActor
{
	GENERATED_BODY()

public:
	ATN_TctWhirlwind();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Servidor: Planter planta un remolino en el suelo delante de ella. nullptr si no hay suelo. */
	static ATN_TctWhirlwind* ServerPlant(ATortugaCharacter* Planter);

	/** Servidor: una vuelta de los lanzamientos (lo que hace su Tick; pública para las pruebas). Devuelve a cuántas lanza. */
	int32 ServerKickTurtles();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Tct")
	TObjectPtr<UStaticMeshComponent> Mesh;

private:
	/** Servidor: hasta cuándo (Age) no vuelve a lanzar a cada tortuga. */
	TMap<TWeakObjectPtr<ATortugaCharacter>, float> RearmUntil;

	float Age = 0.f;
};
