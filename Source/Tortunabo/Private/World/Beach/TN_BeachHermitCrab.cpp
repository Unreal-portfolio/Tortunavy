#include "World/Beach/TN_BeachHermitCrab.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachCritterSynth.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/ProcMap/TN_ProcMapMath.h"
#include "TN_BeachCritterKit.h"
#include "TN_BeachCritterMeshes.h"
#include "TN_BeachEnemyKit.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_CombatTuning.h"

namespace TNBeachHermit
{
	/** Estados del ermitaño (Mover.State). */
	enum class EState : uint8 { Wait, Hide, Roll, Emerge, Walk, Settle, Dizzy };

	inline uint8 ToByte(EState E)
	{
		return static_cast<uint8>(E);
	}

	/** Largo de la calle sin Extent (cm, por el tamaño). */
	constexpr float DefaultLane = 4000.f;
	/** La calle, para saltar: semiancho (por el tamaño), desde dónde por delante de él y hasta cuánto antes del final. */
	constexpr float TriggerHalfWidth = 520.f;
	constexpr float TriggerAhead = 250.f;
	constexpr float TriggerEndMargin = 200.f;
	constexpr float TriggerPeriod = 0.1f;
	/** Tras volver a lo alto, tiempo antes de poder rodar otra vez. */
	constexpr float ReArmTime = 1.2f;
	/** Meterse en la concha antes de rodar. */
	constexpr float HideTime = 0.55f;
	/** Rodada (cm, cm/s y cm/s²): arranque, mínima, máxima (por la raíz del tamaño), aceleración propia y la de la cuesta. */
	constexpr float StartSpeed = 250.f;
	constexpr float MinSpeed = 300.f;
	constexpr float MaxSpeed = 1500.f;
	constexpr float BaseAccel = 380.f;
	constexpr float SlopeAccel = 900.f;
	/** Botes: impacto mínimo para rebotar, rebote y botes sueltos (cm/s hacia arriba); la gravedad, en UTN_CombatTuning. */
	constexpr float BounceMin = 260.f;
	constexpr float Bounce = 0.32f;
	constexpr float HopMin = 170.f;
	constexpr float HopMax = 330.f;
	/** Culebreo: amplitud (por el tamaño), largo de onda y tramo en que crece. */
	constexpr float WeaveAmp = 120.f;
	constexpr float WeaveLen = 1500.f;
	constexpr float WeaveRamp = 500.f;
	/** Frenada al final: tramo y deceleración mínima. */
	constexpr float BrakeLen = 700.f;
	constexpr float BrakeDecel = 450.f;
	/** Paso de la simulación del camino, cada cuánto se guarda una muestra y rodada más larga. */
	constexpr float SimDt = 1.f / 60.f;
	constexpr float SampleDt = 1.f / 30.f;
	constexpr float MaxRollTime = 14.f;
	/** Perfil del suelo de la calle: paso a lo largo y separación de las líneas de los lados (por el tamaño). */
	constexpr float ProfileStep = 150.f;
	constexpr float ProfileSide = 150.f;
	/** Al final: asoma, se sacude y se da la vuelta (s desde que empieza). */
	constexpr float PopOutAt = 0.35f;
	constexpr float ShakeEnd = 1.45f;
	constexpr float EmergeTime = 2.1f;
	/** Vuelta andando (cm/s, por la raíz del tamaño) y darse la vuelta arriba (s). */
	constexpr float WalkSpeed = 230.f;
	constexpr float SettleTime = 0.9f;
	/** Cabeceo de la caracola cargada (grados, la boca hacia abajo). */
	constexpr float CarryTilt = 15.f;
	/** Derribo: empujón (base, parte de la velocidad de la bola, de lado, hacia arriba y tope) y vuelta del ragdoll. */
	constexpr float PushBase = 320.f;
	constexpr float PushAlong = 0.55f;
	constexpr float PushSide = 240.f;
	constexpr float PushUp = 430.f;
	constexpr float PushMax = 1300.f;
	constexpr float PushSpin = 320.f;
	/** Holgura de la tortuga al chocar (cm). */
	constexpr float HitPad = 70.f;
	/** Mareado, como poco esto antes de volver andando. */
	constexpr float DizzyMinTime = 0.6f;
}

ATN_BeachHermitCrab::ATN_BeachHermitCrab()
{
	NetFrequencyNear = 8.f;
	SetNetUpdateFrequency(NetFrequencyNear);
	bThrottleWhenFar = true;
}

float ATN_BeachHermitCrab::GetBodyRadius() const
{
	return BallRadius * 0.9f;
}

float ATN_BeachHermitCrab::GetActiveRange() const
{
	return LaneLength + 3000.f;
}

bool ATN_BeachHermitCrab::GetHitCapsule(FVector& OutA, FVector& OutB, float& OutRadius) const
{
	OutA = ShownLoc + FVector(0.0, 0.0, BallRadius);
	OutB = OutA;
	OutRadius = BallRadius;
	return true;
}

FVector ATN_BeachHermitCrab::GetHitStunAnchor() const
{
	return ShownLoc + FVector(0.0, 0.0, BallRadius * 2.f + 90.f);
}

float ATN_BeachHermitCrab::GetHitStunScale() const
{
	return 2.2f * SizeK;
}

void ATN_BeachHermitCrab::ApplySpec()
{
	SizeK = FMath::Clamp(Spec.SizeScale, 0.8f, 1.25f);
	BallRadius = static_cast<float>(TNBeachCritterMeshes::HermitRadius()) * SizeK;
	LaneLength = Spec.Extent > 100.f ? Spec.Extent : TNBeachHermit::DefaultLane * SizeK;
	LaneHalfWidth = TNBeachHermit::TriggerHalfWidth * SizeK;
	BuildCrab();
}

// ─────────────────────────────────────────────────────────────────────────────
// Calle, perfil del suelo y camino de la rodada (todas las máquinas)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachHermitCrab::EnsureLane()
{
	if (bLaneReady)
	{
		return;
	}
	bLaneReady = true;
	FVector Axis = GetActorForwardVector();
	Axis.Z = 0.0;
	if (!Axis.Normalize())
	{
		Axis = FVector::ForwardVector;
	}
	const FVector Middle = GetActorLocation();
	FVector EndA = Middle - Axis * (LaneLength * 0.5f);
	FVector EndB = Middle + Axis * (LaneLength * 0.5f);
	float ZA = static_cast<float>(EndA.Z);
	float ZB = static_cast<float>(EndB.Z);
	GroundHeightAt(EndA, ZA);
	GroundHeightAt(EndB, ZB);
	EndA.Z = ZA;
	EndB.Z = ZB;
	// Lo alto es el extremo de atrás (-X local), como lo pone el reparto (la calle baja hacia +X); solo si el de delante
	// queda claramente más alto (puesto a mano cuesta arriba), rueda al revés.
	const bool bATop = ZB <= ZA + 60.f;
	LaneTop = bATop ? EndA : EndB;
	LaneBottom = bATop ? EndB : EndA;
	ServerMoveTo(LaneTop, static_cast<float>(LaneDir().Rotation().Yaw));
	ServerSetState(TNBeachHermit::ToByte(TNBeachHermit::EState::Wait), LaneBottom);
}

