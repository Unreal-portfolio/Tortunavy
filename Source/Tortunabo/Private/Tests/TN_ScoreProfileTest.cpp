// Conchas al perfil (tienda) en partidas de varias rondas (#567). Se recorre lo que hace cada máquina con su jugadora local,
// con las funciones de TN_ScoreDecisions.h que usan ATN_CoopGameState (Results: PersistLocalPlayerScoreIfResults,
// BroadcastFlowStateChange y OnRep_MatchFlowState) y ATN_CoopPlayerState (ronda cerrada: BankRoundScoreToProfile y
// ClientBankRoundScore), también con RaceScore y MatchFlowState llegando en distinto orden a un cliente.
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

		/** ClientBankRoundScore con el valor que manda BankRoundScoreToProfile. */
		void BankRound(int32 RoundScore)
		{
			Profile += TNScoreLogic::ComputeRoundBank(RoundScore);
		}
	};

	/** Una ronda que no es la última: se juega, se guarda entera y se reinicia (StartNextRound). */
	void PlayClosedRound(FMachine& M, int32 Shells)
	{
		M.SetRaceScore(Shells);
		M.BankRound(Shells);
		M.SetRaceScore(0);
		M.SetFlow(false);
	}

	/** La última ronda: se juega y llega Results. */
	void PlayFinalRound(FMachine& M, int32 Shells)
	{
		M.SetRaceScore(Shells);
		M.SetFlow(true);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNScoreProfileRoundsTest,
	"Tortunabo.Score.Profile.AllRoundsReachProfile",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNScoreProfileRoundsTest::RunTest(const FString& Parameters)
{
	using namespace TNScoreProfileTest;

	// Tres rondas de carrera con 25, 50 y 25 puntos: suben 100 (antes solo los 25 de la última).
	FMachine Host;
	PlayClosedRound(Host, 25);
	PlayClosedRound(Host, 50);
	PlayFinalRound(Host, 25);
	TestEqual(TEXT("Anfitrión: las tres rondas en el perfil"), Host.Profile, 100);

	// Cliente: el reinicio de RaceScore llega antes que el RPC de la ronda (lleva el valor, así que da igual).
	FMachine Client;
	Client.SetRaceScore(25);
	Client.SetRaceScore(0);
	Client.BankRound(25);
	Client.SetFlow(false);
	PlayClosedRound(Client, 50);
	// Results llega antes que las últimas conchas: se guarda lo visible y el resto al llegar RaceScore.
	Client.SetRaceScore(10);
	Client.SetFlow(true);
	Client.SetRaceScore(25);
	TestEqual(TEXT("Cliente: las tres rondas en el perfil"), Client.Profile, 100);

	TestEqual(TEXT("Una ronda sin conchas no suma"), TNScoreLogic::ComputeRoundBank(0), 0);
	TestEqual(TEXT("Nunca resta"), TNScoreLogic::ComputeRoundBank(-5), 0);
	return true;
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
