#include "World/Beach/TN_BeachToyTank.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachCritterSynth.h"
#include "World/Beach/TN_BeachStun.h"
#include "TN_BeachCritterKit.h"
#include "TN_BeachCritterMeshes.h"
#include "TN_BeachEnemyKit.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_CombatTuning.h"

namespace TNBeachTank
{
	/** Estados del tanque (Mover.State). */
	enum class EState : uint8 { Patrol, Turn, Engage, Stunned };

	inline uint8 ToByte(EState E)
	{
		return static_cast<uint8>(E);
	}

	/** Tramo de patrulla sin Extent (cm, por el tamaño). */
	constexpr float DefaultPatrol = 2400.f;
	/** Marcha (cm/s y cm/s²) y giro sobre sí mismo en las puntas (grados/s). */
	constexpr float DriveSpeed = 260.f;
	constexpr float DriveAccel = 300.f;
	constexpr float PivotRate = 150.f;
	/** Ve a 25 m y deja de apuntar a 29 m; si se queda sin nadie este tiempo, vuelve a patrullar. */
	constexpr float DetectRadius = 2500.f;
	constexpr float LoseRadius = 2900.f;
	constexpr float LoseGrace = 1.2f;
	constexpr float ScanPeriod = 0.2f;
	/** Torreta (grados/s), tolerancia para disparar, primer disparo y recarga (s). */
	constexpr float TurretRate = 110.f;
	constexpr float AimTolerance = 7.f;
	constexpr float FirstShotDelay = 0.7f;
	constexpr float ReloadTime = 4.f;
	/** Bolita: velocidad de salida (cm/s) y parte del adelanto a la tortuga (la gravedad, en UTN_CombatTuning). */
	constexpr float MuzzleSpeed = 1900.f;
	constexpr float LeadFactor = 0.6f;
	/** Golpe: holgura (cm) y empujón (cm/s) de lado y hacia arriba (el mareo en bola, en UTN_CombatTuning). */
	constexpr float HitPad = 70.f;
	constexpr float HitPush = 520.f;
	constexpr float HitUp = 260.f;
	/** La bolita dura esto como mucho y deja de botar tras tantos botes. */
	constexpr float FoamLife = 2.4f;
	constexpr int32 MaxBounces = 3;
	/** Retroceso: lo que recula el cañón (cm, por el tamaño) y el cabeceo del casco (grados). */
	constexpr float RecoilDist = 45.f;
	constexpr float HullKick = 5.f;
}

ATN_BeachToyTank::ATN_BeachToyTank()
{
	NetFrequencyNear = 10.f;
	SetNetUpdateFrequency(NetFrequencyNear);
	bThrottleWhenFar = true;
}

float ATN_BeachToyTank::GetBodyRadius() const
{
	return 240.f * SizeK;
}

float ATN_BeachToyTank::GetActiveRange() const
{
	return PatrolHalf + TNBeachTank::DetectRadius + 1500.f;
}

bool ATN_BeachToyTank::GetHitCapsule(FVector& OutA, FVector& OutB, float& OutRadius) const
{
	const TNBeachCritterMeshes::FTankDims D = TNBeachCritterMeshes::TankDims();
	const FQuat Q(FRotator(0.f, ShownYaw, 0.f));
	const double Z = D.HullTop * 0.55 * SizeK;
	OutA = ShownLoc + Q.RotateVector(FVector(-D.HalfLength * 0.7 * SizeK, 0.0, Z));
	OutB = ShownLoc + Q.RotateVector(FVector(D.HalfLength * 0.7 * SizeK, 0.0, Z));
	OutRadius = 110.f * SizeK;
	return true;
}

FVector ATN_BeachToyTank::GetHitStunAnchor() const
{
	const TNBeachCritterMeshes::FTankDims D = TNBeachCritterMeshes::TankDims();
	return ShownLoc + FVector(0.0, 0.0, (D.HullTop + 2.4 * TNBeach::Scale) * SizeK + 150.0);
}

float ATN_BeachToyTank::GetHitStunScale() const
{
	return 3.f * SizeK;
}

