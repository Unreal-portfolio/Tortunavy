// Respuesta a los golpes (#350): sacudida y vibración proporcionales al golpe y apagables desde Ajustes.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.HitFeedback; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_HitFeedback.h"
#include "Settings/TN_SettingsSaveGame.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNHitFeedbackStrengthTest,
	"Tortunabo.HitFeedback.Strength",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNHitFeedbackStrengthTest::RunTest(const FString& Parameters)
{
	using namespace TNHitFeedback;
	TestEqual(TEXT("Sin empujón, la fuerza mínima de un derribo"), StrengthFromImpulse(0.f), MinStrength);
	TestEqual(TEXT("Un empujón negativo no baja de la mínima"), StrengthFromImpulse(-500.f), MinStrength);
	TestEqual(TEXT("Con el empujón entero, fuerza 1"), StrengthFromImpulse(FullImpulse), 1.f);
	TestEqual(TEXT("Por encima, sigue en 1"), StrengthFromImpulse(FullImpulse * 3.f), 1.f);
	TestTrue(TEXT("Más empujón, más fuerza"), StrengthFromImpulse(900.f) > StrengthFromImpulse(300.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNHitFeedbackPlanTest,
	"Tortunabo.HitFeedback.Plan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNHitFeedbackPlanTest::RunTest(const FString& Parameters)
{
	using namespace TNHitFeedback;
	const FToggles All;
	const FPlan Soft = MakePlan(MinStrength, All);
	const FPlan Hard = MakePlan(1.f, All);
	TestTrue(TEXT("Un golpe flojo tiembla y vibra"), Soft.ShakeTrauma > 0.f && Soft.VibrationIntensity > 0.f);
	TestTrue(TEXT("Más golpe, más temblor"), Hard.ShakeTrauma > Soft.ShakeTrauma);
	TestTrue(TEXT("Más golpe, más vibración"), Hard.VibrationIntensity > Soft.VibrationIntensity);
	TestTrue(TEXT("Más golpe, vibración más larga"), Hard.VibrationSeconds > Soft.VibrationSeconds);
	TestTrue(TEXT("La vibración es corta"), Hard.VibrationSeconds <= 0.5f);
	TestTrue(TEXT("El temblor es menor que el mazazo del cangrejo gigante (0,8)"), Hard.ShakeTrauma < 0.8f);

	FToggles NoShake;
	NoShake.bCameraShake = false;
	TestEqual(TEXT("Con el temblor apagado no hay sacudida"), MakePlan(1.f, NoShake).ShakeTrauma, 0.f);
	TestTrue(TEXT("...pero sí vibración"), MakePlan(1.f, NoShake).VibrationIntensity > 0.f);

	FToggles NoPad;
	NoPad.bVibration = false;
	TestEqual(TEXT("Con la vibración apagada no vibra"), MakePlan(1.f, NoPad).VibrationIntensity, 0.f);
	TestEqual(TEXT("...ni dura"), MakePlan(1.f, NoPad).VibrationSeconds, 0.f);
	TestTrue(TEXT("...pero sí tiembla"), MakePlan(1.f, NoPad).ShakeTrauma > 0.f);

	FToggles VR;
	VR.bVR = true;
	TestEqual(TEXT("En VR nunca hay sacudida (marea)"), MakePlan(1.f, VR).ShakeTrauma, 0.f);
	TestTrue(TEXT("En VR sí vibra"), MakePlan(1.f, VR).VibrationIntensity > 0.f);

	const FPlan None = MakePlan(0.f, All);
	TestTrue(TEXT("Sin golpe, nada"), None.ShakeTrauma == 0.f && None.VibrationIntensity == 0.f);
	TestEqual(TEXT("Una fuerza por encima de 1 cuenta como 1"), MakePlan(5.f, All).VibrationIntensity, Hard.VibrationIntensity);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNHitFeedbackSettingsTest,
	"Tortunabo.HitFeedback.Settings",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNHitFeedbackSettingsTest::RunTest(const FString& Parameters)
{
	const FTNGameSettings Defaults;
	TestTrue(TEXT("La vibración del mando viene encendida"), Defaults.bGamepadVibration);
	TestTrue(TEXT("El temblor de cámara viene encendido"), Defaults.bCameraShake);

	// Es un campo del guardado: apagada, se escribe y se lee apagada por el mismo camino que la ranura de ajustes. Un
	// guardado anterior a la versión 4 no lo trae y se queda con el valor de serie (encendida).
	UTN_SettingsSaveGame* Saved = NewObject<UTN_SettingsSaveGame>();
	Saved->Settings.bGamepadVibration = false;
	TArray<uint8> Bytes;
	TestTrue(TEXT("Se guarda en memoria"), UGameplayStatics::SaveGameToMemory(Saved, Bytes));
	const UTN_SettingsSaveGame* Loaded = Cast<UTN_SettingsSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (TestNotNull(TEXT("Se carga de memoria"), Loaded))
	{
		TestFalse(TEXT("Apagada se guarda y se carga apagada"), Loaded->Settings.bGamepadVibration);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
