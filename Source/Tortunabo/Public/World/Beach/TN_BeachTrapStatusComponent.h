#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "World/Beach/TN_BeachCreatureRules.h"
#include "TN_BeachTrapStatusComponent.generated.h"

class ACharacter;
class APlayerController;

/** Ralentización temporal replicada (hora del servidor en que acaba y fracción de la velocidad de andar que queda). */
USTRUCT()
struct FTNBeachSlowState
{
	GENERATED_BODY()

	UPROPERTY()
	float EndServerTime = 0.f;

	UPROPERTY()
	float SpeedFactor = 1.f;

	/** Sube en cada ralentización (aunque repita valores). */
	UPROPERTY()
	uint8 Serial = 0;
};

/** Atrapada sin control (arenas movedizas): dónde queda quieta y, al soltarla, el saltito y el mareo. */
USTRUCT()
struct FTNBeachTrapState
{
	GENERATED_BODY()

	UPROPERTY()
	bool bTrapped = false;

	UPROPERTY()
	FVector_NetQuantize Anchor = FVector_NetQuantize(0.0, 0.0, 0.0);

	UPROPERTY()
	FVector_NetQuantize10 HopVelocity = FVector_NetQuantize10(0.0, 0.0, 0.0);

	UPROPERTY()
	float DizzySeconds = 0.f;

	UPROPERTY()
	uint8 Serial = 0;
};

/**
 * Estado de las criaturas del Excel sobre una tortuga (lote #691): ralentización temporal, atrapada sin control y
 * forcejeo para soltarse machacando salto. Lo crea el servidor la primera vez que hace falta (FindOrAddTo) y se replica
 * como subobjeto del personaje; nada de vida.
 *
 * Red: el servidor decide y escribe el estado replicado. El tope de velocidad y el movimiento apagado se aplican donde
 * se simula a la tortuga (servidor y cliente dueño, como TN_SlowZoneVolume y la concha que atrapa), así que la
 * predicción no pelea con el servidor. El salto del dueño, con el forcejeo armado, va al servidor (ServerEscapePress)
 * en lugar de saltar (ATortugaCharacter::Jump).
 */
UCLASS(ClassGroup = (Custom))
class TORTUNABO_API UTN_BeachTrapStatusComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_BeachTrapStatusComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Servidor: el componente de la tortuga (lo crea y lo replica si aún no tiene). */
	static UTN_BeachTrapStatusComponent* FindOrAddTo(ACharacter* Turtle);
	static UTN_BeachTrapStatusComponent* FindOn(const AActor* Turtle);

	/** Servidor: ralentiza a SpeedFactor de la velocidad de andar durante Seconds (se queda la más fuerte de las dos si ya lo estaba). */
	void ServerSlow(float SpeedFactor, float Seconds);

	/** Servidor: la deja quieta y sin control en Anchor (By la soltará con ServerRelease). Arma el forcejeo. */
	void ServerTrap(AActor* By, const FVector& Anchor);

	/** Servidor: la suelta con un saltito HopVelocity y mareada DizzySeconds. Desarma el forcejeo. */
	void ServerRelease(const FVector& HopVelocity, float DizzySeconds);

	bool IsTrapped() const { return TrapState.bTrapped; }
	bool IsTrappedBy(const AActor* By) const { return TrapState.bTrapped && TrappedBy.Get() == By; }

	/** Servidor: el salto pasa a ser forcejeo (lo usan también los enemigos que la sujetan). Al armarlo, la cuenta empieza de cero. */
	void ServerArmEscape(bool bArmed);
	bool IsEscapeArmed() const { return bEscapeArmed; }

	/** Dueño (o servidor con la tortuga local): una pulsación de salto con el forcejeo armado. */
	void PressEscape();

	/** Servidor: ya se ha soltado machacando salto. */
	bool HasEscaped() const;

	/** Servidor: ralentizada ahora (para las pruebas y los registros). */
	bool IsSlowed() const;

	/** Nombre del tope de velocidad de las criaturas en UTN_StaminaComponent. */
	static FName SlowSource();

protected:
	UFUNCTION(Server, Reliable)
	void ServerEscapePress();

	UFUNCTION()
	void OnRep_Slow();

	UFUNCTION()
	void OnRep_Trap();

	UPROPERTY(ReplicatedUsing = OnRep_Slow)
	FTNBeachSlowState SlowState;

	UPROPERTY(ReplicatedUsing = OnRep_Trap)
	FTNBeachTrapState TrapState;

	UPROPERTY(Replicated)
	bool bEscapeArmed = false;

private:
	/** Donde se simula a la tortuga: pone el tope hasta el final replicado y programa quitarlo. */
	void ApplySlowLocal();
	void ClearSlowLocal();
	/** Donde se simula: movimiento apagado y quieta en el ancla; la local, sin teclas de mover. */
	void ApplyTrapLocal();
	/** Todas las máquinas: la suelta (saltito donde se simula) y los pajaritos del mareo. */
	void ReleaseTrapLocal();
	void EndDizzyLocal();
	/** Seguro del servidor: si quien la atrapó desaparece o pasa demasiado, la suelta. */
	void ServerTrapWatchdog();

	bool SimulatesOwner() const;
	double ServerNowSeconds() const;

	TWeakObjectPtr<AActor> TrappedBy;
	TWeakObjectPtr<APlayerController> IgnoringController;
	TNBeachCreatureRules::FMashCounter Mash;
	FTimerHandle SlowTimer;
	FTimerHandle DizzyTimer;
	FTimerHandle WatchdogTimer;
	double TrappedAt = 0.0;
	double LastPressSent = -1.0;
	uint8 AppliedTrapSerial = 0;
	bool bTrapApplied = false;
};
