// Reglas puras del modo Supervivencia (TN_SurvivalRules.h). Sin mundo ni actores. Correr desde Session Frontend
// (categoría "Tortunabo.Survival") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Survival; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Game/TN_SurvivalRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FTNSurvivalPlayer Alive(int32 Id, bool bFinished = false)
	{
		FTNSurvivalPlayer P;
		P.Id = Id;
		P.bFinishedLevel = bFinished;
		return P;
	}

	FTNSurvivalPlayer Dead(int32 Id, int32 Level, float Remaining, float Time)
	{
		FTNSurvivalPlayer P;
		P.Id = Id;
		P.bAlive = false;
		P.LevelDied = Level;
		P.DeathRemaining = Remaining;
		P.DeathTime = Time;
		return P;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Resultado de un nivel en grupo
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalGroupOutcomeTest,
	"Tortunabo.Survival.GroupOutcome",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalGroupOutcomeTest::RunTest(const FString& Parameters)
{
	using namespace TNSurvivalLogic;

	TestTrue(TEXT("Dos vivos jugando → sigue"),
		DecideLevelOutcome({ Alive(1), Alive(2, true) }, 2).Outcome == ETNSurvivalOutcome::Continue);

	TestTrue(TEXT("Todos los vivos en meta → siguiente nivel"),
		DecideLevelOutcome({ Alive(1, true), Alive(2, true), Dead(3, 1, 500.f, 10.f) }, 3).Outcome == ETNSurvivalOutcome::Advance);

	const FTNSurvivalDecision OneLeft = DecideLevelOutcome({ Alive(1), Dead(2, 1, 500.f, 10.f), Dead(3, 1, 200.f, 12.f) }, 3);
	TestTrue(TEXT("Queda una viva → gana al momento, aunque no haya llegado"), OneLeft.Outcome == ETNSurvivalOutcome::Winner);
	TestEqual(TEXT("Gana la viva"), OneLeft.WinnerId, 1);

	const FTNSurvivalDecision OneFinished = DecideLevelOutcome({ Alive(1, true), Dead(2, 2, 100.f, 30.f) }, 2);
	TestTrue(TEXT("Queda una viva ya en meta → gana (no hay nivel siguiente)"), OneFinished.Outcome == ETNSurvivalOutcome::Winner);
	TestEqual(TEXT("Gana la que llegó"), OneFinished.WinnerId, 1);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Desempate cuando mueren todas en el mismo nivel
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalTieBreakTest,
	"Tortunabo.Survival.TieBreak",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalTieBreakTest::RunTest(const FString& Parameters)
{
	using namespace TNSurvivalLogic;

	const FTNSurvivalDecision Closest = DecideLevelOutcome(
		{ Dead(1, 1, 900.f, 5.f), Dead(2, 3, 800.f, 40.f), Dead(3, 3, 300.f, 38.f) }, 3);
	TestTrue(TEXT("Mueren todas → hay ganadora"), Closest.Outcome == ETNSurvivalOutcome::Winner);
	TestEqual(TEXT("Gana la que murió más cerca de la meta en el último nivel"), Closest.WinnerId, 3);

	const FTNSurvivalDecision SameDistance = DecideLevelOutcome(
		{ Dead(1, 2, 400.f, 20.f), Dead(2, 2, 400.f, 21.f) }, 2);
	TestEqual(TEXT("A igual distancia gana la que aguantó más"), SameDistance.WinnerId, 2);

	const FTNSurvivalDecision HigherLevel = DecideLevelOutcome(
		{ Dead(1, 1, 0.f, 50.f), Dead(2, 2, 5000.f, 60.f) }, 2);
	TestEqual(TEXT("Un nivel más alto pesa más que la distancia"), HigherLevel.WinnerId, 2);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Solitario
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalSoloTest,
	"Tortunabo.Survival.Solo",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalSoloTest::RunTest(const FString& Parameters)
{
	using namespace TNSurvivalLogic;

	TestTrue(TEXT("Solitario vivo → sigue (una sola viva no gana)"),
		DecideLevelOutcome({ Alive(1) }, 1).Outcome == ETNSurvivalOutcome::Continue);
	TestTrue(TEXT("Solitario en meta → siguiente nivel"),
		DecideLevelOutcome({ Alive(1, true) }, 1).Outcome == ETNSurvivalOutcome::Advance);

	const FTNSurvivalDecision Over = DecideLevelOutcome({ Dead(1, 4, 100.f, 90.f) }, 1);
	TestTrue(TEXT("Solitario muerto → fin"), Over.Outcome == ETNSurvivalOutcome::SoloOver);
	TestEqual(TEXT("El resultado es suyo"), Over.WinnerId, 1);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Puestos finales
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalRankTest,
	"Tortunabo.Survival.Rank",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalRankTest::RunTest(const FString& Parameters)
{
	using namespace TNSurvivalLogic;

	const TArray<int32> Ranked = RankPlayers(
		{ Dead(1, 1, 200.f, 10.f), Alive(2), Dead(3, 2, 900.f, 50.f), Dead(4, 2, 100.f, 45.f) }, 2);
	TestEqual(TEXT("Cuatro puestos"), Ranked.Num(), 4);
	if (Ranked.Num() == 4)
	{
		TestEqual(TEXT("1.º la ganadora"), Ranked[0], 2);
		TestEqual(TEXT("2.º la que murió más cerca en el nivel más alto"), Ranked[1], 4);
		TestEqual(TEXT("3.º la otra del nivel 2"), Ranked[2], 3);
		TestEqual(TEXT("4.º la que cayó en el nivel 1"), Ranked[3], 1);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dificultad por nivel
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalDifficultyTest,
	"Tortunabo.Survival.LevelDifficulty",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalDifficultyTest::RunTest(const FString& Parameters)
{
	using namespace TNSurvivalLogic;

	TestEqual(TEXT("Nivel 1 = dificultad 1"), LevelMapDifficulty(1), 1);
	TestEqual(TEXT("Nivel 3 = dificultad 3"), LevelMapDifficulty(3), 3);
	TestEqual(TEXT("Nivel 5 = dificultad 5"), LevelMapDifficulty(5), 5);
	TestEqual(TEXT("Del 5 en adelante, 5"), LevelMapDifficulty(9), 5);
	TestEqual(TEXT("Un nivel no válido cuenta como el 1"), LevelMapDifficulty(0), 1);

	// La dificultad elegida con el general decide dónde se empieza (#730).
	TestEqual(TEXT("Fácil: empieza en la 1"), StartMapDifficulty(ETNProcDifficulty::Easy), 1);
	TestEqual(TEXT("Normal: empieza en la 3"), StartMapDifficulty(ETNProcDifficulty::Normal), 3);
	TestEqual(TEXT("Difícil: empieza en la 5"), StartMapDifficulty(ETNProcDifficulty::Hard), 5);
	TestEqual(TEXT("Normal, nivel 1"), LevelMapDifficulty(1, 3), 3);
	TestEqual(TEXT("Normal, nivel 2"), LevelMapDifficulty(2, 3), 4);
	TestEqual(TEXT("Normal, del nivel 3 en adelante, 5"), LevelMapDifficulty(7, 3), 5);
	TestEqual(TEXT("Difícil: siempre 5"), LevelMapDifficulty(1, 5), 5);
	TestEqual(TEXT("Difícil, nivel 9: 5"), LevelMapDifficulty(9, 5), 5);
	TestEqual(TEXT("Un inicio fuera de rango se acota"), LevelMapDifficulty(1, 9), 5);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
