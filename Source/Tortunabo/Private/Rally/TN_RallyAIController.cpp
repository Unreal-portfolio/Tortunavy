#include "Rally/TN_RallyAIController.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Rally/TN_RallyGameState.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyTrack.h"
#include "Rally/TN_RallyVehicle.h"

namespace
{
	/** Distancia extra hasta la tangente con la que se mide la curva que viene (cm) y segundos de velocidad que se suman. */
	constexpr double RallyAICornerProbeCm = 3000.0;
	constexpr double RallyAICornerProbeSeconds = 1.0;
	/** Con el acelerador a fondo y a menos de esta velocidad durante RallyAIStuckSeconds, marcha atrás RallyAIReverseSeconds. */
	constexpr float RallyAIStuckKmh = 3.f;
	constexpr float RallyAIStuckSeconds = 2.f;
	constexpr double RallyAIReverseSeconds = 1.5;
	/** Un salto mayor es una reaparición: el arco se vuelve a buscar en toda la pista. */
	constexpr double RallyAIJumpCm = 3000.0;
	/** Coseno del cono de disparo hacia delante. */
	constexpr double RallyAIFireConeCos = 0.6;
	/** Más lejos del eje está fuera de la calzada (14 m de calzada y arcén): vuelve a ella mirando cerca y despacio. */
	constexpr double RallyAIOffRoadCm = 900.0;
	constexpr double RallyAIRejoinLookAheadCm = 1500.0;
	constexpr float RallyAIRejoinKmh = 35.f;
	/** Pendiente que se mide por delante y cuánto baja la velocidad objetivo por cada unidad de bajada (12 % -> x0,7). */
	constexpr double RallyAIGradeProbeCm = 2000.0;
	constexpr double RallyAIDownhillSlowdown = 2.5;
	constexpr double RallyAIMinDownhillFactor = 0.6;
	/** Tras reaparecer, el buggy está inmóvil 3 s: no es un atasco. */
	constexpr double RallyAITeleportGraceSeconds = 4.0;
	/** Una marcha atrás a menos de esto de la anterior alarga la siguiente, hasta RallyAIReverseMaxSeconds. */
	constexpr double RallyAIReverseMemorySeconds = 12.0;
	constexpr double RallyAIReverseMaxSeconds = 4.0;
}

ATN_RallyAIController::ATN_RallyAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 1.f / 30.f;
}

void ATN_RallyAIController::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// PlayerState propio (como AAIController con bWantsPlayerState): nombre en la tabla de puestos y en el HUD.
	if (IsValid(this) && GetNetMode() != NM_Client)
	{
		InitPlayerState();
		if (PlayerState)
		{
			PlayerState->SetIsABot(true);
		}
	}
}

ATN_RallyTrack* ATN_RallyAIController::ResolveTrack()
{
	if (!CachedTrack.IsValid())
	{
		const ATN_RallyGameState* RallyState = GetWorld() ? GetWorld()->GetGameState<ATN_RallyGameState>() : nullptr;
		CachedTrack = RallyState ? RallyState->GetTrack() : nullptr;
	}
	return CachedTrack.Get();
}

void ATN_RallyAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(GetPawn());
	const ATN_RallyGameState* RallyState = GetWorld() ? GetWorld()->GetGameState<ATN_RallyGameState>() : nullptr;
	ATN_RallyTrack* Track = ResolveTrack();
	if (!HasAuthority() || !RallyVehicle || !RallyState || !Track || !Track->IsBuilt())
	{
		return;
	}
	if (RallyState->Phase != ETNRallyPhase::Racing && RallyState->Phase != ETNRallyPhase::Finishing)
	{
		RallyVehicle->SetAIDriveInput(0.f, 1.f, 0.f, true);
		return;
	}
	Drive(DeltaSeconds, *Track);
}

