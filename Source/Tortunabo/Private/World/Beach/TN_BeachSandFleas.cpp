#include "World/Beach/TN_BeachSandFleas.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachCritterSynth.h"
#include "World/Beach/TN_BeachStun.h"
#include "TN_BeachCritterKit.h"
#include "TN_BeachCritterMeshes.h"
#include "TN_BeachEnemyKit.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_CombatTuning.h"

namespace TNBeachFleas
{
	/** Estados del enjambre (Mover.State). */
	enum class EState : uint8 { Wander, Hunt, Infest, Scatter };

	inline uint8 ToByte(EState E)
	{
		return static_cast<uint8>(E);
	}

	/** Pulgas de la nube. */
	constexpr int32 NumFleas = 48;
	/** Radios (cm, por el tamaño): nube junta, la vista y la correa desde su sitio, y la nube dispersa. */
	constexpr float CloudRadius = 170.f;
	constexpr float DetectRadius = 1800.f;
	constexpr float LeashRadius = 2400.f;
	constexpr float ScatterRadius = 650.f;
	/** Velocidades del centro (cm/s): paseo y hacia una tortuga (despacio: se le escapa andando a 4,5 m/s). */
	constexpr float WanderSpeed = 70.f;
	constexpr float HuntSpeed = 160.f;
	/** Alcanza a la tortuga a CloudRadius más esto (cm). */
	constexpr float ReachPad = 100.f;
	/** Picada: lo que dura, primer saltito, cada cuánto, y velocidades (cm/s) hacia arriba y de lado. */
	constexpr float InfestTime = 2.f;
	constexpr float HopFirst = 0.1f;
	constexpr float HopEvery = 0.3f;
	constexpr float HopUpMin = 320.f;
	constexpr float HopUpMax = 420.f;
	constexpr float HopSideMin = 140.f;
	constexpr float HopSideMax = 260.f;
	/** Un saltito que le llega al dueño más tarde que esto (s) ya no se aplica (entra a mitad de la picada). */
	constexpr float HopLateLimit = 0.25f;
	/** Lo que tardan en dispersarse y juntarse (el mareo y la inmunidad, en UTN_CombatTuning). */
	constexpr float ScatterOut = 1.2f;
	constexpr float ScatterTime = 3.2f;
	constexpr float ScanPeriod = 0.2f;
}

ATN_BeachSandFleas::ATN_BeachSandFleas()
{
	NetFrequencyNear = 8.f;
	SetNetUpdateFrequency(NetFrequencyNear);
	bThrottleWhenFar = true;
}

void ATN_BeachSandFleas::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachSandFleas, Infested);
}

float ATN_BeachSandFleas::GetActiveRange() const
{
	return LeashRadius + DetectRadius + 1000.f;
}

bool ATN_BeachSandFleas::GetHitCapsule(FVector& OutA, FVector& OutB, float& OutRadius) const
{
	using TNBeachFleas::EState;
	const EState State = static_cast<EState>(GetMoverState());
	// Dispersa no hay a qué darle; picando, la nube está sobre la tortuga.
	if (State == EState::Scatter)
	{
		return false;
	}
	const FVector Center = (State == EState::Infest && Infested) ? Infested->GetActorLocation() : ShownLoc + FVector(0.0, 0.0, 50.0);
	OutA = Center;
	OutB = Center + FVector(0.0, 0.0, 40.0);
	OutRadius = CloudRadius * 0.9f;
	return true;
}

FVector ATN_BeachSandFleas::GetHitStunAnchor() const
{
	return ShownLoc + FVector(0.0, 0.0, 190.0);
}

float ATN_BeachSandFleas::GetHitStunScale() const
{
	return 1.6f;
}

void ATN_BeachSandFleas::ApplySpec()
{
	SizeK = FMath::Clamp(Spec.SizeScale, 0.8f, 1.25f);
	CloudRadius = TNBeachFleas::CloudRadius * SizeK;
	DetectRadius = TNBeachFleas::DetectRadius * FMath::Sqrt(SizeK);
	LeashRadius = FMath::Max(TNBeachFleas::LeashRadius, GetFootprintRadius() * 1.8f);
	GroundZ = static_cast<float>(GetActorLocation().Z);
	FleaRng = (FleaRng ^ (static_cast<uint32>(Spec.Seed) * 2654435761u)) | 1u;
	BuildSwarm();
}

