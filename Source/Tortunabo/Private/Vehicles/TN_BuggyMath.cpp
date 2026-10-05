#include "Vehicles/TN_BuggyMath.h"

DEFINE_LOG_CATEGORY(LogTNBuggy);

namespace TNBuggy
{
	float SlipAngleDeg(const FVector& Forward, const FVector& Velocity)
	{
		const FVector Flat(Velocity.X, Velocity.Y, 0.f);
		if (Flat.Size() < MinSlipSpeed)
		{
			return 0.f;
		}
		const FVector F = FVector(Forward.X, Forward.Y, 0.f).GetSafeNormal();
		const FVector V = Flat.GetSafeNormal();
		return FMath::RadiansToDegrees(FMath::Atan2(FVector::CrossProduct(F, V).Z, FVector::DotProduct(F, V)));
	}

	float AssistSteer(float Input, float SlipDeg, float Assist, float MaxAngleDeg, float StartDeg)
	{
		const float AbsSlip = FMath::Abs(SlipDeg);
		const float Start = FMath::Max(0.f, StartDeg);
		if (AbsSlip > MaxAssistedSlipDeg || MaxAngleDeg <= Start)
		{
			return FMath::Clamp(Input, -1.f, 1.f);
		}
		const float Fraction = FMath::Clamp((AbsSlip - Start) / (MaxAngleDeg - Start), 0.f, 1.f);
		const float Correction = Assist * Fraction * FMath::Sign(SlipDeg);
		return FMath::Clamp(Input + Correction, -1.f, 1.f);
	}

	float EffectiveSteerDeg(float Input, float SpeedCms, float SlipDeg, float BaseAngleDeg, float Assist,
		float AssistMaxAngleDeg, float AssistStartDeg)
	{
		return MaxSteerAngleDeg(SpeedCms, BaseAngleDeg) * AssistSteer(Input, SlipDeg, Assist, AssistMaxAngleDeg, AssistStartDeg);
	}

	bool IsRise(float StepCm, bool bBothInContact, const FBumpTuning& Tuning)
	{
		return bBothInContact && StepCm >= Tuning.MinStepCm;
	}

	float KickSpeed(float StepCm, float ForwardSpeed, float WheelRadius, const FBumpTuning& Tuning)
	{
		if (WheelRadius <= 0.f || StepCm <= 0.f)
		{
			return 0.f;
		}
		const float Severity = FMath::Min(StepCm / WheelRadius, 1.f);
		return FMath::Clamp(Tuning.Scale * FMath::Abs(ForwardSpeed) * Severity, 0.f, Tuning.MaxKick);
	}

	FBumpStep BumpStep(const FWheelTrack& Track, float StepCm, float ForwardSpeed, float WheelRadius,
		bool bBothInContact, const FBumpTuning& Tuning)
	{
		const bool bRise = IsRise(StepCm, bBothInContact, Tuning);
		FBumpStep Out;
		Out.Kick = bRise ? 0.f : Track.PendingKick;
		Out.Track.bPrevRise = bRise;
		Out.Track.PendingKick = (bRise && !Track.bPrevRise) ? KickSpeed(StepCm, ForwardSpeed, WheelRadius, Tuning) : 0.f;
		return Out;
	}

	bool IsFlipped(float UpZ)
	{
		return UpZ < FlippedUpZ;
	}

	float AdvanceFlipped(float FlippedSeconds, float UpZ, float Dt)
	{
		return IsFlipped(UpZ) ? FlippedSeconds + FMath::Max(Dt, 0.f) : 0.f;
	}

	ESelfRight DecideSelfRight(float FlippedSeconds, bool bRequested, float ManualDelay, float AutoDelay)
	{
		if (FlippedSeconds <= 0.f)
		{
			return ESelfRight::None;
		}
		if (bRequested && FlippedSeconds >= ManualDelay)
		{
			return ESelfRight::Manual;
		}
		return FlippedSeconds >= AutoDelay ? ESelfRight::Auto : ESelfRight::None;
	}

