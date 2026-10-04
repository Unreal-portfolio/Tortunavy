// Animación procedural de las tortugas sentadas en el buggy (#305): la conductora gira el volante, da cabezazos al
// frenar o chocar, echa la cabeza atrás al acelerar, se inclina hacia fuera en las curvas y se encoge en los saltos con
// rebote al aterrizar; la artillera gira el cuerpo y la cabeza con el apuntado de la torreta, se echa atrás al disparar,
// hace un gesto al cambiar de munición, se agarra encogida en los saltos y se inclina en las curvas.
// Cosmético y local en cada máquina: solo lee datos que ya están replicados (velocidad del chasis, contacto de las
// ruedas, estado de la torreta). Sin RPC. Lógica pura (muelle-amortiguador y reacciones) en TNRiderAnim.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TN_BuggyRiderAnimComponent.generated.h"

class UChaosWheeledVehicleMovementComponent;
class USkeletalMeshComponent;
class UTN_BuggyTurretComponent;
class UTN_ProcAnimInstance;

/** Plaza que ocupa la tortuga animada. */
UENUM(BlueprintType)
enum class ETNBuggyRiderRole : uint8
{
	Driver UMETA(DisplayName = "Conductora"),
	Gunner UMETA(DisplayName = "Artillera")
};

/** Muelle-amortiguador de un canal de la animación (un ángulo en grados, un desplazamiento en cm o un peso). */
USTRUCT(BlueprintType)
struct TORTUNABO_API FTNRiderSpringTuning
{
	GENERATED_BODY()

	FTNRiderSpringTuning() = default;
	FTNRiderSpringTuning(float InFrequencyHz, float InDampingRatio, float InLimit)
		: FrequencyHz(InFrequencyHz), DampingRatio(InDampingRatio), Limit(InLimit) {}

	/** Frecuencia natural (Hz): cuánto tarda en seguir al objetivo. 0 = sigue al objetivo sin muelle. */
	UPROPERTY(EditAnywhere, Category = "Muelle", meta = (ClampMin = "0", ClampMax = "30"))
	float FrequencyHz = 3.f;

	/** Amortiguamiento relativo: < 1 rebota, 1 llega sin pasarse, > 1 llega despacio. Siempre acaba parándose. */
	UPROPERTY(EditAnywhere, Category = "Muelle", meta = (ClampMin = "0.05", ClampMax = "3"))
	float DampingRatio = 0.5f;

	/** Límite simétrico del valor (mismas unidades que el canal). 0 = sin límite. */
	UPROPERTY(EditAnywhere, Category = "Muelle", meta = (ClampMin = "0"))
	float Limit = 0.f;
};

/** Cómo responden la cabeza y el cuerpo a la aceleración del buggy (cm/s², en los ejes de la malla de la tortuga). */
USTRUCT(BlueprintType)
struct TORTUNABO_API FTNRiderReactionTuning
{
	GENERATED_BODY()

	/** Aceleración o frenada (cm/s²) por debajo de la cual la cabeza no reacciona: solo responde a lo fuerte. */
	UPROPERTY(EditAnywhere, Category = "Reacción", meta = (ClampMin = "0"))
	float HeadAccelThreshold = 500.f;

	/** Grados de cabeceo por cm/s² que pasan del umbral (frenada: hacia delante; aceleración: hacia atrás). */
	UPROPERTY(EditAnywhere, Category = "Reacción", meta = (ClampMin = "0"))
	float HeadDegPerAccel = 0.015f;

	/** Aceleración lateral (cm/s²) por debajo de la cual el cuerpo no se inclina. */
	UPROPERTY(EditAnywhere, Category = "Reacción", meta = (ClampMin = "0"))
	float LeanAccelThreshold = 250.f;

	/** Grados de inclinación hacia fuera de la curva por cm/s² de aceleración lateral que pasan del umbral. */
	UPROPERTY(EditAnywhere, Category = "Reacción", meta = (ClampMin = "0"))
	float LeanDegPerAccel = 0.012f;

	/** Frenada o golpe lateral (cm/s², ya suavizados) a partir del cual hay sacudida de choque. */
	UPROPERTY(EditAnywhere, Category = "Reacción", meta = (ClampMin = "0"))
	float ImpactDecel = 3500.f;

