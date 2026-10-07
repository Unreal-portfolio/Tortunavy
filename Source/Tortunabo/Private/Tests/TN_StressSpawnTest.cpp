// Estrés: los enemigos se crean repartidos en varios fotogramas con un presupuesto por fotograma (#79).
// Se testea la regla de TN_StressScenarios.h que usa UTN_StressSubsystem::SpawnPending.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Stress; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Testing/TN_StressScenarios.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNStressSpawnBudgetTest,
	"Tortunabo.Stress.SpawnBudget",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNStressSpawnBudgetTest::RunTest(const FString& Parameters)
{
	using namespace TNStress;

	TestTrue(TEXT("Cangrejos, gaviotas y tanques: repartidos"),
		IsSpreadGroup(EGroup::Crabs) && IsSpreadGroup(EGroup::Gulls) && IsSpreadGroup(EGroup::Patrols));
	TestFalse(TEXT("Referencia: nada que crear"), IsSpreadGroup(EGroup::Baseline));

	TestTrue(TEXT("El primero del fotograma siempre (aunque el presupuesto ya no dé)"), ShouldSpawnMore(10, 0, 50.0));
	TestTrue(TEXT("Con presupuesto, otro más"), ShouldSpawnMore(10, 3, SPAWN_BUDGET_MS - 0.5));
	TestFalse(TEXT("Gastado el presupuesto, al siguiente fotograma"), ShouldSpawnMore(10, 3, SPAWN_BUDGET_MS));
	TestFalse(TEXT("Sin pendientes, nada"), ShouldSpawnMore(0, 0, 0.0));

	// 100 cangrejos a 0,4 ms cada uno (lo medido el 29-09): ninguno de los fotogramas pasa de presupuesto + uno.
	const double CostMs = 0.4;
	int32 Left = 100;
	int32 Frames = 0;
	double WorstMs = 0.0;
	while (Left > 0 && Frames < 1000)
	{
		int32 Made = 0;
		double Spent = 0.0;
		while (ShouldSpawnMore(Left, Made, Spent))
		{
			Spent += CostMs;
			--Left;
			++Made;
		}
		WorstMs = FMath::Max(WorstMs, Spent);
		++Frames;
	}
	TestEqual(TEXT("Se crean todos"), Left, 0);
	TestTrue(TEXT("En varios fotogramas"), Frames > 1);
	TestTrue(TEXT("Ningún fotograma pasa del presupuesto más uno"), WorstMs <= SPAWN_BUDGET_MS + CostMs);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
