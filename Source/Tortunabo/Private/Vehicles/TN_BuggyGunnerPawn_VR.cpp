// ATN_BuggyGunnerPawn con gafas (Docs/Modo_VR.md, «Vehículos»): manos que ven los demás y cámara del asiento sin gafas.

#include "Vehicles/TN_BuggyGunnerPawn.h"
#include "VR/TN_VRSeatComponent.h"

void ATN_BuggyGunnerPawn::UpdateVRGunner(float DeltaSeconds)
{
	// Lo que ven todos: las manos de la tortuga sentada en los mandos.
	const FTNVRSeatHand& Left = VRSeat->GetHand(0);
	const FTNVRSeatHand& Right = VRSeat->GetHand(1);
	VRSeat->SetDisplayHands(Left.Location, Right.Location, Left.bTracked, Right.bTracked);
	// Sin gafas (simulado), la cámara del asiento mira hacia el apuntado (el ratón o el stick), como en primera persona.
	if (!VRSeat->IsHeadsetView())
	{
		VRSeat->SetSimulatedLook(LocalAim);
	}
}
