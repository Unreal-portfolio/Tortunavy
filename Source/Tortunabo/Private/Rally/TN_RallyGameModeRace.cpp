// ATN_RallyGameMode: progreso de la carrera en el servidor (salida anticipada, puertas, cajas, contramano, reaparición y
// puestos). Las reglas son las de TNRally (TN_RallyLogic.h).
#include "Rally/TN_RallyGameMode.h"

#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Kart/TN_KartItemBox.h"
#include "Rally/TN_RallyPlayerState.h"
#include "Rally/TN_RallyTrack.h"
#include "Rally/TN_RallyVehicle.h"
#include "World/TN_DeathZoneVolume.h"

namespace
{
	/** Un salto mayor entre dos fotogramas es un teletransporte: no suma al recorrido ni cruza puertas. */
	constexpr double RallyTeleportJumpCm = 5000.0;
	/** Antes de la salida el progreso se mide desde 100 m detrás de la puerta 0 (parrilla). */
	constexpr double RallyPreStartRefCm = 10000.0;
	/** Margen bajo la cota del agua para reaparecer. */
	constexpr double RallyWaterMarginCm = 50.0;
}

ETNRallyRespawnReason TNRallyRace::ResolveRespawn(const FRespawnSignals& Signals, bool bRacing, bool bFinished, bool bRetired,
	bool bImmune)
{
	if (Signals.bFellOutOfWorld)
	{
		return ETNRallyRespawnReason::Hazard;
	}
	if (Signals.bDestroyed)
	{
		return bRetired ? ETNRallyRespawnReason::None : ETNRallyRespawnReason::Destroyed;
	}
	if (Signals.bRequested && bRacing && !bFinished && !bRetired && !bImmune)
	{
		return ETNRallyRespawnReason::Request;
	}
	return ETNRallyRespawnReason::None;
}

void ATN_RallyGameMode::ConsumeRespawnRequests(bool bRacing)
{
	const double Time = Now();
	for (FTeamRuntime& Team : Teams)
	{
		ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Team.Vehicle.Get());
		if (!RallyVehicle)
		{
			continue;
		}
		// Las tres señales se consumen siempre: una que no toca ahora (R antes del verde, un reventón junto a una caída) no
		// se guarda para otro fotograma. Antes de la salida, la reaparición devuelve el buggy a su hueco.
		TNRallyRace::FRespawnSignals Signals;
		Signals.bFellOutOfWorld = RallyVehicle->ConsumeFellOutOfWorld();
		Signals.bDestroyed = RallyVehicle->ConsumeDestroyed();
		Signals.bRequested = RallyVehicle->ConsumeRespawnRequest();
		const ETNRallyRespawnReason Reason = TNRallyRace::ResolveRespawn(Signals, bRacing, Team.bFinished, Team.bRetired,
			Time < Team.ImmuneUntil);
		if (Reason != ETNRallyRespawnReason::None)
		{
			// En el podio no se reaparece en la pista: vuelve a su hueco.
			Team.bParked ? ParkTeam(Team) : RespawnTeam(Team, Reason);
		}
	}
}

void ATN_RallyGameMode::HoldBuggiesOnGrid()
{
	// El motor ya está cortado (par 0); el freno de carrera del vehículo frena de verdad en su hueco, sin tocar la
	// velocidad ni teletransportar, así que el buggy se asienta y no cae de nuevo (#289). StartRacing lo suelta. La
	// llamada se repite cada fotograma (el vehículo ignora la que no cambia nada) para que un equipo creado durante el
	// calentamiento también quede frenado.
	for (const FTeamRuntime& Team : Teams)
	{
		if (ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Team.Vehicle.Get()))
		{
			RallyVehicle->SetRaceBrakeHeld(true);
		}
	}
}

