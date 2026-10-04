#include "World/Beach/TN_BeachPoolOctopus.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachCritterSynth.h"
#include "World/Beach/TN_BeachLayout.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/Beach/TN_BeachStun.h"
#include "TN_BeachCritterKit.h"
#include "TN_BeachCritterMeshes.h"
#include "TN_BeachEnemyKit.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_CombatTuning.h"

namespace TNBeachOctopus
{
	/** Estados del pulpo (Mover.State). */
	enum class EState : uint8 { Lurk, Stalk, Grab, Throw, Retreat, Dizzy };

	inline uint8 ToByte(EState E)
	{
		return static_cast<uint8>(E);
	}

	/** Radio del charco propio fuera de una poza (cm, por el tamaño). */
	constexpr float FallbackRadius = 700.f;
	/** Velocidades bajo el agua (cm/s): paseo, a por la nadadora (por la raíz del tamaño) y vuelta al centro. */
	constexpr float LurkSpeed = 80.f;
	constexpr float StalkSpeed = 520.f;
	constexpr float RetreatSpeed = 320.f;
	/** Se fija en ella (burbujas y puntas que asoman) antes de salir a por ella; se rinde a los 7 s. */
	constexpr float NoticeTime = 0.5f;
	constexpr float StalkTimeout = 7.f;
	constexpr float RetreatTime = 3.f;
	/** Alcance del agarre (cm, por el tamaño, en planta desde el centro del pulpo). */
	constexpr float GrabReach = 300.f;
	/** Agarre: tiempo en subirla, tiempo colgando antes de lanzar y altura sobre el agua (por el tamaño). */
	constexpr float LiftTime = 0.35f;
	constexpr float HoldTime = 0.8f;
	constexpr float HoldHeight = 230.f;
	/** Lanzamiento: ángulo, cuánto más allá de la orilla cae y alcance mínimo y máximo (el mareo, en UTN_CombatTuning). */
	constexpr float ThrowAngle = 42.f;
	constexpr float ThrowBeyond = 600.f;
	constexpr float ThrowMinRange = 900.f;
	constexpr float ThrowMaxRange = 3400.f;
	/** Lo que dura la tinta y el brazo que acompaña (la inmunidad tras lanzarla, en UTN_CombatTuning). */
	constexpr float ThrowShowTime = 0.6f;
	/** Mareado, como poco esto flotando. */
	constexpr float DizzyMinTime = 0.8f;
	/** Cada cuánto busca nadadoras y hasta qué radio normalizado de la poza se mueve (no se mete en lo poco hondo). */
	constexpr float ScanPeriod = 0.15f;
	constexpr float InnerU = 0.85f;
	/** Lo que se pierde por el rozamiento en el lanzamiento (la gravedad, en UTN_CombatTuning). */
	constexpr float ThrowDragBoost = 1.08f;
	/**
	 * Burbujas que suenan al acechar (las que se ven no cambian): solo a menos de BubbleAudibleDistance del oyente, solo
	 * las de los BubbleMaxAudible pulpos más cercanos, sin dos disparos a menos de BubbleMinGap s entre sí y cada pulpo
	 * una cada BubbleSoundMin..BubbleSoundMin + BubbleSoundSpread s. Antes sonaba cada burbuja visible (cada 1-2,5 s), en
	 * los 35 m alrededor de la cámara y en todos los pulpos: con dos o tres pozas a tiro era un borboteo que no paraba.
	 */
	constexpr float BubbleAudibleDistance = 2400.f;
	constexpr int32 BubbleMaxAudible = 2;
	constexpr float BubbleMinGap = 1.6f;
	constexpr float BubbleSoundMin = 3.5f;
	constexpr float BubbleSoundSpread = 3.5f;
}

ATN_BeachPoolOctopus::ATN_BeachPoolOctopus()
{
	NetFrequencyNear = 8.f;
	SetNetUpdateFrequency(NetFrequencyNear);
	bThrottleWhenFar = true;
}

void ATN_BeachPoolOctopus::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachPoolOctopus, Grabbed);
}

float ATN_BeachPoolOctopus::GetActiveRange() const
{
	return PoolRadius * 1.3f + 3000.f;
}

bool ATN_BeachPoolOctopus::GetHitCapsule(FVector& OutA, FVector& OutB, float& OutRadius) const
{
	if (!bPoolResolved)
	{
		return false;
	}
	const float S = static_cast<float>(TNBeachCritterMeshes::OctoSize()) * SizeK;
	OutA = FVector(ShownLoc.X, ShownLoc.Y, WaterZ - DepthFor(GetMoverState(), ShownLoc));
	OutB = OutA + FVector(0.0, 0.0, S * 0.6f);
	OutRadius = S * 0.32f;
	return true;
}

FVector ATN_BeachPoolOctopus::GetHitStunAnchor() const
{
	const float S = static_cast<float>(TNBeachCritterMeshes::OctoSize()) * SizeK;
	const float Top = FMath::Max(WaterZ, WaterZ - DepthFor(GetMoverState(), ShownLoc) + S * 0.7f);
	return FVector(ShownLoc.X, ShownLoc.Y, Top + 160.f);
}

float ATN_BeachPoolOctopus::GetHitStunScale() const
{
	return 2.4f * SizeK;
}

void ATN_BeachPoolOctopus::ApplySpec()
{
	SizeK = FMath::Clamp(Spec.SizeScale, 0.8f, 1.3f);
	ResolvePool();
	BuildOctopus();
}

