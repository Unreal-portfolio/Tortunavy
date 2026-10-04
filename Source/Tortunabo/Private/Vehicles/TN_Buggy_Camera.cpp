// ATN_Buggy: cámara dinámica de la conductora (#298). Solo en la máquina de la conductora local y solo cosmética: se
// adelanta hacia el interior de la curva, se acerca y baja al frenar fuerte, se inclina un poco en el derrape, abre el FOV
// con el turbo y tiembla al aterrizar y con el turbo. La lógica está en TNBuggy::AdvanceDriverCamera (tests en
// Tortunabo.Rally.Drive.Camera).

#include "Vehicles/TN_Buggy.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"

void ATN_Buggy::UpdateCamera(float DeltaSeconds)
{
	const TNBuggy::FDriverCameraTuning& Tuning = TNBuggy::DefaultDriverCamera();
	const USkeletalMeshComponent* Chassis = GetMesh();
	const FVector Velocity = GetVelocity();

	TNBuggy::FDriverCameraInput In;
	In.Dt = DeltaSeconds;
	In.ForwardSpeedCms = GetForwardSpeedCms();
	In.YawRateDegPerSec = FMath::RadiansToDegrees(static_cast<float>(Chassis->GetPhysicsAngularVelocityInRadians() | GetActorUpVector()));
	In.SlipDeg = TNBuggy::SlipAngleDeg(GetActorForwardVector(), Velocity);
	In.VerticalSpeedCms = static_cast<float>(Velocity.Z);
	In.bAirborne = bAirborne;
	In.bBoosting = IsBoosting();
	In.AddedTrauma = PendingCameraTrauma;
	PendingCameraTrauma = 0.f;
	CameraState = TNBuggy::AdvanceDriverCamera(CameraState, In, Tuning);

	const float Alpha = FMath::Clamp(static_cast<float>(Velocity.Size()) / Tuning.FullSpeedCms, 0.f, 1.f);
	Camera->SetFieldOfView(FMath::Lerp(Tuning.BaseFov, Tuning.MaxFov, Alpha) + CameraState.BoostFovDeg);
	SpringArm->TargetArmLength = FMath::Lerp(Tuning.BaseArmCm, Tuning.MaxArmCm, Alpha) + CameraState.ArmDeltaCm;
	SpringArm->SocketOffset = FVector(0.f, CameraState.LateralCm, Tuning.SocketHeightCm + CameraState.HeightDeltaCm);

	// La sacudida mueve la cámara sobre el extremo del brazo, sin tocar el brazo (su retardo no la suaviza).
	const float Time = static_cast<float>(GetWorld()->GetTimeSeconds());
	const FVector ShakeRot = TNBuggy::ShakeRotation(CameraState.Trauma, Time, Tuning.MaxShakeDeg);
	Camera->SetRelativeLocationAndRotation(TNBuggy::ShakeOffset(CameraState.Trauma, Time, Tuning.MaxShakeCm),
		FRotator(ShakeRot.X, ShakeRot.Y, CameraState.RollDeg));
}

void ATN_Buggy::AddCameraTrauma(float Amount)
{
	if (IsLocallyControlled() && IsPlayerControlled())
	{
		PendingCameraTrauma = FMath::Clamp(PendingCameraTrauma + FMath::Max(Amount, 0.f), 0.f, 1.f);
	}
}
