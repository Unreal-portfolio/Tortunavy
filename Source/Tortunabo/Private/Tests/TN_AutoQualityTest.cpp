// Calidad del primer arranque (#556): con la marca sin poner (GameUserSettings.ini sin resultados de la prueba del equipo,
// que el motor guarda como -1) la lógica de decisión pide la prueba; con la marca puesta, no. Tampoco la pide el editor ni un
// proceso sin pantalla. Se testean las funciones de TN_AutoQualityDecisions.h que usa UTN_GameSettingsSubsystem::Initialize.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Settings.AutoQuality; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Settings/TN_AutoQualityDecisions.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNAutoQualityDecideTest,
	"Tortunabo.Settings.AutoQuality.Decide",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNAutoQualityDecideTest::RunTest(const FString& Parameters)
{
	using namespace TNAutoQuality;

	// Primera vez: el motor guarda -1 mientras no ha habido prueba.
	TestTrue(TEXT("Sin marca (-1, -1) y con pantalla → pasa la prueba"), Decide(false, true, -1.f, -1.f) == EDecision::RunBenchmark);
	TestTrue(TEXT("Solo la CPU medida (fichero a medias) → se pasa de nuevo"), Decide(false, true, 80.f, -1.f) == EDecision::RunBenchmark);
	TestTrue(TEXT("Solo la GPU medida (fichero a medias) → se pasa de nuevo"), Decide(false, true, -1.f, 80.f) == EDecision::RunBenchmark);

	// Segunda vez: los resultados ya están en el fichero.
	TestTrue(TEXT("Con marca (CPU y GPU >= 0) → no se repite"), Decide(false, true, 95.f, 120.f) == EDecision::SkipAlreadyDone);
	TestTrue(TEXT("Un resultado de 0 también es marca (el motor lo deja en 0 si no midió un paso)"), Decide(false, true, 0.f, 0.f) == EDecision::SkipAlreadyDone);

	// Primer arranque y segundo seguidos: tras la prueba el fichero trae los resultados y el siguiente arranque la salta.
	float Cpu = -1.f;
	float Gpu = -1.f;
	TestTrue(TEXT("Arranque 1: pide la prueba"), Decide(false, true, Cpu, Gpu) == EDecision::RunBenchmark);
	Cpu = 112.f;
	Gpu = 64.f;
	TestTrue(TEXT("Arranque 2: ya no la pide"), Decide(false, true, Cpu, Gpu) == EDecision::SkipAlreadyDone);
	TestTrue(TEXT("Arranque 3: tampoco"), Decide(false, true, Cpu, Gpu) == EDecision::SkipAlreadyDone);

	// Donde no se pasa nunca, aunque no haya marca.
	TestTrue(TEXT("En el editor (PIE) no se pasa"), Decide(true, true, -1.f, -1.f) == EDecision::SkipEditor);
	TestTrue(TEXT("En el editor tampoco con marca"), Decide(true, true, 90.f, 90.f) == EDecision::SkipEditor);
	TestTrue(TEXT("Sin pantalla (-nullrhi, servidor, commandlet) no se pasa"), Decide(false, false, -1.f, -1.f) == EDecision::SkipNoRender);
	TestTrue(TEXT("El editor manda sobre la falta de pantalla"), Decide(true, false, -1.f, -1.f) == EDecision::SkipEditor);

	TestFalse(TEXT("HasBenchmarkResults(-1, -1)"), HasBenchmarkResults(-1.f, -1.f));
	TestTrue(TEXT("HasBenchmarkResults(10, 20)"), HasBenchmarkResults(10.f, 20.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNAutoQualityFrameRateTest,
	"Tortunabo.Settings.AutoQuality.FrameRate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNAutoQualityFrameRateTest::RunTest(const FString& Parameters)
{
	using namespace TNAutoQuality;

	TestEqual(TEXT("Sin límite (0) → 60"), FirstBootFrameRateLimit(0.f), 60.f);
	TestEqual(TEXT("Valor negativo (fichero raro) → 60"), FirstBootFrameRateLimit(-1.f), 60.f);
	TestEqual(TEXT("Un límite que ya hay se conserva (144)"), FirstBootFrameRateLimit(144.f), 144.f);
	TestEqual(TEXT("Un límite que ya hay se conserva (30)"), FirstBootFrameRateLimit(30.f), 30.f);
	return true;
}

#endif
