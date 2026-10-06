#pragma once

#include "CoreMinimal.h"

class UWorld;
class ATN_BeachElement;

/**
 * La cima de los castillos con patio o terraza (#741): el castillo enorme y el castillo con salas llevan, como las
 * fortalezas, una catapulta potenciada que lanza hacia delante por la ruta y un cofre con lo mejor de la carrera para
 * cualquier puesto (TNBeach::FlagBoosted y TNBeach::FlagSummitPrize). Los crea el servidor con el castillo y los destruye
 * con él. Aquí, los sitios del castillo enorme (el del castillo con salas los pone su propia planta, TN_BeachSandDungeon.cpp)
 * y la creación de la pareja.
 */
namespace TNBeachCastlePrizes
{
	/** Tamaño de la catapulta de estos castillos: el menor (brazo de 8,2 m), para que quepa en el patio del enorme. */
	constexpr float CatapultSize = 0.72f;

	/** Medio ancho de la catapulta con su cartel (cm): a un lado (SideOf) el cartel queda a ~3,1 m de su eje. */
	constexpr double CatapultSignReach = 313.0;

	/** Sitios del patio del castillo enorme en el espacio de su malla (tamaño 1, puerta al +X); la malla es la de BuildSandCastleHuge. */
	struct FHugeCastlePlan
	{
		/** Catapulta: pie del brazo, en la franja +Y del patio (entre el torreón y la muralla), mirando al -X del castillo. */
		FVector CatapultAt = FVector::ZeroVector;
		double CatapultYaw = 180.0;
		/** Cofre: en la franja -X del patio, de cara al centro. */
		FVector ChestAt = FVector::ZeroVector;
		double ChestYaw = 0.0;
	};

	/** Los sitios, con la catapulta corrida para que su cartel (lado SideOf de CatapultSpecSeed) no se meta en el torreón ni en la muralla. */
	FHugeCastlePlan PlanHugeCastle(int32 CatapultSpecSeed);

	/** Semillas de la catapulta y del cofre de un castillo de semilla CastleSeed. */
	int32 CatapultSeedOf(int32 CastleSeed);
	int32 ChestSeedOf(int32 CastleSeed);

	/** Crea la catapulta potenciada y el cofre de cima en las posiciones dadas (sin escala); los añade a OutSpawned. */
	void SpawnSummit(UWorld* World, const FTransform& CatapultXf, int32 CatapultSeed, const FTransform& ChestXf, int32 ChestSeed,
		TArray<ATN_BeachElement*>& OutSpawned);

	/** Crea los de un castillo enorme cuya malla está en CastleXf (con su escala y su giro, en el mundo). */
	void SpawnHugeCastle(UWorld* World, const FTransform& CastleXf, int32 CastleSeed, TArray<ATN_BeachElement*>& OutSpawned);
}