void ATN_BeachToyTank::ApplySpec()
{
	SizeK = FMath::Clamp(Spec.SizeScale, 0.8f, 1.2f);
	const TNBeachCritterMeshes::FTankDims D = TNBeachCritterMeshes::TankDims();
	const float Total = Spec.Extent > 100.f ? Spec.Extent : TNBeachTank::DefaultPatrol * SizeK;
	PatrolHalf = FMath::Max(300.f, Total * 0.5f - static_cast<float>(D.HalfLength) * SizeK * 0.6f);
	GroundZ = static_cast<float>(GetActorLocation().Z);
	BuildTank();
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachToyTank::EnsurePatrol()
{
	if (bPlaced)
	{
		return;
	}
	bPlaced = true;
	FVector Axis = GetActorForwardVector();
	Axis.Z = 0.0;
	PatrolAxis = Axis.Normalize() ? Axis : FVector::ForwardVector;
	PatrolCenter = GetActorLocation();
	float Z = GroundZ;
	if (GroundHeightAt(PatrolCenter, Z))
	{
		GroundZ = Z;
		PatrolCenter.Z = Z;
	}
	PatrolSign = 1.f;
	ServerTurretYaw = static_cast<float>(PatrolAxis.Rotation().Yaw);
	ServerMoveTo(PatrolCenter, ServerTurretYaw);
	ServerSetState(TNBeachTank::ToByte(TNBeachTank::EState::Patrol), PatrolCenter + PatrolAxis * PatrolHalf);
}

ATortugaCharacter* ATN_BeachToyTank::ScanTarget(float Radius) const
{
	TArray<ATortugaCharacter*> Turtles;
	GatherTurtles(this, Turtles);
	ATortugaCharacter* Best = nullptr;
	double BestSq = FMath::Square(static_cast<double>(Radius));
	for (ATortugaCharacter* Turtle : Turtles)
	{
		if (!IsTargetable(Turtle))
		{
			continue;
		}
		const FVector At = Turtle->GetActorLocation();
		const double DistSq = FVector::DistSquared2D(At, SimLoc);
		if (DistSq < BestSq && FMath::Abs(At.Z - SimLoc.Z) < 800.0)
		{
			BestSq = DistSq;
			Best = Turtle;
		}
	}
	return Best;
}

void ATN_BeachToyTank::DriveToward(const FVector& Goal, float MaxSpeed, float DeltaSeconds, bool bBrake)
{
	FVector Flat = Goal - SimLoc;
	Flat.Z = 0.0;
	const float Dist = static_cast<float>(Flat.Size());
	float Wanted = MaxSpeed;
	if (bBrake)
	{
		// Frena para llegar parado (así la última réplica va despacio y los clientes no se pasan).
		Wanted = FMath::Min(Wanted, FMath::Sqrt(2.f * TNBeachTank::DriveAccel * FMath::Max(0.f, Dist - 5.f)));
	}
	DriveSpeed = FMath::FInterpConstantTo(DriveSpeed, Wanted, DeltaSeconds, TNBeachTank::DriveAccel);
	if (Dist < 1.f || DriveSpeed < 0.5f)
	{
		return;
	}
	FVector Next = SimLoc + Flat / Dist * FMath::Min(Dist, DriveSpeed * DeltaSeconds);
	GroundTimer -= DeltaSeconds;
	if (GroundTimer <= 0.f)
	{
		GroundTimer = 0.15f;
		float Z = GroundZ;
		if (GroundHeightAt(Next, Z))
		{
			GroundZ = Z;
		}
	}
	Next.Z = FMath::FInterpTo(SimLoc.Z, static_cast<double>(GroundZ), static_cast<double>(DeltaSeconds), 8.0);
	ServerMoveTo(Next, SimYaw);
}

float ATN_BeachToyTank::SolveElevation(float Dx, float Dz) const
{
	const float V2 = TNBeachTank::MuzzleSpeed * TNBeachTank::MuzzleSpeed;
	const float G = UTN_CombatTuning::Get().ToyTankFoamGravity;
	const float Disc = V2 * V2 - G * (G * Dx * Dx + 2.f * Dz * V2);
	if (Disc < 0.f || Dx < 1.f)
	{
		return 40.f;
	}
	// El tiro bajo (el más rápido) de los dos que llegan.
	return FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan((V2 - FMath::Sqrt(Disc)) / (G * Dx))), -12.f, 45.f);
}

FVector ATN_BeachToyTank::MuzzleAt(const FVector& Base, float HullYaw, float TurretYaw, float Pitch) const
{
	const TNBeachCritterMeshes::FTankDims D = TNBeachCritterMeshes::TankDims();
	const FVector PivotW = Base + FRotator(0.f, HullYaw, 0.f).RotateVector(D.TurretPivot * SizeK);
	const FVector BarrelBase = PivotW + FRotator(0.f, TurretYaw, 0.f).RotateVector(D.BarrelPivot * SizeK);
	return BarrelBase + FRotator(Pitch, TurretYaw, 0.f).Vector() * ((D.BarrelLength + 0.4 * TNBeach::Scale) * SizeK);
}

void ATN_BeachToyTank::Fire(ATortugaCharacter* Victim)
{
	const double Now = ServerNow(this);
	// Adelanto a donde irá la tortuga (una parte, para que se pueda esquivar cambiando de dirección).
	const FVector Guess = MuzzleAt(SimLoc, SimYaw, ServerTurretYaw, 10.f);
	FVector TargetAt = Victim->GetActorLocation();
	const float Flight = static_cast<float>(FVector::Dist2D(Guess, TargetAt)) / (TNBeachTank::MuzzleSpeed * 0.9f);
	FVector Lead = Victim->GetVelocity() * (Flight * TNBeachTank::LeadFactor);
	Lead.Z = 0.0;
	TargetAt += Lead;
	FVector To = TargetAt - Guess;
	const float ShotYaw = static_cast<float>(FVector(To.X, To.Y, 0.0).Rotation().Yaw);
	const float Pitch = SolveElevation(static_cast<float>(To.Size2D()), static_cast<float>(To.Z));
	const FVector MouthAt = MuzzleAt(SimLoc, SimYaw, ShotYaw, Pitch);
	const FVector Velocity = FRotator(Pitch, ShotYaw, 0.f).Vector() * TNBeachTank::MuzzleSpeed;
	float LandZ = static_cast<float>(TargetAt.Z) - 70.f;
	GroundHeightAt(TargetAt, LandZ);

	const uint8 Id = NextShotId++;
	FTNTankShot& Shot = Shots[Id % MaxShots];
	Shot = FTNTankShot();
	Shot.bAlive = true;
	Shot.bArmed = true;
	Shot.Id = Id;
	Shot.P0 = MouthAt;
	Shot.V0 = Velocity;
	Shot.T0 = Now;
	Shot.Born = Now;
	Shot.Origin = MouthAt;
	Shot.GroundFrom = static_cast<float>(SimLoc.Z);
	Shot.GroundTo = LandZ;
	Shot.AimDist = FMath::Max(100.f, static_cast<float>(FVector::Dist2D(MouthAt, TargetAt)));
	Shot.Pos = MouthAt;
	Shot.LastPos = MouthAt;
	ServerTurretYaw = ShotYaw;
	ReloadLeft = TNBeachTank::ReloadTime;
	MulticastFire(Id, MouthAt, Velocity, static_cast<float>(Now), LandZ);
}

void ATN_BeachToyTank::BounceOffTurtle(FTNTankShot& Shot, const FVector& Where, double Now)
{
	const float T = static_cast<float>(FMath::Max(0.0, Now - Shot.T0));
	const FVector Vel = Shot.V0 + FVector(0.0, 0.0, -UTN_CombatTuning::Get().ToyTankFoamGravity * T);
	const float Progress = FMath::Clamp(static_cast<float>(FVector::Dist2D(Where, Shot.Origin)) / Shot.AimDist, 0.f, 1.f);
	const float Ground = FMath::Lerp(Shot.GroundFrom, Shot.GroundTo, Progress);
	Shot.P0 = Where;
	Shot.V0 = -Vel * 0.22f + FVector(0.0, 0.0, 260.0);
	Shot.T0 = Now;
	Shot.bArmed = false;
	Shot.Origin = Where;
	Shot.GroundFrom = Ground;
	Shot.GroundTo = Ground;
	Shot.AimDist = 1000.f;
}

