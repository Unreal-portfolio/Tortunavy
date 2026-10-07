#pragma once

#include "CoreMinimal.h"

/**
 * Reconexión (#345) como funciones PURAS: sin mundo ni actores, para que los tests (Tortunabo.Net.Reconnect) prueben
 * exactamente lo que guarda ATN_CoopPlayerState::CopyProperties.
 *
 * Quien vuelve a la misma sala recupera su PlayerState inactivo (AGameMode::FindInactivePlayer); ATN_RunGameMode lo
 * pone a cero y entra jugando desde la salida, como quien llega nuevo.
 */

/** Estado de partida del PlayerState que sobrevive a una desconexión (ATN_CoopPlayerState::CopyProperties). */
struct FTNReconnectState
{
	bool bIsAlive = true;
	bool bIsDBNO = false;
	bool bHasFinishedRun = false;
	bool bIsEliminated = false;
};

namespace TNLateJoinLogic
{
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
