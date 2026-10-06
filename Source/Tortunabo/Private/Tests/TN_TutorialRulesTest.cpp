// Reglas del recorrido del tutorial (issue #17): TNTutorialRules (TN_TutorialRules.h), las que usa el servidor en
// ServerGoToStation. Correr desde Session Frontend (categoría "Tortunabo.Tutorial") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Tutorial; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Lobby/TN_TutorialRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTutorialGoToStationTest,
	"Tortunabo.Tutorial.GoToStation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTutorialGoToStationTest::RunTest(const FString& Parameters)
{
	using namespace TNTutorialRules;
	constexpr int32 NumStations = 19;

	// Un cliente cualquiera (sin el salto de pruebas del anfitrión).
	TestFalse(TEXT("Estación negativa: rechazada"), CanGoToStation(-1, NumStations, 5, false));
	TestFalse(TEXT("Estación que no existe: rechazada"), CanGoToStation(NumStations, NumStations, 5, false));
	TestFalse(TEXT("Valor arbitrario enorme: rechazado"), CanGoToStation(2147483647, NumStations, 18, false));
	TestFalse(TEXT("Fuera del tutorial: no entra por aquí"), CanGoToStation(0, NumStations, INDEX_NONE, false));
	TestFalse(TEXT("Adelantarse a una estación no alcanzada: rechazado"), CanGoToStation(12, NumStations, 5, false));
	TestTrue(TEXT("Volver a una ya alcanzada: sí"), CanGoToStation(3, NumStations, 5, false));
	TestTrue(TEXT("Repetir la actual: sí"), CanGoToStation(5, NumStations, 5, false));

	// El anfitrión en una build de pruebas (TN.Tutorial.Station): a cualquiera que exista, aunque no esté dentro.
	TestTrue(TEXT("Anfitrión de pruebas: a la 12 sin haber llegado"), CanGoToStation(11, NumStations, INDEX_NONE, true));
	TestFalse(TEXT("Anfitrión de pruebas: tampoco a una que no existe"), CanGoToStation(NumStations, NumStations, 0, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTutorialResetProgressTest,
	"Tortunabo.Tutorial.ResetProgress",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTutorialResetProgressTest::RunTest(const FString& Parameters)
{
	using namespace TNTutorialRules;

	// Repetir desde dentro (#85): volver a la salida con el tutorial ya empezado avisa al cliente para vaciar su HUD.
	TestTrue(TEXT("Dentro y de vuelta a la salida: el HUD empieza de cero"), ShouldResetProgress(true, 0));

	// Quien entra de nuevas no necesita el aviso: su bInTutorial cambia y su máquina se reinicia sola.
	TestFalse(TEXT("Entrando de nuevas por la salida: sin aviso"), ShouldResetProgress(false, 0));
	TestFalse(TEXT("Entrando de nuevas por una estación: sin aviso"), ShouldResetProgress(false, 7));

	// Volver a una estación ya pisada no es repetir: las tareas de las anteriores siguen hechas.
	TestFalse(TEXT("Dentro y a la estación 2: se conserva lo hecho"), ShouldResetProgress(true, 1));
	TestFalse(TEXT("Dentro y a la última: se conserva lo hecho"), ShouldResetProgress(true, 18));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTutorialCheckpointStationTest,
	"Tortunabo.Tutorial.CheckpointStation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTutorialCheckpointStationTest::RunTest(const FString& Parameters)
{
	using namespace TNTutorialRules;
	constexpr int32 Catapult = 11;
	TestEqual(TEXT("Antes del cañón, cada punto es su estación"), StationOfCheckpoint(4, Catapult), 4);
	TestEqual(TEXT("La catapulta"), StationOfCheckpoint(Catapult, Catapult), Catapult);
	TestEqual(TEXT("El punto de pasado el cañón sigue siendo de la catapulta"), StationOfCheckpoint(Catapult + 1, Catapult), Catapult);
	TestEqual(TEXT("Después, uno menos (nadar)"), StationOfCheckpoint(Catapult + 2, Catapult), Catapult + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTutorialVRInputTest,
	"Tortunabo.Tutorial.VRInput",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTutorialVRInputTest::RunTest(const FString& Parameters)
{
	using namespace TNTutorialRules;

	// Gatillos y agarres de los Touch (#645): por su valor, con el mismo umbral que el resto de la VR.
	TestFalse(TEXT("Gatillo sin tocar"), VRButtonDown(0.f, false));
	TestFalse(TEXT("Gatillo a medias (por debajo del 55 %)"), VRButtonDown(0.4f, false));
	TestTrue(TEXT("Gatillo a fondo"), VRButtonDown(1.f, false));
	TestTrue(TEXT("Gatillo justo en el umbral"), VRButtonDown(TNVRMath::AnalogPressThreshold, false));
	TestTrue(TEXT("Con el clic, aunque el valor no llegue"), VRButtonDown(0.f, true));

	// Stick izquierdo: andar y liberarse de Berta.
	TestFalse(TEXT("Stick en reposo"), VRStickMoved(0.f, 0.f));
	TestFalse(TEXT("Stick con un roce dentro de la zona muerta"), VRStickMoved(0.2f, -0.1f));
	TestTrue(TEXT("Stick hacia delante"), VRStickMoved(0.f, 0.9f));
	TestTrue(TEXT("Stick hacia la izquierda"), VRStickMoved(-0.8f, 0.f));

	// Soltar con el agarre: el objeto sale de la aleta (sin ir al caparazón) con el agarre apretado hace poco.
	TestTrue(TEXT("Soltado al abrir el agarre"), VRDropCounts(true, false, false, 0.2f));
	TestTrue(TEXT("Soltado un instante después (el servidor tarda un viaje)"), VRDropCounts(true, false, false, 1.4f));
	TestFalse(TEXT("Salió de la aleta sin tocar el agarre: no cuenta"), VRDropCounts(true, false, false, 30.f));
	TestFalse(TEXT("Lo guardó en el caparazón (X): no es soltar"), VRDropCounts(true, false, true, 0.2f));
	TestFalse(TEXT("Sigue con el objeto: no hay nada soltado"), VRDropCounts(true, true, false, 0.2f));
	TestFalse(TEXT("No llevaba nada: no hay nada que soltar"), VRDropCounts(false, false, false, 0.2f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
