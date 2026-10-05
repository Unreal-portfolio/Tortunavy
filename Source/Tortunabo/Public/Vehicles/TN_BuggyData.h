// Ajuste del buggy del Rally. Port de UHYBuggyData (HellYeah) sin carga: los valores por defecto parten de BUGGY_DATA
// (Tools/Unreal/build_data_assets.py de HellYeah), con el agarre, el par y el turbo de #288 y #294, y los usa ATN_Buggy si
// no se le asigna un asset.
#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "TN_BuggyData.generated.h"

UCLASS(BlueprintType)
class TORTUNABO_API UTN_BuggyData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Par máximo del motor (N·m), unas 1,65 veces los 850 de HellYeah: con DriveRearShare, 0-60 km/h en 1,9 s en llano sin
	 * turbo (#294, Tortunabo.Rally.Measure.ZeroToSixtyFlat; el arranque va entero en la parte plana de
	 * TNBuggy::TorqueCurveKeys). La curva conserva el par absoluto antiguo desde el 80 % de MaxRPM: misma punta.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy")
	float MaxTorque = 1400.f;

	/**
	 * Parte del par que va al eje trasero (tracción total; 1 = solo trasera). Con solo tracción trasera el arranque patinaba
	 * (unos 0,77 g de tope) y más par no bajaba el 0-60 de 2,2 s; con el 70 % detrás tiene la tracción de las cuatro ruedas,
	 * sigue derrapando con el freno de mano y gira algo más cerrado a mucha velocidad (#294).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy", meta = (ClampMin = "0.5", ClampMax = "1"))
	float DriveRearShare = 0.7f;

	/** Régimen máximo (rpm): fija la punta, unos 110 km/h con la relación final 2,0 (el cambio no pasa de 1.ª). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy")
	float MaxRPM = 3400.f;

	/** Relación final de la transmisión: junto con MaxRPM (en 1.ª) fija la punta. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy")
	float FinalDriveRatio = 2.0f;

	/**
	 * Multiplicador de fricción de las ruedas delanteras. Igual que el trasero (#606): con 3,0 frente a 3,4 el eje delantero
	 * saturaba antes y el buggy subviraba a mucha velocidad. Con este agarre, girar fuerte muy rápido vuelca antes que deslizar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy")
	float FrontFriction = 3.6f;

	/** Multiplicador de fricción de las ruedas traseras; el derrape largo queda para el freno de mano. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy")
	float RearFriction = 3.6f;

	/** Fricción trasera con el freno de mano: más baja = más derrape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy")
	float HandbrakeRearFriction = 1.4f;

	/**
	 * Ángulo máximo de la rueda delantera interior (grados), el mismo a cualquier velocidad (#606: el de un buggy real, 35-40).
	 * La exterior gira TNBuggy::SteerAngleRatio de esto. A mucha velocidad y con el volante a fondo puede volcar (el
	 * enderezado lo recupera).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dirección", meta = (ClampMin = "10", ClampMax = "45"))
	float MaxSteerAngleDeg = 38.f;

	/** Rapidez del volante: fracción del recorrido por segundo al girar (Chaos trae 2,5: 0,4 s de centro a tope). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dirección", meta = (ClampMin = "0.5"))
	float SteerRiseRate = 7.f;

	/** Rapidez al soltar o cambiar de lado (fracción por segundo; Chaos trae 5). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dirección", meta = (ClampMin = "0.5"))
	float SteerFallRate = 9.f;

	/**
	 * Respuesta lineal del volante: media palanca, medio ángulo. Con false, la cuadrática de Chaos (media palanca = un cuarto),
	 * que hacía que el buggy pareciera no girar con el mando.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dirección")
	bool bLinearSteerResponse = true;

	/** Contravolante añadido (fracción de la dirección) al llegar a MaxAssistAngleDeg de deriva. Con 0,8 sobrecorregía. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy")
	float CounterSteerAssist = 0.5f;

	/** Deriva (grados) a la que la asistencia llega a su máximo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy")
	float MaxAssistAngleDeg = 35.f;

	/**
	 * Deriva (grados) por debajo de la cual no hay contravolante (#606): la de un giro con agarre a mucha velocidad (unos
	 * grados) restaba dirección y el buggy no giraba. Solo se asiste el derrape de verdad.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Buggy", meta = (ClampMin = "0"))
	float CounterSteerStartSlipDeg = 12.f;

	/**
	 * Control de estabilidad sin freno de mano (TNBuggy::StabilityYawAccel): deriva a la que empieza (grados). 20 y no 6
	 * (#606): con 6 frenaba la guiñada de cualquier curva rápida (subviraje); ahora solo ataja el trompo.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Estabilidad")
	float StabilityStartSlipDeg = 20.f;

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

	/**
	 * Turbo progresivo (#630): segundos con el turbo pisado hasta el empuje completo y segundos en apagarse al soltarlo. El
	 * empuje, el par extra, el FOV de la cámara, la llama y el sonido siguen a la misma fuerza (GetBoostStrength).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turbo", meta = (ClampMin = "0"))
	float BoostRampUpSeconds = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turbo", meta = (ClampMin = "0"))
	float BoostRampDownSeconds = 0.4f;

	/** Forma de la subida si BoostRampCurve no tiene puntos: fuerza = avance ^ exponente (1 = recta; más, empieza más suave). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turbo", meta = (ClampMin = "0.1"))
	float BoostRampExponent = 1.5f;

	/** Curva propia de la subida (X: avance 0..1 de la rampa; Y: fuerza 0..1). Sin puntos, la potencia de BoostRampExponent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turbo")
	FRuntimeFloatCurve BoostRampCurve;

	/** El sonido del turbo va de este volumen (con la fuerza mínima) a 1 y su tono, de X a Y, siguiendo la fuerza. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turbo", meta = (ClampMin = "0", ClampMax = "1"))
	float BoostSoundMinVolume = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turbo")
	FVector2D BoostSoundPitchRange = FVector2D(0.85, 1.1);

	/** Fuerza del turbo (0..1) para un avance de la rampa (0..1): la curva propia o la potencia. */
	float EvaluateBoostRamp(float Progress01) const;

	/**
	 * Freno de la parrilla (#611): ganancia (1/s) con la que el buggy vuelve a su hueco si resbala cuesta abajo antes de la
	 * salida (TNBuggy::GridHoldVelocity). 0 = solo el freno de estacionamiento de Chaos.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Salida", meta = (ClampMin = "0"))
	float GridHoldGain = 12.f;

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

	/**
	 * Velocidad (cm/s) hasta la que el antivuelco corrige entero el alabeo con ruedas en el suelo (45 km/h) y a la que deja
	 * de corregirlo (70 km/h): girar a tope muy rápido vuelca el buggy (#606, decisión del director del 03-10); el enderezado
	 * (#104) lo recupera. El cabeceo y el aire no cambian.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Antivuelco", meta = (ClampMin = "0"))
	float AntiRollGroundRollFullSpeedCms = 1250.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Antivuelco", meta = (ClampMin = "0"))
	float AntiRollGroundRollZeroSpeedCms = 1950.f;

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

	/**
	 * Ganancia del frenado al pasar de la velocidad tope del charco, del agua o de la vida perdida: deceleración (cm/s²) por
	 * cada cm/s de más.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impactos")
	float PuddleBrakeGain = 3.f;

	/**
	 * En el agua (#719): con las ruedas metidas (vados del Rally, mar y pozas de los mapas generados), la punta sin turbo por
	 * esto. Flotando como balsa (Karts) ya va más despacio (UTN_KartTraversalComponent::MaxFloatSpeedCms).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Agua", meta = (ClampMin = "0.1", ClampMax = "1"))
	float WaterSpeedMultiplier = 0.5f;

	/** Cuánto se hunde en el agua el borde de abajo de una rueda para contar como metida (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Agua", meta = (ClampMin = "0"))
	float WadeDepthCm = 10.f;

	/** Ruedas metidas en el agua a partir de las que el buggy va por el agua. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Agua", meta = (ClampMin = "1", ClampMax = "4"))
	int32 WadeMinWheels = 2;

	/**
	 * Daño (#720): el par, la punta y el giro bajan en línea con la vida perdida hasta estas fracciones con la vida a 0, y
	 * vuelven al reaparecer o al curarse (salen de la vida replicada en cada máquina).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Daño", meta = (ClampMin = "0.1", ClampMax = "1"))
	float DamagedTorqueScale = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Daño", meta = (ClampMin = "0.1", ClampMax = "1"))
	float DamagedTopSpeedScale = 0.775f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Daño", meta = (ClampMin = "0.1", ClampMax = "1"))
	float DamagedSteerScale = 0.825f;
};
