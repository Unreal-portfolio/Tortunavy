#include "Vehicles/TN_RallyCombatLogic.h"

namespace TNRallyCombat
{
	FImpactSpec ImpactFor(ETNRallyAmmo Ammo)
	{
		FImpactSpec Spec;
		switch (Ammo)
		{
		case ETNRallyAmmo::Coco:
			Spec = { 350.f, 220.f, 10.f };
			break;
		case ETNRallyAmmo::Alga:
			Spec = { 0.f, 0.f, 5.f };
			break;
		case ETNRallyAmmo::Mortero:
			// El empujón del mortero es el vertical de su explosión (ATN_Buggy::ApplyMortarBlast).
			Spec = { 0.f, 0.f, 25.f };
			break;
		case ETNRallyAmmo::Tinta:
			Spec = { 120.f, 0.f, 5.f };
			break;
		case ETNRallyAmmo::Ancla:
			Spec = { 150.f, 0.f, 8.f };
			break;
		case ETNRallyAmmo::Erizos:
			// Púa (#715): el empujón es lateral y lo da ATN_Buggy::ApplySpikeHit; aquí solo el daño.
			Spec = { 0.f, 0.f, 2.f };
			break;
		default:
			break;
		}
		return Spec;
	}

	EHitZone ClassifyHitZone(const FVector& LocalPoint, float HalfLengthCm, float HalfWidthCm)
	{
		const float NormX = static_cast<float>(LocalPoint.X) / FMath::Max(HalfLengthCm, 1.f);
		const float NormY = static_cast<float>(LocalPoint.Y) / FMath::Max(HalfWidthCm, 1.f);
		if (FMath::Abs(NormY) > FMath::Abs(NormX))
		{
			return EHitZone::Side;
		}
		return NormX >= 0.f ? EHitZone::Front : EHitZone::Rear;
	}

	namespace
	{
		FVector FlatShotDir(const FVector& LocalPoint, const FVector& LocalShotDir)
		{
			FVector Flat(LocalShotDir.X, LocalShotDir.Y, 0.f);
			if (Flat.IsNearlyZero())
			{
				Flat = FVector(-LocalPoint.X, -LocalPoint.Y, 0.f);
			}
			return Flat.IsNearlyZero() ? FVector::BackwardVector : Flat.GetSafeNormal();
		}

		float SignOrPlus(double Value)
		{
			return Value < 0.0 ? -1.f : 1.f;
		}
	}

	FImpactPush ComputeImpactPush(ETNRallyAmmo Ammo, const FVector& LocalPoint, const FVector& LocalShotDir,
		float HalfLengthCm, float HalfWidthCm)
	{
		const FImpactSpec Spec = ImpactFor(Ammo);
		const FVector Flat = FlatShotDir(LocalPoint, LocalShotDir);
		const float HalfLength = FMath::Max(HalfLengthCm, 1.f);
		const float HalfWidth = FMath::Max(HalfWidthCm, 1.f);

		FImpactPush Out;
		Out.Zone = ClassifyHitZone(LocalPoint, HalfLength, HalfWidth);
		Out.LocalImpulseCms = Flat * Spec.PushCms;
		switch (Out.Zone)
		{
		case EHitZone::Front:
			Out.LocalImpulseCms.Z += Spec.NoseLiftCms;
			Out.LocalPoint = FVector(HalfLength * 0.9f, LocalPoint.Y, 0.f);
			break;
		case EHitZone::Rear:
			Out.LocalPoint = FVector(-HalfLength * 0.9f, LocalPoint.Y, 0.f);
			break;
		default:
		{
			// Lejos del centro a lo largo: el empujón lateral siempre gira el buggy (en el centro no giraría).
			const float LeverX = SignOrPlus(LocalPoint.X) * FMath::Max(FMath::Abs(static_cast<float>(LocalPoint.X)), HalfLength * SideLeverFraction);
			Out.LocalPoint = FVector(LeverX, SignOrPlus(LocalPoint.Y) * HalfWidth, 0.f);
			break;
		}
		}
		return Out;
	}

