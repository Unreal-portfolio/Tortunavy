#include "Kart/TN_KartAIController.h"

#include "Engine/World.h"
#include "Kart/TN_KartGameMode.h"

void ATN_KartAIController::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (ATN_KartGameMode* KartMode = GetWorld() ? GetWorld()->GetAuthGameMode<ATN_KartGameMode>() : nullptr)
	{
		KartMode->ConfigureBot(*this);
	}
}
