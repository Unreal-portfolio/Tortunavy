// Lógica pura del buggy del Rally (sin mundo ni física): derrape asistido, golpe de rueda, enderezado, reaparición
// pulsando, tinte por equipo y frenado del charco. Port de FHYDriftMath y FHYBumpMath (HellYeah) con tests en
// Tortunabo.Rally.Buggy.*. Curvas de dirección y de par, estabilidad, turbo y cámara de la conductora con tests en
// Tortunabo.Rally.Drive.*.
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

DECLARE_LOG_CATEGORY_EXTERN(LogTNBuggy, Log, All);

namespace TNBuggy
{
	// ── Derrape asistido ────────────────────────────────────────────────────────

	/** Por debajo de esta velocidad horizontal (cm/s) no se mide deriva. */
	constexpr float MinSlipSpeed = 100.f;

	/** Deriva a partir de la cual se deja de asistir (marcha atrás o trompo). */
	constexpr float MaxAssistedSlipDeg = 90.f;

	/**
	 * Ángulo con signo, en grados y en (-180, 180], entre el morro y la velocidad, ambos en el plano XY. Positivo si la
	 * velocidad apunta a la derecha del morro. 0 si la velocidad horizontal es menor que MinSlipSpeed.
	 */
	TORTUNABO_API float SlipAngleDeg(const FVector& Forward, const FVector& Velocity);

	/**
	 * Dirección final en [-1, 1]: la pedida más un contravolante proporcional a la deriva que pasa de StartDeg (0 en StartDeg,
	 * Assist en MaxAngleDeg o más). Sin corrección si la deriva supera MaxAssistedSlipDeg. Con StartDeg > 0, la deriva de un
	 * giro con agarre (unos grados a mucha velocidad) no resta dirección (#606).
	 */
	TORTUNABO_API float AssistSteer(float Input, float SlipDeg, float Assist, float MaxAngleDeg, float StartDeg = 0.f);

	/**
	 * Ángulo efectivo de la rueda interior (grados, con signo) con la palanca Input, a SpeedCms y con SlipDeg de deriva: el
	 * de la curva de dirección (MaxSteerAngleDeg) por la dirección tras el contravolante (AssistSteer). Es lo que manda
	 * ATN_Buggy::ApplySteeringAssist a Chaos; los tests comprueban que no cae con la velocidad (#606).
	 */
	TORTUNABO_API float EffectiveSteerDeg(float Input, float SpeedCms, float SlipDeg, float BaseAngleDeg, float Assist,
		float AssistMaxAngleDeg, float AssistStartDeg);

	// ── Golpe de rueda contra un escalón ────────────────────────────────────────

	struct FBumpTuning
	{
		/** Subida mínima del suelo (cm) entre dos frames para contar como subida. */
		float MinStepCm = 4.f;
		/** Fracción de la velocidad de subida teórica (avance × escalón / radio) que pasa al chasis. */
		float Scale = 0.15f;
		/** Tope del golpe (cm/s hacia arriba en la rueda). */
		float MaxKick = 200.f;
	};

	struct FWheelTrack
	{
		bool bPrevRise = false;
		float PendingKick = 0.f;
	};

	struct FBumpStep
	{
		FWheelTrack Track;
		/** Golpe que hay que aplicar en este frame (cm/s); 0 si ninguno. */
		float Kick = 0.f;
	};

	TORTUNABO_API bool IsRise(float StepCm, bool bBothInContact, const FBumpTuning& Tuning);
	TORTUNABO_API float KickSpeed(float StepCm, float ForwardSpeed, float WheelRadius, const FBumpTuning& Tuning);

	/**
	 * Avanza una rueda un frame. Una subida aislada (escalón) golpea un frame después; si sigue subiendo (rampa) se
	 * descarta.
	 */
	TORTUNABO_API FBumpStep BumpStep(const FWheelTrack& Track, float StepCm, float ForwardSpeed, float WheelRadius,
		bool bBothInContact, const FBumpTuning& Tuning);

