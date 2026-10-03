#pragma once

#include "CoreMinimal.h"

/**
 * Qué dice la segunda línea de cada jugador en el menú de pausa (la lista de tortugas de arriba y la página «Sala»), como
 * lógica pura (sin mundo ni PlayerState). La usa UTN_PauseMenuWidget; los tests de Tortunabo.UI.PlayerRow la cubren.
 */
namespace TNPlayerRowRules
{
	enum class ESub : uint8
	{
		/** «Tú»: partida local, sin servidor al que medirle el ping. */
		You,
		/** «Tú · anfitrión»: el anfitrión es el servidor, no tiene ping contra nadie. */
		YouHost,
		/** «Tú · 42 ms»: un invitado, con su propio ping al anfitrión (#256). */
		YouPing,
		/** «Anfitrión», en la lista de un invitado. */
		Host,
		/** «42 ms»: el ping de otra tortuga. */
		Ping,
	};

	/**
	 * @param bMe            La fila es la del jugador de esta máquina.
	 * @param bRowHost       La fila es la del anfitrión.
	 * @param bLocalIsClient Esta máquina es un invitado (NM_Client): es la única que tiene un ping propio que enseñar.
	 *                       El anfitrión lo es contra nadie y una partida local no tiene servidor.
	 */
	inline ESub Decide(bool bMe, bool bRowHost, bool bLocalIsClient)
	{
		if (bMe)
		{
			if (bRowHost)
			{
				return ESub::YouHost;
			}
			return bLocalIsClient ? ESub::YouPing : ESub::You;
		}
		return bRowHost ? ESub::Host : ESub::Ping;
	}

	/** true si la línea lleva un ping. */
	inline bool ShowsPing(ESub Sub)
	{
		return Sub == ESub::YouPing || Sub == ESub::Ping;
	}
}
