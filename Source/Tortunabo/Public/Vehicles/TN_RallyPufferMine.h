// Pez globo del Rally y de Karts (#773): mina que se queda en la pista TNRallyTurret::PufferLifeSeconds, apoyada en el suelo
// según su normal. Se arma a los 0,5 s; un buggy a menos de 4 m (quien la lanzó, pasada su inmunidad) la dispara: se hincha
// 0,3 s y explota con la explosión del mortero (ATN_RallyProjectile::MortarBlastAt) y su ráfaga. Lo decide el servidor; los
// clientes ven el actor replicado y su hinchado (bTriggered).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_RallyPufferMine.generated.h"

class ATN_Buggy;
class UStaticMeshComponent;

UCLASS()
class TORTUNABO_API ATN_RallyPufferMine : public AActor
{
	GENERATED_BODY()

public:
	ATN_RallyPufferMine();

	/**
	 * Solo servidor: mina en el suelo bajo Where (solo el escenario, objetos WorldStatic), girada según su normal. Thrower,
	 * quien la lanza, es inmune a ella al principio. Null si no hay suelo debajo.
	 */
	static ATN_RallyPufferMine* SpawnOnGround(UWorld* World, const FVector& Where, ATN_Buggy* Thrower);

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool IsTriggered() const { return bTriggered; }

protected:
	virtual void BeginPlay() override;

private:
	/** Servidor: busca un buggy que la dispare. */
	void CheckTrigger();
	/** Servidor: explosión del mortero y fuera. */
	void Explode();
	void ApplyInflate();

	UFUNCTION()
	void OnRep_Triggered();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(ReplicatedUsing = OnRep_Triggered)
	bool bTriggered = false;

	/** Quien la lanzó (servidor): no la dispara durante su inmunidad. */
	TWeakObjectPtr<ATN_Buggy> Thrower;
	/** El buggy que la ha disparado (servidor): la ráfaga va por su torreta. */
	TWeakObjectPtr<ATN_Buggy> TriggeredBy;

	/** Hora local (s de mundo) en que se disparó, para el hinchado en cada máquina. */
	double TriggeredAt = -1.0;
	float CheckAccumulator = 0.f;
	bool bExploded = false;
};
