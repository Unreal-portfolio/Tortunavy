#include "Vehicles/TN_RallyTurretLogic.h"

namespace TNRallyTurret
{
	FAmmoSpec SpecFor(ETNRallyAmmo Ammo)
	{
		FAmmoSpec Spec;
		switch (Ammo)
		{
		case ETNRallyAmmo::Coco:
			Spec = { 6000.f, 0.3f, 3.f, 120.f, 0.25f, 0 };
			break;
		case ETNRallyAmmo::Alga:
			// Cae antes (#770): con 3500 cm/s y la gravedad normal llegaba a 50 m y se iba por encima del blanco.
			Spec = { AlgaSpeedCms, AlgaGravityScale, 4.f, 60.f, 0.5f, 2 };
			break;
		case ETNRallyAmmo::Burbuja:
			// Retroceso pequeño (#629): todas las municiones empujan al buggy, pero la burbuja sale lenta para poder cogerla.
			Spec = { 900.f, 0.f, BubbleFloatSeconds, 40.f, 0.5f, 1 };
			break;
		case ETNRallyAmmo::Mortero:
			Spec = { 2800.f, 1.f, 5.f, 700.f, 0.8f, 1 };
			break;
		case ETNRallyAmmo::Tinta:
			Spec = { 5000.f, 0.3f, 3.f, 60.f, 0.5f, 2 };
			break;
		case ETNRallyAmmo::Ancla:
			Spec = { 4500.f, 0.5f, 3.f, 200.f, 0.6f, 2 };
			break;
		case ETNRallyAmmo::Concha:
			// Conchas de las cajas «?» (#629): corren por el suelo a su velocidad (ATN_KartShell), sin gravedad de vuelo.
			Spec = { ShellSpeedCms, 0.f, 6.f, 250.f, 0.5f, 2 };
			break;
		case ETNRallyAmmo::ConchaGuiada:
			Spec = { ShellSpeedCms, 0.f, 12.f, 250.f, 0.5f, 1 };
			break;
		case ETNRallyAmmo::Erizos:
			// Cada púa (#715): rápida, cae poco y empuja poco; la cadencia es la de la ráfaga y una carga da una ráfaga.
			Spec = { ErizosSpeedCms, ErizosGravityScale, ErizosLifeSeconds, ErizosRecoilCms, ErizosSpikeInterval, 1 };
			break;
		default:
			break;
		}
		return Spec;
	}

	bool IsSpecial(ETNRallyAmmo Ammo)
	{
		return Ammo != ETNRallyAmmo::None && Ammo != ETNRallyAmmo::Coco;
	}

	bool IsGroundShell(ETNRallyAmmo Ammo)
	{
		return Ammo == ETNRallyAmmo::Concha || Ammo == ETNRallyAmmo::ConchaGuiada;
	}

	bool IsOverheated(const FHeat& State)
	{
		return State.OverheatLeft > 0.f;
	}

	bool CanFireCoco(const FHeat& State)
	{
		return !IsOverheated(State);
	}

	FHeat AfterCocoShot(const FHeat& State)
	{
		FHeat Out = State;
		Out.SinceShot = 0.f;
		if (IsOverheated(Out))
		{
			return Out;
		}
		// Redondeo: seis sumas de 1/6 en float pueden quedarse en 0,99999.
		Out.Heat = FMath::Min(1.f, Out.Heat + 1.f / ShotsToOverheat + KINDA_SMALL_NUMBER);
		if (Out.Heat >= 1.f)
		{
			Out.Heat = 1.f;
			Out.OverheatLeft = OverheatSeconds;
		}
		return Out;
	}

	FHeat Cool(const FHeat& State, float Dt)
	{
		FHeat Out = State;
		const float Step = FMath::Max(Dt, 0.f);
		Out.SinceShot += Step;
		if (IsOverheated(Out))
		{
			Out.OverheatLeft = FMath::Max(0.f, Out.OverheatLeft - Step);
			if (Out.OverheatLeft <= 0.f)
			{
				Out.Heat = 0.f;
			}
			return Out;
		}
		if (Out.SinceShot >= CoolDelaySeconds)
		{
			Out.Heat = FMath::Max(0.f, Out.Heat - CoolPerSecond * Step);
		}
		return Out;
	}

	FSpecial Give(ETNRallyAmmo Ammo, int32 Charges)
	{
		FSpecial Out;
		if (IsSpecial(Ammo) && Charges > 0)
		{
			Out.Ammo = Ammo;
			Out.Charges = Charges;
		}
		return Out;
	}

