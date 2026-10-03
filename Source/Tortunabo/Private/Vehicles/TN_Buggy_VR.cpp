// ATN_Buggy con gafas (Docs/Modo_VR.md, «Vehículos»): asiento de la conductora, manos que ven los demás y cabeza propia
// oculta en la vista sentada. Sin gafas no hace nada: la vista sentada solo la enciende ATN_VRRig.

#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyGunnerPawn.h"
#include "VR/TN_VRSeatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"

namespace TNBuggyVRDetail
{
	/** Hueso de la cabeza de la tortuga sentada (se oculta en la vista sentada propia, como en primera persona). */
	const FName HeadBone(TEXT("Head"));
}

UTN_VRSeatComponent* ATN_Buggy::GetVRSeat(ETNRallySeat Seat) const
{
	if (Seat == ETNRallySeat::Driver)
	{
		return DriverVRSeat;
	}
	return GunnerPawn ? GunnerPawn->GetVRSeat() : nullptr;
}

void ATN_Buggy::UpdateVRDriving(float DeltaSeconds)
{
	UTN_VRSeatComponent* Seat = DriverVRSeat;
	if (!Seat || !Seat->IsVRView())
	{
		return;
	}
	// Lo que ven todos: las manos de la tortuga sentada en los mandos.
	const FTNVRSeatHand& Left = Seat->GetHand(0);
	const FTNVRSeatHand& Right = Seat->GetHand(1);
	Seat->SetDisplayHands(Left.Location, Right.Location, Left.bTracked, Right.bTracked);
}

void ATN_Buggy::UpdateVRVisuals()
{
	using namespace TNBuggyVRDetail;
	// La cabeza propia no se ve desde la vista sentada (la cámara va dentro de ella), como la tortuga en primera persona.
	// Solo en la máquina de quien va sentada: IsVRView solo es cierto para el dueño del asiento.
	for (int32 SeatIndex = 0; SeatIndex < 2; ++SeatIndex)
	{
		const UTN_VRSeatComponent* Seat = GetVRSeat(SeatIndex == 0 ? ETNRallySeat::Driver : ETNRallySeat::Gunner);
		const bool bHide = Seat && Seat->IsVRView();
		USkeletalMeshComponent* Turtle = SeatTurtles.IsValidIndex(SeatIndex) ? SeatTurtles[SeatIndex].Get() : nullptr;
		if (Turtle && Turtle->GetSkeletalMeshAsset() && Turtle->GetBoneIndex(HeadBone) != INDEX_NONE
			&& Turtle->IsBoneHiddenByName(HeadBone) != bHide)
		{
			if (bHide)
			{
				Turtle->HideBoneByName(HeadBone, PBO_None);
			}
			else
			{
				Turtle->UnHideBoneByName(HeadBone);
			}
		}
		if (UStaticMeshComponent* Helmet = SeatHelmets.IsValidIndex(SeatIndex) ? SeatHelmets[SeatIndex].Get() : nullptr)
		{
			Helmet->SetVisibility(!bHide);
		}
	}
}
