#include "Multiplayer/TN_CosmeticSlot.h"

#include "Core/TN_Log.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Multiplayer/TN_CosmeticSaveGame.h"
#include "OnlineSubsystem.h"
#include "TN_SaveGameIO.h"

namespace
{
	/** Usuario de plataforma de los guardados locales (uno por máquina; la cuenta va en el nombre de la ranura). */
	constexpr int32 TNCosmeticSlotUserIndex = 0;

	/** Nombre del subsistema en línea de Steam (STEAM_SUBSYSTEM, sin depender del módulo de Steam). */
	FName TNSteamSubsystemName()
	{
		return FName(TEXT("STEAM"));
	}

	/** Lee el perfil _Local entero; nullptr si no existe, no se puede leer o está truncado. */
	UTN_CosmeticSaveGame* TNLoadIntactLocal(const FString& LocalSlotName)
	{
		UTN_CosmeticSaveGame* Local = Cast<UTN_CosmeticSaveGame>(
			UGameplayStatics::LoadGameFromSlot(LocalSlotName, TNCosmeticSlotUserIndex));
		return Local && Local->IsIntact() ? Local : nullptr;
	}
}

FString TNCosmeticSlot::SanitizeAccountId(const FString& RawId)
{
	FString Clean;
	Clean.Reserve(RawId.Len());
	for (const TCHAR Char : RawId)
	{
		if (FChar::IsAlnum(Char) && Char < 128)
		{
			Clean.AppendChar(Char);
		}
	}
	return Clean;
}

FString TNCosmeticSlot::LocalSlot(const FString& Prefix)
{
	return FString::Printf(TEXT("%s_Local"), *Prefix);
}

FString TNCosmeticSlot::SlotFor(const FString& Prefix, const FString& AccountId)
{
	const FString Clean = SanitizeAccountId(AccountId);
	return Clean.IsEmpty() ? LocalSlot(Prefix) : FString::Printf(TEXT("%s_%s"), *Prefix, *Clean);
}

TNCosmeticSlot::ELocalMigration TNCosmeticSlot::DecideLocalMigration(const FString& AccountId, bool bAccountSlotExists,
	bool bLocalSlotExists, const FString& LocalClaimedBy)
{
	const FString Clean = SanitizeAccountId(AccountId);
	if (Clean.IsEmpty() || bAccountSlotExists || !bLocalSlotExists)
	{
		return ELocalMigration::None;
	}
	const bool bClaimedByOther = !LocalClaimedBy.IsEmpty() && LocalClaimedBy != Clean;
	return bClaimedByOther ? ELocalMigration::None : ELocalMigration::CopyToAccount;
}

FString TNCosmeticSlot::SteamAccountIdOf(IOnlineSubsystem* OnlineSubsystem)
{
	if (!OnlineSubsystem || OnlineSubsystem->GetSubsystemName() != TNSteamSubsystemName())
	{
		return FString();
	}
	const IOnlineIdentityPtr Identity = OnlineSubsystem->GetIdentityInterface();
	const FUniqueNetIdPtr UserId = Identity.IsValid() ? Identity->GetUniquePlayerId(TNCosmeticSlotUserIndex) : nullptr;
	return UserId.IsValid() && UserId->IsValid() ? SanitizeAccountId(UserId->ToString()) : FString();
}

bool TNCosmeticSlot::MigrateLocalToAccount(const FString& Prefix, const FString& AccountId)
{
	const FString Clean = SanitizeAccountId(AccountId);
	const FString AccountSlotName = SlotFor(Prefix, Clean);
	const FString LocalSlotName = LocalSlot(Prefix);
	if (Clean.IsEmpty() || UGameplayStatics::DoesSaveGameExist(AccountSlotName, TNCosmeticSlotUserIndex))
	{
		return false;
	}
	UTN_CosmeticSaveGame* Local = TNLoadIntactLocal(LocalSlotName);
	const bool bLocalUsable = Local != nullptr;
	const FString ClaimedBy = bLocalUsable ? Local->ClaimedByAccountId : FString();
	if (DecideLocalMigration(Clean, false, bLocalUsable, ClaimedBy) != ELocalMigration::CopyToAccount)
	{
		return false;
	}

	Local->ClaimedByAccountId = Clean;
	if (!TNSaveGameIO::SaveChecked(Local, AccountSlotName, TNCosmeticSlotUserIndex, TEXT("Perfil cosmético")))
	{
		// Sin la copia de la cuenta no se marca el _Local: el próximo arranque lo vuelve a intentar.
		return false;
	}
	if (!TNSaveGameIO::SaveChecked(Local, LocalSlotName, TNCosmeticSlotUserIndex, TEXT("Perfil cosmético")))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[SaveGame] '%s' copiado a '%s' sin marcar: otra cuenta de este equipo también lo heredaría."),
			*LocalSlotName, *AccountSlotName);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[SaveGame] Perfil cosmético '%s' heredado por la cuenta en '%s'."),
		*LocalSlotName, *AccountSlotName);
	return true;
}
