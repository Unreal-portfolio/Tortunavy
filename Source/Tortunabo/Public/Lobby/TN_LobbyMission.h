#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcMapEnums.h"

class UObject;

/**
 * La «misión» de la próxima partida: el modo y la dificultad que se eligen antes de salir del lobby. Lo comparten el
 * menú principal (UMP_MainMenuWidget, al crear partida), el General Galápago (pestaña «Misión» de UTN_BriefingWidget) y
 * los selectores del lobby (ATN_ProcModeSelector), para que ninguno repita la lógica.
 *
 * La elección vive en la GameInstance del anfitrión (UMP_GameInstance::SelectedProcMode y SelectedProcDifficulty), que
 * ATN_HQGameMode lee al viajar. Lo que ven los demás se replica en el lobby: el general
 * (ATN_GeneralBriefing::SyncMissionFromGameInstance, con su pizarra) y las etiquetas de los selectores
 * (ATN_ProcModeSelector::SyncFromGameInstance). Sin RPC: solo elige el anfitrión, y en servidor escucha su interfaz corre
 * en el servidor; un cliente no puede cambiarla.
 */
namespace TNLobbyMission
{
	/**
	 * Los modos que se ofrecen al crear partida, en la sala y con el general, en su orden (#632): todos los jugables. El
	 * 2 vs 2 se puede elegir, pero se juega como Carrera si al salir del lobby no son exactamente cuatro.
	 */
	inline constexpr ETNProcGameMode MenuModes[] = { ETNProcGameMode::Coop, ETNProcGameMode::Race, ETNProcGameMode::Survival,
		ETNProcGameMode::FreeForAll, ETNProcGameMode::TwoVsTwo };

	/**
	 * true si el modo se puede jugar en esta build. Todos contra Todos necesita su arena por defecto, que se lee de
	 * Scripts/ y no se empaqueta (ATN_TctGameMode::HasDefaultArena): en una build cocinada no se ofrece. Karts y Rally
	 * no tienen modo de juego desde #848. El resto, siempre.
	 */
	TORTUNABO_API bool IsModePlayable(ETNProcGameMode Mode);

	/** Los de MenuModes en su orden, sin Todos contra Todos si no se puede jugar. */
	TORTUNABO_API TArray<ETNProcGameMode> FilterMenuModes(bool bFreeForAllPlayable);

	/** Los modos que se ofrecen de verdad (FilterMenuModes con lo que hay en esta build): menú, salas y general. */
	TORTUNABO_API TArray<ETNProcGameMode> GetMenuModes();

	/** Las dificultades, en su orden. */
	inline constexpr ETNProcDifficulty Difficulties[] = { ETNProcDifficulty::Easy, ETNProcDifficulty::Normal, ETNProcDifficulty::Hard };

	/** Nombre del modo («Cooperativo», «Carrera», «2 vs 2», «Clásico», «Supervivencia», «Todos contra Todos»). */
	TORTUNABO_API FText ModeName(ETNProcGameMode Mode);

	/** El modo si es uno de GetMenuModes; si no (Clásico o uno que no se puede jugar en esta build), Cooperativo. Lo que admite una sala nueva. */
	TORTUNABO_API ETNProcGameMode NormalizeMenuMode(ETNProcGameMode Mode);

	/**
	 * Nombre de la misión: el del modo y, en Todos contra Todos, también la arena («Todos contra Todos · Coliseo»).
	 * MapVariant es el de GetHostMissionMap.
	 */
	TORTUNABO_API FText MissionTitle(ETNProcGameMode Mode, FName MapVariant);

	/** Arena de Todos contra Todos que se elige por defecto (la del modo, ATN_TctGameMode::DefaultArenaVariant). */
	inline const TCHAR* const DefaultTctArena = TEXT("A01_diana");

	/**
	 * true si el texto de un manifest de variante es una arena de Todos contra Todos: "mode": "tct" y al menos un trozo
	 * en "cells".
	 */
	TORTUNABO_API bool IsTctArenaManifest(const FString& JsonText);

	/**
	 * Las arenas en el orden del menú: las conocidas en su orden (A01_diana primero) y las demás por nombre. Sin repetidos
	 * ni vacíos.
	 */
	TORTUNABO_API TArray<FName> SortTctArenas(const TArray<FName>& Arenas);

