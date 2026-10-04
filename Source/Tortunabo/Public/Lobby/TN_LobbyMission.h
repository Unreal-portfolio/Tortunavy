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
		ETNProcGameMode::Karts, ETNProcGameMode::FreeForAll, ETNProcGameMode::Rally, ETNProcGameMode::TwoVsTwo };

	/** Circuito del Rally que se elige por defecto: el primero del generador de vueltas (#622; #692 quitó E01B). */
	inline const TCHAR* const DefaultRallyCircuit = TEXT("R01_circuito_dunas");

	/** generator.generator de los manifests de Scripts/gen_terrain_rally_circuit.py: el único origen de circuitos del Rally (#692). */
	inline const TCHAR* const RallyCircuitGenerator = TEXT("rally_circuit_vueltas");

	/**
	 * true si el modo se puede jugar en esta build. Todos contra Todos necesita su arena por defecto y el Rally algún
	 * circuito, que se leen de Scripts/ y no se empaquetan (ATN_TctGameMode::HasDefaultArena, RallyMapOptions): en una
	 * build cocinada no se ofrecen. El resto, siempre.
	 */
	TORTUNABO_API bool IsModePlayable(ETNProcGameMode Mode);

	/** Los de MenuModes en su orden, sin Todos contra Todos ni el Rally si no se pueden jugar. */
	TORTUNABO_API TArray<ETNProcGameMode> FilterMenuModes(bool bFreeForAllPlayable, bool bRallyPlayable = true);

	/** Los modos que se ofrecen de verdad (FilterMenuModes con lo que hay en esta build): menú, salas y general. */
	TORTUNABO_API TArray<ETNProcGameMode> GetMenuModes();

	/** Las dificultades, en su orden. */
	inline constexpr ETNProcDifficulty Difficulties[] = { ETNProcDifficulty::Easy, ETNProcDifficulty::Normal, ETNProcDifficulty::Hard };

	/** Nombre del modo («Cooperativo», «Carrera», «Karts», «Rally», «2 vs 2», «Clásico», «Supervivencia», «Todos contra Todos»). */
	TORTUNABO_API FText ModeName(ETNProcGameMode Mode);

	/** El modo si es uno de GetMenuModes; si no (Clásico o uno que no se puede jugar en esta build), Cooperativo. Lo que admite una sala nueva. */
	TORTUNABO_API ETNProcGameMode NormalizeMenuMode(ETNProcGameMode Mode);

	/**
	 * Nombre de un circuito del Rally de LVL_Rally (#631): los conocidos tienen nombre traducido y los demás (variantes
	 * nuevas) salen con su identificador.
	 */
	TORTUNABO_API FText RallyMapName(FName Variant);

	/**
	 * Nombre de la misión: el del modo y, en el Rally, también el circuito («Rally · España»); en Todos contra Todos, la
	 * arena («Todos contra Todos · Coliseo»). MapVariant es el de GetHostMissionMap.
	 */
	TORTUNABO_API FText MissionTitle(ETNProcGameMode Mode, FName MapVariant);

	/** Plazas por buggy del Rally y de Karts: «Una por buggy» (1) o «Por parejas» (2). */
	TORTUNABO_API FText RallySeatsName(int32 Seats);

	/**
	 * true si el texto de un manifest de variante (Scripts/terrain_volumes/Variants/<v>/manifest.json) es un circuito del
	 * Rally: "mode": "rally", al menos dos checkpoints_uu y generado por el generador de vueltas (generator.generator ==
	 * RallyCircuitGenerator, #692).
	 */
	TORTUNABO_API bool IsRallyCircuitManifest(const FString& JsonText);

	/**
	 * Los circuitos del Rally en el orden del menú: los conocidos en su orden (R01 a R06) y los demás por
	 * nombre. Sin repetidos ni vacíos (el mapa generado es Karts, otro modo).
	 */
	TORTUNABO_API TArray<FName> SortRallyMaps(const TArray<FName>& Circuits);

	/**
	 * Los circuitos del Rally que se ofrecen: los de Scripts/terrain_volumes/Variants con pista (incluidas las variantes
	 * nuevas). Se buscan una vez por sesión. En un juego empaquetado sin manifests no hay ninguno y el Rally no se ofrece.
	 */
	TORTUNABO_API const TArray<FName>& RallyMapOptions();

	/** El circuito elegido si está entre las opciones; si no, el primero (NAME_None si no hay ninguno). */
	TORTUNABO_API FName ResolveRallyMap(FName Selected, const TArray<FName>& Options);

	/** URL del viaje del lobby al Rally: LVL_Rally?Variant=<v>?FromLobby (al acabar los resultados, de vuelta al lobby). */
	TORTUNABO_API FString RallyTravelURL(FName Variant, const FString& RallyMapPath);

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
	 * El mapa de la misión del anfitrión según el modo: el circuito en el Rally, la arena en Todos contra Todos y NAME_None
	 * en los demás. Es lo que el general replica para que todos vean «Rally · España» o «Todos contra Todos · Coliseo».
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
	 * Supervivencia → Karts → Todos contra Todos (este, solo si bFreeForAllPlayable) → Rally (este, solo si bRallyPlayable).
	 */
	TORTUNABO_API ETNProcGameMode NextSelectorMode(ETNProcGameMode Current, int32 ConnectedPlayers, bool bFreeForAllPlayable,
		bool bRallyPlayable = true);

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

	/** Circuito del Rally y plazas por buggy (Rally y Karts) que tiene ahora la GameInstance de esta máquina (el circuito, ya resuelto). */
	TORTUNABO_API FName GetHostRallyMap(const UObject* WorldContext);
	TORTUNABO_API int32 GetHostRallySeats(const UObject* WorldContext);

	/** Anfitrión: fija el circuito del Rally (uno de RallyMapOptions) y lo replica en el lobby. false en un cliente. */
	TORTUNABO_API bool SetRallyMap(const UObject* WorldContext, FName Variant);

	/** Anfitrión: fija las plazas por buggy del Rally y de Karts (1 o 2) y las replica en el lobby. false en un cliente. */
	TORTUNABO_API bool SetRallySeats(const UObject* WorldContext, int32 Seats);

	/** Servidor: vuelve a copiar la misión de la GameInstance en el general y en los selectores de este mundo. */
	TORTUNABO_API void SyncLobby(const UObject* WorldContext);
}
