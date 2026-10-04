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
	TestTrue(TEXT("Manifest de Rally con puertas"),
		IsRallyCircuitManifest(TEXT("{\"mode\": \"rally\", \"checkpoints_uu\": [[0,0,0],[100,0,0]]}")));
	TestFalse(TEXT("Sin puertas no es un circuito"), IsRallyCircuitManifest(TEXT("{\"mode\": \"rally\"}")));
	TestFalse(TEXT("Otro modo no es un circuito"),
		IsRallyCircuitManifest(TEXT("{\"mode\": \"coop\", \"checkpoints_uu\": [[0,0,0],[100,0,0]]}")));
	TestFalse(TEXT("JSON roto"), IsRallyCircuitManifest(TEXT("{ no")));

	const TArray<FName> Sorted = SortRallyMaps({ FName(TEXT("Z99_nuevo")), FName(TEXT("I03R_tortuga_magna")), FName(DefaultRallyCircuit),
		FName(TEXT("A01_otro")), FName(DefaultRallyCircuit), FName(NAME_None) });
	TestEqual(TEXT("Cuatro circuitos sin repetir ni vacíos"), Sorted.Num(), 4);
	if (Sorted.Num() == 4)
	{
		TestEqual(TEXT("E01B, el primero"), Sorted[0], FName(DefaultRallyCircuit));
		TestEqual(TEXT("I03R, el segundo"), Sorted[1], FName(TEXT("I03R_tortuga_magna")));
		TestEqual(TEXT("Las variantes nuevas, por nombre"), Sorted[2], FName(TEXT("A01_otro")));
		TestEqual(TEXT("Y la última"), Sorted[3], FName(TEXT("Z99_nuevo")));
	}
	TestEqual(TEXT("Circuito elegido que ya no existe: el primero"), ResolveRallyMap(FName(TEXT("Borrado")), Sorted), Sorted[0]);
	TestEqual(TEXT("Sin opciones: ninguno"), ResolveRallyMap(FName(DefaultRallyCircuit), TArray<FName>()), FName(NAME_None));

	TestEqual(TEXT("Circuito: LVL_Rally con su variante y vuelta al lobby"),
		RallyTravelURL(FName(TEXT("I03R_tortuga_magna")), TEXT("/Game/Maps/Rally/LVL_Rally")),
		FString(TEXT("/Game/Maps/Rally/LVL_Rally?Variant=I03R_tortuga_magna?FromLobby")));

	// En el repositorio están los manifests: E01B e I03R se ofrecen, sin mapa generado (ese es Karts).
	const TArray<FName>& Options = RallyMapOptions();
	TestTrue(TEXT("E01B entre los circuitos"), Options.Contains(FName(DefaultRallyCircuit)));
	TestTrue(TEXT("I03R entre los circuitos"), Options.Contains(FName(TEXT("I03R_tortuga_magna"))));
	TestFalse(TEXT("Sin mapa generado entre los circuitos"), Options.Contains(FName(NAME_None)));
	TestTrue(TEXT("Con circuitos, el Rally se puede jugar"), IsModePlayable(ETNProcGameMode::Rally));

	TestTrue(TEXT("La misión del Rally lleva el circuito"),
		MissionTitle(ETNProcGameMode::Rally, FName(DefaultRallyCircuit)).ToString().Contains(RallyMapName(FName(DefaultRallyCircuit)).ToString()));
	TestTrue(TEXT("Karts, solo el modo"), MissionTitle(ETNProcGameMode::Karts, FName(DefaultRallyCircuit)).EqualTo(ModeName(ETNProcGameMode::Karts)));
	TestTrue(TEXT("Las demás misiones, solo el modo"),
		MissionTitle(ETNProcGameMode::Coop, NAME_None).EqualTo(ModeName(ETNProcGameMode::Coop)));
	return true;
}

#endif
