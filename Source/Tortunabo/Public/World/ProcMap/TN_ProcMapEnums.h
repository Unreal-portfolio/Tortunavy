#pragma once

#include "CoreMinimal.h"
#include "TN_ProcMapEnums.generated.h"

/**
 * Enums del mapa procedural compartidos entre la lógica pura (TNProcMap::) y los
 * actores/DataAssets. Los valores numéricos se usan como índices de arrays en la
 * lógica: no reordenar sin revisar TNProcMap::NumBiomes y las tablas por bioma.
 */

/** Biomas del mapa. Cada región de bioma agrupa varios módulos contiguos. */
UENUM(BlueprintType)
enum class ETNProcBiome : uint8
{
	Jungle    UMETA(DisplayName = "Selva"),
	Beach     UMETA(DisplayName = "Playa"),
	Desert    UMETA(DisplayName = "Desierto"),
	Volcanic  UMETA(DisplayName = "Volcánico"),
	Water     UMETA(DisplayName = "Agua con isletas"),
	Rocky     UMETA(DisplayName = "Acantilados rocosos"),
	Mangrove  UMETA(DisplayName = "Manglar"),
	Human     UMETA(DisplayName = "Zona humana"),
	Count     UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ETNProcDifficulty : uint8
{
	Easy    UMETA(DisplayName = "Fácil"),
	Normal  UMETA(DisplayName = "Normal"),
	Hard    UMETA(DisplayName = "Difícil"),
	Count   UMETA(Hidden)
};

/** Qué son los módulos por los que no pasa el camino principal. */
UENUM(BlueprintType)
enum class ETNProcEmptyModuleMode : uint8
{
	/** Terreno alto e inaccesible que da contexto al camino. */
	Elevated            UMETA(DisplayName = "Paisaje elevado"),
	/** Pueden llevar ramas; lo que no se usa queda elevado. */
	BranchesAndScenery  UMETA(DisplayName = "Ramas y paisaje"),
	/** Terreno accesible sin camino marcado (atajos, secretos). */
	Explorable          UMETA(DisplayName = "Explorable"),
	/** Mezcla aleatoria por módulo de los tres anteriores. */
	Mixed               UMETA(DisplayName = "Mezcla")
};