	bool CanFireSpecial(const FSpecial& State)
	{
		return IsSpecial(State.Ammo) && State.Charges > 0;
	}

	FSpecial AfterSpecialShot(const FSpecial& State)
	{
		if (!CanFireSpecial(State))
		{
			return FSpecial();
		}
		return Give(State.Ammo, State.Charges - 1);
	}

	FRotator ClampAim(const FRotator& RelativeAim)
	{
		return FRotator(
			FMath::Clamp(static_cast<float>(RelativeAim.Pitch), MinPitchDeg, MaxPitchDeg),
			FRotator::NormalizeAxis(RelativeAim.Yaw),
			0.f);
	}

	bool IsAimFinite(float Yaw, float Pitch)
	{
		return FMath::IsFinite(Yaw) && FMath::IsFinite(Pitch);
	}

	FVector AimWorldDirection(const FRotator& BuggyRotation, const FRotator& RelativeAim)
	{
		const FQuat World = BuggyRotation.Quaternion() * ClampAim(RelativeAim).Quaternion();
		return World.GetForwardVector();
	}

	FRotator RelativeAimFromWorld(const FRotator& BuggyRotation, const FVector& WorldDir)
	{
		const FVector Local = BuggyRotation.Quaternion().UnrotateVector(WorldDir.GetSafeNormal());
		return ClampAim(Local.Rotation());
	}

	FVector ResolveClientFireDirection(const FVector& ServerDir, const FVector& ClientDir, float MaxErrorDeg)
	{
		const FVector Server = ServerDir.GetSafeNormal();
		if (ClientDir.ContainsNaN() || ClientDir.IsNearlyZero())
		{
			return Server;
		}
		const FVector Client = ClientDir.GetSafeNormal();
		const double CosMax = FMath::Cos(FMath::DegreesToRadians(static_cast<double>(FMath::Max(MaxErrorDeg, 0.f))));
		return FVector::DotProduct(Server, Client) >= CosMax ? Client : Server;
	}

	FVector MuzzleWorldLocation(const FVector& PivotWorld, const FRotator& BuggyRotation, const FRotator& RelativeAim, float ForwardCm,
		float SideCm)
	{
		const FQuat World = BuggyRotation.Quaternion() * ClampAim(RelativeAim).Quaternion();
		return PivotWorld + World.RotateVector(FVector(ForwardCm, SideCm, 0.f));
	}

	bool CadenceOk(double Now, double LastShot, float Interval, float Tolerance)
	{
		return Now - LastShot >= static_cast<double>(Interval) * Tolerance;
	}

	FVector RecoilVelocity(const FVector& AimWorldDir, float RecoilCms)
	{
		const FVector Flat = FVector(AimWorldDir.X, AimWorldDir.Y, 0.f).GetSafeNormal();
		return -Flat * FMath::Max(RecoilCms, 0.f);
	}

	FRecoilLift RecoilLift(const FVector& LocalAimDir, float RecoilCms, float HalfLengthCm)
	{
		FRecoilLift Out;
		const FVector Flat = FVector(LocalAimDir.X, LocalAimDir.Y, 0.f).GetSafeNormal();
		const float Forwardness = static_cast<float>(Flat.X);
		if (FMath::Abs(Forwardness) < KINDA_SMALL_NUMBER || RecoilCms <= 0.f)
		{
			return Out;
		}
		Out.LiftCms = FMath::Min(RecoilCms * RecoilLiftRatio * FMath::Abs(Forwardness), MaxRecoilLiftCms);
		Out.LocalPoint = FVector(FMath::Sign(Forwardness) * FMath::Max(HalfLengthCm, 0.f) * 0.9f, 0.f, 0.f);
		return Out;
	}

	TArray<ETNRallyAmmo> AvailableAmmo(const FSpecial& Special)
	{
		TArray<ETNRallyAmmo> Out;
		Out.Add(ETNRallyAmmo::Coco);
		if (CanFireSpecial(Special))
		{
			Out.Add(Special.Ammo);
		}
		return Out;
	}

	ETNRallyAmmo ResolveSelection(ETNRallyAmmo Selected, const FSpecial& Special)
	{
		if (!IsSpecial(Selected) || !CanFireSpecial(Special))
		{
			return ETNRallyAmmo::Coco;
		}
		return Special.Ammo;
	}

