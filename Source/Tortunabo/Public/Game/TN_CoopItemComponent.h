#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Game/TN_CoopItemRules.h"
#include "TN_CoopItemComponent.generated.h"

class ACharacter;
class UStaticMeshComponent;

/** Efectos de los objetos del coop en una tortuga, en un solo struct replicado (horas del servidor; 0 = ninguno). */
USTRUCT()
struct FTNCoopEffectNet
{
	GENERATED_BODY()

	/** Pez globo: fin de la protección y fin del mareo de después. */
	UPROPERTY()
	float ProtectEnd = 0.f;

	UPROPERTY()
	float DizzyEnd = 0.f;
};

/**
 * Lo que los objetos del coop dejan en una tortuga. No viene en la tortuga: el servidor lo añade en ejecución la primera vez
 * que hace falta y se replica solo (componente dinámico replicado, como UTN_TctItemComponent).
 *
 *  - Pez globo (GrantPuffer): TNCoopItemTuning::PufferSeconds sin derribo ni aturdimiento (ATortugaCharacter::ApplyKnockdown,
 *    TNBeach::StunTurtle y KnockDownTurtle lo miran con TNRaceItems::IsInvulnerable) y, al acabar, un mareo corto: lenta
 *    (tope de velocidad en UTN_StaminaComponent, que cada máquina pone por la hora replicada, como el lastre del ancla: el
 *    dueño, el servidor y los demás la mueven igual, sin correcciones) y con los pajaritos. Con pantalla, la tortuga se
 *    infla de pinchos mientras dura la protección.
 *
 * Servidor: GrantPuffer y ClearEffects. Todas las máquinas: IsProtected e IsDizzy.
 */
UCLASS(ClassGroup = (Custom))
class TORTUNABO_API UTN_CoopItemComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_CoopItemComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** El componente de la tortuga, si ya lo tiene (en cualquier máquina). */
	static UTN_CoopItemComponent* FindOn(const AActor* Turtle);

	/** Servidor: el de la tortuga, creándolo y registrándolo (replicado) si aún no lo tiene. */
	static UTN_CoopItemComponent* FindOrAddOn(ACharacter* Turtle);

	/** true si el pez globo protege ahora a Turtle (cualquier máquina). */
	static bool IsTurtleProtected(const AActor* Turtle);

	/** Servidor: come un pez globo. false si aún le dura la protección del anterior (no se apila). */
	bool GrantPuffer();

	/** Servidor: quita los efectos ya. */
	void ClearEffects();

	bool IsProtected() const;
	bool IsDizzy() const;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Effects)
	FTNCoopEffectNet Effects;

	UFUNCTION()
	void OnRep_Effects();

	/** Pone el mareo y lo que se ve según los datos y la hora; enciende el tick mientras quede algo. */
	void ApplyEffects();

	FTNPufferState PufferState() const;
	double Now() const;

	/** Pinchos del pez globo alrededor de la tortuga (solo en máquinas con pantalla, se crean la primera vez). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Spikes;

	bool bDizzyApplied = false;
	bool bProtectShown = false;
	float PulseClock = 0.f;
};
