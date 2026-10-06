#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class USaveGame;

/**
 * Lectura y escritura de los guardados locales con las reglas de TN_SaveGameDecisions.h:
 * un fichero ilegible se aparta a <ranura>_corrupto_<fecha> en vez de sobrescribirse,
 * y un SaveGameToSlot fallido se reintenta una vez y queda en el log.
 */
namespace TNSaveGameIO
{
	struct FLoadResult
	{
		/** Guardado leído con la clase esperada; nullptr = hay que crear uno nuevo. */
		USaveGame* Loaded = nullptr;

		/** El fichero existía, no se podía leer y se ha apartado con otro nombre. */
		bool bQuarantined = false;

		/**
		 * El fichero ilegible no se ha podido apartar: no se debe escribir encima en esta sesión
		 * (se pierde lo desbloqueado). El juego sigue con un perfil en memoria.
		 */
		bool bSaveBlocked = false;
	};

	/**
	 * @brief Lee Slot esperando ExpectedClass. Si el fichero existe pero no se puede leer
	 * (truncado, otra clase), lo copia a <Slot>_corrupto_<fecha>, borra el original y avisa en el log.
	 * @param IsIntact  Comprueba el objeto ya leído (p. ej. la marca de fin); false = fichero truncado.
	 * @param What      Nombre legible para el log («perfil cosmético», «ajustes»).
	 */
	FLoadResult LoadOrQuarantine(const FString& Slot, int32 UserIndex, const UClass* ExpectedClass,
		TFunctionRef<bool(const USaveGame&)> IsIntact, const TCHAR* What);

	/** @brief SaveGameToSlot comprobado: un reintento y error en el log si falla. Devuelve si se ha escrito. */
	bool SaveChecked(USaveGame* Save, const FString& Slot, int32 UserIndex, const TCHAR* What);

	/**
	 * @brief Copia los bytes de Slot a la ranura Backup (la sobrescribe) sin tocar el original. Devuelve si la copia
	 * existe. Es para antes de reescribir un guardado que esta build no sabe leer entero (lo guardó una más nueva).
	 */
	bool BackupSlot(const FString& Slot, const FString& Backup, int32 UserIndex, const TCHAR* What);
}
