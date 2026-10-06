// Guardados locales (cosméticos y ajustes): versión, migración y fichero dañado apartado en vez de sobrescrito.
// Reglas puras de TN_SaveGameDecisions.h y E/S real de TNSaveGameIO sobre ranuras de prueba (Saved/SaveGames).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.SaveGame; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "Multiplayer/TN_CosmeticSaveGame.h"
#include "Multiplayer/TN_SaveGameDecisions.h"
#include "Multiplayer/TN_SaveGameIO.h"
#include "PlatformFeatures.h"
#include "SaveGameSystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSaveGameDecisionsTest,
	"Tortunabo.SaveGame.Decisions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSaveGameDecisionsTest::RunTest(const FString& Parameters)
{
	using namespace TNSaveLogic;

	TestTrue(TEXT("Sin fichero: perfil nuevo"), DecideLoadAction(false, false, false) == ELoadAction::CreateFresh);
	TestTrue(TEXT("Fichero leído: se usa"), DecideLoadAction(true, true, true) == ELoadAction::UseLoaded);
	TestTrue(TEXT("Fichero ilegible: se aparta, nunca se pisa"),
		DecideLoadAction(true, true, false) == ELoadAction::QuarantineAndCreateFresh);
	// Sin compilar: verificar en local. Un fallo de E/S pasajero no puede apartar ni pisar un guardado bueno.
	TestTrue(TEXT("Fichero bloqueado (sin bytes): no se toca"),
		DecideLoadAction(true, false, false) == ELoadAction::KeepAndBlockSaves);

	TestTrue(TEXT("v0 → actual: migrar"), DecideMigration(0, COSMETIC_SAVE_VERSION) == EMigration::Upgrade);
	TestTrue(TEXT("Misma versión: nada"),
		DecideMigration(COSMETIC_SAVE_VERSION, COSMETIC_SAVE_VERSION) == EMigration::UpToDate);
	TestTrue(TEXT("Versión futura: se avisa"),
		DecideMigration(COSMETIC_SAVE_VERSION + 1, COSMETIC_SAVE_VERSION) == EMigration::FromNewerBuild);

	TestTrue(TEXT("v1 sin marca de fin: truncado"), IsTruncated(1, false));
	TestFalse(TEXT("v1 con marca de fin: entero"), IsTruncated(1, true));
	TestFalse(TEXT("v0 (sin campos): no se puede saber, se da por bueno"), IsTruncated(0, false));

	const FString Name = BuildQuarantineSlotName(TEXT("CosmeticProfile_Local"), FDateTime(2026, 9, 29, 17, 5, 9));
	TestEqual(TEXT("Nombre de la copia apartada"), Name, FString(TEXT("CosmeticProfile_Local_corrupto_20260929-170509")));

	const TArray<FName> Clean = SanitizeIds({ NAME_None, FName(TEXT("A")), FName(TEXT("B")), FName(TEXT("A")) });
	TestEqual(TEXT("Migración: sin None ni repetidos"), Clean.Num(), 2);
	TestEqual(TEXT("Migración: conserva el orden"), Clean.Num() == 2 ? Clean[0] : NAME_None, FName(TEXT("A")));
	return true;
}

namespace
{
	const TCHAR* TN_TEST_SLOT = TEXT("TNTest_SaveGameIO");

	bool TNIsCosmeticIntact(const USaveGame& Save)
	{
		return CastChecked<UTN_CosmeticSaveGame>(&Save)->IsIntact();
	}

	/** Borra la ranura de prueba y cualquier copia apartada suya. */
	void TNCleanTestSlots(ISaveGameSystem& SaveSystem)
	{
		TArray<FString> Names;
		SaveSystem.GetSaveGameNames(Names, 0);
		for (const FString& Name : Names)
		{
			if (Name.StartsWith(TN_TEST_SLOT))
			{
				SaveSystem.DeleteGame(false, *Name, 0);
			}
		}
	}