	// ── Enderezado y reaparición ────────────────────────────────────────────────

	/** Volcado: el eje Z del buggy apunta por debajo de este valor. */
	constexpr float FlippedUpZ = 0.3f;

	enum class ESelfRight : uint8
	{
		None,
		/** La ocupante pulsó R (o Y) con el buggy volcado. */
		Manual,
		/** Lleva AutoDelay segundos volcado. */
		Auto
	};

	TORTUNABO_API bool IsFlipped(float UpZ);

	/** Segundos seguidos volcado tras un frame: suma Dt si está volcado; si no, vuelve a 0. */
	TORTUNABO_API float AdvanceFlipped(float FlippedSeconds, float UpZ, float Dt);

	/**
	 * Qué enderezado toca. Manual si se pide y lleva al menos ManualDelay volcado; Auto a los AutoDelay segundos
	 * volcado aunque nadie lo pida; None en otro caso.
	 */
	TORTUNABO_API ESelfRight DecideSelfRight(float FlippedSeconds, bool bRequested, float ManualDelay, float AutoDelay);

	/** Transform del enderezado: Lift cm más arriba, con solo la guiñada (cabeceo y alabeo a 0). */
	TORTUNABO_API FTransform SelfRightTransform(const FTransform& Current, float LiftCm);

	// ── Antivuelco ──────────────────────────────────────────────────────────────

	struct FAntiRollTuning
	{
		/** Alabeo (grados respecto a la vertical) que se tolera con ruedas en el suelo: peraltes y taludes. */
		float GroundFreeRollDeg = 20.f;
		/** Cabeceo tolerado con ruedas en el suelo (las rampas de la pista no pasan de 12 grados). */
		float GroundFreePitchDeg = 30.f;
		/** Muelle (1/s²): aceleración angular por radián de exceso. */
		float Stiffness = 14.f;
		/** Amortiguador (1/s) sobre la velocidad de alabeo y de cabeceo. */
		float Damping = 4.f;
		/** Tope de la aceleración angular (rad/s²). */
		float MaxAccel = 25.f;
		/**
		 * Con ruedas en el suelo, la corrección del alabeo es entera hasta esta velocidad horizontal (cm/s, 45 km/h) y se
		 * apaga del todo en GroundRollZeroSpeedCms (70 km/h): despacio el giro cerrado no vuelca; muy rápido, girar fuerte
		 * vuelca el buggy por la transferencia de peso (#606) y el enderezado lo recupera. El cabeceo y el aire no cambian.
		 */
		float GroundRollFullSpeedCms = 1250.f;
		float GroundRollZeroSpeedCms = 1950.f;
	};

	/** Fracción (0..1) de la corrección del alabeo en el suelo a SpeedCms (FAntiRollTuning::GroundRollFullSpeedCms). */
	TORTUNABO_API float GroundRollHelp(float SpeedCms, const FAntiRollTuning& Tuning);

	/**
	 * Aceleración angular (rad/s², ejes del mundo) que devuelve el buggy hacia la vertical: muelle sobre el alabeo y el
	 * cabeceo que pasan de lo tolerado (en el aire no se tolera nada) y amortiguador sobre su velocidad. No toca la
	 * guiñada. Cero si ya está volcado (UpZ < FlippedUpZ: lo endereza DecideSelfRight) o si el ajuste es nulo. En el suelo, el
	 * alabeo se corrige por GroundRollHelp(SpeedCms): a mucha velocidad, nada.
	 */
	TORTUNABO_API FVector AntiRollAccel(const FVector& Forward, const FVector& Up, const FVector& AngularVelocityRad, bool bAirborne,
		const FAntiRollTuning& Tuning, float SpeedCms = 0.f);