// ─────────────────────────────────────────────────────────────────────────────
// Poza (todas las máquinas)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachPoolOctopus::ResolvePool()
{
	if (bPoolResolved)
	{
		return;
	}
	bPoolResolved = true;
	const FVector Here = GetActorLocation();
	FVector Back = -GetActorForwardVector();
	if (const ATN_BeachRaceGenerator* Gen = FindGenerator())
	{
		GenXf = Gen->GetActorTransform();
		Back = -Gen->GetSeaDirection();
		const FVector Local = GenXf.InverseTransformPosition(Here);
		const FVector2D Local2D(Local.X, Local.Y);
		const TArray<TNBeachLayout::FPool>& All = TNBeachLayout::Pools();
		double BestU = 1.35;
		for (int32 i = 0; i < All.Num(); ++i)
		{
			const double U = TNBeachLayout::PoolU(All[i], Local2D);
			if (U < BestU)
			{
				BestU = U;
				PoolIndex = i;
			}
		}
		if (PoolIndex != INDEX_NONE)
		{
			const TNBeachLayout::FPool& Pool = All[PoolIndex];
			bHasPool = true;
			// El reparto lo pone en el centro; si lo han puesto a mano dentro de la poza, se queda donde está.
			const FVector2D At = BestU < 0.7 ? Local2D : Pool.Center;
			PoolHome = GenXf.TransformPosition(FVector(At.X, At.Y, Pool.Water));
			WaterZ = static_cast<float>(PoolHome.Z);
			PoolRadius = static_cast<float>(FMath::Min(Pool.Rx, Pool.Ry) * GenXf.GetScale3D().X);
		}
	}
	if (!bHasPool)
	{
		// Sin poza (otro mapa o puesto a mano en la arena): su propio charco alrededor.
		float Ground = static_cast<float>(Here.Z);
		TraceGround(this, Here, Ground);
		PoolRadius = TNBeachOctopus::FallbackRadius * SizeK;
		WaterZ = Ground + 45.f;
		PoolHome = FVector(Here.X, Here.Y, WaterZ);
	}
	Back.Z = 0.0;
	BackDir = Back.Normalize() ? Back : -FVector::ForwardVector;
	ShownDepth = DepthFor(TNBeachOctopus::ToByte(TNBeachOctopus::EState::Lurk), PoolHome);
}

bool ATN_BeachPoolOctopus::IsInPool(const FVector& Point, float UMax) const
{
	if (bHasPool && TNBeachLayout::Pools().IsValidIndex(PoolIndex))
	{
		const FVector Local = GenXf.InverseTransformPosition(Point);
		return TNBeachLayout::PoolU(TNBeachLayout::Pools()[PoolIndex], FVector2D(Local.X, Local.Y)) <= UMax;
	}
	return FVector::Dist2D(Point, PoolHome) <= PoolRadius * UMax;
}

float ATN_BeachPoolOctopus::BedDepthAt(const FVector& Where) const
{
	if (bHasPool && TNBeachLayout::Pools().IsValidIndex(PoolIndex))
	{
		const TNBeachLayout::FPool& Pool = TNBeachLayout::Pools()[PoolIndex];
		const FVector Local = GenXf.InverseTransformPosition(Where);
		const double Bed = TNBeachLayout::PoolBedZ(Pool, TNBeachLayout::PoolU(Pool, FVector2D(Local.X, Local.Y)));
		return static_cast<float>(FMath::Max(0.0, Pool.Water - Bed));
	}
	return 45.f;
}

float ATN_BeachPoolOctopus::DepthFor(uint8 State, const FVector& Where) const
{
	using TNBeachOctopus::EState;
	const float Bed = BedDepthAt(Where);
	switch (static_cast<EState>(State))
	{
	case EState::Stalk: return FMath::Clamp(FMath::Min(70.f * SizeK, Bed - 25.f), 30.f, 150.f);
	case EState::Grab:
	case EState::Throw: return 20.f;
	case EState::Dizzy: return 4.f;
	case EState::Lurk:
	case EState::Retreat:
	default: return FMath::Clamp(Bed - 25.f, 40.f, 150.f);
	}
}

FRotator ATN_BeachPoolOctopus::BodyTilt(uint8 State) const
{
	using TNBeachOctopus::EState;
	switch (static_cast<EState>(State))
	{
	case EState::Stalk: return FRotator(-62.f, 0.f, 0.f);
	case EState::Grab:
	case EState::Throw: return FRotator(8.f, 0.f, 0.f);
	case EState::Dizzy: return FRotator(-20.f, 0.f, 70.f);
	case EState::Lurk:
	case EState::Retreat:
	default: return FRotator(-45.f, 0.f, 0.f);
	}
}

FVector ATN_BeachPoolOctopus::GripAt(float Age) const
{
	const FVector Start = Mover.Aim;
	const FVector Octo = Mover.Location;
	FVector Hold = FMath::Lerp(Start, Octo, 0.45);
	Hold.Z = WaterZ + TNBeachOctopus::HoldHeight * SizeK;
	const float U = static_cast<float>(TNProcMap::SmoothStep(0.0, TNBeachOctopus::LiftTime, Age));
	FVector Grip = FMath::Lerp(Start, Hold, static_cast<double>(U));
	// Cuelga meciéndose del brazo mientras la sube.
	Grip += FVector(25.0 * FMath::Sin(Age * 9.f), 15.0 * FMath::Cos(Age * 7.f), 10.0 * FMath::Sin(Age * 12.f)) * (U * SizeK);
	return Grip;
}

