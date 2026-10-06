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
 * Objeto del coop lanzado en arco: la cáscara resbaladiza (que queda en el suelo como parche) y la concha (que aturde al enemigo
 * al que va).
 *
 * Red, como el proyectil de Todos contra Todos: sin movimiento replicado. El servidor lo crea con el lanzamiento (Throw, en el
 * primer paquete) y cada máquina pone el objeto en el mismo arco por la hora del servidor (TNCoopItemRules::ArcPoint): lo ve
 * igual quien lanza, el anfitrión y los demás, y quien llega tarde lo ve ya en el suelo. Lo que pasa lo decide solo el
 * servidor:
 *  - Cáscara: al caer queda como parche. Pasado PeelArmSeconds, la primera tortuga que lo pisa (cualquiera, también quien lo
 *    lanzó) resbala (UTN_TurtleMovementComponent::LaunchFromServer con TNCoopItemRules::SlipVelocity: hacia donde iba y algo
 *    en el aire, sin agarre ni apenas control un instante, sin derribo; el dueño lo estrena en su propio movimiento, sin
 *    corrección) y el parche se gasta. Un enemigo que lo pisa se marea un momento. Se va solo a los PeelLifeSeconds.
 *  - Concha: el servidor elige al lanzarla el enemigo de la mira a ShellRange como mucho (ATN_BeachEnemy que se marea o
 *    ITN_EnemyTargetInterface; nunca una tortuga: TNCoopItemRules::CanShellStun) y la concha vuela hacia él. Al llegar, si el
 *    enemigo sigue cerca del punto (IsShellHit), lo aturde ShellStunSeconds. Sin enemigo, cae al suelo y se va.
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

	/** Servidor: Thrower lanza una concha al enemigo de su mira (o al suelo si no hay). false si no se ha podido crear. */
	static bool ServerThrowShell(ATortugaCharacter* Thrower);

	/**
	 * Servidor: el enemigo al que va una concha lanzada por Thrower (el más cercano en la mira a ShellRange, sin tortugas) en
	 * OutEnemy y el punto al que hay que tirarla (el enemigo o, si no hay, donde acaba la mira).
	 */
	static FVector FindShellTarget(const ATortugaCharacter* Thrower, AActor*& OutEnemy);

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

	/** Servidor: crea el lanzamiento de Kind de Origin a Target (ya en su sitio). Null si no se ha podido. */
	static ATN_CoopThrownItem* SpawnThrow(ATortugaCharacter* Thrower, uint8 Kind, const FVector& Origin, const FVector& Target);

	/** De dónde sale lo que lanza Thrower (delante de ella, a la altura de la aleta). */
	static FVector HandOf(const ATortugaCharacter* Thrower);

	/** Servidor: la concha ha llegado: aturde a StunTarget si sigue ahí. */
	void ServerShellImpact();

	/** Servidor: ya ha hecho lo suyo (el parche pisado, la concha que ha dado): que se esconda y se vaya. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSpent(FVector_NetQuantize Where);

	/** Servidor: mira si alguien pisa el parche de la cáscara. */
	void ServerTickPeel();

	double Now() const;
	float FlightAlpha() const;

	/** Servidor: el enemigo al que va la concha. */
	TWeakObjectPtr<AActor> StunTarget;

	bool bThrowApplied = false;
	bool bLandedHere = false;
	bool bSpent = false;
	float PeelScanClock = 0.f;
};