bool ATN_BeachHermitCrab::ResolveLane()
{
	if (bLaneReady)
	{
		return true;
	}
	if (HasAuthority())
	{
		EnsureLane();
		return true;
	}
	// Hasta que el servidor no ha elegido (primer cambio de estado) no se sabe qué extremo es lo alto.
	if (Mover.Serial == 0)
	{
		return false;
	}
	using TNBeachHermit::EState;
	const EState State = static_cast<EState>(GetMoverState());
	const bool bTopIsLocation = State == EState::Wait || State == EState::Hide || State == EState::Roll || State == EState::Settle;
	const FVector Hint = bTopIsLocation ? FVector(Mover.Location) : FVector(Mover.Aim);
	FVector Axis = GetActorForwardVector();
	Axis.Z = 0.0;
	if (!Axis.Normalize())
	{
		Axis = FVector::ForwardVector;
	}
	const FVector Middle = GetActorLocation();
	const FVector EndA = Middle - Axis * (LaneLength * 0.5f);
	const FVector EndB = Middle + Axis * (LaneLength * 0.5f);
	const bool bATop = FVector::DistSquared2D(EndA, Hint) < FVector::DistSquared2D(EndB, Hint);
	LaneTop = bATop ? EndA : EndB;
	LaneBottom = bATop ? EndB : EndA;
	LaneTop.Z = Hint.Z;
	LaneBottom.Z = Hint.Z;
	bLaneReady = true;
	return true;
}

FVector ATN_BeachHermitCrab::LaneDir() const
{
	FVector Dir = LaneBottom - LaneTop;
	Dir.Z = 0.0;
	return Dir.Normalize() ? Dir : GetActorForwardVector().GetSafeNormal2D();
}

void ATN_BeachHermitCrab::EnsureProfile(const FVector& Top, const FVector& Dir)
{
	if (bProfileReady && FVector::DistSquared2D(Top, ProfileTop) < 100.0)
	{
		return;
	}
	bProfileReady = true;
	ProfileTop = Top;
	ProfileDir = Dir;
	const FVector SideDir(-Dir.Y, Dir.X, 0.0);
	const float SideGap = TNBeachHermit::ProfileSide * SizeK;
	ProfileCount = FMath::CeilToInt32((LaneLength + 400.f) / TNBeachHermit::ProfileStep) + 1;
	Profile.SetNumUninitialized(ProfileCount * 3);
	for (int32 i = 0; i < ProfileCount; ++i)
	{
		for (int32 Line = 0; Line < 3; ++Line)
		{
			const FVector At = Top + Dir * (i * TNBeachHermit::ProfileStep) + SideDir * ((Line - 1) * SideGap);
			float Z = static_cast<float>(Top.Z);
			GroundHeightAt(At, Z);
			Profile[Line * ProfileCount + i] = Z;
		}
	}
}

float ATN_BeachHermitCrab::ProfileGround(float S, float Lat) const
{
	if (!bProfileReady || ProfileCount < 2)
	{
		return static_cast<float>(ProfileTop.Z);
	}
	const float Fi = FMath::Clamp(S / TNBeachHermit::ProfileStep, 0.f, static_cast<float>(ProfileCount - 1));
	const int32 I0 = FMath::Min(FMath::FloorToInt32(Fi), ProfileCount - 2);
	const float T = Fi - static_cast<float>(I0);
	auto LineAt = [this, I0, T](int32 Line)
	{
		return FMath::Lerp(Profile[Line * ProfileCount + I0], Profile[Line * ProfileCount + I0 + 1], T);
	};
	const float Li = FMath::Clamp(Lat / (TNBeachHermit::ProfileSide * SizeK), -1.f, 1.f);
	const float Mid = LineAt(1);
	const float Edge = LineAt(Li >= 0.f ? 2 : 0);
	return FMath::Lerp(Mid, Edge, FMath::Abs(Li));
}

float ATN_BeachHermitCrab::GroundAt(const FVector& Where) const
{
	if (bProfileReady)
	{
		const FVector D = Where - ProfileTop;
		const float Along = static_cast<float>(FVector::DotProduct(D, ProfileDir));
		const float Lat = static_cast<float>(D.X * -ProfileDir.Y + D.Y * ProfileDir.X);
		if (Along > -300.f && Along < LaneLength + 300.f && FMath::Abs(Lat) < TNBeachHermit::ProfileSide * SizeK * 1.6f)
		{
			return ProfileGround(Along, Lat);
		}
	}
	float Z = static_cast<float>(Where.Z);
	GroundHeightAt(Where, Z);
	return Z;
}