	FTransform SelfRightTransform(const FTransform& Current, float LiftCm)
	{
		// La guiñada sale del morro proyectado en horizontal: boca abajo, el rotador puede traerla girada 180 grados.
		const FVector Forward = Current.GetRotation().GetForwardVector();
		const FVector FlatForward(Forward.X, Forward.Y, 0.f);
		const double Yaw = FlatForward.IsNearlyZero(0.01f) ? Current.Rotator().Yaw : FlatForward.Rotation().Yaw;
		const FRotator Upright(0.f, Yaw, 0.f);
		return FTransform(Upright, Current.GetLocation() + FVector::UpVector * LiftCm, Current.GetScale3D());
	}

	bool AdvanceHold(FHold& Hold, bool bPressed, float Dt, float HoldSeconds)
	{
		if (!bPressed)
		{
			Hold = FHold();
			return false;
		}
		Hold.Held += FMath::Max(Dt, 0.f);
		if (!Hold.bFired && Hold.Held >= HoldSeconds)
		{
			Hold.bFired = true;
			return true;
		}
		return false;
	}

	namespace
	{
		/** Ángulo con signo (rad) de Axis alrededor del que Current se aparta de la vertical, con la zona libre Free (rad). */
		double ExcessTilt(const FVector& Axis, const FVector& Current, double Free)
		{
			const FVector Vertical = (FVector::UpVector - Axis * (FVector::UpVector | Axis)).GetSafeNormal();
			if (Vertical.IsNearlyZero())
			{
				return 0.0;
			}
			const double Angle = FMath::Atan2(Axis | (Vertical ^ Current), Vertical | Current);
			return FMath::Sign(Angle) * FMath::Max(0.0, FMath::Abs(Angle) - Free);
		}
	}

	float GroundRollHelp(float SpeedCms, const FAntiRollTuning& Tuning)
	{
		const float Full = FMath::Max(0.f, Tuning.GroundRollFullSpeedCms);
		const float Zero = Tuning.GroundRollZeroSpeedCms;
		if (Zero <= Full)
		{
			return 1.f;
		}
		return 1.f - FMath::Clamp((FMath::Abs(SpeedCms) - Full) / (Zero - Full), 0.f, 1.f);
	}

	FVector AntiRollAccel(const FVector& Forward, const FVector& Up, const FVector& AngularVelocityRad, bool bAirborne,
		const FAntiRollTuning& Tuning, float SpeedCms)
	{
		if (IsFlipped(static_cast<float>(Up.Z)) || Tuning.Stiffness <= 0.f)
		{
			return FVector::ZeroVector;
		}
		const FVector F = Forward.GetSafeNormal();
		const FVector R = (Up ^ F).GetSafeNormal();
		const double FreeRoll = bAirborne ? 0.0 : FMath::DegreesToRadians(Tuning.GroundFreeRollDeg);
		const double FreePitch = bAirborne ? 0.0 : FMath::DegreesToRadians(Tuning.GroundFreePitchDeg);
		const double Roll = ExcessTilt(F, Up, FreeRoll);
		const double Pitch = ExcessTilt(R, Up, FreePitch);
		// El amortiguador solo actúa mientras hay exceso (o en el aire): en el suelo no frena el balanceo normal.
		const double RollRate = (Roll != 0.0 || bAirborne) ? (AngularVelocityRad | F) : 0.0;
		const double PitchRate = (Pitch != 0.0 || bAirborne) ? (AngularVelocityRad | R) : 0.0;
		// En el suelo, el alabeo se deja libre a mucha velocidad: girar fuerte puede volcar (#606).
		const double RollHelp = bAirborne ? 1.0 : GroundRollHelp(SpeedCms, Tuning);
		const FVector Accel = F * (RollHelp * (-Tuning.Stiffness * Roll - Tuning.Damping * RollRate))
			+ R * (-Tuning.Stiffness * Pitch - Tuning.Damping * PitchRate);
		return Accel.GetClampedToMaxSize(FMath::Max(0.f, Tuning.MaxAccel));
	}