	/** Botón de reaparecer: true el frame en que se cumplen HoldSeconds pulsado (una vez por pulsación). */
	struct FHold
	{
		float Held = 0.f;
		bool bFired = false;
	};

	TORTUNABO_API bool AdvanceHold(FHold& Hold, bool bPressed, float Dt, float HoldSeconds);

	// ── Tinte, charco y bamboleo ────────────────────────────────────────────────

	/** Color de la carrocería del equipo Index (8 colores que se repiten; los negativos, blanco). */
	TORTUNABO_API FLinearColor TeamColor(int32 Index);

	/**
	 * Deceleración (cm/s², hacia atrás) para no pasar de Cap: 0 por debajo, Gain × exceso por encima. La aplica cada
	 * máquina que simula el buggy mientras está en un charco.
	 */
	TORTUNABO_API float SpeedCapDecel(float Speed, float Cap, float Gain);

	/** Lo que limita la velocidad del buggy en un fotograma: charco, agua (#719) y vida perdida (#720). */
	struct FSpeedCapInput
	{
		/** Punta sin turbo y con el turbo a tope (cm/s), y fuerza del turbo en [0, 1]. */
		float TopSpeedCms = 0.f;
		float BoostTopSpeedCms = 0.f;
		float BoostStrength01 = 0.f;
		/** Tope del charco (cm/s); 0 = fuera del charco. */
		float PuddleCapCms = 0.f;
		/** Con las ruedas metidas en el agua, la punta sin turbo por WaterSpeedMultiplier. */
		bool bWading = false;
		float WaterSpeedMultiplier = 1.f;
		/** Fracción de la punta que deja la vida perdida (1 = sin daño): se aplica a la punta del turbo que lleve y al agua. */
		float DamageScale = 1.f;
	};

	/** Velocidad máxima (cm/s) que imponen el charco, el agua y el daño (la menor); 0 si nada la limita y manda el motor. */
	TORTUNABO_API float SpeedCapCms(const FSpeedCapInput& In);

	/** Fracción de una estadística (par, punta o giro) con la vida Health01 (#720): 1 con la vida llena y MinScale a 0. */
	TORTUNABO_API float DamageStatScale(float Health01, float MinScale);

	/** Dirección extra del bamboleo del coco: seno de Frequency Hz que se apaga linealmente hasta TimeLeft = 0. */
	TORTUNABO_API float SteerWobble(float TimeLeft, float Duration, float Amplitude, float Frequency);

	// ── Curvas de conducción (#288, #294; tests en Tortunabo.Rally.Drive.*) ─────

	/** Punto de una curva lineal a tramos. */
	struct FCurveKey
	{
		float X = 0.f;
		float Y = 0.f;
	};

	/** Interpolación lineal entre Keys (ordenadas por X); fuera del rango, el valor del extremo. 0 sin claves. */
	TORTUNABO_API float EvalLinearKeys(TConstArrayView<FCurveKey> Keys, float X);

	/**
	 * Ángulo máximo de dirección de la rueda interior (grados), el mismo a cualquier velocidad (#606, decisión del director del
	 * 03-10: como un buggy de verdad; girar fuerte muy rápido puede volcar y el enderezado lo recupera). Es el MaxSteerAngle de
	 * UTN_BuggyWheelFront; UTN_BuggyData::MaxSteerAngleDeg lo ajusta en ejecución.
	 */
	constexpr float DefaultSteerAngleDeg = 38.f;
	/** Fracción del ángulo de la rueda exterior (ESteeringType::AngleRatio de Chaos, como una geometría Ackermann). */
	constexpr float SteerAngleRatio = 0.7f;
	/** Batalla de SK_TN_BuggyChassis (cm): del eje delantero (X 168,3) al trasero (X -135,2). */
	constexpr float WheelbaseCm = 303.5f;

