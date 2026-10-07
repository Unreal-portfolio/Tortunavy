// Saldo de puntos de la tienda y caja sorpresa del UMP_GameInstance (#873).

#include "Multiplayer/MP_GameInstance.h"
#include "Core/TN_Log.h"
#include "Core/TN_PointsEconomy.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Multiplayer/TN_CosmeticSaveGame.h"

int32 UMP_GameInstance::GetShopPointsFor(const APlayerController* PC) const
{
	const UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	return Profile ? FMath::Max(0, Profile->ShopPoints) : 0;
}

TArray<TNMysteryBox::FCandidate> UMP_GameInstance::GetMysteryBoxCandidates() const
{
	TArray<TNMysteryBox::FCandidate> Out;
	const UDataTable* SkinDT = GetSkinDataTable();
	if (!SkinDT)
	{
		return Out;
	}
	for (const FName Row : SkinDT->GetRowNames())
	{
		const FTN_SkinData* SkinRow = SkinDT->FindRow<FTN_SkinData>(Row, TEXT("GetMysteryBoxCandidates"));
		if (SkinRow && SkinRow->Category != ETNCosmeticCategory::Helmet)
		{
			Out.Add({ Row, SkinRow->Category, SkinRow->Rarity });
		}
	}
	return Out;
}

FTN_MysteryBoxResult UMP_GameInstance::OpenMysteryBoxFor(const APlayerController* PC, FRandomStream& Stream)
{
	UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	if (!Profile)
	{
		return FTN_MysteryBoxResult();
	}
	const TArray<TNMysteryBox::FCandidate> Candidates = GetMysteryBoxCandidates();
	const FTN_MysteryBoxRules& Rules = UTN_PointsEconomy::Get().MysteryBox;
	const FTN_MysteryBoxResult Result = TNMysteryBox::Open(FMath::Max(0, Profile->ShopPoints), Candidates, Rules,
		[Profile](FName Id) { return Profile->UnlockedSkinIds.Contains(Id); }, Stream);
	if (!Result.bOpened)
	{
		return Result;
	}
	Profile->ShopPoints = Result.BalanceAfter;
	if (!Result.bDuplicate)
	{
		Profile->UnlockedSkinIds.AddUnique(Result.SkinId);
	}
	SaveCosmeticsFor(PC);
	UE_LOG(LogTortunabo, Log, TEXT("[Tienda] Caja sorpresa: '%s' (%s), repetida=%d (+%d); quedan %d puntos."), *Result.SkinId.ToString(),
		*UEnum::GetValueAsString(Result.Rarity), Result.bDuplicate ? 1 : 0, Result.RefundPoints, Result.BalanceAfter);
	return Result;
}

#if !UE_BUILD_SHIPPING
// Para probar la tienda: suma puntos al saldo del perfil local y los guarda (como si se ganaran al acabar una partida).
static FAutoConsoleCommandWithWorldAndArgs GTNShopAddPointsCommand(
	TEXT("TN.Shop.AddPoints"),
	TEXT("Tienda: TN.Shop.AddPoints <puntos = 1000>: suma puntos de final de partida al perfil local (saldo de la tienda)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UMP_GameInstance* GI = World ? Cast<UMP_GameInstance>(World->GetGameInstance()) : nullptr;
		if (!GI)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("TN.Shop.AddPoints: no hay UMP_GameInstance"));
			return;
		}
		GI->AddCoopScore(Args.Num() > 0 ? FMath::Max(1, FCString::Atoi(*Args[0])) : 1000);
	}));
#endif
