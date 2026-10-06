#pragma once

#include "CoreMinimal.h"
#include "World/TN_InteractableBase.h"
#include "TN_DirectInteractableBase.generated.h"

/**
 * @brief Variante de TN_InteractableBase para interacciones DIRECTAS con efecto único + cooldown.
 *        Provee CooldownSeconds anti-spam y un BP event OnDirectInteraction para implementar el efecto.
 *        Casos de uso: estaciones de cosméticos, sombrilla y tótem manual.
 */
UCLASS()
class TORTUNABO_API ATN_DirectInteractableBase : public ATN_InteractableBase
{
	GENERATED_BODY()

public:
	virtual bool CanInteract(APawn* Interactor) const override;
	virtual void Interact(APawn* Interactor) override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction")
	float CooldownSeconds = 0.25f;

	UPROPERTY(BlueprintReadOnly, Category = "Interaction")
	float LastInteractionServerTime = -1000.f;

	UFUNCTION(BlueprintImplementableEvent, Category = "Interaction")
	void OnDirectInteraction(APawn* Interactor);
};

