#include "Rally/TN_RallyKartBuggy.h"

ATN_RallyKartGunnerPawn::ATN_RallyKartGunnerPawn()
{
	bItemControls = false;
}

ATN_RallyKartBuggy::ATN_RallyKartBuggy()
{
	bDriverItems = false;
	GunnerPawnClass = ATN_RallyKartGunnerPawn::StaticClass();
}
