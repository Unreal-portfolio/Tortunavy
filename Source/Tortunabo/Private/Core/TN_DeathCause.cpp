#include "Core/TN_DeathCause.h"
#include "World/ProcMap/TN_PathStorm.h"
#include "World/ProcMap/TN_ProcTraversalActors.h"
#include "World/ProcMap/TN_ProcWaterActors.h"
#include "World/TN_CrabActor.h"
#include "World/TN_EnemySeagull.h"
#include "World/TN_QuadActor.h"
#include "World/TN_SeagullDroppingActor.h"

FText TNDeathCause::Describe(ETNDeathCause Cause)
{
	switch (Cause)
	{
		case ETNDeathCause::Storm:           return NSLOCTEXT("TNHUD", "DeathCauseStorm", "Te ha alcanzado la tormenta");
		case ETNDeathCause::Fall:            return NSLOCTEXT("TNHUD", "DeathCauseFall", "Caída desde demasiado alto");
		case ETNDeathCause::KillVolume:      return NSLOCTEXT("TNHUD", "DeathCauseKillVolume", "Has caído a un foso mortal");
		case ETNDeathCause::Void:            return NSLOCTEXT("TNHUD", "DeathCauseVoid", "Has caído al vacío");
		case ETNDeathCause::DeathZone:       return NSLOCTEXT("TNHUD", "DeathCauseDeathZone", "Demasiado tiempo en zona mortal");
		case ETNDeathCause::Water:           return NSLOCTEXT("TNHUD", "DeathCauseWater", "Has caído al agua");
		case ETNDeathCause::Crab:            return NSLOCTEXT("TNHUD", "DeathCauseCrab", "Te ha atrapado un cangrejo");
		case ETNDeathCause::Seagull:         return NSLOCTEXT("TNHUD", "DeathCauseSeagull", "Te ha cazado una gaviota");
		case ETNDeathCause::SeagullDropping: return NSLOCTEXT("TNHUD", "DeathCauseSeagullDropping", "Te ha caído una caca de gaviota");
		case ETNDeathCause::Quad:            return NSLOCTEXT("TNHUD", "DeathCauseQuad", "Te ha atropellado un quad");
		case ETNDeathCause::Whirlpool:       return NSLOCTEXT("TNHUD", "DeathCauseWhirlpool", "Te ha tragado un remolino");
		case ETNDeathCause::Predator:        return NSLOCTEXT("TNHUD", "DeathCausePredator", "Te ha comido un depredador marino");
		case ETNDeathCause::Bleedout:        return NSLOCTEXT("TNHUD", "DeathCauseBleedout", "Nadie llegó a reanimarte");
		default:                             return NSLOCTEXT("TNHUD", "ResultsRankEliminated", "Eliminado");
	}
}

ETNDeathCause TNDeathCause::FromInstigator(const AActor* KillInstigator, const AActor* Victim)
{
	if (!KillInstigator)
	{
		return ETNDeathCause::Unknown;
	}
	// La propia tortuga solo se mata al aterrizar de una caída larga (ATortugaCharacter::Landed).
	if (KillInstigator == Victim)
	{
		return ETNDeathCause::Fall;
	}
	if (KillInstigator->IsA<ATN_PathStorm>())            { return ETNDeathCause::Storm; }
	if (KillInstigator->IsA<ATN_ProcKillVolume>())       { return ETNDeathCause::KillVolume; }
	if (KillInstigator->IsA<ATN_ProcWhirlpool>())        { return ETNDeathCause::Whirlpool; }
	if (KillInstigator->IsA<ATN_ProcWaterPredator>())    { return ETNDeathCause::Predator; }
	if (KillInstigator->IsA<ATN_CrabActor>())            { return ETNDeathCause::Crab; }
	if (KillInstigator->IsA<ATN_EnemySeagull>())         { return ETNDeathCause::Seagull; }
	if (KillInstigator->IsA<ATN_SeagullDroppingActor>()) { return ETNDeathCause::SeagullDropping; }
	if (KillInstigator->IsA<ATN_QuadActor>())            { return ETNDeathCause::Quad; }
	return ETNDeathCause::Unknown;
}