	float ApplyDamage(float Health, float Damage, float MaxHealth)
	{
		return FMath::Clamp(Health - FMath::Max(Damage, 0.f), 0.f, FMath::Max(MaxHealth, 0.f));
	}

	bool IsSmoking(float Health, float MaxHealth)
	{
		return Health > 0.f && Health <= MaxHealth * SmokeHealthFraction;
	}

	float SmokePuffsPerSecond(float Health, float MaxHealth)
	{
		if (!IsSmoking(Health, MaxHealth))
		{
			return 0.f;
		}
		const float Damage01 = 1.f - FMath::Clamp(Health / (MaxHealth * SmokeHealthFraction), 0.f, 1.f);
		return FMath::Lerp(SmokeMinPuffsPerSecond, SmokeMaxPuffsPerSecond, Damage01);
	}

	bool IsDestroyed(float Health)
	{
		return Health <= 0.f;
	}

	float CrashDamage(float DeltaVCms, float ImpactNormalZ)
	{
		if (!FMath::IsFinite(DeltaVCms) || FMath::Abs(ImpactNormalZ) > CrashMaxNormalZ || DeltaVCms < CrashMinDeltaVCms)
		{
			return 0.f;
		}
		const float Alpha = FMath::Clamp((DeltaVCms - CrashMinDeltaVCms) / (CrashMaxDeltaVCms - CrashMinDeltaVCms), 0.f, 1.f);
		// Un choque justo en el umbral ya quita algo (una quinta parte del máximo).
		return FMath::Lerp(CrashMaxDamage * 0.2f, CrashMaxDamage, Alpha);
	}

	float RearBumpCmsFor(float LocalHitX, float HalfLengthCm, float ClosingSpeedCms)
	{
		const bool bInRear = LocalHitX < -0.5f * FMath::Max(HalfLengthCm, 1.f);
		return bInRear && ClosingSpeedCms >= RearBumpMinClosingCms ? RearBumpCms : 0.f;
	}

	FVector TurtleLaunchVelocity(const FVector& BuggyVelocity, const FVector& BuggyToTurtle)
	{
		const FVector FlatVelocity(BuggyVelocity.X, BuggyVelocity.Y, 0.f);
		const float Speed = static_cast<float>(FlatVelocity.Size());
		if (Speed < RunOverMinSpeedCms)
		{
			return FVector::ZeroVector;
		}
		const FVector Ahead = FlatVelocity / Speed;
		FVector Away(BuggyToTurtle.X, BuggyToTurtle.Y, 0.f);
		Away = Away.GetSafeNormal();
		// Hacia fuera solo la parte lateral: la tortuga sale despedida a un lado del morro, no se queda delante.
		Away -= FVector::DotProduct(Away, Ahead) * Ahead;
		const FVector Dir = (Ahead * 0.75f + Away.GetSafeNormal() * 0.25f).GetSafeNormal();
		const float Horizontal = FMath::Min(Speed * 0.8f, RunOverMaxHorizontalCms);
		const float Up = FMath::Min(RunOverBaseUpCms + 0.25f * Speed, RunOverMaxUpCms);
		return Dir * Horizontal + FVector(0.f, 0.f, Up);
	}

	FVector AnchorDragAccel(const FVector& Velocity)
	{
		const FVector Flat(Velocity.X, Velocity.Y, 0.f);
		const float Speed = static_cast<float>(Flat.Size());
		if (Speed < 1.f)
		{
			return FVector::ZeroVector;
		}
		const float Decel = AnchorMaxDecelCms2 * FMath::Min(1.f, Speed / AnchorFullSpeedCms);
		return -Flat / Speed * Decel;
	}

	FVector DragAnchor(const FVector& Anchor, const FVector& Hook, float RopeLengthCm)
	{
		const FVector Delta = Anchor - Hook;
		const float Length = FMath::Max(RopeLengthCm, 0.f);
		if (Delta.SizeSquared() <= FMath::Square(Length))
		{
			return Anchor;
		}
		return Hook + Delta.GetSafeNormal() * Length;
	}
}
