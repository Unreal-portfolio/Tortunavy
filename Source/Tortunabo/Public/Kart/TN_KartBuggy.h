// Kart de los karts del mapa del cooperativo: el buggy de SkiTemplar (ATN_Buggy, sin tocarlo) con lo que es solo de los
// karts. Objetos (#304, UTN_KartItemComponent): los usa la artillera si la hay y si no la conductora. Artillera (#295,
// ATN_KartGunnerPawn): su inclinación cambia cuánto gira el kart. Conductora sola (#295): mira alrededor con el ratón o el
// stick derecho y la torreta sigue a la cámara (dispara hacia donde mira, con un poco de ayuda al apuntar). Géiseres,
// cascadas y agua (#293, UTN_KartTraversalComponent): sube en géiser, baja por la cascada y flota como una balsa.
//
// Red: la inclinación de la artillera y el apuntado de la conductora sola llegan por RPC validada y se replican; la
// inclinación la aplican el servidor y la conductora local, que simulan el chasis.
//
// Conducción de los karts (#742): más punta y aceleración que el buggy del Rally, dirección que se cierra a mucha velocidad
// y derrape con el freno de mano que, al soltarlo, da un mini-turbo (ATN_Buggy::GrantTimedBoost) según lo que haya durado.
// Solo en Karts: el Rally de LVL_Rally (ATN_RallyKartBuggy) conduce como siempre. TN.Kart.Tuning 0 lo apaga para comparar.
#pragma once

#include "CoreMinimal.h"
#include "Vehicles/TN_Buggy.h"
#include "TN_KartBuggy.generated.h"

class UTN_BuggyData;
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

	// ── Conducción de los karts (#742) ──────────────────────────────────────────

	/** Cuánto más rápido que el buggy del Rally: punta, par y empuje del turbo (1,3 = un 30 % más). */
	inline constexpr float DefaultSpeedScale = 1.3f;
	/** Multiplicador del par de la parte alta de la curva (UTN_BuggyData::TopEndTorqueScale) que lleva la punta a SpeedScale. */
	inline constexpr float DefaultTopEndTorqueScale = 1.7f;

	/**
	 * Pone en Data (con los valores de serie de UTN_BuggyData) los de los karts: MaxRPM, par y par de la parte alta por
	 * SpeedScale; la punta, el empuje y la rampa del turbo siguiendo a la del kart; el antivuelco que deja de corregir el
	 * alabeo a la nueva velocidad; y el freno de mano con más agarre trasero y contravolante más tardío (derrape que se
	 * controla).
	 */
	TORTUNABO_API void ApplyKartTuning(UTN_BuggyData& Data, float SpeedScale = DefaultSpeedScale, float TopEndTorqueScale = DefaultTopEndTorqueScale);

	/**
	 * Dirección según la velocidad (solo la conductora humana: la IA ya limita su giro por aceleración lateral). El buggy
	 * tiene tanto agarre que a 90 km/h un 10 % del volante ya da más de 1 g: el ángulo de las ruedas baja con la velocidad
	 * (1 / (1 + (v / SteerHalfSpeedCms)^SteerFallExponent)) hasta MinSteerFraction, para
	 * que el volante sea progresivo y no solo «nada» o «derrapar».
	 */
	inline constexpr float SteerHalfSpeedCms = 1600.f;
	inline constexpr float SteerFallExponent = 2.5f;
	inline constexpr float MinSteerFraction = 0.1f;

	/** Fracción del giro máximo de las ruedas a esta velocidad de avance: 1 parado, la mitad a SteerHalfSpeedCms, nunca menos de MinSteerFraction. */
	TORTUNABO_API float SpeedSteerMultiplier(float ForwardSpeedCms);

	/**
	 * Derrape que da mini-turbo: velocidad de avance mínima (cm/s), dirección mínima (0..1) o deriva mínima (grados: también
	 * cuenta contravolantear un derrape) y tramos (s de derrape) con su turbo (s).
	 */
	inline constexpr float DriftMinSpeedCms = 600.f;
	inline constexpr float DriftMinSteer = 0.25f;
	inline constexpr float DriftMinSlipDeg = 15.f;
	inline constexpr float DriftTier1Seconds = 0.7f;
	inline constexpr float DriftTier2Seconds = 1.4f;
	inline constexpr float DriftTier3Seconds = 2.2f;
	inline constexpr float DriftBoost1Seconds = 0.6f;
	inline constexpr float DriftBoost2Seconds = 1.0f;
	inline constexpr float DriftBoost3Seconds = 1.5f;

	/**
	 * Derrape controlable: el freno de mano del buggy frena a 6000 N·m en las traseras y no hay control de estabilidad
	 * (TNBuggy::StabilityYawAccel lo deja libre), así que un derrape acababa en trompo. El kart conserva esta fracción del
	 * freno de mano y, con él puesto, devuelve el morro hacia la velocidad si la deriva pasa de DriftHoldSlipDeg.
	 */
	inline constexpr float DriftHandbrakeTorqueFraction = 0.25f;
	inline constexpr float DriftHoldSlipDeg = 22.f;
	inline constexpr float DriftHoldStiffness = 14.f;
	inline constexpr float DriftHoldDamping = 4.f;
	inline constexpr float DriftHoldMaxAccel = 10.f;

	/** Segundos de mini-turbo que da un derrape de DriftSeconds (0 si no llega al primer tramo). */
	TORTUNABO_API float DriftBoostSeconds(float DriftSeconds);

	struct FDriftStep
	{
		/** Segundos de derrape acumulados tras este paso. */
		float DriftSeconds = 0.f;
		/** Mini-turbo ganado en este paso (solo al soltar el freno de mano tras un derrape válido). */
		float BoostSeconds = 0.f;
	};

	/**
	 * Un paso del derrape: con el freno de mano puesto, en el suelo, a más de DriftMinSpeedCms y girando (|Steer| >=
	 * DriftMinSteer) o deslizando (|SlipDeg| >= DriftMinSlipDeg) suma Dt; al soltar el freno de mano da DriftBoostSeconds y
	 * vuelve a 0. Si se frena hasta casi parar (menos de DriftMinSpeedCms) con el freno puesto, el derrape se pierde sin turbo.
	 */
	TORTUNABO_API FDriftStep AdvanceDrift(float DriftSeconds, bool bHandbrake, bool bGrounded, float ForwardSpeedCms, float Steer,
		float SlipDeg, float Dt);
}

