// Kart de los karts del mapa del cooperativo: el buggy de SkiTemplar (ATN_Buggy, sin tocarlo) con lo que es solo de los
// karts. Objetos (#304, UTN_KartItemComponent): los usa la artillera si la hay y si no la conductora. Artillera (#295,
// ATN_KartGunnerPawn): su inclinación cambia cuánto gira el kart. Conductora sola (#295): mira alrededor con el ratón o el
// stick derecho y la torreta sigue a la cámara (dispara hacia donde mira, con un poco de ayuda al apuntar). Géiseres,
// cascadas y agua (#293, UTN_KartTraversalComponent): sube en géiser, baja por la cascada y flota como una balsa.
//
// Red: la inclinación de la artillera y el apuntado de la conductora sola llegan por RPC validada y se replican; la
// inclinación la aplican el servidor y la conductora local, que simulan el chasis.
#pragma once

#include "CoreMinimal.h"
#include "Vehicles/TN_Buggy.h"
#include "TN_KartBuggy.generated.h"

class UTN_KartInputSet;
class UTN_KartItemComponent;
class UTN_KartTraversalComponent;
class USpringArmComponent;
struct FInputActionValue;

namespace TNKart
{
	/** Cuánto cambia el giro máximo de las ruedas delanteras con la artillera inclinada a tope (fracción). */
	inline constexpr float LeanSteerGain = 0.35f;

	/**
	 * Multiplicador del giro máximo de las ruedas con la inclinación Lean (-1..1) y la dirección Steer (-1..1): inclinarse
	 * hacia dentro de la curva cierra el giro (hasta 1 + LeanSteerGain) y hacia fuera lo abre (hasta 1 - LeanSteerGain);
	 * sin girar, nada.
	 */
	TORTUNABO_API float LeanSteerMultiplier(float Lean, float Steer);

	/** Lleva el desplazamiento de la cámara hacia el centro: RecenterDegPerSecond, sin pasarse. */
	TORTUNABO_API float RecenterLook(float Degrees, float RecenterDegPerSecond, float DeltaSeconds);
}

UCLASS()
class TORTUNABO_API ATN_KartBuggy : public ATN_Buggy
{
	GENERATED_BODY()

public:
	ATN_KartBuggy();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Reaparición y hueco de la parrilla: el sitio que pide la carrera, pero a la altura del suelo que hay debajo más lo que
	 * el kart levanta sobre sus ruedas (medido en marcha). La carrera del Rally la calcula con una medida del buggy que en
	 * el mapa del cooperativo sale a 0 y dejaba el kart medio enterrado.
	 */
	virtual void RallyTeleport(const FTransform& Where, float LockSeconds, float GhostSeconds) override;

	UTN_KartItemComponent* GetItems() const { return Items; }
	UTN_KartTraversalComponent* GetTraversal() const { return Traversal; }

	/** Inclinación de la artillera en [-1, 1] (replicada). */
	UFUNCTION(BlueprintPure, Category = "Karts")
	float GetGunnerLean() const { return static_cast<float>(GunnerLeanQ) / 100.f; }

	/** Solo servidor: la pone la artillera (ATN_KartGunnerPawn). */
	void SetGunnerLean(float Lean);

	/**
	 * Quién usa los objetos: la artillera si hay una tortuga en esa plaza; si no, la conductora. Controller es quien lo pide;
	 * false si no le toca.
	 */
	bool MayUseItems(const AController* Requester) const;

	/** Conductora local: lo que se ha girado la cámara (guiñada y cabeceo, grados). */
	FRotator GetLookOffset() const { return FRotator(LookPitch, LookYaw, 0.f); }

	/** Velocidad del ratón y del stick al mirar alrededor y la vuelta al centro al soltarlos. */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Cámara")
	float LookMouseDegreesPerUnit = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "Karts|Cámara")
	float LookStickDegreesPerSecond = 160.f;

	UPROPERTY(EditDefaultsOnly, Category = "Karts|Cámara")
	float LookRecenterDelaySeconds = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Karts|Cámara")
	float LookRecenterDegPerSecond = 140.f;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UTN_KartItemComponent> Items;

	/** Géiseres, cascadas y agua (#293). */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UTN_KartTraversalComponent> Traversal;

private:
	UTN_KartInputSet* GetKartInput();
	void OnUseItem(const FInputActionValue& Value);
	void OnBackwardPressed(const FInputActionValue& Value);
	void OnBackwardReleased(const FInputActionValue& Value);
	void OnLookMouse(const FInputActionValue& Value);
	void OnLookStick(const FInputActionValue& Value);
	void AddLook(float DeltaYaw, float DeltaPitch);
	void OnFire(const FInputActionValue& Value);

	/** Conductora local: cámara girada por la entrada (y vuelta al centro) y apuntado de la torreta si va sola. */
	void UpdateLook(float DeltaSeconds);
	/** Servidor y conductora local: giro máximo de las ruedas delanteras según la inclinación de la artillera. */
	void ApplyLeanSteering();
	/** Servidor: con las cuatro ruedas en el suelo y casi parado, lo que el origen del kart levanta sobre el suelo. */
	void MeasureRideHeight();
	/** Quita la colisión a los cuerpos del chasis que no simulan (los de las ruedas del PhysicsAsset): se quedan atrás. */
	void DisableDetachedBodies();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerUseItem(bool bBackward);

	/** Conductora sola: apuntado de la torreta relativo al kart (lo que mira la cámara). */
	UFUNCTION(Server, Unreliable, WithValidation)
	void ServerSetSoloAim(float Yaw, float Pitch);

	/** Conductora sola: coco hacia Dir (lo que mira su cámara); el servidor lo acepta si no se separa mucho de la torreta. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSoloFire(FVector_NetQuantizeNormal Dir);

	/** Dirección en mundo hacia la que mira la cámara de la conductora local. */
	FVector GetLookWorldDirection() const;

	UPROPERTY(Transient)
	TObjectPtr<UTN_KartInputSet> KartInput;

	UPROPERTY(Transient)
	TObjectPtr<USpringArmComponent> CameraArm;

	/** Inclinación de la artillera en centésimas (-100..100). */
	UPROPERTY(Replicated)
	int8 GunnerLeanQ = 0;

	float LookYaw = 0.f;
	float LookPitch = 0.f;
	float LookIdleSeconds = 0.f;
	bool bBackwardHeld = false;
	FRotator CameraArmBaseRotation = FRotator::ZeroRotator;
	float AimSendAccumulator = 0.f;
	double LastFireRequest = -1000.0;
	FRotator LastSentAim = FRotator(1000.f, 0.f, 0.f);
	/** Multiplicador del giro máximo (UTN_BuggyData::MaxSteerAngleDeg) puesto en las ruedas delanteras. */
	float AppliedLeanSteer = 1.f;
	/** Altura del origen sobre el suelo con el kart apoyado (cm); hasta medirla, la de reserva del Rally más un margen. */
	float RideHeightCm = 90.f;
	bool bRideHeightMeasured = false;
};
