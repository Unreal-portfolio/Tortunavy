#pragma once

#include "CoreMinimal.h"
#include "Misc/DateTime.h"

/**
 * Lógica pura de los guardados locales (cosméticos y ajustes): qué hacer al
 * cargar, cómo se llama la copia de un guardado corrupto y qué migración toca.
 * Sin UGameplayStatics ni disco, para que Tortunabo.SaveGame.* cubra las reglas
 * que usa TNSaveGameIO en producción.
 */
namespace TNSaveLogic
{
	/** Versión actual del perfil cosmético. 0 = guardado anterior al campo SaveVersion. */
	constexpr int32 COSMETIC_SAVE_VERSION = 1;

	/**
	 * Versión actual de los ajustes (UTN_SettingsSaveGame::Version): 1 sonido, voz y juego; 2 teclas, micrófono e interfaz;
	 * 3 idioma y ojo de pez; 4 vibración del mando. 0 = guardado sin número (ver TNSettingsMigration).
	 */
	constexpr int32 SETTINGS_SAVE_VERSION = 4;

	/**
	 * Guardado automático de los ajustes: segundos sin cambios antes del primer intento y tope de la espera entre
	 * reintentos cuando el guardado falla (ver SaveRetryDelay).
	 */
	constexpr double SETTINGS_AUTOSAVE_DELAY = 3.0;
	constexpr double SETTINGS_AUTOSAVE_MAX_DELAY = 60.0;

	/** Qué hacer con una ranura al cargarla. */
	enum class ELoadAction : uint8
	{
		/** No hay fichero: perfil nuevo. */
		CreateFresh,
		/** El fichero se ha leído bien: usarlo. */
		UseLoaded,
		/** Hay fichero pero no se puede leer: apartarlo con otro nombre y empezar de cero. Nunca sobrescribirlo. */
		QuarantineAndCreateFresh,
		/**
		 * Hay fichero pero el sistema no ha devuelto sus bytes (bloqueado por el antivirus o la nube, permisos...):
		 * no se sabe si está dañado, así que ni se aparta ni se sobrescribe en esta sesión.
		 */
		KeepAndBlockSaves
	};

	/** Qué migración aplicar a un guardado leído. */
	enum class EMigration : uint8
	{
		UpToDate,
		/** Versión antigua: se migra en memoria y se sella con la actual. */
		Upgrade,
		/** Guardado por una build más nueva: se usa tal cual y se avisa en el log. */
		FromNewerBuild
	};

	/**
	 * @brief Decide la acción de carga a partir de si el fichero existe, si el sistema ha devuelto sus bytes y si se
	 * han leído con la clase esperada.
	 */
	inline ELoadAction DecideLoadAction(bool bFileExists, bool bBytesRead, bool bLoadedOk)
	{
		if (!bFileExists)
		{
			return ELoadAction::CreateFresh;
		}
		if (!bBytesRead)
		{
			return ELoadAction::KeepAndBlockSaves;
		}
		return bLoadedOk ? ELoadAction::UseLoaded : ELoadAction::QuarantineAndCreateFresh;
	}

	/**
	 * @brief Un guardado con versión (>= 1) lleva SaveVersion como primera propiedad y bWriteComplete = true como
	 * última: si se ha leído la versión pero no la marca de fin, el fichero está truncado. Los de versión 0
	 * (anteriores al campo) no llevan marca y no se pueden comprobar.
	 */
	inline bool IsTruncated(int32 SavedVersion, bool bWriteComplete)
	{
		return SavedVersion >= 1 && !bWriteComplete;
	}

	/** @brief Decide la migración de un guardado con versión SavedVersion frente a la actual. */
	inline EMigration DecideMigration(int32 SavedVersion, int32 CurrentVersion)
	{
		if (SavedVersion == CurrentVersion)
		{
			return EMigration::UpToDate;
		}
		return SavedVersion < CurrentVersion ? EMigration::Upgrade : EMigration::FromNewerBuild;
	}

	/** @brief Nombre de la ranura a la que se aparta un guardado ilegible: <Slot>_corrupto_AAAAMMDD-HHMMSS. */
	inline FString BuildQuarantineSlotName(const FString& Slot, const FDateTime& When)
	{
		return FString::Printf(TEXT("%s_corrupto_%s"), *Slot, *When.ToString(TEXT("%Y%m%d-%H%M%S")));
	}

	/**
	 * @brief Segundos de espera antes del siguiente intento de guardar, dados los fallos seguidos hasta ahora: BaseSeconds
	 * sin fallos y el doble con cada uno, con tope en MaxSeconds (con 3 y 60: 3, 6, 12, 24, 48, 60, 60...). Sin esto, un
	 * guardado que falla (fichero de solo lectura, disco lleno) se reintentaba en cada fotograma.
	 */
	inline double SaveRetryDelay(int32 FailedAttempts, double BaseSeconds, double MaxSeconds)
	{
		const double Base = FMath::Max(BaseSeconds, 0.0);
		const double Cap = FMath::Max(MaxSeconds, Base);
		double Delay = Base;
		// Se corta al llegar al tope (o si la base es 0, que no crece): el bucle no depende de lo grande que sea FailedAttempts.
		for (int32 Attempt = 0; Attempt < FailedAttempts && Delay > 0.0 && Delay < Cap; ++Attempt)
		{
			Delay *= 2.0;
		}
		return FMath::Min(Delay, Cap);
	}

	/**
	 * @brief Ranura a la que se copia un guardado de una build más nueva antes de reescribirlo con esta:
	 * <Slot>_respaldo_v<versión del fichero>.
	 */
	inline FString BuildNewerBuildBackupSlotName(const FString& Slot, int32 SavedVersion)
	{
		return FString::Printf(TEXT("%s_respaldo_v%d"), *Slot, SavedVersion);
	}

	/** @brief Copia de una lista de IDs sin NAME_None ni duplicados, en el orden original (migración v0 → v1). */
	inline TArray<FName> SanitizeIds(const TArray<FName>& Ids)
	{
		TArray<FName> Result;
		Result.Reserve(Ids.Num());
		for (const FName Id : Ids)
		{
			if (Id != NAME_None)
			{
				Result.AddUnique(Id);
			}
		}
		return Result;
	}
}
