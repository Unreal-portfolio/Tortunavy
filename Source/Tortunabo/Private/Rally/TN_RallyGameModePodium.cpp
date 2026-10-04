// ATN_RallyGameMode: podio de la llegada (#306). Cada buggy que cruza la meta se aparca en el podio que flota sobre ella
// cuando acaba su plano lateral (TNRallyCamera::ParkDelaySeconds): los tres primeros en los escalones y el resto en fila
// detrás, con el motor, el freno y la torreta bloqueados. Así la cámara del podio enseña a los primeros con sus tortugas y
// los que ya han llegado no estorban en la pista a los que siguen.

#include "Rally/TN_RallyGameMode.h"

#include "Rally/TN_RallyCameraLogic.h"
#include "Rally/TN_RallyPodium.h"
#include "Rally/TN_RallyTrack.h"
#include "Rally/TN_RallyVehicle.h"

void ATN_RallyGameMode::SpawnPodium()
{
	if (Podium || !Track || !Track->IsBuilt() || Track->GetGateCount() < 2)
	{
		return;
	}
	const int32 FinishGate = Track->IsCircuit() ? 0 : Track->GetGateCount() - 1;
	const FTransform Finish = Track->GetGateCrossingTransform(FinishGate);
	Podium = ATN_RallyPodium::SpawnAtFinish(GetWorld(), Finish.GetLocation(), Finish.GetRotation().GetForwardVector());
}

void ATN_RallyGameMode::NoteTeamFinished(FTeamRuntime& Team)
{
	int32 Before = 0;
	for (const FTeamRuntime& Other : Teams)
	{
		Before += (&Other != &Team && Other.bFinished) ? 1 : 0;
	}
	Team.FinishOrder = Before + 1;
	Team.ParkAtTime = Now() + TNRallyCamera::ParkDelaySeconds;
	Team.bParked = false;
}

void ATN_RallyGameMode::ParkFinishedTeams()
{
	if (!Podium)
	{
		return;
	}
	const double Time = Now();
	for (FTeamRuntime& Team : Teams)
	{
		if (Team.bFinished && !Team.bParked && Team.FinishOrder > 0 && Time >= Team.ParkAtTime)
		{
			ParkTeam(Team);
		}
	}
}

void ATN_RallyGameMode::ParkTeam(FTeamRuntime& Team)
{
	ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Team.Vehicle.Get());
	if (!Podium || !RallyVehicle)
	{
		return;
	}
	RallyVehicle->RallyTeleport(Podium->GetSlotTransform(Team.FinishOrder, Team.OriginAboveBottomCm), 0.f, 0.f);
	RallyVehicle->SetEngineLocked(true);
	RallyVehicle->SetRaceBrakeHeld(true);
	RallyVehicle->SetWeaponsLocked(true);
	Team.bParked = true;
	// Que el salto al podio no cuente como recorrido ni dispare las comprobaciones de la pista.
	Team.PrevLocation = Team.Vehicle->GetActorLocation();
	UE_LOG(LogTNRally, Log, TEXT("[RallyGameMode] Equipo %d aparcado en el podio (%d.º en llegar)."), Team.TeamIndex, Team.FinishOrder);
}
