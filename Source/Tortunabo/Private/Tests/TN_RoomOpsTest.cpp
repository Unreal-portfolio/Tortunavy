// Reglas de las operaciones de sala (issue #73): qué búsqueda se queda con el turno cuando hay varias (TNRoomSearchRules) y
// cuándo otro «Crear» o «Unirse» sobra porque ya hay una sala creándose o una entrada en marcha (FTNRoomOpState, TNRoomOpRules),
// las que usa UMP_GameInstance. Correr desde Session Frontend (categoría "Tortunabo.Multiplayer") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Multiplayer.RoomOps; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Multiplayer/TN_RoomTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRoomSearchQueueTest,
	"Tortunabo.Multiplayer.RoomOps.SearchQueue",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRoomSearchQueueTest::RunTest(const FString& Parameters)
{
	using namespace TNRoomSearchRules;

	// Sin nadie esperando, cualquiera ocupa el turno; «ninguna» no es una búsqueda.
	TestTrue(TEXT("Cola vacía: la lista entra"), CanTakeQueue(ETNRoomSearch::None, ETNRoomSearch::List));
	TestTrue(TEXT("Cola vacía: el código entra"), CanTakeQueue(ETNRoomSearch::None, ETNRoomSearch::Code));
	TestTrue(TEXT("Cola vacía: «unirse a la primera» entra"), CanTakeQueue(ETNRoomSearch::None, ETNRoomSearch::QuickJoin));
	TestFalse(TEXT("Cola vacía: «ninguna» no entra"), CanTakeQueue(ETNRoomSearch::None, ETNRoomSearch::None));
	TestFalse(TEXT("Con un código esperando, «ninguna» no lo borra"), CanTakeQueue(ETNRoomSearch::Code, ETNRoomSearch::None));

	// El fallo del issue: la lista (que se repite sola) no pisa un código que espera respuesta ni un «unirse a la primera».
	TestFalse(TEXT("La lista no pisa un código en cola"), CanTakeQueue(ETNRoomSearch::Code, ETNRoomSearch::List));
	TestFalse(TEXT("La lista no pisa «unirse a la primera» en cola"), CanTakeQueue(ETNRoomSearch::QuickJoin, ETNRoomSearch::List));
	TestFalse(TEXT("El código no pisa «unirse a la primera» (su pantalla de carga solo se quita al contestar)"),
		CanTakeQueue(ETNRoomSearch::QuickJoin, ETNRoomSearch::Code));

	// Lo contrario sí: lo más importante toma el turno de lo que valía menos.
	TestTrue(TEXT("El código toma el turno de la lista"), CanTakeQueue(ETNRoomSearch::List, ETNRoomSearch::Code));
	TestTrue(TEXT("«Unirse a la primera» toma el turno de la lista"), CanTakeQueue(ETNRoomSearch::List, ETNRoomSearch::QuickJoin));
	TestTrue(TEXT("«Unirse a la primera» toma el turno de un código"), CanTakeQueue(ETNRoomSearch::Code, ETNRoomSearch::QuickJoin));

	// Igual con igual: la última petición manda (otro código, otra lista).
	TestTrue(TEXT("Otro código sustituye al anterior"), CanTakeQueue(ETNRoomSearch::Code, ETNRoomSearch::Code));
	TestTrue(TEXT("Otra lista sustituye a la anterior"), CanTakeQueue(ETNRoomSearch::List, ETNRoomSearch::List));

	// Un orden total y estable: ninguna prioridad repetida entre propósitos distintos.
	TestTrue(TEXT("Orden: «unirse a la primera» > código > lista > ninguna"),
		Priority(ETNRoomSearch::QuickJoin) > Priority(ETNRoomSearch::Code)
		&& Priority(ETNRoomSearch::Code) > Priority(ETNRoomSearch::List)
		&& Priority(ETNRoomSearch::List) > Priority(ETNRoomSearch::None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRoomOpBusyTest,
	"Tortunabo.Multiplayer.RoomOps.Busy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRoomOpBusyTest::RunTest(const FString& Parameters)
{
	FTNRoomOpState State;
	TestFalse(TEXT("Sin nada en marcha, no está ocupado"), State.IsBusy());
	TestFalse(TEXT("Sin nada en marcha, no espera a Steam"), State.IsWaitingOnline());

	// Cada fase por separado cuenta como ocupada: cerrar la vieja (para crear o para entrar), crear, entrar y viajar.
	State = FTNRoomOpState();
	State.bHostAfterDestroy = true;
	TestTrue(TEXT("Cerrando la sesión vieja para crear: ocupado"), State.IsBusy());
	TestTrue(TEXT("...y espera a Steam"), State.IsWaitingOnline());

	State = FTNRoomOpState();
	State.bJoinAfterDestroy = true;
	TestTrue(TEXT("Cerrando la sesión vieja para entrar: ocupado"), State.IsBusy());

	State = FTNRoomOpState();
	State.bCreating = true;
	TestTrue(TEXT("Sesión en Creating: ocupado (el segundo «Crear» no la destruye)"), State.IsBusy());
	TestTrue(TEXT("...y espera a Steam"), State.IsWaitingOnline());

	State = FTNRoomOpState();
	State.bJoining = true;
	TestTrue(TEXT("Entrando en una sala: ocupado"), State.IsBusy());
	TestTrue(TEXT("...y espera a Steam"), State.IsWaitingOnline());

	State = FTNRoomOpState();
	State.bTravelling = true;
	TestTrue(TEXT("Sesión creada o unida, falta el viaje (Pending): ocupado"), State.IsBusy());
	TestFalse(TEXT("...pero ya no espera a Steam"), State.IsWaitingOnline());

	// Al terminar todo vuelve a quedar libre.
	State = FTNRoomOpState();
	TestFalse(TEXT("Restablecido: libre otra vez"), State.IsBusy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRoomOpTimeoutTest,
	"Tortunabo.Multiplayer.RoomOps.Timeout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRoomOpTimeoutTest::RunTest(const FString& Parameters)
{
	using namespace TNRoomOpRules;

	// El plazo de Steam es más corto que el del viaje, y los dos son plazos de verdad (no cero ni absurdos).
	TestTrue(TEXT("Plazos razonables"), OnlineTimeoutSeconds >= 10.0 && TravelTimeoutSeconds > OnlineTimeoutSeconds && TravelTimeoutSeconds <= 300.0);

	// Sin nada en marcha no hay nada que dar por perdido, por mucho tiempo que pase.
	const FTNRoomOpState Idle;
	TestFalse(TEXT("Libre: nunca caduca"), HasTimedOut(Idle, 1.0e6));

	FTNRoomOpState Creating;
	Creating.bCreating = true;
	TestFalse(TEXT("Creando, justo dentro del plazo"), HasTimedOut(Creating, OnlineTimeoutSeconds - 0.1));
	TestTrue(TEXT("Creando, pasado el plazo de Steam"), HasTimedOut(Creating, OnlineTimeoutSeconds + 0.1));

	FTNRoomOpState Joining;
	Joining.bJoining = true;
	TestFalse(TEXT("Entrando, justo dentro del plazo"), HasTimedOut(Joining, OnlineTimeoutSeconds - 0.1));
	TestTrue(TEXT("Entrando, pasado el plazo de Steam"), HasTimedOut(Joining, OnlineTimeoutSeconds + 0.1));

	FTNRoomOpState Closing;
	Closing.bHostAfterDestroy = true;
	TestTrue(TEXT("Cerrando la vieja y sin contestar: caduca"), HasTimedOut(Closing, OnlineTimeoutSeconds + 0.1));

	// El viaje tiene el plazo largo: la conexión tiene sus propios plazos en el motor.
	FTNRoomOpState Travelling;
	Travelling.bTravelling = true;
	TestFalse(TEXT("Viajando, pasado el plazo de Steam pero dentro del del viaje"), HasTimedOut(Travelling, OnlineTimeoutSeconds + 1.0));
	TestTrue(TEXT("Viajando, pasado el plazo del viaje"), HasTimedOut(Travelling, TravelTimeoutSeconds + 0.1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRoomModeSchemaTest,
	"Tortunabo.Rooms.ModeSchema",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRoomModeSchemaTest::RunTest(const FString& Parameters)
{
	using namespace TNRoomKeys;
	// La numeración que anuncian las salas no se mueve: un cambio aquí rompe la lista con otras versiones.
	TestEqual(TEXT("Coop = 0"), static_cast<int32>(ETNProcGameMode::Coop), 0);
	TestEqual(TEXT("Race = 1"), static_cast<int32>(ETNProcGameMode::Race), 1);
	TestEqual(TEXT("TwoVsTwo = 2"), static_cast<int32>(ETNProcGameMode::TwoVsTwo), 2);
	TestEqual(TEXT("Classic = 3"), static_cast<int32>(ETNProcGameMode::Classic), 3);
	TestEqual(TEXT("Survival = 4"), static_cast<int32>(ETNProcGameMode::Survival), 4);
	TestEqual(TEXT("Karts = 5"), static_cast<int32>(ETNProcGameMode::Karts), 5);
	TestEqual(TEXT("FreeForAll = 6"), static_cast<int32>(ETNProcGameMode::FreeForAll), 6);
	TestEqual(TEXT("Rally = 7"), static_cast<int32>(ETNProcGameMode::Rally), 7);

	// Esquema vigente: cada número es su modo.
	TestTrue(TEXT("Esquema vigente: 5 es Karts"), DecodeMode(true, 5, ModeSchemaVersion) == ETNProcGameMode::Karts);
	TestTrue(TEXT("Esquema vigente: 6 es Todos contra Todos"), DecodeMode(true, 6, ModeSchemaVersion) == ETNProcGameMode::FreeForAll);
	TestTrue(TEXT("Esquema vigente: 7 es Rally"), DecodeMode(true, 7, ModeSchemaVersion) == ETNProcGameMode::Rally);
	// Sala de una versión sin esquema: la parte común vale; lo demás no se interpreta (sale cooperativo).
	TestTrue(TEXT("Sin esquema: 4 sigue siendo Supervivencia"), DecodeMode(true, 4, 1) == ETNProcGameMode::Survival);
	TestTrue(TEXT("Sin esquema: un 5 no se lee como Karts"), DecodeMode(true, 5, 1) == ETNProcGameMode::Coop);
	// Datos rotos o ausentes.
	TestTrue(TEXT("Sin modo: cooperativo"), DecodeMode(false, 3, ModeSchemaVersion) == ETNProcGameMode::Coop);
	TestTrue(TEXT("Negativo: cooperativo"), DecodeMode(true, -1, ModeSchemaVersion) == ETNProcGameMode::Coop);
	TestTrue(TEXT("Fuera de rango: cooperativo"), DecodeMode(true, static_cast<int32>(ETNProcGameMode::Count), ModeSchemaVersion) == ETNProcGameMode::Coop);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
