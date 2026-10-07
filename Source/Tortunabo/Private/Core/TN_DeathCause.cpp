#include "Core/TN_DeathCause.h"
#include "World/TN_CrabActor.h"
#include "World/TN_SeagullDroppingActor.h"
#include "World/TN_ThrowableItemActor.h"

FText TNDeathCause::Describe(ETNDeathCause Cause)
{
	switch (Cause)
	{
		case ETNDeathCause::Storm:           return NSLOCTEXT("TNHUD", "DeathCauseStorm", "Te ha alcanzado la tormenta");
		case ETNDeathCause::Fall:            return NSLOCTEXT("TNHUD", "DeathCauseFall", "Caída desde demasiado alto");
		case ETNDeathCause::Void:            return NSLOCTEXT("TNHUD", "DeathCauseVoid", "Has caído al vacío");
		case ETNDeathCause::DeathZone:       return NSLOCTEXT("TNHUD", "DeathCauseDeathZone", "Demasiado tiempo en zona mortal");
		case ETNDeathCause::Water:           return NSLOCTEXT("TNHUD", "DeathCauseWater", "Has caído al agua");
		case ETNDeathCause::Crab:            return NSLOCTEXT("TNHUD", "DeathCauseCrab", "Te ha atrapado un cangrejo");
		case ETNDeathCause::Seagull:         return NSLOCTEXT("TNHUD", "DeathCauseSeagull", "Te ha cazado una gaviota");
		case ETNDeathCause::SeagullDropping: return NSLOCTEXT("TNHUD", "DeathCauseSeagullDropping", "Te ha caído una caca de gaviota");
		case ETNDeathCause::Quad:            return NSLOCTEXT("TNHUD", "DeathCauseQuad", "Te ha atropellado un quad");
		case ETNDeathCause::Bleedout:        return NSLOCTEXT("TNHUD", "DeathCauseBleedout", "Nadie llegó a reanimarte");
		case ETNDeathCause::ThrownItem:      return NSLOCTEXT("TNHUD", "DeathCauseThrownItem", "Te ha dado un objeto lanzado");
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
	if (KillInstigator->IsA<ATN_CrabActor>())            { return ETNDeathCause::Crab; }
	if (KillInstigator->IsA<ATN_ThrowableItemActor>())   { return ETNDeathCause::ThrownItem; }
	if (KillInstigator->IsA<ATN_SeagullDroppingActor>()) { return ETNDeathCause::SeagullDropping; }
	return ETNDeathCause::Unknown;
}
