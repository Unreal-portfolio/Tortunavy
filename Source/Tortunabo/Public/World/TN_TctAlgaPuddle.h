#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_TctAlgaPuddle.generated.h"

class ATortugaCharacter;
class UStaticMeshComponent;

/**
 * Charco de alga de Todos contra Todos (#714): lo deja el objeto Alga (a los pies o lanzado corto) durante
 * TNTctItemTuning::AlgaPuddleSeconds. Las tortugas con los pies dentro (TNTctItemRules::IsInPuddle) resbalan: casi sin
 * rozamiento ni frenada y con poca aceleración (TNTctItemRules::SlipperyGrip), así que al girar siguen derivando hacia donde
 * iban, y el salto se queda corto. El caparazón no protege del resbalón.
 *
 * Red: el servidor lo crea en el suelo y lo destruye al acabarse (o si lo cubre el agua). El sitio llega en el primer paquete;
 * el resbalón lo aplica cada máquina que mueve a la tortuga (el servidor a todas y el cliente a la suya) con el mismo charco
 * y la misma regla, a través de UTN_TctItemComponent::SetSlipping, como las zonas lentas: el dueño y el servidor mueven igual.
 */
UCLASS()
class TORTUNABO_API ATN_TctAlgaPuddle : public AActor
{
	GENERATED_BODY()

public:
	ATN_TctAlgaPuddle();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Servidor: deja un charco en el suelo bajo Where (traza hacia abajo contra el escenario). nullptr si no hay suelo
	 * debajo.
	 */
	static ATN_TctAlgaPuddle* ServerSpawn(UWorld* World, const FVector& Where, APawn* Instigator);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Tct")
	TObjectPtr<UStaticMeshComponent> Mesh;

private:
	/** Nombre del resbalón de este charco en UTN_TctItemComponent (varios charcos a la vez no se pisan). */
	FName SlipSource() const;

	/** Pone o quita el resbalón a Turtle en esta máquina. */
	void SetSlipping(ATortugaCharacter* Turtle, bool bSlipping);

	/** Tortugas a las que esta máquina les ha puesto el resbalón. */
	TSet<TWeakObjectPtr<ATortugaCharacter>> Slipping;

	float Age = 0.f;
};