	/** Sacudida del choque: velocidad (grados/s) que recibe la cabeza al llegar al umbral (el doble como mucho). */
	UPROPERTY(EditAnywhere, Category = "Reacción", meta = (ClampMin = "0"))
	float ImpactKickDegPerSec = 600.f;

	/** Cuánto amortigua el agarre de la artillera las reacciones (0 = nada, 1 = rígida del todo). */
	UPROPERTY(EditAnywhere, Category = "Reacción", meta = (ClampMin = "0", ClampMax = "1"))
	float GripRigidity = 0.75f;
};

/** Cómo sigue la artillera el apuntado de la torreta (grados, relativos al buggy). */
USTRUCT(BlueprintType)
struct TORTUNABO_API FTNGunnerAimTuning
{
	GENERATED_BODY()

	/**
	 * Giro máximo del cuerpo y el cuello juntos hacia cada lado: más allá la torreta gira sola (las piernas no se mueven).
	 * 150: con 110, apuntando atrás la cabeza quedaba de lado y el poste de la torreta, delante de ella, la rozaba (#435).
	 */
	UPROPERTY(EditAnywhere, Category = "Apuntado", meta = (ClampMin = "0", ClampMax = "170"))
	float MaxYawDeg = 150.f;

	/** Parte del giro que hace el torso (el resto, el cuello). */
	UPROPERTY(EditAnywhere, Category = "Apuntado", meta = (ClampMin = "0", ClampMax = "1"))
	float TorsoShare = 0.65f;

	/** Fracción del cabeceo de la torreta que sigue la cabeza. */
	UPROPERTY(EditAnywhere, Category = "Apuntado", meta = (ClampMin = "0", ClampMax = "1"))
	float PitchFollow = 0.8f;

	/** Cabeceo máximo de la cabeza hacia arriba y hacia abajo. */
	UPROPERTY(EditAnywhere, Category = "Apuntado", meta = (ClampMin = "0", ClampMax = "80"))
	float MaxPitchUpDeg = 35.f;

	UPROPERTY(EditAnywhere, Category = "Apuntado", meta = (ClampMin = "0", ClampMax = "80"))
	float MaxPitchDownDeg = 10.f;
};

/** Lógica pura de la animación de las ocupantes (sin mundo ni actores): la prueban Tortunabo.Rally.RiderAnim.*. */
namespace TNRiderAnim
{
	/** Estado de un canal: valor y su velocidad (unidades por segundo). */
	struct FSpring
	{
		float Value = 0.f;
		float Velocity = 0.f;
	};

	/** Objetivos que dicta la aceleración: cabeceo (+ = hacia delante) e inclinación (+ = hacia la izquierda, +X). */
	struct FTargets
	{
		float HeadPitchDeg = 0.f;
		float LeanRollDeg = 0.f;
	};

	/** Subpaso máximo de la integración y paso máximo que se acepta de una vez (tirones de fotograma). */
	constexpr float MaxSubstepSeconds = 1.f / 120.f;
	constexpr float MaxStepSeconds = 0.1f;

	/**
	 * Avanza un muelle-amortiguador hacia Target durante DeltaSeconds (Euler semiimplícito con el amortiguamiento
	 * implícito y subpasos: estable con cualquier paso). El valor queda dentro de ±Limit y, al tocarlo, pierde la
	 * velocidad que lo empujaba hacia fuera. Un estado no finito se reinicia.
	 */
	TORTUNABO_API FSpring StepSpring(const FSpring& State, float Target, const FTNRiderSpringTuning& Tuning, float DeltaSeconds);

	/** Suma una sacudida a la velocidad del canal (golpe, aterrizaje o disparo). */
	TORTUNABO_API FSpring KickSpring(const FSpring& State, float VelocityKick);

	/** Lo que sobra de |Value| por encima de Threshold, con el signo de Value (0 dentro del umbral). */
	TORTUNABO_API float DeadZone(float Value, float Threshold);

