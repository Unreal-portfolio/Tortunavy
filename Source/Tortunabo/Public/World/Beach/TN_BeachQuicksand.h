#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachElement.h"
#include "TN_BeachQuicksand.generated.h"

class ATN_Quicksand;

/**
 * Arenas movedizas en la carrera (ETNBeachElement::Quicksand, #684). El generador la coloca como cualquier elemento
 * replicado; cada máquina crea en ApplySpec su ATN_Quicksand local (no replicado, como las zonas lentas), del radio de la
 * huella. Las decisiones (atrapar y soltar) las toma la del servidor; la ralentización, la de cada máquina con su tortuga.
 */
UCLASS()
class TORTUNABO_API ATN_BeachQuicksand : public ATN_BeachElement
{
	GENERATED_BODY()

public:
	ATN_BeachQuicksand();

	ATN_Quicksand* GetZone() const { return Zone; }

protected:
	virtual void ApplySpec() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** El charco de esta máquina (local). */
	UPROPERTY(Transient)
	TObjectPtr<ATN_Quicksand> Zone;
};
