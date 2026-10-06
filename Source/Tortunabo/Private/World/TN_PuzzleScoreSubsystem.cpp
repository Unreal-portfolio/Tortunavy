#include "World/TN_PuzzleScoreSubsystem.h"

#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

bool UTN_PuzzleScoreSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UTN_PuzzleScoreSubsystem* UTN_PuzzleScoreSubsystem::ForServer(const AActor* Puzzle)
{
	UWorld* World = Puzzle ? Puzzle->GetWorld() : nullptr;
	if (!World || !Puzzle->HasAuthority() || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	return World->GetSubsystem<UTN_PuzzleScoreSubsystem>();
}

float UTN_PuzzleScoreSubsystem::Now() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.f;
}

TNCoopScore::FPuzzleRun& UTN_PuzzleScoreSubsystem::FindOrAdd(const AActor* Puzzle)
{
	const FObjectKey Key(Puzzle);
	if (TNCoopScore::FPuzzleRun* Found = Runs.Find(Key))
	{
		return *Found;
	}
	Order.Add(Key);
	return Runs.Add(Key);
}

void UTN_PuzzleScoreSubsystem::Register(const AActor* Puzzle)
{
	if (UTN_PuzzleScoreSubsystem* Self = ForServer(Puzzle))
	{
		Self->FindOrAdd(Puzzle);
	}
}

void UTN_PuzzleScoreSubsystem::NotifyProgress(const AActor* Puzzle)
{
	UTN_PuzzleScoreSubsystem* Self = ForServer(Puzzle);
	if (!Self)
	{
		return;
	}
	TNCoopScore::FPuzzleRun& Run = Self->FindOrAdd(Puzzle);
	if (Run.ProgressTime < 0.f && !Run.bSolved)
	{
		Run.ProgressTime = Self->Now();
	}
}

void UTN_PuzzleScoreSubsystem::NotifySolved(const AActor* Puzzle)
{
	UTN_PuzzleScoreSubsystem* Self = ForServer(Puzzle);
	if (!Self)
	{
		return;
	}
	TNCoopScore::FPuzzleRun& Run = Self->FindOrAdd(Puzzle);
	if (Run.bSolved)
	{
		return;
	}
	Run.bSolved = true;
	Run.SolveTime = Self->Now();
	UE_LOG(LogTortunabo, Log, TEXT("[PuzzleScore] %s resuelto (eficiencia %.2f)."), *GetNameSafe(Puzzle), TNCoopScore::RunEfficiency(Run));
}

TArray<TNCoopScore::FPuzzleRun> UTN_PuzzleScoreSubsystem::GetRuns() const
{
	TArray<TNCoopScore::FPuzzleRun> Out;
	Out.Reserve(Order.Num());
	for (const FObjectKey& Key : Order)
	{
		if (const TNCoopScore::FPuzzleRun* Run = Runs.Find(Key))
		{
			Out.Add(*Run);
		}
	}
	return Out;
}