	/**
	 * Las arenas de Todos contra Todos que se ofrecen: las de Scripts/terrain_volumes/Variants con "mode": "tct". Se buscan
	 * una vez por sesión. En un juego empaquetado sin manifests no hay ninguna.
	 */
	TORTUNABO_API const TArray<FName>& TctArenaOptions();

	/** La arena elegida si está entre las opciones; si no, la primera (NAME_None si no hay ninguna). */
	TORTUNABO_API FName ResolveTctArena(FName Selected, const TArray<FName>& Options);

	/** Nombre de una arena: las conocidas, traducido; las demás (variantes nuevas), con su identificador. */
	TORTUNABO_API FText TctArenaName(FName Variant);

	/** URL del viaje del lobby a Todos contra Todos: <mapa>?game=Tct?Arena=<v>. */
	TORTUNABO_API FString TctTravelURL(FName Arena, const FString& TctMapPath);

	/** Arena de Todos contra Todos que tiene ahora la GameInstance de esta máquina (ya resuelta). */
	TORTUNABO_API FName GetHostTctArena(const UObject* WorldContext);

	/** Anfitrión: fija la arena de Todos contra Todos (una de TctArenaOptions) y la replica en el lobby. false en un cliente. */
	TORTUNABO_API bool SetTctArena(const UObject* WorldContext, FName Arena);

	/**
	 * El mapa de la misión del anfitrión según el modo: la arena en Todos contra Todos y NAME_None en los demás. Es lo que
	 * el general replica para que todos vean «Todos contra Todos · Coliseo».
	 */
	TORTUNABO_API FName GetHostMissionMap(const UObject* WorldContext);

	/** Nombre de la dificultad («Fácil», «Normal», «Difícil»). */
	TORTUNABO_API FText DifficultyName(ETNProcDifficulty Difficulty);

	/** Una línea que explica el modo, sin su nombre delante (menú principal y pestaña «Misión» del general). */
	TORTUNABO_API FText ModeBlurb(ETNProcGameMode Mode);

	/** Una línea que explica la dificultad, sin su nombre delante. */
	TORTUNABO_API FText DifficultyBlurb(ETNProcDifficulty Difficulty);

	/**
	 * El siguiente modo del selector del lobby: Clásico → Coop → Carrera → 2 vs 2 (este, solo con exactamente 4 jugadores) →
	 * Supervivencia → Todos contra Todos (este, solo si bFreeForAllPlayable). Karts y Rally no se ofrecen nunca.
	 */
	TORTUNABO_API ETNProcGameMode NextSelectorMode(ETNProcGameMode Current, int32 ConnectedPlayers, bool bFreeForAllPlayable);

	/** NextSelectorMode con lo que hay en esta build (IsModePlayable). */
	TORTUNABO_API ETNProcGameMode NextSelectorMode(ETNProcGameMode Current, int32 ConnectedPlayers);

	/** true si en esta máquina se puede elegir la misión: es el anfitrión (servidor escucha o partida sola). */
	TORTUNABO_API bool CanLocalPlayerChoose(const UObject* WorldContext);

	/** Modo y dificultad que tiene ahora la GameInstance de esta máquina (la buena es la del anfitrión). */
	TORTUNABO_API ETNProcGameMode GetHostMode(const UObject* WorldContext);
	TORTUNABO_API ETNProcDifficulty GetHostDifficulty(const UObject* WorldContext);

	/**
	 * Anfitrión: fija el modo en su GameInstance y lo replica en el lobby (general y selectores). false en un cliente, sin
	 * GameInstance o si el modo no se puede jugar en esta build. El 2 vs 2 se puede elegir con cualquier número de
	 * jugadores (#632): si al salir del lobby no son cuatro, ATN_HQGameMode lo juega como Carrera. Con el mismo modo no
	 * cambia nada (true).
	 */
	TORTUNABO_API bool SetMode(const UObject* WorldContext, ETNProcGameMode Mode);

	/** Anfitrión: fija la dificultad y la replica en el lobby. false en un cliente o sin GameInstance. */
	TORTUNABO_API bool SetDifficulty(const UObject* WorldContext, ETNProcDifficulty Difficulty);

	/** Servidor: vuelve a copiar la misión de la GameInstance en el general y en los selectores de este mundo. */
	TORTUNABO_API void SyncLobby(const UObject* WorldContext);
}