	/**
	 * Curva de dirección: X = velocidad de avance (cm/s), Y = fracción del ángulo máximo. Plana a 1 (#606): antes bajaba de 40
	 * grados parado a 12 a punta y el buggy apenas giraba.
	 */
	TORTUNABO_API TConstArrayView<FCurveKey> SteerCurveKeys();

	/** Ángulo máximo de dirección (grados) a SpeedCms de avance con BaseAngleDeg parado (el signo no cuenta). */
	TORTUNABO_API float MaxSteerAngleDeg(float SpeedCms, float BaseAngleDeg = DefaultSteerAngleDeg);

	/**
	 * Radio de giro cinemático (cm, centro del eje trasero) con la rueda interior a InnerSteerDeg y la exterior a AngleRatio de
	 * ese ángulo: batalla / tan(media de los dos). Sin derrape; el radio real a 20 km/h sale algo mayor (subviraje).
	 */
	TORTUNABO_API float KinematicTurnRadiusCm(float WheelbaseCmIn, float InnerSteerDeg, float AngleRatio);

	/** Radio de giro medido (cm) a partir de la velocidad horizontal y la guiñada: v / |guiñada|. 0 si no gira. */
	TORTUNABO_API float TurnRadiusFromYawRate(float SpeedCms, float YawRateRad);

	/**
	 * Fracción de la dirección (0..1) que no pasa de MaxLateralAccelCms2 de aceleración lateral a SpeedCms con la geometría del
	 * buggy (ángulo = atan(batalla · a / v²)). La usa el piloto IA para no volcar en las curvas (#606); 1 despacio.
	 */
	TORTUNABO_API float SafeSteerFraction(float SpeedCms, float MaxSteerDeg, float WheelbaseCmIn, float MaxLateralAccelCms2);

	/** Par máximo de la versión anterior (N·m): la curva nueva da el mismo par absoluto desde el 80 % de MaxRPM. */
	constexpr float LegacyMaxTorque = 850.f;

	/**
	 * Curva de par: X = fracción de MaxRPM, Y = fracción del par máximo (su máximo es 1, porque Chaos la normaliza).
	 * Plana a 1 desde el 10 % hasta el 55 % (todo el arranque hasta 60 km/h; el ralentí de Chaos ya está en el 35 %) y,
	 * desde el 80 %, el mismo par absoluto que la curva antigua con LegacyMaxTorque: la punta no cambia.
	 */
	TORTUNABO_API TArray<FCurveKey> TorqueCurveKeys(float MaxTorque);

	/** Curva antigua (OffroadCar_TorqueCurve de TP_VehicleAdvBP, interpolada lineal) para comparar en los tests. */
	TORTUNABO_API TConstArrayView<FCurveKey> LegacyTorqueCurveKeys();

	// ── Parrilla (#611) ─────────────────────────────────────────────────────────

	struct FGridHoldTuning
	{
		/** Ganancia (1/s) con la que se devuelve el buggy a su sitio: con 12, en una rampa de 15 grados se aparta ~0,4 cm. */
		float PositionGain = 12.f;
		/** Tope de la velocidad de corrección (cm/s). */
		float MaxCorrectionCms = 200.f;
		/** Más lejos de su sitio que esto (cm) es otro sitio (lo han recolocado): se vuelve a anclar. */
		float ReanchorDistanceCm = 200.f;
	};

	/**
	 * Velocidad que deja el buggy quieto en su hueco de salida con el freno de la carrera (#611): conserva la componente a lo
	 * largo de Up (la suspensión se asienta) y cambia la del plano del suelo por -PositionGain × la deriva en ese plano
	 * (DriftCm = posición - ancla), con tope. Así no rueda cuesta abajo aunque la rueda bloqueada resbale.
	 */
	TORTUNABO_API FVector GridHoldVelocity(const FVector& Velocity, const FVector& Up, const FVector& DriftCm, const FGridHoldTuning& Tuning);

