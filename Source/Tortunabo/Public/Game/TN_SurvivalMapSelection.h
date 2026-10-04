#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Game/TN_SurvivalRules.h"
#include "World/ProcMap/TN_SurvivalCatalog.h"

/**
 * Elección del mapa de cada nivel de Supervivencia sobre el catálogo de 50 mapas (#518, decisión en #143).
 * Funciones PURAS, como TN_SurvivalRules.h: los tests (Tortunabo.Survival.Catalogo.Eleccion) prueban lo que corre
 * en juego.
 *
 *  - El nivel N pide la dificultad min(N, 5) y toma una entrada de esa dificultad que no haya salido en la partida.
 *  - La elección sale de la semilla de la partida y del nivel: la misma semilla repite la misma partida.
 *  - Única excepción a no repetir: cuando ya han salido todos los mapas de la dificultad (los 14 de la 5, nivel 19
 *    en adelante), se olvidan los jugados y se vuelve a elegir entre todos, salvo el que acaba de salir.
 *  - El mapa de pruebas (TNSurvivalCatalog::TestMaps) nunca sale en una partida; solo con ?SurvivalMap=6.
 *
 * Solo elige el servidor (ATN_ChunkManager::BuildLevel). Los invitados reciben la semilla y la dificultad del mapa
 * por la configuración replicada del generador y construyen el mismo mapa.
 */

/** Mapa elegido para un nivel. */
struct FTNSurvivalMapPick
{
	/** Semilla de la entrada del catálogo. */
	uint32 Seed = 0;
	/** Dificultad de la entrada (la que se pasa al generador). 0 = sin mapa. */
	int32 Difficulty = 0;
	/** true si para elegirlo se olvidaron los mapas jugados (ya habían salido todos los de su dificultad). */
	bool bForgotPlayed = false;

	bool IsValid() const { return Difficulty > 0; }
};

namespace TNSurvivalMapSelection
{
	/** Semillas del catálogo de una dificultad, en el orden del catálogo (sin el mapa de pruebas). */
	inline TArray<uint32> MapsOfDifficulty(int32 Difficulty)
	{
		TArray<uint32> Out;
		for (const TNSurvivalCatalog::FMapEntry& M : TNSurvivalCatalog::Maps)
		{
			if (M.Difficulty == Difficulty)
			{
				Out.Add(M.Seed);
			}
		}
		return Out;
	}

	/** Semilla del sorteo de un nivel: depende de la semilla de la partida y del nivel, no del orden de llamadas. */
	inline int32 LevelDrawSeed(int32 MatchSeed, int32 Level)
	{
		return static_cast<int32>(HashCombine(GetTypeHash(MatchSeed), GetTypeHash(Level)));
	}

	/**
	 * Mapa del nivel Level en la partida de semilla MatchSeed.
	 * @param Played  Mapas que ya han salido en la partida, en orden (el último es el del nivel anterior).
	 * @return El mapa elegido; inválido solo si el catálogo no tiene mapas de esa dificultad.
	 */
	inline FTNSurvivalMapPick PickLevelMap(int32 MatchSeed, int32 Level, const TArray<uint32>& Played)
	{
		FTNSurvivalMapPick Pick;
		const int32 Difficulty = TNSurvivalLogic::LevelMapDifficulty(Level);
		const TArray<uint32> All = MapsOfDifficulty(Difficulty);
		if (All.Num() == 0)
		{
			return Pick;
		}

		TArray<uint32> Free = All.FilterByPredicate([&Played](uint32 Seed) { return !Played.Contains(Seed); });
		if (Free.Num() == 0)
		{
			// Ya salieron todos: se olvidan los jugados, pero el primero tras olvidar no es el que acaba de salir.
			Pick.bForgotPlayed = true;
			const uint32 Last = Played.Num() > 0 ? Played.Last() : 0u;
			Free = All.FilterByPredicate([Last](uint32 Seed) { return Seed != Last; });
			if (Free.Num() == 0)
			{
				Free = All;
			}
		}

		const FRandomStream Stream(LevelDrawSeed(MatchSeed, Level));
		Pick.Seed = Free[Stream.RandRange(0, Free.Num() - 1)];
		Pick.Difficulty = Difficulty;
		return Pick;
	}

	/**
	 * Mapa pedido con ?SurvivalMap=<semilla>: una entrada del catálogo o el mapa de pruebas, con su dificultad.
	 * @return Inválido si la semilla no está en el catálogo.
	 */
	inline FTNSurvivalMapPick PickForcedMap(uint32 MapSeed)
	{
		FTNSurvivalMapPick Pick;
		if (const TNSurvivalCatalog::FMapEntry* Entry = TNSurvivalCatalog::FindMap(MapSeed))
		{
			Pick.Seed = Entry->Seed;
			Pick.Difficulty = Entry->Difficulty;
		}
		return Pick;
	}

	/** Mapas jugados tras jugar Pick: una copia nueva con él al final (solo él si para elegirlo se olvidaron). */
	inline TArray<uint32> RecordPlayed(const TArray<uint32>& Played, const FTNSurvivalMapPick& Pick)
	{
		TArray<uint32> Out = Pick.bForgotPlayed ? TArray<uint32>() : Played;
		if (Pick.IsValid())
		{
			Out.Add(Pick.Seed);
		}
		return Out;
	}
}
