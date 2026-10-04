// Sincronización de cosméticos cliente -> servidor, común a los PlayerController del juego (AMP_GamePlayerController)
// y del Rally (ATN_RallyPlayerController). El cliente lee de su GameInstance lo desbloqueado y lo equipado; el servidor
// lo valida contra sus DataTables (DT_Helmets y DT_Skins) y lo escribe en el ATN_CoopPlayerState, que lo replica.
#pragma once

#include "CoreMinimal.h"
#include "Core/TN_CosmeticsTypes.h"
#include "TN_CosmeticsSync.generated.h"

class ATN_CoopPlayerState;
class UDataTable;
class UMP_GameInstance;

/** Lote completo de cosméticos de un jugador, tal como lo guarda su GameInstance. */
USTRUCT()
struct TORTUNABO_API FTNCosmeticLoadout
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FName> UnlockedHelmetIds;

	UPROPERTY()
	TArray<FName> UnlockedSkinIds;

	UPROPERTY()
	FName HelmetId = NAME_None;

	UPROPERTY()
	FName SkinId = NAME_None;

	UPROPERTY()
	FName ShellId = NAME_None;

	UPROPERTY()
	FName EyesId = NAME_None;
};

namespace TNCosmeticsSync
{
	/** Cota de las RPC: arrays mayores se consideran manipulados (WithValidation -> desconexión). */
	constexpr int32 RpcArrayCap = 256;
	/** Cotas de lo que el servidor acepta (el cliente legítimo manda menos). */
	constexpr int32 MaxUnlockedHelmets = 50;
	constexpr int32 MaxUnlockedSkins = 100;

	/** Cliente: lo desbloqueado y lo equipado del save local. */
	TORTUNABO_API FTNCosmeticLoadout ReadLocalLoadout(const UMP_GameInstance& GameInstance);

	/** True si el lote cabe en las cotas de las RPC. */
	TORTUNABO_API bool IsLoadoutWithinRpcCaps(const FTNCosmeticLoadout& Loadout);

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

	/**
	 * Servidor: valida el lote completo y escribe en PlayerState lo que pase la validación (lo que no, se queda como
	 * estaba y se avisa en el log). Devuelve cuántos de los cuatro huecos (casco, color, caparazón, ojos) se aplicaron.
	 */
	TORTUNABO_API int32 ApplyLoadoutOnServer(const UMP_GameInstance* GameInstance, ATN_CoopPlayerState& PlayerState,
		const FTNCosmeticLoadout& Loadout);
}
