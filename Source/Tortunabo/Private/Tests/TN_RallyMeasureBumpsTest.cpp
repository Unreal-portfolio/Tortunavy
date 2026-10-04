// Baches de los circuitos de tierra del Rally con física y sin ventana (#695, #696), sobre las variantes generadas R02 a R06:
//  - BumpsFullThrottle: el buggy entra a fondo (BumpEntryKmh) en cada tren de baches (whoops y tabla de lavar) y en los
//    baches de aviso, y en los de aviso también frenando a tope como el piloto IA. Ninguno vuelca; la tabla de lavar y los
//    baches de aviso no lo despegan (las cuatro ruedas en el aire como mucho MaxAirSeconds seguidos).
//  - AIBumpLaps: 3 vueltas del piloto IA (una por hueco de parrilla) a cada variante: ningún vuelco en un tren de baches
//    y como mucho 1 en toda la variante. Sin GameMode: sin munición (el retroceso del mortero lo mide RecoilNoFlip y las
//    carreras completas, Scripts/rally_medir_vuelcos.py).
// Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Measure.Bump; Automation RunTests Tortunabo.Rally.Measure.AIBump; Quit" -nullrhi -unattended -NoSteam

#include "Misc/AutomationTest.h"
#include "TN_RallyAIRaceKit.h"
#include "TN_RallyPhysicsTestKit.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Rally/TN_RallyCircuit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyMeasureBumps
{
	using namespace TNRallyPhysicsMeasure;

	/** Variantes con baches (perfil tierra, #682 y #692). */
	const TCHAR* const TierraVariants[] = {
		TEXT("R02_circuito_tierra"), TEXT("R03_circuito_dunas_costeras"), TEXT("R04_circuito_cantera"),
		TEXT("R05_circuito_marismas"), TEXT("R06_circuito_lomas")};
	/** A fondo: casi la punta del buggy (~110 km/h). */
	constexpr float BumpEntryKmh = 100.f;
	/** Como llega el piloto IA a la frenada de una horquilla (AI MaxSpeedKmh). */
	constexpr float BrakingEntryKmh = 90.f;
	constexpr double RunUpCm = 2500.0;
	constexpr double LookAheadCm = 1500.0;
	constexpr double ExitCm = 500.0;
	/** Más de esto con las cuatro ruedas en el aire es despegar (unos pocos fotogramas de rebote no lo son). */
	constexpr float MaxAirSeconds = 0.1f;
	/** Margen para atribuir un vuelco del piloto IA a un tren de baches: antes del primero y tras el último. */
	constexpr double FlipBeforeCm = 500.0;
	constexpr double FlipAfterCm = 3000.0;

	struct FTrain
	{
		FString Id;
		FString Pattern;
		double StartCm = 0.0;
		double EndCm = 0.0;
	};

	struct FPass
	{
		bool bFlipped = false;
		float MaxAirSeconds = 0.f;
		float MaxTiltDeg = 0.f;
		float EntryKmh = 0.f;
		bool bSpawned = true;
		/** Si ha pasado el tren entero (sin esto, un buggy atascado antes no mediría nada). */
		bool bPassed = false;
	};

	/** Trenes de baches (baches y baches_aviso) del manifest con su arco en la spline de la pista. */
	bool ReadTrains(FName Variant, double SplineLengthCm, TArray<FTrain>& Out)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *TNRally::VariantManifestPath(Variant))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* Lap = nullptr;
		double RoadLengthM = 0.0;
		const TArray<TSharedPtr<FJsonValue>>* Elements = nullptr;
		if (!Root->TryGetObjectField(TEXT("lap"), Lap) || !(*Lap)->TryGetNumberField(TEXT("length_m"), RoadLengthM)
			|| !Root->TryGetArrayField(TEXT("elements"), Elements))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Elements)
		{
			const TSharedPtr<FJsonObject> Element = Value->AsObject();
			FString Type;
			const TArray<TSharedPtr<FJsonValue>>* Span = nullptr;
			if (!Element || !Element->TryGetStringField(TEXT("type"), Type) || !Type.StartsWith(TEXT("baches"))
				|| !Element->TryGetArrayField(TEXT("train_s_m"), Span) || Span->Num() != 2)
			{
				continue;
			}
			FTrain Train;
			Element->TryGetStringField(TEXT("id"), Train.Id);
			Element->TryGetStringField(TEXT("pattern"), Train.Pattern);
			Train.StartCm = TNRallyCircuit::RoadMetersToArc((*Span)[0]->AsNumber(), RoadLengthM * 100.0, SplineLengthCm, true);
			Train.EndCm = TNRallyCircuit::RoadMetersToArc((*Span)[1]->AsNumber(), RoadLengthM * 100.0, SplineLengthCm, true);
			Out.Add(Train);
		}
		return true;
	}

	double Wrap(double ArcCm, double LengthCm)
	{
		return FMath::Fmod(FMath::Fmod(ArcCm, LengthCm) + LengthCm, LengthCm);
	}

	/** Cm recorridos desde FromCm hasta ToCm hacia delante por el lazo. */
	double Ahead(double FromCm, double ToCm, double LengthCm)
	{
		return Wrap(ToCm - FromCm, LengthCm);
	}

	bool AllWheelsInAir(const ATN_Buggy& Buggy)
	{
		const UChaosWheeledVehicleMovementComponent* Move = Buggy.GetWheeledMovement();
		if (!Move || Move->Wheels.IsEmpty())
		{
			return false;
		}
		for (int32 Index = 0; Index < Move->Wheels.Num(); ++Index)
		{
			if (Move->GetWheelState(Index).bInContact)
			{
				return false;
			}
		}
		return true;
	}

	/** Volante hacia el punto del eje LookAheadCm por delante (persecución pura). */
	float FollowRoadSteer(const ATN_Buggy& Buggy, const ATN_RallyTrack& Track, double ArcCm)
	{
		const FVector Target = Track.GetLocationAtArc(Wrap(ArcCm + LookAheadCm, Track.GetTrackLengthCm()));
		const FVector Local = Buggy.GetActorTransform().InverseTransformPosition(Target);
		const float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
		return FMath::Clamp(AngleDeg / FMath::Max(Buggy.GetMaxSteerAngleDeg(), 1.f), -1.f, 1.f);
	}

	/**
	 * Buggy en el eje RunUpCm antes del tren, mirando hacia delante y lanzado a EntryKmh, siguiendo la pista hasta pasarlo: a
	 * fondo o, con bBrake, sosteniendo EntryKmh hasta el tren y frenando a tope dentro de él (como el piloto IA en la frenada).
	 */
	FPass DriveThrough(const FPhysicsWorld& Test, const ATN_RallyTrack& Track, const FTrain& Train, float EntryKmh, bool bBrake)
	{
		FPass Out;
		const double LengthCm = Track.GetTrackLengthCm();
		const double SpawnArc = Wrap(Train.StartCm - RunUpCm, LengthCm);
		const FVector Direction = Track.GetDirectionAtArc(SpawnArc).GetSafeNormal2D();
		ATN_Buggy* Buggy = SpawnBuggy(*Test.World, FTransform(FRotator(0.0, Direction.Rotation().Yaw, 0.0),
			Track.GetLocationAtArc(SpawnArc) + FVector(0.0, 0.0, SpawnLiftCm)));
		if (!Buggy)
		{
			Out.bSpawned = false;
			return Out;
		}
		Settle(Test, *Buggy);
		Buggy->GetMesh()->SetPhysicsLinearVelocity(Direction * (EntryKmh / 0.036f));
		double Arc = SpawnArc;
		const double TrainLength = Ahead(Train.StartCm, Train.EndCm, LengthCm);
		float AirSeconds = 0.f;
		bool bInTrain = false;
		const int32 MaxSteps = 20 * StepsPerSecond;
		for (int32 Step = 0; Step < MaxSteps; ++Step)
		{
			const bool bBraking = bBrake && bInTrain;
			const float Throttle = bBraking ? 0.f : (!bBrake || Kmh(*Buggy) < EntryKmh ? 1.f : 0.f);
			Buggy->SetAIDriveInput(Throttle, bBraking ? 1.f : 0.f, FollowRoadSteer(*Buggy, Track, Arc), false);
			Test.Step();
			Arc = Track.FindArcNear(Buggy->GetActorLocation(), Arc);
			const double FromStart = Ahead(Train.StartCm, Arc, LengthCm);
			const bool bBefore = FromStart > 0.5 * LengthCm;
			if (bBefore)
			{
				Out.EntryKmh = Kmh(*Buggy);
				continue;
			}
			if (FromStart > TrainLength + ExitCm)
			{
				Out.bPassed = true;
				break;
			}
			bInTrain = true;
			AirSeconds = AllWheelsInAir(*Buggy) ? AirSeconds + StepSeconds : 0.f;
			Out.MaxAirSeconds = FMath::Max(Out.MaxAirSeconds, AirSeconds);
			const double UpZ = FMath::Clamp(Buggy->GetActorUpVector().Z, -1.0, 1.0);
			Out.MaxTiltDeg = FMath::Max(Out.MaxTiltDeg, static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(UpZ))));
			Out.bFlipped |= Buggy->IsFlipped();
		}
		Buggy->Destroy();
		return Out;
	}

	FString Describe(const FTrain& Train, const TCHAR* How, const FPass& Pass)
	{
		return FString::Printf(TEXT("%s (%s), %s: entra a %.0f km/h, %.2f s seguidos en el aire, inclinación máxima %.0f grados, %s"),
			*Train.Id, *Train.Pattern, How, Pass.EntryKmh, Pass.MaxAirSeconds, Pass.MaxTiltDeg,
			Pass.bFlipped ? TEXT("VUELCA") : TEXT("no vuelca"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyMeasureBumpsFullThrottleTest, "Tortunabo.Rally.Measure.BumpsFullThrottle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyMeasureBumpsFullThrottleTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyMeasureBumps;
	int32 Warnings = 0;
	for (const TCHAR* Variant : TierraVariants)
	{
		FPhysicsWorld Test(TEXT("TNRallyBumpsWorld"));
		ATN_RallyTrack* Track = Test.World ? PrepareMeasureTrack(Test, FName(Variant)) : nullptr;
		TArray<FTrain> Trains;
		if (!TestNotNull(*FString::Printf(TEXT("Pista de %s construida"), Variant), Track)
			|| !TestTrue(*FString::Printf(TEXT("Trenes de baches de %s"), Variant), ReadTrains(FName(Variant), Track->GetTrackLengthCm(), Trains)))
		{
			continue;
		}
		for (const FTrain& Train : Trains)
		{
			const bool bWarning = Train.Pattern == TEXT("aviso");
			Warnings += bWarning ? 1 : 0;
			const FPass Fast = DriveThrough(Test, *Track, Train, BumpEntryKmh, false);
			AddInfo(FString::Printf(TEXT("%s, %s"), Variant, *Describe(Train, TEXT("a fondo"), Fast)));
			const FString Where = FString::Printf(TEXT("%s %s"), Variant, *Train.Id);
			TestTrue(*FString::Printf(TEXT("%s: buggy puesto"), *Where), Fast.bSpawned);
			TestTrue(*FString::Printf(TEXT("%s a fondo: pasa el tren entero"), *Where), Fast.bPassed);
			TestFalse(*FString::Printf(TEXT("%s a fondo no vuelca"), *Where), Fast.bFlipped);
			TestTrue(*FString::Printf(TEXT("%s: llega a más de 80 km/h (%.0f)"), *Where, Fast.EntryKmh), Fast.EntryKmh > 80.f);
			if (Train.Pattern == TEXT("tabla_lavar") || bWarning)
			{
				TestTrue(*FString::Printf(TEXT("%s a fondo no despega (%.2f s en el aire)"), *Where, Fast.MaxAirSeconds),
					Fast.MaxAirSeconds <= MaxAirSeconds);
			}
			if (bWarning)
			{
				const FPass Braking = DriveThrough(Test, *Track, Train, BrakingEntryKmh, true);
				AddInfo(FString::Printf(TEXT("%s, %s"), Variant, *Describe(Train, TEXT("frenando"), Braking)));
				TestTrue(*FString::Printf(TEXT("%s frenando: pasa el tren entero"), *Where), Braking.bPassed);
				TestFalse(*FString::Printf(TEXT("%s frenando no vuelca"), *Where), Braking.bFlipped);
				TestTrue(*FString::Printf(TEXT("%s frenando no despega (%.2f s en el aire)"), *Where, Braking.MaxAirSeconds),
					Braking.MaxAirSeconds <= MaxAirSeconds);
			}
		}
	}
	TestTrue(TEXT("hay baches de aviso en alguna variante"), Warnings > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyMeasureAIBumpLapsTest, "Tortunabo.Rally.Measure.AIBumpLaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyMeasureAIBumpLapsTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyMeasureBumps;
	constexpr int32 Laps = 3;
	for (const TCHAR* Variant : TierraVariants)
	{
		FPhysicsWorld Test(TEXT("TNRallyAIBumpLapsWorld"));
		ATN_RallyTrack* Track = Test.World ? PrepareMeasureTrack(Test, FName(Variant)) : nullptr;
		TArray<FTrain> Trains;
		if (!TestNotNull(*FString::Printf(TEXT("Pista de %s construida"), Variant), Track)
			|| !TestTrue(*FString::Printf(TEXT("Trenes de baches de %s"), Variant), ReadTrains(FName(Variant), Track->GetTrackLengthCm(), Trains)))
		{
			continue;
		}
		const double LengthCm = Track->GetTrackLengthCm();
		const float Timeout = static_cast<float>(LengthCm / 100.0) / (25.f / 3.6f) + 30.f;
		int32 Flips = 0;
		int32 BumpFlips = 0;
		int32 Finished = 0;
		for (int32 Slot = 0; Slot < Laps; ++Slot)
		{
			const FRace Race = RunAIRace(Test, *Track, Slot, Timeout, [&](double ArcCm)
			{
				for (const FTrain& Train : Trains)
				{
					const double Window = Ahead(Train.StartCm, Train.EndCm, LengthCm) + FlipBeforeCm + FlipAfterCm;
					if (Ahead(Train.StartCm - FlipBeforeCm, ArcCm, LengthCm) <= Window)
					{
						++BumpFlips;
						AddInfo(FString::Printf(TEXT("%s: vuelco en %s, arco %.0f m"), Variant, *Train.Id, ArcCm / 100.0));
						return;
					}
				}
				AddInfo(FString::Printf(TEXT("%s: vuelco fuera de los baches, arco %.0f m"), Variant, ArcCm / 100.0));
			});
			Flips += Race.Flips;
			Finished += Race.bFinished ? 1 : 0;
			AddInfo(FString::Printf(TEXT("%s, vuelta %d (hueco %d): %s en %.0f s, %d vuelcos, punta %.0f km/h, %.0f s parado."),
				Variant, Slot + 1, Slot, Race.bFinished ? TEXT("llega") : TEXT("NO llega"), Race.Seconds, Race.Flips, Race.MaxKmh,
				Race.StoppedSeconds));
		}
		TestEqual(*FString::Printf(TEXT("%s: vuelcos del piloto IA en los baches en %d vueltas"), Variant, Laps), BumpFlips, 0);
		TestTrue(*FString::Printf(TEXT("%s: como mucho 1 vuelco en %d vueltas (%d)"), Variant, Laps, Flips), Flips <= 1);
		TestEqual(*FString::Printf(TEXT("%s: vueltas que llegan a la meta"), Variant), Finished, Laps);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
