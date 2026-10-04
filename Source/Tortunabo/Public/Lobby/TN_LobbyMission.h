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
	/** Los modos que se ofrecen al crear partida y con el general, en su orden (el selector del lobby viejo recorre todos). */
	inline constexpr ETNProcGameMode MenuModes[] = { ETNProcGameMode::Coop, ETNProcGameMode::Race, ETNProcGameMode::Karts, ETNProcGameMode::Survival };

	/** Las dificultades, en su orden. */
	inline constexpr ETNProcDifficulty Difficulties[] = { ETNProcDifficulty::Easy, ETNProcDifficulty::Normal, ETNProcDifficulty::Hard };

	/** Nombre del modo («Cooperativo», «Carrera», «Karts», «2 vs 2», «Clásico», «Supervivencia»). */
	TORTUNABO_API FText ModeName(ETNProcGameMode Mode);

	/** El modo si es uno de MenuModes; si no (2 vs 2, Clásico), Cooperativo. Lo que admite una sala nueva. */
	TORTUNABO_API ETNProcGameMode NormalizeMenuMode(ETNProcGameMode Mode);

	/** Nombre de la dificultad («Fácil», «Normal», «Difícil»). */
	TORTUNABO_API FText DifficultyName(ETNProcDifficulty Difficulty);

	/** Una línea que explica el modo, sin su nombre delante (menú principal y pestaña «Misión» del general). */
	TORTUNABO_API FText ModeBlurb(ETNProcGameMode Mode);

	/** Una línea que explica la dificultad, sin su nombre delante. */
	TORTUNABO_API FText DifficultyBlurb(ETNProcDifficulty Difficulty);

	/** El siguiente modo del selector del lobby: Clásico → Coop → Carrera → 2 vs 2 (este, solo con exactamente 4 jugadores). */
	TORTUNABO_API ETNProcGameMode NextSelectorMode(ETNProcGameMode Current, int32 ConnectedPlayers);

	/** true si en esta máquina se puede elegir la misión: es el anfitrión (servidor escucha o partida sola). */
	TORTUNABO_API bool CanLocalPlayerChoose(const UObject* WorldContext);

	/** Modo y dificultad que tiene ahora la GameInstance de esta máquina (la buena es la del anfitrión). */
	TORTUNABO_API ETNProcGameMode GetHostMode(const UObject* WorldContext);
	TORTUNABO_API ETNProcDifficulty GetHostDifficulty(const UObject* WorldContext);

	/**
	 * Anfitrión: fija el modo en su GameInstance y lo replica en el lobby (general y selectores). false en un cliente, sin
	 * GameInstance o si no se puede (2 vs 2 sin exactamente cuatro jugadores). Con el mismo modo no cambia nada (true).
	 */
	TORTUNABO_API bool SetMode(const UObject* WorldContext, ETNProcGameMode Mode);

	/** Anfitrión: fija la dificultad y la replica en el lobby. false en un cliente o sin GameInstance. */
	TORTUNABO_API bool SetDifficulty(const UObject* WorldContext, ETNProcDifficulty Difficulty);

	/** Servidor: vuelve a copiar la misión de la GameInstance en el general y en los selectores de este mundo. */
	TORTUNABO_API void SyncLobby(const UObject* WorldContext);
}
