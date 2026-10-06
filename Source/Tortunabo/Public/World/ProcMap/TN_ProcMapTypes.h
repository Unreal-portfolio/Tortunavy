#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "World/ProcMap/TN_ProcMapLayout.h"
#include "TN_ProcMapTypes.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UPCGGraphInterface;

/**
 * Configuración editable de los biomas (valle del lobby y recorrido del tutorial): un DataAsset por bioma con colores,
 * vegetación y peligros.
 * Todo tiene valores por defecto razonables: sin assets asignados el mapa sale
 * en greybox con formas básicas del motor.
 */

/** Cómo se coloca un peligro/enemigo respecto al camino. */
UENUM(BlueprintType)
enum class ETNProcHazardPlacement : uint8
{
	OnPath      UMETA(DisplayName = "Sobre el camino"),
	PathEdge    UMETA(DisplayName = "Borde del camino"),
	NearPath    UMETA(DisplayName = "Junto al camino"),
	InWater     UMETA(DisplayName = "En el agua"),
	AbovePath   UMETA(DisplayName = "Sobre el camino (aire)"),
	OffPathFar  UMETA(DisplayName = "Lejos del camino")
};

/** Dónde puede ir una capa de vegetación/props. */
UENUM(BlueprintType)
enum class ETNProcScatterZone : uint8
{
	/** Terreno fuera del camino (lo normal para árboles, rocas...). */
	OffPath     UMETA(DisplayName = "Fuera del camino"),
	/** Arcén: borde del camino (basura, conchas, hierba). */
	PathEdge    UMETA(DisplayName = "Borde del camino"),
	/** Muros y zonas altas del perímetro (bosque denso, rocas grandes). */
	Walls       UMETA(DisplayName = "Muros del borde"),
	/** Bajo el nivel del agua poco profunda (raíces de manglar, juncos). */
	Shallows    UMETA(DisplayName = "Aguas someras")
};

/** Una capa de vegetación / props instanciados. */
USTRUCT(BlueprintType)
struct TORTUNABO_API FTNProcScatterLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	TObjectPtr<UStaticMesh> Mesh;

	/** Material opcional (si no, el del mesh). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	ETNProcScatterZone Zone = ETNProcScatterZone::OffPath;

	/** Instancias por cada 100 m². */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta = (ClampMin = "0.0"))
	float DensityPer100m2 = 1.f;

	/** Escala uniforme mínima/máxima (X = min, Y = max). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	FVector2D ScaleRange = FVector2D(0.8, 1.2);

	/** Escala no uniforme adicional (para greybox con formas básicas). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	FVector ScaleAxes = FVector(1.0, 1.0, 1.0);

	/** Distancia mínima al borde del camino (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta = (ClampMin = "0.0"))
	float MinPathDistance = 300.f;

	/** Pendiente máxima (grados). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float MaxSlopeDeg = 35.f;

	/** Hunde la instancia en el suelo (cm) para que no flote en pendiente. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	float ZOffset = -10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	bool bAlignToNormal = false;

	/** Árboles, rocas grandes: bloquean. Hierba, basura pequeña: no. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	bool bCollision = true;

	/** Distancia de culling (cm, 0 = sin culling). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta = (ClampMin = "0.0"))
	float CullDistance = 30000.f;

	/** Greybox: tiñe el material (parámetro "Color") para distinguir capas sin arte. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	bool bApplyTint = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta = (EditCondition = "bApplyTint"))
	FLinearColor Tint = FLinearColor::White;
};

/** Un peligro/enemigo/spawner que el generador coloca en el bioma. */
USTRUCT(BlueprintType)
struct TORTUNABO_API FTNProcHazardEntry
{
	GENERATED_BODY()

	/** Clase a spawnear (BP de enemigo, zona de spawn, peligro...). Si replica, solo la crea el servidor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peligro")
	TSubclassOf<AActor> ActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peligro", meta = (ClampMin = "0.0"))
	float PerKm = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peligro")
	ETNProcHazardPlacement Placement = ETNProcHazardPlacement::NearPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peligro")
	ETNProcDifficulty MinDifficulty = ETNProcDifficulty::Easy;

	/** Distancia mínima entre dos del mismo tipo (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peligro", meta = (ClampMin = "0.0"))
	float Clearance = 3000.f;

	/** Solo en el camino principal (no en ramas). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peligro")
	bool bMainPathOnly = false;

	/** Altura extra sobre el suelo (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peligro")
	float ZOffset = 0.f;
};

/** Aspecto y contenido de un bioma. */
UCLASS(BlueprintType)
class TORTUNABO_API UTN_ProcBiomeDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bioma")
	ETNProcBiome Biome = ETNProcBiome::Jungle;

