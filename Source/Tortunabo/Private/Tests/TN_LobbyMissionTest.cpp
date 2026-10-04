// Lista de modos del menú, la sala y el general (#632): todos los modos jugables (Karts y el Rally, cada uno con su nombre,
// y el 2 vs 2) y los circuitos del Rally (Scripts/terrain_volumes/Variants). Correr desde Session Frontend (categoría
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
	TestTrue(TEXT("Karts en el menú"), HasMode(ETNProcGameMode::Karts));
	TestTrue(TEXT("Todos contra Todos en el menú"), HasMode(ETNProcGameMode::FreeForAll));
	TestTrue(TEXT("Rally en el menú (#632)"), HasMode(ETNProcGameMode::Rally));
	TestTrue(TEXT("2 vs 2 en el menú (#632)"), HasMode(ETNProcGameMode::TwoVsTwo));
	TestFalse(TEXT("El Clásico sigue solo en los selectores del cuartel"), HasMode(ETNProcGameMode::Classic));
	TestEqual(TEXT("Siete modos, sin repetidos"), static_cast<int32>(UE_ARRAY_COUNT(MenuModes)), 7);

	TestEqual(TEXT("Una sala de 2 vs 2 se queda en 2 vs 2"), NormalizeMenuMode(ETNProcGameMode::TwoVsTwo), ETNProcGameMode::TwoVsTwo);
	TestEqual(TEXT("Una sala de Karts se queda en Karts"), NormalizeMenuMode(ETNProcGameMode::Karts), ETNProcGameMode::Karts);
	TestEqual(TEXT("Una sala de Rally se queda en Rally (con circuitos)"), NormalizeMenuMode(ETNProcGameMode::Rally),
		IsModePlayable(ETNProcGameMode::Rally) ? ETNProcGameMode::Rally : ETNProcGameMode::Coop);
	TestEqual(TEXT("El Clásico no se crea desde el menú"), NormalizeMenuMode(ETNProcGameMode::Classic), ETNProcGameMode::Coop);
	TestTrue(TEXT("El Rally va al final del enum (el número se guarda en las salas)"),
		static_cast<int32>(ETNProcGameMode::Rally) > static_cast<int32>(ETNProcGameMode::FreeForAll));

	// Karts conserva su nombre (decisión del 04-10 en #627) y el Rally tiene el suyo.
	TestEqual(TEXT("Karts se llama «Karts» (clave)"),
		FTextInspector::GetKey(ModeName(ETNProcGameMode::Karts)).Get(FString()), FString(TEXT("MissionModeKarts")));
	TestEqual(TEXT("El Rally se llama «Rally» (clave)"),
		FTextInspector::GetKey(ModeName(ETNProcGameMode::Rally)).Get(FString()), FString(TEXT("MissionModeRally")));
	TestFalse(TEXT("El Rally tiene explicación propia"), ModeBlurb(ETNProcGameMode::Rally).EqualTo(ModeBlurb(ETNProcGameMode::Karts)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNLobbyMissionRallyMapsTest, "Tortunabo.Lobby.Mission.RallyMaps",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNLobbyMissionRallyMapsTest::RunTest(const FString& Parameters)
{
	using namespace TNLobbyMission;
	TestTrue(TEXT("Manifest de Rally con puertas y del generador de vueltas"), IsRallyCircuitManifest(
		TEXT("{\"mode\": \"rally\", \"checkpoints_uu\": [[0,0,0],[100,0,0]], \"generator\": {\"generator\": \"rally_circuit_vueltas\"}}")));
	TestFalse(TEXT("Sin puertas no es un circuito"),
		IsRallyCircuitManifest(TEXT("{\"mode\": \"rally\", \"generator\": {\"generator\": \"rally_circuit_vueltas\"}}")));
	TestFalse(TEXT("Otro modo no es un circuito"), IsRallyCircuitManifest(
		TEXT("{\"mode\": \"coop\", \"checkpoints_uu\": [[0,0,0],[100,0,0]], \"generator\": {\"generator\": \"rally_circuit_vueltas\"}}")));
	// Caso negativo (#692): un circuito de autor (I03R, I04, I06) no viene del generador de vueltas y no entra en el Rally.
	TestFalse(TEXT("Circuito de autor: fuera del Rally"), IsRallyCircuitManifest(
		TEXT("{\"mode\": \"rally\", \"checkpoints_uu\": [[0,0,0],[100,0,0]], \"generator\": {\"generator\": \"shape_kit\"}}")));
	TestFalse(TEXT("Sin generador: fuera del Rally"),
		IsRallyCircuitManifest(TEXT("{\"mode\": \"rally\", \"checkpoints_uu\": [[0,0,0],[100,0,0]]}")));
	TestFalse(TEXT("JSON roto"), IsRallyCircuitManifest(TEXT("{ no")));

	const TArray<FName> Sorted = SortRallyMaps({ FName(TEXT("Z99_nuevo")), FName(TEXT("R02_circuito_tierra")), FName(DefaultRallyCircuit),
		FName(TEXT("A01_otro")), FName(DefaultRallyCircuit), FName(NAME_None) });
	TestEqual(TEXT("Cuatro circuitos sin repetir ni vacíos"), Sorted.Num(), 4);
	if (Sorted.Num() == 4)
	{
		TestEqual(TEXT("R01, el primero"), Sorted[0], FName(DefaultRallyCircuit));
		TestEqual(TEXT("R02, el segundo"), Sorted[1], FName(TEXT("R02_circuito_tierra")));
		TestEqual(TEXT("Las variantes nuevas, por nombre"), Sorted[2], FName(TEXT("A01_otro")));
		TestEqual(TEXT("Y la última"), Sorted[3], FName(TEXT("Z99_nuevo")));
	}
	TestEqual(TEXT("Circuito elegido que ya no existe: el primero"), ResolveRallyMap(FName(TEXT("Borrado")), Sorted), Sorted[0]);
	TestEqual(TEXT("Sin opciones: ninguno"), ResolveRallyMap(FName(DefaultRallyCircuit), TArray<FName>()), FName(NAME_None));

	TestEqual(TEXT("Circuito: LVL_Rally con su variante y vuelta al lobby"),
		RallyTravelURL(FName(TEXT("R04_circuito_cantera")), TEXT("/Game/Maps/Rally/LVL_Rally")),
		FString(TEXT("/Game/Maps/Rally/LVL_Rally?Variant=R04_circuito_cantera?FromLobby")));

	// En el repositorio están los manifests (#692): solo los circuitos del generador de vueltas, R01 a R06 en su orden y
	// con nombre traducido; ni los de autor (E01B, I03R, I04, I06) ni el mapa generado (ese es Karts).
	const TArray<FName>& Options = RallyMapOptions();
	const TCHAR* const Catalog[] = { TEXT("R01_circuito_dunas"), TEXT("R02_circuito_tierra"), TEXT("R03_circuito_dunas_costeras"),
		TEXT("R04_circuito_cantera"), TEXT("R05_circuito_marismas"), TEXT("R06_circuito_lomas") };
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Catalog)); ++Index)
	{
		const FName Circuit(Catalog[Index]);
		TestTrue(*FString::Printf(TEXT("%s, en el puesto %d del selector"), Catalog[Index], Index),
			Options.IsValidIndex(Index) && Options[Index] == Circuit);
		TestFalse(*FString::Printf(TEXT("%s con nombre traducido, no su identificador"), Catalog[Index]),
			RallyMapName(Circuit).ToString().Equals(Catalog[Index]));
	}
	for (const TCHAR* Retired : { TEXT("E01B_espana_rally"), TEXT("I03R_tortuga_magna"), TEXT("I04_volcan_hueco"), TEXT("I06_feroe") })
	{
		TestFalse(*FString::Printf(TEXT("%s fuera del selector del Rally"), Retired), Options.Contains(FName(Retired)));
	}
	TestFalse(TEXT("Sin mapa generado entre los circuitos"), Options.Contains(FName(NAME_None)));
	TestTrue(TEXT("Con circuitos, el Rally se puede jugar"), IsModePlayable(ETNProcGameMode::Rally));

	TestTrue(TEXT("La misión del Rally lleva el circuito"),
		MissionTitle(ETNProcGameMode::Rally, FName(DefaultRallyCircuit)).ToString().Contains(RallyMapName(FName(DefaultRallyCircuit)).ToString()));
	TestTrue(TEXT("Karts, solo el modo"), MissionTitle(ETNProcGameMode::Karts, FName(DefaultRallyCircuit)).EqualTo(ModeName(ETNProcGameMode::Karts)));
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
	TestFalse(TEXT("Sin circuitos del Rally entre las arenas"), Options.Contains(FName(DefaultRallyCircuit)));
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
