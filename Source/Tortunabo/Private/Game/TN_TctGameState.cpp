#include "Game/TN_TctGameState.h"
#include "Net/UnrealNetwork.h"

FTNTctFloodPlan FTNTctFloodState::ToPlan() const
{
	FTNTctFloodPlan Plan;
	Plan.BaseZ = BaseZ;
	Plan.Levels = Levels;
	Plan.SuddenDeathZ = SuddenDeathZ;
	Plan.StartDelay = StartDelay;
	Plan.StepSeconds = StepSeconds;
	Plan.RiseSeconds = RiseSeconds;
	Plan.SuddenDeathRiseSeconds = SuddenDeathRiseSeconds;
	return Plan;
}

void ATN_TctGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_TctGameState, Flood);
	DOREPLIFETIME(ATN_TctGameState, FightersAlive);
}

float ATN_TctGameState::GetWaterZ() const
{
	if (Flood.StartServerTime < 0.f)
	{
		return Flood.HoldZ;
	}
	return TNTctRules::WaterZAt(Flood.ToPlan(), static_cast<float>(GetServerWorldTimeSeconds()) - Flood.StartServerTime);
}

float ATN_TctGameState::GetSecondsToNextRise() const
{
	if (Flood.StartServerTime < 0.f)
	{
		return -1.f;
	}
	const FTNTctFloodPlan Plan = Flood.ToPlan();
	const float Elapsed = static_cast<float>(GetServerWorldTimeSeconds()) - Flood.StartServerTime;
	for (int32 Step = 0; Step <= Plan.Levels.Num(); ++Step)
	{
		const float Start = TNTctRules::StepStartSeconds(Plan, Step);
		if (Start > Elapsed)
		{
			return Start - Elapsed;
		}
	}
	return -1.f;
}