UCLASS()
class TORTUNABO_API ATN_KartBuggy : public ATN_Buggy
{
	GENERATED_BODY()

public:
	ATN_KartBuggy();

	virtual void PostInitializeComponents() override;
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

	/** Objetos de Karts y disparo propio de la conductora sola (false en el Rally: ATN_RallyKartBuggy, #629). */
	bool UsesDriverItems() const { return bDriverItems; }

	/** Con la conducción de los karts (#742) puesta: true en Karts salvo con TN.Kart.Tuning 0; false en el Rally. */
	bool UsesKartTuning() const { return bKartTuned; }

	/** Si esta clase pide la conducción de los karts (true en ATN_KartBuggy; false en ATN_RallyKartBuggy). */
	bool WantsKartTuning() const { return bKartTuning; }

	/** Cuánto más rápido que el buggy del Rally corre este kart (1 sin la conducción de los karts). */
	float GetKartSpeedScale() const { return bKartTuned ? KartSpeedScale : 1.f; }

	/** Peón de la artillera que crea al sentarla (el de Karts o, en el Rally, el que no tiene las teclas de objeto). */
	TSubclassOf<ATN_BuggyGunnerPawn> GetGunnerPawnClass() const { return GunnerPawnClass; }

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

	/**
	 * Karts: las cajas «?» dan objetos (UTN_KartItemComponent) que usa la artillera o, si va sola, la conductora, y la
	 * conductora sola dispara hacia donde mira. False en el Rally (ATN_RallyKartBuggy, #629): las cajas dan munición de la
	 * torreta y se dispara con los controles del buggy (apuntado automático si va sola).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Karts")
	bool bDriverItems = true;

	/**
	 * Conducción de los karts (#742): más punta y aceleración, dirección que se cierra a velocidad y mini-turbo al salir del
	 * derrape. False en el Rally (ATN_RallyKartBuggy): ahí el buggy conduce como siempre.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Conducción")
	bool bKartTuning = true;

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
	/** Servidor y conductora local: suma el derrape con el freno de mano y da el mini-turbo al soltarlo (#742). */
	void UpdateDrift(float DeltaSeconds);
	/** Servidor y conductora local: con el freno de mano, devuelve el morro hacia la velocidad pasada la deriva del derrape (#742). */
	void ApplyDriftStability();
	/** Quita freno de mano a las ruedas traseras al pulsarlo (con la simulación de Chaos lista): el derrape no es una frenada. */
	void ApplyKartHandbrake();
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
	/** La conducción de los karts está puesta en este kart (bKartTuning y TN.Kart.Tuning) y la escala de velocidad que usa. */
	bool bKartTuned = false;
	float KartSpeedScale = 1.f;
	/** Segundos de derrape acumulados con el freno de mano (UpdateDrift). */
	float DriftSeconds = 0.f;
	bool bKartHandbrakeApplied = false;
};
