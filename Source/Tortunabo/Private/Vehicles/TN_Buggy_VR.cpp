// ATN_Buggy con gafas (Docs/Modo_VR.md, «Vehículos»; #529): asiento de la conductora, volante con las manos, cabeza propia
// oculta en la vista sentada y asas de la torreta para la artillera con gafas. Las cuentas, en TNVRVehicle
// (VR/TN_VRVehicleMath.h). Sin gafas no hace nada: la vista sentada solo la enciende ATN_VRRig.

#include "Vehicles/TN_Buggy.h"
#include "TN_BuggyTurretMesh.h"
#include "Vehicles/TN_BuggyGunnerPawn.h"
#include "VR/TN_VRSeatComponent.h"
#include "ChaosVehicleWheel.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace TNBuggyVRDetail
{
	/** Columna del volante hacia el salpicadero (TNBuggyArt::AddHelm y build_buggy.py): el volante queda perpendicular a ella. */
	const FVector WheelColumnBase(97.0, 0.0, 98.0);
	/** Centro del volante respecto de la cadera de la conductora (donde caen sus manos en la postura sentada). */
	const FVector WheelAboveHip(52.0, 0.0, 25.0);
	/** Radio del aro (cm). */
	constexpr double WheelRadius = 16.0;
	/** Una mano coge el volante si al cerrarla está a menos de esto del aro (cm). */
	constexpr double WheelGrabReachCm = 22.0;
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

TNVRVehicle::FWheelFrame ATN_Buggy::GetWheelFrame()
{
	using namespace TNBuggyVRDetail;
	TNVRVehicle::FWheelFrame Wheel;
	Wheel.Center = DriverSeatLocal + WheelAboveHip;
	// Eje de la columna, hacia la conductora y arriba; las 12 del volante, perpendiculares a él en el plano vertical.
	Wheel.Axis = (Wheel.Center - WheelColumnBase).GetSafeNormal();
	Wheel.Up = FVector(Wheel.Axis.Z, 0.0, -Wheel.Axis.X).GetSafeNormal();
	Wheel.Radius = WheelRadius;
	return Wheel;
}

FVector ATN_Buggy::GetTurretHandleWorld(bool bRight) const
{
	const USceneComponent* Mount = TurretMount ? static_cast<const USceneComponent*>(TurretMount.Get()) : GetRootComponent();
	return Mount ? Mount->GetComponentTransform().TransformPosition(TNBuggyTurretMesh::HandleGripPoint(bRight)) : GetActorLocation();
}

void ATN_Buggy::ResetVRDriving()
{
	VRWheel = TNVRVehicle::FWheelState();
	bVRWheelHand[0] = false;
	bVRWheelHand[1] = false;
	if (bVRSteering)
	{
		bVRSteering = false;
		SteerRequest = StickSteer;
	}
}

void ATN_Buggy::UpdateVRDriving(float DeltaSeconds)
{
	using namespace TNBuggyVRDetail;
	UTN_VRSeatComponent* Seat = DriverVRSeat;
	if (!Seat || !Seat->IsVRView())
	{
		if (bVRSteering || VRWheel.Mask != 0)
		{
			ResetVRDriving();
		}
		return;
	}
	const TNVRVehicle::FWheelFrame Wheel = GetWheelFrame();
	const FTransform& ToWorld = GetMesh()->GetComponentTransform();
	FVector Local[2];
	bool bTracked[2];
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		const FTNVRSeatHand& Pose = Seat->GetHand(Hand);
		Local[Hand] = ToWorld.InverseTransformPosition(Pose.Location);
		bTracked[Hand] = Pose.bTracked;
		// Cerrar la mano cerca del aro coge el volante; abrirla (o perder el mando) lo suelta.
		const int32 Edge = Seat->StepGrip(Hand);
		if (Edge > 0)
		{
			bVRWheelHand[Hand] = TNVRVehicle::DistanceToRim(Local[Hand], Wheel) <= WheelGrabReachCm;
		}
		else if (Edge < 0 || !bTracked[Hand])
		{
			bVRWheelHand[Hand] = false;
		}
	}
	VRWheel = TNVRVehicle::StepWheel(VRWheel, bVRWheelHand[0], bVRWheelHand[1], Local[0], Local[1], Wheel, DeltaSeconds);
	// Cogido (o volviendo solo al centro) manda el volante; recto y sin manos, el stick (OnSteer).
	bVRSteering = VRWheel.Mask != 0 || !FMath::IsNearlyZero(VRWheel.WheelDeg, 0.5) || FMath::Abs(VRWheel.Steer) > 0.01f;
	SteerRequest = bVRSteering ? VRWheel.Steer : StickSteer;

	// Lo que ven todos: las manos que agarran, en el aro; las demás, en los mandos.
	FVector Shown[2];
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		Shown[Hand] = ToWorld.TransformPosition(bVRWheelHand[Hand] ? TNVRVehicle::RimPoint(Local[Hand], Wheel) : Local[Hand]);
	}
	Seat->SetDisplayHands(Shown[0], Shown[1], bTracked[0], bTracked[1]);
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
	// Las asas de la torreta, solo con una artillera con gafas (para el resto la torreta es la de siempre).
	const UTN_VRSeatComponent* GunnerSeat = GetVRSeat(ETNRallySeat::Gunner);
	const bool bHandles = bGunnerSeated && GunnerSeat && GunnerSeat->IsVROccupied();
	if (TurretHandles && bHandles != bTurretHandlesShown)
	{
		bTurretHandlesShown = bHandles;
		TurretHandles->SetVisibility(bHandles);
	}
}

