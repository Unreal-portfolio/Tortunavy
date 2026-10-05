#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcSearchSpot.h"
#include "TN_SurvivalSearchSpot.generated.h"

/**
 * Decorado rebuscable de los mapas de Supervivencia (#724): el de siempre, con la suerte de la playa y la lista de objetos
 * de Supervivencia (TN_SurvivalLoot.h: los de siempre salvo el tótem y unos cuantos de la carrera, con los pesos de la
 * carrera según el puesto). Se puede volver a rebuscar TNSurvivalLoot::RefillSeconds después de cada resultado. Lo crea
 * ATN_ProcMapGenerator::SpawnSearchSpots en los mapas de Supervivencia.
 */
UCLASS()
class TORTUNABO_API ATN_SurvivalSearchSpot : public ATN_ProcSearchSpot
{
	GENERATED_BODY()

public:
	ATN_SurvivalSearchSpot();

protected:
	virtual bool PickLoot(FTN_InventoryItem& OutItem, const APawn* Searcher) const override;
};
