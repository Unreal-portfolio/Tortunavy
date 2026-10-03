#pragma once

#include "CoreMinimal.h"

/**
 * Entrada tardía y reconexión (#345) como funciones PURAS, igual que TN_SurvivalRules.h: sin mundo ni actores, para que
 * los tests (Tortunabo.Survival.LateJoin y Tortunabo.Net.Reconnect) prueben exactamente lo que decide ATN_RunGameMode.
 *
 * Quien vuelve a la misma sala recupera su PlayerState inactivo (AGameMode::FindInactivePlayer): puntos, vivo o muerto
 * y meta. Lo que cada modo hace con él y con quien llega nuevo lo marca su ETNLateJoinPolicy.
 */

/** Cómo trata un modo a quien entra con la partida en marcha. */
enum class ETNLateJoinPolicy : uint8
{
	/** Clásico y Carrera: entra jugando desde la salida y con la carrera a cero (lo de siempre). */
	FreshStart,
	/** Supervivencia: quien no estaba al empezar, o se fue, no puede ganarla: espera como espectador. */
	SpectateUntilMatchEnds,
	/** Coop: quien vuelve recupera su estado; vivo (o nuevo), aparece en un punto seguro del camino. */
	ResumeOnPath,
	/** Carrera y 2vs2 del mapa procedural: recupera su estado y espera como espectador a la ronda siguiente. */
	SpectateUntilNextRound,
};

/** Dónde aparece el jugador que entra. */
enum class ETNJoinRole : uint8
{
	/** Con un pawn en la salida, como al empezar. */
	PlayFromStart,
	/** Con un pawn en un punto seguro del camino ya recorrido (sin sitio, espectador). */
	PlayOnPath,
	/** Sin pawn, mirando a los demás. */
	Spectate,
};

/** Lo que el GameMode sabe del jugador al entrar. */
struct FTNJoinContext
{
	ETNLateJoinPolicy Policy = ETNLateJoinPolicy::FreshStart;
	/** La partida (o la ronda) ya está en juego. */
	bool bMatchInProgress = false;
	/** Vuelve a la sala y ha recuperado su PlayerState inactivo. */
	bool bReactivated = false;
	/** Estado recuperado (solo cuenta con bReactivated). */
	bool bWasAlive = true;
	bool bHadFinished = false;
};

struct FTNJoinDecision
{
	ETNJoinRole Role = ETNJoinRole::PlayFromStart;
	/** Empieza de cero (ATN_CoopPlayerState::ResetForNewRace). false = conserva lo que traía al volver. */
	bool bResetRaceState = true;
	/** No juega la partida (o la ronda) en curso: no cuenta como vivo ni puede ganarla. */
	bool bSitsOut = false;
};

/** Estado de carrera del PlayerState que sobrevive a una desconexión (ATN_CoopPlayerState::CopyProperties). */
struct FTNReconnectState
{
	bool bIsAlive = true;
	bool bIsDBNO = false;
	bool bHasFinishedRun = false;
	bool bIsEliminated = false;
};

namespace TNLateJoinLogic
{
	/** Cómo entra el jugador según el modo, si la partida está en marcha y lo que traía al volver. */
	inline FTNJoinDecision DecideJoin(const FTNJoinContext& Context)
	{
		FTNJoinDecision Decision;
		if (!Context.bMatchInProgress || Context.Policy == ETNLateJoinPolicy::FreshStart)
		{
			return Decision;
		}

		Decision.bResetRaceState = !Context.bReactivated;
		switch (Context.Policy)
		{
		case ETNLateJoinPolicy::ResumeOnPath:
		{
			const bool bOutOfRace = Context.bReactivated && (!Context.bWasAlive || Context.bHadFinished);
			Decision.Role = bOutOfRace ? ETNJoinRole::Spectate : ETNJoinRole::PlayOnPath;
			break;
		}
		case ETNLateJoinPolicy::SpectateUntilMatchEnds:
		case ETNLateJoinPolicy::SpectateUntilNextRound:
		default:
			Decision.Role = ETNJoinRole::Spectate;
			Decision.bSitsOut = true;
			break;
		}
		return Decision;
	}

	/**
	 * Lo que se guarda de quien se desconecta. Derribado (DBNO) cuenta como muerto: irse y volver no puede servir para
	 * esquivar el desangrado ni para que te reanimen gratis.
	 */
	inline FTNReconnectState SanitizeForReconnect(const FTNReconnectState& State)
	{
		FTNReconnectState Out = State;
		if (Out.bIsDBNO)
		{
			Out.bIsDBNO = false;
			Out.bIsAlive = false;
		}
		return Out;
	}
}
