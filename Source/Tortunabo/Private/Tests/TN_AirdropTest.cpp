// Airdrop (#860): vuelo de la caja (aviso, caída y aterrizaje), elección del punto por el gestor y apertura solo en el
// suelo. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Supply.Airdrop; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "World/TN_AirdropSubsystem.h"
#include "World/TN_SupplyDrop.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNAirdropTestDetail
{
	FTNAirdropFlight MakeFlight(float Start, float Warn, float Fall, float Height)
	{
		FTNAirdropFlight Flight;
		Flight.Landing = FVector(100.0, 200.0, 10.0);
		Flight.StartTime = Start;
		Flight.WarnSeconds = Warn;
		Flight.FallSeconds = Fall;
		Flight.Height = Height;
		return Flight;
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNAirdropFlightTest,
	"Tortunabo.Supply.Airdrop.Flight",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNAirdropFlightTest::RunTest(const FString& Parameters)
{
	using namespace TNAirdropRules;
	const FTNAirdropFlight Flight = TNAirdropTestDetail::MakeFlight(100.f, 8.f, 12.f, 4000.f);
	TestTrue(TEXT("Al empezar, aviso"), PhaseAt(Flight, 100.0) == ETNAirdropPhase::Warning);
	TestEqual(TEXT("En el aviso, arriba del todo"), HeightAt(Flight, 104.0), 4000.f);
	TestTrue(TEXT("Pasado el aviso, cae"), PhaseAt(Flight, 108.0) == ETNAirdropPhase::Falling);
	TestNearlyEqual(TEXT("A media caída, a media altura (velocidad constante)"), HeightAt(Flight, 114.0), 2000.f, 0.5f);
	TestTrue(TEXT("Pasada la caída, en el suelo"), PhaseAt(Flight, 120.0) == ETNAirdropPhase::Landed);
	TestEqual(TEXT("En el suelo, altura 0"), HeightAt(Flight, 130.0), 0.f);
	TestNearlyEqual(TEXT("Al empezar faltan 20 s"), SecondsToLand(Flight, 100.0), 20.f, 1e-3f);
	TestEqual(TEXT("En el suelo no falta nada"), SecondsToLand(Flight, 500.0), 0.f);

	// Caso negativo: tiempos sin sentido no dejan la caja colgada ni bajo el suelo.
	const FTNAirdropFlight Instant = TNAirdropTestDetail::MakeFlight(0.f, -5.f, 0.f, 3000.f);
	TestTrue(TEXT("Sin aviso ni caída aterriza al momento"), PhaseAt(Instant, 0.0) == ETNAirdropPhase::Landed);
	TestEqual(TEXT("Y está en el suelo"), HeightAt(Instant, 0.0), 0.f);
	const FTNAirdropFlight Below = TNAirdropTestDetail::MakeFlight(0.f, 1.f, 1.f, -500.f);
	TestEqual(TEXT("Altura negativa: nunca bajo el suelo"), HeightAt(Below, 0.5), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNAirdropScheduleTest,
	"Tortunabo.Supply.Airdrop.Schedule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNAirdropScheduleTest::RunTest(const FString& Parameters)
{
	using namespace TNAirdropSchedule;
	TestTrue(TEXT("Quedan airdrops y hay un punto libre"), CanLaunch(0, 3, 1));
	TestFalse(TEXT("Llegado el máximo, no se lanza"), CanLaunch(3, 3, 4));
	TestFalse(TEXT("Sin puntos libres, no se lanza"), CanLaunch(0, 3, 0));
	TestFalse(TEXT("Máximo 0: nunca"), CanLaunch(0, 0, 4));

	const TArray<bool> AllFree = { false, false, false };
	for (float Roll : { 0.f, 0.4f, 0.99f })
	{
		TestNotEqual(FString::Printf(TEXT("Con otros libres no repite el último (tirada %.2f)"), Roll), PickPoint(AllFree, 1, Roll), 1);
	}
	const TArray<bool> OnlyLast = { true, false, true };
	TestEqual(TEXT("Si solo queda libre el último, se repite"), PickPoint(OnlyLast, 1, 0.7f), 1);
	const TArray<bool> NoneFree = { true, true };
	TestEqual(TEXT("Ninguno libre: ninguno"), PickPoint(NoneFree, INDEX_NONE, 0.2f), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Sin puntos: ninguno"), PickPoint(TArray<bool>(), INDEX_NONE, 0.2f), static_cast<int32>(INDEX_NONE));
	const TArray<bool> SecondBusy = { false, true, false };
	TestEqual(TEXT("Tirada baja: el primer libre"), PickPoint(SecondBusy, INDEX_NONE, 0.f), 0);
	TestEqual(TEXT("Tirada alta: el último libre, nunca el ocupado"), PickPoint(SecondBusy, INDEX_NONE, 0.99f), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNAirdropOpensOnGroundTest,
	"Tortunabo.Supply.Airdrop.OpensOnGround",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNAirdropOpensOnGroundTest::RunTest(const FString& Parameters)
{
	using namespace TNAirdropTestDetail;
	UWorld* World = CreateWorld(TEXT("TNAirdropTestWorld"));
	if (!TestNotNull(TEXT("Mundo de prueba"), World))
	{
		return false;
	}
	// Sin objetos (no hace falta el catálogo) y sin clase de chapa: la apertura solo se registra.
	FTNLootTableDef NoItems = TNLootRules::AirdropDefaults();
	NoItems.MinItems = 0;
	NoItems.MaxItems = 0;

	// En el aire no se abre (caso negativo).
	ATN_SupplyDrop* Falling = ATN_SupplyDrop::ServerLaunch(World, nullptr, FVector::ZeroVector, 3000.f, 5.f, 10.f);
	if (TestNotNull(TEXT("Airdrop en el aviso"), Falling))
	{
		Falling->OverrideLoot(NoItems);
		TestTrue(TEXT("Recién lanzado está en el aviso"), Falling->GetPhase() == ETNAirdropPhase::Warning);
		TestTrue(TEXT("Empieza arriba"), Falling->GetActorLocation().Z > 2900.0);
		TestFalse(TEXT("En el aire no se puede abrir"), Falling->ServerOpen());
		TestFalse(TEXT("Sigue cerrado"), Falling->IsOpened());
	}

	// Sin aviso ni caída: en el suelo, se abre una vez con la tabla del airdrop.
	ATN_SupplyDrop* Landed = ATN_SupplyDrop::ServerLaunch(World, nullptr, FVector(800.0, 0.0, 0.0), 3000.f, 0.f, 0.f);
	if (TestNotNull(TEXT("Airdrop en el suelo"), Landed))
	{
		TestTrue(TEXT("Usa la tabla del airdrop"), Landed->GetLootDef().ChapaChances == TNLootRules::AirdropDefaults().ChapaChances);
		Landed->OverrideLoot(NoItems);
		TestTrue(TEXT("Aterriza al momento"), Landed->GetPhase() == ETNAirdropPhase::Landed);
		TestTrue(TEXT("En el suelo se abre"), Landed->ServerOpen());
		TestFalse(TEXT("Y no se abre dos veces"), Landed->ServerOpen());
	}

	TestTrue(TEXT("Sin mundo no se lanza"), ATN_SupplyDrop::ServerLaunch(nullptr, nullptr, FVector::ZeroVector, 1.f, 0.f, 0.f) == nullptr);
	DestroyWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