FVector ATN_BeachPoolOctopus::ThrowLaunch(const FVector& From, float& OutFlight) const
{
	// Distancia hasta la orilla de atrás (hacia la salida) y un poco más, para que caiga en la arena.
	float Exit = 0.f;
	for (float Step = 0.f; Step <= 4000.f; Step += 100.f)
	{
		Exit = Step;
		if (!IsInPool(From + BackDir * Step, 1.2f))
		{
			break;
		}
	}
	const float Range = FMath::Clamp(Exit + TNBeachOctopus::ThrowBeyond, TNBeachOctopus::ThrowMinRange, TNBeachOctopus::ThrowMaxRange);
	const float Height = FMath::Max(0.f, static_cast<float>(From.Z) - (WaterZ + 40.f));
	const float Theta = FMath::DegreesToRadians(TNBeachOctopus::ThrowAngle);
	const float C = FMath::Cos(Theta);
	const float Sn = FMath::Sin(Theta);
	// Tiro parabólico desde Height sobre donde cae: v² = g R² / (2 cos² (R tan + H)).
	const float V = FMath::Sqrt(UTN_CombatTuning::Get().PoolOctopusThrowGravity * Range * Range / (2.f * C * C * (Range * Sn / C + Height))) * TNBeachOctopus::ThrowDragBoost;
	OutFlight = Range / FMath::Max(1.f, V * C);
	return BackDir * (V * C) + FVector(0.0, 0.0, V * Sn);
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_BeachPoolOctopus::IsSwimmer(const ATortugaCharacter* Turtle) const
{
	if (!IsTargetable(Turtle))
	{
		return false;
	}
	const FVector At = Turtle->GetActorLocation();
	if (!IsInPool(At, 1.02f))
	{
		return false;
	}
	if (!bHasPool)
	{
		return true;
	}
	const UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
	return (Move && Move->IsSwimming()) || At.Z < WaterZ + 40.f;
}

ATortugaCharacter* ATN_BeachPoolOctopus::FindSwimmer() const
{
	TArray<ATortugaCharacter*> Turtles;
	GatherTurtles(this, Turtles);
	ATortugaCharacter* Best = nullptr;
	double BestSq = TNumericLimits<double>::Max();
	for (ATortugaCharacter* Turtle : Turtles)
	{
		if (!IsSwimmer(Turtle))
		{
			continue;
		}
		const double DistSq = FVector::DistSquared2D(Turtle->GetActorLocation(), SimLoc);
		if (DistSq < BestSq)
		{
			BestSq = DistSq;
			Best = Turtle;
		}
	}
	return Best;
}

void ATN_BeachPoolOctopus::SwimToward(const FVector& Goal, float Speed, float DeltaSeconds)
{
	FVector Flat = Goal - SimLoc;
	Flat.Z = 0.0;
	const double Dist = Flat.Size();
	if (Dist < 1.0)
	{
		return;
	}
	const FVector Dir = Flat / Dist;
	FVector Next = SimLoc + Dir * FMath::Min(Dist, static_cast<double>(Speed * DeltaSeconds));
	// No se mete en lo poco hondo: si el paso le saca de ahí, se queda en el borde.
	if (!IsInPool(Next, TNBeachOctopus::InnerU) && IsInPool(SimLoc, TNBeachOctopus::InnerU))
	{
		Next = SimLoc;
	}
	Next.Z = WaterZ;
	ServerMoveTo(Next, TNBeachCritterKit::TurnToward(SimYaw, static_cast<float>(Dir.Rotation().Yaw), 360.f * DeltaSeconds));
}

void ATN_BeachPoolOctopus::OnHoldAborted(ATortugaCharacter* Turtle)
{
	// Se la quitan de los brazos (red de seguridad, gusano): la olvida y se hunde a su sitio, sin lanzarla al acabar el agarre.
	if (!HasAuthority() || !Turtle || Grabbed != Turtle)
	{
		return;
	}
	Grabbed = nullptr;
	ServerSetState(TNBeachOctopus::ToByte(TNBeachOctopus::EState::Retreat), PoolHome);
	ForceNetUpdate();
}

void ATN_BeachPoolOctopus::ServerTick(float DeltaSeconds)
{
	using TNBeachOctopus::EState;
	using TNBeachOctopus::ToByte;
	ResolvePool();
	if (!bPlaced)
	{
		bPlaced = true;
		DriftGoal = PoolHome;
		ServerMoveTo(PoolHome, static_cast<float>(GetActorRotation().Yaw));
		ServerSetState(ToByte(EState::Lurk), PoolHome);
	}
	const EState State = static_cast<EState>(GetMoverState());
	const float Age = GetStateAge();
	const bool bStunned = IsHitStunned();
	const bool bLive = IsRaceLive(this);
	switch (State)
	{
	case EState::Lurk:
		if (bStunned)
		{
			ServerSetState(ToByte(EState::Dizzy), SimLoc);
			break;
		}
		// Pasea despacio por el centro de la poza.
		DriftTimer -= DeltaSeconds;
		if (DriftTimer <= 0.f || FVector::Dist2D(SimLoc, DriftGoal) < 60.0)
		{
			DriftTimer = ServerRng.FRandRange(4.f, 7.f);
			DriftGoal = PoolHome;
			for (int32 Try = 0; Try < 5; ++Try)
			{
				const float Angle = ServerRng.FRandRange(0.f, 2.f * PI);
				const float Dist = PoolRadius * 0.35f * FMath::Sqrt(ServerRng.FRandRange(0.1f, 1.f));
				const FVector Candidate = PoolHome + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.f);
				if (IsInPool(Candidate, 0.6f))
				{
					DriftGoal = Candidate;
					break;
				}
			}
		}
		SwimToward(DriftGoal, TNBeachOctopus::LurkSpeed * SizeK, DeltaSeconds);
		ScanTimer -= DeltaSeconds;
		if (bLive && ScanTimer <= 0.f)
		{
			ScanTimer = TNBeachOctopus::ScanPeriod;
			if (ATortugaCharacter* Swimmer = FindSwimmer())
			{
				Target = Swimmer;
				MoveSpeedNow = 0.f;
				ServerSetState(ToByte(EState::Stalk), Swimmer->GetActorLocation());
			}
		}
		break;

	case EState::Stalk:
	{
		if (bStunned)
		{
			ServerSetState(ToByte(EState::Dizzy), SimLoc);
			break;
		}
		ATortugaCharacter* Victim = Target.Get();
		if (!bLive || !IsSwimmer(Victim))
		{
			Victim = bLive ? FindSwimmer() : nullptr;
			Target = Victim;
			if (!Victim)
			{
				ServerSetState(ToByte(EState::Retreat), PoolHome);
				break;
			}
		}
		const FVector At = Victim->GetActorLocation();
		ServerSetAim(At);
		if (Age < TNBeachOctopus::NoticeTime)
		{
			// Se fija en ella: se da la vuelta hacia ella sin moverse.
			FVector To = At - SimLoc;
			To.Z = 0.0;
			ServerMoveTo(SimLoc, TNBeachCritterKit::TurnToward(SimYaw, static_cast<float>(To.Rotation().Yaw), 540.f * DeltaSeconds));
			break;
		}
		MoveSpeedNow = TNBeachCritterKit::Ease(MoveSpeedNow, TNBeachOctopus::StalkSpeed * FMath::Sqrt(SizeK), DeltaSeconds, 0.25f);
		SwimToward(At, MoveSpeedNow, DeltaSeconds);
		if (FVector::Dist2D(SimLoc, At) < TNBeachOctopus::GrabReach * SizeK)
		{
			// La agarra: cada máquina la sujeta desde donde estaba (Aim) con la hora del estado.
			Grabbed = Victim;
			IgnoreTurtle(Victim, UTN_CombatTuning::Get().PoolOctopusIgnoreSeconds + TNBeachOctopus::HoldTime);
			ServerSetState(ToByte(EState::Grab), At);
		}
		else if (Age > TNBeachOctopus::StalkTimeout)
		{
			ServerSetState(ToByte(EState::Retreat), PoolHome);
		}
		break;
	}

	case EState::Grab:
	{
		ATortugaCharacter* Victim = Grabbed;
		// Otro la mueve (en bola, derribada, aturdida o recolocada): la suelta sin lanzarla, en vez de pelearse por ella.
		const bool bTaken = IsValid(Victim) && (Victim->IsInShell() || Victim->IsKnockedDown() || TNBeach::IsTurtleStunned(Victim)
			|| TNBeach::IsTurtleRelocating(Victim));
		if (bStunned || bTaken || !IsValid(Victim) || Victim->IsDead())
		{
			// Mareado por un golpe (o sin nadie): la suelta y cae al agua.
			EndHoldTurtle();
			Grabbed = nullptr;
			ServerSetState(ToByte(bStunned ? EState::Dizzy : EState::Retreat), SimLoc);
			break;
		}
		if (Age >= TNBeachOctopus::HoldTime)
		{
			float Flight = 1.f;
			const FVector From = Victim->GetActorLocation();
			const FVector Launch = ThrowLaunch(From, Flight);
			EndHoldTurtle();
			StunTurtle(Victim, Flight + UTN_CombatTuning::Get().PoolOctopusStunExtraSeconds, Launch);
			IgnoreTurtle(Victim, UTN_CombatTuning::Get().PoolOctopusIgnoreSeconds);
			MulticastThrow(Victim, Launch);
			ServerSetState(ToByte(EState::Throw), From);
		}
		break;
	}

	case EState::Throw:
		if (Age >= TNBeachOctopus::ThrowShowTime)
		{
			Grabbed = nullptr;
			ServerSetState(ToByte(EState::Retreat), PoolHome);
		}
		break;

	case EState::Retreat:
		if (bStunned)
		{
			ServerSetState(ToByte(EState::Dizzy), SimLoc);
			break;
		}
		SwimToward(PoolHome, TNBeachOctopus::RetreatSpeed * SizeK, DeltaSeconds);
		if (FVector::Dist2D(SimLoc, PoolHome) < 80.0 || Age > TNBeachOctopus::RetreatTime)
		{
			DriftTimer = 0.f;
			ServerSetState(ToByte(EState::Lurk), PoolHome);
		}
		break;

	case EState::Dizzy:
	default:
		if (Grabbed)
		{
			Grabbed = nullptr;
			ForceNetUpdate();
		}
		if (!bStunned && Age >= TNBeachOctopus::DizzyMinTime)
		{
			ServerSetState(ToByte(EState::Retreat), PoolHome);
		}
		break;
	}

	if (IsDebugDraw())
	{
		UWorld* World = GetWorld();
		DrawDebugCircle(World, FVector(PoolHome.X, PoolHome.Y, WaterZ + 20.f), PoolRadius, 40, FColor::Cyan, false, -1.f, 0, 8.f,
			FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
		DrawDebugCircle(World, FVector(SimLoc.X, SimLoc.Y, WaterZ + 25.f), TNBeachOctopus::GrabReach * SizeK, 24, FColor::Red, false, -1.f, 0, 8.f,
			FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
		DrawDebugDirectionalArrow(World, FVector(SimLoc.X, SimLoc.Y, WaterZ + 60.f), FVector(SimLoc.X, SimLoc.Y, WaterZ + 60.f) + BackDir * 800.0, 120.f,
			FColor::Orange, false, -1.f, 0, 10.f);
	}
}

void ATN_BeachPoolOctopus::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateHold();
}

void ATN_BeachPoolOctopus::UpdateHold()
{
	ATortugaCharacter* Victim = Grabbed;
	const bool bHold = IsValid(Victim) && !Victim->IsDead() && static_cast<TNBeachOctopus::EState>(GetMoverState()) == TNBeachOctopus::EState::Grab;
	if (!bHold)
	{
		if (GetHeldTurtle())
		{
			EndHoldTurtle();
		}
		return;
	}
	BeginHoldTurtle(Victim);
	const float Age = GetStateAge();
	const FVector Grip = GripAt(Age);
	FVector ToOcto = FVector(Mover.Location) - Grip;
	ToOcto.Z = 0.0;
	const float Yaw = (ToOcto.IsNearlyZero() ? 0.f : static_cast<float>(ToOcto.Rotation().Yaw)) + 20.f * FMath::Sin(Age * 8.f);
	PlaceHeldTurtle(Grip, Yaw);
}

// ─────────────────────────────────────────────────────────────────────────────
// Efectos (todas las máquinas)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachPoolOctopus::OnMoverStateChanged(uint8 OldState)
{
	using TNBeachOctopus::EState;
	if (!bHasScreen)
	{
		return;
	}
	const EState State = static_cast<EState>(GetMoverState());
	const FVector Surface(ShownLoc.X, ShownLoc.Y, WaterZ + 5.f);
	const bool bNear = LocalViewDistance(this, Surface) < 9000.f;
	switch (State)
	{
	case EState::Stalk:
		TNBeachKit::BurstAt(Bubbles, Surface, FVector::UpVector, 10);
		if (Sound && bNear)
		{
			Sound->Play(ETNBeachCritterSfx::Bubble, 0.8f, 0.9f);
			Sound->Play(ETNBeachCritterSfx::Bubble, 1.2f, 0.7f);
		}
		break;
	case EState::Grab:
	{
		const FVector At = Mover.Aim;
		TNBeachKit::BurstAt(Splash, FVector(At.X, At.Y, WaterZ + 10.f), FVector::UpVector, 24);
		if (Sound && bNear)
		{
			Sound->Play(ETNBeachCritterSfx::Splash, 1.f, 1.1f);
			Sound->Play(ETNBeachCritterSfx::Slap, 1.f / FMath::Sqrt(SizeK), 1.f);
		}
		ShowPop(NSLOCTEXT("TNBeach", "OctopusGrab", "¡SLURP!"), FColor(200, 120, 255), FVector(At.X, At.Y, WaterZ + 320.f), 150.f);
		break;
	}
	case EState::Dizzy:
		TNBeachKit::BurstAt(Splash, Surface, FVector::UpVector, 12);
		if (Sound && bNear)
		{
			Sound->Play(ETNBeachCritterSfx::Splash, 1.3f, 0.7f);
		}
		break;
	default:
		break;
	}
}

void ATN_BeachPoolOctopus::MulticastThrow_Implementation(ATortugaCharacter* Victim, FVector_NetQuantize10 Launch)
{
	if (!bHasScreen)
	{
		return;
	}
	const FVector Surface(ShownLoc.X, ShownLoc.Y, WaterZ + 8.f);
	const FVector Dir = FVector(Launch).GetSafeNormal2D();
	// Nube de tinta que se extiende por el agua y un chorro hacia donde la lanza.
	TNBeachKit::BurstAt(Ink, Surface, FVector::UpVector, 36);
	TNBeachKit::BurstAt(Ink, Surface + FVector(0.0, 0.0, 60.0), (Dir + FVector(0.0, 0.0, 0.5)).GetSafeNormal(), 14);
	const FVector At = Victim ? Victim->GetActorLocation() : Surface;
	TNBeachKit::BurstAt(Splash, At, (FVector::UpVector + Dir * 0.5).GetSafeNormal(), 18);
	if (Sound && LocalViewDistance(this, Surface) < 9000.f)
	{
		Sound->Play(ETNBeachCritterSfx::Ink, 1.f / FMath::Sqrt(SizeK), 1.1f);
		Sound->Play(ETNBeachCritterSfx::Splash, 0.85f, 0.8f);
	}
	UTN_BeachCameraShake::Kick(this, Surface, 0.3f, 600.f, 2500.f);
	ShowPop(NSLOCTEXT("TNBeach", "OctopusThrow", "¡FUERA!"), FColor(255, 160, 60), At + FVector(0.0, 0.0, 260.0), 160.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Visual
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachPoolOctopus::BuildOctopus()
{
	if (!bHasScreen || BodyRoot)
	{
		return;
	}
	using TNProcMesh::FTNProcMeshBuffers;
	const int32 Pal = ((Spec.Seed % 4) + 4) % 4;
	const TNBeachCritterMeshes::FOctoLook Look = TNBeachCritterMeshes::OctoPalette(Pal);
	const FString Key = FString::Printf(TEXT("Beach.Octopus.%d."), Pal);
	BodyRoot = TNBeachCritterKit::AddWorldRoot(this, FTransform(FQuat::Identity, PoolHome, FVector(SizeK)));
	UStaticMesh* BodyM = TNBeachKit::CachedMesh(Key + TEXT("Body"), [&Look, Pal](FTNProcMeshBuffers& M)
	{
		TNBeachCritterMeshes::BuildOctoBody(M, Look, static_cast<uint32>(Pal) * 17u + 3u);
	});
	BodyMesh = TNBeachCritterKit::AddPart(this, BodyRoot, BodyM, FVector::ZeroVector, true, TN_ART("Beach.PoolOctopus.Body"));
	UStaticMesh* SegM = TNBeachKit::CachedMesh(Key + TEXT("Arm"), [&Look](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildOctoArmSegment(M, Look); });
	Arms = TNBeachCritterKit::AddInstances(this, GetRootComponent(), SegM, 8 * TNBeachCritterMeshes::OctoArmSegments, true, false, TN_ART("Beach.PoolOctopus.ArmSegment"));
	ArmXf.Init(FTransform(FQuat::Identity, PoolHome, FVector::ZeroVector), 8 * TNBeachCritterMeshes::OctoArmSegments);

	UStaticMesh* SilM = TNBeachKit::CachedMesh(TEXT("Beach.Octopus.Silhouette"), [](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildOctoSilhouette(M); },
		TNBeachKit::EBeachMeshMat::SoftVertexAlpha);
	Silhouette = TNBeachCritterKit::AddPart(this, GetRootComponent(), SilM, FVector::ZeroVector, false);
	if (Silhouette)
	{
		Silhouette->SetAbsolute(true, true, true);
		Silhouette->SetTranslucentSortPriority(3);
	}
	if (!bHasPool)
	{
		UStaticMesh* PuddleM = TNBeachKit::CachedMesh(TEXT("Beach.Octopus.Puddle"), [](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildPuddle(M); },
			TNBeachKit::EBeachMeshMat::SoftVertexAlpha);
		Puddle = TNBeachCritterKit::AddPart(this, GetRootComponent(), PuddleM, FVector::ZeroVector, false);
		if (Puddle)
		{
			Puddle->SetAbsolute(true, true, true);
			Puddle->SetTranslucentSortPriority(1);
			const double R = PoolRadius / 100.0;
			Puddle->SetWorldTransform(FTransform(FQuat::Identity, FVector(PoolHome.X, PoolHome.Y, WaterZ - 40.f + 4.f), FVector(R, R, 1.0)));
		}
	}

	using TNAmbientFX::EShape;
	TNAmbientFX::FEmitterDesc BubbleDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.86f, 0.95f, 1.f), true, 0.7f, 40, 1.f, 60.f, 0.f, 0.3f, 0.7f, 18.f, 32.f);
	BubbleDesc.Buoyancy = 120.f;
	BubbleDesc.SpawnRadius = 70.f * SizeK;
	TNBeachKit::InitEmitter(Bubbles, this, BubbleDesc, static_cast<uint32>(Spec.Seed) + 41u);
	TNAmbientFX::FEmitterDesc InkDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.07f, 0.05f, 0.12f), true, 0.8f, 60, 0.f, 380.f, 0.f, 2.5f, 4.f, 90.f, 420.f);
	InkDesc.Spread = 1.5f;
	InkDesc.Drag = 1.4f;
	InkDesc.SpawnRadius = 60.f;
	TNBeachKit::InitEmitter(Ink, this, InkDesc, static_cast<uint32>(Spec.Seed) + 43u);
	TNAmbientFX::FEmitterDesc SplashDesc = TNBeachKit::MakeDesc(EShape::Drop, FLinearColor(0.75f, 0.88f, 0.98f), false, 1.f, 40, 0.f, 650.f, -980.f, 0.5f, 1.f, 22.f, 10.f);
	SplashDesc.Spread = 0.7f;
	SplashDesc.SpawnRadius = 60.f;
	TNBeachKit::InitEmitter(Splash, this, SplashDesc, static_cast<uint32>(Spec.Seed) + 47u);
	Sound = UTN_BeachCritterSynthComponent::AttachTo(this, BodyRoot, 900.f, 6500.f);
	// Cada pulpo empieza a burbujear a su hora, no todos a la vez al aparecer.
	BubbleSoundTimer = 1.5f + 4.f * TNBeachCritterKit::Hash3(static_cast<uint32>(Spec.Seed), 11u, 0u);
}

