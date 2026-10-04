// Lógica pura de la animación de las ocupantes del buggy (TNRiderAnim). Ver TN_BuggyRiderAnimComponent.h; tests en
// Tortunabo.Rally.RiderAnim.*.

#include "Vehicles/TN_BuggyRiderAnimComponent.h"

namespace TNRiderAnim
{
	namespace
	{
		constexpr float MaxFrequencyHz = 30.f;
		constexpr float MinDampingRatio = 0.05f;

		void ClampToLimit(FSpring& State, float Limit)
		{
			if (Limit <= 0.f || FMath::Abs(State.Value) <= Limit)
			{
				return;
			}
			const float Side = FMath::Sign(State.Value);
			State.Value = Side * Limit;
			if (State.Velocity * Side > 0.f)
			{
				State.Velocity = 0.f;
			}
		}
	}

	FSpring StepSpring(const FSpring& State, float Target, const FTNRiderSpringTuning& Tuning, float DeltaSeconds)
	{
		const float Limit = Tuning.Limit;
		const float Goal = Limit > 0.f ? FMath::Clamp(Target, -Limit, Limit) : Target;
		if (!FMath::IsFinite(Goal))
		{
			return FSpring();
		}
		if (Tuning.FrequencyHz <= KINDA_SMALL_NUMBER)
		{
			return FSpring{ Goal, 0.f };
		}
		FSpring Out = State;
		if (!FMath::IsFinite(Out.Value) || !FMath::IsFinite(Out.Velocity))
		{
			Out = FSpring();
		}
		const float Dt = FMath::Clamp(FMath::IsFinite(DeltaSeconds) ? DeltaSeconds : 0.f, 0.f, MaxStepSeconds);
		const float Omega = UE_TWO_PI * FMath::Min(Tuning.FrequencyHz, MaxFrequencyHz);
		const float Zeta = FMath::Max(Tuning.DampingRatio, MinDampingRatio);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Dt / MaxSubstepSeconds));
		const float H = Dt / static_cast<float>(Steps);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			// Muelle explícito y amortiguador implícito: el amortiguamiento nunca invierte la velocidad.
			Out.Velocity = (Out.Velocity + Omega * Omega * (Goal - Out.Value) * H) / (1.f + 2.f * Zeta * Omega * H);
			Out.Value += Out.Velocity * H;
			ClampToLimit(Out, Limit);
		}
		return Out;
	}

	FSpring KickSpring(const FSpring& State, float VelocityKick)
	{
		FSpring Out = State;
		if (FMath::IsFinite(VelocityKick))
		{
			Out.Velocity += VelocityKick;
		}
		return Out;
	}

	float DeadZone(float Value, float Threshold)
	{
		const float Excess = FMath::Abs(Value) - FMath::Max(Threshold, 0.f);
		return Excess > 0.f ? FMath::Sign(Value) * Excess : 0.f;
	}

	FTargets ReactionTargets(const FVector& AccelMesh, float Grip01, const FTNRiderReactionTuning& Tuning)
	{
		const float Soft = 1.f - FMath::Clamp(Grip01, 0.f, 1.f) * FMath::Clamp(Tuning.GripRigidity, 0.f, 1.f);
		FTargets Out;
		// Frenar (aceleración hacia atrás, -Y) lleva la cabeza hacia delante por inercia; acelerar, hacia atrás.
		Out.HeadPitchDeg = DeadZone(static_cast<float>(-AccelMesh.Y), Tuning.HeadAccelThreshold) * Tuning.HeadDegPerAccel * Soft;
		// La aceleración centrípeta apunta al interior de la curva: el cuerpo se va hacia el lado contrario.
		Out.LeanRollDeg = DeadZone(static_cast<float>(-AccelMesh.X), Tuning.LeanAccelThreshold) * Tuning.LeanDegPerAccel * Soft;
		return Out;
	}

	float SteerFromYawRate(float YawRateDegPerSec, float ForwardSpeedCms, float WheelbaseCm, float MaxSteerDeg, float MinSpeedCms)
	{
		if (FMath::Abs(ForwardSpeedCms) < FMath::Max(MinSpeedCms, 1.f) || MaxSteerDeg <= 0.f || WheelbaseCm <= 0.f)
		{
			return 0.f;
		}
		const float AngleRad = FMath::Atan(FMath::DegreesToRadians(YawRateDegPerSec) * WheelbaseCm / ForwardSpeedCms);
		const float Steer = FMath::RadiansToDegrees(AngleRad) / MaxSteerDeg;
		return FMath::IsFinite(Steer) ? FMath::Clamp(Steer, -1.f, 1.f) : 0.f;
	}

	bool IsShotSignal(float PrevHeat01, float Heat01, int32 PrevCharges, int32 Charges, bool bSameAmmo, float MinHeatStep)
	{
		if (Heat01 - PrevHeat01 >= FMath::Max(MinHeatStep, KINDA_SMALL_NUMBER))
		{
			return true;
		}
		return Charges < PrevCharges && (bSameAmmo || Charges == 0);
	}

	FGunnerAim GunnerAimTargets(float AimYawDeg, float AimPitchDeg, bool bKnocked, const FTNGunnerAimTuning& Tuning)
	{
		FGunnerAim Out;
		if (bKnocked || !FMath::IsFinite(AimYawDeg) || !FMath::IsFinite(AimPitchDeg))
		{
			return Out;
		}
		const float MaxYaw = FMath::Clamp(Tuning.MaxYawDeg, 0.f, 170.f);
		Out.YawDeg = FMath::Clamp(FRotator::NormalizeAxis(AimYawDeg), -MaxYaw, MaxYaw);
		Out.PitchDeg = FMath::Clamp(AimPitchDeg * FMath::Clamp(Tuning.PitchFollow, 0.f, 1.f), -FMath::Max(Tuning.MaxPitchDownDeg, 0.f),
			FMath::Max(Tuning.MaxPitchUpDeg, 0.f));
		return Out;
	}

	bool IsAmmoSwapSignal(bool bHasPrev, uint8 PrevAmmo, uint8 Ammo)
	{
		return bHasPrev && PrevAmmo != Ammo;
	}

	FTNRiderSpringTuning DefaultRecoilSpring()
	{
		// ~18° hacia atrás a los ~70 ms de la sacudida de DefaultRecoilKickDegPerSec y de vuelta en ~0,4 s.
		return FTNRiderSpringTuning(2.5f, 0.7f, 30.f);
	}

	FTNRiderSpringTuning DefaultSwapSpring()
	{
		// Gesto de ~0,5 s que llega casi a 1 con DefaultSwapKickPerSec.
		return FTNRiderSpringTuning(1.5f, 0.75f, 1.2f);
	}
}
