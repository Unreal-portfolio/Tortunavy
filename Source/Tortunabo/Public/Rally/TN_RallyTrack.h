// Pista del Rally: spline del eje, puertas, arco de meta, bordes, parrilla y cajas «?» (ATN_KartItemBox, #629). Se construye con los
// checkpoints_uu del manifest de la variante (ATN_MapVariantLoader) o, si no hay manifest, con los ATN_RallyCheckpoint
// colocados a mano. Cada máquina construye la suya con los mismos datos; solo el servidor pone las cajas (replicadas).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rally/TN_RallyLogic.h"
#include "TN_RallyTrack.generated.h"

class USplineComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;
class ATN_RallyGate;
class ATN_KartItemBox;

/** Reglas puras de la parrilla y la reaparición (Tortunabo.Rally.Race.*): sin mundo, para poder probarlas. */
namespace TNRallyRace
{
	/** Carriles de reaparición tras cada puerta: centro, izquierda y derecha, en ese orden de preferencia. */
	inline constexpr int32 RespawnLaneCount = 3;
	/** Separación lateral entre carriles de reaparición (cm). */
	inline constexpr double RespawnLaneSpacingCm = 350.0;
	/** Un carril está libre si no hay otro buggy a menos de esto de su punto de reaparición (cm). */
	inline constexpr double RespawnClearRadiusCm = 300.0;
	/** El fantasma dura al menos el bloqueo más esto: el buggy no recupera la colisión mientras sigue inmóvil (s). */
	inline constexpr float GhostAfterLockSeconds = 0.5f;
	/** Holgura sobre el suelo al aparecer en la parrilla o al reaparecer (cm). */
	inline constexpr double SpawnClearanceCm = 5.0;
	/** Tope de la altura del origen sobre el punto más bajo del vehículo: más es una medida rota (cm). */
	inline constexpr double MaxOriginAboveBottomCm = 300.0;
	/** Altura que se usa si no se puede medir el vehículo (la de antes de #289) (cm). */
	inline constexpr double FallbackOriginAboveBottomCm = 75.0;

	/** Desplazamiento lateral del carril (0 = centro, 1 = izquierda, 2 = derecha; módulo RespawnLaneCount). */
	TORTUNABO_API double RespawnLaneLateralCm(int32 Lane, double SpacingCm = RespawnLaneSpacingCm);

	/**
	 * Primer carril (en orden de preferencia) sin ningún Occupied a menos de ClearRadiusCm; si no hay ninguno libre, el de
	 * mayor distancia al buggy más cercano. INDEX_NONE sin carriles.
	 */
	TORTUNABO_API int32 PickFreeRespawnLane(const TArray<FVector>& LaneLocations, const TArray<FVector>& Occupied,
		double ClearRadiusCm = RespawnClearRadiusCm);

	/** Fantasma efectivo de una reaparición: nunca menos que el bloqueo más GhostAfterLockSeconds. */
	TORTUNABO_API float EffectiveGhostSeconds(float LockSeconds, float GhostSeconds);

	/** Altura del origen sobre el suelo para que el vehículo quede apoyado: lo medido (acotado) más la holgura. */
	TORTUNABO_API double RestingLiftCm(double OriginAboveBottomCm, double ClearanceCm = SpawnClearanceCm);
}

/** Checkpoint colocado a mano para circuitos sin manifest: el orden de las puertas lo da Order (0 = salida). */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_RallyCheckpoint : public AActor
{
	GENERATED_BODY()

public:
	ATN_RallyCheckpoint();

	UPROPERTY(EditAnywhere, Category = "Rally")
	int32 Order = 0;

	/** Marca la meta de un punto a punto. En circuito no hace falta (la meta es la puerta 0). */
	UPROPERTY(EditAnywhere, Category = "Rally")
	bool bIsFinish = false;
};

UCLASS(Blueprintable)
class TORTUNABO_API ATN_RallyTrack : public AActor
{
	GENERATED_BODY()

public:
	ATN_RallyTrack();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Construye desde Scripts/terrain_volumes/Variants/<Variant>/manifest.json; si no hay, desde los checkpoints colocados. */
	UFUNCTION(BlueprintCallable, Category = "Rally")
	bool BuildFromVariant(FName Variant);

	/** Igual que BuildFromVariant con la ruta del manifest (pruebas con variantes de otra copia del repo). */
	UFUNCTION(BlueprintCallable, Category = "Rally")
	bool BuildFromManifestFile(const FString& ManifestPath);

	UFUNCTION(BlueprintCallable, Category = "Rally")
	bool BuildFromPlacedCheckpoints();