	/**
	 * Objetivos de cabeza y cuerpo para una aceleración en los ejes de la malla (X = su izquierda, Y = delante):
	 * frenar lleva la cabeza hacia delante, acelerar hacia atrás, y la aceleración lateral inclina el cuerpo hacia fuera
	 * de la curva. Grip01 (agarre de la artillera) los reduce según GripRigidity.
	 */
	TORTUNABO_API FTargets ReactionTargets(const FVector& AccelMesh, float Grip01, const FTNRiderReactionTuning& Tuning);

	/**
	 * Dirección estimada en [-1, 1] (+ = derecha) a partir de la guiñada (grados/s, + = a la derecha) y la velocidad
	 * hacia delante (modelo de bicicleta: tan(ángulo) = guiñada · batalla / velocidad). Marcha atrás invierte el signo.
	 * Por debajo de MinSpeedCms devuelve 0.
	 */
	TORTUNABO_API float SteerFromYawRate(float YawRateDegPerSec, float ForwardSpeedCms, float WheelbaseCm, float MaxSteerDeg,
		float MinSpeedCms);

	/**
	 * Si el estado replicado de la torreta delata un disparo: el calor del coco sube al menos MinHeatStep, o las cargas
	 * especiales bajan con la misma munición (o se agotan). Una caja nueva con otra munición no cuenta.
	 */
	TORTUNABO_API bool IsShotSignal(float PrevHeat01, float Heat01, int32 PrevCharges, int32 Charges, bool bSameAmmo,
		float MinHeatStep);

	/** Hacia dónde mira la artillera: giro total (+ = derecha) y cabeceo de la cabeza (+ = arriba), en grados. */
	struct FGunnerAim
	{
		float YawDeg = 0.f;
		float PitchDeg = 0.f;
	};

	/**
	 * Objetivo de la artillera para un apuntado relativo al buggy (el de UTN_BuggyTurretComponent::GetDisplayAim): el giro
	 * se normaliza a [-180, 180] y se limita a ±MaxYawDeg; el cabeceo se escala por PitchFollow y se limita. Un apuntado
	 * no finito, o la artillera noqueada, mira al frente.
	 */
	TORTUNABO_API FGunnerAim GunnerAimTargets(float AimYawDeg, float AimPitchDeg, bool bKnocked, const FTNGunnerAimTuning& Tuning);

	/** Si la munición seleccionada ha cambiado desde la muestra anterior (sin muestra anterior no cuenta). */
	TORTUNABO_API bool IsAmmoSwapSignal(bool bHasPrev, uint8 PrevAmmo, uint8 Ammo);

	/** Muelles por defecto del retroceso y del gesto de cambio de munición (los del componente; los prueban los tests). */
	TORTUNABO_API FTNRiderSpringTuning DefaultRecoilSpring();
	TORTUNABO_API FTNRiderSpringTuning DefaultSwapSpring();
	/** Sacudidas por defecto: grados/s del retroceso por disparo y 1/s del gesto de cambio de munición. */
	constexpr float DefaultRecoilKickDegPerSec = 620.f;
	constexpr float DefaultSwapKickPerSec = 22.f;
}

/**
 * Anima por procedimiento una tortuga sentada en el buggy (la malla que se le da con Setup).
 *
 * Con un UTN_ProcAnimInstance en la malla (UTN_TurtleAnimInstance lo es), suma giros y desplazamientos a la pose que
 * ya sale de la animación (cadera, columna, cuello, cabeza y brazos, con sus hijos) y los escribe en BoneQuat/BoneLoc,
 * que esa clase aplica al final de la evaluación: no hace falta tocar la clase de animación ni un AnimBP. La pose base
 * es la del fotograma anterior (un fotograma de retraso, invisible en una tortuga sentada). Sin esa clase, mueve y
 * gira el componente de la malla entero.
 *
 * Datos: aceleración por diferencias de la velocidad del actor (física replicada), ruedas en el aire por su contacto,
 * dirección por la entrada local (conductora local o IA en el servidor) o, si no la hay, estimada por la guiñada,
 * apuntado por UTN_BuggyTurretComponent::GetDisplayAim (el local en la máquina de la artillera, el replicado en el resto),
 * cambios de munición por la munición seleccionada replicada y disparos por el calor y las cargas replicadas de la
 * torreta o por NotifyShot (lo llama el multicast del disparo). No corre en el servidor dedicado.
 */