void ATN_RallyAIController::Drive(float DeltaSeconds, ATN_RallyTrack& Track)
{
	APawn* Vehicle = GetPawn();
	ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Vehicle);
	const FVector Location = Vehicle->GetActorLocation();
	const FVector Forward = Vehicle->GetActorForwardVector();
	const double Time = GetWorld()->GetTimeSeconds();
	if (!bHasArc || FVector::Dist(Location, LastLocation) > RallyAIJumpCm)
	{
		Arc = Track.FindArcGlobal(Location);
		bHasArc = true;
		// Reaparición o salida: empieza de cero.
		IgnoreSlowUntil = Time + RallyAITeleportGraceSeconds;
		ReverseUntil = 0.0;
		SlowSeconds = 0.f;
		ReverseStreak = 0;
	}
	else
	{
		Arc = Track.FindArcNear(Location, Arc);
	}
	LastLocation = Location;

	if (RallyVehicle->IsFlipped())
	{
		// Volcado: el buggy se endereza solo a los 4 s.
		RallyVehicle->SetAIDriveInput(0.f, 0.f, 0.f, false);
		return;
	}

	const double SpeedCms = FMath::Abs(RallyVehicle->GetForwardSpeedCms());
	const float SpeedKmh = static_cast<float>(TNRally::CmsToKmh(SpeedCms));
	// Fuera de la calzada (un golpe, un charco o una explosión): mira a un punto del eje 15 m por delante y va despacio,
	// para volver en diagonal por el talud en vez de subirlo de frente.
	const bool bOffRoad = FVector::Dist2D(Location, Track.GetLocationAtArc(Arc)) > RallyAIOffRoadCm;
	const double LookAhead = bOffRoad ? RallyAIRejoinLookAheadCm : LookAheadBaseCm + SpeedCms * LookAheadSeconds;
	const FVector Target = Track.GetLocationAtArc(Arc + LookAhead);
	const float Steer = TNRally::SteerToward(Forward, Target - Location, SteerSaturationDeg);
	const float CornerKmh = TNRally::CornerSpeedKmh(Track.GetDirectionAtArc(Arc),
		Track.GetDirectionAtArc(Arc + RallyAICornerProbeCm + SpeedCms * RallyAICornerProbeSeconds), MaxSpeedKmh, MinCornerSpeedKmh);
	// Cuesta abajo se frena peor: menos velocidad objetivo (en las bajadas con curva de E01B se salían por fuera).
	const double Grade = (Track.GetLocationAtArc(Arc + RallyAIGradeProbeCm).Z - Track.GetLocationAtArc(Arc).Z) / RallyAIGradeProbeCm;
	const double Downhill = FMath::Clamp(1.0 + RallyAIDownhillSlowdown * FMath::Min(0.0, Grade), RallyAIMinDownhillFactor, 1.0);
	const float TargetKmh = bOffRoad ? FMath::Min(CornerKmh, RallyAIRejoinKmh) : static_cast<float>(CornerKmh * Downhill);

	if (Time < ReverseUntil)
	{
		// Marcha atrás con la dirección invertida (en Chaos, frenar parado da marcha atrás).
		RallyVehicle->SetAIDriveInput(0.f, 1.f, -Steer, false);
		return;
	}
	const float Throttle = SpeedKmh < TargetKmh ? 1.f : 0.25f;
	const float Brake = SpeedKmh > TargetKmh + 12.f ? 0.8f : 0.f;
	SlowSeconds = (Throttle > 0.5f && SpeedKmh < RallyAIStuckKmh && Time >= IgnoreSlowUntil) ? SlowSeconds + DeltaSeconds : 0.f;
	if (SlowSeconds > RallyAIStuckSeconds)
	{
		SlowSeconds = 0.f;
		ReverseStreak = Time - LastReverseTime < RallyAIReverseMemorySeconds ? ReverseStreak + 1 : 0;
		LastReverseTime = Time;
		ReverseUntil = Time + FMath::Min(RallyAIReverseSeconds * (1 + ReverseStreak), RallyAIReverseMaxSeconds);
		UE_LOG(LogTNRally, Verbose, TEXT("[RallyAI] %s marcha atrás en (%.0f, %.0f, %.0f), arco %.0f m, a %.0f m del eje, giro %.2f, arriba.Z %.2f"),
			*GetNameSafe(Vehicle), Location.X, Location.Y, Location.Z, Arc / 100.0,
			FVector::Dist(Location, Track.GetLocationAtArc(Arc)) / 100.0, Steer, Vehicle->GetActorUpVector().Z);
	}
	RallyVehicle->SetAIDriveInput(Throttle, Brake, Steer, false);
	TryFire(Location, Forward);
}

void ATN_RallyAIController::TryFire(const FVector& Location, const FVector& Forward)
{
	const double Time = GetWorld()->GetTimeSeconds();
	const ATN_RallyGameState* RallyState = GetWorld()->GetGameState<ATN_RallyGameState>();
	ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(GetPawn());
	if (Time < NextFireTime || !RallyState || !RallyVehicle)
	{
		return;
	}
	// Con una persona de artillera la torreta es suya: el piloto solo conduce. Si la plaza se queda libre, vuelve a disparar.
	if (Cast<APlayerController>(RallyVehicle->GetSeatController(ETNRallySeat::Gunner)))
	{
		return;
	}
	const FTNRallyStanding* Mine = RallyState->FindStandingForVehicle(GetPawn());
	if (!Mine || Mine->bFinished || Mine->Place <= 1 || !RallyState->Standings.IsValidIndex(Mine->Place - 2))
	{
		return;
	}
	const APawn* Ahead = RallyState->Standings[Mine->Place - 2].Vehicle;
	if (!Ahead)
	{
		return;
	}
	const FVector ToTarget = Ahead->GetActorLocation() - Location;
	const FVector Direction = ToTarget.GetSafeNormal();
	if (ToTarget.SizeSquared() > FMath::Square(FireRangeCm) || (Direction | Forward) < RallyAIFireConeCos)
	{
		return;
	}
	const bool bSpecial = RallyVehicle->GetSpecialAmmo() != ETNRallyAmmo::None && FMath::FRand() < SpecialFireChance;
	RallyVehicle->AIFire(Direction, bSpecial);
	NextFireTime = Time + FireIntervalSeconds;
}