	/**
	 * Construye con las puertas dadas. Con RoadAxis (road_uu) la spline sigue la calzada y cada puerta se proyecta en ella;
	 * sin él, la spline pasa por las puertas con su rumbo. RoadWidthCm (0 = desconocido) acerca los bordes a la calzada.
	 * RoadBankDeg (bank_deg, uno por punto de RoadAxis) inclina las puertas con el peralte (#622). RoadWidthsCm (road_widths_m en
	 * cm, uno por punto de RoadAxis) da el ancho de cada tramo («Tramos variados», #622); sin él, RoadWidthCm en todo el trazado.
	 */
	bool BuildFromGates(const TArray<TNRally::FGateDef>& GateDefs, bool bCircuit, const TArray<FVector>& RoadAxis = TArray<FVector>(),
		double RoadWidthCm = 0.0, TConstArrayView<double> RoadBankDeg = TConstArrayView<double>(),
		TConstArrayView<double> RoadWidthsCm = TConstArrayView<double>());

	UFUNCTION(BlueprintCallable, Category = "Rally")
	void ClearTrack();

	UFUNCTION(BlueprintPure, Category = "Rally")
	bool IsBuilt() const { return bBuilt; }

	UFUNCTION(BlueprintPure, Category = "Rally")
	bool IsCircuit() const { return bClosed; }

	UFUNCTION(BlueprintPure, Category = "Rally")
	int32 GetGateCount() const { return GateArcs.Num(); }

	UFUNCTION(BlueprintPure, Category = "Rally")
	float GetTrackLengthCm() const;

	UFUNCTION(BlueprintPure, Category = "Rally")
	int32 GetBorderInstanceCount() const;

	UFUNCTION(BlueprintPure, Category = "Rally")
	int32 GetItemBoxCount() const { return ItemBoxes.Num(); }

	USplineComponent* GetSpline() const { return Spline; }
	/** Cajas «?» de la pista (solo en el servidor; los clientes las reciben replicadas). */
	const TArray<TObjectPtr<ATN_KartItemBox>>& GetItemBoxes() const { return ItemBoxes; }
	/** Arcos de las filas de cajas (cm de la spline, TNRally::AmmoRowArcs). También en los clientes, que no crean las cajas. */
	const TArray<double>& GetAmmoRowArcs() const { return AmmoRowArcs; }

	double GetGateArc(int32 GateIndex) const;
	/** Arco de spline hacia delante de la puerta From a la To (cm). */
	double GetArcBetweenGates(int32 FromGate, int32 ToGate) const;
	/** Centro del volumen de la puerta con X en el sentido de la carrera. */
	FTransform GetGateCrossingTransform(int32 GateIndex) const;
	FVector GetGateHalfExtent() const;
	/** Ancho de la calzada del manifest (road_width_m, en cm; con ancho por tramos, el máximo); 0 si no viene. */
	double GetRoadWidthCm() const { return ManifestRoadWidthCm; }

	/**
	 * Ancho de la calzada (cm) en el arco: el de road_widths_m interpolado si el manifest lo trae (tramos estrechos y anchos, #622)
	 * y, si no, road_width_m (0 si tampoco viene).
	 */
	UFUNCTION(BlueprintPure, Category = "Rally")
	double GetRoadWidthAtArcCm(double Arc) const;

	/** true si el manifest trae ancho por tramos (road_widths_m). */
	bool HasRoadWidthsPerPoint() const { return WidthSampleCm.Num() > 0; }

	/** Hueco Slot (0..7) de la parrilla 2 × 4 detrás de la salida, a ras de suelo (traza hacia abajo) + Lift. */
	FTransform GetGridSlotTransform(int32 Slot, double LiftCm = 80.0) const;
	/** Punto de reaparición tras la puerta (orientado a la spline) en el carril Lane (TNRallyRace::RespawnLaneLateralCm). */
	FTransform GetRespawnTransform(int32 GateIndex, int32 Lane, double LiftCm = 100.0) const;
	/**
	 * Punto de reaparición tras la puerta en el primer carril libre (TNRallyRace::PickFreeRespawnLane) respecto a las
	 * posiciones Occupied (los demás buggies). Lo usa ATN_RallyGameMode::RespawnTeam.
	 */
	FTransform FindFreeRespawnTransform(int32 GateIndex, const TArray<FVector>& Occupied, double LiftCm, int32* OutLane = nullptr) const;

	/** Arco más cercano en la ventana [Prev - 20 m, Prev + 120 m]. */
	double FindArcNear(const FVector& Location, double PrevArc) const;
	/** Arco más cercano en toda la spline (al aparecer). */
	double FindArcGlobal(const FVector& Location) const;
	FVector GetLocationAtArc(double Arc) const;
	FVector GetDirectionAtArc(double Arc) const;

	/** Peralte (grados, positivo: la derecha de la marcha más baja) en el arco; 0 si el manifest no trae bank_deg. */
	double GetBankDegAtArc(double Arc) const;

	/** Saltos, rasantes, horquillas... del manifest (elements) con los arcos en la spline; vacío si no los trae (#622). */
	const TArray<TNRallyCircuit::FFeatureArc>& GetFeatures() const { return Features; }

