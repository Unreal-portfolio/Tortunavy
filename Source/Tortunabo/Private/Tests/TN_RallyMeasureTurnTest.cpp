// Giro y vuelcos del buggy del Rally con física y sin ventana (#606, criterio 2).
//  - TurnRadius: radio de giro con el volante a tope entrando a 20 km/h (objetivo < 6 m) y a 72 km/h (20 m/s), con el ajuste
//    actual y con el de antes de #606 (UTN_BuggyData de 9a082625^ y el ángulo de su curva de dirección a esa velocidad; la
//    rigidez lateral de la rueda, 750 entonces, está en código y no se reproduce). Mismo método que TN.Rally.MeasureTurn.
//  - AIRacesR01: 5 vueltas del piloto IA (ATN_RallyAIController) a R01, una por hueco de parrilla: 0 vuelcos.
// Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Measure; Quit" -nullrhi -unattended -NoSteam

#include "Misc/AutomationTest.h"
#include "TN_RallyPhysicsTestKit.h"
#include "Components/SkeletalMeshComponent.h"
#include "TN_RallyAIRaceKit.h"
#include "UObject/Package.h"
#include "Vehicles/TN_BuggyMath.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyMeasureTurn
{
	using namespace TNRallyPhysicsMeasure;

	/** Ajuste de antes de #606 (UTN_BuggyData en 9a082625^) con el ángulo que daba su curva de dirección a SpeedCms. */
	UTN_BuggyData* MakeLegacyTurnData(float SpeedCms)
	{
		UTN_BuggyData* Legacy = NewObject<UTN_BuggyData>(GetTransientPackage());
		Legacy->FrontFriction = 3.0f;
		Legacy->RearFriction = 3.4f;
		Legacy->StabilityStartSlipDeg = 6.f;
		Legacy->CounterSteerStartSlipDeg = 0.f;
		// Volante de serie de Chaos: 2,5 y 5 por segundo y respuesta cuadrática.
		Legacy->SteerRiseRate = 2.5f;
		Legacy->SteerFallRate = 5.f;
		Legacy->bLinearSteerResponse = false;
		// Antivuelco entero a cualquier velocidad (TNBuggy::GroundRollHelp da 1 con los dos umbrales iguales).
		Legacy->AntiRollGroundRollFullSpeedCms = 0.f;
		Legacy->AntiRollGroundRollZeroSpeedCms = 0.f;
		// Curva antigua (TNBuggy::SteerCurveKeys de 9a082625^): grados por cm/s.
		static const TNBuggy::FCurveKey OldSteerKeys[] = {
			{ 0.f, 40.f }, { 500.f, 36.f }, { 1000.f, 28.f }, { 1700.f, 19.f }, { 2400.f, 14.f }, { 3050.f, 12.f } };
		Legacy->MaxSteerAngleDeg = TNBuggy::EvalLinearKeys(OldSteerKeys, SpeedCms);
		return Legacy;
	}

	struct FTurn
	{
		float RadiusM = 0.f;
		float MeanKmh = 0.f;
		float MaxRollDeg = 0.f;
		bool bFlipped = false;
		bool bReachedSpeed = false;
	};

	/**
	 * Como TN.Rally.MeasureTurn: llega a TargetKmh en línea recta y gira a tope a la derecha intentando sostener la velocidad
	 * con el acelerador; el radio sale de la velocidad y la guiñada (TNBuggy::TurnRadiusFromYawRate), de media entre 1 y 3 s.
	 */
	FTurn MeasureTurn(const FPhysicsWorld& Test, ATN_Buggy& Buggy, float TargetKmh)
	{
		FTurn Out;
		for (int32 Step = 0; Step < 20 * StepsPerSecond && Kmh(Buggy) < TargetKmh; ++Step)
		{
			Buggy.SetAIDriveInput(1.f, 0.f, HoldHeadingSteer(Buggy), false);
			Test.Step();
		}
		Out.bReachedSpeed = Kmh(Buggy) >= TargetKmh;
		double RadiusSum = 0.0;
		double SpeedSum = 0.0;
		int32 Samples = 0;
		for (int32 Step = 1; Step <= 4 * StepsPerSecond; ++Step)
		{
			const float Speed = Kmh(Buggy);
			Buggy.SetAIDriveInput(Speed < TargetKmh ? 0.7f : 0.f, Speed > TargetKmh + 3.f ? 0.3f : 0.f, 1.f, false);
			Test.Step();
			const FVector Up = Buggy.GetActorUpVector();
			Out.MaxRollDeg = FMath::Max(Out.MaxRollDeg, static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Up.Z, -1.0, 1.0)))));
			Out.bFlipped |= Buggy.IsFlipped();
			const float Time = Step * StepSeconds;
			if (Time >= 1.f && Time <= 3.f)
			{
				const FVector Velocity = Buggy.GetVelocity();
				const float Flat = static_cast<float>(FVector(Velocity.X, Velocity.Y, 0.0).Size());
				const float YawRate = static_cast<float>(Buggy.GetMesh()->GetPhysicsAngularVelocityInRadians() | Up);
				RadiusSum += TNBuggy::TurnRadiusFromYawRate(Flat, YawRate);
				SpeedSum += Flat;
				++Samples;
			}
		}
		Out.RadiusM = Samples > 0 ? static_cast<float>(RadiusSum / Samples / 100.0) : 0.f;
		Out.MeanKmh = Samples > 0 ? static_cast<float>(TNRally::CmsToKmh(SpeedSum / Samples)) : 0.f;
		return Out;
	}

	/** Cada medida en un mundo y una losa nuevos: así empiezan todas igual. */
	FTurn TurnOnFreshGround(float TargetKmh, UTN_BuggyData* Data)
	{
		FPhysicsWorld Test(TEXT("TNRallyTurnMeasureWorld"));
		if (!Test.World || !SpawnFlatGround(*Test.World))
		{
			return FTurn();
		}
		ATN_Buggy* Buggy = SpawnBuggy(*Test.World, FTransform(FVector(-100000.0, 0.0, SpawnLiftCm)), Data);
		if (!Buggy)
		{
			return FTurn();
		}
		Settle(Test, *Buggy);
		return MeasureTurn(Test, *Buggy, TargetKmh);
	}

	FString DescribeTurn(const TCHAR* Label, const FTurn& Turn)
	{
		return FString::Printf(TEXT("%s: radio %.2f m a %.1f km/h de media en el giro, alabeo máximo %.0f grados, %s%s"), Label, Turn.RadiusM,
			Turn.MeanKmh, Turn.MaxRollDeg, Turn.bFlipped ? TEXT("VUELCA") : TEXT("no vuelca"),
			Turn.bReachedSpeed ? TEXT("") : TEXT(" (no llegó a la velocidad)"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyMeasureTurnRadiusTest, "Tortunabo.Rally.Measure.TurnRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyMeasureTurnRadiusTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyMeasureTurn;
	constexpr float SlowKmh = 20.f;
	constexpr float FastKmh = 72.f;
	const FTurn SlowNow = TurnOnFreshGround(SlowKmh, nullptr);
	const FTurn SlowBefore = TurnOnFreshGround(SlowKmh, MakeLegacyTurnData(static_cast<float>(SlowKmh / 0.036f)));
	const FTurn FastNow = TurnOnFreshGround(FastKmh, nullptr);
	const FTurn FastBefore = TurnOnFreshGround(FastKmh, MakeLegacyTurnData(static_cast<float>(FastKmh / 0.036f)));
	AddInfo(DescribeTurn(TEXT("Entrando a 20 km/h, ahora"), SlowNow));
	AddInfo(DescribeTurn(TEXT("Entrando a 20 km/h, antes de #606"), SlowBefore));
	AddInfo(DescribeTurn(TEXT("Entrando a 72 km/h (20 m/s), ahora"), FastNow));
	AddInfo(DescribeTurn(TEXT("Entrando a 72 km/h (20 m/s), antes de #606"), FastBefore));
	TestTrue(TEXT("llega a 20 km/h"), SlowNow.bReachedSpeed);
	TestTrue(*FString::Printf(TEXT("radio a 20 km/h menor de 6 m (%.2f m)"), SlowNow.RadiusM), SlowNow.RadiusM > 0.f && SlowNow.RadiusM < 6.f);
	TestFalse(TEXT("a 20 km/h con el volante a tope no vuelca"), SlowNow.bFlipped);
	TestTrue(TEXT("caso negativo: antes de #606 el radio a 20 km/h era mayor"), SlowBefore.RadiusM > SlowNow.RadiusM);
	TestTrue(TEXT("a 72 km/h gira más cerrado que antes de #606"), FastNow.RadiusM > 0.f && FastNow.RadiusM < FastBefore.RadiusM);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyMeasureAIRacesTest, "Tortunabo.Rally.Measure.AIRacesR01",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyMeasureAIRacesTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyMeasureTurn;
	if (!TestTrue(TEXT("Manifest de R01"), FPaths::FileExists(TNRally::VariantManifestPath(FName(MeasureVariant())))))
	{
		return false;
	}
	FPhysicsWorld Test(TEXT("TNRallyAIRacesWorld"));
	ATN_RallyTrack* Track = Test.World ? PrepareMeasureTrack(Test) : nullptr;
	if (!TestNotNull(TEXT("Pista de R01 construida"), Track))
	{
		return false;
	}
	const float LengthM = Track->GetTrackLengthCm() / 100.f;
	// Más lento que una media de 25 km/h ya es un recorrido roto (atascado o fuera de la pista).
	const float Timeout = LengthM / (25.f / 3.6f) + 30.f;
	int32 Flips = 0;
	int32 Finished = 0;
	constexpr int32 Races = 5;
	for (int32 Slot = 0; Slot < Races; ++Slot)
	{
		const FRace Race = RunAIRace(Test, *Track, Slot, Timeout);
		Flips += Race.Flips;
		Finished += Race.bFinished ? 1 : 0;
		AddInfo(FString::Printf(TEXT("R01, recorrido %d (hueco %d): %s en %.0f s, %.0f de %.0f m, %d vuelcos, punta %.0f km/h, %.0f s parado."),
			Slot + 1, Slot, Race.bFinished ? TEXT("llega") : TEXT("NO llega"), Race.Seconds, Race.ProgressM, LengthM, Race.Flips,
			Race.MaxKmh, Race.StoppedSeconds));
	}
	TestEqual(TEXT("vuelcos del piloto IA en 5 recorridos de R01"), Flips, 0);
	TestEqual(TEXT("recorridos que llegan a la meta"), Finished, Races);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