	FLinearColor TeamColor(int32 Index)
	{
		static const FLinearColor Palette[] = {
			FLinearColor(0.90f, 0.20f, 0.15f), // rojo
			FLinearColor(0.15f, 0.45f, 0.95f), // azul
			FLinearColor(0.20f, 0.80f, 0.25f), // verde
			FLinearColor(0.98f, 0.80f, 0.10f), // amarillo
			FLinearColor(0.70f, 0.25f, 0.90f), // morado
			FLinearColor(1.00f, 0.50f, 0.05f), // naranja
			FLinearColor(0.10f, 0.85f, 0.85f), // turquesa
			FLinearColor(0.95f, 0.40f, 0.70f), // rosa
		};
		if (Index < 0)
		{
			return FLinearColor::White;
		}
		return Palette[Index % UE_ARRAY_COUNT(Palette)];
	}

	float SpeedCapDecel(float Speed, float Cap, float Gain)
	{
		const float Excess = FMath::Abs(Speed) - FMath::Max(Cap, 0.f);
		return Excess > 0.f ? Excess * FMath::Max(Gain, 0.f) : 0.f;
	}

	float SpeedCapCms(const FSpeedCapInput& In)
	{
		float Cap = 0.f;
		// Nunca 0 (sería «sin tope»): como mucho, 1 cm/s.
		const auto Limit = [&Cap](float Value)
		{
			Value = FMath::Max(Value, 1.f);
			Cap = Cap > 0.f ? FMath::Min(Cap, Value) : Value;
		};
		if (In.PuddleCapCms > 0.f)
		{
			Limit(In.PuddleCapCms);
		}
		if (In.bWading)
		{
			// Con daño, el agua frena también (#719, #720).
			Limit(In.TopSpeedCms * FMath::Clamp(In.WaterSpeedMultiplier, 0.f, 1.f) * FMath::Clamp(In.DamageScale, 0.f, 1.f));
		}
		if (In.DamageScale < 1.f)
		{
			const float Top = FMath::Lerp(In.TopSpeedCms, FMath::Max(In.BoostTopSpeedCms, In.TopSpeedCms), FMath::Clamp(In.BoostStrength01, 0.f, 1.f));
			Limit(Top * FMath::Max(In.DamageScale, 0.f));
		}
		return Cap;
	}

	float DamageStatScale(float Health01, float MinScale)
	{
		return FMath::Lerp(FMath::Clamp(MinScale, 0.f, 1.f), 1.f, FMath::Clamp(Health01, 0.f, 1.f));
	}

	float SteerWobble(float TimeLeft, float Duration, float Amplitude, float Frequency)
	{
		if (TimeLeft <= 0.f || Duration <= 0.f)
		{
			return 0.f;
		}
		const float Elapsed = Duration - FMath::Min(TimeLeft, Duration);
		const float Fade = FMath::Clamp(TimeLeft / Duration, 0.f, 1.f);
		return Amplitude * Fade * FMath::Sin(2.f * UE_PI * Frequency * Elapsed + UE_HALF_PI);
	}
}

namespace TNBuggy
{
	float EvalLinearKeys(TConstArrayView<FCurveKey> Keys, float X)
	{
		if (Keys.Num() == 0)
		{
			return 0.f;
		}
		if (X <= Keys[0].X)
		{
			return Keys[0].Y;
		}
		for (int32 Index = 1; Index < Keys.Num(); ++Index)
		{
			const FCurveKey& A = Keys[Index - 1];
			const FCurveKey& B = Keys[Index];
			if (X <= B.X)
			{
				const float Span = B.X - A.X;
				return Span > UE_KINDA_SMALL_NUMBER ? FMath::Lerp(A.Y, B.Y, (X - A.X) / Span) : B.Y;
			}
		}
		return Keys.Last().Y;
	}

	TConstArrayView<FCurveKey> SteerCurveKeys()
	{
		// El mismo ángulo a cualquier velocidad (#606). Dos claves: Chaos muestrea la curva hasta la última X.
		static const FCurveKey Keys[] = { { 0.f, 1.f }, { 4000.f, 1.f } };
		return Keys;
	}

