#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachCreatureRules.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "TN_BeachBurrowCrab.generated.h"

class USoundBase;
class UStaticMeshComponent;

/**
 * Cangrejo subterráneo (ETNBeachElement::BurrowCrab, #686, Excel_DayT «Cangrejo 3»): enterrado bajo un montículo de arena.
 * Con una tortuga a TellRadius el montículo tiembla (aviso), saca la pinza y, si sigue al alcance, la atrapa unos segundos
 * en alto (se suelta machacando salto) y la lanza por el aire en bola y mareada (TNBeach::StunTurtle). Después recarga
 * enterrado. Un objeto lanzado lo hace esconderse sin atacar. Sin daño. Máquina de estados pura en
 * TNBeachCreatureRules::BurrowCrab.
 *
 * Red: el servidor decide (Mover.State y Grabbed replicados) y cada máquina anima el montículo y la pinza con la edad del
 * estado y sujeta a la atrapada con BeginHoldTurtle/PlaceHeldTurtle, como el pulpo.
 */
UCLASS()
class TORTUNABO_API ATN_BeachBurrowCrab : public ATN_BeachEnemy
{
	GENERATED_BODY()

public:
	ATN_BeachBurrowCrab();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, Category = "Cangrejo subterráneo")
	float TellRadius = 400.f;

	UPROPERTY(EditAnywhere, Category = "Cangrejo subterráneo")
	float GrabRadius = 190.f;

	/** Lanzamiento al soltarla: hacia fuera y hacia arriba (cm/s), y mareo en bola. */
	UPROPERTY(EditAnywhere, Category = "Cangrejo subterráneo")
	float ThrowOut = 650.f;

	UPROPERTY(EditAnywhere, Category = "Cangrejo subterráneo")
	float ThrowUp = 850.f;

	UPROPERTY(EditAnywhere, Category = "Cangrejo subterráneo")
	float ThrowStunSeconds = 2.f;

	UPROPERTY(EditDefaultsOnly, Category = "Cangrejo subterráneo|Feedback")
	TObjectPtr<USoundBase> SnapSound;

protected:
	virtual void ApplySpec() override;
	virtual void ServerTick(float DeltaSeconds) override;
	virtual void VisualTick(float DeltaSeconds) override;
	virtual void OnMoverStateChanged(uint8 OldState) override;
	virtual void OnHoldAborted(ATortugaCharacter* Turtle) override;
	virtual float GetBodyRadius() const override { return 130.f; }

	UPROPERTY(Replicated)
	TObjectPtr<ATortugaCharacter> Grabbed;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Mound;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> ClawRoot;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Finger;

private:
	void Build();
	void UpdateHold();
	/** Servidor: la suelta lanzada y mareada (o sin lanzar si bThrow es false). */
	void ReleaseVictim(bool bThrow);
	/** Altura de la pinza (cm sobre la arena) en cada estado. */
	float ClawHeight(TNBeachCreatureRules::BurrowCrab::EState State, float Age) const;
	FVector GripPoint() const;

	TNBeachCreatureRules::BurrowCrab::FTimes Times;
	bool bPlaced = false;
	TWeakObjectPtr<ATortugaCharacter> Target;
	float ShakeClock = 0.f;
};
