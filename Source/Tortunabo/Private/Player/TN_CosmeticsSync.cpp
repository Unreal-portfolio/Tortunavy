#include "Player/TN_CosmeticsSync.h"

#include "Engine/DataTable.h"
#include "Multiplayer/MP_GameInstance.h"

namespace TNCosmeticsSync
{
	bool FilterKnownRows(const UDataTable* Table, const TArray<FName>& Ids, int32 MaxIds, TSet<FName>& OutKnown)
	{
		if (!Table || Ids.Num() > MaxIds)
		{
			return false;
		}
		const TArray<FName> Known = Table->GetRowNames();
		OutKnown.Reset();
		for (const FName Id : Ids)
		{
			if (Id != NAME_None && Known.Contains(Id))
			{
				OutKnown.Add(Id);
			}
		}
		return true;
	}

	bool CanEquipHelmet(FName HelmetId, const TSet<FName>& UnlockedHelmets)
	{
		return HelmetId == NAME_None || UnlockedHelmets.Contains(HelmetId);
	}

	bool CanEquipSkin(const UMP_GameInstance* GameInstance, FName SkinId, const TSet<FName>& UnlockedSkins)
	{
		if (SkinId == NAME_None)
		{
			return true;
		}
		const UDataTable* SkinTable = GameInstance ? GameInstance->GetSkinDataTable() : nullptr;
		return SkinTable && SkinTable->GetRowNames().Contains(SkinId) && UnlockedSkins.Contains(SkinId);
	}

	bool CanEquipSkinOfCategory(const UMP_GameInstance* GameInstance, FName Id, ETNCosmeticCategory Category,
		const TSet<FName>& UnlockedSkins, const TCHAR* Context)
	{
		if (Id == NAME_None)
		{
			return true;
		}
		const FTN_SkinData* Row = GameInstance ? GameInstance->FindSkinRow(Id, Context) : nullptr;
		return Row && Row->Category == Category && UnlockedSkins.Contains(Id);
	}
}