void ATN_BeachHermitCrab::BuildPath(const FVector& Top, const FVector& Bottom, uint32 Seed)
{
	RollPath.Reset();
	FVector Dir = Bottom - Top;
	Dir.Z = 0.0;
	const float Len = static_cast<float>(Dir.Size());
	Dir = Len > 1.f ? Dir / Len : GetActorForwardVector().GetSafeNormal2D();
	EnsureProfile(Top, Dir);
	PathTop = Top;
	PathDir = Dir;

	FRandomStream Rng(static_cast<int32>(Seed));
	const float Amp = TNBeachHermit::WeaveAmp * SizeK * Rng.FRandRange(0.7f, 1.2f);
	const float WaveLen = TNBeachHermit::WeaveLen * Rng.FRandRange(0.8f, 1.25f);
	const float Phase = Rng.FRandRange(0.f, 2.f * PI);
	const float TopSpeed = TNBeachHermit::MaxSpeed * FMath::Sqrt(SizeK);
	auto LatAt = [Amp, WaveLen, Phase, Len](float S)
	{
		const float Ramp = static_cast<float>(TNProcMap::SmoothStep(0.0, TNBeachHermit::WeaveRamp, S) * (1.0 - 0.6 * TNProcMap::SmoothStep(Len - 600.0, Len, S)));
		return Amp * Ramp * FMath::Sin(2.f * PI * S / WaveLen + Phase);
	};

	float S = 0.f;
	float V = TNBeachHermit::StartSpeed;
	float Vz = 0.f;
	float Spin = 0.f;
	float Z = ProfileGround(0.f, 0.f);
	float PrevG = Z;
	float HopTimer = Rng.FRandRange(0.4f, 0.8f);
	float LandAccum = 0.f;
	bool bAir = false;
	bool bDone = false;
	const int32 MaxSteps = FMath::CeilToInt32(TNBeachHermit::MaxRollTime / TNBeachHermit::SimDt);
	const int32 Every = FMath::Max(1, FMath::RoundToInt32(TNBeachHermit::SampleDt / TNBeachHermit::SimDt));
	const float Gravity = UTN_CombatTuning::Get().HermitCrabGravity;
	for (int32 Step = 0; Step <= MaxSteps && !bDone; ++Step)
	{
		const float Lat = LatAt(S);
		if (Step % Every == 0)
		{
			FTNHermitRollSample& Sample = RollPath.AddDefaulted_GetRef();
			Sample.S = S;
			Sample.Lat = Lat;
			Sample.Z = Z;
			Sample.Speed = V;
			Sample.Spin = Spin;
			Sample.Land = LandAccum;
			LandAccum = 0.f;
		}
		// A lo largo: acelera con la cuesta (y un poco siempre) y frena al final.
		const bool bBrake = S > Len - TNBeachHermit::BrakeLen * SizeK;
		if (bBrake)
		{
			const float Left = FMath::Max(Len - S, 20.f);
			V = FMath::Max(0.f, V - FMath::Max(V * V / (2.f * Left), TNBeachHermit::BrakeDecel) * TNBeachHermit::SimDt);
			bDone = V <= 1.f;
		}
		else
		{
			const float Ahead = ProfileGround(S + 60.f, Lat);
			const float Behind = ProfileGround(S - 60.f, Lat);
			const float Slope = FMath::Clamp((Behind - Ahead) / 120.f, -0.8f, 0.8f);
			V = FMath::Clamp(V + (TNBeachHermit::BaseAccel + TNBeachHermit::SlopeAccel * Slope) * TNBeachHermit::SimDt, TNBeachHermit::MinSpeed, TopSpeed);
		}
		S = FMath::Min(S + V * TNBeachHermit::SimDt, Len + 150.f);
		Spin += V * TNBeachHermit::SimDt / FMath::Max(1.f, BallRadius);
		bDone = bDone || S >= Len + 150.f;

		// En altura: cae con la gravedad; si toca el suelo, lo sigue (y sale despedida en las crestas) o rebota.
		const float G = ProfileGround(S, LatAt(S));
		const float GroundVz = FMath::Clamp((G - PrevG) / TNBeachHermit::SimDt, -V, V * 0.8f);
		PrevG = G;
		Vz -= Gravity * TNBeachHermit::SimDt;
		Z += Vz * TNBeachHermit::SimDt;
		if (Z <= G)
		{
			const float Impact = GroundVz - Vz;
			Z = G;
			if (bAir && Impact > TNBeachHermit::BounceMin)
			{
				Vz = GroundVz + Impact * TNBeachHermit::Bounce;
				LandAccum = FMath::Max(LandAccum, Impact);
			}
			else
			{
				if (bAir && Impact > 120.f)
				{
					LandAccum = FMath::Max(LandAccum, Impact);
				}
				Vz = GroundVz;
			}
			bAir = Vz - GroundVz > 60.f;
		}
		else if (Z > G + 2.f)
		{
			bAir = true;
		}
		// Botes sueltos de las piedrecitas cuando va deprisa.
		HopTimer -= TNBeachHermit::SimDt;
		if (!bAir && !bBrake && V > 500.f && HopTimer <= 0.f)
		{
			Vz += Rng.FRandRange(TNBeachHermit::HopMin, TNBeachHermit::HopMax) * (0.5f + 0.5f * V / TopSpeed);
			bAir = true;
			HopTimer = Rng.FRandRange(0.55f, 1.1f);
		}
	}
	// Última muestra: parada al final, en el suelo.
	FTNHermitRollSample& Last = RollPath.AddDefaulted_GetRef();
	Last.S = S;
	Last.Lat = LatAt(S);
	Last.Z = ProfileGround(S, Last.Lat);
	Last.Speed = 0.f;
	Last.Spin = Spin;
	Last.Land = LandAccum;
	PathDuration = static_cast<float>(RollPath.Num() - 1) * TNBeachHermit::SampleDt;
}

bool ATN_BeachHermitCrab::EnsurePath()
{
	if (bPathValid && PathSerial == Mover.Serial)
	{
		return true;
	}
	if (static_cast<TNBeachHermit::EState>(GetMoverState()) != TNBeachHermit::EState::Roll || !ResolveLane())
	{
		return false;
	}
	const uint32 Seed = static_cast<uint32>(Spec.Seed) * 7919u + static_cast<uint32>(Mover.Serial) * 131u;
	BuildPath(LaneTop, LaneBottom, Seed);
	PathSerial = Mover.Serial;
	bPathValid = RollPath.Num() > 1;
	return bPathValid;
}

FVector ATN_BeachHermitCrab::SamplePath(float Age, float& OutSpin, float& OutSpeed, int32& OutIndex) const
{
	if (RollPath.Num() == 0)
	{
		OutSpin = 0.f;
		OutSpeed = 0.f;
		OutIndex = 0;
		return PathTop;
	}
	const float Fi = FMath::Clamp(Age / TNBeachHermit::SampleDt, 0.f, static_cast<float>(RollPath.Num() - 1));
	const int32 I0 = FMath::Min(FMath::FloorToInt32(Fi), RollPath.Num() - 1);
	const int32 I1 = FMath::Min(I0 + 1, RollPath.Num() - 1);
	const float T = Fi - static_cast<float>(I0);
	const FTNHermitRollSample& A = RollPath[I0];
	const FTNHermitRollSample& B = RollPath[I1];
	const float S = FMath::Lerp(A.S, B.S, T);
	const float Lat = FMath::Lerp(A.Lat, B.Lat, T);
	OutSpin = FMath::Lerp(A.Spin, B.Spin, T);
	OutSpeed = FMath::Lerp(A.Speed, B.Speed, T);
	OutIndex = I0;
	const FVector SideDir(-PathDir.Y, PathDir.X, 0.0);
	FVector Pos = PathTop + PathDir * S + SideDir * Lat;
	Pos.Z = FMath::Lerp(A.Z, B.Z, T);
	return Pos;
}

