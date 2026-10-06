// Puntuación final del Coop (#789, TN_CoopScore.h): los cuatro términos con sus pesos y los casos extremos (todo / nada),
// la eficiencia de puzle y el texto del desglose de la pantalla de resultados (TN_ResultsTexts.h). Sin mundo: el mismo
// código que usan ATN_ProcMapGameMode y UTN_CoopFlowHUDWidget. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Coop.Score; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Core/TN_CoopScore.h"
#include "UI/HUD/TN_ResultsTexts.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopScoreComputeTest,
	"Tortunabo.Coop.Score.Compute",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopScoreComputeTest::RunTest(const FString& Parameters)
{
	using namespace TNCoopScore;
	TestEqual(TEXT("los pesos suman el máximo"), DollWeight + ShellWeight + FinishWeight + PuzzleWeight, MaxScore);

	FInputs All;
	All.DollsCollected = 3;
	All.DollsTotal = 3;
	All.ShellsCollected = 480;
	All.ShellsTotal = 480;
	All.bFinished = true;
	All.PuzzleEfficiency = 1.f;
	const FTN_CoopScoreBreakdown Top = Compute(All);
	TestTrue(TEXT("calculada"), Top.bValid);
	TestEqual(TEXT("todo: el máximo"), Top.Total, MaxScore);
	TestEqual(TEXT("todo: muñecos enteros"), Top.DollPoints, DollWeight);
	TestEqual(TEXT("todo: meta entera"), Top.FinishPoints, FinishWeight);

	FInputs Nothing = All;
	Nothing.DollsCollected = 0;
	Nothing.ShellsCollected = 0;
	Nothing.bFinished = false;
	Nothing.PuzzleEfficiency = 0.f;
	const FTN_CoopScoreBreakdown Zero = Compute(Nothing);
	TestEqual(TEXT("nada: cero"), Zero.Total, 0);
	TestEqual(TEXT("nada: cero en cada término"), Zero.DollPoints + Zero.ShellPoints + Zero.FinishPoints + Zero.PuzzlePoints, 0);

	// Un término sin nada que medir da sus puntos enteros (el máximo no cambia).
	FInputs NoPuzzles = Nothing;
	NoPuzzles.PuzzleEfficiency = -1.f;
	const FTN_CoopScoreBreakdown Free = Compute(NoPuzzles);
	TestFalse(TEXT("sin puzles: lo dice"), Free.HasPuzzles());
	TestEqual(TEXT("sin puzles: su peso entero"), Free.Total, PuzzleWeight);
	FInputs Empty;
	Empty.PuzzleEfficiency = -1.f;
	Empty.bFinished = true;
	TestEqual(TEXT("nivel sin muñecos, conchas ni puzles y con meta: el máximo"), Compute(Empty).Total, MaxScore);

	// Parciales: se redondea cada término.
	FInputs Part = Nothing;
	Part.DollsCollected = 1;
	Part.ShellsCollected = 240;
	Part.PuzzleEfficiency = 0.5f;
	const FTN_CoopScoreBreakdown Half = Compute(Part);
	TestEqual(TEXT("1 de 3 muñecos"), Half.DollPoints, FMath::RoundToInt32(DollWeight / 3.0));
	TestEqual(TEXT("la mitad de las conchas"), Half.ShellPoints, ShellWeight / 2);
	TestEqual(TEXT("puzle a medias"), Half.PuzzlePoints, PuzzleWeight / 2);
	TestEqual(TEXT("el total es la suma de los términos"), Half.Total, Half.DollPoints + Half.ShellPoints + Half.FinishPoints + Half.PuzzlePoints);

	// Entradas fuera de rango: no pasan del máximo ni bajan de cero.
	FInputs Over = All;
	Over.DollsCollected = 9;
	Over.ShellsCollected = 9999;
	Over.PuzzleEfficiency = 3.f;
	TestEqual(TEXT("más de lo que había: el máximo"), Compute(Over).Total, MaxScore);
	FInputs Negative = Nothing;
	Negative.DollsCollected = -4;
	Negative.ShellsCollected = -10;
	TestEqual(TEXT("negativos: cero"), Compute(Negative).Total, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopScorePuzzleTest,
	"Tortunabo.Coop.Score.Puzzle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopScorePuzzleTest::RunTest(const FString& Parameters)
{
	using namespace TNCoopScore;
	TestTrue(TEXT("sin puzles: negativa"), PuzzleEfficiency({}) < 0.f);

	FPuzzleRun Unsolved;
	Unsolved.ProgressTime = 10.f;
	FPuzzleRun Switch;
	Switch.bSolved = true;
	Switch.SolveTime = 50.f;
	FPuzzleRun Quick;
	Quick.bSolved = true;
	Quick.ProgressTime = 100.f;
	Quick.SolveTime = 100.f + PuzzleParSeconds - 1.f;
	FPuzzleRun Slow;
	Slow.bSolved = true;
	Slow.ProgressTime = 200.f;
	Slow.SolveTime = 200.f + 2.f * PuzzleParSeconds;

	TestEqual(TEXT("sin resolver: 0"), RunEfficiency(Unsolved), 0.f);
	TestEqual(TEXT("interruptor sin avance previo: 1"), RunEfficiency(Switch), 1.f);
	TestEqual(TEXT("dentro del tiempo: 1"), RunEfficiency(Quick), 1.f);
	TestEqual(TEXT("el doble del tiempo: la mitad"), RunEfficiency(Slow), 0.5f, 1e-4f);
	TestEqual(TEXT("todo resuelto a tiempo: 1"), PuzzleEfficiency({ Switch, Quick }), 1.f);
	TestEqual(TEXT("nada resuelto: 0"), PuzzleEfficiency({ Unsolved, Unsolved }), 0.f);
	TestEqual(TEXT("media de los puzles"), PuzzleEfficiency({ Unsolved, Quick, Slow, Switch }), (0.f + 1.f + 0.5f + 1.f) / 4.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopScoreTextTest,
	"Tortunabo.Coop.Score.Text",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopScoreTextTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("sin puntuación: vacío"), TNResultsTexts::CoopScoreBreakdown(FTN_CoopScoreBreakdown()).IsEmpty());

	TNCoopScore::FInputs In;
	In.DollsCollected = 2;
	In.DollsTotal = 3;
	In.ShellsCollected = 120;
	In.ShellsTotal = 480;
	In.bFinished = true;
	const FTN_CoopScoreBreakdown Score = TNCoopScore::Compute(In);
	const FString Text = TNResultsTexts::CoopScoreBreakdown(Score).ToString();
	TArray<FString> Lines;
	Text.ParseIntoArray(Lines, TEXT("\n"));
	TestEqual(TEXT("una línea por término y la del total"), Lines.Num(), 5);
	TestTrue(TEXT("muñecos recogidos / total"), Text.Contains(TEXT("2/3")));
	TestTrue(TEXT("conchas recogidas / total"), Text.Contains(TEXT("120/480")));
	TestTrue(TEXT("el total al final"), Lines.Num() == 5 && Lines[4].Contains(FString::FromInt(Score.Total)));
	return true;
}

#endif
