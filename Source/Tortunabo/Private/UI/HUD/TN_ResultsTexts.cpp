#include "UI/HUD/TN_ResultsTexts.h"

#include "Core/TN_CoopScore.h"
#include "Core/TN_EndTitles.h"
#include "Core/TN_LocText.h"

namespace TNResultsTextsPrivate
{
	void AddRaceLines(const FTN_CoopScoreBreakdown& Score, TArray<FText>& Lines)
	{
		if (!Score.bFinished)
		{
			Lines.Add(FText::Format(NSLOCTEXT("TNHUD", "CoopScoreNotFinished", "Sin llegar a la meta  +{0}"), TNLocText::Int(Score.FinishPoints)));
			return;
		}
		Lines.Add(FText::Format(NSLOCTEXT("TNHUD", "CoopScoreFinished", "Llegada a la meta  +{0}"), TNLocText::Int(Score.FinishPoints)));
		Lines.Add(FText::Format(NSLOCTEXT("TNHUD", "CoopScorePosition", "Puesto {0}  +{1}"),
			TNLocText::Int(Score.FinishRank), TNLocText::Int(Score.PositionPoints)));
		Lines.Add(FText::Format(NSLOCTEXT("TNHUD", "CoopScoreTime", "Tiempo {0} (objetivo {1})  +{2}"),
			TNLocText::MinutesSeconds(FMath::FloorToInt32(Score.FinishTimeSeconds)),
			TNLocText::MinutesSeconds(FMath::FloorToInt32(Score.TargetSeconds)), TNLocText::Int(Score.TimePoints)));
	}

	void AddTitleLine(const FTN_CoopScoreBreakdown& Score, TArray<FText>& Lines)
	{
		TArray<FText> Names;
		for (const uint8 Flag : { TNEndTitleFlags::Jumper, TNEndTitleFlags::Treasurer, TNEndTitleFlags::Healer })
		{
			if (Score.HasTitle(Flag)) { Names.Add(TNResultsTexts::TitleName(Flag)); }
		}
		if (Names.Num() > 0)
		{
			Lines.Add(FText::Format(NSLOCTEXT("TNHUD", "CoopScoreTitles", "Títulos: {0}  +{1}"),
				TNLocText::JoinList(Names), TNLocText::Int(Score.TitlePoints)));
		}
	}
}

FText TNResultsTexts::CoopScoreBreakdown(const FTN_CoopScoreBreakdown& Score)
{
	if (!Score.bValid)
	{
		return FText::GetEmpty();
	}
	TArray<FText> Lines;
	TNResultsTextsPrivate::AddRaceLines(Score, Lines);
	Lines.Add(FText::Format(NSLOCTEXT("TNHUD", "CoopScoreDollCount", "Muñecos tortuga: {0}  +{1}"),
		TNLocText::Int(Score.DollsCollected), TNLocText::Int(Score.DollPoints)));
	Lines.Add(Score.HasItems()
		? FText::Format(NSLOCTEXT("TNHUD", "CoopScoreItems", "Objetos del nivel: {0}/{1}  +{2}"),
			TNLocText::Int(Score.ItemsCollected), TNLocText::Int(Score.ItemsTotal), TNLocText::Int(Score.CollectPoints))
		: FText::Format(NSLOCTEXT("TNHUD", "CoopScoreNoItems", "Sin objetos en el nivel  +{0}"), TNLocText::Int(Score.CollectPoints)));
	Lines.Add(Score.HasPuzzles()
		? FText::Format(NSLOCTEXT("TNHUD", "CoopScorePuzzle", "Eficiencia de puzle: {0}  +{1}"),
			FText::AsPercent(Score.PuzzleEfficiency), TNLocText::Int(Score.PuzzlePoints))
		: FText::Format(NSLOCTEXT("TNHUD", "CoopScoreNoPuzzle", "Sin puzles en el nivel  +{0}"), TNLocText::Int(Score.PuzzlePoints)));
	TNResultsTextsPrivate::AddTitleLine(Score, Lines);
	Lines.Add(FText::Format(NSLOCTEXT("TNHUD", "CoopScorePointsTotal", "Total: {0} {0}|plural(one=punto,other=puntos)"), Score.Total));
	return FText::Join(INVTEXT("\n"), Lines);
}

FText TNResultsTexts::TitleName(uint8 Flag)
{
	switch (Flag)
	{
	case TNEndTitleFlags::Treasurer: return NSLOCTEXT("TNHUD", "TitleTreasurer", "Tesorero");
	case TNEndTitleFlags::Healer:    return NSLOCTEXT("TNHUD", "TitleHealer", "Curandero");
	default:                         return NSLOCTEXT("TNHUD", "TitleJumper", "Saltarín");
	}
}

FText TNResultsTexts::JumperTitle(const FTN_EndTitle& Title)
{
	if (!Title.IsAwarded())
	{
		return FText::GetEmpty();
	}
	return FText::Format(NSLOCTEXT("TNHUD", "EndTitleJumper", "Saltarín: {0} ({1} {1}|plural(one=salto,other=saltos))"),
		TNLocText::PlayerName(Title.PlayerName), Title.Count);
}

FText TNResultsTexts::TreasurerTitle(const FTN_EndTitle& Title)
{
	if (!Title.IsAwarded())
	{
		return FText::GetEmpty();
	}
	return FText::Format(NSLOCTEXT("TNHUD", "EndTitleTreasurer", "Tesorero: {0} ({1} {1}|plural(one=chapa,other=chapas))"),
		TNLocText::PlayerName(Title.PlayerName), Title.Count);
}

FText TNResultsTexts::HealerTitle(const FTN_EndTitle& Title)
{
	if (!Title.IsAwarded())
	{
		return FText::GetEmpty();
	}
	return FText::Format(NSLOCTEXT("TNHUD", "EndTitleHealer", "Curandero: {0} ({1} {1}|plural(one=cura,other=curas))"),
		TNLocText::PlayerName(Title.PlayerName), Title.Count);
}

FText TNResultsTexts::EndTitles(const FTN_EndTitle& Jumper, const FTN_EndTitle& Treasurer, const FTN_EndTitle& Healer)
{
	TArray<FText> Lines;
	for (const FText& Line : { JumperTitle(Jumper), TreasurerTitle(Treasurer), HealerTitle(Healer) })
	{
		if (!Line.IsEmpty()) { Lines.Add(Line); }
	}
	return Lines.Num() > 0 ? FText::Join(INVTEXT("\n"), Lines) : FText::GetEmpty();
}
