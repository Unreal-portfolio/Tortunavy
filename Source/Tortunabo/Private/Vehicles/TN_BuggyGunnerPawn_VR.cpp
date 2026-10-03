// ATN_BuggyGunnerPawn con gafas (Docs/Modo_VR.md, «Vehículos»; #529): asas de la torreta y apuntado con las manos. Las
// cuentas, en TNVRVehicle (VR/TN_VRVehicleMath.h). El apuntado sale como siempre: LocalAim, que Tick manda al servidor con
// ServerSetAim (limitado allí también con SetAimRelative), y el disparo con el gatillo (UTN_BuggyInputSet).

#include "Vehicles/TN_BuggyGunnerPawn.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Vehicles/TN_RallyTurretLogic.h"
#include "VR/TN_VRSeatComponent.h"
#include "VR/TN_VRVehicleMath.h"
#include "Engine/World.h"
#include "TimerManager.h"

void ATN_BuggyGunnerPawn::UpdateVRGunner(float DeltaSeconds)
{
	FVector Shown[2];
	bool bShown[2];
	FVector AimDir[2];
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		const FTNVRSeatHand& Pose = VRSeat->GetHand(Hand);
		// Cerrar la mano cerca de un puño coge las asas; abrirla (o perder el mando) las suelta.
		const int32 Edge = VRSeat->StepGrip(Hand);
		if (Edge > 0)
		{
			const double Nearest = FMath::Min(FVector::Dist(Pose.Location, Buggy->GetTurretHandleWorld(false)),
				FVector::Dist(Pose.Location, Buggy->GetTurretHandleWorld(true)));
			bVRHandle[Hand] = Nearest <= VRHandleGrabReachCm;
		}
		else if (Edge < 0 || !Pose.bTracked)
		{
			bVRHandle[Hand] = false;
		}
		// Lo que ven todos: la mano que agarra, en su puño; la otra, en el mando.
		Shown[Hand] = bVRHandle[Hand] ? Buggy->GetTurretHandleWorld(Hand == 1) : Pose.Location;
		bShown[Hand] = Pose.bTracked;
		AimDir[Hand] = Pose.AimDir;
	}
	if (bVRHandle[0] || bVRHandle[1])
	{
		// Apuntado absoluto: hacia donde apuntan las manos que agarran, en los ejes del buggy y limitado como siempre.
		const FVector Dir = TNVRVehicle::AverageAimDir(AimDir[0], bVRHandle[0], AimDir[1], bVRHandle[1]);
		if (!Dir.IsNearlyZero())
		{
			const FRotator Target = TNVRVehicle::AimFromHands(Buggy->GetActorQuat(), Dir);
			LocalAim = TNRallyTurret::ClampAim(DeltaSeconds > 0.f ? FMath::RInterpTo(LocalAim, Target, DeltaSeconds, VRAimSmoothing) : Target);
		}
	}
	VRSeat->SetDisplayHands(Shown[0], Shown[1], bShown[0], bShown[1]);
	// Sin gafas (simulado), la cámara del asiento mira hacia el apuntado (el ratón o el stick), como en primera persona.
	if (!VRSeat->IsHeadsetView())
	{
		VRSeat->SetSimulatedLook(LocalAim);
	}
}

void ATN_BuggyGunnerPawn::DebugVRAimPose(float YawDeg, float PitchDeg, float Seconds)
{
	if (!VRSeat || !VRSeat->IsVRView() || !Buggy)
	{
		UE_LOG(LogTNBuggy, Warning, TEXT("[VR] %s: sin la vista sentada de la artillera (TN.VR 2 o gafas): no se simulan las manos."), *GetName());
		return;
	}
	// La mano derecha en su puño (que gira con el carro) con el agarre cerrado, apuntando a (YawDeg, PitchDeg) respecto del
	// buggy; la izquierda, abierta junto al suyo. Todo en los ejes del asiento.
	TWeakObjectPtr<ATN_BuggyGunnerPawn> Self(this);
	VRSeat->SetDebugPose([Self, YawDeg, PitchDeg](float Elapsed, FTNVRSeatHand& Left, FTNVRSeatHand& Right)
	{
		const ATN_BuggyGunnerPawn* Pawn = Self.Get();
		if (!Pawn || !Pawn->Buggy || !Pawn->VRSeat)
		{
			return;
		}
		const FTransform& SeatToWorld = Pawn->VRSeat->GetComponentTransform();
		const FVector AimWorld = Pawn->Buggy->GetActorQuat().RotateVector(FRotator(PitchDeg, YawDeg, 0.f).Vector());
		Right.Location = SeatToWorld.InverseTransformPosition(Pawn->Buggy->GetTurretHandleWorld(true));
		Right.AimDir = SeatToWorld.InverseTransformVectorNoScale(AimWorld);
		Right.Grip = 1.f;
		Right.bTracked = true;
		Left.Location = SeatToWorld.InverseTransformPosition(Pawn->Buggy->GetTurretHandleWorld(false));
		Left.AimDir = FVector::ForwardVector;
		Left.Grip = 0.f;
		Left.bTracked = true;
	}, FMath::Max(Seconds, 1.f) + 0.5f);
	UE_LOG(LogTNBuggy, Log, TEXT("[VR] %s: la artillera coge el asa derecha apuntando a guiñada %.0f° y cabeceo %.0f°."), *GetName(), YawDeg, PitchDeg);

	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, [Self, YawDeg, PitchDeg]()
	{
		const ATN_BuggyGunnerPawn* Pawn = Self.Get();
		const UTN_BuggyTurretComponent* Turret = Pawn && Pawn->Buggy ? Pawn->Buggy->GetTurret() : nullptr;
		if (!Turret)
		{
			return;
		}
		const FRotator Wanted = TNRallyTurret::ClampAim(FRotator(PitchDeg, YawDeg, 0.f));
		// La torreta replicada (la del servidor) en los ejes del buggy.
		const FRotator Replicated = TNVRVehicle::AimFromHands(Pawn->Buggy->GetActorQuat(), Turret->GetAimWorldDirection());
		UE_LOG(LogTNBuggy, Display, TEXT("[VR] %s: asas %s; apuntado local guiñada %.1f° cabeceo %.1f° (pedido %.1f°, %.1f°); torreta del servidor %.1f°, %.1f°."),
			*Pawn->GetName(), Pawn->IsVRHandleHeld() ? TEXT("cogidas") : TEXT("sueltas"), Pawn->LocalAim.Yaw, Pawn->LocalAim.Pitch,
			Wanted.Yaw, Wanted.Pitch, Replicated.Yaw, Replicated.Pitch);
	}, FMath::Max(Seconds, 1.f), false);
}
