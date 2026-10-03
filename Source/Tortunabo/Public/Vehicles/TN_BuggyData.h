// Ajuste del buggy del Rally. Port de UHYBuggyData (HellYeah) sin carga: los valores por defecto parten de BUGGY_DATA
// (Tools/Unreal/build_data_assets.py de HellYeah), con el agarre, el par y el turbo de #288 y #294, y los usa ATN_Buggy si
// no se le asigna un asset.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TN_BuggyData.generated.h"

UCLASS(BlueprintType)
class TORTUNABO_API UTN_BuggyData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Par máximo del motor (N·m). 1275 = 1,5 × 850: 0-60 km/h en ~2 s en vez de ~3 s (el arranque va entero en la parte
	 * plana de TNBuggy::TorqueCurveKeys). La curva conserva el par absoluto antiguo desde el 80 % de MaxRPM: misma punta.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy")
	float MaxTorque = 1275.f;

	/** Régimen máximo (rpm): fija la punta, unos 110 km/h con la relación final 2,0 (el cambio no pasa de 1.ª). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy")
	float MaxRPM = 3400.f;

	/** Relación final de la transmisión: junto con MaxRPM (en 1.ª) fija la punta. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy")
	float FinalDriveRatio = 2.0f;

	/** Multiplicador de fricción de las ruedas delanteras. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy")
	float FrontFriction = 3.0f;

	/**
	 * Multiplicador de fricción de las ruedas traseras. Mayor que el delantero: con el volante a fondo satura antes el
	 * eje delantero (subviraje) y la trasera no se va; el derrape largo queda para el freno de mano.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy")
	float RearFriction = 3.4f;

	/** Fricción trasera con el freno de mano: más baja = más derrape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy")
	float HandbrakeRearFriction = 1.4f;

	/** Contravolante añadido (fracción de la dirección) al llegar a MaxAssistAngleDeg de deriva. Con 0,8 sobrecorregía. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy")
	float CounterSteerAssist = 0.5f;

	/** Deriva (grados) a la que la asistencia llega a su máximo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy")
	float MaxAssistAngleDeg = 35.f;

	/** Control de estabilidad sin freno de mano (TNBuggy::StabilityYawAccel): deriva a la que empieza (grados). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Estabilidad")
	float StabilityStartSlipDeg = 6.f;

	/** Muelle (1/s² por radián de deriva de más), amortiguador (1/s) y tope (rad/s²); muelle 0 lo desactiva. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Estabilidad")
	float StabilityStiffness = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Estabilidad")
	float StabilityDamping = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Estabilidad")
	float StabilityMaxAccel = 8.f;

	/** Turbo: multiplicador del par mientras está activo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turbo")
	float BoostTorqueMultiplier = 1.35f;

	/** Turbo: multiplicador de la punta (TNRallyTurret::BuggyTopSpeedCms) que alcanza el empuje del turbo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turbo")
	float BoostTopSpeedMultiplier = 1.15f;

	/** Turbo: empuje (cm/s², hacia delante) y banda (cm/s) en la que se apaga antes de la punta del turbo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turbo")
	float BoostPushAccel = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turbo")
	float BoostPushFadeBandCms = 200.f;

	/** Turbo: gasto (barra/s) y recargas derrapando con el freno de mano y en el aire (barra/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turbo")
	float BoostDrainPerSecond = 1.f / 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turbo")
	float BoostDriftRechargePerSecond = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turbo")
	float BoostAirRechargePerSecond = 0.4f;

	/** Turbo: deriva mínima (grados) del derrape que recarga y carga al aparecer el buggy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turbo")
	float BoostMinDriftSlipDeg = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turbo")
	float BoostStartCharge = 0.5f;

	/** Segundos volcado antes de poder enderezar pulsando R (o Y). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enderezado")
	float SelfRightManualDelay = 0.5f;

	/** Segundos volcado a los que el servidor lo endereza solo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enderezado")
	float SelfRightAutoDelay = 4.f;

	/** Cuánto sube el buggy (cm) al enderezarlo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enderezado")
	float SelfRightLiftCm = 100.f;

	/** Segundos manteniendo R (o Y) para pedir la reaparición. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enderezado")
	float RespawnHoldSeconds = 1.5f;

	/** Antivuelco: alabeo y cabeceo tolerados con ruedas en el suelo (grados); en el aire, ninguno. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Antivuelco")
	float AntiRollGroundFreeRollDeg = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Antivuelco")
	float AntiRollGroundFreePitchDeg = 30.f;

	/** Muelle (aceleración angular por radián de exceso, 1/s²); 0 lo desactiva. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Antivuelco")
	float AntiRollStiffness = 14.f;

	/** Amortiguador (1/s) y tope (rad/s²). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Antivuelco")
	float AntiRollDamping = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Antivuelco")
	float AntiRollMaxAccel = 25.f;

	/** Subida mínima del suelo bajo la rueda (cm) en un frame para contar como escalón. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension")
	float BumpMinStepCm = 4.f;

	/** Fracción de la velocidad de subida teórica que pasa al chasis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension")
	float BumpKickScale = 0.15f;

	/** Tope del golpe (cm/s hacia arriba en la rueda). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspension")
	float BumpKickMax = 200.f;

	/** Dirección máxima del bamboleo del coco (fracción del volante) y su frecuencia (Hz). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impactos")
	float WobbleAmplitude = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impactos")
	float WobbleFrequency = 5.f;

	/** Ganancia del frenado en el charco: deceleración (cm/s²) por cada cm/s de más sobre la velocidad tope. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impactos")
	float PuddleBrakeGain = 3.f;
};
