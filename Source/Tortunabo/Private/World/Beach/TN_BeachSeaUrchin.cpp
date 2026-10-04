#include "World/Beach/TN_BeachSeaUrchin.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachEnemySynth.h"
#include "World/Beach/TN_BeachStun.h"
#include "TN_BeachEnemyKit.h"
#include "TN_BeachEnemyMeshes.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_CombatTuning.h"

namespace TNBeachUrchin
{
	/** Estados del erizo (Mover.State). Dazed: mareado por algo que se le ha lanzado (ni rueda ni pincha). */
	enum class EState : uint8 { Idle, Wander, Roll, Recoil, Dazed };

	inline uint8 ToByte(EState E)
	{
		return static_cast<uint8>(E);
	}

	/** Nota a una tortuga a esta distancia (cm, por las vibraciones: alrededor) y no se aleja de su sitio más que la correa (o 1,35 veces la huella). */
	constexpr float DetectRadius = 2200.f;
	constexpr float LeashRadius = 1600.f;
	/** Velocidades (cm/s): paseo y rodar hacia una tortuga (la tortuga anda a 450: solo pilla a quien se despista). */
	constexpr float WanderSpeed = 120.f;
	constexpr float RollSpeed = 210.f;
	/** Paseos: casi seguidos, con respiros cortos, hasta este tanto de la huella; si no llega en este tiempo, otro. */
	constexpr float RestMin = 0.6f;
	constexpr float RestMax = 1.6f;
	constexpr float WanderReach = 0.8f;
	constexpr float WanderTimeout = 7.f;
	/** Retroceso tras pinchar: velocidad y duración. */
	constexpr float RecoilSpeed = 260.f;
	constexpr float RecoilTime = 0.8f;
	/** Pinchazo: empujón (cm/s) y vuelta del ragdoll (grados/s); el derribo y la inmunidad, en UTN_CombatTuning. */
	constexpr float PushSpeed = 620.f;
	constexpr float PushUp = 380.f;
	constexpr float PushSpin = 260.f;
}

ATN_BeachSeaUrchin::ATN_BeachSeaUrchin()
{
	NetFrequencyNear = 8.f;
	SetNetUpdateFrequency(NetFrequencyNear);
	bThrottleWhenFar = true;
}

float ATN_BeachSeaUrchin::GetBodyRadius() const
{
	return RollRadius * 1.15f;
}

void ATN_BeachSeaUrchin::ApplySpec()
{
	SizeK = FMath::Clamp(Spec.SizeScale, 0.75f, 1.35f);
	RollRadius = static_cast<float>(TNBeachMeshes::UrchinRollRadius()) * SizeK;
	HitDistance = RollRadius * 0.95f + 45.f;
	DetectRadius = TNBeachUrchin::DetectRadius * FMath::Sqrt(SizeK);
	LeashRadius = FMath::Max(TNBeachUrchin::LeashRadius, GetFootprintRadius() * 1.35f);
	GroundZ = static_cast<float>(GetActorLocation().Z);
	if (Ball)
	{
		return;
	}
	USceneComponent* RigRoot = MakeRig();
	if (!bHasScreen)
	{
		return;
	}
	const int32 Pal = ((Spec.Seed % 4) + 4) % 4;
	const TNBeachMeshes::FUrchinLook Look = TNBeachMeshes::UrchinPalette(Pal);
	const uint32 ShapeSeed = static_cast<uint32>(Pal) * 7u + 3u;
	UStaticMesh* Mesh = TNBeachKit::CachedMesh(FString::Printf(TEXT("Beach.Urchin.%d"), Pal), [&Look, ShapeSeed](TNProcMesh::FTNProcMeshBuffers& M)
	{
		TNBeachMeshes::BuildUrchin(M, Look, ShapeSeed);
	});
	Ball = TNBeachKit::AddPart(this, RigRoot, Mesh, FVector(0.0, 0.0, RollRadius), true, TN_ART("Beach.SeaUrchin.Ball"));
	if (Ball)
	{
		Ball->SetUsingAbsoluteRotation(true);
		Ball->SetRelativeScale3D(FVector(SizeK));
		Spin = FQuat(FVector::UpVector, TNBeachKit::Hash01(static_cast<uint32>(Spec.Seed)) * 2.0 * PI);
		Ball->SetWorldRotation(Spin);
	}
	using TNAmbientFX::EShape;
	TNAmbientFX::FEmitterDesc DustDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.86f, 0.77f, 0.58f), true, 0.5f, 24, 0.f, 120.f, 0.f, 0.8f, 1.6f, 80.f, 190.f);
	DustDesc.Buoyancy = 30.f;
	DustDesc.SpawnRadius = 80.f;
	TNBeachKit::InitEmitter(Dust, this, DustDesc, static_cast<uint32>(Spec.Seed) + 21u);
	GetVoice(RigRoot, 900.f, 7000.f);
	LastShown = ShownLoc;
}