void ATN_BeachToyTank::AdvanceShots(double Now, bool bServer)
{
	const float Radius = static_cast<float>(TNBeachCritterMeshes::TankDims().FoamRadius) * SizeK;
	const float FoamGravity = UTN_CombatTuning::Get().ToyTankFoamGravity;
	TArray<ATortugaCharacter*> Turtles;
	bool bGathered = false;
	for (FTNTankShot& Shot : Shots)
	{
		if (!Shot.bAlive)
		{
			continue;
		}
		if (Now - Shot.Born > TNBeachTank::FoamLife)
		{
			Shot.bAlive = false;
			continue;
		}
		const float T = static_cast<float>(FMath::Max(0.0, Now - Shot.T0));
		FVector P = Shot.P0 + Shot.V0 * T + FVector(0.0, 0.0, -0.5 * FoamGravity * T * T);
		const FVector Vel = Shot.V0 + FVector(0.0, 0.0, -FoamGravity * T);
		// Suelo sin trazas: entre el de la boca del cañón y el de donde apuntaba.
		const float Progress = FMath::Clamp(static_cast<float>(FVector::Dist2D(P, Shot.Origin)) / Shot.AimDist, 0.f, 1.f);
		const float Ground = FMath::Lerp(Shot.GroundFrom, Shot.GroundTo, Progress);
		// Lo que hay por medio (una pared, una muralla, una fortaleza, el decorado o una duna): la bolita se para en ello y rebota
		// en vez de cruzarlo (en todas las máquinas, con el mismo barrido). Antes, solo el suelo sin trazas la paraba.
		FHitResult WallHit;
		const bool bWall = SweepFoam(Shot.Pos, P, Radius, WallHit);
		if (bWall)
		{
			P = WallHit.Location + WallHit.ImpactNormal;
		}
		else if (P.Z - Radius < Ground && Vel.Z < 0.0)
		{
			// Bota: pierde fuerza, pero sigue mareando a quien pase hasta que da a alguien o se acaba.
			P.Z = Ground + Radius;
			Shot.P0 = P;
			Shot.V0 = FVector(Vel.X * 0.55, Vel.Y * 0.55, -Vel.Z * 0.42);
			Shot.T0 = Now;
			++Shot.Bounces;
			if (Shot.Bounces > TNBeachTank::MaxBounces || Shot.V0.SizeSquared() < 80.0 * 80.0)
			{
				Shot.bAlive = false;
			}
		}
		Shot.LastPos = Shot.Pos;
		Shot.Pos = P;
		// A quién da (servidor): solo con lo recorrido hasta la pared.
		if (bServer && Shot.bArmed && IsRaceLive(this))
		{
			if (!bGathered)
			{
				bGathered = true;
				GatherTurtles(this, Turtles);
			}
			if (HitTurtleOnSegment(Shot, Turtles, Radius, Vel, Now))
			{
				continue;
			}
		}
		if (bWall)
		{
			// Rebota en la pared, pierde fuerza y ya no marea a nadie.
			Shot.P0 = P;
			Shot.V0 = TNBeachTankFoam::BounceOffWall(Vel, WallHit.ImpactNormal);
			Shot.T0 = Now;
			Shot.bArmed = false;
			Shot.Origin = P;
			Shot.GroundFrom = FMath::Min(Ground, static_cast<float>(P.Z) - Radius);
			Shot.GroundTo = Shot.GroundFrom;
			Shot.AimDist = 1000.f;
			++Shot.Bounces;
			if (Shot.Bounces > TNBeachTank::MaxBounces || Shot.V0.SizeSquared() < 80.0 * 80.0)
			{
				Shot.bAlive = false;
			}
		}
	}
}

bool ATN_BeachToyTank::HitTurtleOnSegment(FTNTankShot& Shot, const TArray<ATortugaCharacter*>& Turtles, float Radius, const FVector& Velocity, double Now)
{
	for (ATortugaCharacter* Turtle : Turtles)
	{
		if (!IsTargetable(Turtle))
		{
			continue;
		}
		const FVector At = Turtle->GetActorLocation();
		if (FMath::PointDistToSegment(At, Shot.LastPos, Shot.Pos) > Radius + TNBeachTank::HitPad)
		{
			continue;
		}
		// Con algo entre la bolita y la tortuga (la pared en la que acaba de chocar, una muralla fina), no le da: la holgura del
		// golpe no pasa a través de las paredes.
		FHitResult Between;
		if (SweepFoam(FMath::ClosestPointOnSegment(At, Shot.LastPos, Shot.Pos), At, 5.f, Between))
		{
			continue;
		}
		// Empuja hacia donde iba la bolita y marea un poco (en bola).
		const FVector Launch = Velocity.GetSafeNormal2D() * TNBeachTank::HitPush + FVector(0.0, 0.0, TNBeachTank::HitUp);
		StunTurtle(Turtle, UTN_CombatTuning::Get().ToyTankHitStunSeconds, Launch);
		BounceOffTurtle(Shot, Shot.Pos, Now);
		MulticastFoamHit(Shot.Id, Shot.Pos, Turtle);
		return true;
	}
	return false;
}