void ATN_RallyGameMode::TickProgress()
{
	const TNRally::FLapRules Rules = MakeLapRules();
	for (FTeamRuntime& Team : Teams)
	{
		APawn* Vehicle = Team.Vehicle.Get();
		if (!Cast<ITN_RallyVehicle>(Vehicle) || Team.bRetired)
		{
			continue;
		}
		const FVector Current = Vehicle->GetActorLocation();
		const FVector Previous = Team.PrevLocation;
		Team.PrevLocation = Current;
		if (Team.bFinished || FVector::DistSquared(Previous, Current) > FMath::Square(RallyTeleportJumpCm))
		{
			continue;
		}
		Team.OdometerCm += FVector::Dist(Previous, Current);

		const int32 NextGate = Rules.NextGateIndex(Team.GatesPassed);
		double Alpha = 0.0;
		bool bForward = false;
		const FTransform Gate = Track->GetGateCrossingTransform(NextGate);
		if (TNRally::SegmentCrossesGate(Previous, Current, Gate, Track->GetGateHalfExtent(), Alpha, bForward))
		{
			HandleGateCrossing(Team, NextGate, bForward, Alpha, GetWorld()->GetDeltaSeconds());
		}
		else if (UE_LOG_ACTIVE(LogTNRally, Verbose))
		{
			// Diagnóstico de puertas que no cuentan: cruza el plano de la puerta fuera de su rectángulo (#622, peraltes).
			const FVector A = Gate.InverseTransformPositionNoScale(Previous);
			const FVector B = Gate.InverseTransformPositionNoScale(Current);
			if (A.X < 0.0 && B.X >= 0.0)
			{
				const FVector Hit = FMath::Lerp(A, B, A.X / (A.X - B.X));
				UE_LOG(LogTNRally, Verbose, TEXT("[RallyGameMode] Equipo %d: cruza el plano de la puerta %d fuera de ella (lateral %.0f cm, altura %.0f cm sobre el centro)."),
					Team.TeamIndex, NextGate, Hit.Y, Hit.Z);
			}
		}
		CheckItemBoxes(Team, Previous, Current);
	}
}

void ATN_RallyGameMode::HandleGateCrossing(FTeamRuntime& Team, int32 GateIndex, bool bForward, double Alpha, float DeltaSeconds)
{
	const TNRally::FLapRules Rules = MakeLapRules();
	const double SplineBetween = Team.LastGate == INDEX_NONE ? 0.0 : Track->GetArcBetweenGates(Team.LastGate, GateIndex);
	const TNRally::EGateCheck Check = TNRally::CheckGate(GateIndex, Rules.NextGateIndex(Team.GatesPassed), bForward, Team.OdometerCm,
		SplineBetween, Team.GatesPassed > 0);
	if (Check != TNRally::EGateCheck::Valid)
	{
		UE_LOG(LogTNRally, Verbose, TEXT("[RallyGameMode] Equipo %d: puerta %d no cuenta (%s; recorrido %.0f m de %.0f m)."),
			Team.TeamIndex, GateIndex, Check == TNRally::EGateCheck::Shortcut ? TEXT("atajo") : TEXT("sentido contrario"),
			Team.OdometerCm / 100.0, SplineBetween / 100.0);
		return;
	}
	++Team.GatesPassed;
	Team.LastGate = GateIndex;
	Team.OdometerCm = 0.0;
	UE_LOG(LogTNRally, Verbose, TEXT("[RallyGameMode] Equipo %d: puerta %d (vuelta %d) a los %.1f s."), Team.TeamIndex, GateIndex,
		Rules.LapForGates(Team.GatesPassed), Now() - GetRallyGameState()->StartServerTime);
	if (Rules.IsFinished(Team.GatesPassed))
	{
		ATN_RallyGameState* RallyState = GetRallyGameState();
		// Hora del cruce interpolada dentro del fotograma.
		const double CrossTime = Now() - (1.0 - Alpha) * DeltaSeconds;
		Team.bFinished = true;
		Team.FinishSeconds = FMath::Max(0.0, CrossTime - RallyState->StartServerTime);
		Team.bWrongWay = false;
		NoteTeamFinished(Team);
		UE_LOG(LogTNRally, Log, TEXT("[RallyGameMode] Equipo %d en meta: %.2f s (%d.º en llegar)."), Team.TeamIndex, Team.FinishSeconds,
			Team.FinishOrder);
		if (RallyState->Phase == ETNRallyPhase::Racing)
		{
			StartFinishing();
		}
	}
	RebuildStandings();
}