void ATN_Buggy::DebugVRWheelPose(float WheelDeg, float Seconds)
{
	UTN_VRSeatComponent* Seat = DriverVRSeat;
	if (!Seat || !Seat->IsVRView())
	{
		UE_LOG(LogTNBuggy, Warning, TEXT("[VR] %s: sin la vista sentada de la conductora (TN.VR 2 o gafas): no se simulan las manos."), *GetName());
		return;
	}
	const TNVRVehicle::FWheelFrame Wheel = GetWheelFrame();
	const FTransform SeatInChassis = Seat->GetRelativeTransform();
	// Las dos manos cogen el volante recto (a las 9 y a las 3) y lo giran poco a poco (0,4 s quietas y 1 s girando).
	Seat->SetDebugPose([Wheel, SeatInChassis, WheelDeg](float Elapsed, FTNVRSeatHand& Left, FTNVRSeatHand& Right)
	{
		const double Alpha = FMath::SmoothStep(0.0, 1.0, FMath::Clamp((static_cast<double>(Elapsed) - 0.4) / 1.0, 0.0, 1.0));
		FVector LeftLocal;
		FVector RightLocal;
		TNVRVehicle::HandsOnWheel(Wheel, WheelDeg * Alpha, LeftLocal, RightLocal);
		for (FTNVRSeatHand* Hand : { &Left, &Right })
		{
			Hand->Grip = 1.f;
			Hand->bTracked = true;
			Hand->AimDir = FVector::ForwardVector;
		}
		Left.Location = SeatInChassis.InverseTransformPosition(LeftLocal);
		Right.Location = SeatInChassis.InverseTransformPosition(RightLocal);
	}, FMath::Max(Seconds, 1.5f) + 0.5f);
	UE_LOG(LogTNBuggy, Log, TEXT("[VR] %s: la conductora coge el volante con las dos manos y lo gira %.0f°."), *GetName(), WheelDeg);

	FTimerHandle Handle;
	TWeakObjectPtr<ATN_Buggy> Self(this);
	GetWorldTimerManager().SetTimer(Handle, [Self, WheelDeg]()
	{
		ATN_Buggy* Buggy = Self.Get();
		UChaosWheeledVehicleMovementComponent* Move = Buggy ? Buggy->GetWheeledMovement() : nullptr;
		if (!Move)
		{
			return;
		}
		float WheelSteer[2] = { 0.f, 0.f };
		if (Move->HasValidPhysicsState() && Move->PhysicsVehicleOutput())
		{
			for (int32 Index = 0; Index < 2 && Index < Move->Wheels.Num() && Index < Move->PhysicsVehicleOutput()->Wheels.Num(); ++Index)
			{
				WheelSteer[Index] = Move->Wheels[Index] ? Move->Wheels[Index]->GetSteerAngle() : 0.f;
			}
		}
		UE_LOG(LogTNBuggy, Display, TEXT("[VR] %s: volante %.1f° (pedido %.1f°) con %s, dirección %.2f (entrada de Chaos %.2f), ruedas delanteras %.1f° y %.1f°."),
			*Buggy->GetName(), Buggy->GetVRWheelDeg(), WheelDeg, Buggy->IsVRWheelHeld() ? TEXT("las manos") : TEXT("ninguna mano"),
			Buggy->SteerRequest, Move->GetSteeringInput(), WheelSteer[0], WheelSteer[1]);
	}, FMath::Max(Seconds, 1.5f), false);
}

#if !UE_BUILD_SHIPPING
namespace TNBuggyVRDebug
{
	float Arg(const TArray<FString>& Args, int32 Index, float Default)
	{
		return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : Default;
	}

	FAutoConsoleCommandWithWorldAndArgs CmdSeatPose(TEXT("TN.VR.SeatPose"),
		TEXT("VR (fuera de Shipping, con TN.VR 2 o gafas): TN.VR.SeatPose [volante° = 30] [guiñada° = 60] [cabeceo° = 10] [segundos = 3]: "
			"simula las manos en el vehículo propio. Conductora: coge el volante con las dos manos y lo gira; artillera: coge el asa "
			"derecha apuntando con esa guiñada y cabeceo respecto del buggy. Al acabar escribe en el registro el giro de las ruedas o el apuntado."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			const float Seconds = Arg(Args, 3, 3.f);
			if (ATN_Buggy* Buggy = Cast<ATN_Buggy>(Pawn))
			{
				Buggy->DebugVRWheelPose(Arg(Args, 0, 30.f), Seconds);
			}
			else if (ATN_BuggyGunnerPawn* Gunner = Cast<ATN_BuggyGunnerPawn>(Pawn))
			{
				Gunner->DebugVRAimPose(Arg(Args, 1, 60.f), Arg(Args, 2, 10.f), Seconds);
			}
			else
			{
				UE_LOG(LogTNBuggy, Warning, TEXT("[VR] TN.VR.SeatPose: la jugadora local no va en un buggy (peón %s)."), *GetNameSafe(Pawn));
			}
		}));
}
#endif