	/** Vueltas que pide el manifest (laps); 0 si no lo dice. */
	int32 GetManifestLaps() const { return ManifestLaps; }

	bool HasWaterZ() const { return bHasWater; }
	double GetWaterZ() const { return WaterZ; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Rally")
	TObjectPtr<USplineComponent> Spline;

	UPROPERTY(VisibleAnywhere, Category = "Rally")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Borders;

	UPROPERTY(EditAnywhere, Category = "Rally")
	TSubclassOf<ATN_RallyGate> GateClass;

	/** Caja «?» de las filas de cajas (#629: sustituye a la caja de munición del Rally). */
	UPROPERTY(EditAnywhere, Category = "Rally|Cajas")
	TSubclassOf<ATN_KartItemBox> ItemBoxClass;

	UPROPERTY(EditAnywhere, Category = "Rally|Bordes")
	TObjectPtr<UStaticMesh> BorderMesh;

	/** Separación de las rocas de borde a lo largo del eje (cm). */
	UPROPERTY(EditAnywhere, Category = "Rally|Bordes", meta = (ClampMin = "200"))
	float BorderSpacingCm = 1000.f;

	/** Distancia del eje a cada fila de rocas: calzada de 14 m + arcén (cm). */
	UPROPERTY(EditAnywhere, Category = "Rally|Bordes")
	float BorderOffsetCm = 1300.f;

	/** Con road_width_m en el manifest, los bordes van a media calzada más esto (cm). */
	UPROPERTY(EditAnywhere, Category = "Rally|Bordes")
	float BorderOutsideRoadCm = 200.f;

	/** Separación de los puntos de la spline cuando se construye desde road_uu (cm). */
	UPROPERTY(EditAnywhere, Category = "Rally", meta = (ClampMin = "100"))
	float RoadSampleStepCm = 1000.f;

	UPROPERTY(EditAnywhere, Category = "Rally|Bordes")
	FVector BorderSizeCm = FVector(140.0, 140.0, 90.0);

	/** Sin roca si el suelo queda a más de esto de la cota del eje (barrancos, taludes, túneles). */
	UPROPERTY(EditAnywhere, Category = "Rally|Bordes")
	float BorderMaxHeightDeltaCm = 600.f;

	UPROPERTY(EditAnywhere, Category = "Rally|Cajas", meta = (ClampMin = "1", ClampMax = "8"))
	int32 AmmoBoxesPerRow = 4;

	UPROPERTY(EditAnywhere, Category = "Rally|Cajas")
	float AmmoLateralSpacingCm = 400.f;

	/** Sin cajas en los últimos metros de un punto a punto (final por conducción). */
	UPROPERTY(EditAnywhere, Category = "Rally|Cajas")
	float NoAmmoBeforeFinishCm = 35500.f;

	/** Altura del sitio de cada caja sobre el suelo (cm). */
	UPROPERTY(EditAnywhere, Category = "Rally|Cajas")
	float ItemBoxLiftCm = 90.f;

	/** Cajas creadas (servidor). Las subclases que ponen sus propias filas las añaden aquí: ClearTrack las destruye. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ATN_KartItemBox>> ItemBoxes;

	/** Destruye las cajas «?» (también lo hace ClearTrack). */
	void ClearItemBoxes();

private:
	void BuildSpline(const TArray<TNRally::FGateDef>& GateDefs);
	void BuildSplineFromRoad(const TArray<FVector>& Road, const TArray<TNRally::FGateDef>& GateDefs);
	void SpawnGates(const TArray<TNRally::FGateDef>& GateDefs);
	void SpawnAmmoRows();
	void SpawnAmmoRow(double Arc);
	/** Suelo bajo Location (traza vertical); false si no hay. */
	bool TraceGround(const FVector& Location, double UpCm, double DownCm, FVector& OutGround) const;
	FVector OffsetAtArc(double Arc, double LateralCm) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ATN_RallyGate>> Gates;

	TArray<double> GateArcs;
	TArray<double> AmmoRowArcs;
	/** Peralte de road_uu: arco de la spline de cada punto y su peralte (vacíos sin bank_deg). */
	TArray<double> BankSampleArcs;
	TArray<double> BankSampleDeg;
	/** Ancho de road_uu (road_widths_m): arco de la spline de cada punto y su ancho en cm (vacíos sin el campo). */
	TArray<double> WidthSampleArcs;
	TArray<double> WidthSampleCm;
	TArray<TNRallyCircuit::FFeatureArc> Features;
	/** Longitud de la polilínea road_uu (cm; 0 sin ella): pasa los metros de elements a arcos de la spline. */
	double RoadLengthCm = 0.0;
	bool bClosed = false;
	bool bBuilt = false;
	bool bHasWater = false;
	double WaterZ = 0.0;
	int32 ManifestLaps = 0;
	double ActiveBorderOffsetCm = 1300.0;
	double ManifestRoadWidthCm = 0.0;
};
