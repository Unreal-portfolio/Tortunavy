// Reglas puras de la carrera del Rally (sin mundo ni actores): orden de puestos, puertas, vueltas, contramano, atasco,
// fuera de pista, puntos, reparto de munición especial y lectura del manifest de la variante. Las usan ATN_RallyGameMode,
// ATN_RallyTrack y ATN_RallyAIController, y las prueban los tests Tortunabo.Rally.Logic.* (Docs/Rally_MVP.md).
#pragma once

#include "CoreMinimal.h"
#include "Rally/TN_RallyCircuit.h"
#include "Rally/TN_RallyVehicle.h"

TORTUNABO_API DECLARE_LOG_CATEGORY_EXTERN(LogTNRally, Log, All);

namespace TNRally
{
	// ---- Constantes de reglas (Docs/superpowers/specs/modos/03-Rally.md §5) ----

	/** Fracción mínima de la spline entre dos puertas que hay que recorrer para que la segunda cuente (atajos). */
	inline constexpr double MinTravelFraction = 0.6;
	/** Contramano: producto escalar velocidad·tangente por debajo de este valor... */
	inline constexpr double WrongWayDot = -0.5;
	/** ...a más de esta velocidad... */
	inline constexpr double WrongWayMinKmh = 20.0;
	/** ...durante este tiempo enciende el aviso... */
	inline constexpr double WrongWayWarnSeconds = 1.5;
	/** ...y a este tiempo el servidor gira el buggy hacia la tangente. */
	inline constexpr double WrongWayTurnSeconds = 4.0;
	/** Atasco: menos de StuckRadiusCm de desplazamiento en StuckSeconds. */
	inline constexpr double StuckSeconds = 8.0;
	inline constexpr double StuckRadiusCm = 500.0;
	/** Fuera de pista: a más de esta distancia del eje durante OffTrackGraceSeconds. */
	inline constexpr double OffTrackDistanceCm = 4000.0;
	inline constexpr double OffTrackGraceSeconds = 1.0;
	/** Ventana de búsqueda del arco s alrededor del anterior (lazos y niveles superpuestos). */
	inline constexpr double ArcWindowBehindCm = 2000.0;
	inline constexpr double ArcWindowAheadCm = 12000.0;
	/** Salida anticipada: desplazamiento hacia delante antes del verde que la delata. */
	inline constexpr double EarlyStartDisplacementCm = 100.0;
	/** Parrilla 2 × 4: primera fila a 10 m de la salida, 8 m entre filas y 3,5 m a cada lado del eje. */
	inline constexpr int32 MaxGridSlots = 8;
	inline constexpr double GridFirstRowBackCm = 1000.0;
	inline constexpr double GridRowSpacingCm = 800.0;
	inline constexpr double GridHalfSpacingCm = 350.0;

	inline double CmsToKmh(double Cms) { return Cms * 0.036; }

	// ---- Manifest de la variante ----

	/** Una puerta: centro a la cota de la calzada y rumbo (yaw, grados) en el sentido de la carrera. */
	struct FGateDef
	{
		FVector Location = FVector::ZeroVector;
		double YawDeg = 0.0;
		/** False si el manifest no trae rumbo: BuildGateList lo deduce de las puertas vecinas. */
		bool bHasYaw = true;
	};

	/** Lo que la carrera lee del manifest.json de una variante (Scripts/terrain_volumes/Variants/<v>/). */
	struct FTrackSource
	{
		TArray<FGateDef> Checkpoints;
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		bool bHasStart = false;
		bool bHasEnd = false;
		/** Cota del agua (water_uu): por debajo, el buggy reaparece. */
		bool bHasWater = false;
		double WaterZ = 0.0;
		/** Eje de la calzada (road_uu, un punto por metro); vacío si el manifest no lo trae. */
		TArray<FVector> Road;
		/** closed del manifest: si viene, manda sobre la comparación de start_uu con end_uu. */
		bool bHasClosed = false;
		bool bClosed = false;
		/** laps del manifest (0 = no viene). */
		int32 Laps = 0;
		/** start_yaw: rumbo de la salida cuando no es uno de los checkpoints. */
		bool bHasStartYaw = false;
		double StartYawDeg = 0.0;
		/** road_width_m en cm (0 = no viene). En los circuitos con ancho por tramos es el máximo. */
		double RoadWidthCm = 0.0;
		/** road_widths_m: ancho de la calzada en cada punto de Road (cm; «Tramos variados», #622). Vacío si no viene. */
		TArray<double> RoadWidthsCm;
		/** bank_deg: peralte de cada punto de Road (grados; positivo, la derecha más baja). Vacío si no viene. */
		TArray<double> RoadBankDeg;
		/** elements: saltos, rasantes, horquillas... de los circuitos generados (#622). Vacío si no viene. */
		TArray<TNRallyCircuit::FElement> Elements;
	};

