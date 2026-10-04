#include "Kart/TN_KartShellLogic.h"

namespace TNKart
{
	int32 HomingTargetPlace(int32 Place)
	{
		return Place > 1 ? Place - 1 : INDEX_NONE;
	}

	FVector SteerShell(const FVector& Current, const FVector& ToTarget, float MaxTurnDeg)
	{
		const FVector2D From = FVector2D(Current).GetSafeNormal();
		const FVector2D To = FVector2D(ToTarget).GetSafeNormal();
		if (From.IsNearlyZero())
		{
			return To.IsNearlyZero() ? FVector::ForwardVector : FVector(To, 0.0);
		}
		if (To.IsNearlyZero())
		{
			return FVector(From, 0.0);
		}
		const double FromAngle = FMath::Atan2(From.Y, From.X);
		const double Delta = FMath::FindDeltaAngleRadians(FromAngle, FMath::Atan2(To.Y, To.X));
		const double Max = FMath::DegreesToRadians(FMath::Max(0.f, MaxTurnDeg));
		const double Angle = FromAngle + FMath::Clamp(Delta, -Max, Max);
		return FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
	}
}
