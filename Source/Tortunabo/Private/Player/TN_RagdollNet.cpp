#include "Player/TN_RagdollNet.h"

#include "Engine/NetSerialization.h"

bool FTNRagdollRootPose::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	uint8 Flags = (bActive ? 1 : 0) | (bSettled ? 2 : 0);
	Ar.SerializeBits(&Flags, 2);
	bActive = (Flags & 1) != 0;
	bSettled = (Flags & 2) != 0;

	// Como FVector_NetQuantize10 (0,1 cm) y la velocidad como FVector_NetQuantize (1 cm/s); el giro, 16 bits por eje.
	bOutSuccess = SerializePackedVector<10, 24>(Location, Ar);
	Rotation.SerializeCompressedShort(Ar);
	if (bSettled)
	{
		// Asentado: quieto, sin velocidad que mandar.
		Velocity = FVector::ZeroVector;
	}
	else
	{
		bOutSuccess &= SerializePackedVector<1, 20>(Velocity, Ar);
	}
	return true;
}

namespace TNRagdollNet
{
	FVector QuantizeLocation(const FVector& Location)
	{
		return FVector(
			FMath::RoundToDouble(Location.X * 10.0) / 10.0,
			FMath::RoundToDouble(Location.Y * 10.0) / 10.0,
			FMath::RoundToDouble(Location.Z * 10.0) / 10.0);
	}

	FVector ResolveStandLocation(bool bHasAuthority, const FVector& ServerStandLocation, const FVector& LocalStandLocation)
	{
		if (bHasAuthority || ServerStandLocation.IsZero())
		{
			return LocalStandLocation;
		}
		return ServerStandLocation;
	}

	FVector ExtrapolateTarget(const FTNRagdollRootPose& Pose, float AgeSeconds, float OneWayLatencySeconds, const FTNRagdollNetTuning& Tuning)
	{
		if (Pose.bSettled)
		{
			return Pose.Location;
		}
		const float Ahead = FMath::Clamp(AgeSeconds + OneWayLatencySeconds, 0.f, Tuning.MaxExtrapolationSeconds);
		return Pose.Location + Pose.Velocity * Ahead;
	}

	bool ShouldSendPose(float SecondsSinceLastSend, const FTNRagdollRootPose& LastSent, const FTNRagdollRootPose& Current, const FTNRagdollNetTuning& Tuning)
	{
		if (LastSent.bActive != Current.bActive || LastSent.bSettled != Current.bSettled)
		{
			return true;
		}
		if (Current.bSettled)
		{
			// Quieto y ya mandado: nada nuevo que contar.
			return false;
		}
		const float Interval = 1.f / FMath::Max(Tuning.SendRateHz, 1.f);
		if (SecondsSinceLastSend < Interval)
		{
			return false;
		}
		return FVector::DistSquared(LastSent.Location, Current.Location) >= FMath::Square(Tuning.SendMinMove)
			|| !LastSent.Rotation.Equals(Current.Rotation, 1.f);
	}

	float AdvanceSettleTimer(float SettleTimer, float LinearSpeed, float AngularSpeedDeg, float DeltaSeconds, const FTNRagdollNetTuning& Tuning)
	{
		if (LinearSpeed > Tuning.SettleLinearSpeed || AngularSpeedDeg > Tuning.SettleAngularSpeedDeg)
		{
			return 0.f;
		}
		return SettleTimer + FMath::Max(DeltaSeconds, 0.f);
	}

	bool IsSettled(float SettleTimer, const FTNRagdollNetTuning& Tuning)
	{
		return SettleTimer >= Tuning.SettleSeconds;
	}

