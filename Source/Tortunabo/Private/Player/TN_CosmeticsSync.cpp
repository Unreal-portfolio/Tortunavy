#include "Player/TN_CosmeticsSync.h"

#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_Log.h"
#include "Engine/DataTable.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Vehicles/TN_BuggyCosmetics.h"

namespace TNCosmeticsSync
{
	FTNCosmeticLoadout ReadLocalLoadout(const UMP_GameInstance& GameInstance)
	{
		FTNCosmeticLoadout Loadout;
		Loadout.UnlockedHelmetIds = GameInstance.GetUnlockedHelmetIds();
		Loadout.UnlockedSkinIds = GameInstance.GetUnlockedSkinIds();
		Loadout.HelmetId = GameInstance.GetEquippedHelmetId();
		Loadout.SkinId = GameInstance.GetEquippedSkinId();
		Loadout.ShellId = GameInstance.GetEquippedShellId();
		Loadout.EyesId = GameInstance.GetEquippedEyesId();
		Loadout.UnlockedBuggyIds = GameInstance.GetUnlockedBuggyIds();
		Loadout.BuggyLook = GameInstance.GetEquippedBuggyLook();
		return Loadout;
	}

	bool IsLoadoutWithinRpcCaps(const FTNCosmeticLoadout& Loadout)
	{
		return Loadout.UnlockedHelmetIds.Num() <= RpcArrayCap && Loadout.UnlockedSkinIds.Num() <= RpcArrayCap
			&& Loadout.UnlockedBuggyIds.Num() <= RpcArrayCap;
	}

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

	bool CanEquipBuggyLook(const FTN_BuggyLook& Look, const TSet<FName>& UnlockedBuggy)
	{
		return TNBuggyCosmetics::CanEquip(Look, UnlockedBuggy);
	}

	int32 ApplyLoadoutOnServer(const UMP_GameInstance* GameInstance, ATN_CoopPlayerState& PlayerState,
		const FTNCosmeticLoadout& Loadout)
	{
		TSet<FName> UnlockedHelmets;
		TSet<FName> UnlockedSkins;
		FilterKnownRows(GameInstance ? GameInstance->GetHelmetDataTable() : nullptr, Loadout.UnlockedHelmetIds,
			MaxUnlockedHelmets, UnlockedHelmets);
		FilterKnownRows(GameInstance ? GameInstance->GetSkinDataTable() : nullptr, Loadout.UnlockedSkinIds,
			MaxUnlockedSkins, UnlockedSkins);

		int32 Applied = 0;
		const auto Apply = [&Applied, &PlayerState](bool bValid, FName& Slot, FName Id, const TCHAR* What)
		{
			if (!bValid)
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[Cosmetics] %s '%s' no desbloqueado para %s: se mantiene '%s'."),
					What, *Id.ToString(), *PlayerState.GetPlayerName(), *Slot.ToString());
				return;
			}
			Slot = Id;
			++Applied;
		};
		Apply(CanEquipHelmet(Loadout.HelmetId, UnlockedHelmets), PlayerState.EquippedHelmetId, Loadout.HelmetId, TEXT("Casco"));
		Apply(CanEquipSkin(GameInstance, Loadout.SkinId, UnlockedSkins), PlayerState.EquippedSkinId, Loadout.SkinId, TEXT("Color"));
		Apply(CanEquipSkinOfCategory(GameInstance, Loadout.ShellId, ETNCosmeticCategory::Shell, UnlockedSkins, TEXT("Cosmetics.Shell")),
			PlayerState.EquippedShellId, Loadout.ShellId, TEXT("Caparazón"));
		Apply(CanEquipSkinOfCategory(GameInstance, Loadout.EyesId, ETNCosmeticCategory::Eyes, UnlockedSkins, TEXT("Cosmetics.Eyes")),
			PlayerState.EquippedEyesId, Loadout.EyesId, TEXT("Ojos"));
		TSet<FName> UnlockedBuggy;
		TNBuggyCosmetics::FilterKnownIds(Loadout.UnlockedBuggyIds, TNBuggyCosmetics::MaxUnlocked, UnlockedBuggy);
		if (CanEquipBuggyLook(Loadout.BuggyLook, UnlockedBuggy))
		{
			PlayerState.SetEquippedBuggyLook(Loadout.BuggyLook);
			++Applied;
		}
		else
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Cosmetics] Buggy '%s' no desbloqueado para %s: se mantiene '%s'."),
				*TNBuggyCosmetics::LookKey(Loadout.BuggyLook), *PlayerState.GetPlayerName(), *TNBuggyCosmetics::LookKey(PlayerState.EquippedBuggyLook));
		}
		PlayerState.ForceNetUpdate();
		return Applied;
	}
}
