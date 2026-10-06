#include "Voice/TN_VoiceRouting.h"

namespace TNVoiceRouting
{
	bool IsInRange(double DistanceSquared, double OuterRadiusCm)
	{
		return DistanceSquared <= FMath::Square(OuterRadiusCm);
	}

	TArray<bool> SelectListeners(TConstArrayView<double> DistancesSquared, double OuterRadiusCm, int32 MaxListeners)
	{
		TArray<bool> Selected;
		Selected.Reserve(DistancesSquared.Num());
		TArray<int32> InRange;
		for (int32 Index = 0; Index < DistancesSquared.Num(); ++Index)
		{
			const bool bInRange = IsInRange(DistancesSquared[Index], OuterRadiusCm);
			Selected.Add(bInRange);
			if (bInRange)
			{
				InRange.Add(Index);
			}
		}
		if (MaxListeners <= 0 || InRange.Num() <= MaxListeners)
		{
			return Selected;
		}
		// Solo los más cercanos: los de lejos la oirían muy baja de todos modos.
		InRange.StableSort([&DistancesSquared](int32 A, int32 B) { return DistancesSquared[A] < DistancesSquared[B]; });
		for (int32 Rank = MaxListeners; Rank < InRange.Num(); ++Rank)
		{
			Selected[InRange[Rank]] = false;
		}
		return Selected;
	}
}
