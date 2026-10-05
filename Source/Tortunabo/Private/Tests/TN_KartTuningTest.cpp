// Conducción de los karts (#742): ajuste del buggy (punta, par, turbo, antivuelco, freno de mano), dirección que se cierra con
// la velocidad y mini-turbo del derrape. Lógica pura y valores por defecto, sin mundo. Correr desde Session Frontend
// (categoría "Tortunabo.Kart.Tuning") o headless con UnrealEditor-Win64-DebugGame-Cmd <uproject>
// -ExecCmds="Automation RunTests Tortunabo.Kart.Tuning; Quit". La medida con física está en Tortunabo.Kart.Measure.*
// (TN_KartMeasureTest.cpp); TN.Kart.Tuning 0 deja el kart como el Rally para comparar en el juego.

#include "Misc/AutomationTest.h"
#include "Kart/TN_KartBuggy.h"
#include "Rally/TN_RallyKartBuggy.h"
#include "Vehicles/TN_BuggyData.h"
#include "Vehicles/TN_BuggyMath.h"
#include "Vehicles/TN_RallyTurretLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartTuningValuesTest, "Tortunabo.Kart.Tuning.Values",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartTuningValuesTest::RunTest(const FString& Parameters)
{
	const UTN_BuggyData* Base = GetDefault<UTN_BuggyData>();
	UTN_BuggyData* Tuned = NewObject<UTN_BuggyData>(GetTransientPackage());
	TNKart::ApplyKartTuning(*Tuned);

	TestTrue(TEXT("el régimen sube entre un 25 y un 35 %"), Tuned->MaxRPM >= 1.25f * Base->MaxRPM && Tuned->MaxRPM <= 1.35f * Base->MaxRPM);
	TestTrue(TEXT("el par sube entre un 25 y un 35 %"), Tuned->MaxTorque >= 1.25f * Base->MaxTorque && Tuned->MaxTorque <= 1.35f * Base->MaxTorque);
	TestTrue(TEXT("el par de la parte alta (el que fija la punta) sube"), Tuned->TopEndTorqueScale > 1.f);
	const float BaseBoostTop = TNRallyTurret::BuggyTopSpeedCms * Base->BoostTopSpeedMultiplier;
	const float KartBoostTop = TNRallyTurret::BuggyTopSpeedCms * Tuned->BoostTopSpeedMultiplier;
	TestTrue(TEXT("la punta del turbo sigue a la del kart"), KartBoostTop >= 1.25f * BaseBoostTop);
	TestTrue(TEXT("el turbo empuja más fuerte"), Tuned->BoostPushAccel > Base->BoostPushAccel);
	TestTrue(TEXT("el turbo sube y baja deprisa (el mini-turbo dura poco)"),
		Tuned->BoostRampUpSeconds < Base->BoostRampUpSeconds && Tuned->BoostRampDownSeconds <= Base->BoostRampDownSeconds);
	TestTrue(TEXT("el antivuelco corrige el alabeo hasta más velocidad"),
		Tuned->AntiRollGroundRollFullSpeedCms >= 1.25f * Base->AntiRollGroundRollFullSpeedCms
		&& Tuned->AntiRollGroundRollZeroSpeedCms >= 1.25f * Base->AntiRollGroundRollZeroSpeedCms);
	TestTrue(TEXT("el freno de mano suelta menos la trasera, sin igualar el agarre normal"),
		Tuned->HandbrakeRearFriction > Base->HandbrakeRearFriction && Tuned->HandbrakeRearFriction < Tuned->RearFriction);
	TestTrue(TEXT("el ángulo de las ruedas y el reparto del par no cambian"),
		Tuned->MaxSteerAngleDeg == Base->MaxSteerAngleDeg && Tuned->DriveRearShare == Base->DriveRearShare);

	// Una escala de 1 deja el par y el régimen como están.
	UTN_BuggyData* Same = NewObject<UTN_BuggyData>(GetTransientPackage());
	TNKart::ApplyKartTuning(*Same, 1.f, 1.f);
	TestEqual(TEXT("con escala 1, el régimen no cambia"), Same->MaxRPM, Base->MaxRPM);
	TestEqual(TEXT("con escala 1, el par no cambia"), Same->MaxTorque, Base->MaxTorque);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartTuningRallyTest, "Tortunabo.Kart.Tuning.RallyUntouched",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartTuningRallyTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("el kart de Karts pide la conducción de los karts"), GetDefault<ATN_KartBuggy>()->WantsKartTuning());
	TestFalse(TEXT("el buggy del Rally de LVL_Rally no la pide"), GetDefault<ATN_RallyKartBuggy>()->WantsKartTuning());
	TestFalse(TEXT("en el CDO no hay ajuste puesto (se pone al aparecer)"), GetDefault<ATN_KartBuggy>()->UsesKartTuning());
	TestEqual(TEXT("sin ajuste, escala de velocidad 1"), GetDefault<ATN_KartBuggy>()->GetKartSpeedScale(), 1.f);

	// El Rally no cambia por los valores nuevos de UTN_BuggyData.
	const UTN_BuggyData* Base = GetDefault<UTN_BuggyData>();
	TestEqual(TEXT("el par de la parte alta de serie es 1"), Base->TopEndTorqueScale, 1.f);
	const TArray<TNBuggy::FCurveKey> Default = TNBuggy::TorqueCurveKeys(Base->MaxTorque);
	const TArray<TNBuggy::FCurveKey> Explicit = TNBuggy::TorqueCurveKeys(Base->MaxTorque, 1.f);
	TestEqual(TEXT("la curva de serie no cambia"), Default.Num(), Explicit.Num());
	for (int32 Index = 0; Index < FMath::Min(Default.Num(), Explicit.Num()); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("clave %d"), Index), Default[Index].Y, Explicit[Index].Y);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartTuningTorqueTest, "Tortunabo.Kart.Tuning.TopEndTorque",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartTuningTorqueTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	UTN_BuggyData* Tuned = NewObject<UTN_BuggyData>(GetTransientPackage());
	TNKart::ApplyKartTuning(*Tuned);
	const TArray<FCurveKey> Keys = TorqueCurveKeys(Tuned->MaxTorque, Tuned->TopEndTorqueScale);
	float CurveMax = 0.f;
	for (const FCurveKey& Key : Keys)
	{
		CurveMax = FMath::Max(CurveMax, Key.Y);
	}
	TestEqual(TEXT("el máximo de la curva sigue siendo 1"), CurveMax, 1.f);
	// Desde el 80 % de MaxRPM, el par absoluto antiguo por TopEndTorqueScale (la punta sube).
	for (float Rpm = 0.8f; Rpm <= 1.f + KINDA_SMALL_NUMBER; Rpm += 0.05f)
	{
		const float KartTorque = EvalLinearKeys(Keys, Rpm) * Tuned->MaxTorque;
		const float OldTorque = EvalLinearKeys(LegacyTorqueCurveKeys(), Rpm) * LegacyMaxTorque;
		TestEqual(*FString::Printf(TEXT("al %.0f %% de MaxRPM el par absoluto es el antiguo por la escala"), Rpm * 100.f),
			KartTorque, FMath::Min(OldTorque * Tuned->TopEndTorqueScale, Tuned->MaxTorque), 1.f);
	}
	// El arranque (hasta el 55 %) sigue en el par máximo.
	TestEqual(TEXT("el arranque sigue plano al par máximo"), EvalLinearKeys(Keys, 0.4f) * Tuned->MaxTorque, Tuned->MaxTorque, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartTuningSteerTest, "Tortunabo.Kart.Tuning.SteerAtSpeed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartTuningSteerTest::RunTest(const FString& Parameters)
{
	using namespace TNKart;
	TestEqual(TEXT("parado, giro entero"), SpeedSteerMultiplier(0.f), 1.f);
	TestEqual(TEXT("a SteerHalfSpeedCms, la mitad"), SpeedSteerMultiplier(SteerHalfSpeedCms), 0.5f, 0.001f);
	TestEqual(TEXT("marcha atrás cuenta igual"), SpeedSteerMultiplier(-3500.f), SpeedSteerMultiplier(3500.f));
	TestEqual(TEXT("muy rápido, el mínimo"), SpeedSteerMultiplier(20000.f), MinSteerFraction);
	float Previous = 1.f;
	for (float Speed = 0.f; Speed <= 6000.f; Speed += 100.f)
	{
		const float Multiplier = SpeedSteerMultiplier(Speed);
		TestTrue(*FString::Printf(TEXT("a %.0f cm/s no sube al ir más deprisa"), Speed), Multiplier <= Previous + KINDA_SMALL_NUMBER);
		TestTrue(TEXT("entre el mínimo y 1"), Multiplier >= MinSteerFraction - KINDA_SMALL_NUMBER && Multiplier <= 1.f);
		Previous = Multiplier;
	}
	// Despacio (maniobras, horquillas) casi entero; a la punta del kart (3050 cm/s por 1,3) bastante menos.
	TestTrue(TEXT("a 20 km/h casi entero"), SpeedSteerMultiplier(20.f / 0.036f) > 0.8f);
	TestTrue(TEXT("a la punta del kart, menos de un tercio"), SpeedSteerMultiplier(3050.f * DefaultSpeedScale) < 0.3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartTuningDriftTest, "Tortunabo.Kart.Tuning.DriftBoost",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartTuningDriftTest::RunTest(const FString& Parameters)
{
	using namespace TNKart;
	constexpr float Fast = 2400.f;
	TestEqual(TEXT("un derrape corto no da turbo"), DriftBoostSeconds(DriftTier1Seconds - 0.01f), 0.f);
	TestEqual(TEXT("primer tramo"), DriftBoostSeconds(DriftTier1Seconds), DriftBoost1Seconds);
	TestEqual(TEXT("segundo tramo"), DriftBoostSeconds(DriftTier2Seconds), DriftBoost2Seconds);
	TestEqual(TEXT("tercer tramo"), DriftBoostSeconds(DriftTier3Seconds + 5.f), DriftBoost3Seconds);
	TestTrue(TEXT("más derrape, más turbo"), DriftBoost1Seconds < DriftBoost2Seconds && DriftBoost2Seconds < DriftBoost3Seconds);

	// Un derrape de verdad: freno de mano, en el suelo, rápido y girando. Suma por pasos y se cobra al soltar.
	float Drift = 0.f;
	for (int32 Step = 0; Step < 90; ++Step)
	{
		const FDriftStep Next = AdvanceDrift(Drift, true, true, Fast, 1.f, 0.f, 1.f / 60.f);
		TestEqual(TEXT("mientras derrapa no hay turbo"), Next.BoostSeconds, 0.f);
		Drift = Next.DriftSeconds;
	}
	TestEqual(TEXT("90 pasos de 1/60 s son 1,5 s de derrape"), Drift, 1.5f, 0.001f);
	const FDriftStep Released = AdvanceDrift(Drift, false, true, Fast, 1.f, 0.f, 1.f / 60.f);
	TestEqual(TEXT("al soltar el freno de mano, el turbo del segundo tramo"), Released.BoostSeconds, DriftBoost2Seconds);
	TestEqual(TEXT("y el derrape vuelve a 0"), Released.DriftSeconds, 0.f);

	// Lo que no cuenta como derrape.
	TestEqual(TEXT("sin girar ni deslizar no suma (solo frena)"), AdvanceDrift(0.f, true, true, Fast, 0.f, 3.f, 0.5f).DriftSeconds, 0.f);
	TestEqual(TEXT("con poca dirección no suma"), AdvanceDrift(0.f, true, true, Fast, DriftMinSteer * 0.5f, 0.f, 0.5f).DriftSeconds, 0.f);
	TestEqual(TEXT("deslizando suma aunque el contravolante se coma la dirección"), AdvanceDrift(0.f, true, true, Fast, 0.1f, -DriftMinSlipDeg, 0.5f).DriftSeconds, 0.5f);
	TestEqual(TEXT("en el aire no suma, pero conserva lo que llevaba"), AdvanceDrift(0.4f, true, false, Fast, 1.f, 30.f, 0.5f).DriftSeconds, 0.4f);
	TestEqual(TEXT("a la izquierda también suma"), AdvanceDrift(0.f, true, true, Fast, -1.f, 0.f, 0.5f).DriftSeconds, 0.5f);
	TestEqual(TEXT("casi parado con el freno puesto se pierde el derrape"), AdvanceDrift(1.5f, true, true, DriftMinSpeedCms - 1.f, 1.f, 30.f, 0.1f).DriftSeconds, 0.f);
	TestEqual(TEXT("y no da turbo"), AdvanceDrift(1.5f, true, true, 100.f, 1.f, 30.f, 0.1f).BoostSeconds, 0.f);
	TestEqual(TEXT("soltar el freno sin derrape no da turbo"), AdvanceDrift(0.f, false, true, Fast, 1.f, 0.f, 0.1f).BoostSeconds, 0.f);
	TestEqual(TEXT("soltar tras un derrape corto no da turbo"), AdvanceDrift(DriftTier1Seconds * 0.5f, false, true, Fast, 1.f, 0.f, 0.1f).BoostSeconds, 0.f);
	TestEqual(TEXT("un paso sin tiempo no suma"), AdvanceDrift(0.3f, true, true, Fast, 1.f, 0.f, -1.f).DriftSeconds, 0.3f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
