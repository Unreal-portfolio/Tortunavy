// Piloto IA de los karts: el del Rally (ATN_RallyAIController, que sigue la spline de la pista; en los karts, la línea que
// rodea los obstáculos del camino) con la velocidad y la puntería de la dificultad de la partida (ATN_KartGameMode::
// ConfigureBot). Solo en el servidor.
#pragma once

#include "CoreMinimal.h"
#include "Rally/TN_RallyAIController.h"
#include "TN_KartAIController.generated.h"

UCLASS()
class TORTUNABO_API ATN_KartAIController : public ATN_RallyAIController
{
	GENERATED_BODY()

public:
	virtual void PostInitializeComponents() override;
};