// ─────────────────────────────────────────────────────────────────────────────
// Saltitos (servidor y dueño de la tortuga, con las mismas cuentas)
// ─────────────────────────────────────────────────────────────────────────────

int32 ATN_BeachSandFleas::NumHops() const
{
	return FMath::FloorToInt32((TNBeachFleas::InfestTime - TNBeachFleas::HopFirst) / TNBeachFleas::HopEvery) + 1;
}

float ATN_BeachSandFleas::HopTime(int32 Index) const
{
	return TNBeachFleas::HopFirst + TNBeachFleas::HopEvery * Index;
}

FVector ATN_BeachSandFleas::HopVelocity(int32 Index) const
{
	// Misma semilla en todas las máquinas: la de la picada (número de estado) y el índice del saltito.
	FRandomStream Rng(static_cast<int32>(static_cast<uint32>(Spec.Seed) * 7919u + static_cast<uint32>(Mover.Serial) * 131u + static_cast<uint32>(Index) * 17u));
	const float Angle = Rng.FRandRange(0.f, 2.f * PI);
	const float Side = Rng.FRandRange(TNBeachFleas::HopSideMin, TNBeachFleas::HopSideMax);
	const float Up = Rng.FRandRange(TNBeachFleas::HopUpMin, TNBeachFleas::HopUpMax);
	return FVector(FMath::Cos(Angle) * Side, FMath::Sin(Angle) * Side, Up);
}

