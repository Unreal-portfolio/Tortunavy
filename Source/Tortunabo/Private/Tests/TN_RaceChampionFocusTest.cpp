// Pantalla del campeón con mando (#557): el foco empieza en el primer botón activo (Salir en un cliente), salta al
// siguiente si el suyo se apaga, y la B lleva a Salir y desde Salir sale. Se testean las reglas de TNRaceChampionFocus
// (TN_RaceChampionWidget.h), las mismas que usa UTN_RaceChampionWidget.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.UI.RaceChampion; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "UI/Race/TN_RaceChampionWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRaceChampionFocusTest,
	"Tortunabo.UI.RaceChampion.Focus",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRaceChampionFocusTest::RunTest(const FString& Parameters)
{
	using namespace TNRaceChampionFocus;

	// Qué botones se pueden pulsar.
	TestTrue(TEXT("Anfitrión: Volver a jugar activo"), IsEnabled(PlayAgain, true, false));
	TestTrue(TEXT("Anfitrión: Cambiar de modo activo"), IsEnabled(ChangeMode, true, false));
	TestFalse(TEXT("Cliente: Volver a jugar apagado"), IsEnabled(PlayAgain, false, false));
	TestFalse(TEXT("Cliente: Cambiar de modo apagado"), IsEnabled(ChangeMode, false, false));
	TestTrue(TEXT("Cliente: Salir activo"), IsEnabled(Quit, false, false));
	TestFalse(TEXT("Ya elegido: Salir apagado"), IsEnabled(Quit, true, true));
	TestFalse(TEXT("Índice fuera de rango"), IsEnabled(ButtonCount, true, false));

	// Foco al abrir (sin foco en ningún botón).
	TestEqual(TEXT("Anfitrión: empieza en Volver a jugar"), PickFocus(INDEX_NONE, true, false), static_cast<int32>(PlayAgain));
	TestEqual(TEXT("Cliente: empieza en Salir"), PickFocus(INDEX_NONE, false, false), static_cast<int32>(Quit));
	TestEqual(TEXT("Ya elegido: ningún botón"), PickFocus(INDEX_NONE, true, true), static_cast<int32>(INDEX_NONE));

	// El foco se queda donde está mientras el botón siga activo.
	TestEqual(TEXT("Anfitrión en Cambiar de modo: se queda"), PickFocus(ChangeMode, true, false), static_cast<int32>(ChangeMode));
	TestEqual(TEXT("Cliente en Salir: se queda"), PickFocus(Quit, false, false), static_cast<int32>(Quit));

	// Si el botón enfocado se apaga, pasa al siguiente activo.
	TestEqual(TEXT("Volver a jugar se apaga: pasa a Salir"), PickFocus(PlayAgain, false, false), static_cast<int32>(Quit));
	TestEqual(TEXT("Cambiar de modo se apaga: pasa a Salir"), PickFocus(ChangeMode, false, false), static_cast<int32>(Quit));
	TestEqual(TEXT("Todo apagado tras elegir: sin foco nuevo"), PickFocus(PlayAgain, true, true), static_cast<int32>(INDEX_NONE));

	// Atrás (B / Escape).
	TestTrue(TEXT("B en Salir: sale"), OnBack(Quit, false) == EBackAction::Quit);
	TestTrue(TEXT("B en Volver a jugar: lleva a Salir"), OnBack(PlayAgain, false) == EBackAction::FocusQuit);
	TestTrue(TEXT("B sin foco en los botones: lleva a Salir"), OnBack(INDEX_NONE, false) == EBackAction::FocusQuit);
	TestTrue(TEXT("B ya elegido: nada"), OnBack(Quit, true) == EBackAction::None);
	TestTrue(TEXT("La B del mando es atrás"), IsBackKey(EKeys::Gamepad_FaceButton_Right));
	TestTrue(TEXT("Escape es atrás"), IsBackKey(EKeys::Escape));
	TestFalse(TEXT("La A no es atrás"), IsBackKey(EKeys::Gamepad_FaceButton_Bottom));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
