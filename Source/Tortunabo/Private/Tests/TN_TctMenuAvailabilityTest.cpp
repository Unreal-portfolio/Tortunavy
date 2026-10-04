// Todos contra Todos solo se ofrece con su arena (#651): la arena se lee de Scripts/terrain_volumes/Variants, que no se
// empaqueta. Sin ella, ni el menú ni el selector del lobby lo dejan elegir (TNLobbyMission).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Tct; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Game/TN_TctGameMode.h"
#include "Lobby/TN_LobbyMission.h"
#include "World/TN_TctArena.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctMenuAvailabilityTest,
	"Tortunabo.Tct.MenuAvailability",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctMenuAvailabilityTest::RunTest(const FString& Parameters)
{
	// Con la arena, los cuatro modos del menú en su orden.
	const TArray<ETNProcGameMode> WithArena = TNLobbyMission::FilterMenuModes(true);
	TestEqual(TEXT("Con arena: los cuatro modos"), WithArena.Num(), static_cast<int32>(UE_ARRAY_COUNT(TNLobbyMission::MenuModes)));
	TestTrue(TEXT("Con arena: Todos contra Todos se ofrece"), WithArena.Contains(ETNProcGameMode::FreeForAll));

	// Sin la arena (build cocinada), Todos contra Todos no sale y el resto mantiene su orden.
	const TArray<ETNProcGameMode> WithoutArena = TNLobbyMission::FilterMenuModes(false);
	TestFalse(TEXT("Sin arena: Todos contra Todos no se ofrece"), WithoutArena.Contains(ETNProcGameMode::FreeForAll));
	const TArray<ETNProcGameMode> Expected = { ETNProcGameMode::Coop, ETNProcGameMode::Race, ETNProcGameMode::Survival };
	TestTrue(TEXT("Sin arena: Cooperativo, Carrera y Supervivencia en su orden"), WithoutArena == Expected);

	// Selector del lobby viejo: tras Supervivencia, Todos contra Todos solo con arena; sin ella, vuelta al Cooperativo.
	TestTrue(TEXT("Selector con arena"), TNLobbyMission::NextSelectorMode(ETNProcGameMode::Survival, 2, true) == ETNProcGameMode::FreeForAll);
	TestTrue(TEXT("Selector sin arena"), TNLobbyMission::NextSelectorMode(ETNProcGameMode::Survival, 2, false) == ETNProcGameMode::Coop);
	TestTrue(TEXT("Selector sin arena desde Todos contra Todos"),
		TNLobbyMission::NextSelectorMode(ETNProcGameMode::FreeForAll, 2, false) == ETNProcGameMode::Coop);

	// Lo que se ofrece de verdad sigue a la arena por defecto del modo, la misma que exige el lobby para viajar.
	const bool bArena = ATN_TctArena::VariantExists(FName(TEXT("A01_diana")));
	TestEqual(TEXT("HasDefaultArena sigue a A01_diana"), ATN_TctGameMode::HasDefaultArena(), bArena);
	TestEqual(TEXT("IsModePlayable(Todos contra Todos) sigue a la arena"), TNLobbyMission::IsModePlayable(ETNProcGameMode::FreeForAll), bArena);
	TestTrue(TEXT("El cooperativo siempre se puede jugar"), TNLobbyMission::IsModePlayable(ETNProcGameMode::Coop));
	TestTrue(TEXT("GetMenuModes es FilterMenuModes con lo que hay"), TNLobbyMission::GetMenuModes() == TNLobbyMission::FilterMenuModes(bArena));
	TestTrue(TEXT("Sin arena, una sala en Todos contra Todos pasa al cooperativo"),
		TNLobbyMission::NormalizeMenuMode(ETNProcGameMode::FreeForAll) == (bArena ? ETNProcGameMode::FreeForAll : ETNProcGameMode::Coop));
	return true;
}

#endif
