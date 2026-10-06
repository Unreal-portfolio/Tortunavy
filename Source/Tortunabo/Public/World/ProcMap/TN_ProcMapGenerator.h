#pragma once

#include "CoreMinimal.h"
#include "Core/TN_MatchStartTypes.h"
#include "GameFramework/Actor.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "World/ProcMap/TN_ProcMapLayout.h"
#include "World/ProcMap/TN_ProcMapTypes.h"
#include "World/ProcMap/TN_ProcMapTerrainDetail.h"
#include "World/ProcMap/TN_SurvivalTrapPlacement.h"
#include "World/ProcMap/TN_CoopIntensity.h"
#include "TN_ProcMapGenerator.generated.h"

class UProceduralMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UPointLightComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UPrimitiveComponent;
class UMaterialInterface;
class UPCGComponent;
class APlayerStart;
class ATN_ProcEggNest;
class ATN_ProcStartStructure;
class ATN_ProcWaterVolume;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnProcMapGenerated, int32, Generation);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnProcMapGeneratedNative, int32 /*Generation*/);

/** Una concha especial (50 o 100) del mapa, en el mundo: para ir a por ella con «TNShells Especial». */
struct FTNShellSpot
{
	/** Centro de la concha, dónde ponerse (en el suelo, unos metros antes) y hacia dónde mirar. */
	FVector Shell = FVector::ZeroVector;
	FVector Stand = FVector::ZeroVector;
	FVector Facing = FVector::ForwardVector;
	int32 Value = 0;
	/** Qué sitio es (TNProcMap::ShellSpotName). */
	FString Where;
};

/** Una muestra del camino principal en el mundo (para el Rally, que hace su pista con ellas). */
struct FTNProcPathPoint
{
	/** Centro del camino a la cota del suelo. */
	FVector Location = FVector::ZeroVector;
	/** Dirección del camino en horizontal (unitaria). */
	FVector Direction = FVector::ForwardVector;
	/** Ancho del camino (cm). */
	float Width = 0.f;
	/** TNProcMap::PathFlags de la muestra (cueva, salida, playa final...). */
	uint32 Flags = 0;
};

/** Lo único que se replica del mapa: con esto cada máquina genera el mismo. */
USTRUCT(BlueprintType)
struct TORTUNABO_API FTNProcMapNetConfig
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ProcMap")
	int32 Seed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ProcMap")
	ETNProcGameMode Mode = ETNProcGameMode::Coop;

	UPROPERTY(BlueprintReadOnly, Category = "ProcMap")
	ETNProcDifficulty Difficulty = ETNProcDifficulty::Normal;

	/** Supervivencia: dificultad 1–5 del nivel (0 = la que corresponde a Difficulty: 1, 3 o 5). */
	UPROPERTY(BlueprintReadOnly, Category = "ProcMap")
	int32 SurvivalDifficulty = 0;

	/**
	 * Supervivencia: trampas que se buscan cada 100 m de camino, en décimas (0 = las del catálogo; fácil 65, normal 100,
	 * difícil 150, #730). Cada máquina saca de aquí cuántas copias de los puntos del catálogo hacen falta.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "ProcMap")
	int32 SurvivalTrapsPer100mTenths = 0;

	/** Supervivencia: rebuscables que se buscan cada 100 m de camino, en décimas (0 = los del decorado; #724). */
	UPROPERTY(BlueprintReadOnly, Category = "ProcMap")
	int32 SurvivalSearchPer100mTenths = 0;

	/** Se incrementa en cada (re)generación, p. ej. entre rondas. 0 = sin mapa. */
	UPROPERTY(BlueprintReadOnly, Category = "ProcMap")
	int32 Generation = 0;

	/** Coop: ronda (desde 1) con la que se lee la tabla de intensidad (#788). 0 = la primera. */
	UPROPERTY(BlueprintReadOnly, Category = "ProcMap")
	int32 CoopRound = 0;
};