bool ATN_BeachToyTank::SweepFoam(const FVector& From, const FVector& To, float Radius, FHitResult& OutHit) const
{
	UWorld* World = GetWorld();
	if (!World || From.Equals(To, 0.1))
	{
		return false;
	}
	// Como un objeto que se mueve por el mundo (canal WorldDynamic): la arena, el decorado, las fortalezas y los muros. Sin las
	// tortugas ni los demás cuerpos (tipo Pawn: los enemigos, este tanque) ni las bolas de caparazón: a las tortugas se les da
	// con su propia cuenta. Lo que ya tocaba al salir (la boca del cañón pegada a una pared) no cuenta.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BeachTankFoam), false, this);
	Params.bFindInitialOverlaps = false;
	FCollisionResponseParams Response(ECR_Block);
	Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	Response.CollisionResponse.SetResponse(ECC_PhysicsBody, ECR_Ignore);
	return World->SweepSingleByChannel(OutHit, From, To, FQuat::Identity, ECC_WorldDynamic, FCollisionShape::MakeSphere(Radius), Params, Response);
}

void ATN_BeachToyTank::ServerTick(float DeltaSeconds)
{
	using TNBeachTank::EState;
	using TNBeachTank::ToByte;
	EnsurePatrol();
	const EState State = static_cast<EState>(GetMoverState());
	const float Age = GetStateAge();
	const bool bStunned = IsHitStunned();
	const bool bLive = IsRaceLive(this);
	ReloadLeft -= DeltaSeconds;
	const FVector Goal = PatrolCenter + PatrolAxis * (PatrolHalf * PatrolSign);

	if (bStunned && State != EState::Stunned)
	{
		// Mareado: se para en seco (y la torreta se queda donde estaba).
		DriveSpeed = 0.f;
		Target = nullptr;
		ServerSetState(ToByte(EState::Stunned), SimLoc);
	}
	else
	{
		// Busca tortuga mientras patrulla o gira.
		if (bLive && (State == EState::Patrol || State == EState::Turn))
		{
			ScanTimer -= DeltaSeconds;
			if (ScanTimer <= 0.f)
			{
				ScanTimer = TNBeachTank::ScanPeriod;
				if (ATortugaCharacter* Seen = ScanTarget(TNBeachTank::DetectRadius))
				{
					Target = Seen;
					LoseTimer = 0.f;
					ServerSetState(ToByte(EState::Engage), Seen->GetActorLocation());
				}
			}
		}
		switch (static_cast<EState>(GetMoverState()))
		{
		case EState::Patrol:
			DriveToward(Goal, TNBeachTank::DriveSpeed * FMath::Sqrt(SizeK), DeltaSeconds, true);
			if (FVector::Dist2D(SimLoc, Goal) < 20.0)
			{
				// Punta del tramo: gira sobre sí mismo para volver.
				TurnGoalYaw = FRotator::NormalizeAxis(SimYaw + 180.f);
				ServerSetState(ToByte(EState::Turn), Goal);
			}
			break;

		case EState::Turn:
		{
			const float NewYaw = TNBeachCritterKit::TurnToward(SimYaw, TurnGoalYaw, TNBeachTank::PivotRate * DeltaSeconds);
			ServerMoveTo(SimLoc, NewYaw);
			if (FMath::Abs(FMath::FindDeltaAngleDegrees(NewYaw, TurnGoalYaw)) < 0.5f)
			{
				PatrolSign = -PatrolSign;
				ServerSetState(ToByte(EState::Patrol), PatrolCenter + PatrolAxis * (PatrolHalf * PatrolSign));
			}
			break;
		}

		case EState::Engage:
		{
			// Se para (frenando) y apunta con la torreta.
			DriveToward(Goal, 0.f, DeltaSeconds, false);
			ATortugaCharacter* Victim = Target.Get();
			if (!bLive || !IsTargetable(Victim) || FVector::Dist2D(Victim->GetActorLocation(), SimLoc) > TNBeachTank::LoseRadius)
			{
				Victim = bLive ? ScanTarget(TNBeachTank::DetectRadius) : nullptr;
				Target = Victim;
			}
			if (!Victim)
			{
				LoseTimer += DeltaSeconds;
				if (LoseTimer > TNBeachTank::LoseGrace)
				{
					ServerSetState(ToByte(EState::Patrol), Goal);
				}
				break;
			}
			LoseTimer = 0.f;
			const float EngageAge = GetStateAge();
			const FVector At = Victim->GetActorLocation();
			ServerSetAim(At);
			FVector To = At - SimLoc;
			To.Z = 0.0;
			const float WantYaw = static_cast<float>(To.Rotation().Yaw);
			ServerTurretYaw = TNBeachCritterKit::TurnToward(ServerTurretYaw, WantYaw, TNBeachTank::TurretRate * DeltaSeconds);
			if (EngageAge >= TNBeachTank::FirstShotDelay && ReloadLeft <= 0.f && DriveSpeed < 30.f
				&& FMath::Abs(FMath::FindDeltaAngleDegrees(ServerTurretYaw, WantYaw)) < TNBeachTank::AimTolerance)
			{
				Fire(Victim);
			}
			break;
		}

		case EState::Stunned:
		default:
			if (!bStunned && Age > 0.3f)
			{
				ReloadLeft = FMath::Max(ReloadLeft, TNBeachTank::FirstShotDelay);
				ServerSetState(ToByte(EState::Patrol), Goal);
			}
			break;
		}
	}
	AdvanceShots(ServerNow(this), true);

	if (IsDebugDraw())
	{
		UWorld* World = GetWorld();
		const FVector Lift(0.0, 0.0, 50.0);
		DrawDebugLine(World, PatrolCenter - PatrolAxis * PatrolHalf + Lift, PatrolCenter + PatrolAxis * PatrolHalf + Lift, FColor::Orange, false, -1.f, 0, 10.f);
		DrawDebugCircle(World, SimLoc + Lift, TNBeachTank::DetectRadius, 48, FColor::Yellow, false, -1.f, 0, 8.f, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
		DrawDebugDirectionalArrow(World, SimLoc + FVector(0.0, 0.0, 250.0), SimLoc + FVector(0.0, 0.0, 250.0) + FRotator(0.f, ServerTurretYaw, 0.f).Vector() * 600.0,
			120.f, FColor::Red, false, -1.f, 0, 8.f);
		for (const FTNTankShot& Shot : Shots)
		{
			if (Shot.bAlive)
			{
				DrawDebugSphere(World, Shot.Pos, 30.f, 8, Shot.bArmed ? FColor::Red : FColor::White, false, -1.f, 0, 3.f);
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Efectos (todas las máquinas)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachToyTank::MulticastFire_Implementation(uint8 ShotId, FVector_NetQuantize Origin, FVector_NetQuantize10 Velocity, float ServerTime, float LandZ)
{
	const FVector MouthAt = Origin;
	const FVector Vel = Velocity;
	if (!HasAuthority())
	{
		// La bolita del cliente sale con la hora del servidor: la parábola va a la par que la de allí.
		FTNTankShot& Shot = Shots[ShotId % MaxShots];
		Shot = FTNTankShot();
		Shot.bAlive = true;
		Shot.bArmed = true;
		Shot.Id = ShotId;
		Shot.P0 = MouthAt;
		Shot.V0 = Vel;
		Shot.T0 = ServerTime;
		Shot.Born = ServerTime;
		Shot.Origin = MouthAt;
		Shot.GroundFrom = static_cast<float>(ShownLoc.Z);
		Shot.GroundTo = LandZ;
		// Donde toca el suelo de llegada (para repartir el suelo entre la boca y allí).
		const float Vz = static_cast<float>(Vel.Z);
		const float Drop = static_cast<float>(MouthAt.Z) - LandZ;
		const float FoamGravity = UTN_CombatTuning::Get().ToyTankFoamGravity;
		const float TLand = (Vz + FMath::Sqrt(FMath::Max(0.f, Vz * Vz + 2.f * FoamGravity * Drop))) / FoamGravity;
		Shot.AimDist = FMath::Max(100.f, static_cast<float>(Vel.Size2D()) * TLand);
		Shot.Pos = MouthAt;
		Shot.LastPos = MouthAt;
	}
	if (!bHasScreen)
	{
		return;
	}
	// Retroceso, torreta y cañón donde ha salido el tiro, humo en la boca y «¡pomp!».
	RecoilAge = 0.f;
	TurretYawShown = static_cast<float>(Vel.Rotation().Yaw);
	BarrelPitchShown = static_cast<float>(Vel.Rotation().Pitch);
	const FVector Dir = Vel.GetSafeNormal();
	TNBeachKit::BurstAt(Muzzle, MouthAt, Dir, 10);
	if (Sound && LocalViewDistance(this, MouthAt) < 7000.f)
	{
		Sound->Play(ETNBeachCritterSfx::FoamPop, 1.f / FMath::Sqrt(SizeK), 1.1f);
	}
	UTN_BeachCameraShake::Kick(this, MouthAt, 0.15f, 400.f, 1500.f);
}

void ATN_BeachToyTank::MulticastFoamHit_Implementation(uint8 ShotId, FVector_NetQuantize Where, ATortugaCharacter* Victim)
{
	const FVector At = Where;
	if (!HasAuthority())
	{
		FTNTankShot& Shot = Shots[ShotId % MaxShots];
		if (Shot.bAlive && Shot.Id == ShotId)
		{
			BounceOffTurtle(Shot, At, ServerNow(this));
		}
	}
	if (!bHasScreen)
	{
		return;
	}
	TNBeachKit::BurstAt(Trail, At, FVector::UpVector, 10);
	if (Sound && LocalViewDistance(this, At) < 7000.f)
	{
		Sound->Play(ETNBeachCritterSfx::FoamHit, 1.f, 1.2f);
	}
	if (Victim && Victim->IsLocallyControlled())
	{
		UTN_BeachCameraShake::Kick(this, At, 0.3f, 300.f, 1200.f);
	}
	ShowPop(NSLOCTEXT("TNBeach", "TankFoamHit", "¡PAF!"), FColor(255, 150, 40), At + FVector(0.0, 0.0, 200.0), 140.f);
}

void ATN_BeachToyTank::OnMoverStateChanged(uint8 OldState)
{
	using TNBeachTank::EState;
	if (!bHasScreen || !Sound)
	{
		return;
	}
	const EState State = static_cast<EState>(GetMoverState());
	const bool bNear = LocalViewDistance(this, ShownLoc) < 6000.f;
	if (!bNear)
	{
		return;
	}
	if (State == EState::Stunned)
	{
		Sound->Play(ETNBeachCritterSfx::Sputter, 1.f, 1.f);
	}
	else if (static_cast<EState>(OldState) == EState::Stunned)
	{
		// Se le pasa el mareo: la antena vuelve de golpe con su muelle.
		Sound->Play(ETNBeachCritterSfx::Boing, 1.f, 0.9f);
	}
	else if (State == EState::Engage)
	{
		Sound->Play(ETNBeachCritterSfx::Servo, 1.f, 0.7f);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Visual
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachToyTank::BuildTank()
{
	if (Block)
	{
		return;
	}
	const TNBeachCritterMeshes::FTankDims D = TNBeachCritterMeshes::TankDims();
	USceneComponent* RigRoot = MakeRig();

	// Cuerpo sólido en todas las máquinas: la tortuga no lo atraviesa (como el cangrejo gigante).
	Block = NewObject<UBoxComponent>(this, NAME_None, RF_Transient | RF_DuplicateTransient);
	Block->SetupAttachment(RigRoot);
	Block->SetBoxExtent(FVector(D.HalfLength, D.TrackY + D.TrackHalfWidth, D.HullTop * 0.5) * SizeK, false);
	Block->SetRelativeLocation(FVector(0.0, 0.0, D.HullTop * 0.5 * SizeK));
	Block->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Block->SetCollisionObjectType(ECC_Pawn);
	Block->SetCollisionResponseToAllChannels(ECR_Ignore);
	Block->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Block->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Block->SetCanEverAffectNavigation(false);
	Block->SetGenerateOverlapEvents(false);
	Block->RegisterComponent();
	// La bola del caparazón no choca con él en la física: la saca el servidor por un lado (movido sin barrido, la hundía).
	RegisterSolidBlock(Block);

	if (!bHasScreen)
	{
		return;
	}
	using TNProcMesh::FTNProcMeshBuffers;
	const int32 Pal = ((Spec.Seed % 4) + 4) % 4;
	const TNBeachCritterMeshes::FTankLook Look = TNBeachCritterMeshes::TankPalette(Pal);
	const FString Key = FString::Printf(TEXT("Beach.Tank.%d."), Pal);
	const uint32 CamoSeed = static_cast<uint32>(Pal) * 29u + 7u;
	HullRoot = TNBeachCritterKit::AddPivot(this, RigRoot, FVector::ZeroVector);
	HullRoot->SetRelativeScale3D(FVector(SizeK));
	UStaticMesh* HullM = TNBeachKit::CachedMesh(Key + TEXT("Hull"), [&Look, CamoSeed](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildTankHull(M, Look, CamoSeed); });
	UStaticMesh* WheelM = TNBeachKit::CachedMesh(Key + TEXT("Wheel"), [&Look](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildTankWheel(M, Look); });
	UStaticMesh* TurretM = TNBeachKit::CachedMesh(Key + TEXT("Turret"), [&Look, CamoSeed](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildTankTurret(M, Look, CamoSeed); });
	UStaticMesh* BarrelM = TNBeachKit::CachedMesh(Key + TEXT("Barrel"), [&Look](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildTankBarrel(M, Look); });
	UStaticMesh* AntennaM = TNBeachKit::CachedMesh(Key + TEXT("Antenna"), [&Look](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildTankAntenna(M, Look); });
	UStaticMesh* FlagM = TNBeachKit::CachedMesh(TEXT("Beach.Tank.Flag"), [](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildTankFlag(M); });
	UStaticMesh* FoamM = TNBeachKit::CachedMesh(TEXT("Beach.Tank.Foam"), [](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildFoamBall(M); });

	Hull = TNBeachCritterKit::AddPart(this, HullRoot, HullM, FVector::ZeroVector, true, TN_ART("Beach.ToyTank.Hull"));
	Wheels = TNBeachCritterKit::AddInstances(this, HullRoot, WheelM, 10, false, false, TN_ART("Beach.ToyTank.Wheel"));
	WheelXf.SetNum(10);
	for (int32 k = 0; k < 10; ++k)
	{
		WheelXf[k] = FTransform(FQuat::Identity, TNBeachCritterMeshes::TankWheelAt(k), FVector::OneVector);
	}
	TNBeachCritterKit::WriteInstances(Wheels, WheelXf, false);
	TurretPivot = TNBeachCritterKit::AddPivot(this, HullRoot, D.TurretPivot);
	Turret = TNBeachCritterKit::AddPart(this, TurretPivot, TurretM, FVector::ZeroVector, true, TN_ART("Beach.ToyTank.Turret"));
	BarrelPivot = TNBeachCritterKit::AddPivot(this, TurretPivot, D.BarrelPivot);
	Barrel = TNBeachCritterKit::AddPart(this, BarrelPivot, BarrelM, FVector::ZeroVector, true, TN_ART("Beach.ToyTank.Barrel"));
	AntennaPivot = TNBeachCritterKit::AddPivot(this, TurretPivot, D.AntennaPivot);
	Antenna = TNBeachCritterKit::AddPart(this, AntennaPivot, AntennaM, FVector::ZeroVector, false, TN_ART("Beach.ToyTank.Antenna"));
	Flag = TNBeachCritterKit::AddPart(this, AntennaPivot, FlagM, FVector(0.0, 0.0, D.AntennaLength - 0.6 * TNBeach::Scale), false, TN_ART("Beach.ToyTank.Flag"));
	FoamBalls = TNBeachCritterKit::AddInstances(this, GetRootComponent(), FoamM, MaxShots, true, false, TN_ART("Beach.ToyTank.FoamBall"));
	FoamXf.Init(FTransform(FQuat::Identity, GetActorLocation(), FVector::ZeroVector), MaxShots);

	using TNAmbientFX::EShape;
	TNAmbientFX::FEmitterDesc MuzzleDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.94f, 0.94f, 0.9f), true, 0.6f, 24, 0.f, 420.f, 0.f, 0.4f, 0.8f, 50.f, 170.f);
	MuzzleDesc.Spread = 0.5f;
	MuzzleDesc.Drag = 2.5f;
	MuzzleDesc.SpawnRadius = 15.f;
	TNBeachKit::InitEmitter(Muzzle, this, MuzzleDesc, static_cast<uint32>(Spec.Seed) + 61u);
	TNAmbientFX::FEmitterDesc TrailDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(1.f, 0.97f, 0.92f), true, 0.55f, 72, 0.f, 20.f, 0.f, 0.35f, 0.55f, 28.f, 8.f);
	TrailDesc.Spread = 0.3f;
	TrailDesc.SpawnRadius = 6.f;
	TNBeachKit::InitEmitter(Trail, this, TrailDesc, static_cast<uint32>(Spec.Seed) + 67u);
	TNAmbientFX::FEmitterDesc SmokeDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.42f, 0.42f, 0.44f), true, 0.6f, 40, 0.f, 120.f, 0.f, 1.4f, 2.4f, 60.f, 230.f);
	SmokeDesc.Buoyancy = 140.f;
	SmokeDesc.SpawnRadius = 20.f;
	TNBeachKit::InitEmitter(Smoke, this, SmokeDesc, static_cast<uint32>(Spec.Seed) + 71u);
	TNAmbientFX::FEmitterDesc DustDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.87f, 0.78f, 0.58f), true, 0.45f, 30, 0.f, 90.f, 0.f, 0.6f, 1.2f, 50.f, 150.f);
	DustDesc.Buoyancy = 30.f;
	DustDesc.SpawnRadius = 120.f;
	TNBeachKit::InitEmitter(Dust, this, DustDesc, static_cast<uint32>(Spec.Seed) + 73u);
	Sound = UTN_BeachCritterSynthComponent::AttachTo(this, HullRoot, 800.f, 6000.f);
	LastLocShown = GetActorLocation();
	LastYawShown = static_cast<float>(GetActorRotation().Yaw);
	TurretYawShown = LastYawShown;
}

