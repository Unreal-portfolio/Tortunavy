#include "Multiplayer/TN_LocalPlayerProfile.h"
#include "Multiplayer/TN_CosmeticSaveGame.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

UTN_LocalPlayerProfile* UTN_LocalPlayerProfile::Get(const APlayerController* PC)
{
	return PC ? Get(PC->GetLocalPlayer()) : nullptr;
}

UTN_LocalPlayerProfile* UTN_LocalPlayerProfile::Get(const ULocalPlayer* Player)
{
	return Player ? Player->GetSubsystem<UTN_LocalPlayerProfile>() : nullptr;
}

UTN_CosmeticSaveGame* UTN_LocalPlayerProfile::GetGuestCosmetics(const TArray<FName>& DefaultHelmets)
{
	if (!GuestCosmetics)
	{
		// El aspecto de serie, como un perfil recién creado (UMP_GameInstance::LoadCosmeticProfile): sin nada que guardar.
		GuestCosmetics = NewObject<UTN_CosmeticSaveGame>(this, NAME_None, RF_Transient);
		GuestCosmetics->StampCurrentVersion();
		for (const FName Helmet : DefaultHelmets)
		{
			if (Helmet != NAME_None)
			{
				GuestCosmetics->UnlockedHelmetIds.AddUnique(Helmet);
			}
		}
		if (GuestCosmetics->UnlockedHelmetIds.Num() > 0)
		{
			GuestCosmetics->EquippedHelmetId = GuestCosmetics->UnlockedHelmetIds[0];
		}
	}
	return GuestCosmetics;
}