FVector ATN_BeachHermitCrab::WalkPosition(float Age, float& OutSpeed) const
{
	const FVector Start = Mover.Location;
	const FVector Goal = Mover.Aim;
	FVector Flat = Goal - Start;
	Flat.Z = 0.0;
	const float Dist = static_cast<float>(Flat.Size());
	const float Speed = TNBeachHermit::WalkSpeed * FMath::Sqrt(SizeK);
	const float Travel = FMath::Min(Age * Speed, Dist);
	OutSpeed = Travel < Dist ? Speed : 0.f;
	FVector Pos = Dist > 1.f ? Start + Flat * (Travel / Dist) : Goal;
	Pos.Z = GroundAt(Pos);
	return Pos;
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_BeachHermitCrab::AnyTurtleInLane() const
{
	const FVector Dir = LaneDir();
	const FVector SideDir(-Dir.Y, Dir.X, 0.0);
	const float Len = static_cast<float>(FVector::Dist2D(LaneTop, LaneBottom));
	TArray<ATortugaCharacter*> Turtles;
	GatherTurtles(this, Turtles);
	for (const ATortugaCharacter* Turtle : Turtles)
	{
		if (!IsTargetable(Turtle))
		{
			continue;
		}
		const FVector D = Turtle->GetActorLocation() - LaneTop;
		const float Along = static_cast<float>(FVector::DotProduct(D, Dir));
		const float Lat = FMath::Abs(static_cast<float>(FVector::DotProduct(D, SideDir)));
		if (Along < TNBeachHermit::TriggerAhead * SizeK || Along > Len - TNBeachHermit::TriggerEndMargin || Lat > LaneHalfWidth)
		{
			continue;
		}
		// En altura, cerca de la calle (no en lo alto de algo que la cruza por encima).
		const float LaneZ = FMath::Lerp(static_cast<float>(LaneTop.Z), static_cast<float>(LaneBottom.Z), Along / FMath::Max(1.f, Len));
		if (FMath::Abs(static_cast<float>(Turtle->GetActorLocation().Z) - LaneZ) < 900.f)
		{
			return true;
		}
	}
	return false;
}

void ATN_BeachHermitCrab::CheckRollHits(const FVector& From, const FVector& To, float Speed)
{
	const FVector Dir = LaneDir();
	const FVector SideDir(-Dir.Y, Dir.X, 0.0);
	const double Reach = BallRadius + TNBeachHermit::HitPad;
	TArray<ATortugaCharacter*> Turtles;
	GatherTurtles(this, Turtles);
	for (ATortugaCharacter* Turtle : Turtles)
	{
		if (!IsTargetable(Turtle))
		{
			continue;
		}
		const FVector At = Turtle->GetActorLocation();
		double Param = 0.0;
		const double Dist = TNProcMap::DistPointSegment(FVector2D(At.X, At.Y), FVector2D(From.X, From.Y), FVector2D(To.X, To.Y), Param);
		if (Dist > Reach)
		{
			continue;
		}
		const FVector Hit = FMath::Lerp(From, To, Param);
		if (FMath::Abs(At.Z - Hit.Z) > BallRadius + 110.f)
		{
			continue;
		}
		// Como un bolo: hacia donde va la bola (más cuanto más deprisa), hacia su lado y hacia arriba, dando vueltas.
		const float SideSign = FVector::DotProduct(At - Hit, SideDir) >= 0.0 ? 1.f : -1.f;
		FVector Push = Dir * (TNBeachHermit::PushBase + Speed * TNBeachHermit::PushAlong) + SideDir * (SideSign * TNBeachHermit::PushSide);
		Push = Push.GetClampedToMaxSize2D(TNBeachHermit::PushMax);
		Push.Z = TNBeachHermit::PushUp;
		const FVector Tumble = FVector::CrossProduct(FVector::UpVector, Push.GetSafeNormal2D()) * TNBeachHermit::PushSpin;
		KnockDownTurtle(Turtle, UTN_CombatTuning::Get().HermitCrabKnockSeconds, Push, Tumble);
		IgnoreTurtle(Turtle, UTN_CombatTuning::Get().HermitCrabIgnoreSeconds);
		HitsThisRoll = static_cast<uint8>(FMath::Min(255, HitsThisRoll + 1));
		MulticastStrike(Turtle, (At + Hit) * 0.5, HitsThisRoll);
	}
}

void ATN_BeachHermitCrab::EnterDizzy(const FVector& Where, float Yaw)
{
	ServerMoveTo(Where, Yaw);
	ServerSetState(TNBeachHermit::ToByte(TNBeachHermit::EState::Dizzy), LaneTop);
}

void ATN_BeachHermitCrab::ServerTick(float DeltaSeconds)
{
	using TNBeachHermit::EState;
	using TNBeachHermit::ToByte;
	EnsureLane();
	const EState State = static_cast<EState>(GetMoverState());
	const float Age = GetStateAge();
	const bool bStunned = IsHitStunned();
	const float DownYaw = static_cast<float>(LaneDir().Rotation().Yaw);
	switch (State)
	{
	case EState::Wait:
		if (bStunned || Age < TNBeachHermit::ReArmTime)
		{
			break;
		}
		TriggerTimer -= DeltaSeconds;
		if (TriggerTimer <= 0.f)
		{
			TriggerTimer = TNBeachHermit::TriggerPeriod;
			if (AnyTurtleInLane())
			{
				ServerSetState(ToByte(EState::Hide), LaneBottom);
			}
		}
		break;

	case EState::Hide:
		if (bStunned)
		{
			ServerSetState(ToByte(EState::Wait), LaneBottom);
			break;
		}
		if (Age >= TNBeachHermit::HideTime)
		{
			HitsThisRoll = 0;
			bPrevBallValid = false;
			ServerMoveTo(LaneTop, DownYaw);
			ServerSetState(ToByte(EState::Roll), LaneBottom);
			EnsurePath();
		}
		break;

	case EState::Roll:
	{
		if (!EnsurePath())
		{
			EnterDizzy(LaneTop, DownYaw);
			break;
		}
		float Spin = 0.f;
		float Speed = 0.f;
		int32 Index = 0;
		const FVector BallBase = SamplePath(Age, Spin, Speed, Index);
		SimLoc = BallBase;
		const FVector Center = BallBase + FVector(0.0, 0.0, BallRadius);
		if (bStunned)
		{
			// Un golpe la para en seco: asoma mareado donde está.
			FVector Ground = BallBase;
			Ground.Z = GroundAt(BallBase);
			EnterDizzy(Ground, DownYaw);
			break;
		}
		CheckRollHits(bPrevBallValid ? PrevBallCenter : Center, Center, Speed);
		PrevBallCenter = Center;
		bPrevBallValid = true;
		if (Age >= PathDuration)
		{
			float EndSpin = 0.f;
			float EndSpeed = 0.f;
			int32 EndIndex = 0;
			const FVector End = SamplePath(PathDuration, EndSpin, EndSpeed, EndIndex);
			ServerMoveTo(End, DownYaw);
			ServerSetState(ToByte(EState::Emerge), LaneTop);
		}
		break;
	}

	case EState::Emerge:
		if (Age >= TNBeachHermit::EmergeTime)
		{
			const FVector Here = Mover.Location;
			if (bStunned)
			{
				EnterDizzy(Here, DownYaw + 180.f);
				break;
			}
			ServerMoveTo(Here, DownYaw + 180.f);
			ServerSetState(ToByte(EState::Walk), LaneTop);
		}
		break;

	case EState::Walk:
	{
		float Speed = 0.f;
		const FVector Pos = WalkPosition(Age, Speed);
		SimLoc = Pos;
		if (bStunned)
		{
			EnterDizzy(Pos, DownYaw + 180.f);
			break;
		}
		if (FVector::Dist2D(Pos, LaneTop) < 5.0)
		{
			ServerMoveTo(LaneTop, DownYaw);
			ServerSetState(ToByte(EState::Settle), LaneBottom);
		}
		break;
	}

	case EState::Settle:
		if (Age >= TNBeachHermit::SettleTime)
		{
			ServerSetState(ToByte(EState::Wait), LaneBottom);
		}
		break;

	case EState::Dizzy:
	default:
		if (!bStunned && Age >= TNBeachHermit::DizzyMinTime)
		{
			const FVector Here = Mover.Location;
			if (FVector::Dist2D(Here, LaneTop) < 150.0)
			{
				ServerMoveTo(LaneTop, DownYaw);
				ServerSetState(ToByte(EState::Settle), LaneBottom);
			}
			else
			{
				ServerMoveTo(Here, DownYaw + 180.f);
				ServerSetState(ToByte(EState::Walk), LaneTop);
			}
		}
		break;
	}

	if (IsDebugDraw())
	{
		UWorld* World = GetWorld();
		const FVector Dir = LaneDir();
		const FVector SideDir(-Dir.Y, Dir.X, 0.0);
		const FVector Lift(0.0, 0.0, 60.0);
		const FVector A = LaneTop + Dir * (TNBeachHermit::TriggerAhead * SizeK);
		const FVector B = LaneBottom - Dir * TNBeachHermit::TriggerEndMargin;
		DrawDebugLine(World, LaneTop + Lift, LaneBottom + Lift, FColor::Orange, false, -1.f, 0, 10.f);
		DrawDebugLine(World, A + SideDir * LaneHalfWidth + Lift, B + SideDir * LaneHalfWidth + Lift, FColor::Yellow, false, -1.f, 0, 8.f);
		DrawDebugLine(World, A - SideDir * LaneHalfWidth + Lift, B - SideDir * LaneHalfWidth + Lift, FColor::Yellow, false, -1.f, 0, 8.f);
		DrawDebugSphere(World, LaneTop + FVector(0.0, 0.0, BallRadius), BallRadius, 12, FColor::Green, false, -1.f, 0, 4.f);
		if (bPathValid && State == EState::Roll)
		{
			const FVector PathSide(-PathDir.Y, PathDir.X, 0.0);
			for (int32 i = 1; i < RollPath.Num(); i += 2)
			{
				const FVector P0 = PathTop + PathDir * RollPath[i - 1].S + PathSide * RollPath[i - 1].Lat;
				const FVector P1 = PathTop + PathDir * RollPath[i].S + PathSide * RollPath[i].Lat;
				DrawDebugLine(World, FVector(P0.X, P0.Y, RollPath[i - 1].Z + 20.0), FVector(P1.X, P1.Y, RollPath[i].Z + 20.0), FColor::Red, false, -1.f, 0, 6.f);
			}
		}
	}
}

void ATN_BeachHermitCrab::MulticastStrike_Implementation(ATortugaCharacter* Victim, FVector_NetQuantize Where, uint8 HitCount)
{
	if (!bHasScreen)
	{
		return;
	}
	const FVector At = Where;
	if (Sound)
	{
		Sound->Play(ETNBeachCritterSfx::Strike, 1.f / FMath::Sqrt(SizeK), 1.2f);
	}
	TNBeachKit::BurstAt(Dust, At, FVector::UpVector, 10);
	UTN_BeachCameraShake::Kick(this, At, 0.5f, 500.f, 2500.f);
	const bool bStrike = HitCount >= 2;
	ShowPop(bStrike ? NSLOCTEXT("TNBeach", "HermitStrike", "¡STRIKE!") : NSLOCTEXT("TNBeach", "HermitPin", "¡BOLO!"),
		bStrike ? FColor(255, 210, 60) : FColor(255, 150, 90), At + FVector(0.0, 0.0, 280.0), bStrike ? 190.f : 150.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Visual
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachHermitCrab::PlaceBlock(const FVector& Base, float Yaw, bool bRolling)
{
	if (!Block)
	{
		return;
	}
	// Mientras rueda no bloquea: la bola derriba a quien pilla (CheckRollHits) y no se para contra nadie.
	const ECollisionEnabled::Type Wanted = bRolling ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics;
	if (Block->GetCollisionEnabled() != Wanted)
	{
		Block->SetCollisionEnabled(Wanted);
	}
	if (!bRolling)
	{
		Block->SetWorldLocationAndRotation(Base + FVector(0.0, 0.0, BallRadius * 0.9f), FRotator(0.f, Yaw, 0.f));
	}
}

void ATN_BeachHermitCrab::BuildCrab()
{
	if (!bHasScreen || BodyRoot)
	{
		return;
	}
	using TNProcMesh::FTNProcMeshBuffers;
	const int32 Pal = ((Spec.Seed % 4) + 4) % 4;
	const TNBeachCritterMeshes::FHermitLook Look = TNBeachCritterMeshes::HermitPalette(Pal);
	const FString Key = FString::Printf(TEXT("Beach.Hermit.%d."), Pal);
	const double R = TNBeachCritterMeshes::HermitRadius();

	BodyRoot = TNBeachCritterKit::AddWorldRoot(this, FTransform(FQuat::Identity, GetActorLocation() + FVector(0.0, 0.0, BallRadius), FVector(SizeK)));
	UStaticMesh* ShellM = TNBeachKit::CachedMesh(Key + TEXT("Shell"), [&Look, Pal](FTNProcMeshBuffers& M)
	{
		TNBeachCritterMeshes::BuildHermitShell(M, Look, static_cast<uint32>(Pal) * 13u + 5u);
	});
	ShellMesh = TNBeachCritterKit::AddPart(this, BodyRoot, ShellM, FVector::ZeroVector, true, TN_ART("Beach.HermitCrab.Shell"));

	// Cuerpo sólido (como el del tanque): sin él, la caracola quieta o andando se atravesaba.
	Block = NewObject<UBoxComponent>(this, NAME_None, RF_Transient | RF_DuplicateTransient);
	Block->SetupAttachment(GetRootComponent());
	Block->SetAbsolute(true, true, true);
	Block->SetBoxExtent(FVector(BallRadius * 0.85f, BallRadius * 0.85f, BallRadius * 0.9f), false);
	Block->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Block->SetCollisionObjectType(ECC_Pawn);
	Block->SetCollisionResponseToAllChannels(ECR_Ignore);
	Block->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Block->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Block->SetCanEverAffectNavigation(false);
	Block->SetGenerateOverlapEvents(false);
	Block->RegisterComponent();
	Block->SetWorldLocationAndRotation(GetActorLocation() + FVector(0.0, 0.0, BallRadius * 0.9f), FRotator::ZeroRotator);
	RegisterSolidBlock(Block);

	CrabRoot = TNBeachCritterKit::AddPivot(this, BodyRoot, TNBeachCritterMeshes::HermitAperture());
	UStaticMesh* HeadM = TNBeachKit::CachedMesh(Key + TEXT("Head"), [&Look](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildHermitHead(M, Look); });
	UStaticMesh* AntM = TNBeachKit::CachedMesh(Key + TEXT("Antennae"), [&Look](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildHermitAntennae(M, Look); });
	UStaticMesh* EyeM = TNBeachKit::CachedMesh(Key + TEXT("Eye"), [&Look](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildHermitEye(M, Look); });
	UStaticMesh* BigM = TNBeachKit::CachedMesh(Key + TEXT("BigClaw"), [&Look](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildHermitClaw(M, Look, 1.0); });
	UStaticMesh* SmallM = TNBeachKit::CachedMesh(Key + TEXT("SmallClaw"), [&Look](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildHermitClaw(M, Look, 0.62); });
	UStaticMesh* LegLM = TNBeachKit::CachedMesh(Key + TEXT("LegL"), [&Look](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildHermitLeg(M, Look, -1.0); });
	UStaticMesh* LegRM = TNBeachKit::CachedMesh(Key + TEXT("LegR"), [&Look](FTNProcMeshBuffers& M) { TNBeachCritterMeshes::BuildHermitLeg(M, Look, 1.0); });
	Head = TNBeachCritterKit::AddPart(this, CrabRoot, HeadM, FVector::ZeroVector, true, TN_ART("Beach.HermitCrab.Head"));
	Antennae = TNBeachCritterKit::AddPart(this, CrabRoot, AntM, FVector(R * 0.2, 0.0, R * 0.06), false, TN_ART("Beach.HermitCrab.Antennae"));
	for (const double Side : { -1.0, 1.0 })
	{
		Eyes.Add(TNBeachCritterKit::AddPart(this, CrabRoot, EyeM, FVector(R * 0.2, Side * R * 0.08, R * 0.1), false, TN_ART("Beach.HermitCrab.Eye")));
	}
	BigClaw = TNBeachCritterKit::AddPart(this, CrabRoot, BigM, FVector(R * 0.12, -R * 0.2, -R * 0.1), true, TN_ART("Beach.HermitCrab.BigClaw"));
	SmallClaw = TNBeachCritterKit::AddPart(this, CrabRoot, SmallM, FVector(R * 0.12, R * 0.2, -R * 0.1), false, TN_ART("Beach.HermitCrab.SmallClaw"));
	// Patas: 0 y 1 a la izquierda (delante y detrás), 2 y 3 a la derecha.
	const FVector LegPivots[4] = { FVector(R * 0.02, -R * 0.16, -R * 0.14), FVector(-R * 0.07, -R * 0.14, -R * 0.18),
		FVector(R * 0.02, R * 0.16, -R * 0.14), FVector(-R * 0.07, R * 0.14, -R * 0.18) };
	for (int32 k = 0; k < 4; ++k)
	{
		Legs.Add(TNBeachCritterKit::AddPart(this, CrabRoot, k < 2 ? LegLM : LegRM, LegPivots[k], false,
			k < 2 ? TN_ART("Beach.HermitCrab.LegLeft") : TN_ART("Beach.HermitCrab.LegRight")));
	}
	Shadow = TNBeachKit::AddShadow(this, 0.4f);

	using TNAmbientFX::EShape;
	TNAmbientFX::FEmitterDesc DustDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.87f, 0.78f, 0.58f), true, 0.5f, 40, 0.f, 260.f, 0.f, 0.6f, 1.3f, 70.f, 200.f);
	DustDesc.Buoyancy = 40.f;
	DustDesc.SpawnRadius = 90.f;
	DustDesc.Spread = 0.9f;
	TNBeachKit::InitEmitter(Dust, this, DustDesc, static_cast<uint32>(Spec.Seed) + 31u);
	TNAmbientFX::FEmitterDesc GrainDesc = TNBeachKit::MakeDesc(EShape::Flake, FLinearColor(0.93f, 0.84f, 0.62f), false, 1.f, 36, 0.f, 420.f, -980.f, 0.5f, 0.9f, 10.f, 6.f);
	GrainDesc.Spread = 1.2f;
	GrainDesc.SpawnRadius = 120.f;
	TNBeachKit::InitEmitter(Grains, this, GrainDesc, static_cast<uint32>(Spec.Seed) + 37u);
	Sound = UTN_BeachCritterSynthComponent::AttachTo(this, BodyRoot, 900.f, 7000.f);
}

void ATN_BeachHermitCrab::PoseCrab(uint8 State, float Age, float DeltaSeconds, bool bStunned, float MoveSpeed)
{
	using TNBeachHermit::EState;
	const EState St = static_cast<EState>(State);
	const double R = TNBeachCritterMeshes::HermitRadius();
	// Cuánto asoma: dentro al rodar, se mete al esconderse y sale al acabar.
	float OutTarget = 1.f;
	switch (St)
	{
	case EState::Hide: OutTarget = 1.f - static_cast<float>(TNProcMap::SmoothStep(0.0, 0.3, Age)); break;
	case EState::Roll: OutTarget = 0.f; break;
	case EState::Emerge: OutTarget = static_cast<float>(TNProcMap::SmoothStep(TNBeachHermit::PopOutAt, TNBeachHermit::PopOutAt + 0.25, Age)); break;
	default: break;
	}
	const bool bScripted = St == EState::Hide || St == EState::Roll || St == EState::Emerge;
	CrabOut = bScripted ? OutTarget : TNBeachCritterKit::Ease(CrabOut, OutTarget, DeltaSeconds, 0.12f);
	TNBeachCritterKit::SetShown(CrabRoot, CrabOut > 0.04f);
	if (CrabOut <= 0.04f || !CrabRoot)
	{
		return;
	}
	// El cangrejo va derecho aunque la caracola cabecee (se compensa la inclinación de cargarla).
	const FVector Inside(-R * 0.32 * (1.0 - CrabOut), 0.0, 0.0);
	CrabRoot->SetRelativeTransform(FTransform(FRotator(St == EState::Roll ? 0.f : TNBeachHermit::CarryTilt, 0.f, 0.f),
		TNBeachCritterMeshes::HermitAperture() + Inside, FVector(0.3f + 0.7f * CrabOut)));

	const float Clock = VisualClock;
	const bool bDizzy = bStunned || St == EState::Dizzy;
	const bool bShaking = St == EState::Emerge && Age > TNBeachHermit::PopOutAt && Age < TNBeachHermit::ShakeEnd;
	for (int32 i = 0; i < Eyes.Num(); ++i)
	{
		const float Side = i == 0 ? -1.f : 1.f;
		FRotator Rot;
		if (bDizzy)
		{
			// Ojos que dan vueltas, cada uno a su aire.
			Rot = FRotator(12.f * FMath::Sin(Clock * 6.f + Side), Clock * 420.f * Side, 18.f * FMath::Sin(Clock * 9.f));
		}
		else if (bShaking)
		{
			Rot = FRotator(10.f * FMath::Sin(Clock * 31.f), 20.f * FMath::Sin(Clock * 23.f + Side), 0.f);
		}
		else
		{
			Rot = FRotator(5.f * FMath::Sin(Clock * 1.7f + Side), 25.f * FMath::Sin(Clock * 0.9f + Side * 0.6f), 0.f);
		}
		Eyes[i]->SetRelativeRotation(Rot);
	}
	if (Antennae)
	{
		Antennae->SetRelativeRotation(FRotator(6.f * FMath::Sin(Clock * 7.3f) + (bDizzy ? 20.f * FMath::Sin(Clock * 3.f) : 0.f), 8.f * FMath::Sin(Clock * 4.1f), 0.f));
	}
	const float WalkAmp = FMath::Clamp(MoveSpeed / 150.f, 0.f, 1.f);
	if (BigClaw)
	{
		float Pitch = 4.f * FMath::Sin(Clock * 1.9f);
		float Yaw = 0.f;
		if (bDizzy)
		{
			Pitch = -22.f + 8.f * FMath::Sin(Clock * 2.f);
		}
		else if (St == EState::Wait)
		{
			// Saluda cada 3,5 s: sube la pinza y la agita.
			const float Cycle = FMath::Fmod(Clock + static_cast<float>(Spec.Seed % 7), 3.5f);
			if (Cycle < 0.9f)
			{
				Pitch = 38.f * FMath::Sin(PI * Cycle / 0.9f);
				Yaw = -12.f + 10.f * FMath::Sin(Cycle * 22.f);
			}
		}
		else
		{
			Pitch += 10.f * FMath::Sin(LegPhase) * WalkAmp;
		}
		BigClaw->SetRelativeRotation(FRotator(Pitch, Yaw, 0.f));
	}
	if (SmallClaw)
	{
		SmallClaw->SetRelativeRotation(FRotator(6.f * FMath::Sin(Clock * 2.3f + 1.f) + 10.f * FMath::Sin(LegPhase + PI) * WalkAmp, 0.f, 0.f));
	}
	for (int32 k = 0; k < Legs.Num(); ++k)
	{
		const float Phase = LegPhase + ((k % 2) ? PI : 0.f) + (k >= 2 ? PI * 0.5f : 0.f);
		const float Idle = 3.f * FMath::Sin(Clock * 2.f + k);
		const float Swing = 20.f * FMath::Sin(Phase) * WalkAmp;
		const float Lift = 10.f * FMath::Max(0.f, FMath::Cos(Phase)) * WalkAmp;
		Legs[k]->SetRelativeRotation(FRotator(Swing + Idle, 0.f, (k < 2 ? -1.f : 1.f) * Lift));
	}
}

void ATN_BeachHermitCrab::VisualTick(float DeltaSeconds)
{
	using TNBeachHermit::EState;
	VisualClock += DeltaSeconds;
	if (Mover.Serial != PrevSerial)
	{
		PrevSerial = Mover.Serial;
		PrevAge = -1.f;
		LastLandIndex = -1;
	}
	const EState State = static_cast<EState>(GetMoverState());
	const float Age = GetStateAge();
	const bool bStunned = IsHitStunned();
	if (!ResolveLane())
	{
		return;
	}
	const FVector Dir = LaneDir();
	const float DownYaw = static_cast<float>(Dir.Rotation().Yaw);
	const FVector RollAxis = FVector::CrossProduct(FVector::UpVector, Dir);
	auto Crossed = [this, Age](float T) { return PrevAge < T && Age >= T; };

	FVector Base = FVector(Mover.Location);
	FQuat Rot = FRotator(-TNBeachHermit::CarryTilt, DownYaw, 0.f).Quaternion();
	float MoveSpeed = 0.f;
	float RollSpeed = 0.f;
	float GroundZ = static_cast<float>(Base.Z);
	int32 Index = 0;
	bool bRolling = false;
	switch (State)
	{
	case EState::Hide:
	{
		// Se inclina para rodar y tiembla un poco antes de salir.
		const float Lean = static_cast<float>(TNProcMap::SmoothStep(0.1, TNBeachHermit::HideTime, Age));
		const float Rock = 7.f * FMath::Sin(Age * 45.f) * static_cast<float>(TNProcMap::SmoothStep(0.2, TNBeachHermit::HideTime, Age));
		Rot = FRotator(-TNBeachHermit::CarryTilt * (1.f - Lean), DownYaw, Rock).Quaternion();
		if (Crossed(0.f) && Sound)
		{
			Sound->Play(ETNBeachCritterSfx::ShellPop, 1.f / FMath::Sqrt(SizeK), 1.f);
		}
		break;
	}
	case EState::Roll:
		if (EnsurePath())
		{
			float Spin = 0.f;
			Base = SamplePath(Age, Spin, RollSpeed, Index);
			const FVector FromTop = Base - PathTop;
			GroundZ = ProfileGround(static_cast<float>(FVector::DotProduct(FromTop, PathDir)), static_cast<float>(FromTop.Y * PathDir.X - FromTop.X * PathDir.Y));
			// Rueda sobre el eje de lado y se bambolea un poco sobre el de la marcha (una caracola no es una bola perfecta).
			const FQuat Wobble(Dir, FMath::DegreesToRadians(8.f * FMath::Sin(Spin * 0.5f)));
			Rot = Wobble * FQuat(RollAxis, Spin) * FRotator(0.f, DownYaw, 0.f).Quaternion();
			bRolling = true;
			for (int32 i = FMath::Max(0, LastLandIndex + 1); i <= Index; ++i)
			{
				if (RollPath[i].Land > 150.f && ViewDistance < 9000.f)
				{
					const FVector Contact(Base.X, Base.Y, GroundZ);
					TNBeachKit::BurstAt(Dust, Contact, FVector::UpVector, FMath::Clamp(FMath::RoundToInt32(RollPath[i].Land / 90.f), 3, 12));
					if (Sound)
					{
						Sound->Play(ETNBeachCritterSfx::Thud, 1.f / FMath::Sqrt(SizeK), FMath::Clamp(RollPath[i].Land / 700.f, 0.3f, 1.2f));
					}
				}
			}
			LastLandIndex = FMath::Max(LastLandIndex, Index);
		}
		break;
	case EState::Emerge:
	{
		const FQuat Upright = FRotator(-TNBeachHermit::CarryTilt, DownYaw, 0.f).Quaternion();
		if (Age < TNBeachHermit::PopOutAt)
		{
			// Del giro con que se ha parado a derecho.
			const float EndSpin = (bPathValid && RollPath.Num() > 0) ? RollPath.Last().Spin : 0.f;
			const FQuat Rolled = FQuat(RollAxis, EndSpin) * FRotator(0.f, DownYaw, 0.f).Quaternion();
			Rot = FQuat::Slerp(Rolled, Upright, static_cast<float>(TNProcMap::SmoothStep(0.0, TNBeachHermit::PopOutAt, Age)));
		}
		else if (Age < TNBeachHermit::ShakeEnd)
		{
			// Se sacude la arena: vaivén rápido que se apaga.
			const float U = (Age - TNBeachHermit::PopOutAt) / (TNBeachHermit::ShakeEnd - TNBeachHermit::PopOutAt);
			const float Fade = 1.f - U * U;
			Rot = FRotator(-TNBeachHermit::CarryTilt + 4.f * FMath::Sin(Age * 37.f) * Fade, DownYaw + 7.f * FMath::Sin(Age * 29.f) * Fade,
				15.f * FMath::Sin(Age * 2.f * PI * 5.5f) * Fade).Quaternion();
			Grains.Origin = Base + FVector(0.0, 0.0, BallRadius * 1.3f);
			Grains.RateScale = ViewDistance < 6000.f ? 30.f * Fade : 0.f;
		}
		else
		{
			const float Turn = 180.f * static_cast<float>(TNProcMap::SmoothStep(TNBeachHermit::ShakeEnd, TNBeachHermit::EmergeTime, Age));
			Rot = FRotator(-TNBeachHermit::CarryTilt, DownYaw + Turn, 0.f).Quaternion();
			Grains.RateScale = 0.f;
		}
		if (Crossed(TNBeachHermit::PopOutAt) && Sound)
		{
			Sound->Play(ETNBeachCritterSfx::ShellPop, 1.3f / FMath::Sqrt(SizeK), 0.7f);
			Sound->Play(ETNBeachCritterSfx::Rattle, 1.f, 0.9f);
		}
		break;
	}
	case EState::Walk:
	{
		Base = WalkPosition(Age, MoveSpeed);
		FVector ToTop = LaneTop - Base;
		ToTop.Z = 0.0;
		const float WalkYaw = ToTop.SizeSquared() > 100.0 ? static_cast<float>(ToTop.Rotation().Yaw) : DownYaw + 180.f;
		const float Bob = MoveSpeed > 0.f ? FMath::Sin(LegPhase * 2.f) : 0.f;
		Base.Z += 4.f * SizeK * Bob;
		Rot = FRotator(-TNBeachHermit::CarryTilt + 2.f * Bob, WalkYaw, 3.f * FMath::Sin(LegPhase)).Quaternion();
		TapTimer -= DeltaSeconds;
		if (MoveSpeed > 0.f && TapTimer <= 0.f)
		{
			TapTimer = 0.32f;
			if (Sound && ViewDistance < 4000.f)
			{
				Sound->Play(ETNBeachCritterSfx::Tap, 1.1f / FMath::Sqrt(SizeK), 0.35f);
			}
		}
		break;
	}
	case EState::Settle:
	{
		const float Turn = 180.f * (1.f - static_cast<float>(TNProcMap::SmoothStep(0.0, TNBeachHermit::SettleTime, Age)));
		Rot = FRotator(-TNBeachHermit::CarryTilt, DownYaw + Turn, 0.f).Quaternion();
		MoveSpeed = Age < TNBeachHermit::SettleTime ? 60.f : 0.f;
		break;
	}
	case EState::Dizzy:
	{
		const float Yaw = static_cast<float>(FRotator::DecompressAxisFromShort(Mover.Yaw));
		Rot = FRotator(-TNBeachHermit::CarryTilt + 5.f * FMath::Sin(VisualClock * 2.3f), Yaw, 10.f * FMath::Sin(VisualClock * 1.6f * PI)).Quaternion();
		break;
	}
	case EState::Wait:
	default:
		if (bStunned)
		{
			Rot = FRotator(-TNBeachHermit::CarryTilt + 5.f * FMath::Sin(VisualClock * 2.3f), DownYaw, 10.f * FMath::Sin(VisualClock * 1.6f * PI)).Quaternion();
		}
		break;
	}
	if (!bRolling)
	{
		GroundZ = static_cast<float>(Base.Z);
	}
	ShownLoc = Base;
	ShownYaw = static_cast<float>(Rot.Rotator().Yaw);
	PrevAge = Age;
	PlaceBlock(Base, DownYaw, bRolling);

	if (Sound)
	{
		const bool bRollSound = bRolling && ViewDistance < 9000.f;
		const bool bAirborne = Base.Z > GroundZ + 15.f;
		Sound->SetRoll(bRollSound ? FMath::Clamp(RollSpeed / 1100.f, 0.15f, 1.f) * (bAirborne ? 0.35f : 1.f) : 0.f, FMath::Clamp(RollSpeed / 1500.f, 0.f, 1.f));
	}
	if (!BodyRoot || ViewDistance > GetVisualRange())
	{
		return;
	}
	BodyRoot->SetWorldLocationAndRotation(Base + FVector(0.0, 0.0, BallRadius), Rot);
	LegPhase += DeltaSeconds * MoveSpeed / FMath::Max(20.f, BallRadius * 0.35f);
	PoseCrab(static_cast<uint8>(State), Age, DeltaSeconds, bStunned, MoveSpeed);

	// Sombra en la arena (más pequeña si va por el aire) y arena al rodar.
	const float Height = static_cast<float>(Base.Z) - GroundZ;
	TNBeachKit::PlaceShadow(Shadow, FVector(Base.X, Base.Y, GroundZ), BallRadius * 0.95f * (1.f - FMath::Clamp(Height / 500.f, 0.f, 0.45f)));
	FVector View = Base;
	TNBeachKit::LocalCamera(GetWorld(), View);
	Dust.Origin = FVector(Base.X, Base.Y, GroundZ) - Dir * (BallRadius * 0.4f);
	Dust.Desc.Direction = (FVector::UpVector - Dir * 0.6).GetSafeNormal();
	Dust.RateScale = (bRolling && Height < 20.f && ViewDistance < 12000.f) ? FMath::Clamp(RollSpeed / 1200.f, 0.f, 1.f) * 14.f : 0.f;
	TNBeachKit::TickEmitterIfBusy(Dust, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(Grains, DeltaSeconds, View);
}
