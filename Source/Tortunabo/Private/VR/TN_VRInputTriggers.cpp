#include "VR/TN_VRInputTriggers.h"
#include "VR/TN_VRMath.h"

UTN_InputTriggerAnalogDown::UTN_InputTriggerAnalogDown()
{
	ActuationThreshold = TNVRMath::AnalogPressThreshold;
	ReleaseThreshold = TNVRMath::AnalogReleaseThreshold;
}

ETriggerState UTN_InputTriggerAnalogDown::UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue,
	float DeltaTime)
{
	// Como TNVRMath::AnalogButton: pulsado al pasar del umbral, suelto al bajar del de soltar (no al bajar del de pulsar).
	const float Magnitude = ModifiedValue.GetMagnitude();
	bHeldDown = bHeldDown ? Magnitude >= ReleaseThreshold : Magnitude >= ActuationThreshold;
	return bHeldDown ? ETriggerState::Triggered : ETriggerState::None;
}
