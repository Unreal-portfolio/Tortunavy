#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "TN_BeachDragCrab.generated.h"

class UStaticMeshComponent;
class USoundBase;

/**
 * Cangrejo arrastrador (ETNBeachElement::DragCrab, #685, Excel_DayT «Cangrejo 2»): un cangrejo mediano (el del cangrejo
 * gigante a un tercio) que ronda su sitio, persigue a la tortuga que ve a 7 m más despacio de lo que ella corre, la
 * engancha con la pinza y la arrastra hacia atrás (hacia la salida en la carrera; hacia su sitio en Supervivencia) como
 * mucho MaxDragDistance. Al acabar la suelta derribada con un empujón. Machacar salto o un objeto lanzado la sueltan
 * antes. Nunca la deja en una zona de muerte ni en un desnivel: si delante no hay suelo seguro, la suelta ahí
 * (TNBeachCreatureRules::DragCrab).
 *
 * Red: el servidor decide (Mover y Grabbed replicados); cada máquina sujeta a la arrastrada en la pinza con
 * BeginHoldTurtle/PlaceHeldTurtle, igual que el pulpo, así que no hay tirones en el cliente arrastrado.
 */
UCLASS()
class TORTUNABO_API ATN_BeachDragCrab : public ATN_BeachEnemy
{
	GENERATED_BODY()

public:
	ATN_BeachDragCrab();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, Category = "Cangrejo arrastrador")
	float DetectRange = 700.f;

	/** Más despacio que andar (450) y que correr (800): se le escapa. */
	UPROPERTY(EditAnywhere, Category = "Cangrejo arrastrador")
	float ChaseSpeed = 380.f;

	UPROPERTY(EditAnywhere, Category = "Cangrejo arrastrador")
	float RoamSpeed = 140.f;

	UPROPERTY(EditAnywhere, Category = "Cangrejo arrastrador")
	float DragSpeed = 230.f;

	UPROPERTY(EditAnywhere, Category = "Cangrejo arrastrador")
	float MaxDragDistance = 600.f;

	/** No se aleja más de esto de su sitio persiguiendo. */
	UPROPERTY(EditAnywhere, Category = "Cangrejo arrastrador")
	float Leash = 2500.f;

	UPROPERTY(EditAnywhere, Category = "Cangrejo arrastrador")
	float KnockSeconds = 1.2f;

	UPROPERTY(EditDefaultsOnly, Category = "Cangrejo arrastrador|Feedback")
	TObjectPtr<USoundBase> GrabSound;

protected:
	virtual void ApplySpec() override;
	virtual void ServerTick(float DeltaSeconds) override;
	virtual void VisualTick(float DeltaSeconds) override;
	virtual void OnMoverStateChanged(uint8 OldState) override;
	virtual void OnHoldAborted(ATortugaCharacter* Turtle) override;
	virtual float GetBodyRadius() const override { return BodyRadius; }

	/** La que arrastra (para sujetarla en cada máquina). */
	UPROPERTY(Replicated)
	TObjectPtr<ATortugaCharacter> Grabbed;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Scaler;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Legs;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Claw;

private:
	void BuildCrab();
	void UpdateHold();
	/** Servidor: anda hacia Goal a Speed (rodeando lo grande y sin meterse en otros enemigos). */
	void WalkToward(const FVector& Goal, float Speed, float DeltaSeconds);
	/** Servidor: suelta a la arrastrada con el final End. */
	void ReleaseDrag(uint8 End);
	/** Servidor: hacia dónde arrastra a Victim. */
	FVector DragDirectionFor(const ATortugaCharacter* Victim) const;
	/** Servidor: el punto Next es seguro para seguir arrastrando (suelo sin desnivel y fuera de zonas de muerte). */
	bool IsSafeAhead(const FVector& Next) const;
	FVector GripPoint() const;

	float SizeK = 0.32f;
	float BodyRadius = 90.f;
	FVector DragDir = FVector::BackwardVector;
	float Dragged = 0.f;
	FVector RoamGoal = FVector::ZeroVector;
	float RoamTimer = 0.f;
	bool bPlaced = false;
	TWeakObjectPtr<ATortugaCharacter> Target;
	float LegPhase = 0.f;
};
