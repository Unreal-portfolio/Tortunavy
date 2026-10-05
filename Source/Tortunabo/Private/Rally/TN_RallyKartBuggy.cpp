#include "Rally/TN_RallyKartBuggy.h"

ATN_RallyKartGunnerPawn::ATN_RallyKartGunnerPawn()
{
	bItemControls = false;
}

ATN_RallyKartBuggy::ATN_RallyKartBuggy()
{
	bDriverItems = false;
	// Sin la conducción de los karts (#742): el Rally de LVL_Rally conduce como siempre.
	bKartTuning = false;
	GunnerPawnClass = ATN_RallyKartGunnerPawn::StaticClass();
}