	float MaxSteerAngleDeg(float SpeedCms, float BaseAngleDeg)
	{
		return BaseAngleDeg * EvalLinearKeys(SteerCurveKeys(), FMath::Abs(SpeedCms));
	}

	float KinematicTurnRadiusCm(float WheelbaseCmIn, float InnerSteerDeg, float AngleRatio)
	{
		const float MeanDeg = 0.5f * FMath::Abs(InnerSteerDeg) * (1.f + FMath::Clamp(AngleRatio, 0.f, 1.f));
		const float TanMean = FMath::Tan(FMath::DegreesToRadians(FMath::Min(MeanDeg, 89.f)));
		return TanMean > UE_KINDA_SMALL_NUMBER ? FMath::Max(0.f, WheelbaseCmIn) / TanMean : 0.f;
	}

	float TurnRadiusFromYawRate(float SpeedCms, float YawRateRad)
	{
		const float Yaw = FMath::Abs(YawRateRad);
		return Yaw > UE_KINDA_SMALL_NUMBER ? FMath::Abs(SpeedCms) / Yaw : 0.f;
	}

	float SafeSteerFraction(float SpeedCms, float MaxSteerDeg, float WheelbaseCmIn, float MaxLateralAccelCms2)
	{
		const float Speed = FMath::Abs(SpeedCms);
		if (MaxSteerDeg <= 0.f || Speed < 1.f || MaxLateralAccelCms2 <= 0.f)
		{
			return 1.f;
		}
		// Radio mínimo R = v² / a; ángulo de esa curva para la batalla: atan(L / R).
		const float SafeDeg = FMath::RadiansToDegrees(FMath::Atan(FMath::Max(0.f, WheelbaseCmIn) * MaxLateralAccelCms2 / FMath::Square(Speed)));
		return FMath::Clamp(SafeDeg / MaxSteerDeg, 0.f, 1.f);
	}

	FVector GridHoldVelocity(const FVector& Velocity, const FVector& Up, const FVector& DriftCm, const FGridHoldTuning& Tuning)
	{
		const FVector Normal = Up.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const FVector AlongUp = Normal * (Velocity | Normal);
		const FVector PlaneDrift = DriftCm - Normal * (DriftCm | Normal);
		const FVector Correction = (-FMath::Max(0.f, Tuning.PositionGain) * PlaneDrift).GetClampedToMaxSize(FMath::Max(0.f, Tuning.MaxCorrectionCms));
		return AlongUp + Correction;
	}

	FVector GridHoldAngularVelocity(const FVector& AngularVelocity, const FVector& Up)
	{
		const FVector Normal = Up.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		return AngularVelocity - Normal * (AngularVelocity | Normal);
	}

	TConstArrayView<FCurveKey> LegacyTorqueCurveKeys()
	{
		static const FCurveKey Keys[] = {
			{ 0.f, 0.5f }, { 0.1f, 0.8f }, { 0.3f, 1.f }, { 0.55f, 1.f }, { 0.8f, 0.9f }, { 0.9f, 0.4f }, { 1.f, 0.f } };
		return Keys;
	}

	TArray<FCurveKey> TorqueCurveKeys(float MaxTorque)
	{
		// Desde el 80 % de MaxRPM, el par absoluto de la curva antigua: con más MaxTorque, la fracción baja en proporción.
		const float Scale = MaxTorque > UE_KINDA_SMALL_NUMBER ? LegacyMaxTorque / MaxTorque : 1.f;
		const TConstArrayView<FCurveKey> Legacy = LegacyTorqueCurveKeys();
		TArray<FCurveKey> Keys = { { 0.f, 0.9f }, { 0.1f, 1.f }, { 0.55f, 1.f } };
		for (const FCurveKey& Key : Legacy)
		{
			if (Key.X >= 0.8f)
			{
				Keys.Add({ Key.X, FMath::Min(1.f, Key.Y * Scale) });
			}
		}
		return Keys;
	}

