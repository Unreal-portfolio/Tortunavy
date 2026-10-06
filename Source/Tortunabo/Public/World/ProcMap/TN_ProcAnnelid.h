#pragma once

#include "CoreMinimal.h"
#include "World/TN_DirectInteractableBase.h"
#include "TN_ProcAnnelid.generated.h"

class UStaticMeshComponent;
class USoundBase;
class UNiagaraSystem;

/**
 * Reglas del anélido poliqueto (#792) como funciones puras: alcance de 3 m, una sola caza y la animación de salir del
 * suelo y volver a meterse. Las prueban Tortunabo.ProcMap.Annelid.*.
 */
namespace TNAnnelid
{
	/** Radio (cm) desde el centro de la boca en el que se puede cazar (GDD: rango de detección 3 m). */
	constexpr float HUNT_RADIUS = 300.f;

	/** Alcance de interacción de la tortuga (ATortugaCharacter::DefaultInteractionDistance). */
	constexpr float TURTLE_REACH = 250.f;

	/** Segundos que tarda en salir, que se queda fuera y que tarda en meterse (después desaparece). */
	constexpr float EMERGE_SECONDS = 0.35f;
	constexpr float HOLD_SECONDS = 0.6f;
	constexpr float SINK_SECONDS = 0.5f;

	/** Segundos desde la caza hasta que el actor se destruye. */
	constexpr float LIFE_AFTER_HUNT = EMERGE_SECONDS + HOLD_SECONDS + SINK_SECONDS + 0.5f;

	/**
	 * Cuánto (cm) se acerca el punto de interacción a la tortuga desde el centro de la boca: así la tortuga, que alcanza
	 * TURTLE_REACH, puede cazarlo desde HUNT_RADIUS del centro.
	 */
	inline float InteractionPointShift(float DistanceToCenter)
	{
		return FMath::Clamp(DistanceToCenter, 0.f, HUNT_RADIUS - TURTLE_REACH);
	}

	/** true si una tortuga a esa distancia del centro alcanza a cazarlo. */
	inline bool InHuntRange(float DistanceToCenter)
	{
		return DistanceToCenter - InteractionPointShift(DistanceToCenter) <= TURTLE_REACH;
	}

	/** Altura del gusano (0 = escondido bajo la boca, 1 = fuera del todo) a Seconds desde la caza. */
	inline float EmergeHeight01(float Seconds)
	{
		if (Seconds <= 0.f) { return 0.f; }
		if (Seconds < EMERGE_SECONDS) { return FMath::InterpEaseOut(0.f, 1.f, Seconds / EMERGE_SECONDS, 2.f); }
		const float AfterHold = Seconds - EMERGE_SECONDS - HOLD_SECONDS;
		if (AfterHold <= 0.f) { return 1.f; }
		return AfterHold >= SINK_SECONDS ? 0.f : FMath::InterpEaseIn(1.f, 0.f, AfterHold / SINK_SECONDS, 2.f);
	}

	/** Estado de una boca: se caza una sola vez. */
	struct FHuntState
	{
		bool bConsumed = false;

		/** Servidor: true (y la consume) si se puede cazar desde esa distancia y no estaba consumida. */
		bool TryHunt(float DistanceToCenter)
		{
			if (bConsumed || !InHuntRange(DistanceToCenter)) { return false; }
			bConsumed = true;
			return true;
		}
	};
}

/**
 * Anélido poliqueto (#792, GDD: aliado de estamina): una boca estática en el suelo, estilo gusano de Dune. Al cazarlo
 * (interactuar a 3 m como mucho, un solo golpe) el gusano sale, se consume y rellena la estamina de quien lo ha cazado.
 * No toca la vida (regla del director: sin vida, veneno ni curas). Lo coloca el generador del coop solo en los tramos
 * Fácil y Medio de la tabla de intensidad (#788).
 *
 * Red: el servidor decide la caza (bConsumed replicado) y la estamina (UTN_StaminaComponent se replica al dueño); cada
 * máquina anima la salida del gusano y suena al recibir bConsumed.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_ProcAnnelid : public ATN_DirectInteractableBase
{
	GENERATED_BODY()

public:
	ATN_ProcAnnelid();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool CanInteract(APawn* Interactor) const override;
	virtual FVector GetInteractionPointFor(const APawn* Interactor) const override;

	UFUNCTION(BlueprintPure, Category = "Annelid")
	bool IsConsumed() const { return HuntState.bConsumed; }

protected:
	virtual void BeginPlay() override;
	virtual void OnInteracted_Implementation(APawn* Interactor) override;

	/** El gusano que sale de la boca al cazarlo (sin colisión). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Annelid")
	TObjectPtr<UStaticMeshComponent> Body;

	/** Sonido al cazarlo (en cada máquina, en la boca). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Annelid|FX")
	TObjectPtr<USoundBase> HuntSound;

	/** Efecto al salir el gusano (arena que salta), en cada máquina con pantalla. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Annelid|FX")
	TObjectPtr<UNiagaraSystem> EmergeFX;

	/** Cuánto sube el gusano sobre la boca (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Annelid", meta = (ClampMin = "10.0"))
	float EmergeHeight = 160.f;

private:
	UFUNCTION()
	void OnRep_Consumed();

	/** Empieza la animación y los efectos de la caza en esta máquina. */
	void PlayHuntLocal();

	UPROPERTY(ReplicatedUsing = OnRep_Consumed)
	bool bConsumedReplicated = false;

	TNAnnelid::FHuntState HuntState;

	/** Segundos desde que esta máquina vio la caza (negativo: aún no). */
	float SinceHunt = -1.f;

	/** Cota local del gusano escondido. */
	float BodyHiddenZ = 0.f;
};
