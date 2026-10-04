#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcMapEnums.h"

/**
 * @brief Salas públicas y privadas (Docs/Salas.md): claves de la sesión, códigos, configuración y lo que se lee de una sala.
 *
 * Una sala es la sesión de Steam (o del subsistema NULL en el editor) que crea el anfitrión. Anuncia unos ajustes propios
 * para que la lista y el código funcionen sin conectarse: el índice del nombre (TNRoomNames), el código, si es privada, si
 * está cerrada, el modo y cuánta gente hay. Con Steam, la búsqueda filtra en el servidor por esas claves (PRIVATE = 0 para
 * la lista; ROOMCODE = código para entrar con código); con el NULL se filtra aquí, al leer los resultados.
 */
namespace TNRoomKeys
{
	/** Palabra clave de todas las salas del juego (el AppId 480 de pruebas lo comparten muchos proyectos). */
	inline FName Keywords() { static const FName Key(TEXT("SEARCH_KEYWORDS")); return Key; }
	inline const TCHAR* KeywordsValue() { return TEXT("TortunaboLobby"); }

	/** Índice del nombre de la sala en TNRoomNames (cada uno lo lee en su idioma). */
	inline FName NameId() { static const FName Key(TEXT("ROOMNAME_ID")); return Key; }
	/** Código de la sala (5 caracteres de TNRoomCode). Lo tienen todas; solo se enseña en las privadas. */
	inline FName Code() { static const FName Key(TEXT("ROOMCODE")); return Key; }
	/** 1: privada (no sale en la lista; se entra con el código o por invitación). */
	inline FName Private() { static const FName Key(TEXT("PRIVATE")); return Key; }
	/** 1: cerrada por el anfitrión (no entra nadie más). */
	inline FName Locked() { static const FName Key(TEXT("LOCKED")); return Key; }
	/** Modo elegido (ETNProcGameMode como número). */
	inline FName Mode() { static const FName Key(TEXT("MODE")); return Key; }
	/** Tortugas dentro (el anfitrión incluido). */
	inline FName Players() { static const FName Key(TEXT("PLAYERS")); return Key; }

	/** Búsqueda de lobbies de Steam (SEARCH_PRESENCE del motor; el NULL la ignora). */
	inline FName PresenceSearch() { static const FName Key(TEXT("PRESENCESEARCH")); return Key; }

	/**
	 * Motivos de rechazo al entrar (PreLogin del servidor). Viajan como texto de fallo de red y el cliente los traduce a un
	 * mensaje en su idioma (TNRoomText::RefusedMessage).
	 */
	inline const TCHAR* RefusePrefix() { return TEXT("TNRoom:"); }
	inline const TCHAR* RefuseLocked() { return TEXT("TNRoom:Locked"); }
	inline const TCHAR* RefuseFull() { return TEXT("TNRoom:Full"); }
	inline const TCHAR* RefuseKicked() { return TEXT("TNRoom:Kicked"); }
}

/** Códigos de sala: 5 caracteres de un alfabeto sin los que se confunden (sin O, 0, I, 1 ni L). */
namespace TNRoomCode
{
	inline constexpr int32 Length = 5;

	/** Los 31 caracteres que puede llevar un código. */
	inline const TCHAR* Alphabet() { return TEXT("ABCDEFGHJKMNPQRSTUVWXYZ23456789"); }

	/** Un código nuevo al azar. */
	TORTUNABO_API FString Generate();

	/** true si el carácter (ya en mayúscula) está en el alfabeto. */
	TORTUNABO_API bool IsAllowedChar(TCHAR Char);

	/** Mayúsculas y solo caracteres del alfabeto, como mucho Length. */
	TORTUNABO_API FString Normalize(const FString& Raw);

	/**
	 * El código de un texto pegado: la primera palabra de 5 caracteres válidos («Código: K7M2P» → «K7M2P») o, si no hay
	 * ninguna, los primeros caracteres válidos.
	 */
	TORTUNABO_API FString FromPasted(const FString& Text);

	/** true si tiene los 5 caracteres y todos valen. */
	TORTUNABO_API bool IsComplete(const FString& Code);
}

/** Lo que elige el anfitrión al crear la sala (y lo que la sala es mientras dura). */
struct TORTUNABO_API FTNRoomConfig
{
	ETNProcGameMode Mode = ETNProcGameMode::Coop;
	bool bPrivate = false;
	/** Plazas, el anfitrión incluido: 4, 6 u 8 (TNRoomLimits). */
	int32 MaxPlayers = 8;
	/** Índice en TNRoomNames. */
	int32 NameId = 0;
	FString Code;
	/** Cerrada: no entra nadie nuevo (los que ya estaban sí pueden volver). */
	bool bLocked = false;
	/** Rally: circuito de LVL_Rally; Rally y Karts: tortugas por buggy (1 o 2). */
	FName RallyVariant = FName(TEXT("E01B_espana_rally"));
	int32 RallySeats = 2;
};

/** Plazas que se pueden elegir al crear la sala. */
namespace TNRoomLimits
{
	inline constexpr int32 Options[] = { 4, 6, 8 };
	/** Tope absoluto (la carrera y el lobby están preparados para ocho). */
	inline constexpr int32 Max = 8;
}