void ATN_BeachSeaUrchin::RollToward(const FVector& Goal, float MoveSpeed, float DeltaSeconds)
{
	FVector Flat = Goal - SimLoc;
	Flat.Z = 0.0;
	const double Dist = Flat.Size();
	if (Dist < 1.0)
	{
		return;
	}
	const FVector Dir = Flat / Dist;
	FVector Next = SimLoc + Dir * FMath::Min(Dist, static_cast<double>(MoveSpeed * DeltaSeconds));
	// Sin meterse en otro enemigo ni en lo grande del reparto (lo rodea rodando).
	Next = ResolveStep(Next, GetBodyRadius(), true);
	FVector FromHome = Next - Home;
	FromHome.Z = 0.0;
	if (FromHome.Size() > LeashRadius)
	{
		Next = Home + FromHome.GetSafeNormal() * LeashRadius;
	}
	GroundTimer -= DeltaSeconds;
	if (GroundTimer <= 0.f)
	{
		GroundTimer = 0.12f;
		float Z = GroundZ;
		if (GroundHeightAt(Next, Z))
		{
			GroundZ = Z;
		}
	}
	Next.Z = FMath::FInterpTo(SimLoc.Z, static_cast<double>(GroundZ), static_cast<double>(DeltaSeconds), 6.0);
	ServerMoveTo(Next, static_cast<float>(Dir.Rotation().Yaw));
}

bool ATN_BeachSeaUrchin::CheckPricks()
{
	if (!IsRaceLive(this))
	{
		return false;
	}
	TArray<ATortugaCharacter*> Turtles;
	GatherTurtles(this, Turtles);
	const FVector Center = SimLoc + FVector(0.0, 0.0, RollRadius);
	bool bAny = false;
	for (ATortugaCharacter* Turtle : Turtles)
	{
		if (!IsTargetable(Turtle))
		{
			continue;
		}
		const FVector At = Turtle->GetActorLocation();
		if (FVector::Dist2D(At, Center) > HitDistance || FMath::Abs(At.Z - Center.Z) > RollRadius * 1.6f)
		{
			continue;
		}
		FVector Away = At - Center;
		Away.Z = 0.0;
		Away = Away.IsNearlyZero() ? FRotator(0.f, SimYaw, 0.f).Vector() : Away.GetSafeNormal();
		// Pinchazo: derribo con ragdoll y mareo, despedida hacia fuera y dando una vuelta hacia atrás.
		const FVector Tumble = FVector::CrossProduct(FVector::UpVector, Away) * TNBeachUrchin::PushSpin;
		KnockDownTurtle(Turtle, UTN_CombatTuning::Get().SeaUrchinKnockSeconds, Away * TNBeachUrchin::PushSpeed + FVector(0.0, 0.0, TNBeachUrchin::PushUp), Tumble);
		IgnoreTurtle(Turtle, UTN_CombatTuning::Get().SeaUrchinIgnoreSeconds);
		MulticastPrick(Turtle, (At + Center) * 0.5);
		ServerSetState(TNBeachUrchin::ToByte(TNBeachUrchin::EState::Recoil), At);
		bAny = true;
	}
	return bAny;
}

