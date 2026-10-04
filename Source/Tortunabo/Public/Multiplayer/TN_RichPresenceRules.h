#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcMapEnums.h"

/**
 * Reglas de la presencia de Steam (lo que ven los amigos en su lista): de qué modo, ronda o nivel y plazas sale qué texto.
 * Sin estado ni red, para probarlas sin Steam (tests Tortunabo.Online.RichPresence). Las aplica UTN_RichPresenceSubsystem.
 *
 * Cada estado tiene una clave de Steam («steam_display»: #TN_<Clave>) y un texto en el idioma de esta máquina («status»).
 * La clave se traduce en el idioma de quien mira con el archivo de presencia que se sube a Steamworks
 * (Scripts/steam/rich_presence.vdf, generado desde los Game.po); sus %parámetros% salen de Params.
 */
enum class ETNPresenceMode : uint8
{
	Menu,
	Lobby,
	Coop,
	Race,
	TwoVsTwo,
	Survival,
	/** Una partida de un modo sin texto propio (vista del terreno, mapas de prueba...). */
	Playing,
};

/** Lo que se sabe de la partida en esta máquina. */
struct TORTUNABO_API FTNPresenceState
{
	ETNPresenceMode Mode = ETNPresenceMode::Menu;
	/** Tortugas en la sala y plazas (lobby). Plazas 0: no se sabe y no se enseña la cuenta. */
	int32 Players = 0;
	int32 MaxPlayers = 0;
	/** Nivel (Coop y Supervivencia) y ronda (Carrera y 2 contra 2), desde 1; 0: no se enseña. */
	int32 Level = 0;
	int32 Round = 0;
};

/** Lo que se manda a Steam. */
struct TORTUNABO_API FTNPresenceInfo
{
	/** Clave de «steam_display», sin la almohadilla (la pone el subsistema de Steam): «TN_CoopLevel». */
	FString Token;
	/** El mismo texto en el idioma de esta máquina («status»: «Ver información del juego» en Steam). */
	FText Status;
	/** Parámetros de la clave: «players», «max», «level», «round» (los {Players}... del texto, en minúsculas). */
	TArray<TPair<FString, FString>> Params;

	/** Todo en una cadena, para no volver a mandar lo mismo. */
	FString Signature() const;
};

namespace TNRichPresence
{
	/**
	 * El modo según la clase del GameMode (AGameStateBase::GameModeClass, que también llega a los invitados) y, en el mapa
	 * procedural, el modo de su GameState. Sin clase: Playing.
	 */
	TORTUNABO_API ETNPresenceMode ModeFor(const UClass* GameModeClass, ETNProcGameMode ProcMode);

	/** Texto, clave y parámetros de un estado. */
	TORTUNABO_API FTNPresenceInfo Build(const FTNPresenceState& State);
}
