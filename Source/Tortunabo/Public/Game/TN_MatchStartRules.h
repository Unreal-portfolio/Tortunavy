#pragma once

#include "CoreMinimal.h"

/**
 * Regla del arranque de la partida de ATN_RunGameMode como funciones PURAS: sin mundo ni actores, para que los tests
 * (Tortunabo.MatchStart) prueben exactamente lo que corre en juego.
 *
 * La partida empieza una sola vez. En un servidor que abre el mapa directamente (-game, PIE o el host de un viaje
 * no seamless), el PostLogin del jugador local llega ANTES que el BeginPlay del GameMode: UEngine::LoadMap hace
 * SpawnPlayActor antes de UWorld::BeginPlay. Ese PostLogin no puede arrancar la partida, porque la espera de
 * jugadores aún no ha empezado (no se ha leído cuántos vienen del lobby), y el BeginPlay no puede reabrir una
 * partida ya empezada.
 */
struct FTNMatchStartState
{
	/** El BeginPlay del GameMode ya abrió la espera de jugadores. */
	bool bStagingBegun = false;
	/** La partida ya arrancó (InProgress). */
	bool bStarted = false;
};

namespace TNMatchStartLogic
{
	/** Estado tras el BeginPlay del GameMode: se abre la espera; una partida ya empezada sigue empezada. */
	inline FTNMatchStartState BeginStaging(const FTNMatchStartState& State)
	{
		FTNMatchStartState Next = State;
		Next.bStagingBegun = true;
		return Next;
	}

	/** Estado tras arrancar la partida. */
	inline FTNMatchStartState MarkStarted(const FTNMatchStartState& State)
	{
		FTNMatchStartState Next = State;
		Next.bStarted = true;
		return Next;
	}

	/** true si la partida está esperando jugadores: después del BeginPlay del GameMode y antes de arrancar. */
	inline bool IsWaitingForPlayers(const FTNMatchStartState& State)
	{
		return State.bStagingBegun && !State.bStarted;
	}

	/**
	 * Si TryStartMatch arranca la partida ahora.
	 * @param State     Estado del arranque.
	 * @param Connected Jugadores conectados.
	 * @param Expected  Jugadores que se esperan del lobby.
	 */
	inline bool ShouldStart(const FTNMatchStartState& State, int32 Connected, int32 Expected)
	{
		return IsWaitingForPlayers(State) && Connected >= Expected;
	}
}
