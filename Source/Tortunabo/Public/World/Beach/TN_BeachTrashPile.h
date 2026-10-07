#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "TN_BeachTrashPile.generated.h"

class ACharacter;
class UNiagaraSystem;
class UProceduralMeshComponent;
class USoundBase;
class UStaticMeshComponent;

/**
 * Montón de basura (ETNBeachElement::TrashPile, #690, Excel_DayT «Basura»): latas, bolsas, botellas y cajas apiladas
 * que bloquean un poco el paso. Correr contra él hace tropezar (derribo corto); un golpe lo rompe y deja paso: un objeto
 * lanzado, una bola de caparazón deprisa o un guantazo (#871; TNBeachCreatureRules::TrashPile). Sin daño.
 *
 * Es un enemigo quieto (ATN_BeachEnemy sin movimiento) solo para recibir los golpes de lo que se lanza por el mismo
 * camino que los enemigos (ApplyHitStun desde ATN_ThrowableItemActor y las bolas de caparazón). Red: bBroken replicado;
 * al romperse, cada máquina esconde el montón, quita la colisión y suelta las astillas.
 */
UCLASS()
class TORTUNABO_API ATN_BeachTrashPile : public ATN_BeachEnemy
{
	GENERATED_BODY()

public:
	ATN_BeachTrashPile();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void ApplyHitStun(float Seconds, AActor* InstigatorActor) override;
	virtual bool GetHitCapsule(FVector& OutA, FVector& OutB, float& OutRadius) const override;
	virtual float GetTickWakeDistance() const override { return 6000.f; }

	bool IsBroken() const { return bBroken; }

	/** Velocidad (cm/s) contra el montón desde la que se tropieza y segundos del tropezón. */
	UPROPERTY(EditAnywhere, Category = "Basura")
	float TripSpeed = 520.f;

	UPROPERTY(EditAnywhere, Category = "Basura")
	float TripSeconds = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Basura|Feedback")
	TObjectPtr<USoundBase> BreakSound;

	UPROPERTY(EditDefaultsOnly, Category = "Basura|Feedback")
	TObjectPtr<UNiagaraSystem> BreakVFX;

protected:
	virtual void ApplySpec() override;
	virtual void ServerTick(float DeltaSeconds) override;

	UFUNCTION()
	void OnRep_Broken();

	UPROPERTY(ReplicatedUsing = OnRep_Broken)
	bool bBroken = false;

	UPROPERTY(VisibleAnywhere, Category = "Basura")
	TObjectPtr<UStaticMeshComponent> PileMesh;

	/** Restos aplastados que quedan al romperlo (sin colisión). */
	UPROPERTY(VisibleAnywhere, Category = "Basura")
	TObjectPtr<UStaticMeshComponent> ScrapMesh;

	UPROPERTY(VisibleAnywhere, Category = "Basura")
	TObjectPtr<UProceduralMeshComponent> PileCollision;

private:
	/** En esta máquina: esconde el montón y suelta las astillas (una vez). */
	void ApplyBrokenLocal();

	double PileRadius = 250.0;
	double PileHeight = 70.0;
	bool bBrokenApplied = false;
	TMap<TWeakObjectPtr<ACharacter>, double> TripCooldown;
	FTNTrapBurst Chips;
};
