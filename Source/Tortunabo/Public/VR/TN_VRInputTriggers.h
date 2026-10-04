#pragma once

#include "CoreMinimal.h"
#include "InputTriggers.h"
#include "TN_VRInputTriggers.generated.h"

/**
 * Disparador «Down» con histéresis para los gatillos de los Touch por su eje (OpenXR solo da su valor): se activa a partir
 * de ActuationThreshold (55 %) y sigue activo hasta bajar de ReleaseThreshold (35 %). Con un «Down» sin histéresis, un
 * gatillo que ronda el 55 % corta y vuelve a empezar las interacciones de mantener (rebuscar, cofres). Lo pone
 * ATN_VRRig::MakeAnalogPressTrigger en las asignaciones de IA_Interact e IA_OpenChatWheel.
 */
UCLASS(NotBlueprintable, MinimalAPI, meta = (DisplayName = "Pulsado analógico con histéresis (VR)"))
class UTN_InputTriggerAnalogDown : public UInputTrigger
{
	GENERATED_BODY()

public:
	UTN_InputTriggerAnalogDown();

	/** Por debajo de esto deja de estar pulsado (una vez pulsado). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger Settings", meta = (ClampMin = "0"))
	float ReleaseThreshold = 0.35f;

	virtual ETriggerEventsSupported GetSupportedTriggerEvents() const override { return ETriggerEventsSupported::Instant; }

	/** ¿Está pulsado ahora (lo que decidió el último UpdateState)? */
	bool IsHeldDown() const { return bHeldDown; }

protected:
	virtual ETriggerState UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime) override;

private:
	bool bHeldDown = false;
};
