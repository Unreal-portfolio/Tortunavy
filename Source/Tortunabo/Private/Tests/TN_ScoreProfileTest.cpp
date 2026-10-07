// Puntos al perfil (tienda) al acabar la partida (#567). Se recorre lo que hace cada máquina con su jugadora local, con
// las funciones de TN_ScoreDecisions.h que usa ATN_CoopGameState (Results: PersistLocalPlayerScoreIfResults,
// BroadcastFlowStateChange y OnRep_MatchFlowState), también con RaceScore y MatchFlowState llegando en distinto orden a
// un cliente.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Score.Profile; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Core/TN_ScoreDecisions.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNScoreProfileTest
{
	/** Una máquina (anfitrión o cliente) vista desde su jugadora local. */
	struct FMachine
	{
		/** AccumulatedRaceScore del perfil local. */
		int32 Profile = 0;
		/** ATN_CoopGameState::PersistedScoreThisRace. */
		int32 Persisted = 0;
		/** MatchFlowState == Results, tal como lo ve esta máquina. */
		bool bResults = false;
		/** RaceScore de su PlayerState, tal como lo ve esta máquina. */
		int32 RaceScore = 0;

		/** PersistLocalPlayerScoreIfResults. */
		void PersistIfResults()
		{
			if (!bResults) { return; }
			const int32 Delta = TNScoreLogic::ComputePersistDelta(RaceScore, Persisted);
			if (Delta > 0)
			{
				Profile += Delta;
				Persisted = RaceScore;
			}
		}

		/** BroadcastFlowStateChange (servidor) y OnRep_MatchFlowState (cliente). */
		void SetFlow(bool bIsResults)
		{
			bResults = bIsResults;
			Persisted = TNScoreLogic::PersistedAfterFlowChange(Persisted, bIsResults);
			PersistIfResults();
		}

		/** OnRep_RaceScore (o AddRaceScore / ResetForNewRace en el servidor). */
		void SetRaceScore(int32 Value)
		{
			RaceScore = Value;
			PersistIfResults();
		}
	};

	/** Una partida: se juega y llega Results. */
	void PlayFinalRound(FMachine& M, int32 Shells)
	{
		M.SetRaceScore(Shells);
		M.SetFlow(true);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNScoreProfilePlayAgainTest,
	"Tortunabo.Score.Profile.PlayAgainOnClient",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNScoreProfilePlayAgainTest::RunTest(const FString& Parameters)
{
	using namespace TNScoreProfileTest;

	// Primera partida de 120 y, tras «Volver a jugar», otra de 100 (menos que la primera): se guardan las dos enteras.
	FMachine Client;
	PlayFinalRound(Client, 120);
	TestEqual(TEXT("Primera partida guardada"), Client.Profile, 120);
	// Volver a jugar: sale de Results y RaceScore vuelve a 0 (en este orden o al revés).
	Client.SetFlow(false);
	Client.SetRaceScore(0);
	PlayFinalRound(Client, 100);
	TestEqual(TEXT("Segunda partida guardada entera (delta > 0)"), Client.Profile, 220);

	// El otro orden: RaceScore a 0 llega aún en Results y después el cambio de estado.
	FMachine Other;
	PlayFinalRound(Other, 120);
	Other.SetRaceScore(0);
	Other.SetFlow(false);
	PlayFinalRound(Other, 100);
	TestEqual(TEXT("Mismo resultado con el orden inverso"), Other.Profile, 220);

	TestEqual(TEXT("En Results lo guardado se mantiene"), TNScoreLogic::PersistedAfterFlowChange(120, true), 120);
	TestEqual(TEXT("Fuera de Results vuelve a 0"), TNScoreLogic::PersistedAfterFlowChange(120, false), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
