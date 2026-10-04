// Conducción del buggy del Rally (TN_BuggyMath.h): curva de dirección (#288), curva de par (#294), control de estabilidad,
// turbo y cámara de la conductora (#298). Lógica pura, sin mundo. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Drive; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Vehicles/TN_Buggy.h"
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
	const UTN_BuggyData* Tuning = GetDefault<UTN_BuggyData>();
	// #606: el ángulo de un buggy real (35-40 grados), el mismo a cualquier velocidad (antes, 40 parado y 12 a punta).
	TestTrue(TEXT("el ángulo por defecto está entre 35 y 40 grados"), DefaultSteerAngleDeg >= 35.f && DefaultSteerAngleDeg <= 40.f);
	TestEqual(TEXT("el asset de ajuste trae el mismo ángulo"), Tuning->MaxSteerAngleDeg, DefaultSteerAngleDeg);
	// El piloto IA acota la dirección con el ángulo del propio buggy, no con una copia suya (revisión de #606).
	TestEqual(TEXT("el buggy da al piloto IA el ángulo de su ajuste"), GetDefault<ATN_Buggy>()->GetMaxSteerAngleDeg(), Tuning->MaxSteerAngleDeg);
	TestEqual(TEXT("parado gira el ángulo entero"), MaxSteerAngleDeg(0.f), DefaultSteerAngleDeg);
	TestEqual(TEXT("a 80 km/h gira lo mismo"), MaxSteerAngleDeg(2222.f), DefaultSteerAngleDeg, 0.01f);
	TestEqual(TEXT("a punta gira lo mismo"), MaxSteerAngleDeg(Top), DefaultSteerAngleDeg, 0.01f);
	TestEqual(TEXT("con turbo (punta +15 %) gira lo mismo"), MaxSteerAngleDeg(Top * 1.15f), DefaultSteerAngleDeg, 0.01f);
	TestEqual(TEXT("marcha atrás vale lo mismo que hacia delante"), MaxSteerAngleDeg(-1000.f), MaxSteerAngleDeg(1000.f));
	TestEqual(TEXT("el ángulo base se respeta"), MaxSteerAngleDeg(1500.f, 30.f), 30.f, 0.01f);

	// Volante más rápido y lineal que el de serie de Chaos (2,5 por segundo y cuadrático).
	TestTrue(TEXT("el volante llega a tope en menos de 0,2 s"), Tuning->SteerRiseRate >= 5.f);
	TestTrue(TEXT("respuesta lineal"), Tuning->bLinearSteerResponse);

	// Radio de giro: objetivo < 6 m a 20 km/h (buggy_measure lo mide en la pista con TN.Rally.MeasureTurn).
	const float Radius = KinematicTurnRadiusCm(WheelbaseCm, DefaultSteerAngleDeg, SteerAngleRatio);
	TestTrue(*FString::Printf(TEXT("radio cinemático %.0f cm, menos de 6 m"), Radius), Radius > 0.f && Radius < 600.f);
	// La curva antigua daba 35,1 grados a 20 km/h (556 cm/s) y 15 a 80 km/h.
	TestTrue(TEXT("antes, a 20 km/h, el radio era mayor"), KinematicTurnRadiusCm(WheelbaseCm, 35.1f, SteerAngleRatio) > Radius);
	TestTrue(TEXT("y a 80 km/h, mucho mayor"), KinematicTurnRadiusCm(WheelbaseCm, 15.f, SteerAngleRatio) > 2.f * Radius);
	TestEqual(TEXT("radio medido: v / guiñada"), TurnRadiusFromYawRate(556.f, 1.f), 556.f, 0.01f);
	TestEqual(TEXT("sin guiñada no hay radio"), TurnRadiusFromYawRate(556.f, 0.f), 0.f);

	// Piloto IA: a 20 km/h gira a tope; a 90 km/h, solo lo que no pasa de 0,9 g de lateral (no vuelca en la curva).
	const float LateralCap = 0.9f * 981.f;
	TestEqual(TEXT("despacio, toda la dirección"), SafeSteerFraction(556.f, DefaultSteerAngleDeg, WheelbaseCm, LateralCap), 1.f);
	const float Fast = SafeSteerFraction(2500.f, DefaultSteerAngleDeg, WheelbaseCm, LateralCap);
	TestTrue(TEXT("a 90 km/h, menos de un cuarto de la dirección"), Fast > 0.f && Fast < 0.25f);
	const float FastDeg = Fast * DefaultSteerAngleDeg;
	const float FastLateral = FMath::Square(2500.f) / (WheelbaseCm / FMath::Tan(FMath::DegreesToRadians(FastDeg)));
	TestTrue(TEXT("y esa dirección no pasa de 0,9 g"), FastLateral <= LateralCap * 1.01f);
	TestEqual(TEXT("sin velocidad, sin límite"), SafeSteerFraction(0.f, DefaultSteerAngleDeg, WheelbaseCm, LateralCap), 1.f);

	const FCurveKey Keys[] = { { 0.f, 2.f }, { 10.f, 4.f } };
	TestEqual(TEXT("EvalLinearKeys interpola"), EvalLinearKeys(Keys, 5.f), 3.f);
	TestEqual(TEXT("EvalLinearKeys mantiene el extremo inferior"), EvalLinearKeys(Keys, -5.f), 2.f);
	TestEqual(TEXT("EvalLinearKeys mantiene el extremo superior"), EvalLinearKeys(Keys, 50.f), 4.f);
	TestEqual(TEXT("EvalLinearKeys sin claves da 0"), EvalLinearKeys(TConstArrayView<FCurveKey>(), 1.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDriveSteerAtSpeedTest,
	"Tortunabo.Rally.Drive.SteerAtSpeed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDriveSteerAtSpeedTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	const UTN_BuggyData* Tuning = GetDefault<UTN_BuggyData>();
	const float Base = Tuning->MaxSteerAngleDeg;
	const float Top = TNRallyTurret::BuggyTopSpeedCms * Tuning->BoostTopSpeedMultiplier;
	// #606: con el volante a tope y la deriva de un giro con agarre (hasta 10 grados a cualquier lado), la rueda gira el
	// ángulo entero a cualquier velocidad, con turbo incluido.
	const float GripSlips[] = { -10.f, -6.f, -3.f, 0.f, 3.f, 6.f, 10.f };
	float MinDeg = TNumericLimits<float>::Max();
	for (float Speed = 0.f; Speed <= Top; Speed += 250.f)
	{
		for (const float Slip : GripSlips)
		{
			MinDeg = FMath::Min(MinDeg, EffectiveSteerDeg(1.f, Speed, Slip, Base, Tuning->CounterSteerAssist,
				Tuning->MaxAssistAngleDeg, Tuning->CounterSteerStartSlipDeg));
		}
	}
	TestTrue(*FString::Printf(TEXT("la dirección efectiva no cae con la velocidad (mínimo %.2f de %.2f grados)"), MinDeg, Base),
		MinDeg >= Base - 0.01f);
	TestEqual(TEXT("a punta, la izquierda gira lo mismo que la derecha"),
		EffectiveSteerDeg(-1.f, Top, 8.f, Base, Tuning->CounterSteerAssist, Tuning->MaxAssistAngleDeg, Tuning->CounterSteerStartSlipDeg),
		-Base, 0.01f);
	// Caso negativo: con el contravolante desde 0 grados (antes de #606), 10 grados de deriva quitaban más de un 10 %.
	TestTrue(TEXT("sin zona muerta, la deriva de un giro rápido restaba dirección"),
		EffectiveSteerDeg(1.f, Top, -10.f, Base, Tuning->CounterSteerAssist, Tuning->MaxAssistAngleDeg, 0.f) < 0.9f * Base);
	// El derrape de verdad sigue con contravolante.
	TestTrue(TEXT("con 30 grados de deriva hay contravolante"),
		EffectiveSteerDeg(1.f, Top, -30.f, Base, Tuning->CounterSteerAssist, Tuning->MaxAssistAngleDeg, Tuning->CounterSteerStartSlipDeg) < Base);
	TestEqual(TEXT("contravolante: lo de antes con zona muerta 0"), AssistSteer(0.f, 17.5f, 0.8f, 35.f, 0.f), 0.4f, 0.001f);
	TestEqual(TEXT("contravolante: en la zona muerta, nada"), AssistSteer(0.5f, -11.f, 0.8f, 35.f, 12.f), 0.5f, 0.001f);
	TestEqual(TEXT("contravolante: al llegar a su máximo, entero"), AssistSteer(0.f, -35.f, 0.8f, 35.f, 12.f), -0.8f, 0.001f);

	// El control de estabilidad del asset no frena la guiñada de una curva rápida con agarre (antes empezaba a 6 grados).
	FStabilityTuning Stability;
	Stability.StartSlipDeg = Tuning->StabilityStartSlipDeg;
	Stability.Stiffness = Tuning->StabilityStiffness;
	Stability.Damping = Tuning->StabilityDamping;
	Stability.MaxAccel = Tuning->StabilityMaxAccel;
	TestEqual(TEXT("estabilidad: curva rápida con 15 grados de deriva, sin freno de guiñada"),
		StabilityYawAccel(-15.f, 1.5f, Top, false, false, Stability), 0.f);
	TestTrue(TEXT("estabilidad: un trompo (40 grados) sí se ataja"), StabilityYawAccel(-40.f, 1.5f, Top, false, false, Stability) < 0.f);

	// Antivuelco del asset: despacio sujeta el alabeo en el suelo; a mucha velocidad lo deja libre (vuelca) y en el aire, no.
	FAntiRollTuning AntiRoll;
	AntiRoll.GroundFreeRollDeg = Tuning->AntiRollGroundFreeRollDeg;
	AntiRoll.GroundFreePitchDeg = Tuning->AntiRollGroundFreePitchDeg;
	AntiRoll.Stiffness = Tuning->AntiRollStiffness;
	AntiRoll.Damping = Tuning->AntiRollDamping;
	AntiRoll.MaxAccel = Tuning->AntiRollMaxAccel;
	AntiRoll.GroundRollFullSpeedCms = Tuning->AntiRollGroundRollFullSpeedCms;
	AntiRoll.GroundRollZeroSpeedCms = Tuning->AntiRollGroundRollZeroSpeedCms;
	TestEqual(TEXT("antivuelco: parado, entero"), GroundRollHelp(0.f, AntiRoll), 1.f);
	TestEqual(TEXT("antivuelco: a 45 km/h, entero"), GroundRollHelp(1250.f, AntiRoll), 1.f);
	TestEqual(TEXT("antivuelco: a 70 km/h, nada"), GroundRollHelp(1950.f, AntiRoll), 0.f);
	const float Mid = GroundRollHelp(1600.f, AntiRoll);
	TestTrue(TEXT("antivuelco: entre medias, a medias"), Mid > 0.f && Mid < 1.f);
	TestTrue(TEXT("antivuelco: marcha atrás cuenta igual"), FMath::IsNearlyEqual(GroundRollHelp(-1600.f, AntiRoll), Mid));

	const FVector Forward = FVector::ForwardVector;
	const FVector Rolled = FQuat(Forward, FMath::DegreesToRadians(45.0)).RotateVector(FVector::UpVector);
	const FVector RollRate = Forward * 2.0;
	TestFalse(TEXT("45 grados de alabeo a 20 km/h en el suelo: lo sujeta"),
		AntiRollAccel(Forward, Rolled, RollRate, false, AntiRoll, 556.f).IsNearlyZero());
	TestTrue(TEXT("45 grados de alabeo a 90 km/h en el suelo: lo deja volcar"),
		AntiRollAccel(Forward, Rolled, RollRate, false, AntiRoll, 2500.f).IsNearlyZero());
	TestFalse(TEXT("el mismo alabeo a 90 km/h en el aire: lo corrige"),
		AntiRollAccel(Forward, Rolled, RollRate, true, AntiRoll, 2500.f).IsNearlyZero());
	const FVector Pitched = FQuat(FVector::RightVector, FMath::DegreesToRadians(50.0)).RotateVector(FVector::UpVector);
	TestFalse(TEXT("el cabeceo se corrige también a mucha velocidad"),
		AntiRollAccel(Forward, Pitched, FVector::ZeroVector, false, AntiRoll, 2500.f).IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDriveTorqueCurveTest,
	"Tortunabo.Rally.Drive.TorqueCurve",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDriveTorqueCurveTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	const float MaxTorque = GetDefault<UTN_BuggyData>()->MaxTorque;
	// El 0-60 lo mide Tortunabo.Rally.Measure.ZeroToSixtyFlat con física; aquí, que el arranque tiene más par que antes.
	TestTrue(TEXT("el par máximo por defecto es al menos 1,5 veces el antiguo"), MaxTorque >= 1.5f * LegacyMaxTorque);
	const float RearShare = GetDefault<UTN_BuggyData>()->DriveRearShare;
	TestTrue(TEXT("tracción total con más par detrás (el arranque solo con la trasera patinaba)"), RearShare > 0.5f && RearShare < 1.f);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDriveBoostRampTest,
	"Tortunabo.Rally.Drive.BoostRamp",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDriveBoostRampTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	// #630: el turbo crece poco a poco. Se simula a 60 Hz con el ajuste de serie del buggy (lo mismo que hace
	// ATN_Buggy::UpdateBoostRamp) y se mide la aceleración que manda: el empuje y el par extra, por la fuerza del turbo.
	const UTN_BuggyData* Data = GetDefault<UTN_BuggyData>();
	FBoostRampTuning Ramp;
	Ramp.UpSeconds = Data->BoostRampUpSeconds;
	Ramp.DownSeconds = Data->BoostRampDownSeconds;
	Ramp.Exponent = Data->BoostRampExponent;
	constexpr float Dt = 1.f / 60.f;
	const float Top = TNRallyTurret::BuggyTopSpeedCms * Data->BoostTopSpeedMultiplier;
	const float FullPush = BoostPushAccel(1500.f, Top, Data->BoostPushAccel, Data->BoostPushFadeBandCms);
	// Aceleración del turbo tras Seconds pisándolo (a 54 km/h, lejos de la punta): empuje más el par extra, en cm/s².
	const auto AccelAfter = [&](float Seconds, float& OutStrength)
	{
		float Progress = 0.f;
		for (float Time = 0.f; Time < Seconds - 0.5f * Dt; Time += Dt)
		{
			Progress = AdvanceBoostRamp(Progress, true, false, Dt, Ramp);
		}
		OutStrength = Data->EvaluateBoostRamp(Progress);
		return OutStrength * FullPush + (BoostTorqueScale(OutStrength, Data->BoostTorqueMultiplier) - 1.f) * FullPush;
	};
	float QuarterStrength = 0.f;
	float SecondStrength = 0.f;
	const float AtQuarter = AccelAfter(0.25f, QuarterStrength);
	const float AtSecond = AccelAfter(1.f, SecondStrength);
	AddInfo(FString::Printf(TEXT("aceleración del turbo: %.0f cm/s² a 0,25 s y %.0f cm/s² a 1 s"), AtQuarter, AtSecond));
	TestTrue(TEXT("a 0,25 s ya empuja algo"), AtQuarter > 0.f);
	TestTrue(TEXT("la aceleración a 0,25 s es menor que a 1 s"), AtQuarter < AtSecond);
	TestTrue(TEXT("a 0,25 s, menos de la mitad"), QuarterStrength < 0.5f);
	TestEqual(TEXT("a 1 s, el empuje completo"), SecondStrength, 1.f, 0.001f);

	bool bMonotonic = true;
	float Progress = 0.f;
	float Previous = 0.f;
	for (int32 Step = 0; Step < 90; ++Step)
	{
		Progress = AdvanceBoostRamp(Progress, true, false, Dt, Ramp);
		const float Strength = Data->EvaluateBoostRamp(Progress);
		bMonotonic &= Strength >= Previous;
		Previous = Strength;
	}
	TestTrue(TEXT("sube sin saltos hacia atrás"), bMonotonic);
	const float AfterRelease = Data->EvaluateBoostRamp(AdvanceBoostRamp(1.f, false, false, 0.1f, Ramp));
	TestTrue(TEXT("al soltar baja suave, no de golpe"), AfterRelease > 0.f && AfterRelease < 1.f);
	TestEqual(TEXT("y se apaga del todo en su tiempo"), AdvanceBoostRamp(1.f, false, false, Ramp.DownSeconds + Dt, Ramp), 0.f);
	TestEqual(TEXT("con el motor cortado cae a 0 al momento"), AdvanceBoostRamp(1.f, true, true, Dt, Ramp), 0.f);
	TestEqual(TEXT("sin turbo, el par de siempre"), BoostTorqueScale(0.f, Data->BoostTorqueMultiplier), 1.f);
	TestEqual(TEXT("a tope, el par del turbo"), BoostTorqueScale(1.f, Data->BoostTorqueMultiplier), Data->BoostTorqueMultiplier);

	// Curva propia del asset: manda sobre la potencia.
	UTN_BuggyData* Custom = NewObject<UTN_BuggyData>(GetTransientPackage());
	Custom->BoostRampCurve.GetRichCurve()->AddKey(0.f, 0.f);
	Custom->BoostRampCurve.GetRichCurve()->AddKey(1.f, 1.f);
	TestEqual(TEXT("con curva propia, la curva (recta)"), Custom->EvaluateBoostRamp(0.5f), 0.5f, 0.01f);
	TestEqual(TEXT("sin curva, la potencia"), Data->EvaluateBoostRamp(0.5f), BoostRampStrength(0.5f, Data->BoostRampExponent), 0.001f);

	// La cámara acompaña la subida: menos FOV con media fuerza.
	const FDriverCameraTuning& Camera = DefaultDriverCamera();
	FDriverCameraInput Half;
	Half.Dt = Dt;
	Half.ForwardSpeedCms = 3000.f;
	Half.bBoosting = true;
	Half.BoostStrength01 = 0.5f;
	FDriverCameraState State;
	for (int32 Step = 0; Step < 240; ++Step)
	{
		State = AdvanceDriverCamera(State, Half, Camera);
	}
	TestEqual(TEXT("con media fuerza, medio FOV extra"), State.BoostFovDeg, 0.5f * Camera.BoostFovDeg, 0.1f);
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

namespace TNRallyDriveTestHelpers
{
	/**
	 * Buggy apoyado en una rampa de SlopeDeg grados durante Seconds, a 60 Hz: la gravedad a lo largo de la rampa lo empuja
	 * cuesta abajo (el suelo anula la normal) y, con bHold, TNBuggy::GridHoldVelocity corrige cada fotograma como
	 * ATN_Buggy::HoldOnGrid. Devuelve cuánto se ha desplazado (cm).
	 */
	double SlideOnRamp(double SlopeDeg, double Seconds, bool bHold, const TNBuggy::FGridHoldTuning& Tuning)
	{
		const double Slope = FMath::DegreesToRadians(SlopeDeg);
		// Rampa que baja hacia +X: la normal se inclina hacia +X.
		const FVector Up(FMath::Sin(Slope), 0.0, FMath::Cos(Slope));
		const FVector Gravity(0.0, 0.0, -981.0);
		const FVector AlongRamp = Gravity - Up * (Gravity | Up);
		const double Dt = 1.0 / 60.0;
		const FVector Anchor = FVector::ZeroVector;
		FVector Location = Anchor;
		FVector Velocity = FVector::ZeroVector;
		for (int32 Step = 0; Step < FMath::RoundToInt32(Seconds / Dt); ++Step)
		{
			if (bHold)
			{
				Velocity = TNBuggy::GridHoldVelocity(Velocity, Up, Location - Anchor, Tuning);
			}
			Velocity += AlongRamp * Dt;
			Location += Velocity * Dt;
		}
		return FVector::Dist(Location, Anchor);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDriveGridHoldTest,
	"Tortunabo.Rally.Drive.GridHoldOnRamp",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDriveGridHoldTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	// #611: en la espera y la cuenta atrás, el buggy no rueda cuesta abajo (< 5 cm en 5 s en una rampa de 15 grados).
	FGridHoldTuning Tuning;
	Tuning.PositionGain = GetDefault<UTN_BuggyData>()->GridHoldGain;
	const double Free = TNRallyDriveTestHelpers::SlideOnRamp(15.0, 5.0, false, Tuning);
	const double Held = TNRallyDriveTestHelpers::SlideOnRamp(15.0, 5.0, true, Tuning);
	TestTrue(*FString::Printf(TEXT("sin el freno de la parrilla rueda %.0f cm (el fallo de #611)"), Free), Free > 100.0);
	TestTrue(*FString::Printf(TEXT("con el freno de la parrilla se desplaza %.2f cm, menos de 5"), Held), Held < 5.0);

	// La componente a lo largo de la normal (la suspensión que se asienta) no se toca; la guiñada sí se quita.
	const FVector Up = FVector(0.3, 0.0, 1.0).GetSafeNormal();
	const FVector Settling = Up * -50.0;
	TestTrue(TEXT("el asentado de la suspensión se conserva"), GridHoldVelocity(Settling, Up, FVector::ZeroVector, Tuning).Equals(Settling, 0.01));
	TestTrue(TEXT("en su sitio, sin velocidad en el plano del suelo"),
		GridHoldVelocity(FVector(300.0, 200.0, 0.0), FVector::UpVector, FVector::ZeroVector, Tuning).IsNearlyZero(0.01));
	TestTrue(TEXT("la corrección tiene tope"),
		GridHoldVelocity(FVector::ZeroVector, FVector::UpVector, FVector(1000.0, 0.0, 0.0), Tuning).Size() <= Tuning.MaxCorrectionCms + 0.01);
	TestTrue(TEXT("la guiñada se quita y el cabeceo se queda"),
		GridHoldAngularVelocity(FVector(0.5, 0.0, 2.0), FVector::UpVector).Equals(FVector(0.5, 0.0, 0.0), 0.001));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