	/** Velocidad angular sin el giro alrededor de Up (guiñada): el buggy frenado no rota en su sitio. */
	TORTUNABO_API FVector GridHoldAngularVelocity(const FVector& AngularVelocity, const FVector& Up);

	// ── Estabilidad (#288) ──────────────────────────────────────────────────────

	struct FStabilityTuning
	{
		/** Deriva (grados) a partir de la cual se corrige: por debajo, el giro normal no se toca. */
		float StartSlipDeg = 6.f;
		/** Muelle (1/s²): aceleración de guiñada por radián de deriva de más. */
		float Stiffness = 12.f;
		/** Amortiguador (1/s) sobre la guiñada que agranda la deriva. */
		float Damping = 3.f;
		/** Tope (rad/s²). */
		float MaxAccel = 8.f;
		/** Por debajo de esta velocidad (cm/s) no actúa: se puede girar sobre sí mismo parado. */
		float MinSpeedCms = 500.f;
	};

	/**
	 * Aceleración de guiñada (rad/s² sobre el eje vertical del buggy, positiva a la derecha) que devuelve el morro hacia
	 * la velocidad cuando la deriva pasa de StartSlipDeg: un control de estabilidad. Cero con el freno de mano (el
	 * derrape largo es suyo), en el aire, despacio o con más de MaxAssistedSlipDeg de deriva (trompo o marcha atrás).
	 * SlipDeg es el de SlipAngleDeg; YawRateRad, la velocidad de guiñada (positiva a la derecha).
	 */
	TORTUNABO_API float StabilityYawAccel(float SlipDeg, float YawRateRad, float SpeedCms, bool bHandbrake, bool bAirborne,
		const FStabilityTuning& Tuning);

	// ── Turbo (#294) ────────────────────────────────────────────────────────────

	struct FBoostTuning
	{
		/** Gasto con el turbo pisado (barra por segundo): una barra llena dura 3 s. */
		float DrainPerSecond = 1.f / 3.f;
		/** Recarga derrapando con el freno de mano (barra por segundo). */
		float DriftRechargePerSecond = 0.3f;
		/** Recarga en el aire (barra por segundo). */
		float AirRechargePerSecond = 0.4f;
		/** Deriva mínima (grados) para que el derrape recargue. */
		float MinDriftSlipDeg = 20.f;
		/** Velocidad mínima (cm/s) para recargar: ni volcado ni cayendo parado. */
		float MinRechargeSpeedCms = 800.f;
	};

	struct FBoostInput
	{
		bool bWantBoost = false;
		bool bEngineLocked = false;
		bool bHandbrake = false;
		bool bAirborne = false;
		float SlipDeg = 0.f;
		float SpeedCms = 0.f;
	};

	struct FBoostStep
	{
		float Charge01 = 0.f;
		bool bActive = false;
	};

	/** Recarga por segundo que toca ahora (0 si no derrapa con el freno de mano ni vuela lo bastante rápido). */
	TORTUNABO_API float BoostRechargeRate(const FBoostInput& In, const FBoostTuning& Tuning);

	/** Un paso del turbo: activo si se pide, hay carga y el motor no está cortado; gasta si está activo y, si no, recarga. */
	TORTUNABO_API FBoostStep AdvanceBoost(float Charge01, const FBoostInput& In, float Dt, const FBoostTuning& Tuning);

	/**
	 * Empuje del turbo (cm/s², hacia delante) para que la punta suba a BoostTopSpeedCms: PushAccel hasta FadeBandCms por
	 * debajo de esa punta y 0 al llegar a ella o al ir marcha atrás.
	 */
	TORTUNABO_API float BoostPushAccel(float ForwardSpeedCms, float BoostTopSpeedCms, float PushAccel, float FadeBandCms);

	// ── Turbo progresivo (#630) ─────────────────────────────────────────────────

