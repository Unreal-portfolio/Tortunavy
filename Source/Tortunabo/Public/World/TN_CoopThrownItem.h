#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_CoopThrownItem.generated.h"

class ATortugaCharacter;
class UStaticMeshComponent;

/** El lanzamiento de un objeto del coop: replicado una vez; cada máquina dibuja el mismo arco desde aquí. */
USTRUCT()
struct FTNCoopThrowData
{
	GENERATED_BODY()

	/** ETNCoopItem lanzado. */
	UPROPERTY()
	uint8 Kind = 0;

	UPROPERTY()
	FVector_NetQuantize Origin = FVector::ZeroVector;

	/** Dónde cae (ya recortado a su alcance y en el suelo). */
	UPROPERTY()
	FVector_NetQuantize Target = FVector::ZeroVector;

	/** Hora del servidor del lanzamiento y segundos de vuelo. */
	UPROPERTY()
	float StartTime = 0.f;

	UPROPERTY()
	float FlightSeconds = 0.5f;

	UPROPERTY()
	float ArcHeight = 100.f;
};

/**
 * Objeto del coop lanzado en arco: la cáscara resbaladiza (que queda en el suelo como parche).
 *
 * Red, como el proyectil de Todos contra Todos: sin movimiento replicado. El servidor lo crea con el lanzamiento (Throw, en el
 * primer paquete) y cada máquina pone el objeto en el mismo arco por la hora del servidor (TNCoopItemRules::ArcPoint): lo ve
 * igual quien lanza, el anfitrión y los demás, y quien llega tarde lo ve ya en el suelo. Lo que pasa lo decide solo el
 * servidor:
 *  - Cáscara: al caer queda como parche. Pasado PeelArmSeconds, la primera tortuga que lo pisa (cualquiera, también quien lo
 *    lanzó) resbala (UTN_TurtleMovementComponent::LaunchFromServer con TNCoopItemRules::SlipVelocity: hacia donde iba y algo
 *    en el aire, sin agarre ni apenas control un instante, sin derribo; el dueño lo estrena en su propio movimiento, sin
 *    corrección) y el parche se gasta. Un enemigo que lo pisa se marea un momento. Se va solo a los PeelLifeSeconds.
 */
UCLASS(NotPlaceable)
class TORTUNABO_API ATN_CoopThrownItem : public AActor
{
	GENERATED_BODY()

public:
	ATN_CoopThrownItem();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Servidor: Thrower lanza Kind (ETNCoopItem) hacia donde apunta, hasta MaxRange cm en planta y al suelo. false si no se ha
	 * podido crear.
	 */
	static bool ServerThrow(ATortugaCharacter* Thrower, uint8 Kind, float MaxRange);

	uint8 GetKind() const { return Throw.Kind; }

	/** Ya ha caído (cualquier máquina, por la hora del servidor). */
	bool HasLanded() const;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Coop")
	TObjectPtr<UStaticMeshComponent> Mesh;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Throw)
	FTNCoopThrowData Throw;

	UFUNCTION()
	void OnRep_Throw();

	/** Pone la malla del objeto (una vez por máquina). */
	void ApplyThrow();

	/** Servidor: el parche lo ha pisado alguien en Where: sonido y que se vaya. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSlip(FVector_NetQuantize Where);

	/** Servidor: mira si alguien pisa el parche de la cáscara. */
	void ServerTickPeel();

	double Now() const;
	float FlightAlpha() const;

	bool bThrowApplied = false;
	bool bLandedHere = false;
	bool bSpent = false;
	float PeelScanClock = 0.f;
};