/** Una sala encontrada al buscar (lista de partidas públicas o búsqueda por código). */
struct TORTUNABO_API FTNRoomListing
{
	/** Índice en los resultados de la búsqueda que la encontró (para unirse). */
	int32 SearchIndex = INDEX_NONE;
	int32 NameId = INDEX_NONE;
	FString Code;
	FString HostName;
	ETNProcGameMode Mode = ETNProcGameMode::Coop;
	bool bPrivate = false;
	bool bLocked = false;
	int32 Players = 0;
	int32 MaxPlayers = 0;

	bool IsFull() const { return MaxPlayers > 0 && Players >= MaxPlayers; }
	bool CanJoin() const { return !bLocked && !IsFull(); }
};

/** La sala en la que se está ahora (cabecera y página «Sala» del menú de pausa). */
struct TORTUNABO_API FTNRoomSnapshot
{
	bool bValid = false;
	/** Esta máquina es el anfitrión (puede cerrar la sala y echar a alguien). */
	bool bIsHost = false;
	int32 NameId = INDEX_NONE;
	FString Code;
	FString HostName;
	bool bPrivate = false;
	bool bLocked = false;
	int32 Players = 0;
	int32 MaxPlayers = 0;
};

/** Para qué es la búsqueda de sesiones en marcha (UMP_GameInstance). */
enum class ETNRoomSearch : uint8
{
	None,
	/** Lista de salas públicas. */
	List,
	/** Sala de un código. */
	Code,
	/** «Unirse a la primera» (FindAndJoinSession). */
	QuickJoin,
};

/** Cuál de las búsquedas que esperan turno se queda con él (UMP_GameInstance::StartRoomSearch). */
namespace TNRoomSearchRules
{
	/**
	 * Importancia de una búsqueda cuando solo hay sitio para una en la cola. «Unirse a la primera» tapa el menú con la
	 * pantalla de carga y solo se destapa al contestar; el código tiene el aviso «Buscando la sala X...» esperando
	 * respuesta; la lista se repite sola cada pocos segundos y no se echa en falta.
	 */
	inline int32 Priority(ETNRoomSearch Purpose)
	{
		switch (Purpose)
		{
		case ETNRoomSearch::QuickJoin: return 3;
		case ETNRoomSearch::Code: return 2;
		case ETNRoomSearch::List: return 1;
		default: return 0;
		}
	}

	/** true si Incoming puede ocupar el turno de Queued (el vacío lo ocupa cualquiera): igual o más importante, nunca menos. */
	inline bool CanTakeQueue(ETNRoomSearch Queued, ETNRoomSearch Incoming)
	{
		return Incoming != ETNRoomSearch::None && Priority(Incoming) >= Priority(Queued);
	}
}

/**
 * Qué está haciendo UMP_GameInstance con la sesión de la sala: cerrar la que quedaba, crear la nueva, entrar en otra o viajar
 * al mapa. Mientras algo esté en marcha, otro «Crear» o «Unirse» sobra: destruiría la sesión que se está creando.
 */
struct FTNRoomOpState
{
	/** Cerrando la sesión vieja; al acabar se crea la sala o se entra en otra. */
	bool bHostAfterDestroy = false;
	bool bJoinAfterDestroy = false;
	/** CreateSession o JoinSession lanzado y sin respuesta del subsistema online. */
	bool bCreating = false;
	bool bJoining = false;
	/** Sesión creada o unida: falta el viaje (hasta que carga el mapa de la partida o vuelve el menú). */
	bool bTravelling = false;

	/** true mientras haya algo en marcha. */
	bool IsBusy() const { return IsWaitingOnline() || bTravelling; }

	/** true si espera la respuesta del subsistema online (cerrar, crear o entrar), no solo el viaje. */
	bool IsWaitingOnline() const { return bHostAfterDestroy || bJoinAfterDestroy || bCreating || bJoining; }
};

namespace TNRoomOpRules
{
	/** Segundos que se espera la respuesta de Steam al cerrar, crear o entrar antes de darlo por fallido. */
	inline constexpr double OnlineTimeoutSeconds = 30.0;

	/** Segundos que se espera a que cargue el mapa tras crear o entrar (la conexión tiene sus propios plazos en el motor). */
	inline constexpr double TravelTimeoutSeconds = 120.0;

	/** true si la operación lleva Elapsed segundos sin acabar y ya no hay que esperarla. */
	inline bool HasTimedOut(const FTNRoomOpState& State, double Elapsed)
	{
		if (!State.IsBusy())
		{
			return false;
		}
		return Elapsed > (State.IsWaitingOnline() ? OnlineTimeoutSeconds : TravelTimeoutSeconds);
	}
}

/** Textos de las salas en el idioma del que mira. */
namespace TNRoomText
{
	/** «Pública» / «Privada». */
	TORTUNABO_API FText Visibility(bool bPrivate);

	/** Mensaje claro para un rechazo del servidor (TNRoomKeys::Refuse...) o un fallo al unirse. */
	TORTUNABO_API FText RefusedMessage(const FString& Reason);
}
