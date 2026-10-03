#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "TN_CarryComponent.generated.h"

class ATortugaCharacter;
class USoundBase;

/**
 * @brief Coger y lanzar a otra tortuga (fase 2 del issue #6).
 *
 * Reglas:
 *  - Solo se puede coger a una tortuga metida en su caparazón o aturdida (en ese
 *    caso se mete en el caparazón al cogerla). Vale cualquiera, también rivales.
 *  - La llevada no controla su movimiento. Si intenta moverse de forma continuada
 *    SecondsToEscape segundos, se libera. Mientras forcejea, al portador le tiembla
 *    la cámara y su lanzamiento pierde mucha fuerza.
 *  - Lanzamiento como un saque de banda: con la E, las dos aletas toman impulso detrás
 *    de la cabeza (ThrowWindupSeconds) y la sueltan hacia delante, hacia donde apunta la
 *    cámara con un arco bajo (ATortugaCharacter::GetThrowDirection, ~25°). Saltando y
 *    haciendo el panzazo con ella en alto, el panzazo la lanza con su impulso (el de la
 *    carrera y el del salto sumados al del lanzamiento: ThrowWithDive). La lanzada sale
 *    volando como caparazón con física propia (ATN_ShellBody): da volteretas, rebota y
 *    rueda y no puede salir hasta que la caja se para; entonces sale sola y se pone de pie.
 *    Soltada, cae como caparazón y sale cuando quiera.
 *  - Derribada en brazos (el lanzable de un tercero, la piel de plátano, el DBNO), quien
 *    la lleva la suelta y cae derribada como en el suelo (TNCarryRules, #68). Aturdida
 *    (la bola de la carrera), sigue en sus brazos.
 *
 * Red: estado server-authoritative. CarriedTurtle (en el portador) y CarriedBy (en
 * el llevado) replican y cada máquina aplica localmente el enganche. El impulso
 * del lanzamiento se aplica en el servidor y por Client RPC en el dueño del
 * lanzado, para que su predicción no pelee con la corrección.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_CarryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_CarryComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ── Input (cliente dueño) ───────────────────────────────────────────────

	/** Busca una tortuga cogible delante y pide cogerla. true si había candidata. */
	bool TryGrabNearest();

	/** Lanza a la tortuga que lleva, hacia donde apunta la cámara. */
	void RequestThrow();

	/** Deja a la tortuga en el suelo sin lanzarla. */
	void RequestDrop();

	/** El llevado intenta moverse (forcejeo). Solo envía cambios de estado. */
	void SetStruggleInput(bool bInStruggling);

	// ── Estado ──────────────────────────────────────────────────────────────

	UFUNCTION(BlueprintPure, Category = "Carry")
	bool IsCarrying() const { return CarriedTurtle != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Carry")
	bool IsBeingCarried() const { return CarriedBy != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Carry")
	bool IsCarriedStruggling() const { return bCarriedStruggling; }

	ATortugaCharacter* GetCarriedTurtle() const { return CarriedTurtle; }
	ATortugaCharacter* GetCarrier() const { return CarriedBy; }

	/** Lo llama el personaje al aterrizar (servidor y cliente dueño). */
	void NotifyLanded();

	/** Lo llama el personaje al entrar en el agua durante el vuelo. */
	void NotifyEnteredWater();

	bool IsAwaitingBounce() const { return bAwaitingBounce; }

	/** Servidor: suelta a quien lleve sin lanzarlo (muerte, derribo, escape). */
	void ForceRelease(bool bEscapeHop);

	/**
	 * Servidor, al empezar el panzazo llevando a alguien en alto: lo lanza hacia DiveDir con el arco de siempre y, sumado,
	 * parte del impulso del panzazo (DiveVelocity, que ya lleva la carrera) y de la velocidad hacia arriba del salto
	 * (CarrierVelocity, la de antes de lanzarse).
	 */
	void ThrowWithDive(const FVector& DiveDir, const FVector& DiveVelocity, const FVector& CarrierVelocity);

	/**
	 * Toma de impulso del lanzamiento con la E (cosmético, en cada máquina): de 0 a 1 mientras echa las aletas detrás de
	 * la cabeza y vuelve a subirlas para soltar; -1 si no está tomando impulso. Lo lee UTN_TurtleAnimInstance.
	 */
	float GetThrowWindupAlpha() const;

protected:
	/** Alcance para coger (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Carry", meta = (ClampMin = "50.0"))
	float GrabRange = 240.f;

	/** Velocidad del lanzamiento (cm/s). */
	UPROPERTY(EditDefaultsOnly, Category = "Carry", meta = (ClampMin = "100.0"))
	float ThrowSpeed = 1500.f;

	/** Multiplicador de fuerza si el llevado está forcejeando. */
	UPROPERTY(EditDefaultsOnly, Category = "Carry", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StruggleThrowMultiplier = 0.45f;

	// El ángulo del lanzamiento es el de todos los lanzamientos: ATortugaCharacter::GetThrowDirection (Throwable).

	/**
	 * Segundos que tarda en soltarla tras pulsar la E: las dos aletas se echan detrás de la cabeza y vuelven a subir, como
	 * en un saque de banda (0 = la suelta al momento, sin tomar impulso).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Carry|Throw", meta = (ClampMin = "0.0", ClampMax = "0.6"))
	float ThrowWindupSeconds = 0.18f;

	/** Lanzada con el panzazo: parte del impulso horizontal del panzazo (con la carrera dentro) que se lleva la lanzada. */
	UPROPERTY(EditDefaultsOnly, Category = "Carry|Throw", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float DiveThrowCarryFactor = 0.6f;

	/** Lanzada con el panzazo: parte de la velocidad hacia arriba del salto que se lleva la lanzada. */
	UPROPERTY(EditDefaultsOnly, Category = "Carry|Throw", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float DiveThrowJumpFactor = 0.5f;

	/** Tope de la velocidad de la lanzada con el panzazo (cm/s). */
	UPROPERTY(EditDefaultsOnly, Category = "Carry|Throw", meta = (ClampMin = "100.0"))
	float DiveThrowMaxSpeed = 2300.f;

	/** Segundos de forcejeo continuo para liberarse. */
	UPROPERTY(EditDefaultsOnly, Category = "Carry", meta = (ClampMin = "0.1"))
	float SecondsToEscape = 2.f;

	/** Velocidad máxima del portador mientras lleva a alguien. */
	UPROPERTY(EditDefaultsOnly, Category = "Carry", meta = (ClampMin = "0.0"))
	float CarrySpeedCap = 330.f;

	/** Altura sobre el portador a la que va la llevada (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Carry")
	float CarryHeight = 120.f;

	/** Velocidad vertical del rebote al caer tras un lanzamiento. */
	UPROPERTY(EditDefaultsOnly, Category = "Carry", meta = (ClampMin = "0.0"))
	float BounceVelocity = 560.f;

	/** Intensidad del temblor de cámara del portador (grados). */
	UPROPERTY(EditDefaultsOnly, Category = "Carry", meta = (ClampMin = "0.0"))
	float StruggleShakeDegrees = 2.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Carry|Audio")
	TObjectPtr<USoundBase> GrabSound;

	UPROPERTY(EditDefaultsOnly, Category = "Carry|Audio")
	TObjectPtr<USoundBase> ThrowSound;

private:
	UPROPERTY(ReplicatedUsing = OnRep_CarriedTurtle)
	TObjectPtr<ATortugaCharacter> CarriedTurtle;

	UPROPERTY(ReplicatedUsing = OnRep_CarriedBy)
	TObjectPtr<ATortugaCharacter> CarriedBy;

	/** En el portador: su carga forcejea (temblor de cámara local). */
	UPROPERTY(Replicated)
	bool bCarriedStruggling = false;

	/**
	 * Número de la última toma de impulso del lanzamiento con la E (lo sube el servidor; 0 = ninguna). Las demás
	 * máquinas empiezan con él la animación; el dueño ya la empezó al pulsar.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ThrowWindupSerial)
	uint8 ThrowWindupSerial = 0;

	UFUNCTION()
	void OnRep_ThrowWindupSerial();

	/** Servidor: lanza ya a la que lleva hacia AimRotation (con la E, al acabar de tomar impulso). */
	void ThrowCarried(const FRotator& AimRotation);

	/** Servidor: acabada la toma de impulso, la suelta. */
	void FinishThrowWindup();

	/** Servidor: olvida la toma de impulso pendiente (ya se ha soltado por otra cosa). */
	void CancelThrowWindup();

	/** Cosmético, en cada máquina: empieza la animación de tomar impulso (si no estaba ya). */
	void BeginLocalThrowWindup();

	/** Servidor: toma de impulso en curso, hacia dónde apuntaba y cuándo acaba. */
	bool bThrowWindupPending = false;
	FRotator PendingThrowAim = FRotator::ZeroRotator;
	FTimerHandle ThrowWindupTimer;

	/** Cosmético: cuándo empezó en esta máquina la toma de impulso (tiempo del mundo; negativo = ninguna). */
	double LocalWindupStart = -1.0;

	UFUNCTION(Server, Reliable)
	void ServerGrab(ATortugaCharacter* Target);

	UFUNCTION(Server, Reliable)
	void ServerThrow(FRotator AimRotation);

	UFUNCTION(Server, Reliable)
	void ServerDrop();

	/**
	 * Forcejeo de la llevada. Fiable: solo se manda al cambiar (SetStruggleInput), así que uno perdido dejaba al servidor
	 * con el estado viejo para siempre.
	 */
	UFUNCTION(Server, Reliable)
	void ServerSetStruggling(bool bInStruggling);

	/** En el dueño del lanzado: mismo impulso que en el servidor. */
	UFUNCTION(Client, Reliable)
	void ClientApplyThrow(FVector StartLocation, FVector Velocity, bool bBounce);

	UFUNCTION()
	void OnRep_CarriedTurtle();

	UFUNCTION()
	void OnRep_CarriedBy();

	ATortugaCharacter* GetTurtle() const;
	bool CanBeGrabbed(const ATortugaCharacter* Target) const;
	void ApplyCarrierLocalState(bool bCarrying);
	void ApplyCarriedLocalState(ATortugaCharacter* Carrier);
	void Release(ATortugaCharacter* Carried, const FVector& Location, const FVector& Velocity, bool bThrown, bool bExitOnRest);

	/**
	 * Dónde queda la llevada al soltarla: barrido de su cápsula desde donde va (encima de quien la lleva) hasta Target; si
	 * choca con algo, donde choca. Sin él, lanzarla de cara a un muro de menos de ~70 cm la dejaba al otro lado (#69).
	 */
	FVector SweepReleaseLocation(const ATortugaCharacter* Carried, const FVector& Target) const;
	void RestoreCollisionWith(ATortugaCharacter* Other);

	/** Servidor, en el llevado: forcejeo actual y tiempo acumulado. */
	bool bStruggling = false;
	float StruggleTime = 0.f;
	bool bLocalStruggleSent = false;

	/** En el lanzado: rebote pendiente al tocar suelo (servidor y dueño). */
	bool bAwaitingBounce = false;

	/** Portador que acaba de soltarle (para dejar de ignorar su colisión). */
	TWeakObjectPtr<ATortugaCharacter> LastCarrier;
	bool bCarrierStateApplied = false;
	bool bCarriedStateApplied = false;
	float ShakeTime = 0.f;
};
