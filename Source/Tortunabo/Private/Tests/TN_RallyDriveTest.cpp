// Conducción del buggy del Rally (TN_BuggyMath.h): curva de dirección (#288), curva de par (#294), control de estabilidad,
// turbo y cámara de la conductora (#298). Lógica pura, sin mundo. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Drive; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Vehicles/TN_BuggyData.h"
#include "Vehicles/TN_BuggyMath.h"
#include "Vehicles/TN_RallyTurretLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDriveSteerCurveTest,
	"Tortunabo.Rally.Drive.SteerCurve",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDriveSteerCurveTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	const float Top = TNRallyTurret::BuggyTopSpeedCms;
	TestEqual(TEXT("parado gira 40 grados"), MaxSteerAngleDeg(0.f), 40.f);
	TestEqual(TEXT("a punta gira 12 grados"), MaxSteerAngleDeg(Top), 12.f, 0.01f);
	TestEqual(TEXT("con turbo (punta +15 %) sigue en 12 grados"), MaxSteerAngleDeg(Top * 1.15f), 12.f, 0.01f);
	TestEqual(TEXT("marcha atrás vale lo mismo que hacia delante"), MaxSteerAngleDeg(-1000.f), MaxSteerAngleDeg(1000.f));
	const float At80Kmh = MaxSteerAngleDeg(2222.f);
	TestTrue(TEXT("a 80 km/h gira entre 14 y 16 grados"), At80Kmh >= 14.f && At80Kmh <= 16.f);

	float Previous = MaxSteerAngleDeg(0.f);
	bool bMonotonic = true;
	for (float Speed = 50.f; Speed <= Top * 1.2f; Speed += 50.f)
	{
		const float Angle = MaxSteerAngleDeg(Speed);
		bMonotonic &= Angle <= Previous + KINDA_SMALL_NUMBER;
		Previous = Angle;
	}
	TestTrue(TEXT("el ángulo nunca sube con la velocidad"), bMonotonic);

	const FCurveKey Keys[] = { { 0.f, 2.f }, { 10.f, 4.f } };
	TestEqual(TEXT("EvalLinearKeys interpola"), EvalLinearKeys(Keys, 5.f), 3.f);
	TestEqual(TEXT("EvalLinearKeys mantiene el extremo inferior"), EvalLinearKeys(Keys, -5.f), 2.f);
	TestEqual(TEXT("EvalLinearKeys mantiene el extremo superior"), EvalLinearKeys(Keys, 50.f), 4.f);
	TestEqual(TEXT("EvalLinearKeys sin claves da 0"), EvalLinearKeys(TConstArrayView<FCurveKey>(), 1.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDriveTorqueCurveTest,
	"Tortunabo.Rally.Drive.TorqueCurve",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDriveTorqueCurveTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	const float MaxTorque = GetDefault<UTN_BuggyData>()->MaxTorque;
	TestEqual(TEXT("el par máximo por defecto es 1,5 veces el antiguo (0-60 en ~2 s en vez de ~3 s)"), MaxTorque, 1.5f * LegacyMaxTorque);

	const TArray<FCurveKey> Keys = TorqueCurveKeys(MaxTorque);
	float CurveMax = 0.f;
	for (const FCurveKey& Key : Keys)
	{
		CurveMax = FMath::Max(CurveMax, Key.Y);
	}
	TestEqual(TEXT("el máximo de la curva es 1 (Chaos la normaliza por su máximo)"), CurveMax, 1.f);

	// La punta la fija el par desde el 80 % de MaxRPM: igual que antes, en N·m.
	for (float Rpm = 0.8f; Rpm <= 1.f + KINDA_SMALL_NUMBER; Rpm += 0.02f)
	{
		const float NewTorque = EvalLinearKeys(Keys, Rpm) * MaxTorque;
		const float OldTorque = EvalLinearKeys(LegacyTorqueCurveKeys(), Rpm) * LegacyMaxTorque;
		TestEqual(*FString::Printf(TEXT("al %.0f %% de MaxRPM el par absoluto no cambia"), Rpm * 100.f), NewTorque, OldTorque, 0.5f);
	}
	// Arranque hasta 60 km/h: del ralentí de Chaos (35 %) al 52 % de MaxRPM, al menos 1,45 veces el par antiguo.
	for (float Rpm = 0.35f; Rpm <= 0.52f; Rpm += 0.01f)
	{
		const float NewTorque = EvalLinearKeys(Keys, Rpm) * MaxTorque;
		const float OldTorque = EvalLinearKeys(LegacyTorqueCurveKeys(), Rpm) * LegacyMaxTorque;
		TestTrue(*FString::Printf(TEXT("al %.0f %% de MaxRPM hay 1,45 veces el par antiguo"), Rpm * 100.f), NewTorque >= 1.45f * OldTorque);
	}
	TestTrue(TEXT("en baja (10 %) hay más par que antes"),
		EvalLinearKeys(Keys, 0.1f) * MaxTorque > EvalLinearKeys(LegacyTorqueCurveKeys(), 0.1f) * LegacyMaxTorque);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDriveStabilityTest,
	"Tortunabo.Rally.Drive.Stability",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDriveStabilityTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	const FStabilityTuning Tuning;
	const float Fast = 3000.f;
	TestEqual(TEXT("el giro normal (deriva pequeña) no se toca"), StabilityYawAccel(4.f, 1.f, Fast, false, false, Tuning), 0.f);
	TestEqual(TEXT("con freno de mano no corrige: el derrape largo es suyo"), StabilityYawAccel(30.f, 0.f, Fast, true, false, Tuning), 0.f);
	TestEqual(TEXT("en el aire no corrige"), StabilityYawAccel(30.f, 0.f, Fast, false, true, Tuning), 0.f);
	TestEqual(TEXT("despacio no corrige"), StabilityYawAccel(30.f, 0.f, 200.f, false, false, Tuning), 0.f);
	TestEqual(TEXT("en un trompo (más de 90 grados) no corrige"), StabilityYawAccel(120.f, 0.f, Fast, false, false, Tuning), 0.f);

	// Velocidad a la derecha del morro: el morro gira a la derecha (positivo) hacia ella, y al revés.
	TestTrue(TEXT("deriva a la derecha: guiñada a la derecha"), StabilityYawAccel(15.f, 0.f, Fast, false, false, Tuning) > 0.f);
	TestTrue(TEXT("deriva a la izquierda: guiñada a la izquierda"), StabilityYawAccel(-15.f, 0.f, Fast, false, false, Tuning) < 0.f);
	// Sobreviraje en curva a la derecha: el morro gira a la derecha y la velocidad queda a su izquierda.
	const float Spring = StabilityYawAccel(-15.f, 0.f, Fast, false, false, Tuning);
	const float Damped = StabilityYawAccel(-15.f, 1.f, Fast, false, false, Tuning);
	TestTrue(TEXT("la guiñada que agranda la deriva se frena más"), Damped < Spring);
	const float Recovering = StabilityYawAccel(-15.f, -1.f, Fast, false, false, Tuning);
	TestEqual(TEXT("la guiñada que ya corrige no se frena"), Recovering, Spring);
	TestEqual(TEXT("con tope"), StabilityYawAccel(-89.f, 10.f, Fast, false, false, Tuning), -Tuning.MaxAccel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDriveBoostTest,
	"Tortunabo.Rally.Drive.Boost",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDriveBoostTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	const FBoostTuning Tuning;
	FBoostInput Want;
	Want.bWantBoost = true;
	Want.SpeedCms = 2000.f;

	float Charge = 1.f;
	int32 ActiveSteps = 0;
	for (int32 Step = 0; Step < 40; ++Step)
	{
		const FBoostStep Out = AdvanceBoost(Charge, Want, 0.1f, Tuning);
		ActiveSteps += Out.bActive ? 1 : 0;
		Charge = Out.Charge01;
	}
	TestEqual(TEXT("una barra llena dura 3 s (30 pasos de 0,1 s)"), ActiveSteps, 30);
	TestEqual(TEXT("y se vacía"), Charge, 0.f);
	TestFalse(TEXT("vacío no empuja"), AdvanceBoost(0.f, Want, 0.1f, Tuning).bActive);

	FBoostInput Locked = Want;
	Locked.bEngineLocked = true;
	TestFalse(TEXT("con el motor cortado no empuja"), AdvanceBoost(1.f, Locked, 0.1f, Tuning).bActive);
	TestEqual(TEXT("ni gasta"), AdvanceBoost(1.f, Locked, 0.1f, Tuning).Charge01, 1.f);

	FBoostInput Drift;
	Drift.bHandbrake = true;
	Drift.SlipDeg = 25.f;
	Drift.SpeedCms = 1500.f;
	TestEqual(TEXT("derrapar con freno de mano recarga"), BoostRechargeRate(Drift, Tuning), Tuning.DriftRechargePerSecond);
	TestEqual(TEXT("en 1 s de derrape sube 0,3"), AdvanceBoost(0.f, Drift, 1.f, Tuning).Charge01, 0.3f, 0.001f);
	FBoostInput SmallSlip = Drift;
	SmallSlip.SlipDeg = 15.f;
	TestEqual(TEXT("con menos de 20 grados de deriva no recarga"), BoostRechargeRate(SmallSlip, Tuning), 0.f);
	FBoostInput NoHandbrake = Drift;
	NoHandbrake.bHandbrake = false;
	TestEqual(TEXT("sin freno de mano no recarga"), BoostRechargeRate(NoHandbrake, Tuning), 0.f);
	FBoostInput DriftWhileBoosting = Drift;
	DriftWhileBoosting.bWantBoost = true;
	TestTrue(TEXT("con el turbo pisado no recarga: gasta"), AdvanceBoost(0.5f, DriftWhileBoosting, 1.f, Tuning).Charge01 < 0.5f);

	FBoostInput Air;
	Air.bAirborne = true;
	Air.SpeedCms = 1500.f;
	TestEqual(TEXT("saltar una rampa recarga"), BoostRechargeRate(Air, Tuning), Tuning.AirRechargePerSecond);
	Air.SpeedCms = 300.f;
	TestEqual(TEXT("caer parado (volcado, reaparición) no recarga"), BoostRechargeRate(Air, Tuning), 0.f);
	TestEqual(TEXT("la carga no pasa de 1"), AdvanceBoost(0.95f, Drift, 1.f, Tuning).Charge01, 1.f);

	const float Top = TNRallyTurret::BuggyTopSpeedCms * 1.15f;
	TestEqual(TEXT("empuja lejos de la punta del turbo"), BoostPushAccel(1000.f, Top, 300.f, 200.f), 300.f);
	TestEqual(TEXT("a mitad de la banda, la mitad"), BoostPushAccel(Top - 100.f, Top, 300.f, 200.f), 150.f, 0.01f);
	TestEqual(TEXT("en la punta del turbo ya no empuja"), BoostPushAccel(Top, Top, 300.f, 200.f), 0.f);
	TestEqual(TEXT("marcha atrás no empuja"), BoostPushAccel(-500.f, Top, 300.f, 200.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDriveCameraTest,
	"Tortunabo.Rally.Drive.Camera",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDriveCameraTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	const FDriverCameraTuning& Tuning = DefaultDriverCamera();
	auto Run = [&Tuning](FDriverCameraState State, FDriverCameraInput In, int32 Frames)
	{
		In.Dt = 1.f / 60.f;
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			State = AdvanceDriverCamera(State, In, Tuning);
			In.AddedTrauma = 0.f;
		}
		return State;
	};

	FDriverCameraInput Turn;
	Turn.ForwardSpeedCms = 2500.f;
	Turn.YawRateDegPerSec = 60.f;
	const FDriverCameraState Turning = Run(FDriverCameraState(), Turn, 120);
	TestTrue(TEXT("en curva a la derecha se desplaza a la derecha"), Turning.LateralCm > 50.f);
	TestTrue(TEXT("sin pasar del tope"), Turning.LateralCm <= Tuning.MaxLeadCm);
	const FDriverCameraState FirstFrame = Run(FDriverCameraState(), Turn, 1);
	TestTrue(TEXT("suave: el primer fotograma se mueve menos de 10 cm"), FirstFrame.LateralCm < 10.f);

	FDriverCameraInput Cruise;
	Cruise.ForwardSpeedCms = 2500.f;
	FDriverCameraState Braking = Run(FDriverCameraState(), Cruise, 30);
	for (int32 Frame = 0; Frame < 45; ++Frame)
	{
		Cruise.ForwardSpeedCms -= 2500.f / 60.f;
		Braking = Run(Braking, Cruise, 1);
	}
	TestTrue(TEXT("al frenar fuerte se acerca"), Braking.ArmDeltaCm < -20.f);
	TestTrue(TEXT("y baja"), Braking.HeightDeltaCm < -5.f);

	FDriverCameraInput Air;
	Air.ForwardSpeedCms = 2000.f;
	Air.bAirborne = true;
	Air.VerticalSpeedCms = -Tuning.LandingFullFallCms;
	FDriverCameraState Landing = Run(FDriverCameraState(), Air, 30);
	TestEqual(TEXT("en el aire no tiembla"), Landing.Trauma, 0.f);
	Air.bAirborne = false;
	Air.VerticalSpeedCms = 0.f;
	Landing = Run(Landing, Air, 1);
	TestTrue(TEXT("al aterrizar fuerte tiembla"), Landing.Trauma > 0.6f);
	TestEqual(TEXT("y se calma en 1 s"), Run(Landing, Air, 60).Trauma, 0.f);
	TestEqual(TEXT("un bache corto (menos de 0,2 s en el aire) no hace temblar"), LandingTrauma(2000.f, 0.1f, Tuning), 0.f);

	FDriverCameraInput Boost;
	Boost.ForwardSpeedCms = 3000.f;
	Boost.bBoosting = true;
	const FDriverCameraState Boosting = Run(FDriverCameraState(), Boost, 120);
	TestEqual(TEXT("el turbo abre el FOV 8 grados"), Boosting.BoostFovDeg, Tuning.BoostFovDeg, 0.1f);
	TestTrue(TEXT("y tiembla un poco"), Boosting.Trauma >= Tuning.BoostTrauma);

	FDriverCameraInput Drift;
	Drift.ForwardSpeedCms = 1500.f;
	Drift.SlipDeg = 40.f;
	const FDriverCameraState Drifting = Run(FDriverCameraState(), Drift, 120);
	TestTrue(TEXT("en derrape se inclina, poco"), Drifting.RollDeg > 1.f && Drifting.RollDeg <= Tuning.MaxRollDeg);

	TestTrue(TEXT("sin sacudida no hay desplazamiento"), ShakeOffset(0.f, 1.23f, Tuning.MaxShakeCm).IsNearlyZero());
	bool bBounded = true;
	for (float Time = 0.f; Time < 2.f; Time += 0.013f)
	{
		bBounded &= ShakeOffset(1.f, Time, Tuning.MaxShakeCm).GetAbsMax() <= Tuning.MaxShakeCm + KINDA_SMALL_NUMBER;
		bBounded &= ShakeRotation(1.f, Time, Tuning.MaxShakeDeg).GetAbsMax() <= Tuning.MaxShakeDeg + KINDA_SMALL_NUMBER;
	}
	TestTrue(TEXT("la sacudida no pasa de su amplitud"), bBounded);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
