#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TN_TctItemComponent.generated.h"

class ACharacter;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 * Lo que los objetos de Todos contra Todos (#651) dejan en una tortuga y lo que se ve de sus disparos. No viene en la tortuga:
 * el servidor lo añade en ejecución la primera vez que hace falta y se replica solo (componente dinámico replicado, como
 * UTN_RaceItemComponent), así que los demás modos no lo llevan.
 *
 *  - Lastre del ancla (GrantHeavy): lenta y casi sin salto durante unos segundos. La hora de fin (del servidor) va replicada y
 *    cada máquina pone el tope de velocidad y de salto en su UTN_StaminaComponent, como el resto de límites: el dueño, el
 *    servidor y los demás mueven igual a la tortuga.
 *  - Disparos (MulticastShot): la estela de la pistola de noqueo, el cable del garfio y el abanico del trabuco, en todas las
 *    máquinas con pantalla (cosmético).
 *  - Resbalón del charco de alga (SetSlipping, #714): local, en cada máquina que mueve a la tortuga; lo ponen y lo quitan los
 *    charcos (ATN_TctAlgaPuddle) con su nombre. Con alguno puesto, el agarre del movimiento es TNTctItemRules::SlipperyGrip
 *    y el salto se queda corto; sin ninguno, vuelve el de antes.
 */
UCLASS(ClassGroup = (Custom))
class TORTUNABO_API UTN_TctItemComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_TctItemComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** El componente de la tortuga, si ya lo tiene (en cualquier máquina). */
	static UTN_TctItemComponent* FindOn(const AActor* Turtle);

	/** Servidor: el de la tortuga, creándolo y registrándolo (replicado) si aún no lo tiene. */
	static UTN_TctItemComponent* FindOrAddOn(ACharacter* Turtle);

	/** Servidor: lastre durante Seconds (alarga hasta el mayor de los dos finales). */
	void GrantHeavy(float Seconds);

	/** Servidor: quita el lastre ya (nueva ronda). */
	void ClearEffects();

	/** Lastrada ahora (cualquier máquina). */
	bool IsHeavy() const;

	/** Esta máquina: el charco Source empieza (bSlipping) o deja de hacer resbalar a la tortuga. */
	void SetSlipping(FName Source, bool bSlipping);

	/** Resbalando ahora en esta máquina (algún charco la tiene dentro). */
	bool IsSlipping() const { return SlipSources.Num() > 0; }

	/** Estela de un disparo de Kind (ETNTctItem) de From a To, en todas las máquinas con pantalla. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShot(uint8 Kind, FVector_NetQuantize From, FVector_NetQuantize To);

private:
	/** Hora del servidor en que se acaba el lastre (0 = sin lastre). */
	UPROPERTY(ReplicatedUsing = OnRep_HeavyEnd)
	float HeavyEnd = 0.f;

	UFUNCTION()
	void OnRep_HeavyEnd();

	/** Pone o quita los topes del lastre según HeavyEnd y la hora del servidor. */
	void ApplyHeavy();

	/** Una estela que se desvanece. */
	struct FTrail
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		float Age = 0.f;
		float Life = 0.25f;
		float Width = 1.f;
	};
	TArray<FTrail> Trails;

	void AddTrail(const FVector& From, const FVector& To, const FLinearColor& Color, float Width, float Life);
	void TickTrails(float DeltaTime);
	void RefreshTick();

	double Now() const;

	bool bHeavyApplied = false;

	/** Charcos que tienen dentro a la tortuga en esta máquina. */
	TSet<FName> SlipSources;
	/** El agarre de antes del primer charco (se devuelve al salir del último). */
	float BaseGroundFriction = 0.f;
	float BaseBrakingDeceleration = 0.f;
	float BaseMaxAcceleration = 0.f;

	/** Pone o quita el agarre del charco y el salto corto en el movimiento de la tortuga. */
	void ApplySlip(bool bSlip);
};