	/** Ruta del manifest de una variante (solo editor y PIE: Scripts/ no se empaqueta). */
	TORTUNABO_API FString VariantManifestPath(FName Variant);

	/**
	 * Lee checkpoints_uu, start_uu, end_uu, water_uu y, si vienen, road_uu, closed, laps, start_yaw, road_width_m, road_widths_m, bank_deg y
	 * elements. False (con OutError) si el JSON no vale o no hay ni puertas ni salida.
	 */
	TORTUNABO_API bool ParseTrackManifest(const FString& JsonText, FTrackSource& Out, FString& OutError);

	/** Circuito: closed del manifest o, si no viene, salida y meta en el mismo sitio (a menos de 1 m). */
	TORTUNABO_API bool IsCircuit(const FTrackSource& Source);

	/**
	 * Lista ordenada de puertas. La puerta 0 es la salida (en circuito también la meta). Si los checkpoints no empiezan en
	 * start_uu se antepone la salida y, en punto a punto, se añade end_uu como meta si falta. Rumbo de las puertas
	 * añadidas: hacia la siguiente (o desde la anterior en la última).
	 */
	TORTUNABO_API TArray<FGateDef> BuildGateList(const FTrackSource& Source, bool& bOutCircuit);

	// ---- Vueltas y puertas ----

	/**
	 * Progreso contado en puertas validadas desde la salida (GatesPassed), que ordena a la vez vuelta y puerta. En circuito,
	 * cruzar la salida (puerta 0) la primera vez abre la vuelta 1 y no cuenta como vuelta; cada vuelta son NumGates puertas
	 * más y la última es otra vez la 0 (la meta). En punto a punto hay una sola vuelta y la meta es la última puerta.
	 */
	struct TORTUNABO_API FLapRules
	{
		int32 NumGates = 0;
		int32 Laps = 1;
		bool bCircuit = false;

		int32 GatesToFinish() const;
		int32 NextGateIndex(int32 GatesPassed) const;
		/** Vuelta en curso (1..Laps); 0 antes de cruzar la salida. */
		int32 LapForGates(int32 GatesPassed) const;
		bool IsFinished(int32 GatesPassed) const;
		/** La puerta que se acaba de validar con GatesPassed puertas: la de reaparición (-1 si ninguna). */
		int32 LastGateIndex(int32 GatesPassed) const;
	};

	enum class EGateCheck : uint8
	{
		Valid,
		/** No es la siguiente en orden (se ha saltado una). */
		WrongGate,
		/** Cruzada en sentido contrario. */
		WrongDirection,
		/** Recorrido desde la puerta anterior menor que el 60 % de la spline entre ambas. */
		Shortcut
	};

	/** Valida el paso por una puerta. La regla del 60 % solo se aplica si bApplyShortcutRule (no en la salida). */
	TORTUNABO_API EGateCheck CheckGate(int32 GateIndex, int32 ExpectedGate, bool bForward, double TraveledCm,
		double SplineCmBetweenGates, bool bApplyShortcutRule);

	/**
	 * ¿El segmento Prev→Cur atraviesa el rectángulo de la puerta (plano X local, semiextensiones Y y Z de HalfExtent)?
	 * OutAlpha es la fracción del segmento en el cruce y bOutForward si va en el sentido del eje X de la puerta.
	 */
	TORTUNABO_API bool SegmentCrossesGate(const FVector& Prev, const FVector& Cur, const FTransform& Gate, const FVector& HalfExtent,
		double& OutAlpha, bool& bOutForward);

	// ---- Puestos y puntos ----

	struct FStandingKey
	{
		/** Desempate estable (menor primero): índice del equipo. */
		int32 Id = 0;
		bool bFinished = false;
		/** Segundos desde la salida al cruzar la meta. */
		double FinishTime = 0.0;
		int32 Lap = 0;
		/** Puertas validadas desde la salida (vuelta y puerta a la vez). */
		int32 GatesPassed = 0;
		/** Arco recorrido desde la última puerta validada (cm). */
		double SegmentProgressCm = 0.0;
		/** Retirado (sin ocupantes en carrera): siempre detrás. */
		bool bRetired = false;
	};

