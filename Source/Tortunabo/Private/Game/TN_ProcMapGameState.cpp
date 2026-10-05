#include "Game/TN_ProcMapGameState.h"
#include "Net/UnrealNetwork.h"

void ATN_ProcMapGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_ProcMapGameState, ProcMode);
	DOREPLIFETIME(ATN_ProcMapGameState, ProcDifficulty);
	DOREPLIFETIME(ATN_ProcMapGameState, CurrentRound);
	DOREPLIFETIME(ATN_ProcMapGameState, RoundTarget);
	DOREPLIFETIME(ATN_ProcMapGameState, bRoundInProgress);
	DOREPLIFETIME(ATN_ProcMapGameState, RoundResultText);
	DOREPLIFETIME(ATN_ProcMapGameState, MapSeed);
	DOREPLIFETIME(ATN_ProcMapGameState, EstimatedMinutes);
	DOREPLIFETIME(ATN_ProcMapGameState, TurtleDollsTotal);
}
