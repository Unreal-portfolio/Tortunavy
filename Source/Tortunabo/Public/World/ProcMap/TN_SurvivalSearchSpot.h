#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcSearchSpot.h"
#include "TN_SurvivalSearchSpot.generated.h"

/**
 * Decorado rebuscable de los mapas de Supervivencia (#724): el de siempre, con la suerte de la playa y la lista de objetos
 * de Supervivencia (TN_SurvivalLoot.h: los de siempre salvo el tótem y unos cuantos de la carrera, con los pesos de la
 * carrera según el puesto). Cada tortuga lo puede rebuscar una vez: quien va primera no deja vacíos los de las demás. Lo
 * crea ATN_ProcMapGenerator::SpawnSearchSpots en los mapas de Supervivencia.
 */
UCLASS()
class TORTUNABO_API ATN_SurvivalSearchSpot : public ATN_ProcSearchSpot
{
	GENERATED_BODY()

public:
	ATN_SurvivalSearchSpot();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Si la tortuga de PlayerId ya lo ha rebuscado. */
	bool WasSearchedBy(int32 PlayerId) const { return SearchedBy.Contains(PlayerId); }

protected:
	virtual bool PickLoot(FTN_InventoryItem& OutItem, const APawn* Searcher) const override;
	virtual void OnSearchStateChanged(const FTNSearchSpotState& OldState) override;
	virtual bool IsSpentFor(const APawn* Interactor) const override;
	virtual bool IsSpentForLocalView() const override;

private:
	/** PlayerId de las tortugas que ya lo han rebuscado (lo apunta el servidor al acabar cada búsqueda). */
	UPROPERTY(Replicated)
	TArray<int32> SearchedBy;
};
