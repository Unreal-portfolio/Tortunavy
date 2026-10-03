// Segunda línea de cada jugador en el menú de pausa (issue #256): TNPlayerRowRules (UI/Pause/TN_PlayerRowRules.h), la regla
// con la que UTN_PauseMenuWidget decide entre «Tú», «Tú · anfitrión», «Anfitrión» y un ping. Correr desde Session Frontend
// (categoría "Tortunabo.UI.PlayerRow") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.UI.PlayerRow; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "UI/Pause/TN_PlayerRowRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPlayerRowSubTest,
	"Tortunabo.UI.PlayerRow.Sub",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPlayerRowSubTest::RunTest(const FString& Parameters)
{
	using namespace TNPlayerRowRules;

	// El fallo: un invitado veía el ping de los demás pero en su propia fila solo «Tú».
	TestTrue(TEXT("Invitado, su fila: «Tú» con su ping"), Decide(true, false, true) == ESub::YouPing);
	TestTrue(TEXT("...y esa línea lleva ping"), ShowsPing(Decide(true, false, true)));

	// Lo que ya iba bien y no cambia.
	TestTrue(TEXT("Anfitrión, su fila: «Tú · anfitrión» (contra nadie tiene ping)"), Decide(true, true, false) == ESub::YouHost);
	TestFalse(TEXT("...y no lleva ping"), ShowsPing(Decide(true, true, false)));
	TestTrue(TEXT("Partida local, su fila: solo «Tú» (no hay servidor al que medir)"), Decide(true, false, false) == ESub::You);
	TestFalse(TEXT("...y no lleva ping"), ShowsPing(Decide(true, false, false)));
	TestTrue(TEXT("Un invitado ve al anfitrión como «Anfitrión»"), Decide(false, true, true) == ESub::Host);
	TestTrue(TEXT("El anfitrión ve a un invitado con su ping"), Decide(false, false, false) == ESub::Ping);
	TestTrue(TEXT("Un invitado ve a otro invitado con su ping"), Decide(false, false, true) == ESub::Ping);
	TestTrue(TEXT("...y esa línea lleva ping"), ShowsPing(Decide(false, false, true)));
	TestFalse(TEXT("«Anfitrión» no lleva ping"), ShowsPing(ESub::Host));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
