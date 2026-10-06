// Validación de cosméticos en el servidor (TN_CosmeticsSync.h) que usa AMP_GamePlayerController.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Cosmetics; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Core/TN_CosmeticsTypes.h"
#include "Engine/DataTable.h"
#include "Player/TN_CosmeticsSync.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCosmeticsSyncFilterTest,
	"Tortunabo.Cosmetics.Sync.FilterAndEquip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCosmeticsSyncFilterTest::RunTest(const FString& Parameters)
{
	using namespace TNCosmeticsSync;

	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
	Table->RowStruct = FTN_SkinData::StaticStruct();
	Table->AddRow(FName(TEXT("Azul")), FTN_SkinData());
	Table->AddRow(FName(TEXT("Rojo")), FTN_SkinData());

	TSet<FName> Known = { FName(TEXT("Previo")) };
	TestFalse(TEXT("Sin tabla: se rechaza"), FilterKnownRows(nullptr, { FName(TEXT("Azul")) }, 10, Known));
	TestTrue(TEXT("Rechazado: no toca lo anterior"), Known.Contains(FName(TEXT("Previo"))));

	TArray<FName> TooMany;
	TooMany.Init(FName(TEXT("Azul")), 11);
	TestFalse(TEXT("Más de MaxIds: se rechaza"), FilterKnownRows(Table, TooMany, 10, Known));

	TestTrue(TEXT("Lista válida: se acepta"),
		FilterKnownRows(Table, { FName(TEXT("Azul")), FName(TEXT("Inventado")), NAME_None }, 10, Known));
	TestEqual(TEXT("Solo quedan las filas que existen"), Known.Num(), 1);
	TestTrue(TEXT("Azul existe"), Known.Contains(FName(TEXT("Azul"))));

	const TSet<FName> Helmets = { FName(TEXT("Gorro")) };
	TestTrue(TEXT("Sin casco siempre vale"), CanEquipHelmet(NAME_None, Helmets));
	TestTrue(TEXT("Casco desbloqueado"), CanEquipHelmet(FName(TEXT("Gorro")), Helmets));
	TestFalse(TEXT("Casco no desbloqueado"), CanEquipHelmet(FName(TEXT("Corona")), Helmets));

	TestTrue(TEXT("Color de serie siempre vale (sin GameInstance)"), CanEquipSkin(nullptr, NAME_None, Known));
	TestFalse(TEXT("Color sin GameInstance (sin DT_Skins): se rechaza"), CanEquipSkin(nullptr, FName(TEXT("Azul")), Known));
	TestTrue(TEXT("Caparazón de serie siempre vale"),
		CanEquipSkinOfCategory(nullptr, NAME_None, ETNCosmeticCategory::Shell, Known, TEXT("Test")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
