#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "TN_VRSeatComponent.generated.h"

class UCameraComponent;

/** Una mano VR tal como la ve un asiento (en mundo): la que le da ATN_VRRig cada fotograma. */
struct FTNVRSeatHand
{
	FVector Location = FVector::ZeroVector;
	/** Hacia dónde apunta la mano (la pose de apuntar del mando). */
	FVector AimDir = FVector::ForwardVector;
	/** Agarre del mando (0..1). */
	float Grip = 0.f;
	bool bTracked = false;
};

/**
 * Asiento de un vehículo para jugar con gafas (Docs/Modo_VR.md, «Vehículos»). ATN_Buggy (conductora) y
 * ATN_BuggyGunnerPawn (artillera) llevan uno en los ojos de la tortuga sentada; las subclases (el Rally y los karts) lo
 * heredan.
 *
 * - Es el origen del seguimiento: con gafas, ATN_VRRig se engancha aquí y la cabeza mueve la cámara dentro de él. Al
 *   sentarse con gafas se recentra: la cabeza queda en los ojos de la tortuga, mirando al morro.
 * - En la máquina del dueño, su cámara sustituye a la del vehículo (sin brazo, retardo, temblor ni FOV dinámico) mientras
 *   el rig tenga la vista sentada encendida. Sin gafas (simulado), la cámara mira hacia SetSimulatedLook.
 * - El rig le da las manos cada fotograma; el vehículo decide qué hacen (volante, asas) y dónde se ven (SetDisplayHands),
 *   y el asiento las manda al servidor (sin fiabilidad, unas 15 veces por segundo) para que los demás vean los brazos de
 *   la tortuga sentada (UTN_BuggyRiderAnimComponent).
 *
 * Sin gafas no hace nada: la vista sentada solo la enciende el rig.
 */
UCLASS(ClassGroup = (VR), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_VRSeatComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UTN_VRSeatComponent();

	/** El asiento VR de un actor (o nullptr). */
	static UTN_VRSeatComponent* FindOn(const AActor* Actor);

	// ── Vista (solo en la máquina del dueño, desde ATN_VRRig) ──────────────────

	/** Enciende o apaga la vista sentada (con gafas o simulada). Sin ser el dueño local, no hace nada. */
	void SetVRView(bool bOn, bool bHeadset);

	bool IsVRView() const { return bVRView; }
	bool IsHeadsetView() const { return bVRView && bHeadsetView; }
	UCameraComponent* GetVRCamera() const { return VRCamera; }

	/** Cabeza respecto del asiento (cm, ejes del asiento): tras recentrar, en el origen. */
	FVector GetHeadLocal() const;

	/** Giro de la cabeza respecto del asiento. */
	FRotator GetHeadRelativeRotation() const;

	/** Sin gafas: hacia dónde mira la cámara sentada respecto del asiento (el ratón o el apuntado, lo pone el vehículo). */
	void SetSimulatedLook(const FRotator& Relative);

	// ── Manos ──────────────────────────────────────────────────────────────────

	/** El rig: las manos de este fotograma (en mundo). Con una pose de pruebas (SetDebugPose) se usa esa. */
	void SetLocalHands(const FTNVRSeatHand& Left, const FTNVRSeatHand& Right);

	/** Mano 0 (izquierda) o 1 (derecha) del último fotograma. */
	const FTNVRSeatHand& GetHand(int32 Index) const;

	/**
	 * Agarre de una mano con histéresis (TNVRMath::AnalogButton): +1 al cerrarla, -1 al abrirla, 0 si no cambia. Lo llama
	 * el vehículo una vez por fotograma y mano; sin la vista sentada, la mano cuenta como abierta.
	 */
	int32 StepGrip(int32 Index);

	/** Dónde se ven las manos de la tortuga sentada (en mundo): en el volante, en las asas o en los mandos. */
	void SetDisplayHands(const FVector& LeftWorld, const FVector& RightWorld, bool bLeft, bool bRight);

	/** Las manos que se ven: en el dueño, las de SetDisplayHands; en el resto, las que llegan del servidor. */
	bool GetDisplayHands(FVector& OutLeft, FVector& OutRight, bool& bOutLeft, bool& bOutRight) const;

	/** Si quien ocupa el asiento va con gafas (o simulado): en el dueño, su vista; en el resto, lo replicado. */
	bool IsVROccupied() const;

	/** Servidor: cambia el ocupante (vuelve a «sin gafas» hasta que el nuevo diga lo contrario). */
	void ResetOccupant();

	// ── Pruebas ────────────────────────────────────────────────────────────────

	/** Manos de pruebas en los ejes del asiento: posición, hacia dónde apuntan y agarre, según los segundos que lleva. */
	using FDebugPose = TFunction<void(float Seconds, FTNVRSeatHand& Left, FTNVRSeatHand& Right)>;

	/** Las manos salen de Pose durante Duration segundos en lugar de los mandos (TN.VR.SeatPose). */
	void SetDebugPose(FDebugPose Pose, float Duration);
	bool HasDebugPose() const;

	/** Ojos de la tortuga sentada sobre la cadera (cm, ejes del vehículo): donde va el asiento. */
	static const FVector EyeAboveHip;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool IsLocalOwner() const;
	void EnsureCamera();
	/** Manos a los mandos para el servidor (como mucho SendRate veces por segundo). */
	void SendHands();

	UFUNCTION(Server, Reliable)
	void ServerSetVROccupied(bool bOn);

	UFUNCTION(Server, Unreliable)
	void ServerSetHands(FVector_NetQuantize10 Left, FVector_NetQuantize10 Right, uint8 Mask);

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> VRCamera;

	/** Cámaras del vehículo que se apagaron al encender la vista sentada (se vuelven a encender al apagarla). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCameraComponent>> CamerasTurnedOff;

	/** Va con gafas (o simulado) quien ocupa el asiento. */
	UPROPERTY(Replicated)
	bool bRepVROccupied = false;

	/** Manos que se ven, respecto del asiento (cm); el dueño no las recibe. */
	UPROPERTY(Replicated)
	FVector_NetQuantize10 RepHandLeft;

	UPROPERTY(Replicated)
	FVector_NetQuantize10 RepHandRight;

	UPROPERTY(Replicated)
	uint8 RepHandMask = 0;

	FTNVRSeatHand Hands[2];
	bool bGripHeld[2] = { false, false };
	FVector DisplayHand[2] = { FVector::ZeroVector, FVector::ZeroVector };
	bool bDisplayHand[2] = { false, false };
	bool bVRView = false;
	bool bHeadsetView = false;
	double LastHandsSent = -1.0;
	FRotator SimulatedLook = FRotator::ZeroRotator;

	FDebugPose DebugPose;
	double DebugPoseStart = 0.0;
	float DebugPoseDuration = 0.f;
};