	struct FBoostRampTuning
	{
		/** Segundos con el turbo pisado hasta el empuje completo y segundos en apagarse del todo al soltarlo. */
		float UpSeconds = 0.9f;
		float DownSeconds = 0.4f;
		/** Forma de la subida sin curva propia: fuerza = avance ^ Exponent (1 = recta; más, empieza más suave). */
		float Exponent = 1.5f;
	};

	/**
	 * Avance lineal de la rampa del turbo en [0, 1]: sube en UpSeconds mientras empuja y baja en DownSeconds cuando no. Con
	 * el motor cortado (bEngineLocked) cae a 0 de golpe. Igual en el servidor, en la conductora local y en el resto (cada
	 * máquina lo avanza con su IsBoosting).
	 */
	TORTUNABO_API float AdvanceBoostRamp(float Progress01, bool bBoosting, bool bEngineLocked, float Dt, const FBoostRampTuning& Tuning);

	/** Fuerza del turbo (0..1) para un avance de la rampa sin curva propia: Progress ^ Exponent. */
	TORTUNABO_API float BoostRampStrength(float Progress01, float Exponent);

	/** Multiplicador del par con el turbo a Strength (0..1): de 1 a TorqueMultiplier. */
	TORTUNABO_API float BoostTorqueScale(float Strength01, float TorqueMultiplier);

	// ── Llama del turbo (#294) ──────────────────────────────────────────────────

	/** Cono básico de /Engine/BasicShapes: 100 cm de alto y de diámetro, centrado en el origen y con la punta en +Z. */
	constexpr float BasicConeSizeCm = 100.f;

	/**
	 * Transformación (relativa a la carrocería) del cono básico como llama: la base, de DiameterCm, en ExhaustLocal y la
	 * punta a LengthCm * Flicker en la dirección DirLocal (se normaliza; nula = hacia atrás). Flicker se limita a [0,1; 2].
	 */
	TORTUNABO_API FTransform BoostFlameTransform(const FVector& ExhaustLocal, const FVector& DirLocal, float LengthCm,
		float DiameterCm, float Flicker);

	/** Parpadeo del largo de la llama, en [1 - Amount, 1 + Amount] (Amount en [0; 0,9]), sin repetición visible. */
	TORTUNABO_API float BoostFlameFlicker(float TimeSeconds, float Amount);

	// ── Cámara de la conductora (#298) ──────────────────────────────────────────

	struct FDriverCameraTuning
	{
		/** Persecución de HellYeah: FOV y brazo (cm) parado y a FullSpeedCms, y altura del encuadre (cm). */
		float BaseFov = 90.f;
		float MaxFov = 100.f;
		float BaseArmCm = 780.f;
		float MaxArmCm = 860.f;
		float FullSpeedCms = 3000.f;
		float SocketHeightCm = 100.f;
		/** Desplazamiento lateral hacia el interior de la curva (cm por grado/s de guiñada) y su tope. */
		float LeadCmPerDegPerSec = 1.6f;
		float MaxLeadCm = 140.f;
		/** Velocidad (cm/s) a la que el desplazamiento lateral llega a su valor completo. */
		float LeadFullSpeedCms = 1200.f;
		/** Frenada (cm/s² de deceleración) a la que empieza a acercarse y a la que llega al todo. */
		float BrakeStartDecel = 700.f;
		float BrakeFullDecel = 2000.f;
		/** Cuánto se acorta el brazo (cm) y cuánto baja (cm) con la frenada completa. */
		float BrakeArmPullCm = 120.f;
		float BrakeDropCm = 40.f;
		/** Alabeo en el derrape: empieza a StartDeg de deriva y llega a MaxRollDeg a FullDeg. */
		float DriftRollStartDeg = 10.f;
		float DriftRollFullDeg = 40.f;
		float MaxRollDeg = 4.f;
		/** Suavizado (1/s) de los desplazamientos y del alabeo, y de la deceleración medida. */
		float PoseInterpSpeed = 3.f;
		float AccelInterpSpeed = 6.f;
		/** FOV extra con el turbo (grados) y su suavizado (1/s). */
		float BoostFovDeg = 8.f;
		float BoostFovInterpSpeed = 4.f;
		/** Sacudida: caída mínima y completa al aterrizar (cm/s hacia abajo), mínimo de tiempo en el aire (s). */
		float LandingMinFallCms = 400.f;
		float LandingFullFallCms = 1600.f;
		float LandingMinAirSeconds = 0.2f;
		/** Sacudida continua mínima con el turbo, olvido (por segundo) y amplitud máxima (cm y grados). */
		float BoostTrauma = 0.3f;
		float TraumaDecayPerSecond = 1.6f;
		float MaxShakeCm = 14.f;
		float MaxShakeDeg = 1.2f;
	};

