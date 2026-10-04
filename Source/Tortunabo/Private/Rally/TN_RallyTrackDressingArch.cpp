// Pórticos de las puertas del Rally sobre el suelo de sus dos patas (#665): en peralte o en una ladera, el pórtico de una
// pieza se inclina hasta que las dos patas pisan; con más desnivel del que admite, se baja para que ninguna flote.

#include "Rally/TN_RallyTrackDressing.h"

namespace TNRallyDressing
{
	FArchFit FitArchToFeet(double LeftGroundZ, double RightGroundZ, double FootOffsetCm, double MaxRollDeg)
	{
		FArchFit Fit;
		const double Offset = FMath::Max(1.0, FootOffsetCm);
		const double MaxRoll = FMath::Clamp(MaxRollDeg, 0.0, 89.0);
		Fit.RollDeg = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(RightGroundZ - LeftGroundZ, 2.0 * Offset)), -MaxRoll, MaxRoll);
		// Girado, cada pata sube o baja Rise respecto al centro y se acerca a él (Offset·cos): el suelo bajo ella se toma de la
		// recta entre los dos suelos medidos. El centro más alto que deja las dos patas en su suelo o por debajo; con el giro
		// sin recortar, las dos quedan justo en su suelo y el centro, a media altura.
		const double Radians = FMath::DegreesToRadians(Fit.RollDeg);
		const double Rise = Offset * FMath::Sin(Radians);
		const double Mid = 0.5 * (LeftGroundZ + RightGroundZ);
		const double HalfDrop = 0.5 * (RightGroundZ - LeftGroundZ) * FMath::Cos(Radians);
		Fit.BaseZ = FMath::Min(Mid + HalfDrop - Rise, Mid - HalfDrop + Rise);
		return Fit;
	}

	FTransform RollAboutBase(const FTransform& Upright, const FVector& Base, const FVector& Forward, double RollDeg)
	{
		const FVector Axis = Forward.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : Forward.GetSafeNormal2D();
		const FQuat Roll(Axis, FMath::DegreesToRadians(RollDeg));
		FTransform Out = Upright;
		Out.SetLocation(Base + Roll.RotateVector(Upright.GetLocation() - Base));
		Out.SetRotation(Roll * Upright.GetRotation());
		return Out;
	}
}
