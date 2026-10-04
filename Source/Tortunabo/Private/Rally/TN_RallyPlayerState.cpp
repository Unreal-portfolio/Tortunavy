#include "Rally/TN_RallyPlayerState.h"

#include "Net/UnrealNetwork.h"

void ATN_RallyPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_RallyPlayerState, RallyTeamIndex);
	DOREPLIFETIME(ATN_RallyPlayerState, RallySeat);
}

void ATN_RallyPlayerState::SetRallySeat(int32 InTeamIndex, ETNRallySeat InSeat)
{
	if (RallyTeamIndex == InTeamIndex && RallySeat == InSeat)
	{
		return;
	}
	RallyTeamIndex = InTeamIndex;
	RallySeat = InSeat;
	ForceNetUpdate();
}

void ATN_RallyPlayerState::ClearRallySeat()
{
	SetRallySeat(INDEX_NONE, ETNRallySeat::Driver);
}
