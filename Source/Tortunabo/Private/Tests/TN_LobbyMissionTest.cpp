// Lista de modos del menú, la sala y el general (#632): todos los modos jugables (con el 2 vs 2; sin Karts ni el Rally
// desde #848) y las arenas de Todos contra Todos (Scripts/terrain_volumes/Variants). Correr desde Session Frontend (categoría
// "Tortunabo.Lobby.Mission") o headless con UnrealEditor-Win64-DebugGame-Cmd <uproject>
// -ExecCmds="Automation RunTests Tortunabo.Lobby.Mission; Quit".

#include "Misc/AutomationTest.h"
#include "Internationalization/Text.h"
#include "Lobby/TN_LobbyMission.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNLobbyMissionTest
{
	bool HasMode(ETNProcGameMode Mode)
	{
		for (const ETNProcGameMode MenuMode : TNLobbyMission::MenuModes)
		{
			if (MenuMode == Mode)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNLobbyMissionMenuModesTest, "Tortunabo.Lobby.Mission.MenuModes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNLobbyMissionMenuModesTest::RunTest(const FString& Parameters)
{
	using namespace TNLobbyMission;
	using TNLobbyMissionTest::HasMode;
	TestTrue(TEXT("Cooperativo en el menú"), HasMode(ETNProcGameMode::Coop));
	TestTrue(TEXT("Carrera en el menú"), HasMode(ETNProcGameMode::Race));
	TestTrue(TEXT("Supervivencia en el menú"), HasMode(ETNProcGameMode::Survival));
	TestTrue(TEXT("Todos contra Todos en el menú"), HasMode(ETNProcGameMode::FreeForAll));
	TestTrue(TEXT("2 vs 2 en el menú (#632)"), HasMode(ETNProcGameMode::TwoVsTwo));
	TestFalse(TEXT("Karts fuera del menú (#848)"), HasMode(ETNProcGameMode::Karts));
	TestFalse(TEXT("Rally fuera del menú (#848)"), HasMode(ETNProcGameMode::Rally));
	TestFalse(TEXT("El Clásico sigue solo en los selectores del cuartel"), HasMode(ETNProcGameMode::Classic));
	TestEqual(TEXT("Cinco modos, sin repetidos"), static_cast<int32>(UE_ARRAY_COUNT(MenuModes)), 5);

	TestEqual(TEXT("Una sala de 2 vs 2 se queda en 2 vs 2"), NormalizeMenuMode(ETNProcGameMode::TwoVsTwo), ETNProcGameMode::TwoVsTwo);
	TestEqual(TEXT("Una sala de Karts pasa a Cooperativo"), NormalizeMenuMode(ETNProcGameMode::Karts), ETNProcGameMode::Coop);
	TestEqual(TEXT("Una sala de Rally pasa a Cooperativo"), NormalizeMenuMode(ETNProcGameMode::Rally), ETNProcGameMode::Coop);
	TestEqual(TEXT("El Clásico no se crea desde el menú"), NormalizeMenuMode(ETNProcGameMode::Classic), ETNProcGameMode::Coop);
	TestFalse(TEXT("Karts no se puede jugar"), IsModePlayable(ETNProcGameMode::Karts));
	TestFalse(TEXT("El Rally no se puede jugar"), IsModePlayable(ETNProcGameMode::Rally));
	TestTrue(TEXT("Las demás misiones, solo el modo"),
		MissionTitle(ETNProcGameMode::Coop, NAME_None).EqualTo(ModeName(ETNProcGameMode::Coop)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNLobbyMissionTctArenasTest, "Tortunabo.Lobby.Mission.TctArenas",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNLobbyMissionTctArenasTest::RunTest(const FString& Parameters)
{
	using namespace TNLobbyMission;
	TestTrue(TEXT("Manifest tct con trozos: arena"), IsTctArenaManifest(TEXT("{\"mode\": \"tct\", \"cells\": [{\"file\": \"Chunks/r0c0.bin\"}]}")));
	TestFalse(TEXT("Manifest del Rally: no es arena"), IsTctArenaManifest(TEXT("{\"mode\": \"rally\", \"cells\": [{}]}")));
	TestFalse(TEXT("Manifest tct sin trozos: no es arena"), IsTctArenaManifest(TEXT("{\"mode\": \"tct\", \"cells\": []}")));
	TestFalse(TEXT("Texto roto: no es arena"), IsTctArenaManifest(TEXT("{roto")));

	const TArray<FName> Sorted = SortTctArenas({ FName(TEXT("Z99_nueva")), FName(TEXT("N01_coliseo")), FName(DefaultTctArena),
		FName(TEXT("N01_coliseo")), FName(TEXT("B01_otra")), FName(NAME_None) });
	TestEqual(TEXT("Cuatro arenas sin repetir ni vacías"), Sorted.Num(), 4);
	if (Sorted.Num() == 4)
	{
		TestEqual(TEXT("La de por defecto, la primera"), Sorted[0], FName(DefaultTctArena));
		TestEqual(TEXT("Las conocidas, después"), Sorted[1], FName(TEXT("N01_coliseo")));
		TestEqual(TEXT("Las nuevas, por nombre"), Sorted[2], FName(TEXT("B01_otra")));
		TestEqual(TEXT("Y la última"), Sorted[3], FName(TEXT("Z99_nueva")));
	}
	TestEqual(TEXT("Arena elegida que ya no existe: la primera"), ResolveTctArena(FName(TEXT("Borrada")), Sorted), Sorted[0]);
	TestEqual(TEXT("Sin opciones: ninguna"), ResolveTctArena(FName(DefaultTctArena), TArray<FName>()), FName(NAME_None));
	TestEqual(TEXT("Viaje: LVL_Tct con el modo y la arena"), TctTravelURL(FName(TEXT("N01_coliseo")), TEXT("/Game/Maps/Run/LVL_Tct")),
		FString(TEXT("/Game/Maps/Run/LVL_Tct?game=Tct?Arena=N01_coliseo")));

	// En el repositorio están los manifests: las arenas del director (#651) se ofrecen y ninguna es un mapa de país.
	const TArray<FName>& Options = TctArenaOptions();
	const TCHAR* const Expected[] = { TEXT("A01_diana"), TEXT("A06_panal"), TEXT("A07_panal_piramide"), TEXT("A08_panal_roto"),
		TEXT("A09_colmena"), TEXT("A10_ajedrez"), TEXT("A11_zigurat"), TEXT("A12_damas"), TEXT("N01_coliseo"), TEXT("N02_anfiteatro"),
		TEXT("N03_volcan_arena"), TEXT("N04_atolon"), TEXT("N17_fortaleza_estrella"), TEXT("N18_yin_yang") };
	for (const TCHAR* Arena : Expected)
	{
		TestTrue(FString::Printf(TEXT("%s entre las arenas"), Arena), Options.Contains(FName(Arena)));
		TestFalse(FString::Printf(TEXT("%s con nombre traducido"), Arena), TctArenaName(FName(Arena)).ToString().Equals(Arena));
	}
	TestFalse(TEXT("Sin circuitos del Rally entre las arenas"), Options.Contains(FName(TEXT("R01_circuito_dunas"))));
	TestFalse(TEXT("Sin el camino del cooperativo entre las arenas"), Options.Contains(FName(TEXT("C01_camino"))));
	if (Options.Num() > 0)
	{
		TestEqual(TEXT("Diana, la primera"), Options[0], FName(DefaultTctArena));
	}
	TestTrue(TEXT("Variante nueva: su identificador"), TctArenaName(FName(TEXT("Z99_nueva"))).ToString().Equals(TEXT("Z99_nueva")));

	TestTrue(TEXT("La misión de Todos contra Todos lleva la arena"),
		MissionTitle(ETNProcGameMode::FreeForAll, FName(TEXT("N01_coliseo"))).ToString().Contains(TctArenaName(FName(TEXT("N01_coliseo"))).ToString()));
	TestTrue(TEXT("Todos contra Todos sin arena: solo el modo"),
		MissionTitle(ETNProcGameMode::FreeForAll, NAME_None).EqualTo(ModeName(ETNProcGameMode::FreeForAll)));
	return true;
}

#endif
