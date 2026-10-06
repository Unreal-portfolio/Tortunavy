#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"

/**
 * Lógica pura de los RPC de pruebas (TNBooth): decide si el servidor
 * atiende la llamada. Sin UWorld ni controlador, para que el test cubra la regla
 * que usa AMP_GamePlayerController en producción.
 *
 * Regla: nunca en Shipping; en Standalone siempre (no hay invitados); en un
 * servidor escucha solo si quien llama es el controlador local del anfitrión;
 * en un servidor dedicado nunca (no hay anfitrión, todos son remotos).
 */
namespace TNDebugRpcLogic
{
	/** @brief true si la build actual es Shipping (la de Steam). */
	constexpr bool IsShippingBuild()
	{
		return UE_BUILD_SHIPPING != 0;
	}

	/**
	 * @brief Decide si un RPC de pruebas solo para el anfitrión puede ejecutarse.
	 * @param bShippingBuild  Build Shipping: siempre se rechaza.
	 * @param NetMode         Modo de red del mundo del servidor.
	 * @param bCallerIsLocal  El controlador que llama es local en el servidor (el anfitrión).
	 */
	inline bool CanRunHostOnlyDebugRpc(bool bShippingBuild, ENetMode NetMode, bool bCallerIsLocal)
	{
		if (bShippingBuild)
		{
			return false;
		}
		if (NetMode == NM_Standalone)
		{
			return true;
		}
		return NetMode == NM_ListenServer && bCallerIsLocal;
	}
}
