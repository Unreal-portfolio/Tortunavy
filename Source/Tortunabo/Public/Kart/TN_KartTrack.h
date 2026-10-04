// Pista de los karts sobre el mapa generado del cooperativo (#291): el camino principal de ATN_ProcMapGenerator pasa por
// TNKart::PlanRouteFromPath (puertas cada 250 m con la regla del 60 %, salida en el claro inicial y meta en la playa
// final) y la spline de ATN_RallyTrack es la línea del piloto IA (TNKart::PlanRacingLineOffsets), que rodea los obstáculos
// del camino y pasa centrada por las puertas, los arcos y los géiseres. A los lados de cada puerta, un ala de rocas hasta
// el borde del camino: nadie se salta una puerta por fuera del arco (en la salida y en la playa el camino es más ancho que
// él). Sin cajas de munición del Rally: las filas de cajas de objetos (ATN_KartItemBox) las pone el servidor. Cada máquina
// la construye con su generador (el mismo mapa con la semilla replicada), así que todas tienen la misma pista.
#pragma once

#include "CoreMinimal.h"
#include "Kart/TN_KartRoutePlan.h"
#include "Rally/TN_RallyTrack.h"
#include "TN_KartTrack.generated.h"

class ATN_KartItemBox;
class ATN_ProcMapGenerator;
struct FTNProcPathPoint;

namespace TNKart
{
	/** Parámetros del plan: puertas cada 250 m, salida a 42 m del principio y meta en la playa, por encima del mar. */
	TORTUNABO_API FRoutePlanParams MakePlanParams(double SeaLevelZ);

	/** Sin puertas a menos de esto (cm, a lo largo del camino) del agua, las cascadas, los géiseres y los huecos. */
	inline constexpr double GateAwayFromHazardCm = 2500.0;

	/**
	 * Muestras del plan desde el camino del generador: sin puertas en cuevas ni estructuras, ni cerca del agua, las cascadas
	 * y los géiseres (GateAwayFromHazardCm); la playa final, marcada.
	 */
	TORTUNABO_API TArray<FRouteSample> RouteSamplesFrom(const TArray<FTNProcPathPoint>& Points);

	/**
	 * Sin puerta a menos de ClearCm (más su radio) de un obstáculo que la línea del piloto rodea: así la línea pasa por el
	 * centro de todas las puertas y el arco no se planta junto a una roca.
	 */
	TORTUNABO_API void BlockGatesNearObstacles(TArray<FRouteSample>& Samples, const TArray<FLineObstacle>& Obstacles, double ClearCm);
}

UCLASS()
class TORTUNABO_API ATN_KartTrack : public ATN_RallyTrack
{
	GENERATED_BODY()

public:
	ATN_KartTrack();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Construye la pista con el mapa que tiene Generator en esta máquina. False si no hay mapa o no da para una pista. */
	bool BuildFromMap(ATN_ProcMapGenerator& Generator);

	/** Generación del mapa con la que se hizo (0 = sin pista). */
	int32 GetMapGeneration() const { return MapGeneration; }

	/** Arco de la meta en la spline (cm). */
	double GetFinishArc() const { return GetGateArc(GetGateCount() - 1); }

	/** Semiancho del camino en ese arco de la spline (cm). */
	double GetRoadHalfWidthAtArc(double Arc) const;

	/** Cajas de objetos (solo en el servidor; los clientes las reciben replicadas). */
	const TArray<TObjectPtr<ATN_KartItemBox>>& GetItemBoxes() const { return ItemBoxes; }

	/** Separación entre filas de cajas (cm). */
	UPROPERTY(EditAnywhere, Category = "Karts|Cajas", meta = (ClampMin = "5000"))
	float ItemRowSpacingCm = 30000.f;

	/** Las filas no van a menos de esto de una puerta (cm). */
	UPROPERTY(EditAnywhere, Category = "Karts|Cajas")
	float ItemRowMinFromGateCm = 3000.f;

	/** Holgura de la línea del piloto IA alrededor de los obstáculos (cm) y largo de la rampa para volver al eje (cm). */
	UPROPERTY(EditAnywhere, Category = "Karts|Línea")
	float LineClearanceCm = 450.f;

	UPROPERTY(EditAnywhere, Category = "Karts|Línea")
	float LineRampCm = 3000.f;

	/** Sin puertas a menos de esto de un obstáculo (cm, más su radio). */
	UPROPERTY(EditAnywhere, Category = "Karts|Línea")
	float GateObstacleClearCm = 2500.f;

	/** La línea pasa por el centro de cada puerta en este radio (cm). */
	UPROPERTY(EditAnywhere, Category = "Karts|Línea")
	float GateCenteredRadiusCm = 1200.f;

	/** Alas de rocas de las puertas: largo mínimo y máximo de cada una (cm); llegan hasta el borde del camino más 4 m. */
	UPROPERTY(EditAnywhere, Category = "Karts|Puertas")
	float GateWingMinCm = 800.f;

	UPROPERTY(EditAnywhere, Category = "Karts|Puertas")
	float GateWingMaxCm = 3000.f;

	/** Separación de las rocas de las alas (cm). */
	UPROPERTY(EditAnywhere, Category = "Karts|Puertas", meta = (ClampMin = "60"))
	float GateWingSpacingCm = 130.f;

protected:
	UPROPERTY(EditAnywhere, Category = "Karts|Cajas")
	TSubclassOf<ATN_KartItemBox> ItemBoxClass;

private:
	/** Servidor: las filas de cajas de objetos a lo ancho del camino. */
	void SpawnItemRows(const TNKart::FRoutePlan& Plan, double LineStartArc, const ATN_ProcMapGenerator& Generator);
	void ClearItemBoxes();
	/** Las alas de rocas a los lados de cada puerta (todas las máquinas: tienen colisión). */
	void BuildGateWings(const TNKart::FRoutePlan& Plan, const ATN_ProcMapGenerator& Generator);

	UPROPERTY(Transient)
	TArray<TObjectPtr<ATN_KartItemBox>> ItemBoxes;

	/** Arco (en el eje del plan) y semiancho de cada punto del eje, para GetRoadHalfWidthAtArc. */
	TArray<double> RoadArcs;
	TArray<double> RoadHalfWidths;
	int32 MapGeneration = 0;
};
