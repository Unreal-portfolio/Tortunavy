#include "UI/HUD/TN_ResultsTexts.h"

#include "Core/TN_CoopScore.h"
#include "Core/TN_LocText.h"

FText TNResultsTexts::CoopScoreBreakdown(const FTN_CoopScoreBreakdown& Score)
{
	if (!Score.bValid)
	{
		return FText::GetEmpty();
	}
	TArray<FText> Lines;
	Lines.Add(FText::Format(NSLOCTEXT("TNHUD", "CoopScoreDolls", "Muñecos tortuga: {0}/{1}  +{2}"),
		TNLocText::Int(Score.DollsCollected), TNLocText::Int(Score.DollsTotal), TNLocText::Int(Score.DollPoints)));
	Lines.Add(FText::Format(NSLOCTEXT("TNHUD", "CoopScoreShells", "Conchas del nivel: {0}/{1}  +{2}"),
		TNLocText::Int(Score.ShellsCollected), TNLocText::Int(Score.ShellsTotal), TNLocText::Int(Score.ShellPoints)));
	Lines.Add(Score.bFinished
		? FText::Format(NSLOCTEXT("TNHUD", "CoopScoreFinished", "Llegada a la meta  +{0}"), TNLocText::Int(Score.FinishPoints))
		: FText::Format(NSLOCTEXT("TNHUD", "CoopScoreNotFinished", "Sin llegar a la meta  +{0}"), TNLocText::Int(Score.FinishPoints)));
	Lines.Add(Score.HasPuzzles()
		? FText::Format(NSLOCTEXT("TNHUD", "CoopScorePuzzle", "Eficiencia de puzle: {0}  +{1}"),
			FText::AsPercent(Score.PuzzleEfficiency), TNLocText::Int(Score.PuzzlePoints))
		: FText::Format(NSLOCTEXT("TNHUD", "CoopScoreNoPuzzle", "Sin puzles en el nivel  +{0}"), TNLocText::Int(Score.PuzzlePoints)));
	Lines.Add(FText::Format(NSLOCTEXT("TNHUD", "CoopScoreTotal", "Puntuación final: {0}"), TNLocText::Int(Score.Total)));
	return FText::Join(INVTEXT("\n"), Lines);
}
