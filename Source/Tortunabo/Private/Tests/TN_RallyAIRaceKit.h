// Recorrido del piloto IA del Rally con física y sin ventana (#606, #695, #696): lo usan Tortunabo.Rally.Measure.AIRacesR01
// y Tortunabo.Rally.Measure.AIBumpLaps. Sin GameMode: sin cajas «?» ni munición, solo la conducción.
#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Rally/TN_RallyAIController.h"
#include "TN_RallyPhysicsTestKit.h"

namespace TNRallyPhysicsMeasure
{
	struct FRace
	{
		bool bFinished = false;
		float Seconds = 0.f;
		float ProgressM = 0.f;
		int32 Flips = 0;
		float MaxKmh = 0.f;
		float StoppedSeconds = 0.f;
	};

	/** Se llama en cada vuelco nuevo con el arco de la pista (cm) en el que ha volcado. */
	using FFlipObserver = TFunctionRef<void(double ArcCm)>;

	/**
	 * Un recorrido del piloto IA desde el hueco Slot de la parrilla hasta 30 m de la meta (o TimeoutSeconds). En un circuito
	 * la parrilla está detrás de la línea (arco cerca del final): se cuenta lo avanzado con la vuelta y se para a 30 m de
	 * completar una vuelta desde la parrilla.
	 */
	inline FRace RunAIRace(const FPhysicsWorld& Test, ATN_RallyTrack& Track, int32 Slot, float TimeoutSeconds, FFlipObserver OnFlip)
	{
		FRace Out;
		UWorld& World = *Test.World;
		ATN_Buggy* Buggy = SpawnBuggy(World, Track.GetGridSlotTransform(Slot));
		ATN_RallyAIController* Pilot = Buggy ? World.SpawnActor<ATN_RallyAIController>() : nullptr;
		if (!Buggy || !Pilot)
		{
			if (Buggy)
			{
				Buggy->Destroy();
			}
			return Out;
		}
		Pilot->Possess(Buggy);
		const double LengthCm = Track.GetTrackLengthCm();
		double Arc = Track.FindArcGlobal(Buggy->GetActorLocation());
		const double GoalCm = Track.IsCircuit() ? LengthCm - 3000.0 : LengthCm - 3000.0 - Arc;
		double ProgressCm = 0.0;
		bool bWasFlipped = false;
		for (int32 Step = 1; Step <= FMath::RoundToInt32(TimeoutSeconds * StepsPerSecond); ++Step)
		{
			Test.Step();
			const bool bFlipped = Buggy->IsFlipped();
			if (bFlipped && !bWasFlipped)
			{
				++Out.Flips;
				OnFlip(Arc);
			}
			bWasFlipped = bFlipped;
			const float Speed = FMath::Abs(Kmh(*Buggy));
			Out.MaxKmh = FMath::Max(Out.MaxKmh, Speed);
			Out.StoppedSeconds += Speed < 3.f ? StepSeconds : 0.f;
			const double Now = Track.FindArcNear(Buggy->GetActorLocation(), Arc);
			double Delta = Now - Arc;
			if (Track.IsCircuit())
			{
				Delta += Delta < -0.5 * LengthCm ? LengthCm : (Delta > 0.5 * LengthCm ? -LengthCm : 0.0);
			}
			if (Delta > 0.0)
			{
				ProgressCm += Delta;
				Arc = Now;
			}
			Out.Seconds = Step * StepSeconds;
			if (ProgressCm >= GoalCm)
			{
				Out.bFinished = true;
				break;
			}
		}
		Out.ProgressM = static_cast<float>(ProgressCm / 100.0);
		Pilot->UnPossess();
		Pilot->Destroy();
		Buggy->Destroy();
		return Out;
	}

	inline FRace RunAIRace(const FPhysicsWorld& Test, ATN_RallyTrack& Track, int32 Slot, float TimeoutSeconds)
	{
		return RunAIRace(Test, Track, Slot, TimeoutSeconds, [](double) {});
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
