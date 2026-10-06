// Regla del arranque de la partida (TN_MatchStartRules.h): la partida empieza una sola vez. Sin mundo ni actores.
// Correr desde Session Frontend (categoría "Tortunabo.MatchStart") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.MatchStart; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Game/TN_MatchStartRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Reproduce los pasos de ATN_RunGameMode sobre la regla y cuenta cuántas veces arranca la partida. */
	struct FTNMatchStartSim
	{
		FTNMatchStartState State;
		int32 Starts = 0;

		/** TryStartMatch (PostLogin, PostSeamlessTravel, Logout en espera y final del BeginPlay). */
		void TryStart(int32 Connected, int32 Expected)
		{
			if (TNMatchStartLogic::ShouldStart(State, Connected, Expected))
			{
				State = TNMatchStartLogic::MarkStarted(State);
				++Starts;
			}
		}

		/** BeginPlay del GameMode: abre la espera e intenta arrancar. */
		void BeginPlay(int32 Connected, int32 Expected)
		{
			State = TNMatchStartLogic::BeginStaging(State);
			TryStart(Connected, Expected);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMatchStartOnceTest,
	"Tortunabo.MatchStart.StartsOnce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMatchStartOnceTest::RunTest(const FString& Parameters)
{
	// Orden de UEngine::LoadMap al abrir LVL_Demo01 directamente (#362): PostLogin del host y luego BeginPlay.
	FTNMatchStartSim Host;
	Host.TryStart(1, 1);
	TestEqual(TEXT("El PostLogin anterior al BeginPlay no arranca"), Host.Starts, 0);
	Host.BeginPlay(1, 1);
	TestEqual(TEXT("El BeginPlay arranca con el host solo (1/1)"), Host.Starts, 1);
	Host.TryStart(2, 1);
	Host.TryStart(1, 1);
	TestEqual(TEXT("Un PostLogin o un Logout posterior no vuelve a arrancar"), Host.Starts, 1);
	Host.BeginPlay(1, 1);
	TestEqual(TEXT("Otro BeginPlay no reabre una partida empezada"), Host.Starts, 1);
	TestTrue(TEXT("Sigue empezada"), Host.State.bStarted);

	// Viaje seamless desde el lobby: BeginPlay antes de que lleguen todos.
	FTNMatchStartSim Lobby;
	Lobby.BeginPlay(1, 3);
	TestEqual(TEXT("Con 1 de 3 no arranca"), Lobby.Starts, 0);
	TestTrue(TEXT("Espera jugadores"), TNMatchStartLogic::IsWaitingForPlayers(Lobby.State));
	Lobby.TryStart(2, 3);
	TestEqual(TEXT("Con 2 de 3 no arranca"), Lobby.Starts, 0);
	Lobby.TryStart(3, 3);
	TestEqual(TEXT("Con 3 de 3 arranca"), Lobby.Starts, 1);
	TestFalse(TEXT("Ya no espera jugadores"), TNMatchStartLogic::IsWaitingForPlayers(Lobby.State));
	Lobby.TryStart(4, 3);
	TestEqual(TEXT("Un jugador tardío no la vuelve a arrancar"), Lobby.Starts, 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