void ATN_BeachSandFleas::ApplyHops(ATortugaCharacter* Victim, int32& Done, float Age, bool bSkipLate)
{
	if (!IsValid(Victim) || Victim->IsDead() || Victim->IsKnockedDown() || Victim->IsInShell() || IsTurtleHeld(Victim))
	{
		return;
	}
	const int32 Total = NumHops();
	while (Done < Total && Age >= HopTime(Done))
	{
		if (!bSkipLate || Age - HopTime(Done) <= TNBeachFleas::HopLateLimit)
		{
			// Sin control: el saltito le cambia la velocidad entera (también la de lado).
			Victim->LaunchCharacter(HopVelocity(Done), true, true);
		}
		++Done;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachSandFleas::CrawlToward(const FVector& Goal, float Speed, float DeltaSeconds)
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
	FVector FromHome = Next - Home;
	FromHome.Z = 0.0;
	if (FromHome.Size() > LeashRadius)
	{
		Next = Home + FromHome.GetSafeNormal() * LeashRadius;
	}
	GroundTimer -= DeltaSeconds;
	if (GroundTimer <= 0.f)
	{
		GroundTimer = 0.3f;
		float Z = GroundZ;
		if (GroundHeightAt(Next, Z))
		{
			GroundZ = Z;
		}
	}
	Next.Z = FMath::FInterpTo(SimLoc.Z, static_cast<double>(GroundZ), static_cast<double>(DeltaSeconds), 5.0);
	ServerMoveTo(Next, static_cast<float>(Dir.Rotation().Yaw));
}

void ATN_BeachSandFleas::EndInfest(bool bDizzy)
{
	ATortugaCharacter* Victim = Infested;
	if (bDizzy && IsValid(Victim) && !Victim->IsDead())
	{
		StunTurtle(Victim, UTN_CombatTuning::Get().SandFleasDizzySeconds, FVector::ZeroVector);
	}
	if (Victim)
	{
		IgnoreTurtle(Victim, UTN_CombatTuning::Get().SandFleasIgnoreSeconds);
	}
	Infested = nullptr;
	ServerSetState(TNBeachFleas::ToByte(TNBeachFleas::EState::Scatter), SimLoc);
}

void ATN_BeachSandFleas::ServerTick(float DeltaSeconds)
{
	using TNBeachFleas::EState;
	using TNBeachFleas::ToByte;
	const EState State = static_cast<EState>(GetMoverState());
	const float Age = GetStateAge();
	const bool bStunned = IsHitStunned();
	const bool bLive = IsRaceLive(this);
	switch (State)
	{
	case EState::Wander:
	case EState::Hunt:
	{
		if (bStunned)
		{
			// Un golpe la dispersa.
			ServerSetState(ToByte(EState::Scatter), SimLoc);
			break;
		}
		ScanTimer -= DeltaSeconds;
		if (ScanTimer <= 0.f)
		{
			ScanTimer = TNBeachFleas::ScanPeriod;
			ATortugaCharacter* Seen = bLive ? FindTarget(SimLoc, DetectRadius, Home, LeashRadius) : nullptr;
			if (Seen && State == EState::Wander)
			{
				Target = Seen;
				ServerSetState(ToByte(EState::Hunt), Seen->GetActorLocation());
				break;
			}
			if (State == EState::Hunt && (!Seen || !IsTargetable(Target.Get())))
			{
				Target = Seen;
				if (!Seen)
				{
					ServerSetState(ToByte(EState::Wander), SimLoc);
					break;
				}
			}
		}
		if (State == EState::Hunt)
		{
			ATortugaCharacter* Victim = Target.Get();
			if (!IsTargetable(Victim))
			{
				break;
			}
			const FVector At = Victim->GetActorLocation();
			ServerSetAim(At);
			CrawlToward(At, TNBeachFleas::HuntSpeed * FMath::Sqrt(SizeK), DeltaSeconds);
			if (FVector::Dist2D(SimLoc, At) < CloudRadius + TNBeachFleas::ReachPad && FMath::Abs(At.Z - SimLoc.Z) < 400.0)
			{
				// Se le suben encima: la picada empieza ya (los saltitos cuentan desde la hora de este estado).
				Infested = Victim;
				ServerHops = 0;
				ServerSetState(ToByte(EState::Infest), At);
			}
		}
		else
		{
			if (Age > 8.f || FVector::Dist2D(SimLoc, WanderGoal) < 50.0 || WanderGoal.IsZero())
			{
				const float Angle = ServerRng.FRandRange(0.f, 2.f * PI);
				const float Dist = GetFootprintRadius() * 0.6f * FMath::Sqrt(ServerRng.FRandRange(0.1f, 1.f));
				WanderGoal = Home + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.f);
				if (Age > 8.f)
				{
					ServerSetState(ToByte(EState::Wander), WanderGoal);
				}
			}
			CrawlToward(WanderGoal, TNBeachFleas::WanderSpeed * SizeK, DeltaSeconds);
		}
		break;
	}

	case EState::Infest:
	{
		ATortugaCharacter* Victim = Infested;
		// Se acaba antes si un golpe dispersa la nube o si a la tortuga le pasa otra cosa (derribo, bola, pico...).
		if (!IsValid(Victim) || Victim->IsDead() || bStunned || Victim->IsKnockedDown() || TNBeach::IsTurtleStunned(Victim) || IsTurtleHeld(Victim))
		{
			EndInfest(false);
			break;
		}
		FVector At = Victim->GetActorLocation();
		float Z = static_cast<float>(At.Z);
		if (GroundHeightAt(At, Z))
		{
			At.Z = Z;
		}
		ServerMoveTo(At, SimYaw);
		ApplyHops(Victim, ServerHops, Age, false);
		if (Age >= TNBeachFleas::InfestTime)
		{
			EndInfest(true);
		}
		break;
	}

	case EState::Scatter:
	default:
		if (!bStunned && Age >= TNBeachFleas::ScatterTime)
		{
			WanderGoal = FVector::ZeroVector;
			ServerSetState(ToByte(EState::Wander), SimLoc);
		}
		break;
	}

	if (IsDebugDraw())
	{
		UWorld* World = GetWorld();
		DrawDebugCircle(World, SimLoc + FVector(0.0, 0.0, 30.0), DetectRadius, 40, FColor::Yellow, false, -1.f, 0, 8.f, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
		DrawDebugCircle(World, Home + FVector(0.0, 0.0, 30.0), LeashRadius, 40, FColor::Orange, false, -1.f, 0, 8.f, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
		DrawDebugCircle(World, SimLoc + FVector(0.0, 0.0, 30.0), CloudRadius + TNBeachFleas::ReachPad, 24, FColor::Red, false, -1.f, 0, 8.f, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Efectos y pulgas (todas las máquinas)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachSandFleas::OnMoverStateChanged(uint8 OldState)
{
	using TNBeachFleas::EState;
	if (!bHasScreen)
	{
		return;
	}
	const EState State = static_cast<EState>(GetMoverState());
	if (State == EState::Infest)
	{
		const FVector At = Mover.Aim;
		ItchTimer = 0.9f;
		if (Sound && LocalViewDistance(this, At) < 5000.f)
		{
			Sound->Play(ETNBeachCritterSfx::Itch, 1.f, 1.1f);
		}
		ShowPop(NSLOCTEXT("TNBeach", "FleasItch", "¡PICA, PICA!"), FColor(255, 120, 80), At + FVector(0.0, 0.0, 230.0), 140.f);
	}
	else if (State == EState::Scatter)
	{
		TNBeachKit::BurstAt(SandPuffs, ShownLoc, FVector::UpVector, 8);
	}
}

float ATN_BeachSandFleas::RandUnit()
{
	FleaRng ^= FleaRng << 13;
	FleaRng ^= FleaRng >> 17;
	FleaRng ^= FleaRng << 5;
	return static_cast<float>(FleaRng & 0xFFFFFF) / 16777216.f;
}

float ATN_BeachSandFleas::PlaneZ(const FVector& P) const
{
	return static_cast<float>(PlaneOrigin.Z + (P.X - PlaneOrigin.X) * PlaneSlope.X + (P.Y - PlaneOrigin.Y) * PlaneSlope.Y);
}

float ATN_BeachSandFleas::ScatterAmount(uint8 State, float Age) const
{
	if (IsHitStunned())
	{
		return 1.f;
	}
	if (static_cast<TNBeachFleas::EState>(State) != TNBeachFleas::EState::Scatter)
	{
		return 0.f;
	}
	return 1.f - static_cast<float>(TNProcMap::SmoothStep(TNBeachFleas::ScatterOut, TNBeachFleas::ScatterTime, Age));
}

FVector ATN_BeachSandFleas::PickTarget(uint8 State, float Age, float& OutHeight, float& OutDuration, float& OutRest)
{
	const float Angle = RandUnit() * 2.f * PI;
	if (bAnchorOnVictim)
	{
		// Picando: casi todas encima del caparazón y alguna en la arena de alrededor; saltitos cortos y frenéticos.
		OutDuration = 0.16f + 0.12f * RandUnit();
		OutRest = 0.02f + 0.08f * RandUnit();
		if (RandUnit() < 0.75f)
		{
			const float Rad = 25.f + 45.f * RandUnit();
			OutHeight = 25.f + 35.f * RandUnit();
			return FVector(FMath::Cos(Angle) * Rad, FMath::Sin(Angle) * Rad, 15.f + 45.f * RandUnit());
		}
		const float Rad = 70.f + 90.f * RandUnit();
		OutHeight = 40.f + 40.f * RandUnit();
		const FVector Off(FMath::Cos(Angle) * Rad, FMath::Sin(Angle) * Rad, 0.f);
		return FVector(Off.X, Off.Y, PlaneZ(Anchor + Off) - Anchor.Z);
	}
	const float Spread = ScatterAmount(State, Age);
	const float Radius = FMath::Lerp(CloudRadius, TNBeachFleas::ScatterRadius * SizeK, Spread);
	const float Rad = Radius * FMath::Sqrt(0.05f + 0.95f * RandUnit());
	FVector Off(FMath::Cos(Angle) * Rad, FMath::Sin(Angle) * Rad, 0.f);
	if (static_cast<TNBeachFleas::EState>(State) == TNBeachFleas::EState::Hunt)
	{
		// Hacia la tortuga: la nube se estira hacia delante.
		FVector Ahead = FVector(Mover.Aim) - Anchor;
		Ahead.Z = 0.0;
		Off += Ahead.GetSafeNormal() * (CloudRadius * 0.6f * RandUnit());
	}
	Off.Z = PlaneZ(Anchor + Off) - Anchor.Z;
	OutDuration = (0.28f + 0.2f * RandUnit()) * (1.f + 0.8f * Spread);
	OutHeight = (40.f + 70.f * RandUnit()) * SizeK * (1.f + 1.5f * Spread);
	OutRest = 0.05f + 0.3f * RandUnit();
	return Off;
}

void ATN_BeachSandFleas::BuildSwarm()
{
	if (!bHasScreen || FleaDots)
	{
		return;
	}
	using TNProcMesh::FTNProcMeshBuffers;
	const uint32 Variant = static_cast<uint32>(((Spec.Seed % 2) + 2) % 2);
	UStaticMesh* FleaM = TNBeachKit::CachedMesh(FString::Printf(TEXT("Beach.Flea.%u"), Variant), [Variant](FTNProcMeshBuffers& M)
	{
		TNBeachCritterMeshes::BuildFlea(M, Variant);
	});
	FleaDots = TNBeachCritterKit::AddInstances(this, GetRootComponent(), FleaM, TNBeachFleas::NumFleas, true, false, TN_ART("Beach.SandFleas.Flea"));
	FleaXf.Init(FTransform(FQuat::Identity, GetActorLocation(), FVector::ZeroVector), TNBeachFleas::NumFleas);
	Fleas.SetNum(TNBeachFleas::NumFleas);
	for (FFlea& Flea : Fleas)
	{
		const float Angle = RandUnit() * 2.f * PI;
		const float Rad = CloudRadius * FMath::Sqrt(RandUnit());
		Flea.To = FVector(FMath::Cos(Angle) * Rad, FMath::Sin(Angle) * Rad, 0.f);
		Flea.From = Flea.To;
		Flea.Start = -RandUnit();
		Flea.Scale = (0.8f + 0.45f * RandUnit()) * SizeK;
		Flea.Yaw = RandUnit() * 360.f;
	}
	Haze = TNBeachKit::AddShadow(this, 0.16f);
	using TNAmbientFX::EShape;
	TNAmbientFX::FEmitterDesc PuffDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.88f, 0.8f, 0.6f), true, 0.45f, 24, 0.f, 160.f, 0.f, 0.5f, 1.f, 40.f, 110.f);
	PuffDesc.Buoyancy = 30.f;
	PuffDesc.SpawnRadius = 250.f;
	TNBeachKit::InitEmitter(SandPuffs, this, PuffDesc, static_cast<uint32>(Spec.Seed) + 53u);
	Sound = UTN_BeachCritterSynthComponent::AttachTo(this, GetRootComponent(), 600.f, 4000.f);
	Anchor = GetActorLocation();
	PlaneOrigin = Anchor;
}

void ATN_BeachSandFleas::TickFleas(uint8 State, float Age, float DeltaSeconds)
{
	for (int32 i = 0; i < Fleas.Num(); ++i)
	{
		FFlea& Flea = Fleas[i];
		if (VisualClock >= Flea.Start + Flea.Duration + Flea.Rest)
		{
			float Height = 60.f;
			float Duration = 0.3f;
			float Rest = 0.1f;
			Flea.From = Flea.To;
			Flea.To = PickTarget(State, Age, Height, Duration, Rest);
			Flea.Start = VisualClock;
			Flea.Duration = Duration;
			Flea.Height = Height;
			Flea.Rest = Rest;
			const FVector Hop = Flea.To - Flea.From;
			if (Hop.SizeSquared2D() > 1.0)
			{
				Flea.Yaw = static_cast<float>(Hop.Rotation().Yaw);
			}
		}
		const float U = FMath::Clamp((VisualClock - Flea.Start) / FMath::Max(0.05f, Flea.Duration), 0.f, 1.f);
		const FVector Pos = Anchor + FMath::Lerp(Flea.From, Flea.To, static_cast<double>(U)) + FVector(0.0, 0.0, Flea.Height * 4.f * U * (1.f - U));
		// Cabecea hacia arriba al subir y hacia abajo al bajar.
		const float Pitch = U < 1.f ? 35.f * (1.f - 2.f * U) : 0.f;
		FleaXf[i] = FTransform(FRotator(Pitch, Flea.Yaw, 0.f), Pos, FVector(Flea.Scale));
	}
	TNBeachCritterKit::WriteInstances(FleaDots, FleaXf, true);
}

void ATN_BeachSandFleas::VisualTick(float DeltaSeconds)
{
	using TNBeachFleas::EState;
	VisualClock += DeltaSeconds;
	const uint8 StateByte = GetMoverState();
	const EState State = static_cast<EState>(StateByte);
	const float Age = GetStateAge();
	ATortugaCharacter* Victim = Infested;

	// El dueño de la tortuga picada da los mismos saltitos que el servidor, a la misma hora.
	if (!HasAuthority() && State == EState::Infest && IsValid(Victim) && Victim->IsLocallyControlled())
	{
		if (LocalHopSerial != Mover.Serial)
		{
			LocalHopSerial = Mover.Serial;
			LocalHops = 0;
		}
		ApplyHops(Victim, LocalHops, Age, true);
	}
	if (!FleaDots)
	{
		return;
	}

	// Ancla: la tortuga picada o el centro del enjambre. Al cambiar, cada pulga sigue desde donde está.
	const bool bOnVictim = State == EState::Infest && IsValid(Victim);
	const FVector NewAnchor = bOnVictim ? Victim->GetActorLocation() : ShownLoc;
	if (bOnVictim != bAnchorOnVictim)
	{
		for (FFlea& Flea : Fleas)
		{
			const float U = FMath::Clamp((VisualClock - Flea.Start) / FMath::Max(0.05f, Flea.Duration), 0.f, 1.f);
			const FVector Where = Anchor + FMath::Lerp(Flea.From, Flea.To, static_cast<double>(U));
			Flea.From = Where - NewAnchor;
			Flea.To = Flea.From;
			// Salta ya (cada una con un pelín de retraso).
			Flea.Start = VisualClock - Flea.Duration - Flea.Rest + 0.1f * RandUnit();
		}
		bAnchorOnVictim = bOnVictim;
		if (bOnVictim)
		{
			ItchTimer = 0.9f;
		}
	}
	Anchor = NewAnchor;

	const bool bVisible = ViewDistance < GetVisualRange();
	TNBeachCritterKit::SetShown(FleaDots, bVisible);
	if (Sound)
	{
		// Zumbido de fondo del enjambre suelto: bajo y de cerca (era 0,45 hasta 40 m: un chisporroteo agudo casi continuo).
		const float Level = !bVisible || ViewDistance > 3000.f ? 0.f : (bOnVictim ? 1.f : 0.3f);
		Sound->SetWorldLocation(bOnVictim ? NewAnchor : ShownLoc);
		Sound->SetSwarm(Level);
	}
	// La mancha de la nube en la arena, de lejos (también la dispersa, más clara).
	const float Spread = ScatterAmount(StateByte, Age);
	TNBeachKit::PlaceShadow(Haze, FVector(ShownLoc.X, ShownLoc.Y, PlaneZ(ShownLoc)), bOnVictim ? 0.f : FMath::Lerp(CloudRadius * 1.25f, TNBeachFleas::ScatterRadius * SizeK * 0.6f, Spread));
	if (!bVisible)
	{
		return;
	}

	// Plano del suelo alrededor (tres consultas cada 0,4 s): las pulgas caen en la arena sin trazas.
	PlaneTimer -= DeltaSeconds;
	if (PlaneTimer <= 0.f || FVector::DistSquared2D(PlaneOrigin, ShownLoc) > FMath::Square(300.0))
	{
		PlaneTimer = 0.4f;
		float Z0 = static_cast<float>(ShownLoc.Z);
		float ZX = Z0;
		float ZY = Z0;
		GroundHeightAt(ShownLoc, Z0);
		GroundHeightAt(ShownLoc + FVector(200.0, 0.0, 0.0), ZX);
		GroundHeightAt(ShownLoc + FVector(0.0, 200.0, 0.0), ZY);
		PlaneOrigin = FVector(ShownLoc.X, ShownLoc.Y, Z0);
		PlaneSlope = FVector2D((ZX - Z0) / 200.0, (ZY - Z0) / 200.0);
	}
	TickFleas(StateByte, Age, DeltaSeconds);

	// Picor: otro chisporroteo y «¡QUÉ PICOR!» a mitad de la picada.
	if (bOnVictim)
	{
		ItchTimer -= DeltaSeconds;
		if (ItchTimer <= 0.f)
		{
			ItchTimer = 10.f;
			if (Sound)
			{
				Sound->Play(ETNBeachCritterSfx::Itch, 1.2f, 0.9f);
			}
			ShowPop(NSLOCTEXT("TNBeach", "FleasItchMore", "¡QUÉ PICOR!"), FColor(255, 170, 90), NewAnchor + FVector(0.0, 0.0, 240.0), 130.f);
			if (Victim->IsLocallyControlled())
			{
				UTN_BeachCameraShake::Kick(this, NewAnchor, 0.2f, 300.f, 1200.f);
			}
		}
	}
	FVector View = ShownLoc;
	TNBeachKit::LocalCamera(GetWorld(), View);
	SandPuffs.Origin = FVector(ShownLoc.X, ShownLoc.Y, PlaneZ(ShownLoc) + 20.f);
	SandPuffs.RateScale = (!bOnVictim && Spread < 0.5f && ViewDistance < 6000.f) ? 1.5f : 0.f;
	TNBeachKit::TickEmitterIfBusy(SandPuffs, DeltaSeconds, View);
}
