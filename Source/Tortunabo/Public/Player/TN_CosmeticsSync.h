// Validación en el servidor de los cosméticos que manda el cliente (AMP_GamePlayerController): el cliente lee de su
// GameInstance lo desbloqueado y lo equipado; el servidor lo valida contra sus DataTables (DT_Helmets y DT_Skins) y lo
// escribe en el ATN_CoopPlayerState, que lo replica.
#pragma once

#include "CoreMinimal.h"
#include "Core/TN_CosmeticsTypes.h"

class UDataTable;
class UMP_GameInstance;

namespace TNCosmeticsSync
{
	/** Cota de las RPC: arrays mayores se consideran manipulados (WithValidation -> desconexión). */
	constexpr int32 RpcArrayCap = 256;
	/** Cotas de lo que el servidor acepta (el cliente legítimo manda menos). */
	constexpr int32 MaxUnlockedHelmets = 50;
	constexpr int32 MaxUnlockedSkins = 100;

	/**
	 * Filtra Ids a las filas que existen en Table. False, sin tocar OutKnown, si no hay tabla o la lista supera MaxIds;
	 * true y OutKnown sustituido en otro caso.
	 */
	TORTUNABO_API bool FilterKnownRows(const UDataTable* Table, const TArray<FName>& Ids, int32 MaxIds, TSet<FName>& OutKnown);

	/** Casco: NAME_None (sin casco) o uno desbloqueado. */
	TORTUNABO_API bool CanEquipHelmet(FName HelmetId, const TSet<FName>& UnlockedHelmets);

	/** Color: NAME_None (el de serie) o una fila de DT_Skins desbloqueada. */
	TORTUNABO_API bool CanEquipSkin(const UMP_GameInstance* GameInstance, FName SkinId, const TSet<FName>& UnlockedSkins);

	/** Caparazón u ojos: NAME_None o una fila de DT_Skins de esa categoría y desbloqueada. */
	TORTUNABO_API bool CanEquipSkinOfCategory(const UMP_GameInstance* GameInstance, FName Id, ETNCosmeticCategory Category,
		const TSet<FName>& UnlockedSkins, const TCHAR* Context);
}
