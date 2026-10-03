#pragma once

#include "CoreMinimal.h"

/**
 * Lógica pura de la inclinación visual de la tortuga con la pendiente (#586), sin mundo ni componentes: la usa
 * UTN_SlopeTiltComponent en producción y la prueban los tests Tortunabo.Player.SlopeTilt.*.
 *
 * Convención de la inclinación (FRotator con Yaw = 0, en los ejes de la cápsula, que solo gira en yaw):
 * - Pitch > 0: el morro sube (la cuesta sube por delante).
 * - Roll < 0: el lado derecho sube (la cuesta sube por la derecha); Roll > 0, sube el izquierdo.
 * Con FRotator(Pitch, 0, Roll), el eje Z de la tortuga queda exactamente sobre la normal del suelo (hasta el tope).
 */
namespace TNSlopeTilt
{
	/** Estado de la tortuga que decide si se inclina. */
	struct FTiltGate
	{
		bool bOnGround = false;
		bool bInShell = false;
		bool bCarried = false;
		bool bKnockedDown = false;
		bool bDead = false;
		bool bRagdoll = false;
	};

	/**
	 * Estados en los que otro sistema manda en la malla (la bola la pone sobre el caparazón, el derribo y la muerte la
	 * giran o la sueltan a la física): la inclinación se quita al momento, sin interpolar, para no torcer su pose.
	 */
	inline bool IsTakenOver(const FTiltGate& Gate)
	{
		return Gate.bInShell || Gate.bCarried || Gate.bKnockedDown || Gate.bDead || Gate.bRagdoll;
	}

	/** Solo en el suelo y de pie o de tripa: en el aire, en la bola, llevada, derribada, muerta o en ragdoll, no. */
	inline bool ShouldTilt(const FTiltGate& Gate)
	{
		return Gate.bOnGround && !IsTakenOver(Gate);
	}

	/**
	 * Cabeceo y alabeo (grados) que dejan la tortuga paralela a un suelo de normal FloorNormal (mundo) mirando a YawDeg.
	 * La inclinación total se limita a MaxTiltDeg conservando la dirección de la cuesta; por debajo de MinTiltDeg se
	 * considera llano (0). Una normal nula o que no apunta hacia arriba da 0.
	 */
	inline FRotator ComputeTilt(const FVector& FloorNormal, float YawDeg, float MaxTiltDeg, float MinTiltDeg = 0.f)
	{
		const FVector WorldNormal = FloorNormal.GetSafeNormal();
		if (WorldNormal.Z <= UE_KINDA_SMALL_NUMBER)
		{
			return FRotator::ZeroRotator;
		}

		const FVector Local = FRotator(0.f, YawDeg, 0.f).UnrotateVector(WorldNormal);
		const double TiltDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Local.Z, -1.0, 1.0)));
		if (TiltDeg < FMath::Max(0.f, MinTiltDeg))
		{
			return FRotator::ZeroRotator;
		}

		FVector Clamped = Local;
		const double MaxDeg = FMath::Max(0.f, MaxTiltDeg);
		if (TiltDeg > MaxDeg)
		{
			const FVector2D Downhill = FVector2D(Local.X, Local.Y).GetSafeNormal();
			const double MaxRad = FMath::DegreesToRadians(MaxDeg);
			Clamped = FVector(Downhill.X * FMath::Sin(MaxRad), Downhill.Y * FMath::Sin(MaxRad), FMath::Cos(MaxRad));
		}

		const double Pitch = FMath::RadiansToDegrees(FMath::Atan2(-Clamped.X, Clamped.Z));
		const double Roll = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Clamped.Y, -1.0, 1.0)));
		return FRotator(Pitch, 0.0, Roll);
	}

	/** Un paso de la interpolación exponencial hacia Target (Speed en 1/s); al quedar a menos de 0,05° se pega. */
	inline FRotator StepTilt(const FRotator& Current, const FRotator& Target, float DeltaTime, float Speed)
	{
		constexpr float SnapDeg = 0.05f;
		FRotator Next(
			FMath::FInterpTo(Current.Pitch, Target.Pitch, DeltaTime, Speed),
			0.f,
			FMath::FInterpTo(Current.Roll, Target.Roll, DeltaTime, Speed));
		if (FMath::Abs(Next.Pitch - Target.Pitch) < SnapDeg && FMath::Abs(Next.Roll - Target.Roll) < SnapDeg)
		{
			Next = FRotator(Target.Pitch, 0.f, Target.Roll);
		}
		return Next;
	}

	/** Giro relativo de la malla: la inclinación (en los ejes de la cápsula) sobre la base que han dejado los demás sistemas. */
	inline FQuat ComposeTilt(const FQuat& BaseRelative, const FRotator& Tilt)
	{
		return FRotator(Tilt.Pitch, 0.f, Tilt.Roll).Quaternion() * BaseRelative;
	}
}
