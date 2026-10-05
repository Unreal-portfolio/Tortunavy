#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"

/**
 * Qué hace UMP_GameInstance con un fallo de red del motor (UEngine::OnNetworkFailure). El motor también avisa en el anfitrión
 * cuando se cae la conexión de UN invitado (UNetConnection::HandleConnectionTimeout con el driver del servidor), y eso cerraba la
 * partida de todos con «El host abandonó la partida» (#657). Lógica pura, sin mundo: la prueban los tests
 * Tortunabo.Multiplayer.NetworkFailure.
 */
namespace TNNetFailure
{
	/** Fallos del propio driver (no poder escuchar o crearlo): son del anfitrión, no de una conexión. */
	inline bool IsDriverFailure(ENetworkFailure::Type FailureType)
	{
		return FailureType == ENetworkFailure::NetDriverAlreadyExists
			|| FailureType == ENetworkFailure::NetDriverCreateFailure
			|| FailureType == ENetworkFailure::NetDriverListenFailure;
	}

	/**
	 * true si el fallo es de la conexión de un invitado en el servidor (anfitrión o dedicado): el motor cierra esa conexión y
	 * solo sale ese invitado; los demás siguen. Es la regla de UEngine::HandleNetworkFailure, que con un driver de servidor no
	 * hace viajar al anfitrión.
	 * @param FailureType      Tipo de fallo.
	 * @param FailedDriverMode Modo de red del driver que falla (UNetDriver::GetNetMode): NM_Client en un invitado.
	 */
	inline bool IsGuestFailureOnHost(ENetworkFailure::Type FailureType, ENetMode FailedDriverMode)
	{
		const bool bServerDriver = FailedDriverMode == NM_ListenServer || FailedDriverMode == NM_DedicatedServer;
		return bServerDriver && !IsDriverFailure(FailureType);
	}
}
