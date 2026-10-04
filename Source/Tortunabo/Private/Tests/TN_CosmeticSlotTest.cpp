// Perfil cosmético por cuenta de Steam (#83): ranura por SteamID64, caída a _Local sin Steam y herencia del _Local
// antiguo solo para la primera cuenta. Reglas puras de TNCosmeticSlot y E/S real sobre ranuras de prueba
// (Saved/SaveGames, prefijo propio que se borra al terminar).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.SaveGame.Account; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "Multiplayer/TN_CosmeticSaveGame.h"
#include "Multiplayer/TN_CosmeticSlot.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNCosmeticSlotTest
{
	const TCHAR* Prefix = TEXT("TNTest_CosmeticAccount");
	const TCHAR* AccountA = TEXT("76561198000000001");
	const TCHAR* AccountB = TEXT("76561198000000002");
	const FName OldHelmet(TEXT("Test_OldHelmet"));
	const FName NewHelmetA(TEXT("Test_HelmetOfA"));
	constexpr int32 OldScore = 750;

	void DeleteSlots()
	{
		for (const FString& Slot : { TNCosmeticSlot::LocalSlot(Prefix), TNCosmeticSlot::SlotFor(Prefix, AccountA),
				 TNCosmeticSlot::SlotFor(Prefix, AccountB) })
		{
			UGameplayStatics::DeleteGameInSlot(Slot, 0);
		}
	}

	UTN_CosmeticSaveGame* Load(const FString& Slot)
	{
		return Cast<UTN_CosmeticSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	}

	/** Perfil _Local como el que dejaba la build anterior a #83: un casco desbloqueado y puntos. */
	bool WriteOldLocalProfile()
	{
		UTN_CosmeticSaveGame* Old = Cast<UTN_CosmeticSaveGame>(
			UGameplayStatics::CreateSaveGameObject(UTN_CosmeticSaveGame::StaticClass()));
		if (!Old)
		{
			return false;
		}
		Old->StampCurrentVersion();
		Old->UnlockedHelmetIds.Add(OldHelmet);
		Old->AccumulatedRaceScore = OldScore;
		return UGameplayStatics::SaveGameToSlot(Old, TNCosmeticSlot::LocalSlot(Prefix), 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCosmeticSlotDecisionsTest,
	"Tortunabo.SaveGame.Account.Decisions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCosmeticSlotDecisionsTest::RunTest(const FString& Parameters)
{
	using namespace TNCosmeticSlot;

	TestEqual(TEXT("Sin cuenta: ranura de la máquina"), SlotFor(TEXT("Cosmetics"), FString()), FString(TEXT("Cosmetics_Local")));
	TestEqual(TEXT("Con Steam: ranura por SteamID64"), SlotFor(TEXT("Cosmetics"), TEXT("76561198000000001")),
		FString(TEXT("Cosmetics_76561198000000001")));
	TestEqual(TEXT("Id saneado para el nombre de fichero"), SanitizeAccountId(TEXT("../a b:7\\ñ")), FString(TEXT("ab7")));
	TestEqual(TEXT("Id sin nada válido: ranura de la máquina"), SlotFor(TEXT("Cosmetics"), TEXT("/:*")), FString(TEXT("Cosmetics_Local")));
	TestEqual(TEXT("Sin subsistema en línea: sin cuenta"), SteamAccountIdOf(nullptr), FString());

	const FString Id(TEXT("765"));
	TestTrue(TEXT("Primera cuenta y _Local sin dueño: lo hereda"),
		DecideLocalMigration(Id, false, true, FString()) == ELocalMigration::CopyToAccount);
	TestTrue(TEXT("_Local de otra cuenta: empieza de cero"),
		DecideLocalMigration(Id, false, true, TEXT("999")) == ELocalMigration::None);
	TestTrue(TEXT("_Local suyo y su ranura perdida: lo vuelve a heredar"),
		DecideLocalMigration(Id, false, true, Id) == ELocalMigration::CopyToAccount);
	TestTrue(TEXT("La cuenta ya tiene perfil: no se pisa"),
		DecideLocalMigration(Id, true, true, FString()) == ELocalMigration::None);
	TestTrue(TEXT("Sin _Local: nada que heredar"), DecideLocalMigration(Id, false, false, FString()) == ELocalMigration::None);
	TestTrue(TEXT("Sin cuenta: nada que heredar"),
		DecideLocalMigration(FString(), false, true, FString()) == ELocalMigration::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCosmeticSlotTwoAccountsTest,
	"Tortunabo.SaveGame.Account.TwoAccounts",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCosmeticSlotTwoAccountsTest::RunTest(const FString& Parameters)
{
	using namespace TNCosmeticSlotTest;
	DeleteSlots();
	if (!TestTrue(TEXT("Se escribe el _Local antiguo"), WriteOldLocalProfile()))
	{
		DeleteSlots();
		return false;
	}

	TestTrue(TEXT("La cuenta A hereda el _Local"), TNCosmeticSlot::MigrateLocalToAccount(Prefix, AccountA));
	TestFalse(TEXT("La cuenta B no lo hereda"), TNCosmeticSlot::MigrateLocalToAccount(Prefix, AccountB));
	TestFalse(TEXT("Repetir con A no hace nada"), TNCosmeticSlot::MigrateLocalToAccount(Prefix, AccountA));
	TestFalse(TEXT("Sin cuenta no hace nada"), TNCosmeticSlot::MigrateLocalToAccount(Prefix, FString()));

	UTN_CosmeticSaveGame* ProfileA = Load(TNCosmeticSlot::SlotFor(Prefix, AccountA));
	const UTN_CosmeticSaveGame* Local = Load(TNCosmeticSlot::LocalSlot(Prefix));
	TestNotNull(TEXT("A tiene perfil propio"), ProfileA);
	TestFalse(TEXT("B no tiene perfil hasta que juegue"),
		UGameplayStatics::DoesSaveGameExist(TNCosmeticSlot::SlotFor(Prefix, AccountB), 0));
	TestEqual(TEXT("El _Local queda marcado con A"), Local ? Local->ClaimedByAccountId : FString(), FString(AccountA));
	if (ProfileA)
	{
		TestTrue(TEXT("A conserva el casco del _Local"), ProfileA->UnlockedHelmetIds.Contains(OldHelmet));
		TestEqual(TEXT("A conserva los puntos del _Local"), ProfileA->AccumulatedRaceScore, OldScore);

		// Lo que A desbloquea después no le llega a B ni al _Local.
		ProfileA->UnlockedHelmetIds.Add(NewHelmetA);
		UGameplayStatics::SaveGameToSlot(ProfileA, TNCosmeticSlot::SlotFor(Prefix, AccountA), 0);
	}
	const UTN_CosmeticSaveGame* LocalAfter = Load(TNCosmeticSlot::LocalSlot(Prefix));
	TestFalse(TEXT("El _Local no recibe lo de A"), LocalAfter && LocalAfter->UnlockedHelmetIds.Contains(NewHelmetA));

	DeleteSlots();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
