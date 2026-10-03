// Enemigos numerosos de la playa: cada cuánto se actualiza cada uno y el tope de cuántos van a ritmo completo (#79).
// Se testea la regla de TN_BeachEnemyLod.h que usa ATN_BeachEnemy::UpdateLod.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Beach.EnemyLod; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/Beach/TN_BeachEnemyLod.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachEnemyLodTierTest,
	"Tortunabo.Beach.EnemyLod.Tier",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachEnemyLodTierTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachEnemyLod;

	FInput In;
	In.VisualRange = 30000.f;
	In.bHasScreen = true;

	In.bActive = true;
	In.ViewDistance = 25000.f;
	TestTrue(TEXT("Activo (tortuga en su radio): ritmo completo aunque la cámara esté lejos"), Classify(In) == ETier::Full);

	In.bActive = false;
	In.ViewDistance = 1500.f;
	TestTrue(TEXT("Cámara muy cerca: ritmo completo"), Classify(In) == ETier::Full);

	// Antes, todo lo que estaba a menos de la mitad del radio visual (150 m) iba a ritmo completo: con los enemigos del
	// estrés repartidos entre 15 y 140 m, ninguno bajaba.
	In.ViewDistance = 9000.f;
	TestTrue(TEXT("A 90 m sin tortugas cerca: 30 Hz, no ritmo completo"), Classify(In) == ETier::Near);
	TestEqual(TEXT("Near = 30 Hz"), TickInterval(ETier::Near), NEAR_INTERVAL);

	In.ViewDistance = 20000.f;
	TestTrue(TEXT("Lejos pero dentro del radio visual: 15 Hz"), Classify(In) == ETier::FarSeen);

	In.ViewDistance = 31000.f;
	TestTrue(TEXT("Fuera del radio visual: 4 Hz"), Classify(In) == ETier::Hidden);

	In.bHasScreen = false;
	In.ViewDistance = 100.f;
	TestTrue(TEXT("Servidor dedicado sin tortugas cerca: 4 Hz"), Classify(In) == ETier::Hidden);

	TestEqual(TEXT("Full = cada fotograma"), TickInterval(ETier::Full), 0.f);
	TestTrue(TEXT("Los niveles bajan de ritmo según se alejan"),
		TickInterval(ETier::Near) < TickInterval(ETier::FarSeen) && TickInterval(ETier::FarSeen) < TickInterval(ETier::Hidden));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachEnemyLodBudgetTest,
	"Tortunabo.Beach.EnemyLod.Budget",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachEnemyLodBudgetTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachEnemyLod;

	TestTrue(TEXT("Dentro del tope: sigue a ritmo completo"), ApplyBudget(ETier::Full, MAX_FULL_RATE - 1) == ETier::Full);
	TestTrue(TEXT("Fuera del tope: baja a 30 Hz"), ApplyBudget(ETier::Full, MAX_FULL_RATE) == ETier::Near);
	TestTrue(TEXT("El tope no sube a nadie"), ApplyBudget(ETier::Hidden, 0) == ETier::Hidden);
	TestTrue(TEXT("El tope no toca a los que ya van despacio"), ApplyBudget(ETier::FarSeen, 500) == ETier::FarSeen);

	// 200 enemigos que piden ritmo completo: solo los MAX_FULL_RATE más cercanos lo tienen.
	int32 Full = 0;
	for (int32 Closer = 0; Closer < 200; ++Closer)
	{
		Full += ApplyBudget(ETier::Full, Closer) == ETier::Full ? 1 : 0;
	}
	TestEqual(TEXT("Con 200 activos, MAX_FULL_RATE a ritmo completo"), Full, MAX_FULL_RATE);

	TestEqual(TEXT("Prioridad en el servidor: la tortuga más cercana"), Priority(true, 1200.f, true, 5000.f), 1200.f);
	TestEqual(TEXT("Prioridad en un cliente: solo la cámara"), Priority(false, 1200.f, true, 5000.f), 5000.f);
	TestEqual(TEXT("Prioridad en un dedicado: solo las tortugas"), Priority(true, 7000.f, false, 10.f), 7000.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
