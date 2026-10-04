// Versión de los ajustes guardados (#75): un guardado sin número cuenta como de la 1 y se migra sin tocar lo elegido;
// el de la versión actual se usa tal cual y el de una build más nueva también, con aviso y con copia antes de reescribirlo.
// Además, la espera entre reintentos cuando el guardado falla (SaveRetryDelay).
// Se testean las funciones de TN_SettingsMigration.h y TN_SaveGameDecisions.h que usa UTN_GameSettingsSubsystem.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Settings.Version; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Settings/TN_SettingsMigration.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSettingsVersionResolveTest,
	"Tortunabo.Settings.Version.Resolve",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSettingsVersionResolveTest::RunTest(const FString& Parameters)
{
	using namespace TNSettingsMigration;
	using TNSaveLogic::EMigration;

	TestEqual(TEXT("Sin número → la más antigua (1)"), ResolveSavedVersion(0), 1);
	TestEqual(TEXT("Número negativo (fichero raro) → 1"), ResolveSavedVersion(-4), 1);
	TestEqual(TEXT("Con número → ese"), ResolveSavedVersion(2), 2);

	FTNGameSettings Settings;
	TestTrue(TEXT("Sin número → migrar"), Migrate(Settings, 0) == EMigration::Upgrade);
	TestTrue(TEXT("De la 2 → migrar"), Migrate(Settings, 2) == EMigration::Upgrade);
	TestTrue(TEXT("La actual → tal cual"), Migrate(Settings, TNSaveLogic::SETTINGS_SAVE_VERSION) == EMigration::UpToDate);
	TestTrue(TEXT("De una build más nueva → tal cual, con aviso"),
		Migrate(Settings, TNSaveLogic::SETTINGS_SAVE_VERSION + 1) == EMigration::FromNewerBuild);

	UTN_SettingsSaveGame* Save = NewObject<UTN_SettingsSaveGame>();
	TestEqual(TEXT("Un guardado nuevo no tiene número hasta sellarlo (si no, no se escribiría)"), Save->Version, 0);
	Save->StampCurrentVersion();
	TestEqual(TEXT("Sellado con la actual"), Save->Version, TNSaveLogic::SETTINGS_SAVE_VERSION);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSettingsVersionMigrateTest,
	"Tortunabo.Settings.Version.Migrate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSettingsVersionMigrateTest::RunTest(const FString& Parameters)
{
	using namespace TNSettingsMigration;

	// Un guardado de la 2: lo que eligió el jugador y, de los campos de la 3, lo de serie (no venían en el fichero).
	FTNGameSettings FromV2;
	FromV2.MusicVolume = 0.3f;
	FromV2.KeyOverrides.Add(TEXT("IA_Jump#0"), TEXT("SpaceBar"));
	FromV2.UIScale = 1.25f;
	Migrate(FromV2, 2);
	TestEqual(TEXT("2 → 3 conserva el volumen"), FromV2.MusicVolume, 0.3f);
	TestEqual(TEXT("2 → 3 conserva las teclas"), FromV2.KeyOverrides.Num(), 1);
	TestEqual(TEXT("2 → 3 conserva la escala"), FromV2.UIScale, 1.25f);
	TestTrue(TEXT("2 → 3: idioma del sistema"), FromV2.Language.IsEmpty());
	TestTrue(TEXT("2 → 3: ojo de pez de serie (encendido)"), FromV2.bFisheye);

	// Un guardado de la 3: no trae la vibración del mando (4) y se queda la de serie (encendida).
	FTNGameSettings FromV3;
	FromV3.bFisheye = false;
	Migrate(FromV3, 3);
	TestFalse(TEXT("3 → 4 conserva el ojo de pez apagado"), FromV3.bFisheye);
	TestTrue(TEXT("3 → 4: vibración del mando de serie (encendida)"), FromV3.bGamepadVibration);

	// Un guardado sin número que en realidad es de la 3: no se pierde lo elegido de la 3.
	FTNGameSettings Unnumbered;
	Unnumbered.Language = TEXT("en");
	Unnumbered.bFisheye = false;
	Unnumbered.bGamepadVibration = false;
	Migrate(Unnumbered, 0);
	TestEqual(TEXT("Sin número: se queda el idioma elegido"), Unnumbered.Language, FString(TEXT("en")));
	TestFalse(TEXT("Sin número: se queda el ojo de pez apagado"), Unnumbered.bFisheye);
	TestFalse(TEXT("Sin número: se queda la vibración apagada"), Unnumbered.bGamepadVibration);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSettingsVersionNewerBuildTest,
	"Tortunabo.Settings.Version.NewerBuild",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSettingsVersionNewerBuildTest::RunTest(const FString& Parameters)
{
	using namespace TNSettingsMigration;
	using TNSaveLogic::EMigration;

	// Un guardado de una build más nueva se carga tal cual: no se migra ni se toca nada de lo leído.
	FTNGameSettings FromNewer;
	FromNewer.MusicVolume = 0.3f;
	FromNewer.Language = TEXT("en");
	TestTrue(TEXT("Versión mayor → tal cual, con aviso"),
		Migrate(FromNewer, TNSaveLogic::SETTINGS_SAVE_VERSION + 2) == EMigration::FromNewerBuild);
	TestEqual(TEXT("De una build más nueva conserva el volumen"), FromNewer.MusicVolume, 0.3f);
	TestEqual(TEXT("De una build más nueva conserva el idioma"), FromNewer.Language, FString(TEXT("en")));

	// Antes de reescribirlo con esta build, el fichero se copia a <ranura>_respaldo_v<versión del fichero>.
	TestEqual(TEXT("Nombre de la copia del fichero de una build más nueva"),
		TNSaveLogic::BuildNewerBuildBackupSlotName(TEXT("TN_Settings"), 5), FString(TEXT("TN_Settings_respaldo_v5")));

	// Lo que esta build escribe lleva su versión (el número describe el contenido escrito), aunque el fichero fuese de otra mayor.
	UTN_SettingsSaveGame* Save = NewObject<UTN_SettingsSaveGame>();
	Save->StampCurrentVersion();
	TestEqual(TEXT("Se escribe con la versión de esta build"), Save->Version, TNSaveLogic::SETTINGS_SAVE_VERSION);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSettingsVersionSaveRetryTest,
	"Tortunabo.Settings.Version.SaveRetry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSettingsVersionSaveRetryTest::RunTest(const FString& Parameters)
{
	using namespace TNSaveLogic;

	// Con los valores reales del guardado automático (3 s y tope de 60 s): 3, 6, 12, 24, 48, 60, 60...
	const TArray<double> Expected = { 3.0, 6.0, 12.0, 24.0, 48.0, 60.0, 60.0, 60.0 };
	for (int32 N = 0; N < Expected.Num(); ++N)
	{
		TestEqual(*FString::Printf(TEXT("Tras %d fallos seguidos espera %.0f s"), N, Expected[N]),
			SaveRetryDelay(N, SETTINGS_AUTOSAVE_DELAY, SETTINGS_AUTOSAVE_MAX_DELAY), Expected[N]);
	}

	// Bordes: nunca pasa del tope por muchos fallos que haya (ni tarda en calcularse), ni baja de la base.
	TestEqual(TEXT("Un número enorme de fallos se queda en el tope"),
		SaveRetryDelay(MAX_int32, SETTINGS_AUTOSAVE_DELAY, SETTINGS_AUTOSAVE_MAX_DELAY), SETTINGS_AUTOSAVE_MAX_DELAY);
	TestEqual(TEXT("Un contador negativo cuenta como sin fallos"), SaveRetryDelay(-3, 3.0, 60.0), 3.0);
	TestEqual(TEXT("Un tope menor que la base no la recorta"), SaveRetryDelay(5, 10.0, 4.0), 10.0);
	TestEqual(TEXT("Sin espera base no crece (y no se queda en un bucle)"), SaveRetryDelay(MAX_int32, 0.0, 60.0), 0.0);

	// Un disco que siempre falla, a 60 fotogramas por segundo durante 10 minutos con la condición del Tick
	// (Ahora - DirtySince > espera; al fallar, DirtySince = Ahora): 13 intentos. Antes se intentaba en cada fotograma
	// pasados los 3 s, unas 35 800 veces.
	double Now = 0.0;
	double DirtySince = 0.0;
	int32 Failures = 0;
	int32 Attempts = 0;
	for (int32 Frame = 0; Frame < 10 * 60 * 60; ++Frame)
	{
		Now += 1.0 / 60.0;
		if (Now - DirtySince > SaveRetryDelay(Failures, SETTINGS_AUTOSAVE_DELAY, SETTINGS_AUTOSAVE_MAX_DELAY))
		{
			++Attempts;
			++Failures;
			DirtySince = Now;
		}
	}
	TestEqual(TEXT("Intentos en 10 minutos con el disco siempre fallando"), Attempts, 13);
	return true;
}

#endif
