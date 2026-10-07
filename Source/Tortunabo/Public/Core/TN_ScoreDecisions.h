#pragma once

#include "CoreMinimal.h"

/**
 * Decisiones de scoring como funciones PURAS (Fase 4.3): sin UWorld, sin actores,
 * sin estado — entradas escalares, salida determinista. TN_CoopGameState delega
 * aquí para que la lógica testeada por el Automation framework sea EXACTAMENTE
 * la que corre en juego, no una copia.
 *
 * Regla: nada de side effects ni lecturas de singletons. Si una decisión necesita
 * algo del mundo (score replicado, acumulador persistido), el actor lo lee y lo
 * pasa como parámetro.
 */

namespace TNScoreLogic
{
	/**
	 * Delta del RaceScore local pendiente de persistir durante Results.
	 * Persistencia POR DELTA e idempotente: si Results llega antes que el último
	 * update replicado de RaceScore (race de replicación en clientes), se persiste
	 * lo visible ahora y OnRep_RaceScore reinvoca para persistir lo que falte —
	 * nunca se duplica lo ya guardado.
	 * @return Delta a persistir, en [0, ∞). Score menor que lo persistido → 0.
	 */
	inline int32 ComputePersistDelta(int32 CurrentRaceScore, int32 AlreadyPersisted)
	{
		return FMath::Max(0, CurrentRaceScore - AlreadyPersisted);
	}

	/**
	 * Clave de orden del scoreboard de resultados (menor = más arriba):
	 * ranks válidos primero (1, 2, 3...), sin-rank (FinishRank <= 0) después,
	 * eliminados siempre al final aunque tuvieran rank.
	 */
	/**
	 * Lo ya guardado de la partida en curso (PersistedScoreThisRace) tras un cambio de MatchFlowState: fuera de Results
	 * vuelve a 0, en el servidor (BroadcastFlowStateChange) y en los clientes (OnRep_MatchFlowState). Antes solo el
	 * servidor lo reiniciaba: en un cliente, tras «Volver a jugar», la partida siguiente guardaba max(0, nuevo − viejo).
	 */
	inline int32 PersistedAfterFlowChange(int32 AlreadyPersisted, bool bIsResults)
	{
		return bIsResults ? AlreadyPersisted : 0;
	}

	inline int32 ComputeResultSortKey(bool bEliminated, int32 FinishRank)
	{
		if (bEliminated)
		{
			return INT32_MAX;
		}
		return FinishRank > 0 ? FinishRank : INT32_MAX - 1;
	}
}