void ATN_BeachSeaUrchin::ServerTick(float DeltaSeconds)
{
	using TNBeachUrchin::EState;
	using TNBeachUrchin::ToByte;
	StateLeft -= DeltaSeconds;
	// Mareado por lo que se le ha lanzado: quieto, tambaleándose, sin rodar ni pinchar (se le puede pasar al lado).
	if (IsHitStunned())
	{
		if (static_cast<EState>(GetMoverState()) != EState::Dazed)
		{
			Target.Reset();
			ServerSetState(ToByte(EState::Dazed));
		}
		return;
	}
	if (CheckPricks())
	{
		return;
	}
	const EState State = static_cast<EState>(GetMoverState());
	const bool bLive = IsRaceLive(this);
	switch (State)
	{
	case EState::Idle:
	case EState::Wander:
	{
		if (bLive)
		{
			if (ATortugaCharacter* Seen = FindTarget(SimLoc, DetectRadius, Home, LeashRadius * 1.15f))
			{
				Target = Seen;
				ServerSetState(ToByte(EState::Roll));
				break;
			}
		}
		if (State == EState::Idle)
		{
			if (StateLeft <= 0.f)
			{
				// Paseo a otro punto de su zona (nunca dentro de lo grande del reparto).
				for (int32 Try = 0; Try < 6; ++Try)
				{
					const float Angle = ServerRng.FRandRange(0.f, 2.f * PI);
					const float Dist = GetFootprintRadius() * TNBeachUrchin::WanderReach * FMath::Sqrt(ServerRng.FRandRange(0.2f, 1.f));
					WanderGoal = Home + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.f);
					if (!IsInsideObstacle(WanderGoal, GetBodyRadius()))
					{
						break;
					}
					WanderGoal = Home;
				}
				ServerSetState(ToByte(EState::Wander), WanderGoal);
			}
		}
		else
		{
			RollToward(WanderGoal, TNBeachUrchin::WanderSpeed * SizeK, DeltaSeconds);
			if (FVector::Dist2D(SimLoc, WanderGoal) < 80.0 || GetStateAge() > TNBeachUrchin::WanderTimeout)
			{
				StateLeft = ServerRng.FRandRange(TNBeachUrchin::RestMin, TNBeachUrchin::RestMax);
				ServerSetState(ToByte(EState::Idle));
			}
		}
		break;
	}
	case EState::Roll:
	{
		ATortugaCharacter* Victim = Target.Get();
		if (!bLive || !IsTargetable(Victim) || FVector::Dist2D(Victim->GetActorLocation(), Home) > LeashRadius * 1.15f)
		{
			Target = bLive ? FindTarget(SimLoc, DetectRadius, Home, LeashRadius * 1.15f) : nullptr;
			if (!Target.IsValid())
			{
				StateLeft = ServerRng.FRandRange(TNBeachUrchin::RestMin, TNBeachUrchin::RestMax);
				ServerSetState(ToByte(EState::Idle));
			}
			break;
		}
		RollToward(Victim->GetActorLocation(), TNBeachUrchin::RollSpeed * FMath::Sqrt(SizeK), DeltaSeconds);
		break;
	}
	case EState::Dazed:
		// Se le ha pasado el mareo: un respiro y a pasear.
		StateLeft = TNBeachUrchin::RestMin;
		ServerSetState(ToByte(EState::Idle));
		break;
	case EState::Recoil:
	default:
	{
		FVector Away = SimLoc - FVector(Mover.Aim);
		Away.Z = 0.0;
		Away = Away.IsNearlyZero() ? FVector::ForwardVector : Away.GetSafeNormal();
		RollToward(SimLoc + Away * 500.0, TNBeachUrchin::RecoilSpeed, DeltaSeconds);
		if (GetStateAge() >= TNBeachUrchin::RecoilTime)
		{
			StateLeft = TNBeachUrchin::RestMin;
			ServerSetState(ToByte(EState::Idle));
		}
		break;
	}
	}

	if (IsDebugDraw())
	{
		UWorld* World = GetWorld();
		DrawDebugCircle(World, SimLoc + FVector(0.0, 0.0, 30.0), DetectRadius, 40, FColor::Yellow, false, -1.f, 0, 8.f, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
		DrawDebugCircle(World, Home + FVector(0.0, 0.0, 30.0), LeashRadius, 40, FColor::Orange, false, -1.f, 0, 8.f, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
		DrawDebugCircle(World, SimLoc + FVector(0.0, 0.0, 30.0), HitDistance, 24, FColor::Red, false, -1.f, 0, 8.f, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
	}
}

void ATN_BeachSeaUrchin::MulticastPrick_Implementation(ATortugaCharacter* Victim, FVector_NetQuantize Where)
{
	if (!bHasScreen)
	{
		return;
	}
	const FVector At = Where;
	if (Voice)
	{
		Voice->Play(ETNBeachSfx::Prick, 1.f, 1.2f);
	}
	TNBeachKit::BurstAt(Dust, At, FVector::UpVector, 8);
	UTN_BeachCameraShake::Kick(this, At, 0.45f, 400.f, 2000.f);
	ShowPop(NSLOCTEXT("TNBeach", "UrchinPrick", "¡PINCHAZO!"), FColor(190, 90, 255), At + FVector(0.0, 0.0, 260.0));
}

void ATN_BeachSeaUrchin::VisualTick(float DeltaSeconds)
{
	Clock += DeltaSeconds;
	FVector Delta = ShownLoc - LastShown;
	Delta.Z = 0.0;
	LastShown = ShownLoc;
	const double Dist = Delta.Size();
	Speed = FMath::FInterpTo(Speed, static_cast<float>(Dist / FMath::Max(DeltaSeconds, 1e-3f)), DeltaSeconds, 5.f);

	// Rueda de verdad: gira sobre el eje perpendicular al avance lo que ha recorrido entre su radio.
	if (Ball && Dist > 0.01 && RollRadius > 1.f)
	{
		const FVector Axis = FVector::CrossProduct(FVector::UpVector, Delta / Dist);
		Spin = (FQuat(Axis, Dist / RollRadius) * Spin).GetNormalized();
	}
	if (Ball && ViewDistance < GetVisualRange())
	{
		// Respira: las púas se abren y cierran un poco; al rodar se aplasta apenas. Mareado, se tambalea en el sitio.
		const float Breath = 1.f + 0.035f * FMath::Sin(Clock * 2.1f);
		const bool bDazed = static_cast<TNBeachUrchin::EState>(GetMoverState()) == TNBeachUrchin::EState::Dazed;
		const float Wobble = bDazed ? 1.f : 0.f;
		const FQuat Tilt(FVector(FMath::Cos(Clock * 3.1f), FMath::Sin(Clock * 3.1f), 0.0), FMath::DegreesToRadians(9.f * Wobble));
		Ball->SetWorldRotation(Tilt * Spin);
		Ball->SetRelativeScale3D(FVector(SizeK * Breath));
		Ball->SetRelativeLocation(FVector(35.0 * Wobble * FMath::Sin(Clock * 3.1f), 35.0 * Wobble * FMath::Cos(Clock * 3.1f),
			RollRadius * (1.0 - 0.02 * FMath::Min(1.f, Speed / 200.f))));
	}

	FVector View = ShownLoc;
	TNBeachKit::LocalCamera(GetWorld(), View);
	Dust.Origin = ShownLoc + FVector(0.0, 0.0, 40.0);
	Dust.Desc.Direction = FVector(0.0, 0.0, 1.0);
	Dust.RateScale = ViewDistance < GetVisualRange() ? FMath::Clamp(Speed / 150.f, 0.f, 1.f) * 6.f : 0.f;
	TNBeachKit::TickEmitterIfBusy(Dust, DeltaSeconds, View);

	if (Voice && ViewDistance < 9000.f && Speed > 40.f)
	{
		RollSoundTimer -= DeltaSeconds;
		if (RollSoundTimer <= 0.f)
		{
			RollSoundTimer = 0.5f;
			Voice->Play(ETNBeachSfx::Roll, 0.8f + 0.2f / SizeK, FMath::Clamp(Speed / 200.f, 0.2f, 0.8f));
		}
	}
}
