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

/**
 * Modo de juego. Classic y Survival viajan al mapa de chunks de siempre (LVL_Run); Survival, con ?game=Survival.
 * FreeForAll (Todos contra Todos, #651) viaja a LVL_Tct con ?game=Tct (ATN_TctGameMode). Karts y Rally no tienen modo de
 * juego desde #848: se conservan solo para que su número no se reutilice.
 * Los valores se guardan como número en las salas (TNRoomKeys::Mode): cada uno lleva su número fijo, que no se cambia
 * nunca; los nuevos van al final con el siguiente. Si cambia el significado de un número, sube TNRoomKeys::ModeSchemaVersion.
 */
UENUM(BlueprintType)
enum class ETNProcGameMode : uint8
{
	Coop       = 0 UMETA(DisplayName = "Cooperativo"),
	Race       = 1 UMETA(DisplayName = "Carrera (todos contra todos)"),
	TwoVsTwo   = 2 UMETA(DisplayName = "2 vs 2"),
	Classic    = 3 UMETA(DisplayName = "Clásico (chunks)"),
	Survival   = 4 UMETA(DisplayName = "Supervivencia"),
	/** Sin modo de juego desde #848 (número reservado). */
	Karts      = 5 UMETA(DisplayName = "Karts (retirado)"),
	FreeForAll = 6 UMETA(DisplayName = "Todos contra Todos"),
	/** Sin modo de juego desde #848 (número reservado). */
	Rally      = 7 UMETA(DisplayName = "Rally (retirado)"),
	Count      = 8 UMETA(Hidden)
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

/** Tipo de cruce colosal a distinto nivel. */
UENUM(BlueprintType)
enum class ETNProcCrossingType : uint8
{
	/** Arco/puente de roca colosal: el tramo alto pasa por encima, abierto por debajo. */
	Bridge  UMETA(DisplayName = "Puente colosal"),
	/** Muralla colosal: el tramo alto va por su adarve y el bajo la cruza por una puerta altísima. */
	Wall    UMETA(DisplayName = "Muralla con puerta")
};