	/** Índices de Keys en orden de puesto: terminados por tiempo, luego vuelta, puerta y arco; retirados al final. */
	TORTUNABO_API TArray<int32> SortStandings(const TArray<FStandingKey>& Keys);

	/** Puntos de copa por puesto: 10-8-6-5-4-3-2-1; 0 si no terminó o si es 9.º o peor. */
	TORTUNABO_API int32 PointsForPlace(int32 Place, bool bFinished);

	// ---- Estados temporizados del servidor ----

	struct FWrongWayState
	{
		double Seconds = 0.0;
		bool bWarning = false;
	};

	enum class EWrongWayEvent : uint8
	{
		None,
		WarningOn,
		WarningOff,
		/** 4 s en contramano: el servidor gira el buggy hacia la tangente (el estado se reinicia). */
		TurnAround
	};

	TORTUNABO_API EWrongWayEvent UpdateWrongWay(FWrongWayState& State, double DotVelocityTangent, double SpeedKmh, double DeltaSeconds);

	struct FStuckState
	{
		FVector Anchor = FVector::ZeroVector;
		double Seconds = 0.0;
		bool bHasAnchor = false;
	};

	/** True si lleva StuckSeconds sin salir de un radio de StuckRadiusCm (y reinicia el estado). */
	TORTUNABO_API bool UpdateStuck(FStuckState& State, const FVector& Position, double DeltaSeconds);

	struct FOffTrackState
	{
		double Seconds = 0.0;
	};

	/** True si lleva OffTrackGraceSeconds a más de OffTrackDistanceCm del eje (y reinicia el estado). */
	TORTUNABO_API bool UpdateOffTrack(FOffTrackState& State, double DistanceToAxisCm, double DeltaSeconds);

	// ---- Munición especial: cajas «?» (#629) ----

	struct FAmmoWeights
	{
		float Alga = 0.f;
		float Burbuja = 0.f;
		float Mortero = 0.f;
		float Tinta = 0.f;
		float Ancla = 0.f;
		float Concha = 0.f;
		float ConchaGuiada = 0.f;

		float Total() const { return Alga + Burbuja + Mortero + Tinta + Ancla + Concha + ConchaGuiada; }
	};

	/**
	 * Reparto de las cajas «?» por puesto (solo munición de la torreta; nada de turbos ni estrellas): los primeros sacan
	 * más Alga, Tinta y Concha; los últimos, más Mortero, Burbuja y Concha teledirigida. El Ancla es rara en todos los
	 * puestos (menos que el Mortero) y, como él, algo más frecuente para los últimos.
	 */
	TORTUNABO_API FAmmoWeights AmmoWeightsForPlace(int32 Place, int32 NumTeams);

	/** Elige munición con una tirada en [0, 1). Nunca Coco ni None. */
	TORTUNABO_API ETNRallyAmmo PickAmmo(const FAmmoWeights& Weights, float Roll01);

	/** Cargas por caja: Alga 2, Burbuja 1, Mortero 1, Tinta 2, Ancla 2, Concha 2, Concha teledirigida 1. */
	TORTUNABO_API int32 ChargesFor(ETNRallyAmmo Ammo);

	/** Hacia dónde dispara un bot su munición especial (ShouldBotFireSpecial). */
	enum class EBotSpecialShot : uint8
	{
		/** Aún no: se la guarda. */
		Hold,
		/** Al buggy de justo delante. */
		AtAhead,
		/** Al buggy de justo detrás (el alga, que deja el charco en su camino). */
		AtBehind,
		/** Hacia delante sin blanco (la burbuja, que recoge él mismo, o la que lleva demasiado tiempo guardada). */
		Free
	};

	/** Pasado esto con una especial cargada, el bot la gasta aunque no venga a cuento (s). */
	inline constexpr float BotMaxHoldSeconds = 8.f;
	/** Alcance de los bots con las especiales hacia delante (cm) y con el alga hacia atrás (cm). */
	inline constexpr float BotSpecialRangeCm = 6000.f;
	inline constexpr float BotAlgaBehindRangeCm = 4000.f;
	/** La burbuja se suelta al rato de cogerla (s). */
	inline constexpr float BotBubbleDelaySeconds = 1.5f;

