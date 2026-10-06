// Cocinado: todo asset que el código nombra por ruta literal tiene que entrar en el .pak (DirectoriesToAlwaysCook o
// MapsToCook de Config/DefaultGame.ini). Si no, en la build empaquetada LoadObject devuelve nullptr en silencio.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Cook; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Core/TN_CookDecisions.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCookDecisionsTest,
	"Tortunabo.Cook.Decisions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCookDecisionsTest::RunTest(const FString& Parameters)
{
	using namespace TNCookLogic;

	const TArray<FString> Always = { TEXT("/Game/UI"), TEXT("/Game/Blueprints/") };
	const TArray<FString> Maps = { TEXT("/Game/Maps/Lobby/LVL_Menu") };
	const TArray<FString> Never = { TEXT("/Game/Blueprints/_Viejo") };

	TestEqual(TEXT("Paquete de una ruta de clase"), ToPackageName(TEXT("/Game/A/B.B_C")), FString(TEXT("/Game/A/B")));
	TestTrue(TEXT("Dentro de una carpeta cocinada"), IsCooked(TEXT("/Game/UI/HUD/M_X.M_X"), Always, Maps, Never));
	TestTrue(TEXT("La barra final de la carpeta no importa"), IsCooked(TEXT("/Game/Blueprints/BP.BP_C"), Always, Maps, Never));
	TestFalse(TEXT("Un prefijo no es una carpeta"), IsCooked(TEXT("/Game/UIX/M.M"), Always, Maps, Never));
	TestTrue(TEXT("Un mapa de MapsToCook"), IsCooked(TEXT("/Game/Maps/Lobby/LVL_Menu"), Always, Maps, Never));
	TestFalse(TEXT("Un mapa fuera de MapsToCook"), IsCooked(TEXT("/Game/Maps/Dev/LVL_GaleriaAssets"), Always, Maps, Never));
	TestFalse(TEXT("DirectoriesToNeverCook manda"), IsCooked(TEXT("/Game/Blueprints/_Viejo/BP.BP_C"), Always, Maps, Never));
	TestFalse(TEXT("Una carpeta sin cocinar"), IsCooked(TEXT("/Game/Cosmetics/Materials/M.M"), Always, Maps, Never));
	return true;
}

namespace
{
	const TCHAR* TN_PACKAGING_SECTION = TEXT("/Script/UnrealEd.ProjectPackagingSettings");

	/** Lee una lista de structs del ini de empaquetado y devuelve el valor entre comillas: (Path="/Game/UI") → /Game/UI. */
	TArray<FString> TNReadPackagingPaths(const TCHAR* Key)
	{
		TArray<FString> Raw;
		GConfig->GetArray(TN_PACKAGING_SECTION, Key, Raw, GGameIni);
		TArray<FString> Paths;
		for (const FString& Entry : Raw)
		{
			const int32 First = Entry.Find(TEXT("\""));
			const int32 Last = Entry.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (First != INDEX_NONE && Last > First)
			{
				Paths.Add(Entry.Mid(First + 1, Last - First - 1));
			}
		}
		return Paths;
	}

	/** Rutas literales «/Game/...» de los .h y .cpp del módulo, sin los tests (que usan rutas inventadas). */
	TMap<FString, FString> TNCollectLiteralAssetPaths()
	{
		const FString ModuleDir = FPaths::Combine(FPaths::GameSourceDir(), TEXT("Tortunabo"));
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *ModuleDir, TEXT("*.cpp"), true, false, false);
		IFileManager::Get().FindFilesRecursive(Files, *ModuleDir, TEXT("*.h"), true, false, false);

		const FRegexPattern Pattern(TEXT("\"(/Game/[^\"%\\s]+)\""));
		TMap<FString, FString> PathToFile;
		for (const FString& File : Files)
		{
			if (File.Contains(TEXT("/Private/Tests/")))
			{
				continue;
			}
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *File))
			{
				continue;
			}
			FRegexMatcher Matcher(Pattern, Text);
			while (Matcher.FindNext())
			{
				const FString Path = Matcher.GetCaptureGroup(1);
				if (!Path.EndsWith(TEXT("/")))
				{
					PathToFile.FindOrAdd(Path, FPaths::GetCleanFilename(File));
				}
			}
		}
		return PathToFile;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCookStringPathsTest,
	"Tortunabo.Cook.StringPathsAreCooked",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCookStringPathsTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Always = TNReadPackagingPaths(TEXT("DirectoriesToAlwaysCook"));
	const TArray<FString> Maps = TNReadPackagingPaths(TEXT("MapsToCook"));
	const TArray<FString> Never = TNReadPackagingPaths(TEXT("DirectoriesToNeverCook"));
	TestTrue(TEXT("Hay DirectoriesToAlwaysCook en DefaultGame.ini"), Always.Num() > 0);

	const TMap<FString, FString> Literals = TNCollectLiteralAssetPaths();
	TestTrue(TEXT("Se encuentran rutas literales en Source/Tortunabo"), Literals.Num() > 0);

	for (const TPair<FString, FString>& Pair : Literals)
	{
		if (!TNCookLogic::IsCooked(Pair.Key, Always, Maps, Never))
		{
			AddError(FString::Printf(TEXT("%s (%s) no se cocina: añadir su carpeta a DirectoriesToAlwaysCook."),
				*Pair.Key, *Pair.Value));
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