void ATN_RallyGameMode::CheckItemBoxes(FTeamRuntime& Team, const FVector& From, const FVector& To)
{
	int32 Active = 0;
	for (const FTeamRuntime& Entry : Teams)
	{
		Active += Entry.bRetired ? 0 : 1;
	}
	const int32 Place = Team.Place > 0 ? Team.Place : 1;
	for (ATN_KartItemBox* Box : Track->GetItemBoxes())
	{
		if (IsValid(Box) && Box->IsAvailable()
			&& FMath::PointDistToSegment(Box->GetActorLocation(), From, To) <= Box->PickupRadiusCm)
		{
			Box->TryCollect(Team.Vehicle.Get(), Place, FMath::Max(1, Active));
		}
	}
}

void ATN_RallyGameMode::EvaluateTeams(double DeltaSeconds)
{
	for (FTeamRuntime& Team : Teams)
	{
		EvaluateTeam(Team, DeltaSeconds);
	}
}

void ATN_RallyGameMode::EvaluateTeam(FTeamRuntime& Team, double DeltaSeconds)
{
	const APawn* Vehicle = Team.Vehicle.Get();
	if (!Vehicle || Team.bRetired || Team.bFinished)
	{
		return;
	}
	const FVector Location = Vehicle->GetActorLocation();
	Team.Arc = Track->FindArcNear(Location, Team.Arc);
	UpdateSegmentProgress(Team);
	CountFlip(Team, *Vehicle, Location);

	if (Now() < Team.ImmuneUntil)
	{
		return;
	}
	const FVector Velocity = Vehicle->GetVelocity();
	const FVector Tangent = Track->GetDirectionAtArc(Team.Arc);
	switch (TNRally::UpdateWrongWay(Team.WrongWay, Velocity.GetSafeNormal() | Tangent, TNRally::CmsToKmh(Velocity.Size()), DeltaSeconds))
	{
	case TNRally::EWrongWayEvent::WarningOn:
		Team.bWrongWay = true;
		break;
	case TNRally::EWrongWayEvent::WarningOff:
		Team.bWrongWay = false;
		break;
	case TNRally::EWrongWayEvent::TurnAround:
		TurnAround(Team);
		return;
	default:
		break;
	}
	if (IsInHazard(Location))
	{
		RespawnTeam(Team, ETNRallyRespawnReason::Hazard);
		return;
	}
	if (TNRally::UpdateOffTrack(Team.OffTrack, FVector::Dist(Location, Track->GetLocationAtArc(Team.Arc)), DeltaSeconds))
	{
		RespawnTeam(Team, ETNRallyRespawnReason::OffTrack);
		return;
	}
	if (TNRally::UpdateStuck(Team.Stuck, Location, DeltaSeconds))
	{
		RespawnTeam(Team, ETNRallyRespawnReason::Stuck);
	}
}

void ATN_RallyGameMode::UpdateSegmentProgress(FTeamRuntime& Team) const
{
	// Arco recorrido desde la última puerta (desempate dentro de la misma puerta).
	const TNRally::FLapRules Rules = MakeLapRules();
	const double Length = Track->GetTrackLengthCm();
	const bool bClosed = Track->IsCircuit();
	const bool bStarted = Team.LastGate != INDEX_NONE;
	const double Reference = bStarted ? Track->GetGateArc(Team.LastGate) : Track->GetGateArc(0) - RallyPreStartRefCm;
	const double SegmentLength = bStarted
		? Track->GetArcBetweenGates(Team.LastGate, Rules.NextGateIndex(Team.GatesPassed)) : RallyPreStartRefCm;
	const double Progress = TNRally::ForwardArc(bClosed ? TNRally::WrapArc(Reference, Length, true) : Reference, Team.Arc, Length, bClosed);
	Team.SegmentProgressCm = Progress > SegmentLength + RallyTeleportJumpCm ? 0.0 : Progress;
}

void ATN_RallyGameMode::CountFlip(FTeamRuntime& Team, const APawn& Vehicle, const FVector& Location)
{
	const ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(&Vehicle);
	const bool bFlipped = RallyVehicle && RallyVehicle->IsFlipped();
	if (bFlipped && !Team.bWasFlipped)
	{
		++Team.Flips;
		UE_LOG(LogTNRally, Verbose, TEXT("[RallyGameMode] Equipo %d volcado en el arco %.0f m (%.0f km/h) en (%.0f, %.0f, %.0f)."),
			Team.TeamIndex, Team.Arc / 100.0, TNRally::CmsToKmh(Vehicle.GetVelocity().Size()), Location.X, Location.Y, Location.Z);
	}
	Team.bWasFlipped = bFlipped;
}