	struct FDriverCameraInput
	{
		float Dt = 0.f;
		/** Velocidad de avance (cm/s, negativa marcha atrás). */
		float ForwardSpeedCms = 0.f;
		/** Guiñada (grados/s, positiva a la derecha). */
		float YawRateDegPerSec = 0.f;
		float SlipDeg = 0.f;
		/** Velocidad vertical del chasis (cm/s, positiva hacia arriba). */
		float VerticalSpeedCms = 0.f;
		bool bAirborne = false;
		bool bBoosting = false;
		/** Fuerza del turbo (0..1, #630): escala el FOV extra y la sacudida del turbo. */
		float BoostStrength01 = 1.f;
		/** Sacudida externa de este fotograma (impactos, disparos), 0..1. */
		float AddedTrauma = 0.f;
	};

	/** Estado suavizado de la cámara de la conductora. */
	struct FDriverCameraState
	{
		/** Desplazamiento lateral del encuadre (cm, positivo a la derecha). */
		float LateralCm = 0.f;
		/** Cambio del brazo (cm, negativo = más cerca) y de la altura (cm, negativo = más baja). */
		float ArmDeltaCm = 0.f;
		float HeightDeltaCm = 0.f;
		float RollDeg = 0.f;
		float BoostFovDeg = 0.f;
		/** Sacudida en [0, 1]: la amplitud va con su cuadrado. */
		float Trauma = 0.f;
		/** Aceleración longitudinal suavizada (cm/s², negativa al frenar). */
		float LongAccel = 0.f;
		float PrevForwardSpeedCms = 0.f;
		float AirSeconds = 0.f;
		/** Mayor velocidad de caída (cm/s, positiva) del vuelo en curso. */
		float FallSpeedCms = 0.f;
		bool bHasPrevSpeed = false;
	};

	/** Ajuste por defecto de la cámara de la conductora (ATN_Buggy lo usa al construir y en cada fotograma). */
	TORTUNABO_API const FDriverCameraTuning& DefaultDriverCamera();

	/** Sacudida (0..1) de un aterrizaje tras AirSeconds en el aire cayendo a FallSpeedCms. */
	TORTUNABO_API float LandingTrauma(float FallSpeedCms, float AirSeconds, const FDriverCameraTuning& Tuning);

	/** Avanza la cámara un fotograma: objetivo (curva, frenada, derrape, turbo), suavizado y sacudida. */
	TORTUNABO_API FDriverCameraState AdvanceDriverCamera(const FDriverCameraState& State, const FDriverCameraInput& In,
		const FDriverCameraTuning& Tuning);

	/** Desplazamiento de la sacudida (cm en X, Y, Z del brazo): suma de senos sin repetición visible, amplitud Trauma². */
	TORTUNABO_API FVector ShakeOffset(float Trauma, float TimeSeconds, float MaxShakeCm);

	/** Giro de la sacudida (grados de cabeceo y guiñada en X e Y; Z = 0), amplitud Trauma². */
	TORTUNABO_API FVector ShakeRotation(float Trauma, float TimeSeconds, float MaxShakeDeg);
}
