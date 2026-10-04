// Mezcla del sonido continuo del buggy (TNBuggyAudio, TN_BuggyEngineAudioComponent.h): tres capas de motor por RPM y
// derrape por deriva. Lógica pura, sin mundo. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Audio; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Vehicles/TN_BuggyEngineAudioComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyAudioEngineLayersTest,
	"Tortunabo.Rally.Audio.EngineLayers",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyAudioEngineLayersTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggyAudio;
	const FEngineLayerMix Idle = MixEngineLayers(0.f);
	TestEqual(TEXT("parado: solo ralentí"), Idle.Volume[0], 1.f, 0.001f);
	TestEqual(TEXT("parado: medio en silencio"), Idle.Volume[1], 0.f, 0.001f);
	TestEqual(TEXT("parado: alto en silencio"), Idle.Volume[2], 0.f, 0.001f);

	const FEngineLayerMix Mid = MixEngineLayers(0.5f);
	TestEqual(TEXT("media RPM: solo medio"), Mid.Volume[1], 1.f, 0.001f);
	TestEqual(TEXT("media RPM: medio a tono normal"), Mid.Pitch[1], 1.f, 0.001f);

	const FEngineLayerMix High = MixEngineLayers(1.f);
	TestEqual(TEXT("a tope: solo alto"), High.Volume[2], 1.f, 0.001f);
	TestEqual(TEXT("a tope: ralentí en silencio"), High.Volume[0], 0.f, 0.001f);

	bool bConstantPower = true;
	bool bPitchRises = true;
	float PreviousPitch = MixEngineLayers(0.f).Pitch[1];
	for (float Rpm = 0.f; Rpm <= 1.f; Rpm += 0.05f)
	{
		const FEngineLayerMix Mix = MixEngineLayers(Rpm);
		const float Power = FMath::Square(Mix.Volume[0]) + FMath::Square(Mix.Volume[1]) + FMath::Square(Mix.Volume[2]);
		bConstantPower &= FMath::IsNearlyEqual(Power, 1.f, 0.001f);
		bPitchRises &= Mix.Pitch[1] >= PreviousPitch;
		PreviousPitch = Mix.Pitch[1];
	}
	TestTrue(TEXT("potencia constante en todo el rango"), bConstantPower);
	TestTrue(TEXT("el tono sube con la RPM"), bPitchRises);

	const FEngineLayerMix OutOfRange = MixEngineLayers(3.f);
	TestEqual(TEXT("RPM fuera de rango se recorta"), OutOfRange.Volume[2], 1.f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyAudioSkidTest,
	"Tortunabo.Rally.Audio.Skid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyAudioSkidTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggyAudio;
	TestEqual(TEXT("recto no derrapa"), SkidVolume(2.f, 2000.f, true, 12.f, 35.f, 1200.f), 0.f);
	TestEqual(TEXT("de lado y rápido: entero"), SkidVolume(40.f, 2000.f, true, 12.f, 35.f, 1200.f), 1.f);
	TestEqual(TEXT("en el aire no suena"), SkidVolume(40.f, 2000.f, false, 12.f, 35.f, 1200.f), 0.f);
	TestEqual(TEXT("casi parado no suena"), SkidVolume(80.f, 50.f, true, 12.f, 35.f, 1200.f), 0.f);
	TestEqual(TEXT("deriva negativa vale lo mismo"), SkidVolume(-40.f, 2000.f, true, 12.f, 35.f, 1200.f), 1.f);
	const float Half = SkidVolume(40.f, 600.f, true, 12.f, 35.f, 1200.f);
	TestEqual(TEXT("a media velocidad suena a medias"), Half, 0.5f, 0.001f);

	TestEqual(TEXT("RPM estimada parada: ralentí"), EstimateRpm01(0.f, 3000.f), 0.05f, 0.001f);
	TestEqual(TEXT("RPM estimada a punta: 1"), EstimateRpm01(3000.f, 3000.f), 1.f, 0.001f);
	TestEqual(TEXT("RPM estimada marcha atrás"), EstimateRpm01(-1500.f, 3000.f), 0.5f, 0.001f);
	return true;
}

#endif