bool ATN_RallyGameMode::IsInHazard(const FVector& Location) const
{
	if (Track->HasWaterZ() && Location.Z < Track->GetWaterZ() - RallyWaterMarginCm)
	{
		return true;
	}
	// Las cajas de muerte del manifest (kill_boxes_uu) y las que haya en el nivel: dentro = reaparición (el volumen solo
	// sabe matar tortugas a pie a través de ATN_RunGameMode).
	for (TActorIterator<ATN_DeathZoneVolume> It(GetWorld()); It; ++It)
	{
		const UBoxComponent* Box = It->FindComponentByClass<UBoxComponent>();
		if (!Box)
		{
			continue;
		}
		const FVector Local = Box->GetComponentTransform().InverseTransformPosition(Location);
		const FVector Extent = Box->GetUnscaledBoxExtent();
		if (FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z)
		{
			return true;
		}
	}
	return false;
}

void ATN_RallyGameMode::RespawnTeam(FTeamRuntime& Team, ETNRallyRespawnReason Reason)
{
	ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Team.Vehicle.Get());
	if (!RallyVehicle)
	{
		return;
	}
	const bool bStarted = Team.LastGate != INDEX_NONE;
	const FTransform Where = ChooseRespawnTransform(Team);
	if (const APawn* Vehicle = Team.Vehicle.Get())
	{
		const FVector From = Vehicle->GetActorLocation();
		const FVector To = Where.GetLocation();
		UE_LOG(LogTNRally, Verbose, TEXT("[RallyGameMode] Equipo %d: estaba en (%.0f, %.0f, %.0f) a %.0f km/h con arriba.Z %.2f; va a (%.0f, %.0f, %.0f)."),
			Team.TeamIndex, From.X, From.Y, From.Z, RallyVehicle->GetForwardSpeedCms() * 0.036, Vehicle->GetActorUpVector().Z, To.X, To.Y, To.Z);
	}
	// Fantasma hasta medio segundo después del bloqueo: no recupera la colisión mientras sigue inmóvil (#103).
	RallyVehicle->RallyTeleport(Where, RespawnLockSeconds, TNRallyRace::EffectiveGhostSeconds(RespawnLockSeconds, RespawnGhostSeconds));

	const double Time = Now();
	Team.RespawnEndTime = Time + RespawnLockSeconds;
	Team.ImmuneUntil = Team.RespawnEndTime + 0.5;
	Team.LastRespawnReason = Reason;
	Team.LastRespawnTime = Time;
	Team.Arc = bStarted ? Track->GetGateArc(Team.LastGate) : Track->FindArcGlobal(Where.GetLocation());
	Team.PrevLocation = Where.GetLocation();
	Team.OdometerCm = 0.0;
	Team.bWrongWay = false;
	Team.WrongWay = TNRally::FWrongWayState();
	Team.Stuck = TNRally::FStuckState();
	Team.OffTrack = TNRally::FOffTrackState();

	++Team.Respawns[static_cast<uint8>(Reason)];
	static const TCHAR* ReasonNames[RespawnReasonCount] = { TEXT("ninguno"), TEXT("zona de muerte, agua o KillZ"), TEXT("fuera de pista"),
		TEXT("atasco"), TEXT("petición"), TEXT("reventado") };
	UE_LOG(LogTNRally, Verbose, TEXT("[RallyGameMode] Equipo %d reaparece en la puerta %d (%s) desde el arco %.0f m."), Team.TeamIndex,
		Team.LastGate, ReasonNames[static_cast<uint8>(Reason)], Team.Arc / 100.0);
	RebuildStandings();
	if (ATN_RallyGameState* RallyState = GetRallyGameState())
	{
		RallyState->NotifyTeamRespawned(Team.TeamIndex, Reason, static_cast<float>(Time));
		RallyState->ForceNetUpdate();
	}
}