	/** Color del suelo (vertex color del terreno). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colores")
	FLinearColor GroundColor = FLinearColor(0.2f, 0.45f, 0.15f);

	/** Color del camino (tierra, arena apisonada...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colores")
	FLinearColor PathColor = FLinearColor(0.55f, 0.42f, 0.25f);

	/** Color de las pendientes fuertes y muros. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colores")
	FLinearColor RockColor = FLinearColor(0.35f, 0.33f, 0.3f);

	/** Color del lecho bajo el agua. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colores")
	FLinearColor BedColor = FLinearColor(0.6f, 0.55f, 0.4f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contenido")
	TArray<FTNProcScatterLayer> Scatter;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contenido")
	TArray<FTNProcHazardEntry> Hazards;

	/** Mesh de la criatura flotante con comportamiento de medusa de este bioma (medusa, nenúfar, boya...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Agua")
	TObjectPtr<UStaticMesh> WaterBouncerMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Agua")
	FLinearColor WaterBouncerColor = FLinearColor(0.9f, 0.5f, 0.9f);

	/** Grafo PCG opcional que se ejecuta sobre el mapa generado para este bioma (extensión). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PCG")
	TObjectPtr<UPCGGraphInterface> PCGGraph;

	/**
	 * Rellena colores, vegetación y peligros con el greybox del bioma (lo mismo que
	 * usa el generador cuando no hay asset). Punto de partida para sustituir formas
	 * básicas por arte. Ojo: Scatter o Hazards vacíos significan "nada".
	 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Bioma")
	void ResetToGreyboxDefaults();
};

/** Configuración global del mapa procedural. */
UCLASS(BlueprintType)
class TORTUNABO_API UTN_ProcMapSettings : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Un asset por bioma (se busca por su campo Biome). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biomas")
	TArray<TObjectPtr<UTN_ProcBiomeDataAsset>> Biomes;

	// ── Terreno ─────────────────────────────────────────────────────────────

	/** Separación de vértices del terreno (cm). Menos = más detalle y más coste. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terreno", meta = (ClampMin = "100.0", ClampMax = "800.0"))
	float VertexSpacing = 150.f;

	/**
	 * Precisión del detalle fino del terreno (cm): los cuadrados en los que la malla de VertexSpacing se aparta de la
	 * forma real (bordes de taludes, crestas, bocas de cueva) se parten hasta esta separación (se redondea a
	 * VertexSpacing / N, N de 1 a 3). Igual o mayor que VertexSpacing = sin detalle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terreno", meta = (ClampMin = "50.0", ClampMax = "800.0"))
	float DetailSpacing = 50.f;

	/**
	 * Error (cm) de la malla gruesa a partir del cual se añade detalle cerca de los caminos (lejos, el triple). Con 20
	 * la malla crece ~1,5 veces; con 10, ~2 veces.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terreno", meta = (ClampMin = "10.0", ClampMax = "500.0"))
	float DetailError = 20.f;

	/** Cuadrados por lado de cada tile de terreno. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terreno", meta = (ClampMin = "8", ClampMax = "128"))
	int32 TileQuads = 48;

	/** Margen de terreno fuera del mapa jugable (muros, horizonte). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terreno", meta = (ClampMin = "0.0"))
	float OuterMargin = 25000.f;

	/** Mar que se genera más allá de la costa. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terreno", meta = (ClampMin = "0.0"))
	float SeaExtent = 30000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materiales")
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materiales")
	TObjectPtr<UMaterialInterface> WaterMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materiales")
	TObjectPtr<UMaterialInterface> LavaMaterial;

	/** Roca de las estructuras colosales e isletas. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materiales")
	TObjectPtr<UMaterialInterface> RockMaterial;

	/** Madera de pasarelas, labios de huecos y puentes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materiales")
	TObjectPtr<UMaterialInterface> WoodMaterial;

	/** Agua que baja por los toboganes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materiales")
	TObjectPtr<UMaterialInterface> SlideWaterMaterial;

	/** Vegetación y rocas sueltas procedurales: color de vértice, y ha de admitir mallas instanciadas. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materiales")
	TObjectPtr<UMaterialInterface> FoliageMaterial;

	// ── Vegetación ──────────────────────────────────────────────────────────

	/**
	 * Vegetación y rocas sueltas procedurales por bioma (árboles, arbustos, helechos, hierba, juncos,
	 * cactus, peñascos...). Con ella, de las capas de formas básicas de los biomas solo quedan los
	 * props del borde del camino y los de la zona humana.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetación")
	bool bProceduralFlora = true;

	/** Multiplicador de la densidad de la vegetación procedural. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetación", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float FloraDensity = 1.f;

	// ── Clases de los elementos del mapa (por defecto las C++) ─────────────

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clases")
	TSubclassOf<AActor> GeyserClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clases")
	TSubclassOf<AActor> EggNestClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clases")
	TSubclassOf<AActor> ThrowWallClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clases")
	TSubclassOf<AActor> SabotageGateClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clases")
	TSubclassOf<AActor> SwitchClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clases")
	TSubclassOf<AActor> WaterBouncerClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clases")
	TSubclassOf<AActor> PathStormClass;

	/** Busca el asset del bioma (nullptr si no hay). */
	const UTN_ProcBiomeDataAsset* FindBiome(ETNProcBiome Biome) const;
};

/** Colores de greybox por bioma cuando no hay DataAsset. */
TORTUNABO_API void TN_DefaultBiomeColors(ETNProcBiome Biome, FLinearColor& OutGround, FLinearColor& OutPath, FLinearColor& OutRock, FLinearColor& OutBed);

/** Vegetación greybox por bioma (formas básicas del motor teñidas). */
TORTUNABO_API void TN_DefaultBiomeScatter(ETNProcBiome Biome, TArray<FTNProcScatterLayer>& Out);

/** Peligros greybox por bioma: fauna acuática en C++ y los BPs de enemigos, ítems y trampas del juego. */
TORTUNABO_API void TN_DefaultBiomeHazards(ETNProcBiome Biome, TArray<FTNProcHazardEntry>& Out);
