// Objetos del coop (GDD oficial; TN_CoopItemRules.h): ItemId, apilado en el inventario y tabla de botín del coop y del charco
// de pesca. Lógica pura, sin mundo.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Coop.Items; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Game/TN_CoopItemRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopItemsIdTest,
	"Tortunabo.Coop.Items.ItemId",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopItemsIdTest::RunTest(const FString& Parameters)
{
	ETNCoopItem Kind = ETNCoopItem::None;
	int32 Count = 0;
	TestTrue(TEXT("Sin objeto no hay ItemId"), TNCoopItemRules::MakeItemId(ETNCoopItem::None, 1).IsNone());
	TestFalse(TEXT("Un ItemId de TcT no es del coop"), TNCoopItemRules::ParseItemId(FName(TEXT("Tct_Shovel_2")), Kind, Count));
	TestFalse(TEXT("Sin cuenta no vale"), TNCoopItemRules::ParseItemId(FName(TEXT("Coop_Harpoon_")), Kind, Count));
	TestFalse(TEXT("Cuenta cero no vale"), TNCoopItemRules::ParseItemId(FName(TEXT("Coop_Harpoon_0")), Kind, Count));
	TestFalse(TEXT("Código desconocido no vale"), TNCoopItemRules::ParseItemId(FName(TEXT("Coop_Nada_2")), Kind, Count));
	TestTrue(TEXT("Tras el último uso no queda nada"), TNCoopItemRules::ItemIdAfterUse(FName(TEXT("Coop_Nada_1"))).IsNone());

	for (const ETNCoopItem Each : TNCoopItemRules::AllKinds())
	{
		const FTNCoopItemSpec& Spec = TNCoopItemRules::Spec(Each);
		TestEqual(FString::Printf(TEXT("Ficha de %s en su sitio"), Spec.Code), Spec.Kind, Each);
		const FName Id = TNCoopItemRules::MakeItemId(Each, TNCoopItemRules::InitialCount(Each));
		TestTrue(FString::Printf(TEXT("%s se lee"), Spec.Code), TNCoopItemRules::ParseItemId(Id, Kind, Count));
		TestEqual(FString::Printf(TEXT("%s: mismo objeto"), Spec.Code), Kind, Each);
		TestEqual(FString::Printf(TEXT("%s: misma cuenta"), Spec.Code), Count, TNCoopItemRules::InitialCount(Each));
		const FName After = TNCoopItemRules::ItemIdAfterUse(TNCoopItemRules::MakeItemId(Each, 2));
		TestEqual(FString::Printf(TEXT("%s: un uso menos"), Spec.Code), After, TNCoopItemRules::MakeItemId(Each, 1));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopItemsStackTest,
	"Tortunabo.Coop.Items.Stack",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopItemsStackTest::RunTest(const FString& Parameters)
{
	int32 Count = 0;
	// Apilables (límite 3).
	TestEqual(TEXT("1 + 1 de 3: se apila"), TNCoopItemRules::DecideStackCounts(3, 1, 1, 1, Count), ETNCoopStack::Merge);
	TestEqual(TEXT("1 + 1 de 3: quedan 2"), Count, 2);
	TestEqual(TEXT("2 + 2 de 3: se apila hasta el tope"), TNCoopItemRules::DecideStackCounts(3, 1, 2, 2, Count), ETNCoopStack::Merge);
	TestEqual(TEXT("2 + 2 de 3: quedan 3"), Count, 3);
	TestEqual(TEXT("3 de 3: lleno, no se coge"), TNCoopItemRules::DecideStackCounts(3, 1, 3, 1, Count), ETNCoopStack::Full);
	TestEqual(TEXT("3 de 3: siguen 3"), Count, 3);
	// Sin apilado (límite 1).
	TestEqual(TEXT("Límite 1: el segundo no se coge"), TNCoopItemRules::DecideStackCounts(1, 1, 1, 1, Count), ETNCoopStack::Full);
	// Herramientas de varios usos: coger otra recarga, nunca suma.
	TestEqual(TEXT("Herramienta gastada: recarga"), TNCoopItemRules::DecideStackCounts(1, 15, 4, 15, Count), ETNCoopStack::Merge);
	TestEqual(TEXT("Herramienta gastada: vuelve a 15"), Count, 15);
	TestEqual(TEXT("Herramienta llena: no se coge"), TNCoopItemRules::DecideStackCounts(1, 15, 15, 15, Count), ETNCoopStack::Full);
	TestEqual(TEXT("Una con menos usos no recarga"), TNCoopItemRules::DecideStackCounts(1, 15, 10, 3, Count), ETNCoopStack::Full);
	TestEqual(TEXT("Una con menos usos: siguen 10"), Count, 10);
	// Objetos distintos o que no son del coop: otro hueco.
	TestEqual(TEXT("Sin objeto del coop: otro hueco"), TNCoopItemRules::DecideStack(ETNCoopItem::None, 1, ETNCoopItem::None, 1, Count),
		ETNCoopStack::Separate);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopItemsLootTableTest,
	"Tortunabo.Coop.Items.LootTable",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopItemsLootTableTest::RunTest(const FString& Parameters)
{
	// Filas de DT_Items: peso de quien sortea x 15; las de los catálogos de código, nunca.
	TestEqual(TEXT("Fila normal: 15"), TNCoopItemRules::CatalogWeight(ETN_ItemUseType::Throwable, 1.f), 15.f);
	TestEqual(TEXT("Tótem a 0,3: 4,5"), TNCoopItemRules::CatalogWeight(ETN_ItemUseType::Totem, 0.3f), 4.5f, 1.e-4f);
	TestEqual(TEXT("Sin uso: fuera"), TNCoopItemRules::CatalogWeight(ETN_ItemUseType::None, 1.f), 0.f);
	TestEqual(TEXT("De carrera: fuera"), TNCoopItemRules::CatalogWeight(ETN_ItemUseType::RaceItem, 1.f), 0.f);
	TestEqual(TEXT("De TcT: fuera"), TNCoopItemRules::CatalogWeight(ETN_ItemUseType::TctItem, 1.f), 0.f);
	TestEqual(TEXT("Del coop: fuera (salen por su peso)"), TNCoopItemRules::CatalogWeight(ETN_ItemUseType::CoopItem, 1.f), 0.f);
	TestEqual(TEXT("Peso negativo: fuera"), TNCoopItemRules::CatalogWeight(ETN_ItemUseType::Throwable, -1.f), 0.f);

	// Sorteo ponderado.
	TestEqual(TEXT("Sin pesos: nada"), TNCoopItemRules::PickWeighted({}, 0.5f), INDEX_NONE);
	TestEqual(TEXT("Solo pesos 0: nada"), TNCoopItemRules::PickWeighted({ 0.f, 0.f }, 0.5f), INDEX_NONE);
	TestEqual(TEXT("El único con peso sale siempre"), TNCoopItemRules::PickWeighted({ 0.f, 2.f, 0.f }, 0.99f), 1);
	TestEqual(TEXT("Primera mitad"), TNCoopItemRules::PickWeighted({ 1.f, 1.f }, 0.25f), 0);
	TestEqual(TEXT("Segunda mitad"), TNCoopItemRules::PickWeighted({ 1.f, 1.f }, 0.75f), 1);
	TestEqual(TEXT("Tirada de 1 recortada: el último"), TNCoopItemRules::PickWeighted({ 1.f, 1.f }, 1.f), 1);

	// La tabla del coop: primero las filas de DT_Items y después los objetos de código con su peso.
	const TArray<float> Catalog = { 15.f, 15.f };
	float CodeTotal = 0.f;
	for (const ETNCoopItem Kind : TNCoopItemRules::AllKinds())
	{
		CodeTotal += TNCoopItemRules::Spec(Kind).LootWeight;
	}
	const float Total = 30.f + CodeTotal;
	const FTNCoopLootPick First = TNCoopItemRules::PickLoot(Catalog, 0.f);
	TestEqual(TEXT("Tirada 0: la primera fila"), First.CatalogIndex, 0);
	const FTNCoopLootPick Second = TNCoopItemRules::PickLoot(Catalog, 20.f / Total);
	TestEqual(TEXT("La segunda fila"), Second.CatalogIndex, 1);
	const FTNCoopLootPick Empty = TNCoopItemRules::PickLoot({}, 0.5f);
	TestEqual(TEXT("Sin filas: sale un objeto de código si hay alguno con peso"), Empty.IsValid(), CodeTotal > 0.f);
	TestEqual(TEXT("Sin filas nunca sale una fila"), Empty.CatalogIndex, INDEX_NONE);
	if (CodeTotal > 0.f)
	{
		const FTNCoopLootPick Last = TNCoopItemRules::PickLoot(Catalog, 0.9999f);
		TestTrue(TEXT("Tirada alta: un objeto de código"), Last.Kind != ETNCoopItem::None && Last.CatalogIndex == INDEX_NONE);
	}
	float ChanceSum = 0.f;
	for (const ETNCoopItem Kind : TNCoopItemRules::AllKinds())
	{
		ChanceSum += TNCoopItemRules::LootChance(Catalog, Kind);
	}
	TestEqual(TEXT("Probabilidad de los de código: su parte de la tabla"), ChanceSum, CodeTotal / Total, 1.e-4f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