FTransform ATN_RallyGameMode::ChooseRespawnTransform(const FTeamRuntime& Team) const
{
	if (Team.LastGate == INDEX_NONE)
	{
		return Team.GridTransform;
	}
	// Carril libre tras la puerta: otro buggy parado o reapareciendo en el mismo sitio no recibe a este encima (#103).
	TArray<FVector> Occupied;
	for (const FTeamRuntime& Other : Teams)
	{
		const APawn* OtherVehicle = Other.Vehicle.Get();
		if (OtherVehicle && Other.TeamIndex != Team.TeamIndex)
		{
			Occupied.Add(OtherVehicle->GetActorLocation());
		}
	}
	return Track->FindFreeRespawnTransform(Team.LastGate, Occupied, TNRallyRace::RestingLiftCm(Team.OriginAboveBottomCm));
}

void ATN_RallyGameMode::TurnAround(FTeamRuntime& Team)
{
	APawn* Vehicle = Team.Vehicle.Get();
	ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Vehicle);
	if (!RallyVehicle)
	{
		return;
	}
	// En el sitio, mirando hacia la tangente, sin espera (solo 1 s de fantasma).
	const FVector Location = Vehicle->GetActorLocation() + FVector(0.0, 0.0, 50.0);
	const FRotator Facing(0.0, Track->GetDirectionAtArc(Team.Arc).Rotation().Yaw, 0.0);
	RallyVehicle->RallyTeleport(FTransform(Facing, Location), 0.f, TurnAroundGhostSeconds);
	Team.ImmuneUntil = Now() + TurnAroundGhostSeconds;
	Team.PrevLocation = Location;
	Team.bWrongWay = false;
	++Team.TurnArounds;
	UE_LOG(LogTNRally, Verbose, TEXT("[RallyGameMode] Equipo %d girado: 4 s en contramano."), Team.TeamIndex);
}

void ATN_RallyGameMode::LogRaceStats(bool bTimedOut) const
{
	int32 Finished = 0;
	int32 Flips = 0;
	int32 TurnArounds = 0;
	int32 Respawns[RespawnReasonCount] = {};
	double Winner = 0.0;
	for (const FTeamRuntime& Team : Teams)
	{
		Finished += Team.bFinished ? 1 : 0;
		Flips += Team.Flips;
		TurnArounds += Team.TurnArounds;
		for (int32 Reason = 0; Reason < RespawnReasonCount; ++Reason)
		{
			Respawns[Reason] += Team.Respawns[Reason];
		}
		if (Team.bFinished && (Winner <= 0.0 || Team.FinishSeconds < Winner))
		{
			Winner = Team.FinishSeconds;
		}
	}
	const UTN_RallyRaceCounter* Counter = GetRaceCounter();
	UE_LOG(LogTNRally, Log,
		TEXT("[RallyStats] carrera %d variante %s: terminados %d/%d, atascos %d, vuelcos %d, caidas %d, fuera_de_pista %d, peticiones %d, reventados %d, giros %d, ganador %.1f s%s"),
		Counter ? Counter->RacesRun : 0, *Variant.ToString(), Finished, Teams.Num(), Respawns[static_cast<uint8>(ETNRallyRespawnReason::Stuck)], Flips,
		Respawns[static_cast<uint8>(ETNRallyRespawnReason::Hazard)], Respawns[static_cast<uint8>(ETNRallyRespawnReason::OffTrack)],
		Respawns[static_cast<uint8>(ETNRallyRespawnReason::Request)], Respawns[static_cast<uint8>(ETNRallyRespawnReason::Destroyed)], TurnArounds, Winner, bTimedOut ? TEXT(" (tope de tiempo)") : TEXT(""));
	for (const FTeamRuntime& Team : Teams)
	{
		if (!Team.bFinished)
		{
			UE_LOG(LogTNRally, Log, TEXT("[RallyStats]   sin llegar: equipo %d con %d puertas, arco %.0f m%s"), Team.TeamIndex,
				Team.GatesPassed, Team.Arc / 100.0, Team.bRetired ? TEXT(", retirado") : TEXT(""));
		}
	}
}

