#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Game/TN_CoopItemRules.h"
#include "World/Beach/TN_RaceItemSynth.h"
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
 * que hace falta y se replica solo (componente dinámico replicado).
 *
 *  - Pez globo (GrantPuffer): TNCoopItemTuning::PufferSeconds sin derribo ni aturdimiento (ATortugaCharacter::ApplyKnockdown,
 *    TNBeach::StunTurtle y KnockDownTurtle lo miran con TNItemRuntime::IsInvulnerable) y, al acabar, un mareo corto: lenta
 *    (tope de velocidad en UTN_StaminaComponent, que cada máquina pone por la hora replicada, como el lastre del ancla: el
 *    dueño, el servidor y los demás la mueven igual, sin correcciones) y con los pajaritos. Con pantalla, la tortuga se
 *    infla de pinchos mientras dura la protección.
 *
 *  - Sonidos de los objetos (MulticastCue, sintetizados: UTN_RaceItemSynthComponent) y el cable del arpón (MulticastRope),
 *    que el servidor manda a todas las máquinas (TNItemRuntime::PlayCue y ShowHarpoonRope).
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

	/** Un sonido de los objetos en la tortuga para todas las máquinas (el «nop» de no poder usar algo, el soplido de lanzar...). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastCue(ETNRaceSound Sound, float Pitch);

	/** El cable del arpón de From a To, en todas las máquinas con pantalla (se adelgaza hasta desaparecer). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastRope(FVector_NetQuantize From, FVector_NetQuantize To);

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

	/** Sintetizador de los sonidos de los objetos (solo en máquinas con sonido, se crea la primera vez). */
	UPROPERTY(Transient)
	TObjectPtr<UTN_RaceItemSynthComponent> Sfx;

	UTN_RaceItemSynthComponent* GetSfx();

	/** Cable del arpón que se está desvaneciendo (malla del motor estirada de un extremo al otro). */
	struct FRope
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		float Age = 0.f;
	};
	TArray<FRope> Ropes;

	/** Adelgaza los cables y quita los que ya se han acabado. */
	void TickRopes(float DeltaTime);

	/** El tick solo mientras dure un efecto o quede un cable a la vista. */
	void RefreshTick();

	bool bDizzyApplied = false;
	bool bProtectShown = false;
	float PulseClock = 0.f;
};
