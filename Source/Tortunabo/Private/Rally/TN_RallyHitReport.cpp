#include "Rally/TN_RallyHitReport.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyPlayerController.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_RallyCombatLogic.h"

FText TNRallyHitLog::AmmoName(ETNRallyAmmo Ammo)
{
	switch (Ammo)
	{
	case ETNRallyAmmo::Coco: return NSLOCTEXT("Rally", "AmmoCoco", "Coco");
	case ETNRallyAmmo::Alga: return NSLOCTEXT("Rally", "AmmoAlga", "Alga");
	case ETNRallyAmmo::Burbuja: return NSLOCTEXT("Rally", "AmmoBurbuja", "Burbuja");
	case ETNRallyAmmo::Mortero: return NSLOCTEXT("Rally", "AmmoMortero", "Mortero");
	case ETNRallyAmmo::Tinta: return NSLOCTEXT("Rally", "AmmoTinta", "Tinta");
	case ETNRallyAmmo::Ancla: return NSLOCTEXT("Rally", "AmmoAncla", "Ancla");
	case ETNRallyAmmo::Concha: return NSLOCTEXT("Rally", "AmmoConcha", "Concha");
	case ETNRallyAmmo::ConchaGuiada: return NSLOCTEXT("Rally", "AmmoConchaGuiada", "Concha teledirigida");
	case ETNRallyAmmo::Erizos: return NSLOCTEXT("Rally", "AmmoErizos", "Ráfaga de erizos");
	case ETNRallyAmmo::Medusa: return NSLOCTEXT("Rally", "AmmoMedusa", "Medusa saltarina");
	case ETNRallyAmmo::Arpon: return NSLOCTEXT("Rally", "AmmoArpon", "Arpón");
	case ETNRallyAmmo::PezGlobo: return NSLOCTEXT("Rally", "AmmoPezGlobo", "Pez globo");
	default: return FText::GetEmpty();
	}
}

FText TNRallyHitLog::ZoneName(ETNRallyHitZone Zone)
{
	switch (Zone)
	{
	case ETNRallyHitZone::Front: return NSLOCTEXT("Rally", "HitZoneFront", "morro");
	case ETNRallyHitZone::Rear: return NSLOCTEXT("Rally", "HitZoneRear", "cola");
	default: return NSLOCTEXT("Rally", "HitZoneSide", "lateral");
	}
}

FText TNRallyHitLog::LineFor(const FTNRallyHitReport& Report, const FText& OtherName)
{
	const FText Where = Report.bBlocked ? NSLOCTEXT("Rally", "HitBlocked", "escudo") : ZoneName(Report.Zone);
	const FText Ammo = AmmoName(Report.Ammo);
	if (Report.bOutgoing)
	{
		return FText::Format(NSLOCTEXT("Rally", "HitOutgoing", "{0} → {1} · {2}"), Ammo, OtherName, Where);
	}
	return OtherName.IsEmpty()
		? FText::Format(NSLOCTEXT("Rally", "HitIncoming", "Te dan: {0} · {1}"), Ammo, Where)
		: FText::Format(NSLOCTEXT("Rally", "HitIncomingFrom", "Te da {0}: {1} · {2}"), OtherName, Ammo, Where);
}

TArray<TNRallyHitLog::FLine> TNRallyHitLog::Push(const TArray<FLine>& Lines, const FLine& Line)
{
	TArray<FLine> Out;
	Out.Reserve(MaxLines);
	Out.Add(Line);
	for (int32 Index = 0; Index < Lines.Num() && Out.Num() < MaxLines; ++Index)
	{
		Out.Add(Lines[Index]);
	}
	return Out;
}

TArray<TNRallyHitLog::FLine> TNRallyHitLog::Age(const TArray<FLine>& Lines, float Dt)
{
	TArray<FLine> Out;
	Out.Reserve(Lines.Num());
	for (const FLine& Line : Lines)
	{
		FLine Older = Line;
		Older.Age += FMath::Max(Dt, 0.f);
		if (Older.Age < LineSeconds)
		{
			Out.Add(Older);
		}
	}
	return Out;
}

bool TNRallyHitLog::ShowsMarker(const FTNRallyHitReport& Report)
{
	return Report.bOutgoing;
}

namespace TNRallyHitLogDetail
{
	ETNRallyHitZone ZoneOf(const ATN_Buggy& Victim, const FVector& WorldPoint)
	{
		const FVector Local = Victim.GetActorTransform().InverseTransformPosition(WorldPoint);
		switch (TNRallyCombat::ClassifyHitZone(Local, TNRallyCombat::DefaultHalfLengthCm, TNRallyCombat::DefaultHalfWidthCm))
		{
		case TNRallyCombat::EHitZone::Front: return ETNRallyHitZone::Front;
		case TNRallyCombat::EHitZone::Rear: return ETNRallyHitZone::Rear;
		default: return ETNRallyHitZone::Side;
		}
	}

	void SendToCrew(ATN_Buggy& Buggy, const FTNRallyHitReport& Report)
	{
		for (const ETNRallySeat Seat : { ETNRallySeat::Driver, ETNRallySeat::Gunner })
		{
			// Los bots no tienen pantalla: solo las jugadoras.
			if (ATN_RallyPlayerController* Player = Cast<ATN_RallyPlayerController>(Buggy.GetSeatController(Seat)))
			{
				Player->ClientRallyHitReport(Report);
			}
		}
	}
}

void TNRallyHitLog::NotifyServer(ATN_Buggy* Shooter, ATN_Buggy* Victim, ETNRallyAmmo Ammo, const FVector& WorldPoint, bool bBlocked)
{
	if (!Victim || !Victim->HasAuthority())
	{
		return;
	}
	const bool bSelfHit = Shooter == Victim;
	FTNRallyHitReport Report;
	Report.Ammo = Ammo;
	Report.Zone = TNRallyHitLogDetail::ZoneOf(*Victim, WorldPoint);
	Report.bBlocked = bBlocked;
	if (Shooter && !bSelfHit)
	{
		Report.bOutgoing = true;
		Report.OtherVehicle = Victim;
		TNRallyHitLogDetail::SendToCrew(*Shooter, Report);
	}
	Report.bOutgoing = false;
	Report.OtherVehicle = bSelfHit ? nullptr : Shooter;
	TNRallyHitLogDetail::SendToCrew(*Victim, Report);
	UE_LOG(LogTNRally, Log, TEXT("[RallyHit] %s → %s: %s en %s%s"), *GetNameSafe(Shooter), *GetNameSafe(Victim),
		*UEnum::GetValueAsString(Ammo), *UEnum::GetValueAsString(Report.Zone), bBlocked ? TEXT(" (escudo)") : TEXT(""));
}
