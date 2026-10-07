// Cajas de suministros (#861): reparto de objetos y chapas de la tabla de botín (hoja Economía) y apertura una sola vez por
// partida. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Supply; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "World/TN_LootTable.h"
#include "World/TN_SupplyCrate.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNSupplyLootTestDetail
{
	/** Frecuencia de cada número de chapas (0..3) en Samples aperturas con la semilla Seed. */
	TArray<float> ChapaFrequencies(const FTNLootTableDef& Def, int32 Seed, int32 Samples)
	{
		TArray<int32> Counts;
		Counts.Init(0, 4);
		FRandomStream Stream(Seed);
		for (int32 i = 0; i < Samples; ++i)
		{
			const int32 Chapas = TNLootRules::Roll(Def, Stream).ChapaCount;
			Counts[FMath::Clamp(Chapas, 0, 3)]++;
		}
		TArray<float> Out;
		for (const int32 Count : Counts)
		{
			Out.Add(static_cast<float>(Count) / static_cast<float>(Samples));
		}
		return Out;
	}

	/** Mundo de juego sin ventana (con autoridad) para las pruebas con actores; DestroyWorld lo quita. */
	UWorld* CreateWorld(const TCHAR* Name)
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, Name);
		if (World)
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}
		return World;
	}

	void DestroyWorld(UWorld* World)
	{
		if (World)
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	}

	/** Actores de la clase exacta Class en World (las chapas de prueba). */
	int32 CountExact(UWorld* World, const UClass* Class)
	{
		int32 Count = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			Count += It->GetClass() == Class ? 1 : 0;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSupplyLootThresholdsTest,
	"Tortunabo.Supply.Loot.Thresholds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSupplyLootThresholdsTest::RunTest(const FString& Parameters)
{
	using TNLootRules::ChapasFromRoll;
	const TArray<float> Crate = TNLootRules::SupplyCrateDefaults().ChapaChances;
	TestEqual(TEXT("Caja: 0,05 < 0,07 da 2 chapas"), ChapasFromRoll(Crate, 0.05f), 2);
	TestEqual(TEXT("Caja: 0,10 da 1 chapa"), ChapasFromRoll(Crate, 0.10f), 1);
	TestEqual(TEXT("Caja: 0,25 justo no da nada"), ChapasFromRoll(Crate, 0.25f), 0);
	TestEqual(TEXT("Caja: nunca 3 chapas (0 en la hoja)"), ChapasFromRoll(Crate, 0.f), 2);

	const TArray<float> Airdrop = TNLootRules::AirdropDefaults().ChapaChances;
	TestEqual(TEXT("Airdrop: 0,10 da 3 chapas"), ChapasFromRoll(Airdrop, 0.10f), 3);
	TestEqual(TEXT("Airdrop: 0,20 da 2 chapas"), ChapasFromRoll(Airdrop, 0.20f), 2);
	TestEqual(TEXT("Airdrop: 0,50 da 1 chapa"), ChapasFromRoll(Airdrop, 0.50f), 1);
	TestEqual(TEXT("Airdrop: 0,80 no da nada"), ChapasFromRoll(Airdrop, 0.80f), 0);

	// Caso negativo: datos mal puestos no dan más chapas de la cuenta.
	TestEqual(TEXT("Sin umbrales no hay chapas"), ChapasFromRoll(TArray<float>(), 0.f), 0);
	const TArray<float> Rising = { 0.2f, 0.9f };
	TestEqual(TEXT("Un umbral mayor que el anterior se recorta: no es más fácil sacar 2 que 1"), ChapasFromRoll(Rising, 0.5f), 0);
	const TArray<float> OutOfRange = { 1.5f, -0.3f };
	TestEqual(TEXT("Por encima de 1 cuenta como 1; negativo, como 0"), ChapasFromRoll(OutOfRange, 0.99f), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSupplyLootExactTest,
	"Tortunabo.Supply.Loot.ExactChances",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSupplyLootExactTest::RunTest(const FString& Parameters)
{
	using TNLootRules::ChanceOfExactly;
	const TArray<float> Crate = TNLootRules::SupplyCrateDefaults().ChapaChances;
	TestNearlyEqual(TEXT("Caja: ninguna chapa 0,75"), ChanceOfExactly(Crate, 0), 0.75f, 1e-5f);
	TestNearlyEqual(TEXT("Caja: 1 chapa 0,18"), ChanceOfExactly(Crate, 1), 0.18f, 1e-5f);
	TestNearlyEqual(TEXT("Caja: 2 chapas 0,07"), ChanceOfExactly(Crate, 2), 0.07f, 1e-5f);
	TestNearlyEqual(TEXT("Caja: 3 chapas 0"), ChanceOfExactly(Crate, 3), 0.f, 1e-5f);
	const TArray<float> Airdrop = TNLootRules::AirdropDefaults().ChapaChances;
	TestNearlyEqual(TEXT("Airdrop: ninguna 0,3"), ChanceOfExactly(Airdrop, 0), 0.3f, 1e-5f);
	TestNearlyEqual(TEXT("Airdrop: 1 chapa 0,4"), ChanceOfExactly(Airdrop, 1), 0.4f, 1e-5f);
	TestNearlyEqual(TEXT("Airdrop: 2 chapas 0,15"), ChanceOfExactly(Airdrop, 2), 0.15f, 1e-5f);
	TestNearlyEqual(TEXT("Airdrop: 3 chapas 0,15"), ChanceOfExactly(Airdrop, 3), 0.15f, 1e-5f);
	TestEqual(TEXT("Más chapas que umbrales: imposible"), ChanceOfExactly(Airdrop, 4), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSupplyLootDistributionTest,
	"Tortunabo.Supply.Loot.Distribution",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSupplyLootDistributionTest::RunTest(const FString& Parameters)
{
	using namespace TNSupplyLootTestDetail;
	constexpr int32 Samples = 200000;
	constexpr float Tolerance = 0.006f;
	for (const FTNLootTableDef& Def : { TNLootRules::SupplyCrateDefaults(), TNLootRules::AirdropDefaults() })
	{
		const TArray<float> Seen = ChapaFrequencies(Def, 861, Samples);
		for (int32 Chapas = 0; Chapas <= 3; ++Chapas)
		{
			const float Expected = TNLootRules::ChanceOfExactly(Def.ChapaChances, Chapas);
			TestTrue(FString::Printf(TEXT("%d chapas: %.4f frente a %.4f esperado"), Chapas, Seen[Chapas], Expected),
				FMath::Abs(Seen[Chapas] - Expected) <= Tolerance);
		}
	}

	// Semilla fija: el mismo reparto dos veces.
	FRandomStream A(4242);
	FRandomStream B(4242);
	const FTNLootTableDef Airdrop = TNLootRules::AirdropDefaults();
	bool bSame = true;
	for (int32 i = 0; i < 64; ++i)
	{
		const FTNLootRoll RollA = TNLootRules::Roll(Airdrop, A);
		const FTNLootRoll RollB = TNLootRules::Roll(Airdrop, B);
		bSame &= RollA.ItemCount == RollB.ItemCount && RollA.ChapaCount == RollB.ChapaCount;
	}
	TestTrue(TEXT("Con la misma semilla sale lo mismo"), bSame);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSupplyLootItemsTest,
	"Tortunabo.Supply.Loot.Items",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSupplyLootItemsTest::RunTest(const FString& Parameters)
{
	using TNLootRules::ItemsFromRoll;
	TestEqual(TEXT("Caja: siempre 1 objeto"), ItemsFromRoll(1, 1, 0.99f), 1);
	TestEqual(TEXT("De 1 a 3: tirada baja, 1"), ItemsFromRoll(1, 3, 0.f), 1);
	TestEqual(TEXT("De 1 a 3: tirada media, 2"), ItemsFromRoll(1, 3, 0.5f), 2);
	TestEqual(TEXT("De 1 a 3: tirada alta, 3"), ItemsFromRoll(1, 3, 0.999f), 3);
	TestEqual(TEXT("Tirada de 1 (fuera de rango) no se pasa del máximo"), ItemsFromRoll(1, 3, 1.f), 3);
	TestEqual(TEXT("Máximo menor que el mínimo: cuenta el mínimo"), ItemsFromRoll(2, 0, 0.7f), 2);
	TestEqual(TEXT("Mínimo negativo: nunca objetos negativos"), ItemsFromRoll(-3, 0, 0.1f), 0);
	TestEqual(TEXT("Airdrop: 2 objetos"), TNLootRules::AirdropDefaults().MinItems, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSupplyCrateOpensOnceTest,
	"Tortunabo.Supply.Crate.OpensOnce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSupplyCrateOpensOnceTest::RunTest(const FString& Parameters)
{
	using namespace TNSupplyLootTestDetail;
	UWorld* World = CreateWorld(TEXT("TNSupplyCrateTestWorld"));
	if (!TestNotNull(TEXT("Mundo de prueba"), World))
	{
		return false;
	}
	// Sin objetos y con 3 chapas seguras; la «chapa» de prueba es un actor vacío.
	FTNLootTableDef Loot;
	Loot.MinItems = 0;
	Loot.MaxItems = 0;
	Loot.ChapaChances = { 1.f, 1.f, 1.f };
	Loot.ChapaClass = AActor::StaticClass();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APawn* Opener = World->SpawnActor<APawn>(APawn::StaticClass(), FTransform(FVector(150.0, 0.0, 50.0)), Params);
	ATN_SupplyCrate* Crate = ATN_SupplyCrate::ServerSpawn(World, FVector::ZeroVector, 0.f);
	// El mundo ya trae algún actor de la clase base: se cuentan las chapas por encima de esos.
	const int32 Before = CountExact(World, AActor::StaticClass());
	if (TestNotNull(TEXT("Caja"), Crate))
	{
		Crate->OverrideLoot(Loot);
		Crate->SetRollSeed(861);
		TestFalse(TEXT("Recién puesta, cerrada"), Crate->IsOpened());
		TestTrue(TEXT("Cerrada, se puede empezar a abrir"), Crate->CanInteract(Opener));
		TestTrue(TEXT("Se abre"), Crate->ServerOpen());
		TestTrue(TEXT("Queda abierta"), Crate->IsOpened());
		TestEqual(TEXT("Da 3 chapas"), Crate->GetLastRoll().ChapaCount, 3);
		TestEqual(TEXT("Suelta 3 chapas"), CountExact(World, AActor::StaticClass()) - Before, 3);

		// Caso negativo: abierta, no vuelve a dar nada.
		TestFalse(TEXT("Ya abierta no se abre otra vez"), Crate->ServerOpen());
		TestEqual(TEXT("No suelta más chapas"), CountExact(World, AActor::StaticClass()) - Before, 3);
		TestFalse(TEXT("Ya abierta, nadie puede empezar a abrirla"), Crate->CanInteract(Opener));
	}

	// Sin clase de chapa: el reparto se registra y no se suelta nada.
	ATN_SupplyCrate* NoChapa = ATN_SupplyCrate::ServerSpawn(World, FVector(500.0, 0.0, 0.0), 0.f);
	if (TestNotNull(TEXT("Caja sin chapa"), NoChapa))
	{
		Loot.ChapaClass.Reset();
		NoChapa->OverrideLoot(Loot);
		TestTrue(TEXT("Se abre igual"), NoChapa->ServerOpen());
		TestEqual(TEXT("Cuenta 3 chapas"), NoChapa->GetLastRoll().ChapaCount, 3);
		TestEqual(TEXT("Pero no suelta ninguna"), NoChapa->GetLastSpawnedChapas(), 0);
		TestEqual(TEXT("Siguen las 3 de antes"), CountExact(World, AActor::StaticClass()) - Before, 3);
	}
	DestroyWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