UCLASS(ClassGroup = (Rally), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_BuggyRiderAnimComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_BuggyRiderAnimComponent();

	/** Malla de la tortuga que se anima y su plaza. Se llama en el constructor del vehículo o antes de BeginPlay. */
	UFUNCTION(BlueprintCallable, Category = "Rally|Ocupantes")
	void Setup(USkeletalMeshComponent* InRiderMesh, ETNBuggyRiderRole InRole);

	/**
	 * Retroceso de la artillera por un disparo en la dirección WorldDirection (mundo). Cosmético y local: se puede
	 * llamar en cualquier máquina; dos avisos del mismo disparo (este y la detección automática) cuentan una vez.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rally|Ocupantes")
	void NotifyShot(FVector WorldDirection);

	UFUNCTION(BlueprintPure, Category = "Rally|Ocupantes")
	ETNBuggyRiderRole GetRole() const { return Role; }

	UFUNCTION(BlueprintPure, Category = "Rally|Ocupantes")
	USkeletalMeshComponent* GetRiderMesh() const { return RiderMesh; }

	/** Componente de la plaza Role en el actor (nulo si no lo hay). */
	static UTN_BuggyRiderAnimComponent* FindForRole(const AActor* Owner, ETNBuggyRiderRole Role);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Postura sentada de base (#290): piernas dobladas 90° en la cadera y la rodilla y brazos al volante (conductora) o
	 * listos para lanzar (artillera), los de Art/Source/Vehicles/Buggy/turtle_pose.py con los que se midieron los
	 * asientos. Las reacciones van encima.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes")
	bool bSeatedPose = true;

	// ── Reacciones ──────────────────────────────────────────────────────────────

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes")
	FTNRiderReactionTuning Reaction;

	/** Cabeceo de la cabeza (grados, + = hacia delante). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Muelles")
	FTNRiderSpringTuning HeadSpring = FTNRiderSpringTuning(6.f, 0.35f, 28.f);

	/** Inclinación del cuerpo en las curvas (grados, + = hacia la izquierda de la tortuga). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Muelles")
	FTNRiderSpringTuning LeanSpring = FTNRiderSpringTuning(3.5f, 0.6f, 18.f);

	/** Giro de los brazos con el volante (grados). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Muelles")
	FTNRiderSpringTuning SteerSpring = FTNRiderSpringTuning(8.f, 0.9f, 45.f);

	/** Encogerse (1 = del todo; negativo = se estira en el rebote). Poco amortiguado para que rebote al aterrizar. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Muelles")
	FTNRiderSpringTuning CrouchSpring = FTNRiderSpringTuning(5.f, 0.3f, 1.6f);

	/**
	 * Retroceso del torso al disparar (grados que se echa hacia atrás). Lento a propósito: con 9 Hz y unos centímetros
	 * duraba 60 ms y no se veía (#305).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Muelles")
	FTNRiderSpringTuning RecoilDegSpring = TNRiderAnim::DefaultRecoilSpring();

	/** Giro de la artillera siguiendo el apuntado (grados, sin límite propio: lo pone GunnerAim). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Muelles")
	FTNRiderSpringTuning AimYawSpring = FTNRiderSpringTuning(4.f, 0.8f, 0.f);

	/** Cabeceo de la cabeza de la artillera siguiendo el apuntado (grados). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Muelles")
	FTNRiderSpringTuning AimPitchSpring = FTNRiderSpringTuning(5.f, 0.8f, 0.f);

	/** Gesto de cambio de munición (peso: 1 = del todo). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Muelles")
	FTNRiderSpringTuning SwapSpring = TNRiderAnim::DefaultSwapSpring();

	/** Agarre de la artillera en el aire (0..1). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Muelles")
	FTNRiderSpringTuning GripSpring = FTNRiderSpringTuning(6.f, 1.f, 1.f);

	// ── Conductora ──────────────────────────────────────────────────────────────

	/** Grados que sube una mano (y baja la otra) con la dirección a tope. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Conductora", meta = (ClampMin = "0", ClampMax = "90"))
	float WheelTurnDeg = 35.f;

	/** Grados que se recogen los brazos encogida en el aire. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Conductora", meta = (ClampMin = "0", ClampMax = "90"))
	float ArmTuckDeg = 15.f;

	/** Estimación de la dirección por la guiñada (máquinas sin la entrada): batalla, ángulo a tope y velocidad mínima. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Conductora", meta = (ClampMin = "1"))
	float WheelbaseCm = 250.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Conductora", meta = (ClampMin = "1", ClampMax = "80"))
	float MaxSteerDeg = 30.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Conductora", meta = (ClampMin = "0"))
	float MinSteerSpeedCms = 150.f;

	// ── Artillera ───────────────────────────────────────────────────────────────

	/** Cuánto se encoge la artillera en el aire (la conductora, del todo). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Artillera", meta = (ClampMin = "0", ClampMax = "1"))
	float GunnerAirCrouch = 0.75f;

	/**
	 * Grados que bajan los brazos al agarrarse en el aire. 0: al bajarlos, atravesaban el aro y los tirantes que tenía la
	 * torreta (#435, TN.Rally.DebugRiderAirborne con TN.Rally.DebugTurretFit); se queda agarrada a la torreta encogida.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Artillera", meta = (ClampMin = "0", ClampMax = "90"))
	float GripArmDeg = 0.f;

	/** Grados que se encorva hacia delante agarrada en el aire. 0: al encorvarse, la cabeza bajaba hasta el aro (#435). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Artillera", meta = (ClampMin = "0", ClampMax = "60"))
	float GripHunchDeg = 0.f;

	/** Cómo sigue el apuntado de la torreta (cuerpo, cuello y cabeza). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Artillera")
	FTNGunnerAimTuning GunnerAim;

	/** Velocidad (grados/s) que recibe el retroceso del torso con cada disparo. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Artillera", meta = (ClampMin = "0"))
	float RecoilKickDegPerSec = TNRiderAnim::DefaultRecoilKickDegPerSec;

	/** Centímetros que se desplaza el torso hacia atrás por grado de retroceso. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Artillera", meta = (ClampMin = "0"))
	float RecoilBackCmPerDeg = 0.3f;

	/** Latigazo de la cabeza: grados de cabeceo hacia delante por grado de retroceso del torso. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Artillera")
	float RecoilHeadFollow = 0.5f;

	/** Gesto de cambio de munición: sacudida (1/s), grados que sube el brazo izquierdo y que baja la cabeza a mirar. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Artillera", meta = (ClampMin = "0"))
	float SwapKickPerSec = TNRiderAnim::DefaultSwapKickPerSec;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Artillera", meta = (ClampMin = "0", ClampMax = "120"))
	float SwapArmDeg = 70.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Artillera", meta = (ClampMin = "0", ClampMax = "60"))
	float SwapHeadDeg = 20.f;

	/** Detectar los disparos por el estado replicado de la torreta (vale en todas las máquinas sin llamar a nada). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Artillera")
	bool bAutoDetectShots = true;

	/** Subida mínima del calor del coco que cuenta como disparo (cada disparo suma 1/6). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Artillera", meta = (ClampMin = "0.01", ClampMax = "1"))
	float ShotHeatStep = 0.1f;

	/** Avisos de disparo más juntos que esto cuentan como uno (s). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Artillera", meta = (ClampMin = "0"))
	float ShotDedupSeconds = 0.12f;

	// ── Saltos y aceleración ────────────────────────────────────────────────────

	/** Cuánto baja la cadera encogida (cm) y cuánto se mete el cuello en el caparazón (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Saltos", meta = (ClampMin = "0"))
	float CrouchDropCm = 3.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Saltos", meta = (ClampMin = "0"))
	float NeckTuckCm = 7.f;

	/** Rebote al aterrizar: sacudida del canal de encogerse por cm/s de caída, y su máximo. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Saltos", meta = (ClampMin = "0"))
	float LandingKickPerCms = 0.02f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Saltos", meta = (ClampMin = "0"))
	float MaxLandingKick = 30.f;

	/** Suavizado de la aceleración medida (Hz): filtra el ruido de la interpolación de red. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Saltos", meta = (ClampMin = "0.5", ClampMax = "60"))
	float AccelSmoothingHz = 8.f;

	/** Segundos mínimos entre dos sacudidas de choque. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Saltos", meta = (ClampMin = "0"))
	float ImpactCooldownSeconds = 0.35f;

	/** Salto de posición en un fotograma (cm) que se toma por teletransporte: reinicia la medida sin sacudidas. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Ocupantes|Saltos", meta = (ClampMin = "1"))
	float TeleportJumpCm = 600.f;

private:
	/** Lo que se mide del vehículo en un fotograma. */
	struct FVehicleSample
	{
		FVector AccelMesh = FVector::ZeroVector;
		float Steer01 = 0.f;
		float VelocityZ = 0.f;
		bool bAirborne = false;
		bool bValid = false;
	};

	bool IsRiderActive() const;
	void ResetRider();
	FVehicleSample SampleVehicle(float DeltaTime);
	bool IsVehicleAirborne();
	float ReadSteer01(const FVector& Velocity);
	/** Disparos y cambios de munición por el estado replicado de la torreta. */
	void UpdateTurretSignals();
	void UpdateChannels(const FVehicleSample& Sample, float DeltaTime);
	void UpdateGunnerAim(float DeltaTime);
	void UpdateCrouch(const FVehicleSample& Sample, float DeltaTime);
	void ApplyImpactKicks(const FVector& AccelMesh);
	bool IsAtRest() const;
	float HeadPitchWithRecoil() const;
	void ApplyPose();
	void ApplyBonePose(UTN_ProcAnimInstance& Anim);
	void ApplyComponentPose();
	void ClearBoneOverrides();
	void RestoreComponentPose();
	UChaosWheeledVehicleMovementComponent* GetVehicleMovement();
	UTN_BuggyTurretComponent* GetTurret();

	UPROPERTY(VisibleAnywhere, Category = "Rally|Ocupantes")
	TObjectPtr<USkeletalMeshComponent> RiderMesh;

	UPROPERTY(VisibleAnywhere, Category = "Rally|Ocupantes")
	ETNBuggyRiderRole Role = ETNBuggyRiderRole::Driver;

	TWeakObjectPtr<UChaosWheeledVehicleMovementComponent> CachedMovement;
	TWeakObjectPtr<UTN_BuggyTurretComponent> CachedTurret;

	/** Clase de animación en la que se han escrito los huesos y cuáles (para quitarlos al acabar). */
	TWeakObjectPtr<UTN_ProcAnimInstance> PosedAnim;
	TSet<FName> WrittenBones;

	/** Transformación relativa de la malla antes de moverla entera (solo sin UTN_ProcAnimInstance). */
	FTransform BaseRelative = FTransform::Identity;
	bool bComponentPosed = false;

	TNRiderAnim::FSpring HeadPitch;
	TNRiderAnim::FSpring LeanRoll;
	TNRiderAnim::FSpring Steer;
	TNRiderAnim::FSpring Crouch;
	TNRiderAnim::FSpring Recoil;
	TNRiderAnim::FSpring Grip;
	TNRiderAnim::FSpring AimYaw;
	TNRiderAnim::FSpring AimPitch;
	TNRiderAnim::FSpring Swap;

	FVector PrevLocation = FVector::ZeroVector;
	FVector PrevVelocity = FVector::ZeroVector;
	FVector SmoothedAccelWorld = FVector::ZeroVector;
	/** Dirección del retroceso en los ejes de la malla (horizontal, contraria al disparo). */
	FVector RecoilDirMesh = FVector(0.f, -1.f, 0.f);
	bool bHasPrevSample = false;
	bool bWasAirborne = false;
	bool bRiderWasActive = false;
	float MaxFallSpeed = 0.f;
	double LastImpactTime = -1000.0;
	double LastShotTime = -1000.0;

	float PrevHeat01 = 0.f;
	int32 PrevCharges = 0;
	uint8 PrevAmmo = 0;
	uint8 PrevSelectedAmmo = 0;
	bool bHasTurretSample = false;
};
