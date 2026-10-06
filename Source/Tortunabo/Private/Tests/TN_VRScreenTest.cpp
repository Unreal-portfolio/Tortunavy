// Adónde vuelve lo que estaba en el panel VR al apagar las gafas a mitad de partida (#639, VR/TN_VRScreenWidget.h): el widget de
// un jugador de la partida local, a su trozo de la pantalla partida; lo de toda la pantalla y todo lo demás, al viewport.
// Correr con:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.VR.Screen; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "VR/TN_VRScreenWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRScreenReleaseTest,
	"Tortunabo.VR.Screen.ReleaseToPlayerScreen",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRScreenReleaseTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("El widget de un jugador de la partida local vuelve a su trozo"), UTN_VRScreenWidget::ReturnsToPlayerScreen(true, true, true));
	TestFalse(TEXT("Lo de toda la pantalla (pausa, carga, velo de la partida local) vuelve al viewport"), UTN_VRScreenWidget::ReturnsToPlayerScreen(false, true, true));
	TestFalse(TEXT("Sin partida local, al viewport de siempre"), UTN_VRScreenWidget::ReturnsToPlayerScreen(true, true, false));
	TestFalse(TEXT("Sin jugador local no hay trozo al que volver"), UTN_VRScreenWidget::ReturnsToPlayerScreen(true, false, true));
	return true;
}

#endif
