// Tormenta de arena del coop (#790): calendario con semilla, ráfagas de 8 m/s, solo freno y empuje, y el ajuste de
// accesibilidad. Correr desde Session Frontend (categoría "Tortunabo.ProcMap.SandStorm") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.ProcMap.SandStorm; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Settings/TN_SettingsSaveGame.h"
#include "World/ProcMap/TN_SandStormRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSandStormScheduleTest,
	"Tortunabo.ProcMap.SandStorm.Calendario",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSandStormScheduleTest::RunTest(const FString& Parameters)
{
	using namespace TNSandStorm;
	int32 Index = 0;
	double Local = 0.0;
	TestFalse(TEXT("Al empezar la ronda no hay tormenta"), EventAt(1u, 0.0, Index, Local));
	TestFalse(TEXT("Ni antes de la primera"), EventAt(1u, FIRST_DELAY - JITTER - 1.0, Index, Local));

	bool bSomeDiffer = false;
	for (uint32 Seed = 1; Seed <= 25; ++Seed)
	{
		double PrevEnd = -1.0;
		for (int32 i = 0; i < 30; ++i)
		{
			const double Start = EventStart(Seed, i);
			TestTrue(TEXT("Cada tormenta empieza cuando ya se ha ido la anterior"), Start > PrevEnd);
			PrevEnd = Start + DURATION;

			// A mitad de la tormenta i, EventAt da esa misma y sus segundos.
			if (TestTrue(TEXT("En mitad de una tormenta, hay tormenta"), EventAt(Seed, Start + DURATION * 0.5, Index, Local)))
			{
				TestEqual(TEXT("Es la tormenta que toca"), Index, i);
				TestEqual(TEXT("Con los segundos que lleva"), Local, DURATION * 0.5, 1e-6);
			}
			TestFalse(TEXT("Justo al acabar, no"), EventAt(Seed, Start + DURATION + 0.01, Index, Local));
		}
		TestEqual(TEXT("Misma semilla, mismo calendario (todas las máquinas a la vez)"), EventStart(Seed, 3), EventStart(Seed, 3));
		bSomeDiffer |= !FMath::IsNearlyEqual(EventStart(Seed, 0), EventStart(Seed + 100u, 0));
	}
	TestTrue(TEXT("Semillas distintas, calendarios distintos"), bSomeDiffer);

	TestEqual(TEXT("Sin fuerza al empezar"), Intensity01(0.0), 0.f);
	TestEqual(TEXT("En su punto tras llegar"), Intensity01(FADE), 1.f);
	TestEqual(TEXT("En su punto a mitad"), Intensity01(DURATION * 0.5), 1.f);
	TestTrue(TEXT("Casi nada al irse"), Intensity01(DURATION - 0.05) < 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSandStormGustTest,
	"Tortunabo.ProcMap.SandStorm.Rafagas",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSandStormGustTest::RunTest(const FString& Parameters)
{
	using namespace TNSandStorm;
	TestEqual(TEXT("El viento de las ráfagas es de 8 m/s"), GUST_SPEED, 800.f);
	TestTrue(TEXT("Parada, la ráfaga la empuja"), PushAcceleration(0.f, 1.f) > 0.f);
	TestEqual(TEXT("A 8 m/s con el viento ya no empuja más"), PushAcceleration(GUST_SPEED, 1.f), 0.f);
	TestEqual(TEXT("Más rápida que el viento, tampoco"), PushAcceleration(GUST_SPEED + 200.f, 1.f), 0.f);
	TestEqual(TEXT("Sin ráfaga, sin empuje"), PushAcceleration(0.f, 0.f), 0.f);
	TestTrue(TEXT("Contra el viento empuja más que parada"), PushAcceleration(-300.f, 1.f) > PushAcceleration(0.f, 1.f));

	bool bGusted = false;
	bool bCalm = false;
	for (double T = 0.0; T < DURATION; T += 0.1)
	{
		const float G = Gust01(9u, 2, T);
		TestTrue(TEXT("La ráfaga va de 0 a 1"), G >= 0.f && G <= 1.f);
		TestTrue(TEXT("Nunca más fuerte que la tormenta"), G <= Intensity01(T) + 1e-5f);
		bGusted |= G > 0.5f;
		bCalm |= G == 0.f && Intensity01(T) >= 1.f;
	}
	TestTrue(TEXT("Hay ráfagas fuertes"), bGusted);
	TestTrue(TEXT("Y ratos de calma entre ellas"), bCalm);
	TestEqual(TEXT("Fuera de la tormenta no sopla"), Gust01(9u, 2, DURATION + 1.0), 0.f);
	TestEqual(TEXT("El viento es una dirección"), WindDir(9u, 2).Size(), 1.0, 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSandStormHarmlessTest,
	"Tortunabo.ProcMap.SandStorm.SoloFrenoYEmpuje",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSandStormHarmlessTest::RunTest(const FString& Parameters)
{
	using namespace TNSandStorm;
	TestEqual(TEXT("Sin tormenta, velocidad normal"), SpeedFactor(0.f), 1.f);
	TestEqual(TEXT("En su punto, frena"), SpeedFactor(1.f), SPEED_FACTOR);
	TestTrue(TEXT("Frena pero nunca para (no atrapa)"), SPEED_FACTOR > 0.5f && SPEED_FACTOR < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSandStormAccessibilityTest,
	"Tortunabo.ProcMap.SandStorm.Accesibilidad",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSandStormAccessibilityTest::RunTest(const FString& Parameters)
{
	using namespace TNSandStorm;
	const FTNGameSettings Defaults;
	TestEqual(TEXT("«Efectos del clima» viene al 100 %"), Defaults.WeatherEffects, 1.f);
	TestEqual(TEXT("Al 100 %, la tormenta entera"), VisualWeight(1.f, Defaults.WeatherEffects), 1.f);
	TestEqual(TEXT("Al 50 %, la mitad"), VisualWeight(1.f, 0.5f), 0.5f);
	TestEqual(TEXT("Al mínimo, se sigue viendo algo"), VisualWeight(1.f, 0.f), MIN_VISUAL);
	TestEqual(TEXT("Sin tormenta, nada"), VisualWeight(0.f, 1.f), 0.f);
	TestEqual(TEXT("Llegando, a la vez que la tormenta"), VisualWeight(0.5f, 0.5f), 0.25f);
	return true;
}

#endif
