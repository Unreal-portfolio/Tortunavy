#pragma once

#include "CoreMinimal.h"

/**
 * Reglas del modo Supervivencia (ATN_SurvivalGameMode) como funciones PURAS, igual que TN_ChunkDecisions.h:
 * sin mundo ni actores, para que los tests (Tortunabo.Survival) prueben exactamente lo que corre en juego.
 *
 * La partida es una sucesión de niveles cortos. Cuando todos los vivos llegan a la meta, se genera otro.
 * En grupo acaba en cuanto queda una sola tortuga viva; si las últimas mueren en el mismo nivel, gana la que
 * murió más cerca de la meta (y, a igual distancia, la que aguantó más). En solitario acaba al morir.
 */

/** Lo que el GameMode sabe de cada jugador para decidir. */
struct FTNSurvivalPlayer
{
	int32 Id = INDEX_NONE;
	bool bAlive = true;
	/** Llegó a la meta del nivel actual (solo cuenta si sigue vivo). */
	bool bFinishedLevel = false;
	/** Nivel en el que murió (0 = vivo). */
	int32 LevelDied = 0;
	/** Segundos de servidor en que murió. */
	float DeathTime = 0.f;
	/** Distancia (uu) que le faltaba hasta la meta al morir. */
	float DeathRemaining = 0.f;
};

enum class ETNSurvivalOutcome : uint8
{
	/** Aún hay vivos jugando el nivel. */
	Continue,
	/** Todos los vivos llegaron a la meta: siguiente nivel. */
	Advance,
	/** Partida acabada con un ganador (WinnerId). */
	Winner,
	/** Solitario: el único jugador murió. */
	SoloOver,
};

struct FTNSurvivalDecision
{
	ETNSurvivalOutcome Outcome = ETNSurvivalOutcome::Continue;
	int32 WinnerId = INDEX_NONE;
};

namespace TNSurvivalLogic
{
	/** true si A aguantó más que B entre dos muertos: nivel más alto; luego más cerca de la meta; luego murió más tarde. */
	inline bool OutlastedBy(const FTNSurvivalPlayer& A, const FTNSurvivalPlayer& B)
	{
		if (A.LevelDied != B.LevelDied) { return A.LevelDied > B.LevelDied; }
		if (!FMath::IsNearlyEqual(A.DeathRemaining, B.DeathRemaining, 1.f)) { return A.DeathRemaining < B.DeathRemaining; }
		return A.DeathTime > B.DeathTime;
	}

	/**
	 * Qué pasa tras un cambio (muerte, meta o salida de un jugador).
	 * @param Players          Jugadores que siguen en la partida.
	 * @param StartingPlayers  Jugadores al empezar: con 1 es solitario y dura hasta que muere.
	 */
	inline FTNSurvivalDecision DecideLevelOutcome(const TArray<FTNSurvivalPlayer>& Players, int32 StartingPlayers)
	{
		FTNSurvivalDecision Result;

		int32 Alive = 0;
		int32 AliveFinished = 0;
		const FTNSurvivalPlayer* LastAlive = nullptr;
		const FTNSurvivalPlayer* BestDead = nullptr;
		for (const FTNSurvivalPlayer& P : Players)
		{
			if (P.bAlive)
			{
				++Alive;
				LastAlive = &P;
				if (P.bFinishedLevel) { ++AliveFinished; }
			}
			else if (!BestDead || OutlastedBy(P, *BestDead))
			{
				BestDead = &P;
			}
		}

		const bool bSolo = StartingPlayers <= 1;
		if (!bSolo && Alive == 1)
		{
			Result.Outcome = ETNSurvivalOutcome::Winner;
			Result.WinnerId = LastAlive->Id;
			return Result;
		}
		if (Alive == 0)
		{
			if (bSolo)
			{
				Result.Outcome = ETNSurvivalOutcome::SoloOver;
				Result.WinnerId = BestDead ? BestDead->Id : INDEX_NONE;
			}
			else
			{
				Result.Outcome = ETNSurvivalOutcome::Winner;
				Result.WinnerId = BestDead ? BestDead->Id : INDEX_NONE;
			}
			return Result;
		}
		if (AliveFinished == Alive)
		{
			Result.Outcome = ETNSurvivalOutcome::Advance;
		}
		return Result;
	}

	/** Puesto final: el ganador primero; los demás, de quien aguantó más a quien menos. Devuelve los Id ordenados. */
	inline TArray<int32> RankPlayers(const TArray<FTNSurvivalPlayer>& Players, int32 WinnerId)
	{
		TArray<FTNSurvivalPlayer> Sorted = Players;
		Sorted.Sort([WinnerId](const FTNSurvivalPlayer& A, const FTNSurvivalPlayer& B)
		{
			if ((A.Id == WinnerId) != (B.Id == WinnerId)) { return A.Id == WinnerId; }
			if (A.bAlive != B.bAlive) { return A.bAlive; }
			return OutlastedBy(A, B);
		});
		TArray<int32> Ids;
		for (const FTNSurvivalPlayer& P : Sorted) { Ids.Add(P.Id); }
		return Ids;
	}

	/** Dificultad 1–5 del mapa del nivel (TNProcMap::MakeSurvivalParams): el nivel N pide min(N, 5). */
	inline int32 LevelMapDifficulty(int32 Level)
	{
		return FMath::Clamp(Level, 1, 5);
	}

	/**
	 * Velocidad (cm/s) de la tormenta del camino en el nivel Level: FirstLevel en el nivel 1 y PerLevel más en cada
	 * siguiente hasta Max. A diferencia del Coop, sin el tope de andar: va más rápida que una tortuga andando, así que
	 * solo se le escapa esprintando, y cada derribo o cada parada a coger objetos le regala terreno.
	 */
	inline float StormSpeedForLevel(int32 Level, float FirstLevel, float PerLevel, float Max)
	{
		return FMath::Min(FirstLevel + PerLevel * FMath::Max(0, Level - 1), Max);
	}
}