void ATN_BeachToyTank::PoseTank(float DeltaSeconds, bool bStunned)
{
	using TNBeachTank::EState;
	const TNBeachCritterMeshes::FTankDims D = TNBeachCritterMeshes::TankDims();
	const EState State = static_cast<EState>(GetMoverState());
	const float Dt = FMath::Max(DeltaSeconds, 1e-3f);
	const FVector HullFwd = FRotator(0.f, ShownYaw, 0.f).Vector();

	// Marcha vista: avance a lo largo del casco y giro, para las ruedas, el polvo y la antena.
	FVector Delta = ShownLoc - LastLocShown;
	Delta.Z = 0.0;
	LastLocShown = ShownLoc;
	const float Forward = static_cast<float>(FVector::DotProduct(Delta, HullFwd));
	const float YawStep = FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(LastYawShown, ShownYaw));
	LastYawShown = ShownYaw;
	const float PrevSpeed = SpeedShown;
	SpeedShown = TNBeachCritterKit::Ease(SpeedShown, Forward / Dt, DeltaSeconds, 0.1f);
	const float Accel = (SpeedShown - PrevSpeed) / Dt;
	const float WheelR = static_cast<float>(D.WheelRadius) * SizeK;
	const float TrackY = static_cast<float>(D.TrackY) * SizeK;
	if (FMath::Abs(Forward) > 0.01f || FMath::Abs(YawStep) > 1e-4f)
	{
		// Al girar sobre sí mismo, cada oruga va para un lado.
		WheelSpinL += (Forward + YawStep * TrackY) / WheelR;
		WheelSpinR += (Forward - YawStep * TrackY) / WheelR;
		for (int32 k = 0; k < 10; ++k)
		{
			const float Spin = k < 5 ? WheelSpinL : WheelSpinR;
			WheelXf[k] = FTransform(FRotator(-FMath::RadiansToDegrees(Spin), 0.f, 0.f), TNBeachCritterMeshes::TankWheelAt(k), FVector::OneVector);
		}
		TNBeachCritterKit::WriteInstances(Wheels, WheelXf, false);
	}

	// Inclinación del casco por el suelo (cuatro consultas cada 0,2 s) y el cabeceo del retroceso.
	TiltTimer -= DeltaSeconds;
	if (TiltTimer <= 0.f)
	{
		TiltTimer = 0.2f;
		const FVector Side(-HullFwd.Y, HullFwd.X, 0.0);
		const double Reach = D.HalfLength * 0.8 * SizeK;
		float ZF = static_cast<float>(ShownLoc.Z);
		float ZB = ZF;
		float ZL = ZF;
		float ZR = ZF;
		GroundHeightAt(ShownLoc + HullFwd * Reach, ZF);
		GroundHeightAt(ShownLoc - HullFwd * Reach, ZB);
		GroundHeightAt(ShownLoc - Side * TrackY, ZL);
		GroundHeightAt(ShownLoc + Side * TrackY, ZR);
		TiltPitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(ZF - ZB, static_cast<float>(Reach * 2.0))), -18.f, 18.f);
		TiltRoll = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(ZL - ZR, TrackY * 2.f)), -15.f, 15.f);
	}
	RecoilAge += DeltaSeconds;
	const float Kick = RecoilAge < 0.8f ? FMath::Exp(-RecoilAge / 0.16f) : 0.f;
	const float KickPitch = TNBeachTank::HullKick * Kick * FMath::Cos(RecoilAge * 18.f);
	const float Shiver = bStunned ? 2.f * FMath::Sin(VisualClock * 31.f) : 0.f;
	const FVector TurretDir = FRotator(0.f, TurretYawShown - ShownYaw, 0.f).Vector();
	HullRoot->SetRelativeTransform(FTransform(FRotator(TiltPitch + KickPitch * TurretDir.X + Shiver, 0.f, TiltRoll + KickPitch * TurretDir.Y * 0.6f),
		-TurretDir * (20.0 * Kick * SizeK), FVector(SizeK)));

	// Torreta: hacia la tortuga que apunta; si no, al frente. Mareado, cabecea sola.
	float WantYaw = ShownYaw;
	float WantPitch = 0.f;
	if (State == EState::Engage)
	{
		FVector To = FVector(Mover.Aim) - ShownLoc;
		WantYaw = static_cast<float>(FVector(To.X, To.Y, 0.0).Rotation().Yaw);
		WantPitch = SolveElevation(static_cast<float>(To.Size2D()), static_cast<float>(To.Z) - D.TurretPivot.Z * SizeK);
	}
	if (bStunned)
	{
		WantYaw = TurretYawShown + 40.f * FMath::Sin(VisualClock * 2.1f) * Dt;
		WantPitch = -6.f + 4.f * FMath::Sin(VisualClock * 3.f);
	}
	const float PrevTurret = TurretYawShown;
	TurretYawShown = TNBeachCritterKit::TurnToward(TurretYawShown, WantYaw, TNBeachTank::TurretRate * 1.3f * DeltaSeconds);
	const float TurretRate = FMath::Abs(FMath::FindDeltaAngleDegrees(PrevTurret, TurretYawShown)) / Dt;
	ServoLevel = TNBeachCritterKit::Ease(ServoLevel, FMath::Clamp(TurretRate / TNBeachTank::TurretRate, 0.f, 1.f), DeltaSeconds, 0.1f);
	BarrelPitchShown = TNBeachCritterKit::Ease(BarrelPitchShown, WantPitch, DeltaSeconds, 0.2f);
	TurretPivot->SetRelativeRotation(FRotator(0.f, TurretYawShown - ShownYaw, 0.f));
	const float Recoil = TNBeachTank::RecoilDist * (RecoilAge < 0.6f ? FMath::Exp(-RecoilAge / 0.09f) : 0.f);
	BarrelPivot->SetRelativeLocationAndRotation(D.BarrelPivot - FVector(Recoil, 0.0, 0.0), FRotator(BarrelPitchShown, 0.f, 0.f));

	// Antena: látigo con muelle (se echa atrás al acelerar y al disparar); mareado, da vueltas como una hélice.
	if (bStunned)
	{
		AntennaSpin += DeltaSeconds * 720.f;
		AntennaPivot->SetRelativeRotation(FRotator(0.f, AntennaSpin, 0.f).Quaternion() * FRotator(0.f, 0.f, 35.f).Quaternion());
	}
	else
	{
		AntennaSpin = FMath::Fmod(AntennaSpin, 360.f) * FMath::Exp(-DeltaSeconds / 0.2f);
		const FVector2D LeanGoal(FMath::Clamp(-Accel * 0.03f, -20.f, 20.f) + 14.f * Kick, FMath::Clamp(YawStep / Dt * 8.f, -15.f, 15.f));
		const FVector2D Force = (LeanGoal - AntennaLean) * 70.f - AntennaLeanVel * 5.f;
		AntennaLeanVel += Force * DeltaSeconds;
		AntennaLean += AntennaLeanVel * DeltaSeconds;
		AntennaPivot->SetRelativeRotation(FRotator(AntennaLean.X + 2.f * FMath::Sin(VisualClock * 3.1f), AntennaSpin, AntennaLean.Y));
	}
	if (Flag)
	{
		Flag->SetRelativeRotation(FRotator(0.f, 15.f * FMath::Sin(VisualClock * 9.f) + 8.f * FMath::Sin(VisualClock * 13.f), 0.f));
	}

	// Sonido: motor eléctrico por la marcha y el servo de la torreta; mareado, tose.
	if (Sound)
	{
		const float Speed01 = FMath::Clamp(FMath::Abs(SpeedShown) / TNBeachTank::DriveSpeed, 0.f, 1.f);
		if (ViewDistance > 6000.f)
		{
			Sound->SetMotor(0.f, 0.f);
		}
		else if (bStunned)
		{
			const float Stutter = FMath::Sin(VisualClock * 23.f) > 0.2f ? 1.f : 0.25f;
			Sound->SetMotor(0.3f * Stutter, 0.12f);
			SputterTimer -= DeltaSeconds;
			if (SputterTimer <= 0.f)
			{
				SputterTimer = 0.9f;
				Sound->Play(ETNBeachCritterSfx::Sputter, 1.f, 0.8f);
			}
		}
		else
		{
			Sound->SetMotor(0.16f + 0.5f * Speed01 + 0.3f * ServoLevel, 0.2f + 0.6f * Speed01 + 0.25f * ServoLevel);
		}
	}
}

