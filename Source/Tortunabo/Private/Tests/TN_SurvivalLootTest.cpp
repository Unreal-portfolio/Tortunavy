// Objetos y densidad de los rebuscables de Supervivencia (TN_SurvivalLoot.h, #724). Sin mundo ni actores. Correr desde
// Session Frontend (categoría "Tortunabo.Survival.Loot") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Survival.Loot; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Game/TN_SurvivalLoot.h"

#if WITH_DEV_AUTOMATION_TESTS

// ─────────────────────────────────────────────────────────────────────────────
// Qué sale
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalLootListTest,
	"Tortunabo.Survival.Loot.List",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalLootListTest::RunTest(const FString& Parameters)
{
	using namespace TNSurvivalLoot;

	TestTrue(TEXT("Energía sin fin"), AllowsCatalogUse(ETN_ItemUseType::SelfStaminaBoost));
	TestTrue(TEXT("Bola"), AllowsCatalogUse(ETN_ItemUseType::Throwable));
	TestFalse(TEXT("Sin cabezota (#736)"), AllowsCatalogUse(ETN_ItemUseType::BigHead));
	TestTrue(TEXT("Concha trampa"), AllowsCatalogUse(ETN_ItemUseType::Conch));
	TestTrue(TEXT("Tinta"), AllowsCatalogUse(ETN_ItemUseType::InkThrower));
	TestFalse(TEXT("Sin tótem"), AllowsCatalogUse(ETN_ItemUseType::Totem));
	TestFalse(TEXT("Sin filas sin uso"), AllowsCatalogUse(ETN_ItemUseType::None));

	const ETNRaceItem Always[] = { ETNRaceItem::Coconut, ETNRaceItem::TripleCoconut3, ETNRaceItem::SandMine,
		ETNRaceItem::Frisbee };
	for (const ETNRaceItem Kind : Always)
	{
		TestTrue(FString::Printf(TEXT("%s, también en solitario"), *TNRaceItems::CodeName(Kind)), AllowsRaceItem(Kind, 0, 1));
	}
	TestTrue(TEXT("Cangrejo a la segunda"), AllowsRaceItem(ETNRaceItem::HomingCrab, 1, 2));
	TestFalse(TEXT("Sin cangrejo a la primera (no tiene a nadie delante)"), AllowsRaceItem(ETNRaceItem::HomingCrab, 0, 4));
	TestFalse(TEXT("Sin cangrejo en solitario"), AllowsRaceItem(ETNRaceItem::HomingCrab, 0, 1));
	TestTrue(TEXT("Nube en grupo, también a la primera"), AllowsRaceItem(ETNRaceItem::StormCloud, 0, 4));
	TestFalse(TEXT("Sin nube en solitario"), AllowsRaceItem(ETNRaceItem::StormCloud, 0, 1));

	// Sin coco dorado (#736).
	const ETNRaceItem Never[] = { ETNRaceItem::None, ETNRaceItem::Box, ETNRaceItem::TripleCoconut2, ETNRaceItem::TripleCoconut1,
		ETNRaceItem::GoldenCoconut, ETNRaceItem::PelicanTaxi, ETNRaceItem::Sunscreen, ETNRaceItem::GullStrike, ETNRaceItem::Whistle };
	for (const ETNRaceItem Kind : Never)
	{
		TestFalse(FString::Printf(TEXT("Sin %s"), *TNRaceItems::CodeName(Kind)), AllowsRaceItem(Kind, 2, 4));
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pesos del sorteo
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalLootWeightTest,
	"Tortunabo.Survival.Loot.Weight",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalLootWeightTest::RunTest(const FString& Parameters)
{
	using namespace TNSurvivalLoot;

	TestEqual(TEXT("Un objeto de la lista lleva el peso de la carrera"),
		Weight(ETN_ItemUseType::RaceItem, ETNRaceItem::SandMine, 1.8f, 0, 3), 1.8f);
	TestEqual(TEXT("Uno de siempre, también"), Weight(ETN_ItemUseType::Throwable, ETNRaceItem::None, 1.3f, 1, 3), 1.3f);
	TestEqual(TEXT("La cabezota no sale (#736)"), Weight(ETN_ItemUseType::BigHead, ETNRaceItem::None, 0.3f, 1, 3), 0.f);
	TestEqual(TEXT("El tótem no sale"), Weight(ETN_ItemUseType::Totem, ETNRaceItem::None, 1.f, 2, 3), 0.f);
	TestEqual(TEXT("El pelícano no sale"), Weight(ETN_ItemUseType::RaceItem, ETNRaceItem::PelicanTaxi, 2.6f, 2, 3), 0.f);
	TestEqual(TEXT("El cangrejo, a la de detrás"), Weight(ETN_ItemUseType::RaceItem, ETNRaceItem::HomingCrab, 1.5f, 1, 3), 1.5f);
	TestEqual(TEXT("El cangrejo no le sale a la primera"), Weight(ETN_ItemUseType::RaceItem, ETNRaceItem::HomingCrab, 0.7f, 0, 3), 0.f);
	TestEqual(TEXT("Lo que la carrera quita sigue fuera"), Weight(ETN_ItemUseType::RaceItem, ETNRaceItem::StormCloud, 0.f, 0, 3), 0.f);
	TestEqual(TEXT("El coco dorado no sale aunque la carrera lo dé (#736)"),
		Weight(ETN_ItemUseType::RaceItem, ETNRaceItem::GoldenCoconut, 2.f, 3, 4), 0.f);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Densidad: la de la playa
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalLootDensityTest,
	"Tortunabo.Survival.Loot.Density",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalLootDensityTest::RunTest(const FString& Parameters)
{
	using namespace TNSurvivalLoot;

	TestEqual(TEXT("70 % de suerte"), SearchLuck, 0.7f);
	TestEqual(TEXT("9 m entre rebuscables"), MinSpacing, 900.0);
	TestEqual(TEXT("Las formaciones, siempre"), SpotChance(0), 1.0);
	TestEqual(TEXT("Los objetos del camino, casi siempre"), SpotChance(1), 0.85);
	TestEqual(TEXT("Las agujas, casi siempre"), SpotChance(2), 0.85);
	TestEqual(TEXT("Los peñascos, a menudo"), SpotChance(3), 0.6);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cocos al empezar el nivel según el puesto de llegada
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalLootStartItemsTest,
	"Tortunabo.Survival.Loot.StartItems",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalLootStartItemsTest::RunTest(const FString& Parameters)
{
	using namespace TNSurvivalLoot;

	const auto Expect = [this](int32 Finishers, std::initializer_list<ETNRaceItem> Items)
	{
		int32 Place = 0;
		for (const ETNRaceItem Item : Items)
		{
			TestTrue(FString::Printf(TEXT("%d tortugas, %dª: %s"), Finishers, Place + 1, *TNRaceItems::CodeName(Item)),
				StartItemFor(Place, Finishers) == Item);
			++Place;
		}
	};
	using enum ETNRaceItem;
	Expect(1, { None });
	Expect(2, { Coconut, None });
	Expect(3, { TripleCoconut2, Coconut, None });
	Expect(4, { TripleCoconut3, TripleCoconut2, Coconut, None });
	// Sin coco dorado (#736): desde 5, solo las tres primeras.
	Expect(5, { TripleCoconut3, TripleCoconut2, Coconut, None, None });
	Expect(8, { TripleCoconut3, TripleCoconut2, Coconut, None, None, None, None, None });
	TestTrue(TEXT("Puesto no válido: nada"), StartItemFor(-1, 4) == None && StartItemFor(9, 4) == None);
	TestTrue(TEXT("Sin llegadas (nivel 1): nada"), StartItemFor(0, 0) == None);
	return true;
}

#endif
