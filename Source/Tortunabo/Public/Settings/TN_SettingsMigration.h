#pragma once

#include "CoreMinimal.h"
#include "Multiplayer/TN_SaveGameDecisions.h"
#include "Settings/TN_SettingsSaveGame.h"

/**
 * Migración de los ajustes guardados (UTN_SettingsSaveGame) a la versión actual, paso a paso. Lógica pura: la usa
 * UTN_GameSettingsSubsystem::LoadSettings y la prueban los tests Tortunabo.Settings.Version.
 *
 * Hasta ahora ningún guardado lleva el número de versión (ver UTN_SettingsSaveGame::Version): uno sin número puede ser
 * de la 1, la 2 o la 3. Por eso cuenta como de la 1 y los pasos no tocan nada que el jugador haya podido elegir. Todas
 * las versiones hasta la 3 solo han añadido campos, y un campo que no está en el fichero se queda con su valor de serie.
 * Si una versión futura cambia el significado de un campo, su paso ya sabrá de qué versión viene el guardado.
 */
namespace TNSettingsMigration
{
	/** La versión de un guardado: sin número (0), la más antigua posible. */
	inline int32 ResolveSavedVersion(int32 SavedVersion)
	{
		return SavedVersion <= 0 ? 1 : SavedVersion;
	}

	/**
	 * @brief Lleva Settings, leídos de un guardado con SavedVersion, a la versión actual.
	 * @return Upgrade si venía de una versión anterior (hay que volver a guardarlo, ya sellado); UpToDate si ya era la
	 *         actual; FromNewerBuild si lo guardó una build más nueva (se usa tal cual y se avisa; no se reescribe por
	 *         cargarlo, y si el jugador cambia algo el subsistema copia antes el fichero a <ranura>_respaldo_v<versión>
	 *         porque esta build no conoce todos sus campos).
	 */
	inline TNSaveLogic::EMigration Migrate(FTNGameSettings& Settings, int32 SavedVersion)
	{
		const int32 From = ResolveSavedVersion(SavedVersion);
		const TNSaveLogic::EMigration Kind = TNSaveLogic::DecideMigration(From, TNSaveLogic::SETTINGS_SAVE_VERSION);
		if (Kind != TNSaveLogic::EMigration::Upgrade)
		{
			return Kind;
		}
		// 1 → 2 (teclas, pausa, micrófono, escala de la interfaz, aviso de quién habla, silenciar en segundo plano) y 2 → 3
		// (idioma, ojo de pez) y 3 → 4 (vibración del mando) solo añadieron campos: un guardado viejo no los trae y se
		// quedan los de serie. No se fuerzan aquí porque un guardado sin número también puede ser de la 3 o la 4 y llevar
		// los que eligió el jugador.
		static_assert(TNSaveLogic::SETTINGS_SAVE_VERSION == 4,
			"Nueva versión de los ajustes: añade aquí su paso (if (From < N)) y actualiza esta comprobación y los tests.");
		(void)Settings;
		return Kind;
	}
}
