#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachCreatureRules.h"
#include "World/Beach/TN_BeachElement.h"
#include "TN_BeachTankTrap.generated.h"

class ACharacter;
class UProceduralMeshComponent;
class USoundBase;
class UStaticMeshComponent;

/**
 * Erizos checos con comportamiento (#688, Excel_DayT «Erizos checos»): la colisión bloquea el paso y chocar deprisa
 * (corriendo o rodando en bola) rebota y derriba; andando solo bloquea (TNBeachCreatureRules::TankTrap). Sin daño.
 * A pie, derribo con ragdoll; en bola, rebote y mareo dentro del caparazón. Replicado, con su malla de tres vigas
 * cruzadas y su colisión, y el choque sobre su propio sitio (se crea con ATN_BeachElement::SpawnElement).
 */
UCLASS()
class TORTUNABO_API ATN_BeachTankTrap : public ATN_BeachElement
{
	GENERATED_BODY()

public:
	ATN_BeachTankTrap();

	virtual void Tick(float DeltaSeconds) override;
	virtual float GetTickWakeDistance() const override { return 6000.f; }

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
	/** Lo que puede chocar contra los erizos en este paso: la tortuga a pie o su bola de caparazón (#698). */
	struct FImpactor
	{
		/** La tortuga (clave del enfriamiento y de la velocidad anterior). */
		AActor* Actor = nullptr;
		TNBeachCreatureRules::TankTrap::EBody Body = TNBeachCreatureRules::TankTrap::EBody::Walker;
		FVector Location = FVector::ZeroVector;
		/** Velocidad horizontal ahora (cm/s). */
		FVector Velocity = FVector::ZeroVector;
	};

	/** Servidor: las tortugas (a pie o en su bola) que pueden chocar ahora. */
	void GatherImpactors(TArray<FImpactor>& Out) const;
	/** Distancia (cm) del centro del que choca a su contorno. */
	static double ReachOf(const FImpactor& Who);
	/** Servidor: choque de una tortuga o su bola contra un sitio (centro y radio). */
	void CheckImpact(const FImpactor& Who, const FVector& Center, float Radius, double Now);
	/** Servidor: el rebote y el derribo que tocan a Who (Dir: de Who hacia el erizo, plana). */
	void ApplyResponse(const FImpactor& Who, TNBeachCreatureRules::TankTrap::EResponse Response, const FVector& Dir);

	/** Velocidad horizontal del paso anterior (la colisión la anula en el paso del choque). */
	TMap<TWeakObjectPtr<AActor>, FVector> LastVelocity;
	TMap<TWeakObjectPtr<AActor>, double> CooldownUntil;
	float HogRadius = 140.f;
};
