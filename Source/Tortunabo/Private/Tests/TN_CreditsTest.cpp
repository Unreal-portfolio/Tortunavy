// Créditos (Docs/Creditos.md): el fichero de datos versionado (Content/Credits/Credits.json) se lee entero, tiene las secciones
// que exigen las licencias (equipo, fuentes con la OFL y su texto completo, motor con el aviso de marca de Epic), sus títulos y
// papeles se traducen (claves conocidas de «TNCredits») y un JSON roto o una ruta de licencia fuera de la carpeta se rechazan.
// Correr desde Session Frontend (categoría "Tortunabo.UI.Credits") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.UI.Credits; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UI/Credits/TN_CreditsData.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCreditsFileTest, "Tortunabo.UI.Credits.File",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCreditsFileTest::RunTest(const FString& Parameters)
{
	FTNCreditsData Data;
	FString Error;
	const FString Path = TNCredits::DefaultPath();
	if (!TestTrue(FString::Printf(TEXT("Se lee %s (%s)"), *Path, *Error), TNCredits::LoadFile(Path, Data, Error)))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("Sin avisos al leer (%s)"), *Error), Error.IsEmpty());

	// Equipo: al menos las cuatro personas de la issue #351.
	const FTNCreditsSection* Team = Data.FindSection(TEXT("team"));
	if (TestNotNull(TEXT("Sección «team»"), Team))
	{
		TestTrue(TEXT("El equipo tiene al menos cuatro personas"), Team->Entries.Num() >= 4);
		for (const FString& Who : { FString(TEXT("SkiTemplar")), FString(TEXT("Mokius")), FString(TEXT("Rubi")), FString(TEXT("María")) })
		{
			TestTrue(FString::Printf(TEXT("El equipo cita a %s"), *Who),
				Team->Entries.ContainsByPredicate([&Who](const FTNCreditsEntry& Entry) { return Entry.Name.Contains(Who); }));
		}
	}

	// Fuentes: alguna con la SIL Open Font License y el texto completo de la licencia cargado.
	const FTNCreditsSection* Fonts = Data.FindSection(TEXT("fonts"));
	if (TestNotNull(TEXT("Sección «fonts»"), Fonts))
	{
		TestTrue(TEXT("Alguna fuente con la SIL Open Font License"), Fonts->Entries.ContainsByPredicate(
			[](const FTNCreditsEntry& Entry) { return Entry.License.Contains(TEXT("Open Font License")); }));
		TestTrue(TEXT("Texto completo de la OFL 1.1"), Fonts->LicenseText.Contains(TEXT("SIL OPEN FONT LICENSE Version 1.1")));
		TestFalse(TEXT("Sin «\\r» en el texto de la licencia"), Fonts->LicenseText.Contains(TEXT("\r")));
	}

	// Motor: el aviso de marca registrada de Epic Games.
	const FTNCreditsSection* Engine = Data.FindSection(TEXT("engine"));
	if (TestNotNull(TEXT("Sección «engine»"), Engine))
	{
		TestTrue(TEXT("Aviso de marca de Unreal"), Engine->Notes.ContainsByPredicate([](const FString& Note)
		{
			return Note.Contains(TEXT("Unreal® is a trademark or registered trademark of Epic Games, Inc."));
		}));
	}

	// Todo lo que se traduce tiene su clave: secciones y papeles conocidos, sin líneas vacías.
	for (const FTNCreditsSection& Section : Data.Sections)
	{
		TestTrue(FString::Printf(TEXT("Sección «%s» con título traducido"), *Section.Id), TNCredits::IsKnownSection(Section.Id));
		TestTrue(FString::Printf(TEXT("Sección «%s» con contenido"), *Section.Id), Section.Entries.Num() > 0 || Section.Notes.Num() > 0);
		for (const FTNCreditsEntry& Entry : Section.Entries)
		{
			for (const FString& Role : Entry.Roles)
			{
				TestTrue(FString::Printf(TEXT("Papel «%s» de %s traducido"), *Role, *Entry.Name), TNCredits::IsKnownRole(Role));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCreditsParseTest, "Tortunabo.UI.Credits.Parse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCreditsParseTest::RunTest(const FString& Parameters)
{
	FTNCreditsData Data;
	FString Error;

	TestFalse(TEXT("JSON roto"), TNCredits::ParseJson(TEXT("{ \"sections\": [ "), Data, Error));
	TestFalse(TEXT("JSON roto con motivo"), Error.IsEmpty());
	TestFalse(TEXT("Sin «sections»"), TNCredits::ParseJson(TEXT("{ \"format\": 1 }"), Data, Error));
	TestFalse(TEXT("Ninguna sección válida"), TNCredits::ParseJson(TEXT("{ \"sections\": [ { \"entries\": [] } ] }"), Data, Error));

	// Las líneas sin nombre se saltan con aviso; lo demás se conserva y se recorta.
	const FString Json = TEXT("{ \"sections\": [ { \"id\": \"team\", \"entries\": [ { \"name\": \"  Ana  \", \"roles\": [\"code\", \"\"] },")
		TEXT(" { \"roles\": [\"art\"] } ], \"notes\": [\"Nota\"] } ] }");
	if (TestTrue(TEXT("JSON válido"), TNCredits::ParseJson(Json, Data, Error)))
	{
		TestEqual(TEXT("Una sección"), Data.Sections.Num(), 1);
		TestEqual(TEXT("Una línea (la segunda no tiene nombre)"), Data.Sections[0].Entries.Num(), 1);
		TestEqual(TEXT("Nombre recortado"), Data.Sections[0].Entries[0].Name, FString(TEXT("Ana")));
		TestEqual(TEXT("Papel vacío fuera"), Data.Sections[0].Entries[0].Roles.Num(), 1);
		TestEqual(TEXT("Nota"), Data.Sections[0].Notes.Num(), 1);
		TestTrue(TEXT("Aviso de la línea saltada"), Error.Contains(TEXT("name")));
	}

	// Una licencia fuera de la carpeta de los créditos no se lee.
	const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("TN_CreditsTest"));
	const FString File = FPaths::Combine(Folder, TEXT("Credits.json"));
	const FString Escape = TEXT("{ \"sections\": [ { \"id\": \"fonts\", \"notes\": [\"x\"], \"licenseFile\": \"../../Config/DefaultGame.ini\" } ] }");
	if (TestTrue(TEXT("Se escribe el JSON de prueba"), FFileHelper::SaveStringToFile(Escape, *File)))
	{
		TestTrue(TEXT("Se lee aunque la licencia se rechace"), TNCredits::LoadFile(File, Data, Error));
		TestTrue(TEXT("La licencia de fuera no se carga"), Data.Sections.Num() == 1 && Data.Sections[0].LicenseText.IsEmpty());
		TestTrue(TEXT("Aviso de licencia rechazada"), Error.Contains(TEXT("rechazado")));
	}
	TestFalse(TEXT("Fichero que no existe"), TNCredits::LoadFile(FPaths::Combine(Folder, TEXT("NoExiste.json")), Data, Error));

	// Títulos y papeles: traducidos los conocidos; los desconocidos salen con su clave.
	TestTrue(TEXT("«maps» preparado para la #112"), TNCredits::IsKnownSection(TEXT("maps")));
	TestFalse(TEXT("Sección inventada"), TNCredits::IsKnownSection(TEXT("inventada")));
	TestEqual(TEXT("Título de una sección desconocida"), TNCredits::SectionTitle(TEXT("extra")).ToString(), FString(TEXT("EXTRA")));
	TestEqual(TEXT("Papel desconocido"), TNCredits::RoleName(TEXT("chef")).ToString(), FString(TEXT("chef")));
	TestFalse(TEXT("Título de «team» no vacío"), TNCredits::SectionTitle(TEXT("team")).IsEmpty());
	return true;
}

#endif
