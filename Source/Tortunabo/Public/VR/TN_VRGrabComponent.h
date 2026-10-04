#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "TN_VRGrabComponent.generated.h"

class UPhysicsHandleComponent;
class UPrimitiveComponent;
class UWorld;
struct FCollisionQueryParams;

/**
 * Coger objetos con física con las aletas en VR (Docs/Modo_VR.md): el agarre de cada mando coge el objeto con física más
 * cercano a la punta de la aleta, lo lleva pegado a la mano (UPhysicsHandleComponent: sigue chocando con lo demás) y, al
 * soltar, sale con la velocidad de la mano.
 *
 * Red: si el actor se replica, lo mueve el servidor (el dueño manda la mano unas 30 veces por segundo) y los demás lo
 * ven con su réplica de siempre; si no se replica (decorado con física local), se coge solo en la propia máquina. En un
 * cliente, lo replicado no simula física (ATN_PhysicsObjectActor solo simula en el servidor): se elige por clase
 * (ATN_PhysicsObjectActor sin bUseKinematicPush) o por la etiqueta VRGrab, y el servidor lo acepta o lo rechaza
 * (ClientGrabRejected). Mientras se lleva, el servidor lo tiene despierto en red; al soltarlo vuelve a dormirse cuando se
 * para. Nunca coge tortugas, enemigos ni caparazones (esos tienen sus propias reglas), lo que lleva la etiqueta NoVRGrab,
 * un actor replicado que no replica su movimiento ni nada de más de MaxMass kg.
 *
 * Robustez (servidor y dueño): lo que lleva otra tortuga no se coge (un objeto, un dueño); una tortuga derribada, muerta,
 * en el caparazón o llevada no coge y suelta lo que llevaba; lo cogido que se queda enganchado lejos de la mano (una pared,
 * algo que lo sujeta) se suelta solo (TNVRHands::ShouldBreakGrab); la cápsula del dueño no choca con lo que lleva (no se
 * sube encima ni se empuja con ello) y lo cogido usa CCD (no atraviesa paredes finas al lanzarlo). Solo se coge lo que se
 * ve (CanReach): el dueño lo mira desde la cámara VR y el servidor, al aceptarlo, desde los ojos del peón directamente o
 * pasando por la mano que le llega (los ojos de verdad pueden estar algo apartados de la cápsula).
 *
 * Lo usa ATN_VRRig; vive en la tortuga para que sus RPC vayan por la conexión de su dueño.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_VRGrabComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_VRGrabComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Dueño: coge lo más cercano a la mano Hand (0 izquierda, 1 derecha). true si ha cogido algo. */
	bool TryGrab(int32 Hand, const FTransform& HandWorld);

	/** Dueño, cada fotograma mientras se mantiene el agarre. */
	void UpdateGrab(int32 Hand, const FTransform& HandWorld);

	/** Dueño: suelta con la velocidad de la mano. */
	void Release(int32 Hand, const FVector& HandVelocity);

	bool IsGrabbing(int32 Hand) const;

	/** Lo que lleva esa mano (nullptr si nada). */
	UPrimitiveComponent* GetHeld(int32 Hand) const;

	/**
	 * Dueño: cuánto (cm) se ha separado lo cogido de donde debería estar en la mano (para la vibración). 0 si no lleva nada
	 * o si lo mueve el servidor (lo que se ve aquí llega con el retraso de la red).
	 */
	float GetStrain(int32 Hand, const FTransform& HandWorld) const;

	/** ¿Puede coger con las manos? Una tortuga en VR que no esté derribada, muerta, en el caparazón ni llevada. Otro actor, sí. */
	static bool CanOwnerGrab(const AActor* Owner);

	/** Quién lleva ahora ese componente en esta máquina (nullptr si nadie). */
	static UTN_VRGrabComponent* FindHolder(const UPrimitiveComponent* Component);

	/** Entradas del registro de quién lleva cada objeto en esta máquina (para las pruebas). */
	static int32 NumHolderEntries();

	/**
	 * ¿Se puede coger con la mano aquí, donde simula? Con física, móvil, sin dueño pawn ni caparazón, sin NoVRGrab, con su
	 * movimiento replicado si el actor se replica y de hasta MaxMass kg. Lo usa el servidor para aceptar un agarre.
	 */
	static bool IsGrabbable(const UPrimitiveComponent* Component, const AActor* ByActor, float MaxMassKg);

	/**
	 * En un cliente, un actor replicado que mueve el servidor (aquí no simula): ATN_PhysicsObjectActor sin bUseKinematicPush
	 * (su malla), o con física aquí o con la etiqueta VRGrab; con las mismas exclusiones que IsGrabbable. Lo valida el servidor.
	 */
	static bool IsGrabbableFromClient(UPrimitiveComponent* Component, const AActor* ByActor, float MaxMassKg);

	/**
	 * ¿Nada del escenario (canal ECC_Camera, el mismo que para las manos) entre los ojos y Point? Lo que se corta a menos de
	 * ReachTolerance de Point cuenta como despejado (el suelo bajo lo que se toca por abajo); con los ojos ya dentro de algo
	 * también, como las manos (ATN_VRRig::BlockHandLocation), salvo con bPenetratingBlocks (el servidor: con la cabeza metida
	 * en una pared no se coge lo de detrás). Params dice qué no cuenta (la tortuga, el propio objetivo).
	 */
	static bool HasClearReach(const UWorld* World, const FVector& Eyes, const FVector& Point, const FCollisionQueryParams& Params,
		bool bPenetratingBlocks = false);

	/**
	 * ¿Llega la mano al punto Point de Target sin atravesar el escenario? Los ojos (la cámara VR de la tortuga, o el punto de
	 * vista del actor) ven Point sin una pared en medio: la mano se para en la pared (TN_VRRigHands.cpp) y lo que se busca
	 * alrededor de su punta (GrabRadius, VRHandReach) no puede quedar al otro lado. No cuentan la tortuga, lo que lleva
	 * encima o en las manos ni Target.
	 */
	bool CanReach(const AActor* Target, const FVector& Point, bool bPenetratingBlocks = false) const;

	/** Tolerancia (cm) de HasClearReach al final del trazo. */
	static constexpr float ReachTolerance = 3.f;

	/** Radio (cm) alrededor de la punta de la aleta en el que se busca qué coger. */
	UPROPERTY(EditAnywhere, Category = "VR|Grab", meta = (ClampMin = "5.0"))
	float GrabRadius = 22.f;

	/** Masa máxima (kg) de lo que se puede coger. */
	UPROPERTY(EditAnywhere, Category = "VR|Grab", meta = (ClampMin = "1.0"))
	float MaxMass = 250.f;

	/** Distancia máxima (cm) entre la tortuga y lo que coge, para aceptarlo en el servidor. */
	UPROPERTY(EditAnywhere, Category = "VR|Grab", meta = (ClampMin = "50.0"))
	float MaxServerReach = 350.f;