/**
 * ATN_ProcMapGenerator
 *
 * Materializa el mapa procedural por módulos irregulares. La decisión de qué va
 * dónde vive en la lógica pura TNProcMap (TN_ProcMapGenerate.h, testeada); este
 * actor la traduce a:
 *   - Terreno: tiles de UProceduralMeshComponent con colisión y color por vértice
 *     (biomas mezclados, camino, roca en pendientes). Nanite no aplica a mallas
 *     generadas en runtime; la vegetación sí puede usar meshes con Nanite.
 *   - Agua: plano a nivel del mar + volumen nadable con cajas sobre el agua profunda.
 *   - Estructuras: tableros de puentes colosales, techos de cueva, isletas, labios
 *     de huecos, pasarelas, puentes del río, lava.
 *   - Vegetación y props: HISM por capa de bioma (y grafo PCG opcional por bioma).
 *   - Actores: géiseres, toboganes, zonas de muerte, corrientes, remolinos,
 *     depredadores, criaturas rebotadoras, pilas de huevos, muros y compuertas 2vs2,
 *     meta y PlayerStarts.
 *
 * Red: solo se replica FTNProcMapNetConfig. El servidor la fija (ServerGenerate) y
 * cada cliente genera lo mismo en OnRep. Los actores que deben replicar (enemigos,
 * huevos, puzles) los crea solo el servidor; los de movimiento (géiser, tobogán,
 * agua) se crean en todas las máquinas para que la predicción del cliente cuadre.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_ProcMapGenerator : public AActor
{
	GENERATED_BODY()

public:
	ATN_ProcMapGenerator();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Servidor: genera (o regenera) el mapa con esta semilla y lo replica a todos. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "ProcMap")
	void ServerGenerate(int32 InSeed, ETNProcGameMode InMode, ETNProcDifficulty InDifficulty);

	/** Servidor: genera el mapa de Supervivencia con esta semilla y dificultad 1–5 (un nivel de la partida, #274). */
	void ServerGenerateSurvival(int32 InSeed, int32 InSurvivalDifficulty, int32 InTrapsPer100mTenths = 0, int32 InSearchPer100mTenths = 0);

	/**
	 * Servidor: ronda del coop (desde 1) para la tabla de intensidad (#788). La pone el GameMode antes de ServerGenerate;
	 * viaja en la réplica con la semilla, así todas las máquinas hacen el mismo plan.
	 */
	void SetCoopRound(int32 InRound) { NetConfig.CoopRound = FMath::Max(0, InRound); }

	/** Coop: plan de la tabla de intensidad del mapa actual (un elemento por tramo; vacío fuera del coop o sin tabla). */
	const TArray<TNCoopIntensity::FTramoPlan>& GetIntensityPlan() const { return IntensityPlan; }

	/** Genera con los parámetros de edición. Botón en el panel Details. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "ProcMap")
	void GenerateInEditor();

	/** Borra todo lo generado. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "ProcMap")
	void Clear();

	UFUNCTION(BlueprintPure, Category = "ProcMap")
	bool IsMapReady() const { return bMapReady; }

	/**
	 * Supervivencia: trampas del mapa (las sueltas sin las sombrillas, los cruces de quads y los puentes que se rompen) y, en
	 * OutBreakdown, cuántas de cada tipo («12 cáscaras, 3 medusas...», más las placas del atajo, que no cuentan).
	 */
	int32 GetSurvivalTrapCount(FString* OutBreakdown = nullptr) const;

	/** Generación que ya está construida en ESTA máquina. */
	UFUNCTION(BlueprintPure, Category = "ProcMap")
	int32 GetBuiltGeneration() const { return BuiltGeneration; }

	/** Generación pedida por el servidor (replicada). */
	UFUNCTION(BlueprintPure, Category = "ProcMap")
	int32 GetRequestedGeneration() const { return NetConfig.Generation; }

	const FTNProcMapNetConfig& GetNetConfig() const { return NetConfig; }
	const TNProcMap::FLayout& GetLayout() const { return Layout; }
	const FTNProcMapProfile& GetActiveProfile() const { return ActiveProfile; }

	/**
	 * Transform de salida para el jugador N (alrededor del claro inicial). En el servidor, con estructura de salida, los
	 * primeros van dentro de ella (sala o huevos) a 110 cm del suelo, como los del anillo.
	 */
	UFUNCTION(BlueprintPure, Category = "ProcMap")
	FTransform GetStartTransform(int32 PlayerIndex) const;

	/** Si hay colisión del mapa (terreno o estructuras) bajo un punto: la del terreno se cocina en segundo plano. */
	bool MapCollisionUnder(const FVector& WorldLocation) const;

	/**
	 * Estructura de salida (puerta doble o huevos, ATN_ProcStartStructure) que el servidor pone al fondo del claro de
	 * salida en cada generación. La pide el GameMode de la partida antes de generar; si nadie la pide (solo terreno,
	 * nivel abierto a mano), no hay.
	 */
	void SetStartStructureStyle(ETNMatchStartStyle InStyle) { bSpawnStartStructure = true; StartStructureStyle = InStyle; }

	/** Estructura de salida del mapa actual (solo servidor; nullptr si no hay). */
	ATN_ProcStartStructure* GetStartStructure() const;

	/** PlayerStart del sitio de salida Index (los primeros, dentro de la estructura de salida si la hay). Solo servidor. */
	APlayerStart* GetStartPlayerStart(int32 Index) const;

	/** Progreso (cm a lo largo del camino principal) de una posición del mundo. */
	UFUNCTION(BlueprintPure, Category = "ProcMap")
	float GetPathProgress(const FVector& WorldLocation) const;

	/** Punto del camino principal (mundo) a un progreso dado, con su dirección. */
	UFUNCTION(BlueprintCallable, Category = "ProcMap")
	FVector GetPathLocationAtProgress(float Progress, FVector& OutDirection) const;

	UFUNCTION(BlueprintPure, Category = "ProcMap")
	float GetMainPathLength() const;

	/** Minutos estimados de recorrido del camino principal a una velocidad media. */
	UFUNCTION(BlueprintPure, Category = "ProcMap")
	float EstimateTraversalMinutes(float AverageSpeedCmPerSec = 550.f) const;

	/** Pilas de huevos (solo en servidor; los clientes las ven como actores replicados). */
	const TArray<TWeakObjectPtr<ATN_ProcEggNest>>& GetEggNests() const { return EggNests; }

	/** Conchas especiales (50 y 100) del mapa actual, en orden por el camino (solo servidor). */
	const TArray<FTNShellSpot>& GetSpecialShellSpots() const { return SpecialShellSpots; }

	/** Resumen de las conchas del mapa actual (cuántas de cada tamaño y dónde van las especiales; solo servidor). */
	const FString& GetShellSummary() const { return ShellSummary; }

	/** Altura del terreno generado en un punto del mundo (sin trazas: vale antes de cocinar colisión). */
	float GetTerrainHeightAt(const FVector& WorldLocation) const;

	/**
	 * Mapa de los karts (NetConfig.Mode == Karts): el camino del cooperativo hecho para el kart (TNProcMap::FGenParams::
	 * bDrivable) y sin lo que es de las tortugas a pie (huevos, recompensas, rebuscables, peligros, enemigos y conchas). Lo
	 * saben todas las máquinas porque el modo viaja en la réplica.
	 */
	bool IsKartMap() const { return NetConfig.Mode == ETNProcGameMode::Karts; }

	/** Muestras del camino principal en el mundo, de la salida a la playa final (vacío si no hay mapa). */
	void GetMainPathWorld(TArray<FTNProcPathPoint>& OutPoints) const;

	/**
	 * Obstáculos grandes que quedan dentro del camino principal (piezas de explanada y agujas de roca), en el mundo: X, Y y
	 * Z del centro a ras de suelo y W = radio libre (cm). El piloto IA de los karts los rodea. Los arcos que cruzan el camino
	 * van con W negativo (su medio fondo más 4 m): bajo ellos el piloto va por el centro.
	 */
	void GetMainPathObstaclesWorld(TArray<FVector4>& OutObstacles) const;

	/** Cota del mar en el mundo (por debajo, el kart está en el agua). */
	float GetSeaLevelWorldZ() const;

	UTN_ProcMapSettings* GetSettings() const { return Settings; }

	/** Modo de solo terreno (bTerrainOnly): lo pone el GameMode del nivel de solo terreno antes de generar. */
	void SetTerrainOnly(bool bInTerrainOnly) { bTerrainOnly = bInTerrainOnly; }

	/** Ajustes a usar si el generador del nivel no tiene (lo llama el GameMode antes de generar). */
	void SetSettingsIfMissing(UTN_ProcMapSettings* InSettings) { if (!Settings) { Settings = InSettings; } }

	FOnProcMapGeneratedNative OnMapGeneratedNative;

	UPROPERTY(BlueprintAssignable, Category = "ProcMap")
	FOnProcMapGenerated OnMapGenerated;

