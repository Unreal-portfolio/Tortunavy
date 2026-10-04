// Ruedas del buggy sobre SK_TN_BuggyChassis (port de UHYBuggyWheelFront/Rear de HellYeah): radio y ancho de
// SM_TN_BuggyTire, suspensión blanda y con rebote, tracción trasera; el derrape largo es del freno de mano.
#pragma once

#include "CoreMinimal.h"
#include "ChaosVehicleWheel.h"
#include "TN_BuggyWheel.generated.h"

UCLASS()
class TORTUNABO_API UTN_BuggyWheelFront : public UChaosVehicleWheel
{
	GENERATED_BODY()

public:
	UTN_BuggyWheelFront();
};

/** Trasera: motriz y con freno de mano. */
UCLASS()
class TORTUNABO_API UTN_BuggyWheelRear : public UChaosVehicleWheel
{
	GENERATED_BODY()

public:
	UTN_BuggyWheelRear();
};
