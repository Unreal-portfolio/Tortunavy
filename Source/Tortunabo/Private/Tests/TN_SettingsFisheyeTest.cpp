// Ojo de pez leve apagado de serie (#634): un jugador nuevo lo tiene apagado, encenderlo en Ajustes se guarda y los perfiles
// guardados con el valor de serie anterior (encendido) se siguen cargando sin perder nada. Se guarda y se carga con las
// mismas funciones que usa UTN_GameSettingsSubsystem (UGameplayStatics, la ranura TN_Settings), pero en memoria.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Settings.Fisheye; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Settings/TN_SettingsMigration.h"
#include "Settings/TN_SettingsSaveGame.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNSettingsFisheyeTest
{
	/** Guarda y vuelve a cargar los ajustes como hace el juego (cabecera de UGameplayStatics y propiedades con delta). */
	bool RoundTrip(const FTNGameSettings& In, FTNGameSettings& Out, TArray<uint8>* OutBytes = nullptr)
	{
		UTN_SettingsSaveGame* Save = NewObject<UTN_SettingsSaveGame>();
		Save->Settings = In;
		Save->StampCurrentVersion();
		TArray<uint8> Bytes;
		if (!UGameplayStatics::SaveGameToMemory(Save, Bytes))
		{
			return false;
		}
		const UTN_SettingsSaveGame* Loaded = Cast<UTN_SettingsSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
		if (!Loaded)
		{
			return false;
		}
		Out = Loaded->Settings;
		if (OutBytes)
		{
			*OutBytes = MoveTemp(Bytes);
		}
		return true;
	}

	/** true si el nombre de la propiedad aparece escrito en el fichero (el proxy de nombres los escribe como texto). */
	bool MentionsProperty(const TArray<uint8>& Bytes, const char* Name)
	{
		const int32 Length = FCStringAnsi::Strlen(Name);
		for (int32 Index = 0; Index + Length <= Bytes.Num(); ++Index)
		{
			if (FMemory::Memcmp(Bytes.GetData() + Index, Name, Length) == 0)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Un perfil escrito por una build anterior con todas sus propiedades (sin delta), como el que trae el ojo de pez que el
	 * jugador cambió a mano. Se escribe y se lee como UGameplayStatics (proxy de nombres como texto, sin ArIsSaveGame), pero
	 * sin delta: todas las propiedades, también las que valen lo de serie.
	 */
	FTNGameSettings LoadFullyWritten(const FTNGameSettings& Written)
	{
		UTN_SettingsSaveGame* Source = NewObject<UTN_SettingsSaveGame>();
		Source->Settings = Written;
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		FObjectAndNameAsStringProxyArchive WriteAr(Writer, false);
		WriteAr.ArNoDelta = true;
		Source->Serialize(WriteAr);

		UTN_SettingsSaveGame* Target = NewObject<UTN_SettingsSaveGame>();
		FMemoryReader Reader(Bytes, true);
		FObjectAndNameAsStringProxyArchive ReadAr(Reader, true);
		Target->Serialize(ReadAr);
		return Target->Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSettingsFisheyeDefaultTest,
	"Tortunabo.Settings.Fisheye.DefaultOff",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSettingsFisheyeDefaultTest::RunTest(const FString& Parameters)
{
	using namespace TNSettingsFisheyeTest;

	TestFalse(TEXT("De serie, el ojo de pez está apagado"), FTNGameSettings().bFisheye);

	// Un jugador nuevo: el valor de serie no se escribe en el fichero y al cargar sigue apagado.
	FTNGameSettings Loaded;
	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("Se guarda y se carga el perfil de serie"), RoundTrip(FTNGameSettings(), Loaded, &Bytes)))
	{
		return false;
	}
	TestFalse(TEXT("Perfil de serie: apagado al cargar"), Loaded.bFisheye);
	TestFalse(TEXT("Perfil de serie: el valor de serie no se escribe (solo lo que difiere)"), MentionsProperty(Bytes, "bFisheye"));

	// Encendido en Ajustes: se escribe y vuelve encendido.
	FTNGameSettings On;
	On.bFisheye = true;
	if (!TestTrue(TEXT("Se guarda y se carga el perfil con el ojo de pez encendido"), RoundTrip(On, Loaded, &Bytes)))
	{
		return false;
	}
	TestTrue(TEXT("Encendido en Ajustes: se guarda en el fichero"), MentionsProperty(Bytes, "bFisheye"));
	TestTrue(TEXT("Encendido en Ajustes: vuelve encendido al cargar"), Loaded.bFisheye);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSettingsFisheyeOldProfileTest,
	"Tortunabo.Settings.Fisheye.OldProfiles",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSettingsFisheyeOldProfileTest::RunTest(const FString& Parameters)
{
	using namespace TNSettingsFisheyeTest;

	// Perfil de antes en el que el jugador lo había apagado (lo único que se escribía con el valor de serie encendido):
	// se carga apagado y con el resto de lo elegido.
	FTNGameSettings OldOff;
	OldOff.bFisheye = false;
	OldOff.MusicVolume = 0.4f;
	OldOff.Language = TEXT("en");
	OldOff.KeyOverrides.Add(TEXT("IA_Jump#0"), TEXT("SpaceBar"));
	const FTNGameSettings FromOldOff = LoadFullyWritten(OldOff);
	TestFalse(TEXT("Perfil viejo apagado a mano: sigue apagado"), FromOldOff.bFisheye);
	TestEqual(TEXT("Perfil viejo: conserva el volumen"), FromOldOff.MusicVolume, 0.4f);
	TestEqual(TEXT("Perfil viejo: conserva el idioma"), FromOldOff.Language, FString(TEXT("en")));
	TestEqual(TEXT("Perfil viejo: conserva las teclas"), FromOldOff.KeyOverrides.Num(), 1);

	// Perfil escrito con el ojo de pez encendido (con todas sus propiedades): se respeta.
	FTNGameSettings OldOn;
	OldOn.bFisheye = true;
	TestTrue(TEXT("Perfil con el ojo de pez escrito encendido: sigue encendido"), LoadFullyWritten(OldOn).bFisheye);

	// Un perfil de la versión 3 que nunca lo tocó no lo trae: la migración no lo fuerza y queda el de serie (apagado).
	FTNGameSettings Untouched;
	TestTrue(TEXT("La migración de un perfil sin número no falla"),
		TNSettingsMigration::Migrate(Untouched, 0) == TNSaveLogic::EMigration::Upgrade);
	TestFalse(TEXT("Perfil que nunca lo tocó: apagado (el de serie)"), Untouched.bFisheye);
	return true;
}

#endif