protected:
	/** Configuración del mapa (biomas, perfiles, materiales). Opcional: hay valores greybox. Se replica al entrar:
	 * un generador creado en ejecución (Supervivencia) solo la recibe en el servidor (SetSettingsIfMissing). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "ProcMap")
	TObjectPtr<UTN_ProcMapSettings> Settings;

	/** Si ningún GameMode lo pide en X segundos, el servidor genera con los valores de edición. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ProcMap")
	bool bAutoGenerateIfIdle = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ProcMap|Editor")
	int32 EditorSeed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ProcMap|Editor")
	bool bEditorRandomSeed = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ProcMap|Editor")
	ETNProcGameMode EditorMode = ETNProcGameMode::Coop;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ProcMap|Editor")
	ETNProcDifficulty EditorDifficulty = ETNProcDifficulty::Normal;

	/** Dificultad 1–5 del mapa de Supervivencia (con EditorMode = Supervivencia). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ProcMap|Editor", meta = (ClampMin = "1", ClampMax = "5", EditCondition = "EditorMode == ETNProcGameMode::Survival"))
	int32 EditorSurvivalDifficulty = 1;

	/** Salta la vegetación (iterar rápido sobre la forma del mapa). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ProcMap|Debug")
	bool bSkipScatter = false;

	/**
	 * Solo el terreno (LVL_ProcMap_Terrain, para comparar generadores): el terreno y lo integrado en el camino
	 * (cuevas, puentes, murallas, huecos, troncos, obstáculos, géiseres, cascadas); sin vegetación, fauna, formaciones
	 * decorativas, hitos, recompensas, huevos, peligros ni efectos ambientales.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProcMap")
	bool bTerrainOnly = false;

	/** Dibuja camino, ramas y módulos con líneas de debug. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ProcMap|Debug")
	bool bDebugDraw = false;

private:
	UPROPERTY(ReplicatedUsing = OnRep_NetConfig)
	FTNProcMapNetConfig NetConfig;

	UFUNCTION()
	void OnRep_NetConfig();

	/** Construye todo lo de NetConfig en esta máquina. */
	void BuildFromNetConfig();

	bool BuildLayout();
	void BuildTerrain();
	void BuildWater();
	void BuildStructures();
	void BuildScatter();
	/** Vegetación y rocas sueltas procedurales por bioma (TN_ProcMapFlora.h). */
	void BuildFlora();
	void SpawnTraversalActors();
	void SpawnServerActors();
	/** Servidor: la estructura de salida al fondo del claro (antes que los PlayerStart, que van dentro de ella). */
	void SpawnStartStructure();
	/** Servidor: decorados del camino que se pueden rebuscar (ATN_ProcSearchSpot); no en el modo de solo terreno. */
	void SpawnSearchSpots();
	void SpawnHazards();
	/**
	 * Supervivencia (#516, #517): calcula dónde van las trampas del mapa del catálogo (en todas las máquinas y en el
	 * editor). Antes de las mallas: el hueco con puente que se rompe se construye sin su viga.
	 */
	void PlanSurvivalTraps();
	/**
	 * Supervivencia (#724): añade al layout los objetos del camino que hacen falta para la densidad de rebuscables del mapa
	 * (TNSurvivalCatalog::PlaceSearchProps), en todas las máquinas. Después del terreno (se apoyan en su suelo) y antes de las
	 * mallas del decorado (BuildStructures los dibuja); SpawnSearchSpots los hace rebuscables siempre.
	 */
	void PlanSurvivalSearchProps();
	/** ¿El hueco Feature lleva puente que se rompe en lugar de viga? */
	bool IsSurvivalBreakableGap(int32 Feature) const;
	/** Crea las trampas del plan: las replicadas y las de lógica de servidor en el servidor; las zonas lentas en cada máquina. */
	void SpawnSurvivalTraps();
	/** Refugios de los búnkeres (#689): un ATN_BeachShelterVolume local en cada formación Bunker, en todas las máquinas. */
	void SpawnShelters();
	/**
	 * Coop (#788): plan de la tabla de intensidad para la ronda de NetConfig (en todas las máquinas, tras el layout). Fuera
	 * del coop, o sin tabla, lo deja vacío y todo se coloca como antes.
	 */
	void PlanCoopIntensity();
	/** Paso de recorrido de una muestra de camino (en una rama, el de la muestra del principal de la que sale). */
	int32 RouteStepOfSample(int32 BranchIndex, int32 PathIndex) const;
	/** Tramo (desde 0) del plan de intensidad de una muestra de camino; INDEX_NONE sin plan. */
	int32 IntensityTramoOfSample(int32 BranchIndex, int32 PathIndex) const;
	/** Si un peligro por bioma de esta clase y dificultad mínima (0-2) va en la muestra donde lo ha puesto PlanHazards. */
	bool IntensityAllowsHazard(int32 BranchIndex, int32 PathIndex, const UClass* Class, int32 MinDifficulty) const;
	/** Servidor: enemigos de los módulos de diseño del plan, repartidos por su tramo del camino principal (#788). */
	void SpawnIntensityEnemies();
	/** Servidor: anélidos poliquetos (#792) al borde del camino en los tramos Fácil y Medio del plan. */
	void SpawnIntensityAllies();
	/** Muestras del camino principal del tramo Tramo donde se puede poner algo (sin las especiales: salida, puentes...). */
	void CollectTramoSamples(int32 Tramo, TArray<int32>& OutSamples) const;
	/** Punto del mapa a un lado del camino en la muestra Sample (Side en -1..1 del medio ancho); false si cae al agua o en un desnivel. */
	bool PathSideSpot(int32 Sample, double Side, FVector2D& OutPoint, double& OutGround) const;
	/** Marcadores de las trampas del plan (Debug Draw), también en el editor. */
	void DrawSurvivalTrapPlan() const;
	/**
	 * Servidor: conchas de puntos del plan puro (TNProcMap::PlanShells: rachas de 1, arcos de salto, cornisas y
	 * especiales de 50 y 100) y las especiales de los tramos hundidos de los puentes (BrokenSpanPrizes). Después de
	 * SpawnHazards: no pisan lo que este ha puesto (HazardSpots). No en el modo de solo terreno.
	 */
	void SpawnShells();
	/**
	 * Servidor, solo en el Coop (#797): los muñecos tortuga del plan puro (TNProcMap::PlanTurtleDolls), sin pisar los
	 * peligros ni las conchas (Occupied: x, y y radio en el mapa). Lo llama SpawnShells al acabar.
	 */
	void SpawnTurtleDolls(const TArray<FVector>& Occupied);
	void RunBiomePCG();
	void BuildProgressIndex();
	void DrawDebug() const;
	void ReportReadyToServer();
	void FreezeLocalPawnUntilReady();

	// ── Utilidades ──────────────────────────────────────────────────────────
	FVector MapToWorld(const FVector& MapPoint) const;
	FVector MapToWorld2D(const FVector2D& MapPoint, double Z) const { return MapToWorld(FVector(MapPoint.X, MapPoint.Y, Z)); }
	FVector WorldToMap(const FVector& WorldPoint) const;
	double TerrainHeightMap(const FVector2D& MapPoint) const;

	/**
	 * Poza al pie de una cascada (FFeature SlideZone), en coordenadas del mapa: centro un poco adelantado respecto al
	 * pie, radio según el ancho del camino allí y cota plana sobre el punto más alto de su disco (el suelo no asoma).
	 * La usan la malla del agua (TN_ProcMapGenerator_Build) y los efectos del pie (ATN_ProcSlideZone).
	 */
	void SlidePool(const TNProcMap::FFeature& F, FVector2D& OutCenter, double& OutRadius, double& OutZ, FVector2D& OutFoot, FVector2D& OutFlow) const;
	FVector TerrainNormalMap(const FVector2D& MapPoint) const;
	double PathDistanceMap(const FVector2D& MapPoint) const;
	/** Preferred o, si es nulo, el material de color de vértice del proyecto (TNMaterials::VertexColor). */
	UMaterialInterface* ResolveMaterial(UMaterialInterface* Preferred) const;
	void ResolveBiomeColors(ETNProcBiome Biome, FLinearColor& Ground, FLinearColor& Path, FLinearColor& Rock, FLinearColor& Bed) const;
	AActor* SpawnMapActor(UClass* Class, const FTransform& Transform, bool bTrackAsServer);

	TNProcMap::FLayout Layout;
	FTNProcMapProfile ActiveProfile;
	bool bMapReady = false;
	int32 BuiltGeneration = 0;
	bool bReportedReady = false;
	float IdleTimer = 0.f;

	// Malla de alturas (espacio del mapa) para colocar cosas sin depender de la colisión.
	TArray<float> Heights;
	TArray<uint8> PathMask;
	TArray<float> PathDist;
	FVector2D LatticeOrigin = FVector2D::ZeroVector;
	double LatticeSpacing = 250.0;
	int32 LatticeNX = 0;
	int32 LatticeNY = 0;
	/** Detalle fino (DetailSpacing) de los cuadrados del mallado que lo necesitan: lo dibujado y su colisión. */
	TNProcMap::FTerrainDetail TerrainDetail;

	/** Desde cuándo (s) está el mapa listo esperando a que haya suelo bajo el pawn local (-1 = no espera). */
	double ReadySince = -1.0;

	// Índice de progreso: puntos del camino (principal y ramas) con su distancia.
	struct FProgressPoint
	{
		FVector P;
		float Progress;
	};
	TArray<FProgressPoint> ProgressPoints;
	TArray<TArray<int32>> ProgressBuckets;
	FVector2D ProgressOrigin = FVector2D::ZeroVector;
	double ProgressCell = 3000.0;
	int32 ProgressW = 0;
	int32 ProgressH = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> TerrainTiles;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> StructureMesh;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> DecorMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> WaterPlane;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> ScatterComponents;

	/** Mallas de la vegetación procedural (una por bioma, especie y variante), construidas en ejecución. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> FloraMeshes;

	/** Luces tenues dentro de las cuevas. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> CaveLights;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPCGComponent>> PCGComponents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnedActors;

	/** Trampas del mapa del catálogo de Supervivencia (vacío fuera de Supervivencia o con una semilla fuera del catálogo). */
	TArray<TNSurvivalCatalog::FTrapPlacement> SurvivalTrapPlan;
	/** Quads, puentes que se rompen y placas del mapa del catálogo (#517). */
	TNSurvivalCatalog::FTerrainTrapPlan SurvivalTerrainPlan;

	/** Índices en Layout.Features de los objetos del camino añadidos para rebuscar (#724, PlanSurvivalSearchProps). */
	TSet<int32> SurvivalSearchProps;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> BoundaryWalls;

	// Formas básicas del motor para greybox (cargadas en el constructor para que se cocinen).
	UPROPERTY()
	TObjectPtr<UStaticMesh> BasicPlane;

	UPROPERTY()
	TObjectPtr<UStaticMesh> BasicCube;

	UPROPERTY()
	TObjectPtr<UStaticMesh> BasicCylinder;

	UPROPERTY()
	TObjectPtr<UStaticMesh> BasicSphere;

	UPROPERTY()
	TObjectPtr<UStaticMesh> BasicCone;

	bool bFrozeLocalPawn = false;

	TArray<TWeakObjectPtr<ATN_ProcEggNest>> EggNests;
	TArray<FTransform> StartTransforms;

	/** Estructura de salida pedida por el GameMode y su estilo. */
	bool bSpawnStartStructure = false;
	ETNMatchStartStyle StartStructureStyle = ETNMatchStartStyle::Gate;
	TWeakObjectPtr<ATN_ProcStartStructure> StartStructure;

	/** PlayerStart de cada sitio de salida (mismo índice que StartTransforms; solo servidor). */
	TArray<TWeakObjectPtr<APlayerStart>> StartPlayerStarts;

	/**
	 * Punto pisable del medio de cada tramo hundido de un puente colosal (espacio del mapa, a la cota de lo que se pisa:
	 * el codo de las vigas, la cima del poste o el tablón), dónde ponerse antes de él y su cruce y largo. Lo rellena
	 * BuildStructures (en todas las máquinas) y lo usa SpawnShells.
	 */
	struct FBrokenSpanPrize
	{
		FVector Point = FVector::ZeroVector;
		FVector Stand = FVector::ZeroVector;
		FVector2D Facing = FVector2D(1.0, 0.0);
		int32 Crossing = INDEX_NONE;
		double Length = 0.0;
	};
	TArray<FBrokenSpanPrize> BrokenSpanPrizes;

	/** Lo que SpawnHazards ha puesto en el servidor (x, y y radio en el mapa): las conchas del plan no lo pisan. */
	TArray<FVector> HazardSpots;

	/** Coop: plan de la tabla de intensidad del mapa actual (#788). */
	TArray<TNCoopIntensity::FTramoPlan> IntensityPlan;

	/** Conchas especiales del mapa actual y resumen para el registro y TNShells (solo servidor). */
	TArray<FTNShellSpot> SpecialShellSpots;
	FString ShellSummary;
};
