// Registro de tirones (#152): a qué hilo se atribuye un fotograma largo y la mediana del intervalo entre tirones.
// Correr con: UnrealEditor-Win64-DebugGame-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Testing.HitchTracker; Quit" -nullrhi

#include "Misc/AutomationTest.h"
#include "Testing/TN_HitchTracker.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNHitchTrackerClassifyTest, "Tortunabo.Testing.HitchTracker.Classify",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNHitchTrackerClassifyTest::RunTest(const FString& Parameters)
{
	using TNHitch::EBound;
	TestTrue(TEXT("GPU de 240 ms en un fotograma de 260"), TNHitch::Classify(260.f, 12.f, 15.f, 8.f, 240.f) == EBound::Gpu);
	TestTrue(TEXT("Hilo de juego de 230 ms"), TNHitch::Classify(250.f, 230.f, 20.f, 8.f, 18.f) == EBound::GameThread);
	TestTrue(TEXT("Hilo de render de 140 ms"), TNHitch::Classify(150.f, 10.f, 140.f, 8.f, 30.f) == EBound::RenderThread);
	TestTrue(TEXT("Hilo RHI de 180 ms"), TNHitch::Classify(200.f, 10.f, 12.f, 180.f, 15.f) == EBound::RhiThread);
	// Ningún hilo llega a la mitad: una espera (presentación, controlador) o el proceso parado.
	TestTrue(TEXT("Nada explica 250 ms"), TNHitch::Classify(250.f, 12.f, 15.f, 9.f, 18.f) == EBound::Unknown);
	TestTrue(TEXT("Justo la mitad cuenta"), TNHitch::Classify(100.f, 50.f, 10.f, 10.f, 10.f) == EBound::GameThread);
	TestTrue(TEXT("Fotograma nulo"), TNHitch::Classify(0.f, 5.f, 5.f, 5.f, 5.f) == EBound::Unknown);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNHitchTrackerIntervalTest, "Tortunabo.Testing.HitchTracker.Interval",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNHitchTrackerIntervalTest::RunTest(const FString& Parameters)
{
	TNHitch::FTracker Tracker;
	TestEqual(TEXT("Sin tirones no hay intervalo"), Tracker.MedianIntervalSeconds(), 0.0);
	Tracker.Add(10.0);
	TestEqual(TEXT("Con uno tampoco"), Tracker.MedianIntervalSeconds(), 0.0);
	TestEqual(TEXT("Ni intervalo desde el anterior"), Tracker.LastIntervalSeconds(), 0.0);

	// Cada 3-4 s, como en #152, con un intervalo raro que la mediana descarta.
	for (const double Time : { 13.5, 17.0, 20.0, 40.0, 43.5 })
	{
		Tracker.Add(Time);
	}
	TestEqual(TEXT("Total"), Tracker.Total(), 6);
	// Intervalos 3,5 · 3,5 · 3,0 · 20,0 · 3,5 → mediana 3,5.
	TestEqual(TEXT("Mediana de un número impar de intervalos"), Tracker.MedianIntervalSeconds(), 3.5);
	TestEqual(TEXT("Desde el anterior: 43,5 - 40"), Tracker.LastIntervalSeconds(), 3.5);
	Tracker.Add(47.5);
	// Intervalos 3,0 · 3,5 · 3,5 · 3,5 · 4,0 · 20,0 → mediana 3,5.
	TestEqual(TEXT("Mediana de un número par de intervalos"), Tracker.MedianIntervalSeconds(), 3.5);
	Tracker.Add(67.5);
	TestEqual(TEXT("Desde el anterior, aunque la mediana no cambie"), Tracker.LastIntervalSeconds(), 20.0);

	// Solo recuerda los últimos: tras muchos tirones cada 1 s, la mediana es 1 s aunque al principio fueran cada 3,5.
	TNHitch::FTracker Long;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Long.Add(3.5 * Index);
	}
	for (int32 Index = 1; Index <= TNHitch::FTracker::MaxRemembered; ++Index)
	{
		Long.Add(100.0 + Index);
	}
	TestEqual(TEXT("Ventana de los últimos tirones"), Long.MedianIntervalSeconds(), 1.0);
	TestEqual(TEXT("El total no se recorta"), Long.Total(), 5 + TNHitch::FTracker::MaxRemembered);
	return true;
}

#endif
