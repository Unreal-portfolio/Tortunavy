// Fallo de viaje (N-E): el anfitrión recarga el lobby; si ya está en uno en pie, no viaja; el invitado, al menú; un segundo fallo
// seguido, al menú. También: si el ServerTravel al lobby ha arrancado de verdad (devuelve true aunque no haga nada) y si el
// mundo es el lobby al que se vuelve. Se testean las funciones de TN_TravelFailureDecisions.h que usa UTN_TravelFailureSubsystem.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Net; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Multiplayer/TN_TravelFailureDecisions.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTravelFailureTest,
	"Tortunabo.Net.TravelFailure",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTravelFailureTest::RunTest(const FString& Parameters)
{
	using namespace TNTravel;

	// Argumentos: modo de red, en el menú, lobby en pie, fallos previos.
	TestTrue(TEXT("Anfitrión en partida, o en un lobby que lanzaba la partida → recarga el lobby"),
		DecideTravelFailure(NM_ListenServer, false, false, 0) == ETravelFailureAction::ReturnHostToLobby);
	TestTrue(TEXT("Servidor dedicado → lobby"),
		DecideTravelFailure(NM_DedicatedServer, false, false, 0) == ETravelFailureAction::ReturnHostToLobby);
	TestTrue(TEXT("Invitado → menú"),
		DecideTravelFailure(NM_Client, false, false, 0) == ETravelFailureAction::ReturnToMenu);
	TestTrue(TEXT("Sin red → menú"),
		DecideTravelFailure(NM_Standalone, false, false, 0) == ETravelFailureAction::ReturnToMenu);
	TestTrue(TEXT("Ya en el menú → se queda"),
		DecideTravelFailure(NM_Client, true, false, 0) == ETravelFailureAction::StayInMenu);
	TestTrue(TEXT("Anfitrión con un fallo previo → menú (sin bucle lobby → lobby)"),
		DecideTravelFailure(NM_ListenServer, false, false, 1) == ETravelFailureAction::ReturnToMenu);

	// Un lobby en pie (sin cuenta atrás ni pausa antes de viajar) no se recarga; uno que lanzaba la partida sí (primer caso de arriba).
	TestTrue(TEXT("Anfitrión en un lobby en pie → se queda"),
		DecideTravelFailure(NM_ListenServer, false, true, 0) == ETravelFailureAction::StayInLobby);
	TestTrue(TEXT("Anfitrión en un lobby en pie, con fallos previos → se queda (no hay viaje que repetir)"),
		DecideTravelFailure(NM_ListenServer, false, true, 3) == ETravelFailureAction::StayInLobby);
	TestTrue(TEXT("Servidor dedicado en un lobby en pie → se queda"),
		DecideTravelFailure(NM_DedicatedServer, false, true, 0) == ETravelFailureAction::StayInLobby);
	TestTrue(TEXT("Invitado en un lobby en pie → menú (el invitado no decide)"),
		DecideTravelFailure(NM_Client, false, true, 0) == ETravelFailureAction::ReturnToMenu);
	TestTrue(TEXT("Sin red en un lobby en pie → menú"),
		DecideTravelFailure(NM_Standalone, false, true, 0) == ETravelFailureAction::ReturnToMenu);
	TestTrue(TEXT("El menú manda sobre el lobby"),
		DecideTravelFailure(NM_ListenServer, true, true, 0) == ETravelFailureAction::StayInMenu);

	TestEqual(TEXT("Lobby por defecto"), LobbyTravelURL(FString()), FString(TEXT("/Game/Maps/Lobby/LVL_Lobby")));
	TestEqual(TEXT("Lobby del que se salió"), LobbyTravelURL(TEXT("/Game/Maps/Lobby/LVL_HQ")), FString(TEXT("/Game/Maps/Lobby/LVL_HQ")));

	// ¿El mundo es el lobby al que se vuelve?
	TestTrue(TEXT("LVL_Lobby es el lobby por defecto"), IsLobbyMap(TEXT("LVL_Lobby"), FString()));
	TestTrue(TEXT("LVL_HQ es el lobby del que se salió"), IsLobbyMap(TEXT("LVL_HQ"), TEXT("/Game/Maps/Lobby/LVL_HQ")));
	TestFalse(TEXT("LVL_Lobby no es el lobby si se salió de LVL_HQ"), IsLobbyMap(TEXT("LVL_Lobby"), TEXT("/Game/Maps/Lobby/LVL_HQ")));
	TestFalse(TEXT("Un mapa de partida no es el lobby"), IsLobbyMap(TEXT("LVL_Demo01"), FString()));
	TestFalse(TEXT("El menú no es el lobby"), IsLobbyMap(TEXT("LVL_Menu"), FString()));
	TestFalse(TEXT("Sin nombre de mapa no es el lobby"), IsLobbyMap(FString(), FString()));
	TestTrue(TEXT("Las opciones de la URL no cuentan"), IsLobbyMap(TEXT("LVL_Lobby"), TEXT("/Game/Maps/Lobby/LVL_Lobby?game=")));

	// ¿Ha arrancado el ServerTravel? ServerTravel devuelve true aunque no haga nada (NextURL lleno o viaje sin cortes en marcha).
	TestTrue(TEXT("Viaje sin cortes en marcha → arrancó"), DidTravelStart(true, true, false));
	TestTrue(TEXT("NextURL puesto (viaje duro) → arrancó"), DidTravelStart(true, false, true));
	TestFalse(TEXT("Aceptado pero sin efecto (el fallo dentro de ProcessServerTravel) → no arrancó"), DidTravelStart(true, false, false));
	TestFalse(TEXT("Rechazado por el GameMode → no arrancó"), DidTravelStart(false, false, false));
	TestFalse(TEXT("Rechazado aunque haya otro viaje → no es suyo"), DidTravelStart(false, true, true));

	// El mapa al que va un ServerTravel, para comprobar antes de avisar a los invitados que existe.
	const FURL LobbyURL(nullptr, TEXT("/Game/Maps/Lobby/LVL_Lobby?listen"), TRAVEL_Absolute);
	TestEqual(TEXT("Viaje absoluto: el mapa de la URL, sin opciones"),
		TravelMapPackage(LobbyURL, TEXT("/Game/Maps/TN_Inventado?game=X"), true), FString(TEXT("/Game/Maps/TN_Inventado")));
	TestEqual(TEXT("Viaje relativo con mapa: el de la URL"),
		TravelMapPackage(LobbyURL, TEXT("/Game/Maps/TN_Inventado"), false), FString(TEXT("/Game/Maps/TN_Inventado")));
	TestEqual(TEXT("Viaje relativo sin mapa (?Restart): el mapa en el que se está"),
		TravelMapPackage(LobbyURL, TEXT("?Restart"), false), FString(TEXT("/Game/Maps/Lobby/LVL_Lobby")));

	// Solo se anula el viaje al menú que deja pedido UEngine::HandleDisconnect, no otro.
	TestTrue(TEXT("«?closed» es el viaje del motor tras el fallo"), IsEngineDisconnectTravel(TEXT("?closed")));
	TestFalse(TEXT("Sin viaje pedido no hay nada que anular"), IsEngineDisconnectTravel(FString()));
	TestFalse(TEXT("Un viaje de verdad se respeta"), IsEngineDisconnectTravel(TEXT("/Game/Maps/Lobby/LVL_Menu")));

	TestTrue(TEXT("Cada acción tiene su nombre para el registro"),
		FCString::Strcmp(ActionName(ETravelFailureAction::StayInLobby), ActionName(ETravelFailureAction::StayInMenu)) != 0
		&& FCString::Strcmp(ActionName(ETravelFailureAction::ReturnHostToLobby), ActionName(ETravelFailureAction::ReturnToMenu)) != 0);
	return true;
}

#endif