	/**
	 * Bot sin artillera humana (#629): qué hace con la especial Ammo que lleva HeldSeconds. AheadCm y BehindCm, distancia
	 * al buggy de justo delante y de justo detrás (negativa si no hay). Conchas, mortero, tinta y ancla, al de delante a
	 * menos de BotSpecialRangeCm; el alga, al de detrás a menos de BotAlgaBehindRangeCm (si no, al de delante); la burbuja,
	 * al rato. Pasados BotMaxHoldSeconds, la gasta igual.
	 */
	TORTUNABO_API EBotSpecialShot ShouldBotFireSpecial(ETNRallyAmmo Ammo, float HeldSeconds, float AheadCm, float BehindCm);

	// ---- Spline y parrilla ----

	/** Arco envuelto a [0, Length) en circuito o recortado a [0, Length] en punto a punto. */
	TORTUNABO_API double WrapArc(double S, double Length, bool bClosed);

	/** Distancia hacia delante de From a To (en circuito, dando la vuelta si hace falta). */
	TORTUNABO_API double ForwardArc(double From, double To, double Length, bool bClosed);

	/**
	 * Arcos de las filas de cajas de munición: a AfterGateCm de cada puerta par (menos la salida) y a mitad del tramo tras
	 * cada impar; en un punto a punto, ninguna en los últimos NoAmmoBeforeFinishCm. Ordenados por puerta. Igual en todas las
	 * máquinas (la pista no se replica): la tableta los marca en el perfil (#299) sin mirar las cajas.
	 */
	TORTUNABO_API TArray<double> AmmoRowArcs(const TArray<double>& GateArcs, double Length, bool bClosed, double AfterGateCm,
		double NoAmmoBeforeFinishCm);

	/**
	 * Arco más cercano a Point buscando solo en [PrevS - Behind, PrevS + Ahead] con muestras cada StepCm y afinado final,
	 * para que un buggy que pasa por debajo de otro tramo no salte de arco.
	 */
	TORTUNABO_API double FindArcInWindow(TFunctionRef<FVector(double)> PositionAt, double Length, bool bClosed, const FVector& Point,
		double PrevS, double BehindCm, double AheadCm, double StepCm);

	/**
	 * Eje de la calzada a un punto cada StepCm (siempre el primero y, en punto a punto, el último); en circuito quita el
	 * último si repite el primero. Para la spline: road_uu trae un punto por metro.
	 */
	TORTUNABO_API TArray<FVector> DownsampleRoad(const TArray<FVector>& Road, double StepCm, bool bClosed);

	/** Hueco de la parrilla: X = metros hacia atrás de la salida (cm), Y = desplazamiento lateral (cm, + a la derecha). */
	TORTUNABO_API FVector2D GridSlotOffset(int32 Slot);

	// ---- Piloto IA ----

	/** Dirección -1..1 para girar Forward hacia ToTarget (en el plano), saturando a MaxAngleDeg. */
	TORTUNABO_API float SteerToward(const FVector& Forward, const FVector& ToTarget, float MaxAngleDeg);

	/** Velocidad objetivo (km/h) para una curva: ángulo entre la tangente actual y la de más adelante. */
	TORTUNABO_API float CornerSpeedKmh(const FVector& TangentNow, const FVector& TangentAhead, float MaxKmh, float MinKmh);

	/**
	 * Velocidad (km/h) a la que se puede ir ahora para llegar a CornerKmh en DistanceCm frenando con DecelCms2 (cm/s²):
	 * sqrt(v² + 2·a·d). Con distancia 0, la de la curva. El piloto IA toma la menor de las curvas que vienen (#606).
	 */
	TORTUNABO_API float ApproachSpeedKmh(float CornerKmh, double DistanceCm, double DecelCms2);

	// ---- Equipos y armas ----

	/** Qué hace la carrera con un equipo: seguir, quitarlo (antes de la salida) o retirarlo (ya en marcha). */
	enum class ETeamCleanup : uint8
	{
		Keep,
		Remove,
		Retire
	};

	/**
	 * Un equipo sin buggy válido o con el buggy vacío (se han ido sus ocupantes) no sigue en carrera: antes de la salida
	 * se quita y libera su hueco; después se retira. Uno ya retirado se queda como está.
	 */
	TORTUNABO_API ETeamCleanup DecideTeamCleanup(bool bVehicleValid, bool bOccupied, bool bBeforeStart, bool bRetired);

	/** La torreta solo dispara con la carrera en marcha (Racing o Finishing) y el equipo sin retirar. */
	TORTUNABO_API bool AreWeaponsLive(bool bRaceRunning, bool bRetired);
}