void ATN_BeachPoolOctopus::PoseArms(uint8 State, float Age, const FVector& BodyAt, float Yaw, float DeltaSeconds)
{
	using TNBeachOctopus::EState;
	constexpr int32 NumSeg = TNBeachCritterMeshes::OctoArmSegments;
	const EState St = static_cast<EState>(State);
	const float S = static_cast<float>(TNBeachCritterMeshes::OctoSize()) * SizeK;
	const float SegLen = static_cast<float>(TNBeachCritterMeshes::OctoArmLength()) * SizeK / NumSeg;
	const float Clock = VisualClock;

	// Cómo van los brazos en cada estado: abiertos y enroscándose al acechar, detrás al nadar, sueltos flotando mareado.
	float BasePitch = -8.f;
	float Curl = 11.f;
	float Wave = 16.f;
	bool bTrail = false;
	switch (St)
	{
	case EState::Stalk:
	case EState::Retreat: BasePitch = -5.f; Curl = 2.f; Wave = 10.f; bTrail = true; break;
	case EState::Grab:
	case EState::Throw: BasePitch = 12.f; Curl = 6.f; Wave = 26.f; break;
	case EState::Dizzy: BasePitch = -2.f; Curl = 3.f; Wave = 7.f; break;
	default: break;
	}

	// Punto al que van los tres brazos que agarran (la agarrada, o donde estaba al lanzarla).
	ATortugaCharacter* Victim = Grabbed;
	FVector Grip = BodyAt;
	float BlendTarget = 0.f;
	if (St == EState::Grab)
	{
		Grip = GripAt(Age);
		BlendTarget = 1.f;
	}
	else if (St == EState::Throw)
	{
		Grip = (Victim && Age < 0.25f) ? Victim->GetActorLocation() : FVector(Mover.Aim);
		BlendTarget = 1.f - static_cast<float>(TNProcMap::SmoothStep(0.2, 0.55, Age));
	}
	GrabBlend = St == EState::Throw ? BlendTarget : TNBeachCritterKit::Ease(GrabBlend, BlendTarget, DeltaSeconds, 0.08f);
	int32 GrabArms[3] = { -1, -1, -1 };
	if (GrabBlend > 0.01f)
	{
		// Los tres brazos que nacen más hacia ella.
		FVector ToGrip = Grip - BodyAt;
		ToGrip.Z = 0.0;
		const float GripYaw = ToGrip.IsNearlyZero() ? Yaw : static_cast<float>(ToGrip.Rotation().Yaw);
		float Best[3] = { 999.f, 999.f, 999.f };
		for (int32 Arm = 0; Arm < 8; ++Arm)
		{
			const float Diff = FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw + 22.5f + 45.f * Arm, GripYaw));
			for (int32 Slot = 0; Slot < 3; ++Slot)
			{
				if (Diff < Best[Slot])
				{
					for (int32 Move = 2; Move > Slot; --Move)
					{
						Best[Move] = Best[Move - 1];
						GrabArms[Move] = GrabArms[Move - 1];
					}
					Best[Slot] = Diff;
					GrabArms[Slot] = Arm;
					break;
				}
			}
		}
	}

	FVector Joints[NumSeg + 1];
	for (int32 Arm = 0; Arm < 8; ++Arm)
	{
		const float ArmYaw = Yaw + 22.5f + 45.f * Arm;
		const FVector Out(FMath::Cos(FMath::DegreesToRadians(ArmYaw)), FMath::Sin(FMath::DegreesToRadians(ArmYaw)), 0.0);
		const FVector Root = BodyAt + Out * (0.16f * S) + FVector(0.0, 0.0, 0.02f * S);
		// Cadena libre: sale hacia fuera (o hacia atrás al nadar) y se enrosca con una onda que viaja hacia la punta.
		float Pitch = BasePitch;
		float ChainYaw = bTrail ? Yaw + 180.f + (Arm - 3.5f) * 7.f : ArmYaw;
		// Nadando a por alguien, las dos de delante asoman del agua como aletas.
		const bool bPeek = St == EState::Stalk && (Arm == 0 || Arm == 7);
		if (bPeek)
		{
			Pitch = 38.f;
			ChainYaw = ArmYaw;
		}
		Joints[0] = Root;
		for (int32 k = 0; k < NumSeg; ++k)
		{
			Pitch += (bPeek ? 4.f : Curl) + Wave * FMath::Sin(Clock * 1.6f + Arm * 0.9f - k * 0.8f);
			ChainYaw += 9.f * FMath::Sin(Clock * 1.2f + Arm * 1.3f - k * 0.7f);
			Joints[k + 1] = Joints[k] + FRotator(FMath::Clamp(Pitch, -80.f, 85.f), ChainYaw, 0.f).Vector() * (SegLen * (1.12f - 0.05f * k));
		}
		const int32 GrabSlot = (GrabArms[0] == Arm) ? 0 : ((GrabArms[1] == Arm) ? 1 : ((GrabArms[2] == Arm) ? 2 : -1));
		if (GrabSlot >= 0 && GrabBlend > 0.01f)
		{
			// Brazo que agarra: curva de la raíz a la agarrada, por encima del agua, y dos tramos enrollados alrededor.
			FVector Flat = Root - Grip;
			Flat.Z = 0.0;
			const FVector Approach = Grip + Flat.GetSafeNormal() * (40.f * SizeK);
			FVector Ctrl = (Root + Approach) * 0.5;
			Ctrl.Z = FMath::Max(Root.Z, static_cast<double>(WaterZ)) + 120.f * SizeK;
			for (int32 k = 0; k <= NumSeg - 2; ++k)
			{
				const float T = static_cast<float>(k) / static_cast<float>(NumSeg - 2);
				const FVector Bez = Root * ((1.f - T) * (1.f - T)) + Ctrl * (2.f * T * (1.f - T)) + Approach * (T * T);
				Joints[k] = FMath::Lerp(Joints[k], Bez, static_cast<double>(GrabBlend));
			}
			const float Phase = GrabSlot * 2.1f + Clock * 1.5f;
			const float Coil = 45.f * SizeK;
			const FVector C1 = Grip + FVector(FMath::Cos(Phase) * Coil, FMath::Sin(Phase) * Coil, -20.f * SizeK);
			const FVector C2 = Grip + FVector(FMath::Cos(Phase + 2.2f) * Coil, FMath::Sin(Phase + 2.2f) * Coil, 15.f * SizeK);
			Joints[NumSeg - 1] = FMath::Lerp(Joints[NumSeg - 1], C1, static_cast<double>(GrabBlend));
			Joints[NumSeg] = FMath::Lerp(Joints[NumSeg], C2, static_cast<double>(GrabBlend));
		}
		for (int32 k = 0; k < NumSeg; ++k)
		{
			const FVector D = Joints[k + 1] - Joints[k];
			const float Len = static_cast<float>(D.Size());
			const float Radius = FMath::Lerp(0.075f, 0.02f, static_cast<float>(k) / static_cast<float>(NumSeg - 1)) * S;
			ArmXf[Arm * NumSeg + k] = FTransform(D.Rotation(), Joints[k], FVector(FMath::Max(1.f, Len) / 100.f, Radius, Radius));
		}
	}
	TNBeachCritterKit::WriteInstances(Arms, ArmXf, true);
}