void ATN_RallyGameMode::RefreshSeats()
{
	ATN_RallyGameState* RallyState = GetRallyGameState();
	for (APlayerState* Player : RallyState->PlayerArray)
	{
		ATN_RallyPlayerState* RallyPlayer = Cast<ATN_RallyPlayerState>(Player);
		const AController* OwnerController = RallyPlayer ? Cast<AController>(RallyPlayer->GetOwner()) : nullptr;
		if (!RallyPlayer)
		{
			continue;
		}
		bool bSeated = false;
		for (const FTeamRuntime& Team : Teams)
		{
			const ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Team.Vehicle.Get());
			if (!RallyVehicle || !OwnerController)
			{
				continue;
			}
			for (const ETNRallySeat Seat : { ETNRallySeat::Driver, ETNRallySeat::Gunner })
			{
				if (RallyVehicle->GetSeatController(Seat) == OwnerController)
				{
					RallyPlayer->SetRallySeat(Team.TeamIndex, Seat);
					bSeated = true;
				}
			}
		}
		if (!bSeated)
		{
			RallyPlayer->ClearRallySeat();
		}
	}
}

void ATN_RallyGameMode::RebuildStandings()
{
	ATN_RallyGameState* RallyState = GetRallyGameState();
	if (!RallyState || !Track)
	{
		return;
	}
	RefreshSeats();
	const TNRally::FLapRules Rules = MakeLapRules();
	// Un equipo sin buggy (retirado por CleanupTeams) no sale en los puestos.
	TArray<int32> Listed;
	TArray<TNRally::FStandingKey> Keys;
	Keys.Reserve(Teams.Num());
	for (int32 TeamSlot = 0; TeamSlot < Teams.Num(); ++TeamSlot)
	{
		const FTeamRuntime& Team = Teams[TeamSlot];
		if (!Team.Vehicle.IsValid())
		{
			continue;
		}
		Listed.Add(TeamSlot);
		TNRally::FStandingKey Key;
		Key.Id = Team.TeamIndex;
		Key.bFinished = Team.bFinished;
		Key.FinishTime = Team.FinishSeconds;
		Key.Lap = Rules.LapForGates(Team.GatesPassed);
		Key.GatesPassed = Team.GatesPassed;
		Key.SegmentProgressCm = Team.SegmentProgressCm;
		Key.bRetired = Team.bRetired;
		Keys.Add(Key);
	}
	const TArray<int32> Order = TNRally::SortStandings(Keys);
	const double Time = Now();
	TArray<FTNRallyStanding> Standings;
	Standings.Reserve(Order.Num());
	for (int32 Rank = 0; Rank < Order.Num(); ++Rank)
	{
		FTeamRuntime& Team = Teams[Listed[Order[Rank]]];
		Team.Place = Rank + 1;
		const ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Team.Vehicle.Get());
		const AController* Driver = RallyVehicle ? RallyVehicle->GetSeatController(ETNRallySeat::Driver) : nullptr;
		const AController* Gunner = RallyVehicle ? RallyVehicle->GetSeatController(ETNRallySeat::Gunner) : nullptr;
		FTNRallyStanding& Entry = Standings.AddDefaulted_GetRef();
		Entry.TeamIndex = Team.TeamIndex;
		Entry.Vehicle = Team.Vehicle.Get();
		Entry.Driver = Driver ? Driver->PlayerState : nullptr;
		Entry.Gunner = Gunner ? Gunner->PlayerState : nullptr;
		Entry.Place = Team.Place;
		Entry.Lap = Rules.LapForGates(Team.GatesPassed);
		Entry.NextGate = Rules.NextGateIndex(Team.GatesPassed);
		Entry.FinishSeconds = static_cast<float>(Team.FinishSeconds);
		Entry.bFinished = Team.bFinished;
		Entry.bRetired = Team.bRetired;
		Entry.bWrongWay = Team.bWrongWay;
		Entry.bBot = Team.bBot;
		Entry.RespawnEndServerTime = Team.RespawnEndTime > Time ? static_cast<float>(Team.RespawnEndTime) : 0.f;
		Entry.LastRespawnReason = Team.LastRespawnReason;
		Entry.LastRespawnServerTime = static_cast<float>(Team.LastRespawnTime);
		Entry.Points = TNRally::PointsForPlace(Team.Place, Team.bFinished);
	}
	RallyState->Standings = MoveTemp(Standings);
}
