#pragma once

#include "CoreMinimal.h"

class IOnlineSubsystem;

/**
 * Ranura del perfil cosmético por cuenta (#83). Con Steam cada cuenta tiene la suya, <Prefijo>_<SteamID64>; sin Steam
 * (subsistema NULL, Quest, PIE) se usa la de la máquina, <Prefijo>_Local. La primera cuenta que entra en un equipo con
 * un perfil _Local anterior se lo queda (copia) y el _Local se marca como suyo: la segunda cuenta empieza de cero.
 */
namespace TNCosmeticSlot
{
	/** Qué hacer con el perfil _Local al entrar una cuenta. */
	enum class ELocalMigration : uint8
	{
		/** Nada: no hay cuenta, la cuenta ya tiene perfil, no hay _Local o es de otra cuenta. */
		None,
		/** Copiar el _Local a la ranura de la cuenta y marcarlo como suyo. */
		CopyToAccount
	};

	/** @brief Id apto para nombre de fichero: solo letras y dígitos ASCII. Vacío si no queda nada. */
	FString SanitizeAccountId(const FString& RawId);

	/** @brief Ranura de la máquina: <Prefix>_Local. */
	FString LocalSlot(const FString& Prefix);

	/** @brief Ranura de la cuenta (<Prefix>_<Id>) o, con AccountId vacío, la de la máquina. */
	FString SlotFor(const FString& Prefix, const FString& AccountId);

	/**
	 * @brief Decide si la cuenta hereda el perfil _Local. Solo la primera: un _Local marcado por otra cuenta no se
	 * vuelve a copiar. Si está marcado por esta misma pero su ranura ha desaparecido, se vuelve a copiar.
	 */
	ELocalMigration DecideLocalMigration(const FString& AccountId, bool bAccountSlotExists, bool bLocalSlotExists,
		const FString& LocalClaimedBy);

	/**
	 * @brief SteamID64 de la cuenta local (usuario 0) si el subsistema es Steam y hay sesión; vacío en otro caso. Nunca
	 * usa el id del subsistema NULL: lleva un GUID distinto en cada arranque y el perfil se perdería al reiniciar.
	 */
	FString SteamAccountIdOf(IOnlineSubsystem* OnlineSubsystem);

	/**
	 * @brief Aplica DecideLocalMigration en disco: copia el _Local a la ranura de la cuenta y, si se ha escrito, marca
	 * el _Local con la cuenta. Un _Local ilegible o truncado no se toca (lo aparta la carga normal). Devuelve si ha
	 * copiado.
	 */
	bool MigrateLocalToAccount(const FString& Prefix, const FString& AccountId);
}