private:
	UFUNCTION(Server, Reliable)
	void ServerGrab(uint8 Hand, UPrimitiveComponent* Target, FVector_NetQuantize10 HandLocation, FRotator HandRotation);

	UFUNCTION(Server, Unreliable)
	void ServerMoveGrab(uint8 Hand, FVector_NetQuantize10 HandLocation, FRotator HandRotation);

	UFUNCTION(Server, Reliable)
	void ServerRelease(uint8 Hand, FVector_NetQuantize10 Velocity);

	/**
	 * El servidor no ha aceptado el agarre (no se puede coger, está lejos o lo lleva otro) o lo ha soltado él (enganchado,
	 * derribo): el dueño deja de llevarlo.
	 */
	UFUNCTION(Client, Reliable)
	void ClientGrabLost(uint8 Hand, UPrimitiveComponent* Target);

	UPrimitiveComponent* FindGrabbable(const FVector& At) const;
	/** IsGrabbable o, si lo mueve el servidor y esta máquina no lo es, IsGrabbableFromClient. */
	bool IsGrabbableHere(UPrimitiveComponent* Component) const;
	/** Desde dónde mira la tortuga (su cámara VR) o, sin ella, el punto de vista del actor. */
	FVector GetReachEyes() const;
	/** Lo que no tapa a Target al cogerlo o tocarlo: la tortuga, lo que lleva encima o en las manos y el propio Target. */
	FCollisionQueryParams MakeReachParams(const AActor* Target) const;
	/** Servidor de un actor replicado: despierto en red mientras lo lleve esta mano (ReleaseHere lo deja volver a dormirse). */
	void KeepAwakeWhileHeld(int32 Hand, AActor* Target);
	/** Coge en esta máquina (servidor, o local si el actor no se replica). */
	bool GrabHere(int32 Hand, UPrimitiveComponent* Target, const FTransform& HandWorld);
	void MoveHere(int32 Hand, const FTransform& HandWorld);
	void ReleaseHere(int32 Hand, const FVector& Velocity);
	/** Servidor: suelta y, si el dueño es un cliente, se lo dice. */
	void DropOnServer(int32 Hand);
	UPhysicsHandleComponent* GetHandle(int32 Hand);
	/** Dónde se cogió (respecto de la mano y del objeto) y que aún no va enganchado. */
	void RememberGrabPoint(int32 Hand, UPrimitiveComponent* Target, const FTransform& HandWorld);
	/** La cápsula del dueño deja de chocar (o vuelve a chocar) con lo que lleva en esta máquina. */
	void SetOwnerIgnores(UPrimitiveComponent* Target, bool bIgnore) const;
	/** Separación (cm) entre el punto por el que se cogió y donde debería estar con la mano en HandWorld. */
	float SeparationFromHand(int32 Hand, const FTransform& HandWorld) const;
	bool IsOwnerLocal() const;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicsHandleComponent> LeftHandle;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicsHandleComponent> RightHandle;

	/** Lo cogido por cada mano, dónde respecto a la mano y si lo mueve el servidor. */
	TWeakObjectPtr<UPrimitiveComponent> Held[2];
	FTransform HeldFromHand[2];
	/** El punto por el que se cogió, en el espacio del objeto, y desde cuándo va enganchado (-1 si no lo va). */
	FVector GrabPointLocal[2] = { FVector::ZeroVector, FVector::ZeroVector };
	double StrainSince[2] = { -1.0, -1.0 };
	bool bHeldByServer[2] = { false, false };
	double LastMoveSent[2] = { -1.0, -1.0 };
	/** Servidor: el objeto con física que cada mano tiene despierto en red (ATN_PhysicsObjectActor::SetExternallyHeld). */
	TWeakObjectPtr<AActor> AwakeActor[2];
};