void ATN_BeachToyTank::VisualTick(float DeltaSeconds)
{
	VisualClock += DeltaSeconds;
	const double Now = ServerNow(this);
	if (!HasAuthority())
	{
		AdvanceShots(Now, false);
	}
	if (!HullRoot)
	{
		return;
	}
	const bool bStunned = IsHitStunned() || static_cast<TNBeachTank::EState>(GetMoverState()) == TNBeachTank::EState::Stunned;
	const bool bVisible = ViewDistance < GetVisualRange();
	FVector View = ShownLoc;
	TNBeachKit::LocalCamera(GetWorld(), View);

	// Bolitas y su estela (se ven aunque el tanque quede lejos: vuelan hacia la tortuga).
	const float FoamScale = SizeK;
	for (int32 i = 0; i < MaxShots; ++i)
	{
		FTNTankShot& Shot = Shots[i];
		if (!Shot.bAlive)
		{
			FoamXf[i] = FTransform(FQuat::Identity, ShownLoc, FVector::ZeroVector);
			continue;
		}
		FoamXf[i] = FTransform(FRotator(static_cast<float>(Now - Shot.Born) * 540.f, static_cast<float>(i) * 40.f, 0.f), Shot.Pos, FVector(FoamScale));
		Shot.TrailTimer -= DeltaSeconds;
		if (Shot.TrailTimer <= 0.f && LocalViewDistance(this, Shot.Pos) < 9000.f)
		{
			Shot.TrailTimer = 0.035f;
			const FVector Back = (Shot.LastPos - Shot.Pos).GetSafeNormal();
			TNBeachKit::BurstAt(Trail, Shot.Pos, Back.IsNearlyZero() ? FVector::UpVector : Back, 1);
		}
	}
	TNBeachCritterKit::WriteInstances(FoamBalls, FoamXf, true);
	TNBeachKit::TickEmitterIfBusy(Trail, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(Muzzle, DeltaSeconds, View);

	if (bStunned != bWasStunned)
	{
		bWasStunned = bStunned;
		SputterTimer = 0.5f;
	}
	if (!bVisible)
	{
		if (Sound)
		{
			Sound->SetMotor(0.f, 0.f);
		}
		return;
	}
	PoseTank(DeltaSeconds, bStunned);

	// Humo del motor mareado y polvo de las orugas al andar.
	const FTransform HullXf = HullRoot->GetComponentTransform();
	const TNBeachCritterMeshes::FTankDims D = TNBeachCritterMeshes::TankDims();
	Smoke.Origin = HullXf.TransformPosition(FVector(-D.HalfLength - 0.5 * TNBeach::Scale, 0.0, D.HullTop - 0.8 * TNBeach::Scale));
	Smoke.RateScale = bStunned ? 7.f : 0.f;
	TNBeachKit::TickEmitterIfBusy(Smoke, DeltaSeconds, View);
	Dust.Origin = HullXf.TransformPosition(FVector(-D.HalfLength * FMath::Sign(SpeedShown + 0.01f), 0.0, 10.0));
	Dust.Desc.Direction = FVector::UpVector;
	Dust.RateScale = ViewDistance < 9000.f ? FMath::Clamp(FMath::Abs(SpeedShown) / TNBeachTank::DriveSpeed, 0.f, 1.f) * 4.f : 0.f;
	TNBeachKit::TickEmitterIfBusy(Dust, DeltaSeconds, View);
}