	float StabilityYawAccel(float SlipDeg, float YawRateRad, float SpeedCms, bool bHandbrake, bool bAirborne,
		const FStabilityTuning& Tuning)
	{
		const float AbsSlip = FMath::Abs(SlipDeg);
		if (bHandbrake || bAirborne || FMath::Abs(SpeedCms) < Tuning.MinSpeedCms || AbsSlip <= Tuning.StartSlipDeg
			|| AbsSlip > MaxAssistedSlipDeg)
		{
			return 0.f;
		}
		// Deriva positiva = la velocidad va a la derecha del morro: girar a la derecha (guiñada positiva) la reduce.
		const float Excess = FMath::DegreesToRadians(AbsSlip - Tuning.StartSlipDeg) * FMath::Sign(SlipDeg);
		float Accel = Tuning.Stiffness * Excess;
		// Solo se frena la guiñada que agranda la deriva (la que va en contra de su signo); la que la corrige se deja.
		if (YawRateRad * SlipDeg < 0.f)
		{
			Accel -= Tuning.Damping * YawRateRad;
		}
		return FMath::Clamp(Accel, -Tuning.MaxAccel, Tuning.MaxAccel);
	}

	float BoostRechargeRate(const FBoostInput& In, const FBoostTuning& Tuning)
	{
		if (In.bEngineLocked || FMath::Abs(In.SpeedCms) < Tuning.MinRechargeSpeedCms)
		{
			return 0.f;
		}
		if (In.bAirborne)
		{
			return Tuning.AirRechargePerSecond;
		}
		const bool bDrifting = In.bHandbrake && FMath::Abs(In.SlipDeg) > Tuning.MinDriftSlipDeg
			&& FMath::Abs(In.SlipDeg) <= MaxAssistedSlipDeg;
		return bDrifting ? Tuning.DriftRechargePerSecond : 0.f;
	}

	FBoostStep AdvanceBoost(float Charge01, const FBoostInput& In, float Dt, const FBoostTuning& Tuning)
	{
		const float SafeDt = FMath::Max(Dt, 0.f);
		const float Charge = FMath::Clamp(Charge01, 0.f, 1.f);
		FBoostStep Out;
		Out.bActive = In.bWantBoost && !In.bEngineLocked && Charge > 0.f;
		const float Rate = Out.bActive ? -Tuning.DrainPerSecond : BoostRechargeRate(In, Tuning);
		const float Next = FMath::Clamp(Charge + Rate * SafeDt, 0.f, 1.f);
		// Un resto de redondeo no cuenta como carga: si no, el turbo empujaría un paso de más.
		Out.Charge01 = Next < UE_KINDA_SMALL_NUMBER ? 0.f : Next;
		return Out;
	}

	float BoostPushAccel(float ForwardSpeedCms, float BoostTopSpeedCms, float PushAccel, float FadeBandCms)
	{
		if (ForwardSpeedCms < 0.f || ForwardSpeedCms >= BoostTopSpeedCms || PushAccel <= 0.f)
		{
			return 0.f;
		}
		const float Fade = FadeBandCms > 0.f ? FMath::Clamp((BoostTopSpeedCms - ForwardSpeedCms) / FadeBandCms, 0.f, 1.f) : 1.f;
		return PushAccel * Fade;
	}

	float AdvanceBoostRamp(float Progress01, bool bBoosting, bool bEngineLocked, float Dt, const FBoostRampTuning& Tuning)
	{
		if (bEngineLocked)
		{
			return 0.f;
		}
		const float Step = FMath::Max(0.f, Dt);
		const float Delta = bBoosting
			? (Tuning.UpSeconds > 0.f ? Step / Tuning.UpSeconds : 1.f)
			: -(Tuning.DownSeconds > 0.f ? Step / Tuning.DownSeconds : 1.f);
		return FMath::Clamp(Progress01 + Delta, 0.f, 1.f);
	}

	float BoostRampStrength(float Progress01, float Exponent)
	{
		const float Progress = FMath::Clamp(Progress01, 0.f, 1.f);
		return Progress <= 0.f ? 0.f : FMath::Pow(Progress, FMath::Max(0.1f, Exponent));
	}

	float BoostTorqueScale(float Strength01, float TorqueMultiplier)
	{
		return FMath::Lerp(1.f, TorqueMultiplier, FMath::Clamp(Strength01, 0.f, 1.f));
	}
}

