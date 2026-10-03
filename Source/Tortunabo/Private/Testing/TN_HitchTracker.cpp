#include "Testing/TN_HitchTracker.h"

namespace TNHitch
{
	const TCHAR* BoundName(EBound Bound)
	{
		switch (Bound)
		{
		case EBound::GameThread: return TEXT("hilo de juego");
		case EBound::RenderThread: return TEXT("hilo de render");
		case EBound::Gpu: return TEXT("GPU");
		default: return TEXT("ningún hilo (el proceso no corría)");
		}
	}

	EBound Classify(float FrameMs, float GameThreadMs, float RenderThreadMs, float GpuMs)
	{
		if (FrameMs <= 0.f)
		{
			return EBound::Unknown;
		}
		EBound Bound = EBound::GameThread;
		float Longest = GameThreadMs;
		if (RenderThreadMs > Longest)
		{
			Bound = EBound::RenderThread;
			Longest = RenderThreadMs;
		}
		if (GpuMs > Longest)
		{
			Bound = EBound::Gpu;
			Longest = GpuMs;
		}
		return Longest >= 0.5f * FrameMs ? Bound : EBound::Unknown;
	}

	void FTracker::Add(double TimeSeconds)
	{
		++TotalCount;
		Times.Add(TimeSeconds);
		if (Times.Num() > MaxRemembered)
		{
			Times.RemoveAt(0, Times.Num() - MaxRemembered, EAllowShrinking::No);
		}
	}

	double FTracker::MedianIntervalSeconds() const
	{
		if (Times.Num() < 2)
		{
			return 0.0;
		}
		TArray<double> Intervals;
		Intervals.Reserve(Times.Num() - 1);
		for (int32 Index = 1; Index < Times.Num(); ++Index)
		{
			Intervals.Add(Times[Index] - Times[Index - 1]);
		}
		Intervals.Sort();
		const int32 Mid = Intervals.Num() / 2;
		return Intervals.Num() % 2 == 1 ? Intervals[Mid] : 0.5 * (Intervals[Mid - 1] + Intervals[Mid]);
	}
}
