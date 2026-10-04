#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachElement.h"
#include "TN_BeachTankTrap.generated.h"

class ACharacter;
class UProceduralMeshComponent;
class USoundBase;
class UStaticMeshComponent;

/**
 * Erizos checos con comportamiento (#688, Excel_DayT «Erizos checos»): la colisión bloquea el paso y chocar deprisa
 * (corriendo, rodando en bola o en buggy) rebota y derriba; andando solo bloquea (TNBeachCreatureRules::TankTrap). Sin daño.
 *
 * Dos usos:
 *  - Vigilante de la carrera (SpawnGuard): los erizos antitanque de la ronda son decorado instanciado con su colisión
 *    (ATN_BeachDecorField); un único vigilante del servidor, sin réplica ni malla, conoce sus sitios y aplica el choque.
 *  - Erizo suelto de Supervivencia (SpawnStandalone): replicado, con su malla de tres vigas cruzadas y su colisión, y el
 *    choque sobre su propio sitio.
 */
UCLASS()
class TORTUNABO_API ATN_BeachTankTrap : public ATN_BeachElement
{
	GENERATED_BODY()

public:
	ATN_BeachTankTrap();

	virtual void Tick(float DeltaSeconds) override;
	virtual float GetTickWakeDistance() const override { return bGuard ? 0.f : 6000.f; }

	/** Servidor: el vigilante de los erizos de la ronda (Spots: X, Y, Z del suelo y radio de choque en W). */
	static ATN_BeachTankTrap* SpawnGuard(UWorld* World, const TArray<FVector4>& Spots);

	/** Servidor: un erizo suelto de radio Radius (cm) en Transform. */
	static ATN_BeachTankTrap* SpawnStandalone(UWorld* World, const FTransform& Transform, float Radius);

	/** Velocidad (cm/s) hacia el erizo desde la que derriba, segundos de derribo y rebote. */
	UPROPERTY(EditAnywhere, Category = "Erizo checo")
	float KnockSpeed = 600.f;

	UPROPERTY(EditAnywhere, Category = "Erizo checo")
	float KnockSeconds = 1.2f;

	UPROPERTY(EditAnywhere, Category = "Erizo checo")
	float BounceBack = 520.f;

	UPROPERTY(EditAnywhere, Category = "Erizo checo")
	float BounceUp = 380.f;

	/** El choque, en el servidor (lo oyen todos por el derribo de la tortuga; el golpe metálico va aparte). */
	UPROPERTY(EditDefaultsOnly, Category = "Erizo checo|Feedback")
	TObjectPtr<USoundBase> ClangSound;

protected:
	virtual void ApplySpec() override;

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastClang(FVector_NetQuantize At);

	UPROPERTY(VisibleAnywhere, Category = "Erizo checo")
	TObjectPtr<UStaticMeshComponent> HogMesh;

	UPROPERTY(VisibleAnywhere, Category = "Erizo checo")
	TObjectPtr<UProceduralMeshComponent> HogCollision;

private:
	/** Servidor: choque de una tortuga contra un sitio (centro y radio). */
	void CheckImpact(ACharacter* Turtle, const FVector& Center, float Radius, double Now);

	bool bGuard = false;
	/** Sitios vigilados (X, Y, Z, radio). En el suelto, el suyo. */
	TArray<FVector4> Spots;
	/** Velocidad horizontal del paso anterior (la colisión la anula en el paso del choque). */
	TMap<TWeakObjectPtr<ACharacter>, FVector> LastVelocity;
	TMap<TWeakObjectPtr<ACharacter>, double> CooldownUntil;
	float StandaloneRadius = 140.f;
};