namespace TNBuggy
{
	namespace
	{
		/** Rampa de 0 a 1 entre Start y Full (0 si Full <= Start y X < Full). */
		float Ramp01(float X, float Start, float Full)
		{
			return Full > Start ? FMath::Clamp((X - Start) / (Full - Start), 0.f, 1.f) : (X >= Full ? 1.f : 0.f);
		}

		/** Objetivo sin suavizar: desplazamiento hacia la curva, acercamiento al frenar y alabeo en el derrape. */
		FDriverCameraState CameraTarget(const FDriverCameraInput& In, float LongAccel, const FDriverCameraTuning& Tuning)
		{
			FDriverCameraState Target;
			const float SpeedAlpha = Ramp01(FMath::Abs(In.ForwardSpeedCms), 0.f, Tuning.LeadFullSpeedCms);
			Target.LateralCm = FMath::Clamp(In.YawRateDegPerSec * Tuning.LeadCmPerDegPerSec, -Tuning.MaxLeadCm, Tuning.MaxLeadCm)
				* SpeedAlpha * (In.bAirborne ? 0.f : 1.f);
			// Frenar es decelerar yendo hacia delante (marcha atrás, la cámara no se mueve).
			const float Decel = In.ForwardSpeedCms > 200.f ? -LongAccel : 0.f;
			const float Brake = Ramp01(Decel, Tuning.BrakeStartDecel, Tuning.BrakeFullDecel);
			Target.ArmDeltaCm = -Tuning.BrakeArmPullCm * Brake;
			Target.HeightDeltaCm = -Tuning.BrakeDropCm * Brake;
			const float Drift = Ramp01(FMath::Abs(In.SlipDeg), Tuning.DriftRollStartDeg, Tuning.DriftRollFullDeg);
			Target.RollDeg = FMath::Sign(In.SlipDeg) * Tuning.MaxRollDeg * Drift * (In.bAirborne ? 0.f : 1.f);
			return Target;
		}

		/** Olvido, aterrizaje, sacudida externa y mínimo con el turbo. Actualiza el registro del vuelo en Out. */
		void AdvanceTrauma(FDriverCameraState& Out, const FDriverCameraInput& In, float Dt, const FDriverCameraTuning& Tuning)
		{
			float Trauma = FMath::Max(0.f, Out.Trauma - Tuning.TraumaDecayPerSecond * Dt);
			if (In.bAirborne)
			{
				Out.AirSeconds += Dt;
				Out.FallSpeedCms = FMath::Max(Out.FallSpeedCms, -In.VerticalSpeedCms);
			}
			else if (Out.AirSeconds > 0.f)
			{
				Trauma += LandingTrauma(Out.FallSpeedCms, Out.AirSeconds, Tuning);
				Out.AirSeconds = 0.f;
				Out.FallSpeedCms = 0.f;
			}
			Trauma += FMath::Max(0.f, In.AddedTrauma);
			if (In.bBoosting)
			{
				Trauma = FMath::Max(Trauma, Tuning.BoostTrauma * FMath::Clamp(In.BoostStrength01, 0.f, 1.f));
			}
			Out.Trauma = FMath::Clamp(Trauma, 0.f, 1.f);
		}

		/** Suma de dos senos de frecuencias sin múltiplo común, en [-1, 1]. */
		float Wiggle(float Time, float RateA, float RateB, float Phase)
		{
			return (FMath::Sin(Time * RateA + Phase) + 0.5f * FMath::Sin(Time * RateB + 2.f * Phase)) / 1.5f;
		}
	}

	const FDriverCameraTuning& DefaultDriverCamera()
	{
		static const FDriverCameraTuning Tuning;
		return Tuning;
	}

	float LandingTrauma(float FallSpeedCms, float AirSeconds, const FDriverCameraTuning& Tuning)
	{
		if (AirSeconds < Tuning.LandingMinAirSeconds)
		{
			return 0.f;
		}
		return 0.8f * Ramp01(FallSpeedCms, Tuning.LandingMinFallCms, Tuning.LandingFullFallCms);
	}

