// Cuándo la animación de la tortuga pasa a la carrera (#834, Player/TN_RunGait.h), sin mundo ni actores: esprintar sin
// fuerzas no es correr y la velocidad de carrera lo es aunque no se esprinte.
// Correr desde Session Frontend (categoría "Tortunabo.RunGait") o sin ventana:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.RunGait; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_RunGait.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRunGaitSprintTest,
	"Tortunabo.RunGait.Sprint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRunGaitSprintTest::RunTest(const FString& Parameters)
{
	using namespace TNRunGait;
	constexpr float Walk = 450.f;

	TestTrue(TEXT("Esprintando con estamina y ya andando deprisa: carrera"), IsSprintRun(true, false, 0.8f, false, 500.f, Walk));
	TestFalse(TEXT("Sin pedir esprintar, no es de sprint"), IsSprintRun(false, false, 0.8f, false, 500.f, Walk));
	TestFalse(TEXT("Parada: no corre aunque pida esprintar"), IsSprintRun(true, false, 0.8f, false, 50.f, Walk));

	// #834: sin estamina y con la tecla pulsada, a paso de andar.
	TestFalse(TEXT("Agotada: no es carrera aunque siga pidiendo esprintar"), IsSprintRun(true, true, 0.f, false, Walk, Walk));
	TestFalse(TEXT("Sin estamina, tampoco"), IsSprintRun(true, false, 0.f, false, Walk, Walk));
	TestFalse(TEXT("El destello de un fotograma al recargar no es carrera"), IsSprintRun(true, false, MinStaminaFraction * 0.2f, false, Walk, Walk));
	TestTrue(TEXT("Con algo de aliento, vuelve a correr"), IsSprintRun(true, false, MinStaminaFraction * 2.f, false, Walk, Walk));
	TestTrue(TEXT("Con estamina ilimitada no hay agotamiento que valga"), IsSprintRun(true, true, 0.f, true, 500.f, Walk));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRunGaitTargetTest,
	"Tortunabo.RunGait.Target",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRunGaitTargetTest::RunTest(const FString& Parameters)
{
	using namespace TNRunGait;
	constexpr float Walk = 450.f;

	TestEqual(TEXT("Esprintando de verdad, carrera entera desde el primer momento"), RunTarget(true, 300.f, Walk), 1.f);
	TestEqual(TEXT("Andando, nada de carrera"), RunTarget(false, Walk, Walk), 0.f);
	TestEqual(TEXT("Parada, tampoco"), RunTarget(false, 0.f, Walk), 0.f);
	TestEqual(TEXT("A velocidad de sprint, carrera entera aunque no se pida (turbo, bajada)"), RunTarget(false, 800.f, Walk), 1.f);
	const float Half = RunTarget(false, Walk * (BySpeedStartFraction + BySpeedRampFraction * 0.5f), Walk);
	TestEqual(TEXT("A mitad de la subida, media carrera"), Half, 0.5f, 1e-3f);

	// La caída de la estamina: del sprint a 800 a andar a 450, la carrera sale sola.
	TestEqual(TEXT("Sin fuerzas y a paso de andar, andar"), RunTarget(IsSprintRun(true, true, 0.f, false, Walk, Walk), Walk, Walk), 0.f);
	return true;
}

#endif
