// Buggy del Rally de LVL_Rally (#631, decisión de #627): el de Karts (ATN_KartBuggy: mirada libre de la conductora con
// vuelta al centro y vista trasera, peso de la artillera en el giro) sin los objetos de Karts. Las cajas «?» le dan
// munición especial de la torreta (#629) y se dispara con los controles del buggy (la conductora sola, con el apuntado
// automático del Rally). Su artillera (ATN_RallyKartGunnerPawn) se inclina como la de Karts, sin las teclas de objeto.
#pragma once

#include "CoreMinimal.h"
#include "Kart/TN_KartBuggy.h"
#include "Kart/TN_KartGunnerPawn.h"
#include "TN_RallyKartBuggy.generated.h"

UCLASS()
class TORTUNABO_API ATN_RallyKartGunnerPawn : public ATN_KartGunnerPawn
{
	GENERATED_BODY()

public:
	ATN_RallyKartGunnerPawn();
};

UCLASS()
class TORTUNABO_API ATN_RallyKartBuggy : public ATN_KartBuggy
{
	GENERATED_BODY()

public:
	ATN_RallyKartBuggy();
};
