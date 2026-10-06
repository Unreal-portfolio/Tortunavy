// Conducción de los karts con física y sin ventana (#742): el kart de Karts (ATN_KartBuggy) contra el del Rally
// (ATN_RallyKartBuggy, sin la conducción de los karts) en llano, con el mismo mundo y la misma maniobra.
//  - SpeedAndAcceleration: 0-60 km/h, 0-100 km/h y punta a fondo en línea recta.
//  - DriftBoost: derrape con el freno de mano a 80 km/h; sin trompo ni vuelco y mini-turbo al soltarlo (solo en Karts).
//  - HighSpeedTurn: volante a tope a 100 y a 130 km/h; la dirección se cierra con la velocidad y el kart no vuelca.
//  - DriftServerDriver: derrape como lo ve el servidor con una conductora cliente (entrada cruda a 0, solo el estado replicado).
// Headless:
//   UnrealEditor-Win64-DebugGame-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Kart.Measure; Quit" -nullrhi -unattended -NoSteam -nosound

#include "Misc/AutomationTest.h"
#include "TN_RallyPhysicsTestKit.h"
#include "Kart/TN_KartBuggy.h"
#include "Rally/TN_RallyKartBuggy.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Vehicles/TN_BuggyMath.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNKartMeasureTest
{
	using namespace TNRallyPhysicsMeasure;

	constexpr double StartX = -140000.0;
	/** Deriva (grados) a partir de la cual el kart ha hecho un trompo. */
	constexpr float SpinSlipDeg = 75.f;

	ATN_KartBuggy* SpawnKart(UWorld& World, TSubclassOf<ATN_KartBuggy> Class)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<ATN_KartBuggy>(Class, FTransform(FVector(StartX, 0.0, SpawnLiftCm)), Params);
	}

	/** Mundo con suelo llano y un kart asentado; false si algo no se pudo crear. */
	bool Prepare(FPhysicsWorld& Test, TSubclassOf<ATN_KartBuggy> Class, ATN_KartBuggy*& OutKart)
	{
		OutKart = nullptr;
		if (!Test.World || !SpawnFlatGround(*Test.World))
		{
			return false;
		}
		OutKart = SpawnKart(*Test.World, Class);
		if (!OutKart)
		{
			return false;
		}
		Settle(Test, *OutKart);
		return true;
	}

	struct FSprint
	{
		float ZeroToSixty = -1.f;
		float ZeroToHundred = -1.f;
		float TopKmh = 0.f;
	};

	FSprint Sprint(const FPhysicsWorld& Test, ATN_Buggy& Kart, float Seconds)
	{
		FSprint Out;
		for (int32 Step = 1; Step <= FMath::RoundToInt32(Seconds * StepsPerSecond); ++Step)
		{
			Kart.SetAIDriveInput(1.f, 0.f, HoldHeadingSteer(Kart), false);
			Test.Step();
			const float Speed = Kmh(Kart);
			const float Time = Step * StepSeconds;
			Out.TopKmh = FMath::Max(Out.TopKmh, Speed);
			Out.ZeroToSixty = (Out.ZeroToSixty < 0.f && Speed >= 60.f) ? Time : Out.ZeroToSixty;
			Out.ZeroToHundred = (Out.ZeroToHundred < 0.f && Speed >= 100.f) ? Time : Out.ZeroToHundred;
		}
		return Out;
	}

	/** A fondo en línea recta hasta TargetKmh (False si no llega en 25 s). */
	bool ReachSpeed(const FPhysicsWorld& Test, ATN_Buggy& Kart, float TargetKmh)
	{
		for (int32 Step = 0; Step < 25 * StepsPerSecond && Kmh(Kart) < TargetKmh; ++Step)
		{
			Kart.SetAIDriveInput(1.f, 0.f, HoldHeadingSteer(Kart), false);
			Test.Step();
		}
		return Kmh(Kart) >= TargetKmh;
	}

	float YawRateDegPerSecond(const ATN_Buggy& Kart)
	{
		return FMath::RadiansToDegrees(static_cast<float>(Kart.GetMesh()->GetPhysicsAngularVelocityInRadians() | Kart.GetActorUpVector()));
	}

	struct FDrift
	{
		bool bReached = true;
		float EntryKmh = 0.f;
		float MinKmh = 1000.f;
		float MeanSlipDeg = 0.f;
		float MaxSlipDeg = 0.f;
		float MaxYawRate = 0.f;
		bool bSpun = false;
		bool bFlipped = false;
		/** Mini-turbo que tiene al soltar el freno de mano y fuerza máxima del turbo en los 3 s siguientes. */
		float BoostSeconds = 0.f;
		float PeakBoostStrength = 0.f;
		float ReleaseKmh = 0.f;
		float KmhAfter1p5 = 0.f;
		float PeakKmh = 0.f;
		/** Deriva y velocidad cada 0,25 s del derrape, para ver cómo evoluciona. */
		FString Trace;
	};

	/** Derrape de DriftSeconds con el freno de mano y la dirección Steer a EntryKmh, y 3 s con el acelerador a fondo al soltarlo. */
	FDrift DriftRun(TSubclassOf<ATN_KartBuggy> Class, float EntryKmh, float DriftSeconds, float Steer)
	{
		FDrift Out;
		FPhysicsWorld Test(TEXT("TNKartDriftWorld"));
		ATN_KartBuggy* Kart = nullptr;
		if (!Prepare(Test, Class, Kart))
		{
			Out.bReached = false;
			return Out;
		}
		Out.bReached = ReachSpeed(Test, *Kart, EntryKmh);
		Out.EntryKmh = Kmh(*Kart);
		float SlipSum = 0.f;
		int32 Samples = 0;
		for (int32 Step = 0; Step < FMath::RoundToInt32(DriftSeconds * StepsPerSecond); ++Step)
		{
			Kart->SetAIDriveInput(1.f, 0.f, Steer, true);
			Test.Step();
			const float Slip = TNBuggy::SlipAngleDeg(Kart->GetActorForwardVector(), Kart->GetVelocity());
			SlipSum += FMath::Abs(Slip);
			++Samples;
			Out.MaxSlipDeg = FMath::Max(Out.MaxSlipDeg, FMath::Abs(Slip));
			Out.MaxYawRate = FMath::Max(Out.MaxYawRate, FMath::Abs(YawRateDegPerSecond(*Kart)));
			Out.MinKmh = FMath::Min(Out.MinKmh, Kmh(*Kart));
			Out.bSpun |= FMath::Abs(Slip) > SpinSlipDeg;
			Out.bFlipped |= Kart->IsFlipped();
			if (Step % (StepsPerSecond / 4) == 0)
			{
				Out.Trace += FString::Printf(TEXT(" %.2f:%.0f°/%.0f"), Step * StepSeconds, Slip, Kmh(*Kart));
			}
		}
		Out.MeanSlipDeg = Samples > 0 ? SlipSum / Samples : 0.f;
		Out.ReleaseKmh = Kmh(*Kart);
		for (int32 Step = 0; Step < 3 * StepsPerSecond; ++Step)
		{
			Kart->SetAIDriveInput(1.f, 0.f, 0.f, false);
			Test.Step();
			if (Step < StepsPerSecond / 5)
			{
				Out.BoostSeconds = FMath::Max(Out.BoostSeconds, Kart->GetTimedBoostSecondsLeft());
			}
			Out.PeakBoostStrength = FMath::Max(Out.PeakBoostStrength, Kart->GetBoostStrength());
			Out.PeakKmh = FMath::Max(Out.PeakKmh, Kmh(*Kart));
			Out.bFlipped |= Kart->IsFlipped();
			if (Step == FMath::RoundToInt32(1.5f * StepsPerSecond))
			{
				Out.KmhAfter1p5 = Kmh(*Kart);
			}
		}
		return Out;
	}

	FString Describe(const TCHAR* Label, const FDrift& Run)
	{
		return FString::Printf(TEXT("%s: entra a %.1f km/h, deriva media %.1f° y máxima %.1f°, guiñada máxima %.0f°/s, mínima %.1f km/h, trompo %s, vuelco %s; "
			"al soltar %.1f km/h, mini-turbo %.2f s (fuerza máxima %.2f), a 1,5 s %.1f km/h, máxima %.1f km/h"),
			Label, Run.EntryKmh, Run.MeanSlipDeg, Run.MaxSlipDeg, Run.MaxYawRate, Run.MinKmh < 999.f ? Run.MinKmh : 0.f,
			Run.bSpun ? TEXT("SÍ") : TEXT("no"), Run.bFlipped ? TEXT("SÍ") : TEXT("no"), Run.ReleaseKmh, Run.BoostSeconds,
			Run.PeakBoostStrength, Run.KmhAfter1p5, Run.PeakKmh);
	}

	struct FTurn
	{
		bool bReached = true;
		bool bFlipped = false;
		float RadiusM = 0.f;
		float MeanKmh = 0.f;
		float MaxTiltDeg = 0.f;
	};

	/** Volante a tope durante 1,5 s a TargetKmh (el acelerador mantiene la velocidad); el radio sale de la media entre 0,5 y 1,5 s. */
	FTurn TurnRun(TSubclassOf<ATN_KartBuggy> Class, float TargetKmh)
	{
		FTurn Out;
		FPhysicsWorld Test(TEXT("TNKartTurnWorld"));
		ATN_KartBuggy* Kart = nullptr;
		if (!Prepare(Test, Class, Kart))
		{
			Out.bReached = false;
			return Out;
		}
		Out.bReached = ReachSpeed(Test, *Kart, TargetKmh);
		double RadiusSum = 0.0;
		double SpeedSum = 0.0;
		int32 Samples = 0;
		for (int32 Step = 0; Step < FMath::RoundToInt32(1.5f * StepsPerSecond); ++Step)
		{
			Kart->SetAIDriveInput(Kmh(*Kart) < TargetKmh ? 1.f : 0.3f, 0.f, 1.f, false);
			Test.Step();
			const double UpZ = FMath::Clamp(Kart->GetActorUpVector().Z, -1.0, 1.0);
			Out.MaxTiltDeg = FMath::Max(Out.MaxTiltDeg, static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(UpZ))));
			Out.bFlipped |= Kart->IsFlipped();
			if (Step >= StepsPerSecond / 2)
			{
				const FVector Velocity = Kart->GetVelocity();
				const float Flat = static_cast<float>(FVector(Velocity.X, Velocity.Y, 0.0).Size());
				RadiusSum += TNBuggy::TurnRadiusFromYawRate(Flat, YawRateDegPerSecond(*Kart) * PI / 180.f);
				SpeedSum += Flat;
				++Samples;
			}
		}
		Out.RadiusM = Samples > 0 ? static_cast<float>(RadiusSum / Samples / 100.0) : 0.f;
		Out.MeanKmh = Samples > 0 ? static_cast<float>(TNRally::CmsToKmh(SpeedSum / Samples)) : 0.f;
		return Out;
	}

	struct FSteerPoint
	{
		float Stick = 0.f;
		float LateralG = 0.f;
		float Kmh = 0.f;
		bool bFlipped = false;
	};

	/** Aceleración lateral (g) entre 0,7 y 1,3 s de un giro con el volante en Stick a TargetKmh (el acelerador mantiene la velocidad). */
	FSteerPoint SteerPoint(TSubclassOf<ATN_KartBuggy> Class, float TargetKmh, float Stick)
	{
		FSteerPoint Out;
		Out.Stick = Stick;
		FPhysicsWorld Test(TEXT("TNKartSteerWorld"));
		ATN_KartBuggy* Kart = nullptr;
		if (!Prepare(Test, Class, Kart) || !ReachSpeed(Test, *Kart, TargetKmh))
		{
			return Out;
		}
		double GSum = 0.0;
		double SpeedSum = 0.0;
		int32 Samples = 0;
		for (int32 Step = 0; Step < FMath::RoundToInt32(1.3f * StepsPerSecond); ++Step)
		{
			Kart->SetAIDriveInput(Kmh(*Kart) < TargetKmh ? 1.f : 0.4f, 0.f, Stick, false);
			Test.Step();
			Out.bFlipped |= Kart->IsFlipped();
			if (Step >= FMath::RoundToInt32(0.7f * StepsPerSecond))
			{
				const FVector Velocity = Kart->GetVelocity();
				const double Flat = FVector(Velocity.X, Velocity.Y, 0.0).Size();
				GSum += Flat * FMath::DegreesToRadians(FMath::Abs(YawRateDegPerSecond(*Kart))) / 981.0;
				SpeedSum += Flat;
				++Samples;
			}
		}
		Out.LateralG = Samples > 0 ? static_cast<float>(GSum / Samples) : 0.f;
		Out.Kmh = Samples > 0 ? static_cast<float>(TNRally::CmsToKmh(SpeedSum / Samples)) : 0.f;
		return Out;
	}

	/** Parámetros de UChaosVehicleMovementComponent::ServerUpdateState (protegida), en el orden de su declaración. */
	struct FServerUpdateStateParams
	{
		float Steering = 0.f;
		float Throttle = 0.f;
		float Brake = 0.f;
		float Handbrake = 0.f;
		int32 Gear = 0;
		float Roll = 0.f;
		float Pitch = 0.f;
		float Yaw = 0.f;
	};

	/**
	 * Un fotograma de lo que le llega al servidor de una conductora cliente: el estado de sus entradas (ServerUpdateState) y su
	 * freno de mano (ServerSetHandbrake). El mundo no tiene red, así que el RPC se ejecuta en local, y sin controlador local
	 * Chaos solo procesa el estado replicado: el servidor no tiene la entrada cruda de la conductora.
	 */
	void SendRemoteDriverInput(ATN_KartBuggy& Kart, float Steer, bool bHandbrake)
	{
		UChaosWheeledVehicleMovementComponent* Move = Kart.GetWheeledMovement();
		Move->SetRequiresControllerForInputs(true);
		Move->SetSteeringInput(0.f);
		Move->SetThrottleInput(0.f);
		Move->SetBrakeInput(0.f);
		FServerUpdateStateParams Params;
		Params.Steering = Steer;
		Params.Throttle = 1.f;
		Params.Handbrake = bHandbrake ? 1.f : 0.f;
		Params.Gear = 1;
		Move->ProcessEvent(Move->FindFunctionChecked(TEXT("ServerUpdateState")), &Params);
		bool bHeld = bHandbrake;
		Kart.ProcessEvent(Kart.FindFunctionChecked(TEXT("ServerSetHandbrake")), &bHeld);
	}

	FString Describe(const TCHAR* Label, const FTurn& Run)
	{
		return FString::Printf(TEXT("%s: radio %.1f m a %.1f km/h, inclinación máxima %.0f°, %s%s"), Label, Run.RadiusM, Run.MeanKmh,
			Run.MaxTiltDeg, Run.bFlipped ? TEXT("VUELCA") : TEXT("no vuelca"), Run.bReached ? TEXT("") : TEXT(" (no llegó a la velocidad)"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartMeasureSpeedTest, "Tortunabo.Kart.Measure.SpeedAndAcceleration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNKartMeasureSpeedTest::RunTest(const FString& Parameters)
{
	using namespace TNKartMeasureTest;
	TNKartMeasureTest::FSprint Runs[2];
	const TSubclassOf<ATN_KartBuggy> Classes[2] = { ATN_RallyKartBuggy::StaticClass(), ATN_KartBuggy::StaticClass() };
	const TCHAR* const Labels[2] = { TEXT("Rally"), TEXT("Karts") };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FPhysicsWorld Test(TEXT("TNKartSpeedWorld"));
		ATN_KartBuggy* Kart = nullptr;
		if (!TestTrue(*FString::Printf(TEXT("%s: mundo y kart"), Labels[Index]), Prepare(Test, Classes[Index], Kart)))
		{
			return false;
		}
		TestTrue(*FString::Printf(TEXT("%s: conducción de los karts puesta solo en Karts"), Labels[Index]), Kart->UsesKartTuning() == (Index == 1));
		Runs[Index] = Sprint(Test, *Kart, 25.f);
		AddInfo(FString::Printf(TEXT("%s: 0-60 km/h %.2f s, 0-100 km/h %.2f s, punta %.1f km/h"), Labels[Index], Runs[Index].ZeroToSixty,
			Runs[Index].ZeroToHundred, Runs[Index].TopKmh));
	}
	const FSprint& Rally = Runs[0];
	const FSprint& Karts = Runs[1];
	TestTrue(*FString::Printf(TEXT("el kart del Rally no cambia: 0-60 en ~2 s (%.2f) y punta ~110 km/h (%.1f)"), Rally.ZeroToSixty, Rally.TopKmh),
		Rally.ZeroToSixty >= 1.7f && Rally.ZeroToSixty <= 2.2f && Rally.TopKmh >= 105.f && Rally.TopKmh <= 118.f);
	TestTrue(*FString::Printf(TEXT("la punta de Karts sube entre un 25 y un 40 %% (%.1f contra %.1f)"), Karts.TopKmh, Rally.TopKmh),
		Karts.TopKmh >= 1.25f * Rally.TopKmh && Karts.TopKmh <= 1.4f * Rally.TopKmh);
	// El 0-60 lo limita el agarre de salida (1,6 s son ya más de 1 g): la aceleración se mide en el 0-100.
	TestTrue(*FString::Printf(TEXT("el 0-60 de Karts es al menos un 10 %% más rápido (%.2f s contra %.2f s)"), Karts.ZeroToSixty, Rally.ZeroToSixty),
		Karts.ZeroToSixty > 0.f && Karts.ZeroToSixty <= 0.9f * Rally.ZeroToSixty);
	TestTrue(*FString::Printf(TEXT("el 0-100 de Karts es al menos un 20 %% más rápido (%.2f s contra %.2f s)"), Karts.ZeroToHundred, Rally.ZeroToHundred),
		Karts.ZeroToHundred > 0.f && Karts.ZeroToHundred <= 0.8f * Rally.ZeroToHundred);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartMeasureDriftTest, "Tortunabo.Kart.Measure.DriftBoost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNKartMeasureDriftTest::RunTest(const FString& Parameters)
{
	using namespace TNKartMeasureTest;
	// Derecha a fondo y 2 s de freno de mano a 80 km/h.
	const FDrift Rally = DriftRun(ATN_RallyKartBuggy::StaticClass(), 80.f, 2.f, 1.f);
	const FDrift Karts = DriftRun(ATN_KartBuggy::StaticClass(), 80.f, 2.f, 1.f);
	AddInfo(Describe(TEXT("Rally"), Rally));
	AddInfo(Describe(TEXT("Karts"), Karts));
	AddInfo(FString::Printf(TEXT("Karts, deriva(°)/velocidad(km/h) cada 0,25 s:%s"), *Karts.Trace));
	TestTrue(TEXT("Karts llega a la velocidad de entrada"), Karts.bReached);
	TestFalse(TEXT("Karts: sin trompo en el derrape"), Karts.bSpun);
	TestFalse(TEXT("Karts: sin vuelco"), Karts.bFlipped);
	TestTrue(*FString::Printf(TEXT("Karts: el derrape desliza de verdad (deriva máxima %.1f°)"), Karts.MaxSlipDeg), Karts.MaxSlipDeg >= 15.f);
	TestTrue(*FString::Printf(TEXT("Karts: no pierde casi toda la velocidad (mínima %.1f km/h)"), Karts.MinKmh), Karts.MinKmh >= 30.f);
	TestTrue(*FString::Printf(TEXT("Karts: al soltar el freno de mano da mini-turbo (%.2f s)"), Karts.BoostSeconds), Karts.BoostSeconds >= TNKart::DriftBoost1Seconds - 0.1f);
	TestTrue(*FString::Printf(TEXT("Karts: el turbo llega a empujar (fuerza %.2f)"), Karts.PeakBoostStrength), Karts.PeakBoostStrength >= 0.5f);
	TestEqual(TEXT("Rally: sin mini-turbo"), Rally.BoostSeconds, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartMeasureDriftServerTest, "Tortunabo.Kart.Measure.DriftServerDriver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNKartMeasureDriftServerTest::RunTest(const FString& Parameters)
{
	using namespace TNKartMeasureTest;
	// La dirección procesada por Chaos tiene que seguir llamándose así (la lee GetAppliedDriftSteering por reflexión).
	TestNotNull(TEXT("Chaos tiene la propiedad de la dirección procesada"),
		CastField<FFloatProperty>(UChaosVehicleMovementComponent::StaticClass()->FindPropertyByName(ATN_KartBuggy::AppliedSteeringProperty)));
	FPhysicsWorld Test(TEXT("TNKartDriftServerWorld"));
	ATN_KartBuggy* Kart = nullptr;
	if (!TestTrue(TEXT("mundo y kart"), Prepare(Test, ATN_KartBuggy::StaticClass(), Kart)) || !TestTrue(TEXT("llega a 80 km/h"), ReachSpeed(Test, *Kart, 80.f)))
	{
		return false;
	}
	constexpr float Steer = 0.8f;
	float MaxApplied = 0.f;
	// 2 s de derrape a la derecha como los manda una conductora cliente.
	for (int32 Step = 0; Step < 2 * StepsPerSecond; ++Step)
	{
		SendRemoteDriverInput(*Kart, Steer, true);
		Test.Step();
		MaxApplied = FMath::Max(MaxApplied, Kart->GetAppliedDriftSteering());
	}
	TestTrue(*FString::Printf(TEXT("el servidor tiene el giro que manda la conductora (%.2f de %.2f)"), MaxApplied, Steer),
		FMath::IsNearlyEqual(MaxApplied, Steer, 0.01f));
	TestTrue(TEXT("el servidor ve el freno de mano de la conductora"), Kart->IsHandbrakeHeld());
	float BoostSeconds = 0.f;
	for (int32 Step = 0; Step < StepsPerSecond / 5; ++Step)
	{
		SendRemoteDriverInput(*Kart, 0.f, false);
		Test.Step();
		BoostSeconds = FMath::Max(BoostSeconds, Kart->GetTimedBoostSecondsLeft());
	}
	AddInfo(FString::Printf(TEXT("derrape de 2 s con la entrada replicada: mini-turbo de %.2f s"), BoostSeconds));
	TestTrue(*FString::Printf(TEXT("el servidor da el mini-turbo al soltar el freno de mano (%.2f s)"), BoostSeconds),
		BoostSeconds >= TNKart::DriftBoost1Seconds - 0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartMeasureTurnTest, "Tortunabo.Kart.Measure.HighSpeedTurn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNKartMeasureTurnTest::RunTest(const FString& Parameters)
{
	using namespace TNKartMeasureTest;
	// El Rally a 100 km/h (casi su punta) como referencia; Karts a la misma y a 130.
	const FTurn Rally100 = TurnRun(ATN_RallyKartBuggy::StaticClass(), 100.f);
	const FTurn Karts100 = TurnRun(ATN_KartBuggy::StaticClass(), 100.f);
	const FTurn Karts130 = TurnRun(ATN_KartBuggy::StaticClass(), 130.f);
	AddInfo(Describe(TEXT("Rally a 100 km/h"), Rally100));
	AddInfo(Describe(TEXT("Karts a 100 km/h"), Karts100));
	AddInfo(Describe(TEXT("Karts a 130 km/h"), Karts130));
	TestTrue(TEXT("Karts llega a 100 km/h"), Karts100.bReached);
	TestTrue(TEXT("Karts llega a 130 km/h"), Karts130.bReached);
	TestFalse(TEXT("Karts a 100 km/h con el volante a tope: no vuelca"), Karts100.bFlipped);
	TestFalse(TEXT("Karts a 130 km/h con el volante a tope: no vuelca"), Karts130.bFlipped);
	TestTrue(*FString::Printf(TEXT("a 130 km/h con el volante a tope sigue girando (radio %.1f m, menos de 100)"), Karts130.RadiusM),
		Karts130.RadiusM > 0.f && Karts130.RadiusM < 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartMeasureSteerTest, "Tortunabo.Kart.Measure.SteerPrecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNKartMeasureSteerTest::RunTest(const FString& Parameters)
{
	using namespace TNKartMeasureTest;
	// Aceleración lateral según cuánto se gira el volante, a 50, 90 y 125 km/h: cuanto más progresiva, más precisa.
	for (const float Kmh : { 50.f, 90.f, 125.f })
	{
		FString Row;
		float Tenth = 0.f;
		float Full = 0.f;
		for (const float Stick : { 0.1f, 0.25f, 0.5f, 1.f })
		{
			const FSteerPoint Point = SteerPoint(ATN_KartBuggy::StaticClass(), Kmh, Stick);
			Row += FString::Printf(TEXT(" volante %.2f: %.2f g (%.0f km/h)%s;"), Stick, Point.LateralG, Point.Kmh, Point.bFlipped ? TEXT(" VUELCA") : TEXT(""));
			TestFalse(*FString::Printf(TEXT("a %.0f km/h con el volante a %.2f no vuelca"), Kmh, Stick), Point.bFlipped);
			Tenth = Stick == 0.1f ? Point.LateralG : Tenth;
			Full = Stick == 1.f ? Point.LateralG : Full;
		}
		AddInfo(FString::Printf(TEXT("Karts a %.0f km/h:%s"), Kmh, *Row));
		if (Kmh <= 100.f)
		{
			// De referencia, el kart del Rally (sin la conducción de los karts), que llega hasta ~110 km/h.
			FString RallyRow;
			for (const float Stick : { 0.1f, 0.25f, 0.5f, 1.f })
			{
				const FSteerPoint Point = SteerPoint(ATN_RallyKartBuggy::StaticClass(), Kmh, Stick);
				RallyRow += FString::Printf(TEXT(" volante %.2f: %.2f g (%.0f km/h)%s;"), Stick, Point.LateralG, Point.Kmh, Point.bFlipped ? TEXT(" VUELCA") : TEXT(""));
			}
			AddInfo(FString::Printf(TEXT("Rally a %.0f km/h:%s"), Kmh, *RallyRow));
		}
		// Progresivo: un 10 %% del volante da menos de 0,6 g (sin la conducción de los karts, a 90 km/h daba 1,3 g) y el volante a tope
		// sigue dando 1,4 g o más para las curvas cerradas.
		TestTrue(*FString::Printf(TEXT("a %.0f km/h un 10 %% del volante da menos de 0,6 g (%.2f g)"), Kmh, Tenth), Tenth < 0.6f);
		TestTrue(*FString::Printf(TEXT("a %.0f km/h el volante a tope da al menos 1,4 g (%.2f g)"), Kmh, Full), Full >= 1.4f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