	FCorrection ComputeCorrection(const FVector& LocalLocation, const FVector& LocalVelocity, const FVector& TargetLocation,
		const FVector& TargetVelocity, bool bTargetSettled, float DeltaSeconds, const FTNRagdollNetTuning& Tuning)
	{
		FCorrection Out;
		const FVector Error = TargetLocation - LocalLocation;
		const double ErrorSize = Error.Size();

		if (ErrorSize > Tuning.SnapDistance)
		{
			Out.Mode = ECorrectionMode::Snap;
			Out.Translation = Error;
			Out.DeltaVelocity = TargetVelocity - LocalVelocity;
			return Out;
		}

		if (bTargetSettled)
		{
			// Asentado en el servidor: el mismo punto exacto y a dormir. Una vez dormido no se vuelve a mover, así que no
			// tiembla aunque la postura local apoye el cuerpo raíz a otra altura.
			if (ErrorSize > Tuning.SettledTolerance || !LocalVelocity.IsNearlyZero(Tuning.SettleLinearSpeed))
			{
				Out.Mode = ECorrectionMode::SnapAndSleep;
				Out.Translation = Error;
				Out.DeltaVelocity = -LocalVelocity;
			}
			return Out;
		}

		// En marcha: la altura del cuerpo raíz depende de cómo esté tumbado, así que lo vertical solo se corrige si se va
		// mucho (una caída distinta). Lo horizontal, fuera de la zona muerta.
		FVector PositionError(Error.X, Error.Y, 0.0);
		if (PositionError.SizeSquared() < FMath::Square(Tuning.DeadZone))
		{
			PositionError = FVector::ZeroVector;
		}
		if (FMath::Abs(Error.Z) > Tuning.VerticalDeadZone)
		{
			PositionError.Z = Error.Z - FMath::Sign(Error.Z) * Tuning.VerticalDeadZone;
		}

		const FVector DesiredVelocity = TargetVelocity + (PositionError * Tuning.PositionGain).GetClampedToMaxSize(Tuning.MaxCorrectionSpeed);
		FVector VelocityError = DesiredVelocity - LocalVelocity;
		if (PositionError.IsZero())
		{
			// Dentro de la tolerancia: solo la velocidad horizontal, para no pelear con la gravedad y el suelo locales.
			VelocityError.Z = 0.0;
		}
		if (VelocityError.IsNearlyZero(1.0))
		{
			return Out;
		}
		const float Alpha = 1.f - FMath::Exp(-Tuning.VelocityBlendRate * FMath::Max(DeltaSeconds, 0.f));
		Out.Mode = ECorrectionMode::Nudge;
		Out.DeltaVelocity = VelocityError * Alpha;
		return Out;
	}

	FVector ComputePushVelocityChange(const FVector& PusherLocation, const FVector& PusherVelocity, const FVector& BodyLocation,
		const FVector& BodyVelocity, float DeltaSeconds, const FTNRagdollNetTuning& Tuning)
	{
		const FVector ToBody = BodyLocation - PusherLocation;
		if (FMath::Abs(ToBody.Z) > Tuning.PushMaxHeight)
		{
			return FVector::ZeroVector;
		}
		const FVector ToBody2D(ToBody.X, ToBody.Y, 0.0);
		const double Distance = ToBody2D.Size();
		if (Distance > Tuning.PushRadius)
		{
			return FVector::ZeroVector;
		}
		const FVector PusherVel2D(PusherVelocity.X, PusherVelocity.Y, 0.0);
		const double PusherSpeed = PusherVel2D.Size();
		if (PusherSpeed < Tuning.PushMinSpeed)
		{
			return FVector::ZeroVector;
		}
		const FVector PusherDir = PusherVel2D / PusherSpeed;
		// Encima del cuerpo: lo empuja hacia donde camina.
		const FVector Dir = Distance > 1.0 ? ToBody2D / Distance : PusherDir;
		if (FVector::DotProduct(PusherDir, Dir) <= 0.0)
		{
			return FVector::ZeroVector;
		}
		const double Cap = PusherSpeed * Tuning.PushSpeedFactor;
		const double Along = FVector::DotProduct(FVector(BodyVelocity.X, BodyVelocity.Y, 0.0), Dir);
		if (Along >= Cap)
		{
			return FVector::ZeroVector;
		}
		const double Change = FMath::Min(static_cast<double>(Tuning.PushAcceleration) * FMath::Max(DeltaSeconds, 0.f), Cap - Along);
		return Dir * Change;
	}
}
