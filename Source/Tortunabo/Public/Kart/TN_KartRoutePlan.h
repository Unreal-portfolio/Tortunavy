// Pista de los karts sobre un camino ya hecho (el principal del mapa generado del cooperativo, ATN_ProcMapGenerator): de
// sus muestras salen el eje de la spline, las puertas cada GateSpacingCm (fuera de cuevas, estructuras y explanadas más
// anchas que el arco), la salida con la parrilla detrás en el claro inicial, la meta en la playa final, las filas de cajas
// de objetos y la línea del piloto IA alrededor de los obstáculos grandes. Lógica pura (sin mundo ni actores), con tests
// Tortunabo.Kart.Route.*. La usa ATN_KartTrack.
#pragma once

#include "CoreMinimal.h"
#include "Rally/TN_RallyLogic.h"

namespace TNKart
{
	// ---- Camino de entrada ----

	/** Una muestra del camino: centro a ras de suelo, ancho y qué se puede poner ahí. */
	struct FRouteSample
	{
		FVector Location = FVector::ZeroVector;
		double WidthCm = 0.0;
		/** Sin puerta aquí (cueva, puente, géiser, cascada, agua): el arco de la puerta no cabría o no se vería. */
		bool bNoGate = false;
		/** Ya en la playa final (la meta va en ella). */
		bool bShore = false;
	};

	struct FRoutePlanParams
	{
		/** Distancia entre puertas (cm). */
		double GateSpacingCm = 25000.0;
		/** Separación mínima entre dos puertas (cm): una que cae en una cueva se mueve, pero no tan cerca de la siguiente. */
		double MinGateGapCm = 12000.0;
		/** Cuánto se puede adelantar una puerta que cae donde no puede ir (cm); si no encuentra sitio, se quita. */
		double MaxGateShiftCm = 8000.0;
		/** Arco de la salida (puerta 0): la parrilla 2 × 4 queda detrás, en el claro inicial. */
		double StartGateArcCm = 4200.0;
		/** La meta, este trecho dentro de la playa final (cm). */
		double FinishIntoShoreCm = 2500.0;
		/** La meta no pasa de donde el suelo baja de esta cota (orilla del mar); por debajo de -1e9, sin límite. */
		double MinFinishZ = -1.0e10;
		/** Pista pasada la meta, para frenar (cm). */
		double RunOffCm = 6000.0;
		/** Las puertas del Rally miden 24 m: no van donde el camino es más ancho que esto (se rodearían). */
		double MaxGateRoadWidthCm = 2200.0;
		/** Separación de los puntos del eje (cm). */
		double RoadStepCm = 1000.0;
	};

	/** Lo que necesita ATN_RallyTrack: eje con su ancho, puertas (0 = salida, la última = meta) y su arco en el eje. */
	struct FRoutePlan
	{
		bool bValid = false;
		TArray<FVector> Road;
		/** Arco (cm, en 3D) de cada punto de Road desde el primero. */
		TArray<double> RoadArcCm;
		TArray<double> RoadWidthCm;
		TArray<TNRally::FGateDef> Gates;
		TArray<double> GateArcCm;
		double FinishArcCm = 0.0;
		double LengthCm = 0.0;
	};

	/** Arco acumulado (cm, en 3D) de una polilínea: 0 en el primer punto. */
	TORTUNABO_API TArray<double> CumulativeArc(const TArray<FVector>& Points);

	/**
	 * Arco de la meta: FinishIntoShoreCm dentro de la primera muestra de la playa final, sin pasar de donde el suelo baja de
	 * MinFinishZ ni del final del camino menos 10 m. Sin playa, el final del camino menos RunOffCm.
	 */
	TORTUNABO_API double FinishArcForPath(const TArray<FRouteSample>& Samples, const TArray<double>& Arc, const FRoutePlanParams& Params);

	/**
	 * Arcos de las puertas: la salida en StartArc, una cada GateSpacingCm (si cae donde IsForbidden, se adelanta hasta
	 * MaxGateShiftCm o, si no hay sitio, se retrasa otro tanto; si tampoco, esa no se pone) sin acercarse a menos de
	 * MinGateGapCm de la anterior ni de la meta, y la meta en FinishArc. Siempre al menos la salida y la meta si
	 * FinishArc > StartArc.
	 */
	TORTUNABO_API TArray<double> PlanGateArcs(double StartArc, double FinishArc, const FRoutePlanParams& Params,
		TFunctionRef<bool(double)> IsForbidden);

	/** El plan completo; bValid = false si el camino no da para salida y meta (menos de dos muestras o muy corto). */
	TORTUNABO_API FRoutePlan PlanRouteFromPath(const TArray<FRouteSample>& Samples, const FRoutePlanParams& Params);

	// ---- Cajas de objetos ----

	/**
	 * Arcos de las filas de cajas entre StartArc y EndArc: la primera a media separación de la salida y luego cada
	 * SpacingCm; una fila a menos de MinFromGateCm de una puerta se adelanta lo justo (no tapa el arco ni la reaparición).
	 */
	TORTUNABO_API TArray<double> PlanItemRowArcs(double StartArc, double EndArc, const TArray<double>& GateArcs, double SpacingCm,
		double MinFromGateCm);

	/** Cajas de una fila en una calzada de ese ancho: 2 en las estrechas, hasta 6 en las explanadas. */
	TORTUNABO_API int32 ItemBoxesForWidth(double RoadWidthCm);

	/** Separación lateral entre las cajas de una fila (cm). */
	TORTUNABO_API double ItemLateralSpacingCm(double RoadWidthCm, int32 Boxes);

	// ---- Línea del piloto IA ----

	/**
	 * Obstáculo dentro del camino (pieza de explanada, aguja de roca): centro en planta y radio (cm). Con bKeepCentered es
	 * un arco que cruza el camino (sus pies están en los bordes y es más bajo a los lados): la línea pasa por el centro a lo
	 * largo de RadiusCm.
	 */
	struct FLineObstacle
	{
		FVector2D Center = FVector2D::ZeroVector;
		double RadiusCm = 0.0;
		bool bKeepCentered = false;
	};

	/**
	 * Eje desplazado: cada punto de Road movido Offsets (cm, + a la derecha) en horizontal. La spline de la pista de los karts
	 * es la línea del piloto IA, así el piloto (que sigue la spline) rodea los obstáculos sin cambiar nada del Rally.
	 */
	TORTUNABO_API TArray<FVector> OffsetRoad(const TArray<FVector>& Road, const TArray<double>& Offsets);

	/**
	 * Desplazamiento lateral (cm, + a la derecha) de la línea del piloto IA en cada punto del eje: junto a un obstáculo (a lo
	 * largo, a menos de su radio más ClearanceCm), el sitio libre más cercano al eje a ClearanceCm de todos los que tiene al
	 * lado; entre dos obstáculos, de uno a otro en línea recta, y al entrar y salir, rampa de RampCm hasta el eje. Nunca más
	 * allá del semiancho menos 2,5 m. Sin obstáculos, todo 0.
	 */
	TORTUNABO_API TArray<double> PlanRacingLineOffsets(const TArray<FVector>& Road, const TArray<double>& HalfWidthsCm,
		const TArray<FLineObstacle>& Obstacles, double ClearanceCm, double RampCm);
}
