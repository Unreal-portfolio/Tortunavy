// Puntos de final de partida (#873, TN_CoopScore.h): la fórmula del director (meta, puesto, tiempo, muñecos, objetos del
// nivel, puzles y títulos) con los valores de serie de FTN_EndScoreRules, el caso negativo de quien no llega, la eficiencia
// de puzle y el texto del desglose de la pantalla de resultados (TN_ResultsTexts.h). Sin mundo: el mismo código que usan
// ATN_CoopGameState::AwardEndScores y UTN_CoopFlowHUDWidget. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Coop.Score; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Core/TN_CoopScore.h"
#include "Core/TN_PointsEconomy.h"
#include "UI/HUD/TN_ResultsTexts.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNCoopScoreTest
{
	/** Primera en llegar en 4:00 con objetivo de 10:00, 2 muñecos, 3 de 4 objetos, puzles al 50 % y título Saltarín. */
	TNCoopScore::FInputs Winner()
	{
		TNCoopScore::FInputs In;
		In.bFinished = true;
		In.FinishRank = 1;
		In.FinishTimeSeconds = 240.f;
		In.TargetSeconds = 600.f;
		In.DollsCollected = 2;
		In.ItemsCollected = 3;
		In.ItemsTotal = 4;
		In.PuzzleEfficiency = 0.5f;
		In.TitleFlags = TNEndTitleFlags::Jumper;
		return In;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopScoreComputeTest,
	"Tortunabo.Coop.Score.Compute",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopScoreComputeTest::RunTest(const FString& Parameters)
{
	using namespace TNCoopScore;
	const FTN_EndScoreRules Rules;
	TestEqual(TEXT("valores de la decisión: meta"), Rules.FinishPoints, 100);
	TestTrue(TEXT("valores de la decisión: puestos"), Rules.PositionPoints == TArray<int32>({ 50, 30, 20, 10 }));

	const FTN_CoopScoreBreakdown Top = Compute(TNCoopScoreTest::Winner(), Rules);
	TestTrue(TEXT("calculada"), Top.bValid);
	TestEqual(TEXT("meta"), Top.FinishPoints, 100);
	TestEqual(TEXT("primer puesto"), Top.PositionPoints, 50);
	TestEqual(TEXT("360 s bajo el objetivo: 36"), Top.TimePoints, 36);
	TestEqual(TEXT("2 muñecos × 25"), Top.DollPoints, 50);
	TestEqual(TEXT("3/4 de los objetos × 50 (redondeado)"), Top.CollectPoints, 38);
	TestEqual(TEXT("puzles al 50 % × 30"), Top.PuzzlePoints, 15);
	TestEqual(TEXT("un título"), Top.TitlePoints, 20);
	TestEqual(TEXT("total"), Top.Total, 100 + 50 + 36 + 50 + 38 + 15 + 20);

	FInputs Fast = TNCoopScoreTest::Winner();
	Fast.TargetSeconds = 900.f;
	Fast.FinishTimeSeconds = 60.f;
	TestEqual(TEXT("840 s bajo el objetivo: tope de 60"), Compute(Fast, Rules).TimePoints, 60);

	// Puestos y tiempo: +1 por cada 10 s enteros bajo el objetivo; fuera de la tabla de puestos, nada.
	FInputs Fourth = TNCoopScoreTest::Winner();
	Fourth.FinishRank = 4;
	Fourth.FinishTimeSeconds = 571.f;
	const FTN_CoopScoreBreakdown Late = Compute(Fourth, Rules);
	TestEqual(TEXT("cuarto puesto"), Late.PositionPoints, 10);
	TestEqual(TEXT("29 s bajo el objetivo: 2"), Late.TimePoints, 2);
	Fourth.FinishRank = 5;
	Fourth.FinishTimeSeconds = 700.f;
	TestEqual(TEXT("quinto puesto: nada"), Compute(Fourth, Rules).PositionPoints, 0);
	TestEqual(TEXT("por encima del objetivo: nada"), Compute(Fourth, Rules).TimePoints, 0);

	// Los tres títulos.
	FInputs AllTitles = TNCoopScoreTest::Winner();
	AllTitles.TitleFlags = TNEndTitleFlags::All;
	TestEqual(TEXT("tres títulos"), Compute(AllTitles, Rules).TitlePoints, 60);

	// Nada que medir: nivel sin objetos ni puzles no da puntos por esos términos.
	FInputs Empty = TNCoopScoreTest::Winner();
	Empty.ItemsTotal = 0;
	Empty.ItemsCollected = 3;
	Empty.PuzzleEfficiency = -1.f;
	const FTN_CoopScoreBreakdown NoMeasure = Compute(Empty, Rules);
	TestEqual(TEXT("sin objetos: 0"), NoMeasure.CollectPoints, 0);
	TestEqual(TEXT("sin puzles: 0"), NoMeasure.PuzzlePoints, 0);

	// Valores del DataAsset: la fórmula usa los que se le pasen.
	FTN_EndScoreRules Custom;
	Custom.FinishPoints = 7;
	Custom.PositionPoints = { 1 };
	Custom.MaxTimePoints = 0;
	Custom.PointsPerDoll = 0;
	Custom.CollectedRatioPoints = 0;
	Custom.PuzzleEfficiencyPoints = 0;
	Custom.PointsPerTitle = 0;
	TestEqual(TEXT("valores propios"), Compute(TNCoopScoreTest::Winner(), Custom).Total, 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopScoreNotFinishedTest,
	"Tortunabo.Coop.Score.NotFinished",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopScoreNotFinishedTest::RunTest(const FString& Parameters)
{
	using namespace TNCoopScore;
	const FTN_EndScoreRules Rules;
	// Caso negativo: no llega a la meta (eliminada). Aunque traiga puesto y tiempo de otra ronda, no cuentan.
	FInputs Out = TNCoopScoreTest::Winner();
	Out.bFinished = false;
	const FTN_CoopScoreBreakdown Score = Compute(Out, Rules);
	TestFalse(TEXT("no llegó"), Score.bFinished);
	TestEqual(TEXT("sin meta"), Score.FinishPoints, 0);
	TestEqual(TEXT("sin puesto"), Score.PositionPoints, 0);
	TestEqual(TEXT("sin tiempo"), Score.TimePoints, 0);
	TestEqual(TEXT("puesto a 0 en el desglose"), Score.FinishRank, 0);
	TestEqual(TEXT("lo recogido y los títulos sí cuentan"), Score.Total, Score.DollPoints + Score.CollectPoints + Score.PuzzlePoints + Score.TitlePoints);
	TestTrue(TEXT("menos que llegando"), Score.Total < Compute(TNCoopScoreTest::Winner(), Rules).Total);

	FInputs Nothing;
	TestEqual(TEXT("sin llegar y sin nada: 0"), Compute(Nothing, Rules).Total, 0);

	// Entradas fuera de rango: nada negativo ni por encima de lo que había.
	FInputs Weird;
	Weird.DollsCollected = -4;
	Weird.ItemsCollected = 9;
	Weird.ItemsTotal = 4;
	Weird.PuzzleEfficiency = 3.f;
	Weird.TitleFlags = 0xFF;
	const FTN_CoopScoreBreakdown Clamped = Compute(Weird, Rules);
	TestEqual(TEXT("muñecos negativos: 0"), Clamped.DollPoints, 0);
	TestEqual(TEXT("más objetos que el total: el peso entero"), Clamped.CollectPoints, Rules.CollectedRatioPoints);
	TestEqual(TEXT("eficiencia > 1: el peso entero"), Clamped.PuzzlePoints, Rules.PuzzleEfficiencyPoints);
	TestEqual(TEXT("banderas de más: solo tres títulos"), Clamped.TitlePoints, 3 * Rules.PointsPerTitle);
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
	TestEqual(TEXT("media de los puzles"), PuzzleEfficiency({ Unsolved, Quick, Slow, Switch }), (0.f + 1.f + 0.5f + 1.f) / 4.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopScoreTextTest,
	"Tortunabo.Coop.Score.Text",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopScoreTextTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("sin puntuación: vacío"), TNResultsTexts::CoopScoreBreakdown(FTN_CoopScoreBreakdown()).IsEmpty());

	const FTN_CoopScoreBreakdown Score = TNCoopScore::Compute(TNCoopScoreTest::Winner(), FTN_EndScoreRules());
	const FString Text = TNResultsTexts::CoopScoreBreakdown(Score).ToString();
	TArray<FString> Lines;
	Text.ParseIntoArray(Lines, TEXT("\n"));
	// Meta, puesto, tiempo, muñecos, objetos, puzles, títulos y total.
	TestEqual(FString::Printf(TEXT("ocho líneas (%s)"), *Text), Lines.Num(), 8);
	TestTrue(TEXT("objetos recogidos / total"), Text.Contains(TEXT("3/4")));
	TestTrue(TEXT("tiempo y objetivo"), Text.Contains(TEXT("4:00")) && Text.Contains(TEXT("10:00")));
	TestTrue(TEXT("el total al final"), Lines.Num() == 8 && Lines[7].Contains(FString::FromInt(Score.Total)));

	FTN_CoopScoreBreakdown Out = Score;
	Out.bFinished = false;
	Out.TitleFlags = 0;
	TArray<FString> OutLines;
	TNResultsTexts::CoopScoreBreakdown(Out).ToString().ParseIntoArray(OutLines, TEXT("\n"));
	TestEqual(TEXT("sin llegar y sin títulos: cinco líneas"), OutLines.Num(), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPointsEconomyTargetTest,
	"Tortunabo.Coop.Score.LevelTarget",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPointsEconomyTargetTest::RunTest(const FString& Parameters)
{
	UTN_PointsEconomy* Economy = NewObject<UTN_PointsEconomy>();
	Economy->DefaultTargetSeconds = 600.f;
	Economy->LevelTargetSeconds.Add(TEXT("LVL_Demo01"), 420.f);
	TestEqual(TEXT("nivel con su objetivo"), Economy->GetTargetSeconds(TEXT("LVL_Demo01")), 420.f);
	TestEqual(TEXT("nivel sin objetivo: el de serie"), Economy->GetTargetSeconds(TEXT("LVL_Otro")), 600.f);
	TestEqual(TEXT("el asset de serie trae la caja a 150"), UTN_PointsEconomy::Get().MysteryBox.BoxPrice, 150);
	return true;
}

#endif