	TArray<FString> TNFindQuarantined(ISaveGameSystem& SaveSystem)
	{
		TArray<FString> Names;
		SaveSystem.GetSaveGameNames(Names, 0);
		const FString Prefix = FString(TN_TEST_SLOT) + TEXT("_corrupto_");
		return Names.FilterByPredicate([&Prefix](const FString& Name) { return Name.StartsWith(Prefix); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSaveGameQuarantineTest,
	"Tortunabo.SaveGame.CorruptIsQuarantined",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSaveGameQuarantineTest::RunTest(const FString& Parameters)
{
	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	if (!TestNotNull(TEXT("Hay sistema de guardado"), SaveSystem))
	{
		return false;
	}
	TNCleanTestSlots(*SaveSystem);

	UTN_CosmeticSaveGame* Original = NewObject<UTN_CosmeticSaveGame>();
	Original->StampCurrentVersion();
	Original->UnlockedHelmetIds = { FName(TEXT("Casco_A")), FName(TEXT("Casco_B")) };
	Original->AccumulatedRaceScore = 1234;
	TArray<uint8> Bytes;
	TestTrue(TEXT("Serializa el perfil"), UGameplayStatics::SaveGameToMemory(Original, Bytes));

	// 1) Fichero entero: se lee tal cual.
	SaveSystem->SaveGame(false, TN_TEST_SLOT, 0, Bytes);
	TNSaveGameIO::FLoadResult Good = TNSaveGameIO::LoadOrQuarantine(TN_TEST_SLOT, 0,
		UTN_CosmeticSaveGame::StaticClass(), TNIsCosmeticIntact, TEXT("Test"));
	const UTN_CosmeticSaveGame* GoodProfile = Cast<UTN_CosmeticSaveGame>(Good.Loaded);
	TestNotNull(TEXT("Fichero entero: se lee"), GoodProfile);
	TestEqual(TEXT("Fichero entero: conserva los puntos"), GoodProfile ? GoodProfile->AccumulatedRaceScore : -1, 1234);
	TestFalse(TEXT("Fichero entero: no se aparta"), Good.bQuarantined);

	// 2) Fichero truncado (cierre a mitad de escritura): se aparta con sus bytes y no se devuelve nada.
	TArray<uint8> Truncated(Bytes.GetData(), Bytes.Num() - 30);
	SaveSystem->SaveGame(false, TN_TEST_SLOT, 0, Truncated);
	TNSaveGameIO::FLoadResult Bad = TNSaveGameIO::LoadOrQuarantine(TN_TEST_SLOT, 0,
		UTN_CosmeticSaveGame::StaticClass(), TNIsCosmeticIntact, TEXT("Test"));
	TestNull(TEXT("Truncado: no se usa"), Bad.Loaded);
	TestTrue(TEXT("Truncado: se aparta"), Bad.bQuarantined);
	TestFalse(TEXT("Truncado y apartado: se puede volver a guardar"), Bad.bSaveBlocked);
	TestFalse(TEXT("Truncado: la ranura original queda libre"), SaveSystem->DoesSaveGameExist(TN_TEST_SLOT, 0));
	const TArray<FString> Quarantined = TNFindQuarantined(*SaveSystem);
	TestEqual(TEXT("Truncado: una copia <slot>_corrupto_<fecha>"), Quarantined.Num(), 1);
	if (Quarantined.Num() == 1)
	{
		TArray<uint8> Kept;
		SaveSystem->LoadGame(false, *Quarantined[0], 0, Kept);
		TestTrue(TEXT("Truncado: la copia guarda los bytes originales"), Kept == Truncated);
	}

	// 3) Sin fichero: perfil nuevo, sin copias.
	TNSaveGameIO::FLoadResult Missing = TNSaveGameIO::LoadOrQuarantine(TN_TEST_SLOT, 0,
		UTN_CosmeticSaveGame::StaticClass(), TNIsCosmeticIntact, TEXT("Test"));
	TestNull(TEXT("Sin fichero: nada que leer"), Missing.Loaded);
	TestFalse(TEXT("Sin fichero: nada que apartar"), Missing.bQuarantined);

	TNCleanTestSlots(*SaveSystem);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSaveGameBackupTest,
	"Tortunabo.SaveGame.BackupKeepsOriginal",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSaveGameBackupTest::RunTest(const FString& Parameters)
{
	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	if (!TestNotNull(TEXT("Hay sistema de guardado"), SaveSystem))
	{
		return false;
	}
	TNCleanTestSlots(*SaveSystem);

	// La copia de un guardado de una build más nueva (antes de que esta lo reescriba): mismos bytes, original intacto.
	const FString Backup = TNSaveLogic::BuildNewerBuildBackupSlotName(TN_TEST_SLOT, 9);
	TestFalse(TEXT("Sin original no hay copia"), TNSaveGameIO::BackupSlot(TN_TEST_SLOT, Backup, 0, TEXT("Test")));
	TestFalse(TEXT("Sin original no se crea la ranura de la copia"), SaveSystem->DoesSaveGameExist(*Backup, 0));

	const TArray<uint8> Bytes = { 1, 2, 3, 4, 5 };
	SaveSystem->SaveGame(false, TN_TEST_SLOT, 0, Bytes);
	TestTrue(TEXT("Con original se copia"), TNSaveGameIO::BackupSlot(TN_TEST_SLOT, Backup, 0, TEXT("Test")));

	TArray<uint8> Copied;
	TArray<uint8> Original;
	SaveSystem->LoadGame(false, *Backup, 0, Copied);
	SaveSystem->LoadGame(false, TN_TEST_SLOT, 0, Original);
	TestTrue(TEXT("La copia guarda los bytes del original"), Copied == Bytes);
	TestTrue(TEXT("El original queda como estaba"), Original == Bytes);

	TNCleanTestSlots(*SaveSystem);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