	ETNRallyAmmo CycleAmmo(ETNRallyAmmo Selected, const FSpecial& Special, int32 Direction)
	{
		const TArray<ETNRallyAmmo> Options = AvailableAmmo(Special);
		const ETNRallyAmmo Current = ResolveSelection(Selected, Special);
		const int32 Index = FMath::Max(0, Options.IndexOfByKey(Current));
		const int32 Num = Options.Num();
		// Módulo positivo: un paso hacia atrás desde el primero da el último.
		const int32 Next = ((Index + Direction) % Num + Num) % Num;
		return Options[Next];
	}

	float LobRangeCm(float PitchDeg, float HeightCm, float SpeedCms, float GravityCms2)
	{
		if (GravityCms2 <= 0.f || SpeedCms <= 0.f)
		{
			return 0.f;
		}
		const float Rad = FMath::DegreesToRadians(PitchDeg);
		const float Vz = SpeedCms * FMath::Sin(Rad);
		const float Discriminant = Vz * Vz + 2.f * GravityCms2 * HeightCm;
		if (Discriminant < 0.f)
		{
			return 0.f;
		}
		const float FlightSeconds = (Vz + FMath::Sqrt(Discriminant)) / GravityCms2;
		return FMath::Max(0.f, SpeedCms * FMath::Cos(Rad) * FlightSeconds);
	}

	float LobPitchDeg(float RangeCm, float HeightCm, float SpeedCms, float GravityCms2)
	{
		if (GravityCms2 <= 0.f || SpeedCms <= 0.f || RangeCm <= 0.f)
		{
			return 0.f;
		}
		constexpr float StepDeg = 0.5f;
		for (float Pitch = MinPitchDeg; Pitch <= MaxPitchDeg; Pitch += StepDeg)
		{
			if (LobRangeCm(Pitch, HeightCm, SpeedCms, GravityCms2) >= RangeCm)
			{
				return Pitch;
			}
		}
		return MaxPitchDeg;
	}

	int32 PickAutoAimTarget(const FVector& Origin, const FVector& Dir, TConstArrayView<FVector> Candidates,
		float RangeCm, float HalfAngleDeg)
	{
		const FVector FlatDir = FVector(Dir.X, Dir.Y, 0.f).GetSafeNormal();
		if (FlatDir.IsNearlyZero())
		{
			return INDEX_NONE;
		}
		const float MinCos = FMath::Cos(FMath::DegreesToRadians(HalfAngleDeg));
		int32 Best = INDEX_NONE;
		double BestDist = RangeCm;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const FVector To(Candidates[Index].X - Origin.X, Candidates[Index].Y - Origin.Y, 0.f);
			const double Dist = To.Size();
			if (Dist <= KINDA_SMALL_NUMBER || Dist > BestDist)
			{
				continue;
			}
			if (FVector::DotProduct(To / Dist, FlatDir) < MinCos)
			{
				continue;
			}
			Best = Index;
			BestDist = Dist;
		}
		return Best;
	}

	FImpactOutcome ResolveImpact(bool bShielded)
	{
		FImpactOutcome Out;
		Out.bApplies = !bShielded;
		Out.bShieldConsumed = bShielded;
		return Out;
	}

	float PuddleGripMultiplier(bool bInPuddle)
	{
		return bInPuddle ? AlgaGripMultiplier : 1.f;
	}

	float PuddleSpeedCapCms(bool bInPuddle)
	{
		return BuggyTopSpeedCms * (bInPuddle ? AlgaSpeedMultiplier : 1.f);
	}
}

namespace TNRallyTurret
{
	float PuddleEntrySpinDegPerSecond(float SpeedCms, bool bClockwise)
	{
		const float Speed = FMath::Abs(SpeedCms);
		if (Speed < AlgaSpinMinSpeedCms)
		{
			return 0.f;
		}
		const float Alpha = FMath::Clamp((Speed - AlgaSpinMinSpeedCms) / (AlgaSpinFullSpeedCms - AlgaSpinMinSpeedCms), 0.f, 1.f);
		return AlgaSpinYawDegPerSecond * Alpha * (bClockwise ? 1.f : -1.f);
	}

	bool PuddleAffects(bool bIsDropper, float PuddleAgeSeconds)
	{
		return !bIsDropper || PuddleAgeSeconds >= AlgaDropperGraceSeconds;
	}

