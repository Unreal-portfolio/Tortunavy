// Aceleración del buggy del Rally con física y sin ventana (#294: 0-60 km/h en ~2 s sin turbo y sin cambiar la punta).
//  - ZeroToSixtyFlat: en llano, desde parado y a fondo en línea recta: 0-60 y 0-100 km/h y punta.
//  - TrackR01: TN.Rally.Measure en la salida de R01 (sigue la pista, con sus cuestas y curvas), como en el juego.
// Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Measure; Quit" -nullrhi -unattended -NoSteam

#include "Misc/AutomationTest.h"
#include "TN_RallyPhysicsTestKit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyMeasureDrive
{
	using namespace TNRallyPhysicsMeasure;

	struct FSprint
	{
		float ZeroToSixty = -1.f;
		float ZeroToHundred = -1.f;
		float TopKmh = 0.f;
		float StartKmh = 0.f;
	};

	/** Acelerador a fondo durante Seconds desde parado, en línea recta. */
	FSprint Sprint(const FPhysicsWorld& Test, ATN_Buggy& Buggy, float Seconds)
	{
		FSprint Out;
		Out.StartKmh = Kmh(Buggy);
		for (int32 Step = 1; Step <= FMath::RoundToInt32(Seconds * StepsPerSecond); ++Step)
		{
			Buggy.SetAIDriveInput(1.f, 0.f, HoldHeadingSteer(Buggy), false);
			Test.Step();
			const float Speed = Kmh(Buggy);
			const float Time = Step * StepSeconds;
			Out.TopKmh = FMath::Max(Out.TopKmh, Speed);
			Out.ZeroToSixty = (Out.ZeroToSixty < 0.f && Speed >= 60.f) ? Time : Out.ZeroToSixty;
			Out.ZeroToHundred = (Out.ZeroToHundred < 0.f && Speed >= 100.f) ? Time : Out.ZeroToHundred;
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyMeasureZeroToSixtyTest, "Tortunabo.Rally.Measure.ZeroToSixtyFlat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyMeasureZeroToSixtyTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyMeasureDrive;
	FPhysicsWorld Test(TEXT("TNRallyZeroToSixtyWorld"));
	if (!TestNotNull(TEXT("Mundo de prueba"), Test.World) || !TestNotNull(TEXT("Suelo llano"), SpawnFlatGround(*Test.World)))
	{
		return false;
	}
	ATN_Buggy* Buggy = SpawnBuggy(*Test.World, FTransform(FVector(-140000.0, 0.0, SpawnLiftCm)));
	if (!TestNotNull(TEXT("Buggy"), Buggy))
	{
		return false;
	}
	Settle(Test, *Buggy);
	const FSprint Run = Sprint(Test, *Buggy, 25.f);
	AddInfo(FString::Printf(TEXT("Llano, sin turbo: 0-60 km/h %.2f s, 0-100 km/h %.2f s, punta %.1f km/h (salida desde %.1f km/h)."),
		Run.ZeroToSixty, Run.ZeroToHundred, Run.TopKmh, Run.StartKmh));
	TestTrue(*FString::Printf(TEXT("0-60 km/h en ~2 s (%.2f s, entre 1,7 y 2,2)"), Run.ZeroToSixty), Run.ZeroToSixty >= 1.7f && Run.ZeroToSixty <= 2.2f);
	TestTrue(*FString::Printf(TEXT("la punta no cambia: ~110 km/h (%.1f, entre 105 y 118)"), Run.TopKmh), Run.TopKmh >= 105.f && Run.TopKmh <= 118.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyMeasureTrackTest, "Tortunabo.Rally.Measure.TrackR01",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyMeasureTrackTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyPhysicsMeasure;
	if (!TestTrue(TEXT("Manifest de R01"), FPaths::FileExists(TNRally::VariantManifestPath(FName(MeasureVariant())))))
	{
		return false;
	}
	FPhysicsWorld Test(TEXT("TNRallyTrackMeasureWorld"));
	ATN_RallyTrack* Track = Test.World ? PrepareMeasureTrack(Test) : nullptr;
	if (!TestNotNull(TEXT("Pista de R01 construida"), Track) || !TestNotNull(TEXT("Buggy en la parrilla"),
		SpawnBuggy(*Test.World, Track->GetGridSlotTransform(0))))
	{
		return false;
	}
	FLineCapture Capture(TEXT("[Medida]"));
	// Asentado (hasta 6 s), 12 s a fondo por la pista y la frenada desde 60 km/h.
	IConsoleManager::Get().ProcessUserConsoleInput(TEXT("TN.Rally.Measure 12 0"), *GLog, Test.World);
	FString Result;
	for (int32 Second = 0; Second < 60 && Result.IsEmpty(); ++Second)
	{
		Test.Advance(1.f);
		for (const FString& Line : Capture.Take())
		{
			Result = Line.Contains(TEXT("0-60 km/h")) ? Line : Result;
		}
	}
	if (!TestFalse(TEXT("TN.Rally.Measure da resultado en R01"), Result.IsEmpty()))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("R01, salida: %s"), *Result));
	TestTrue(TEXT("la medida acaba bien"), Result.Contains(TEXT("hecho")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
