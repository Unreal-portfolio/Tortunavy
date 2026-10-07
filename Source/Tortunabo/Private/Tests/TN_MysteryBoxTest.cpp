// Caja sorpresa de la tienda (#873, TN_MysteryBox.h): tirada con semilla fija, pesos por rareza, repetidas que devuelven
// puntos y saldo insuficiente que no compra. Sin mundo ni perfil: lo mismo que usa UMP_GameInstance::OpenMysteryBoxFor.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Shop.MysteryBox; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Lobby/TN_MysteryBox.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNMysteryBoxTest
{
	using TNMysteryBox::FCandidate;

	/** Cuatro comunes, tres raras y una épica. */
	TArray<FCandidate> Catalog()
	{
		return {
			{ TEXT("Body_A"), ETNCosmeticCategory::Body, ETNSkinRarity::Common },
			{ TEXT("Body_B"), ETNCosmeticCategory::Body, ETNSkinRarity::Common },
			{ TEXT("Eyes_A"), ETNCosmeticCategory::Eyes, ETNSkinRarity::Common },
			{ TEXT("Shell_A"), ETNCosmeticCategory::Shell, ETNSkinRarity::Common },
			{ TEXT("Shell_B"), ETNCosmeticCategory::Shell, ETNSkinRarity::Rare },
			{ TEXT("Eyes_B"), ETNCosmeticCategory::Eyes, ETNSkinRarity::Rare },
			{ TEXT("Body_C"), ETNCosmeticCategory::Body, ETNSkinRarity::Rare },
			{ TEXT("Shell_C"), ETNCosmeticCategory::Shell, ETNSkinRarity::Epic },
		};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMysteryBoxRollTest,
	"Tortunabo.Shop.MysteryBox.Roll",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMysteryBoxRollTest::RunTest(const FString& Parameters)
{
	using namespace TNMysteryBox;
	const TArray<FCandidate> Catalog = TNMysteryBoxTest::Catalog();
	const FTN_MysteryBoxRules Rules;

	// Semilla fija: la misma secuencia siempre.
	FRandomStream A(873);
	FRandomStream B(873);
	for (int32 i = 0; i < 20; ++i)
	{
		TestEqual(TEXT("misma semilla, misma tirada"), Roll(Catalog, Rules, A), Roll(Catalog, Rules, B));
	}

	// Con 20 000 tiradas la proporción de cada rareza se acerca a su peso (70/25/5).
	FRandomStream Stream(2026);
	int32 Counts[3] = { 0, 0, 0 };
	constexpr int32 Rolls = 20000;
	for (int32 i = 0; i < Rolls; ++i)
	{
		const int32 Index = Roll(Catalog, Rules, Stream);
		if (!TestTrue(TEXT("siempre sale algo"), Catalog.IsValidIndex(Index))) { return false; }
		++Counts[static_cast<int32>(Catalog[Index].Rarity)];
	}
	TestEqual(TEXT("comunes ~70 %"), Counts[0] / static_cast<float>(Rolls), 0.70f, 0.02f);
	TestEqual(TEXT("raras ~25 %"), Counts[1] / static_cast<float>(Rolls), 0.25f, 0.02f);
	TestEqual(TEXT("épicas ~5 %"), Counts[2] / static_cast<float>(Rolls), 0.05f, 0.01f);

	// Una rareza sin filas no se elige aunque pese mucho; sin pesos o sin filas no sale nada.
	FTN_MysteryBoxRules EpicOnly;
	EpicOnly.CommonWeight = 1.f;
	EpicOnly.RareWeight = 0.f;
	EpicOnly.EpicWeight = 1000.f;
	const TArray<FCandidate> Commons = { Catalog[0], Catalog[1] };
	for (int32 i = 0; i < 50; ++i)
	{
		TestTrue(TEXT("sin épicas en el catálogo: sale una común"), Commons.IsValidIndex(Roll(Commons, EpicOnly, Stream)));
	}
	FTN_MysteryBoxRules NoWeights;
	NoWeights.CommonWeight = NoWeights.RareWeight = NoWeights.EpicWeight = 0.f;
	TestEqual(TEXT("todos los pesos a 0: nada"), Roll(Catalog, NoWeights, Stream), INDEX_NONE);
	TestEqual(TEXT("catálogo vacío: nada"), Roll({}, Rules, Stream), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMysteryBoxOpenTest,
	"Tortunabo.Shop.MysteryBox.Open",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMysteryBoxOpenTest::RunTest(const FString& Parameters)
{
	using namespace TNMysteryBox;
	const TArray<FCandidate> Catalog = TNMysteryBoxTest::Catalog();
	const FTN_MysteryBoxRules Rules;
	TestEqual(TEXT("caja a 150"), Rules.BoxPrice, 150);
	TestEqual(TEXT("repetida: la mitad"), RefundFor(Rules), 75);

	// Saldo insuficiente: ni se abre ni se cobra.
	FRandomStream Stream(7);
	const FTN_MysteryBoxResult Poor = Open(149, Catalog, Rules, [](FName) { return false; }, Stream);
	TestFalse(TEXT("149 puntos: no se abre"), Poor.bOpened);
	TestEqual(TEXT("149 puntos: el saldo no cambia"), Poor.BalanceAfter, 149);
	TestFalse(TEXT("CanAfford con 149"), CanAfford(149, 150));
	TestTrue(TEXT("CanAfford con 150"), CanAfford(150, 150));

	// Nueva: cobra la caja entera.
	const FTN_MysteryBoxResult Fresh = Open(150, Catalog, Rules, [](FName) { return false; }, Stream);
	TestTrue(TEXT("150 puntos: se abre"), Fresh.bOpened);
	TestFalse(TEXT("nueva: no es repetida"), Fresh.bDuplicate);
	TestEqual(TEXT("nueva: saldo a 0"), Fresh.BalanceAfter, 0);
	TestTrue(TEXT("sale una del catálogo"), Catalog.ContainsByPredicate([&Fresh](const FCandidate& C) { return C.Id == Fresh.SkinId; }));

	// Repetida: cobra la caja y devuelve la parte.
	const FTN_MysteryBoxResult Again = Open(300, Catalog, Rules, [](FName) { return true; }, Stream);
	TestTrue(TEXT("repetida: se abre"), Again.bOpened && Again.bDuplicate);
	TestEqual(TEXT("repetida: devuelve 75"), Again.RefundPoints, 75);
	TestEqual(TEXT("repetida: 300 − 150 + 75"), Again.BalanceAfter, 225);

	// El tanto por ciento sale de los datos y se acota a [0, 100].
	FTN_MysteryBoxRules Generous = Rules;
	Generous.DuplicateRefundPercent = 250;
	TestEqual(TEXT("más del 100 %: la caja entera"), RefundFor(Generous), 150);
	Generous.DuplicateRefundPercent = 20;
	TestEqual(TEXT("20 %"), RefundFor(Generous), 30);

	// Sin nada que pueda salir no se cobra.
	const FTN_MysteryBoxResult Empty = Open(500, {}, Rules, [](FName) { return false; }, Stream);
	TestFalse(TEXT("catálogo vacío: no se abre"), Empty.bOpened);
	TestEqual(TEXT("catálogo vacío: el saldo no cambia"), Empty.BalanceAfter, 500);

	// La misma semilla da la misma skin (lo que permite reproducir un fallo).
	FRandomStream S1(42);
	FRandomStream S2(42);
	TestEqual(TEXT("semilla fija: misma skin"), Open(150, Catalog, Rules, [](FName) { return false; }, S1).SkinId,
		Open(150, Catalog, Rules, [](FName) { return false; }, S2).SkinId);
	return true;
}

#endif
