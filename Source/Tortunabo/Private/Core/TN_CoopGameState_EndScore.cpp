// Fin de partida del ATN_CoopGameState (#798, #873): títulos y puntos de cada jugadora al entrar en Results (servidor).

#include "Core/TN_CoopGameState.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_CoopScore.h"
#include "Core/TN_Log.h"
#include "Core/TN_PointsEconomy.h"
#include "Kismet/GameplayStatics.h"
#include "World/TN_LevelCollectSubsystem.h"
#include "World/TN_PuzzleScoreSubsystem.h"

namespace TNEndScoreAward
{
	/** Las jugadoras de la sala en el orden de entrada (PlayerId menor primero: gana los empates de los títulos). */
	TArray<ATN_CoopPlayerState*> PlayersInJoinOrder(const TArray<TObjectPtr<APlayerState>>& PlayerArray)
	{
		TArray<ATN_CoopPlayerState*> Players;
		for (APlayerState* PS : PlayerArray)
		{
			if (ATN_CoopPlayerState* TNPS = Cast<ATN_CoopPlayerState>(PS))
			{
				Players.Add(TNPS);
			}
		}
		Players.StableSort([](const ATN_CoopPlayerState& A, const ATN_CoopPlayerState& B) { return A.GetPlayerId() < B.GetPlayerId(); });
		return Players;
	}

	/** El título de quien más tiene según CountOf; vacío si nadie pasa de 0. */
	FTN_EndTitle PickTitle(TConstArrayView<ATN_CoopPlayerState*> Players, TFunctionRef<int32(const ATN_CoopPlayerState&)> CountOf)
	{
		TArray<TNEndTitles::FEntry> Entries;
		for (const ATN_CoopPlayerState* TNPS : Players)
		{
			Entries.Add({ TNPS->GetPlayerId(), CountOf(*TNPS) });
		}
		FTN_EndTitle Title;
		const int32 Top = TNEndTitles::PickTop(Entries);
		if (Players.IsValidIndex(Top))
		{
			Title.PlayerId = Players[Top]->GetPlayerId();
			Title.PlayerName = Players[Top]->GetPlayerName();
			Title.Count = Entries[Top].Count;
		}
		return Title;
	}

	uint8 TitleFlagsFor(int32 PlayerId, const FTN_EndTitle& Jumper, const FTN_EndTitle& Treasurer, const FTN_EndTitle& Healer)
	{
		uint8 Flags = 0;
		if (Jumper.IsAwarded() && Jumper.PlayerId == PlayerId) { Flags |= TNEndTitleFlags::Jumper; }
		if (Treasurer.IsAwarded() && Treasurer.PlayerId == PlayerId) { Flags |= TNEndTitleFlags::Treasurer; }
		if (Healer.IsAwarded() && Healer.PlayerId == PlayerId) { Flags |= TNEndTitleFlags::Healer; }
		return Flags;
	}
}

void ATN_CoopGameState::AwardEndTitles()
{
	const TArray<ATN_CoopPlayerState*> Players = TNEndScoreAward::PlayersInJoinOrder(PlayerArray);
	JumperTitle = TNEndScoreAward::PickTitle(Players, [](const ATN_CoopPlayerState& PS) { return PS.JumpCount; });
	TreasurerTitle = TNEndScoreAward::PickTitle(Players, [](const ATN_CoopPlayerState& PS) { return PS.ChapasCollected; });
	HealerTitle = TNEndScoreAward::PickTitle(Players, [](const ATN_CoopPlayerState& PS) { return PS.PlayersHealed; });
	UE_LOG(LogTortunabo, Log, TEXT("[CoopGameState] Títulos: Saltarín '%s' (%d), Tesorero '%s' (%d), Curandero '%s' (%d)."),
		*JumperTitle.PlayerName, JumperTitle.Count, *TreasurerTitle.PlayerName, TreasurerTitle.Count,
		*HealerTitle.PlayerName, HealerTitle.Count);
	ForceNetUpdate();
}

void ATN_CoopGameState::AwardEndScores()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}
	const UTN_PointsEconomy& Economy = UTN_PointsEconomy::Get();
	const FName MapName(*UGameplayStatics::GetCurrentLevelName(World, true));
	const UTN_LevelCollectSubsystem* Collect = World->GetSubsystem<UTN_LevelCollectSubsystem>();
	const UTN_PuzzleScoreSubsystem* Puzzles = World->GetSubsystem<UTN_PuzzleScoreSubsystem>();

	TNCoopScore::FInputs Shared;
	Shared.TargetSeconds = Economy.GetTargetSeconds(MapName);
	Shared.ItemsTotal = Collect ? Collect->GetTotal() : 0;
	Shared.PuzzleEfficiency = Puzzles ? Puzzles->GetEfficiency() : -1.f;

	for (ATN_CoopPlayerState* TNPS : TNEndScoreAward::PlayersInJoinOrder(PlayerArray))
	{
		TNCoopScore::FInputs In = Shared;
		In.bFinished = TNPS->bHasFinishedRun && !TNPS->bIsEliminated;
		In.FinishRank = TNPS->FinishRank;
		In.FinishTimeSeconds = TNPS->FinishTimeSeconds;
		In.DollsCollected = TNPS->TurtleDollsCollected;
		In.ItemsCollected = TNPS->LevelItemsCollected;
		In.TitleFlags = TNEndScoreAward::TitleFlagsFor(TNPS->GetPlayerId(), JumperTitle, TreasurerTitle, HealerTitle);
		const FTN_CoopScoreBreakdown Score = TNCoopScore::Compute(In, Economy.Score);
		TNPS->SetCoopScore(Score);

		const int32 PlayerId = TNPS->GetPlayerId();
		if (FTN_RaceResultEntry* Entry = RaceResults.FindByPredicate([PlayerId](const FTN_RaceResultEntry& E) { return E.PlayerId == PlayerId; }))
		{
			Entry->RaceScore = Score.Total;
		}
		UE_LOG(LogTortunabo, Log, TEXT("[Puntos] '%s' en %s: %d (meta %d, puesto %d, tiempo %d, muñecos %d, objetos %d, puzles %d, títulos %d)."),
			*TNPS->GetPlayerName(), *MapName.ToString(), Score.Total, Score.FinishPoints, Score.PositionPoints, Score.TimePoints,
			Score.DollPoints, Score.CollectPoints, Score.PuzzlePoints, Score.TitlePoints);
	}
	// Listen-server: OnRep_RaceResults no llega al anfitrión.
	OnRaceResultsUpdated.Broadcast();
	ForceNetUpdate();
}