	bool IsPuddleGround(const FVector& Normal)
	{
		return !Normal.ContainsNaN() && Normal.GetSafeNormal().Z >= PuddleMinGroundNormalZ;
	}

	bool FitGroundPlane(TConstArrayView<FVector> Points, FVector& OutCenter, FVector& OutNormal)
	{
		if (Points.Num() == 0)
		{
			return false;
		}
		OutCenter = Points[0];
		OutNormal = FVector::UpVector;
		if (Points.Num() < 3)
		{
			return true;
		}
		FVector Sum = FVector::ZeroVector;
		for (const FVector& Point : Points)
		{
			Sum += Point;
		}
		OutCenter = Sum / Points.Num();
		// Abanico desde el primero (el centro): cada par de puntos del borde da un triángulo; su normal, siempre hacia arriba.
		FVector NormalSum = FVector::ZeroVector;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FVector& A = Points[Index];
			const FVector& B = Points[Index + 1 < Points.Num() ? Index + 1 : 1];
			FVector Normal = FVector::CrossProduct(A - Points[0], B - Points[0]);
			if (Normal.Z < 0.0)
			{
				Normal = -Normal;
			}
			NormalSum += Normal.GetSafeNormal();
		}
		const FVector Normal = NormalSum.GetSafeNormal();
		OutNormal = Normal.IsNearlyZero() ? FVector::UpVector : Normal;
		return true;
	}

	FQuat PuddleRotation(const FVector& GroundNormal, const FVector& Forward)
	{
		const FVector Up = GroundNormal.IsNearlyZero() || GroundNormal.ContainsNaN() ? FVector::UpVector : GroundNormal.GetSafeNormal();
		FVector X = Forward - FVector::DotProduct(Forward, Up) * Up;
		if (X.IsNearlyZero())
		{
			// Forward paralelo a la normal: cualquier eje del plano vale.
			X = FVector::CrossProduct(Up, FMath::Abs(Up.X) < 0.9 ? FVector::ForwardVector : FVector::RightVector);
		}
		return FRotationMatrix::MakeFromZX(Up, X.GetSafeNormal()).ToQuat();
	}
}

namespace TNRallyTurret
{
	bool IsBurstAmmo(ETNRallyAmmo Ammo)
	{
		return Ammo == ETNRallyAmmo::Erizos;
	}

	bool IsBurstActive(const FBurst& Burst)
	{
		return Burst.SpikesLeft > 0;
	}

	FBurst HoldBurst(const FBurst& Burst, double Now, float HoldSeconds, int32 Spikes)
	{
		FBurst Out = Burst;
		if (!IsBurstActive(Out))
		{
			Out.SpikesLeft = FMath::Max(0, Spikes);
			Out.NextSpikeAt = Now;
		}
		Out.HoldUntil = FMath::Max(Out.HoldUntil, Now + FMath::Max(HoldSeconds, 0.f));
		return Out;
	}

	bool BurstSpikeDue(const FBurst& Burst, double Now)
	{
		return IsBurstActive(Burst) && Now >= Burst.NextSpikeAt && Now <= Burst.HoldUntil;
	}

	FBurst AfterBurstSpike(const FBurst& Burst, double Now, float Interval)
	{
		FBurst Out = Burst;
		Out.SpikesLeft = FMath::Max(0, Out.SpikesLeft - 1);
		// A la hora prevista, aunque el fotograma llegue un poco tarde (la media no se retrasa); tras una pausa, desde ahora.
		const double Next = Out.NextSpikeAt + Interval;
		Out.NextSpikeAt = Next > Now ? Next : Now + Interval;
		return Out;
	}

	float BurstHoldSeconds(bool bHumanTrigger)
	{
		return bHumanTrigger ? ErizosHoldSeconds : ErizosBurstSeconds + 0.5f;
	}
}

namespace TNRallyTurret
{
	FVector SpikePushDir(const FVector& Forward, const FVector& PushDir)
	{
		const FVector FlatForward = FVector(Forward.X, Forward.Y, 0.f).GetSafeNormal();
		const FVector Right(-FlatForward.Y, FlatForward.X, 0.f);
		FVector Lateral(PushDir.X, PushDir.Y, 0.f);
		Lateral -= FVector::DotProduct(Lateral, FlatForward) * FlatForward;
		if (Lateral.IsNearlyZero(0.05f))
		{
			return FVector::DotProduct(PushDir, Right) >= 0.0 ? Right : -Right;
		}
		return Lateral.GetSafeNormal();
	}
}