void ATN_BeachPoolOctopus::VisualTick(float DeltaSeconds)
{
	using TNBeachOctopus::EState;
	VisualClock += DeltaSeconds;
	ResolvePool();
	const uint8 StateByte = GetMoverState();
	const EState State = static_cast<EState>(StateByte);
	const float Age = GetStateAge();
	const bool bStunned = IsHitStunned() || State == EState::Dizzy;
	const FVector XY(ShownLoc.X, ShownLoc.Y, WaterZ);
	ShownDepth = TNBeachCritterKit::Ease(ShownDepth, DepthFor(StateByte, XY), DeltaSeconds, 0.25f);
	if (!BodyRoot || ViewDistance > GetVisualRange())
	{
		return;
	}
	// Cuerpo: se hincha y deshincha (a chorros al nadar) y, mareado, flota de lado meciéndose.
	const FVector BodyAt = XY - FVector(0.0, 0.0, ShownDepth);
	FRotator Tilt = BodyTilt(StateByte);
	if (bStunned)
	{
		Tilt.Roll += 12.f * FMath::Sin(VisualClock * 1.7f);
		Tilt.Pitch += 6.f * FMath::Sin(VisualClock * 2.3f);
	}
	const float Pulse = (State == EState::Stalk || State == EState::Retreat) ? 0.1f * FMath::Sin(VisualClock * 8.f) : 0.04f * FMath::Sin(VisualClock * 2.2f);
	const float Yaw = ShownYaw;
	BodyRoot->SetWorldTransform(FTransform(FRotator(Tilt.Pitch, Yaw, Tilt.Roll), BodyAt, FVector(SizeK * (1.f + Pulse), SizeK * (1.f - Pulse * 0.5f), SizeK * (1.f + Pulse))));
	if (ViewDistance < 9000.f)
	{
		PoseArms(StateByte, Age, BodyAt, Yaw, DeltaSeconds);
	}
	// Silueta oscura en el agua mientras está sumergido.
	const bool bUnder = State == EState::Lurk || State == EState::Stalk || State == EState::Retreat;
	TNBeachCritterKit::SetShown(Silhouette, bUnder);
	if (Silhouette && bUnder)
	{
		Silhouette->SetWorldTransform(FTransform(FRotator(0.f, Yaw + 10.f * FMath::Sin(VisualClock * 0.7f), 0.f), FVector(XY.X, XY.Y, WaterZ + 3.f), FVector(SizeK * 0.9f)));
	}

	// Burbujas: sueltas al acechar, seguidas al nadar.
	FVector View = XY;
	TNBeachKit::LocalCamera(GetWorld(), View);
	Bubbles.Origin = FVector(BodyAt.X, BodyAt.Y, WaterZ + 4.f);
	Bubbles.RateScale = (State == EState::Stalk) ? 14.f : (State == EState::Retreat ? 6.f : 0.f);
	if (State == EState::Lurk)
	{
		BubbleTimer -= DeltaSeconds;
		BubbleSoundTimer -= DeltaSeconds;
		// A tiro del oído: se anuncia al presupuesto de burbujas en cada fotograma para que los más cercanos manden.
		const bool bBubbleAudible = ViewDistance < TNBeachOctopus::BubbleAudibleDistance;
		if (bBubbleAudible)
		{
			TNBeachKit::AmbientVoiceTouch(GetWorld(), this, ViewDistance);
		}
		if (BubbleTimer <= 0.f)
		{
			BubbleTimer = 1.f + 1.5f * TNBeachCritterKit::Hash3(static_cast<uint32>(Spec.Seed), static_cast<uint32>(VisualClock * 10.f), 7u);
			TNBeachKit::BurstAt(Bubbles, Bubbles.Origin, FVector::UpVector, 3);
			// Solo suena una de cada tres o cuatro burbujas y solo si este pulpo es de los más cercanos (si no, la próxima).
			if (bBubbleAudible && BubbleSoundTimer <= 0.f
				&& TNBeachKit::AmbientVoiceClaim(GetWorld(), this, TNBeachOctopus::BubbleMaxAudible, TNBeachOctopus::BubbleMinGap))
			{
				if (!BubbleVoice)
				{
					// Alcance corto (las burbujas ya se filtran por distancia) y en la clase de Ambiente.
					BubbleVoice = UTN_BeachCritterSynthComponent::AttachTo(this, BodyRoot, 500.f, 2200.f);
					if (BubbleVoice)
					{
						BubbleVoice->bAmbientBed = true;
						BubbleVoice->Loudness = 0.8f;
					}
				}
				if (BubbleVoice)
				{
					BubbleVoice->Play(ETNBeachCritterSfx::Bubble, 0.8f + 0.4f * TNBeachCritterKit::Hash3(static_cast<uint32>(Spec.Seed), 3u, static_cast<uint32>(VisualClock * 10.f)), 0.35f);
				}
				BubbleSoundTimer = TNBeachOctopus::BubbleSoundMin
					+ TNBeachOctopus::BubbleSoundSpread * TNBeachCritterKit::Hash3(static_cast<uint32>(Spec.Seed), 5u, static_cast<uint32>(VisualClock * 10.f));
			}
		}
	}
	TNBeachKit::TickEmitterIfBusy(Bubbles, DeltaSeconds, View);
	Ink.Origin = FVector(BodyAt.X, BodyAt.Y, WaterZ + 8.f);
	TNBeachKit::TickEmitterIfBusy(Ink, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(Splash, DeltaSeconds, View);
}