	FDriverCameraState AdvanceDriverCamera(const FDriverCameraState& State, const FDriverCameraInput& In,
		const FDriverCameraTuning& Tuning)
	{
		FDriverCameraState Out = State;
		const float Dt = In.Dt;
		if (Dt <= 0.f)
		{
			return Out;
		}
		const float RawAccel = Out.bHasPrevSpeed ? (In.ForwardSpeedCms - Out.PrevForwardSpeedCms) / Dt : 0.f;
		Out.LongAccel = FMath::FInterpTo(Out.LongAccel, RawAccel, Dt, Tuning.AccelInterpSpeed);
		Out.PrevForwardSpeedCms = In.ForwardSpeedCms;
		Out.bHasPrevSpeed = true;

		const FDriverCameraState Target = CameraTarget(In, Out.LongAccel, Tuning);
		Out.LateralCm = FMath::FInterpTo(Out.LateralCm, Target.LateralCm, Dt, Tuning.PoseInterpSpeed);
		Out.ArmDeltaCm = FMath::FInterpTo(Out.ArmDeltaCm, Target.ArmDeltaCm, Dt, Tuning.PoseInterpSpeed);
		Out.HeightDeltaCm = FMath::FInterpTo(Out.HeightDeltaCm, Target.HeightDeltaCm, Dt, Tuning.PoseInterpSpeed);
		Out.RollDeg = FMath::FInterpTo(Out.RollDeg, Target.RollDeg, Dt, Tuning.PoseInterpSpeed);
		const float BoostFovTarget = In.bBoosting ? Tuning.BoostFovDeg * FMath::Clamp(In.BoostStrength01, 0.f, 1.f) : 0.f;
		Out.BoostFovDeg = FMath::FInterpTo(Out.BoostFovDeg, BoostFovTarget, Dt, Tuning.BoostFovInterpSpeed);
		AdvanceTrauma(Out, In, Dt, Tuning);
		return Out;
	}

	FVector ShakeOffset(float Trauma, float TimeSeconds, float MaxShakeCm)
	{
		const float Amplitude = FMath::Square(FMath::Clamp(Trauma, 0.f, 1.f)) * MaxShakeCm;
		return Amplitude * FVector(Wiggle(TimeSeconds, 41.3f, 67.1f, 0.f), Wiggle(TimeSeconds, 47.9f, 73.3f, 1.3f),
			Wiggle(TimeSeconds, 53.7f, 79.9f, 2.6f));
	}

	FVector ShakeRotation(float Trauma, float TimeSeconds, float MaxShakeDeg)
	{
		const float Amplitude = FMath::Square(FMath::Clamp(Trauma, 0.f, 1.f)) * MaxShakeDeg;
		return Amplitude * FVector(Wiggle(TimeSeconds, 37.1f, 61.7f, 0.7f), Wiggle(TimeSeconds, 43.3f, 71.9f, 2.1f), 0.f);
	}

	FTransform BoostFlameTransform(const FVector& ExhaustLocal, const FVector& DirLocal, float LengthCm, float DiameterCm,
		float Flicker)
	{
		const FVector Dir = DirLocal.GetSafeNormal(UE_SMALL_NUMBER, FVector(-1.f, 0.f, 0.f));
		const float Length = FMath::Max(1.f, LengthCm) * FMath::Clamp(Flicker, 0.1f, 2.f);
		const float Diameter = FMath::Max(1.f, DiameterCm);
		// El eje +Z del cono (la punta) pasa a Dir; el centro queda a media llama del escape.
		const FQuat Rotation = FRotationMatrix::MakeFromZ(Dir).ToQuat();
		return FTransform(Rotation, ExhaustLocal + Dir * (0.5f * Length), FVector(Diameter, Diameter, Length) / BasicConeSizeCm);
	}

	float BoostFlameFlicker(float TimeSeconds, float Amount)
	{
		return 1.f + FMath::Clamp(Amount, 0.f, 0.9f) * Wiggle(TimeSeconds, 31.7f, 57.3f, 0.4f);
	}
}
