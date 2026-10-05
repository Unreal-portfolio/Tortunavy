// Lógica pura del buggy y de su torreta (TN_BuggyMath.h y TN_RallyTurretLogic.h). Sin mundo ni actores. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Vehicles/TN_BuggyMath.h"
#include "Vehicles/TN_RallyTurretLogic.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// ─────────────────────────────────────────────────────────────────────────────
// Buggy
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyBuggySelfRightTest,
	"Tortunabo.Rally.Buggy.SelfRight",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyBuggySelfRightTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	TestTrue(TEXT("boca abajo está volcado"), IsFlipped(-1.f));
	TestTrue(TEXT("de lado está volcado"), IsFlipped(0.f));
	TestFalse(TEXT("derecho no está volcado"), IsFlipped(1.f));
	TestFalse(TEXT("inclinado 60 grados (Z = 0,5) no está volcado"), IsFlipped(0.5f));

	TestEqual(TEXT("el tiempo volcado se acumula"), AdvanceFlipped(1.f, -1.f, 0.5f), 1.5f);
	TestEqual(TEXT("al ponerse derecho vuelve a 0"), AdvanceFlipped(3.f, 1.f, 0.5f), 0.f);

	TestTrue(TEXT("sin volcar no endereza"), DecideSelfRight(0.f, true, 0.5f, 4.f) == ESelfRight::None);
	TestTrue(TEXT("pulsar R antes de la espera manual no endereza"), DecideSelfRight(0.2f, true, 0.5f, 4.f) == ESelfRight::None);
	TestTrue(TEXT("pulsar R volcado endereza"), DecideSelfRight(0.6f, true, 0.5f, 4.f) == ESelfRight::Manual);
	TestTrue(TEXT("sin pulsar, a los 3,9 s no endereza"), DecideSelfRight(3.9f, false, 0.5f, 4.f) == ESelfRight::None);
	TestTrue(TEXT("sin pulsar, a los 4 s endereza solo"), DecideSelfRight(4.f, false, 0.5f, 4.f) == ESelfRight::Auto);

	const FTransform Flipped(FRotator(10.f, 75.f, 180.f), FVector(100.f, 200.f, 50.f));
	const FTransform Upright = SelfRightTransform(Flipped, 100.f);
	TestEqual(TEXT("sube 1 m"), Upright.GetLocation().Z, 150.0);
	TestEqual(TEXT("conserva X"), Upright.GetLocation().X, 100.0);
	TestTrue(TEXT("queda derecho"), Upright.GetRotation().GetUpVector().Z > 0.999);
	TestEqual(TEXT("conserva la guiñada"), Upright.Rotator().Yaw, 75.0, 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyBuggyAntiRollTest,
	"Tortunabo.Rally.Buggy.AntiRoll",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyBuggyAntiRollTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	const FAntiRollTuning Tuning;
	const FVector Forward = FVector::ForwardVector;
	TestTrue(TEXT("derecho y quieto: nada"), AntiRollAccel(Forward, FVector::UpVector, FVector::ZeroVector, true, Tuning).IsNearlyZero());

	const FQuat Roll15(Forward, FMath::DegreesToRadians(15.0));
	TestTrue(TEXT("15 grados de alabeo en el suelo (peralte): nada"),
		AntiRollAccel(Forward, Roll15.RotateVector(FVector::UpVector), FVector::ZeroVector, false, Tuning).IsNearlyZero());
	TestFalse(TEXT("los mismos 15 grados en el aire: corrige"),
		AntiRollAccel(Forward, Roll15.RotateVector(FVector::UpVector), FVector::ZeroVector, true, Tuning).IsNearlyZero());
	TestTrue(TEXT("volcado: no corrige (lo endereza el servidor)"),
		AntiRollAccel(Forward, -FVector::UpVector, FVector::ZeroVector, false, Tuning).IsNearlyZero());
	FAntiRollTuning Off = Tuning;
	Off.Stiffness = 0.f;
	TestTrue(TEXT("rigidez 0: desactivado"), AntiRollAccel(Forward, Roll15.RotateVector(FVector::UpVector), FVector::ZeroVector, true, Off).IsNearlyZero());

	// Integración simple (60 Hz, inercia unidad): un buggy que vuelca a 3 rad/s con 50 grados de alabeo en el suelo
	// no llega a volcar y en 2 s queda cerca de lo tolerado (sin gravedad ni suspensión, dentro de la zona libre no hay
	// nada que lo frene: oscila en su borde).
	FQuat Attitude(Forward, FMath::DegreesToRadians(50.0));
	FVector Omega = Forward * 3.0;
	double MaxTiltDeg = 0.0;
	for (int32 Step = 0; Step < 120; ++Step)
	{
		const FVector Up = Attitude.RotateVector(FVector::UpVector);
		const FVector Fwd = Attitude.RotateVector(FVector::ForwardVector);
		Omega += AntiRollAccel(Fwd, Up, Omega, false, Tuning) / 60.0;
		const double Angle = Omega.Size() / 60.0;
		if (Angle > 0.0)
		{
			Attitude = (FQuat(Omega.GetSafeNormal(), Angle) * Attitude).GetNormalized();
		}
		MaxTiltDeg = FMath::Max(MaxTiltDeg, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Attitude.RotateVector(FVector::UpVector).Z, -1.0, 1.0))));
	}
	const double FinalTiltDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Attitude.RotateVector(FVector::UpVector).Z, -1.0, 1.0)));
	TestTrue(FString::Printf(TEXT("no vuelca (máximo %.0f grados)"), MaxTiltDeg), !IsFlipped(static_cast<float>(FMath::Cos(FMath::DegreesToRadians(MaxTiltDeg)))));
	TestTrue(FString::Printf(TEXT("vuelve cerca de lo tolerado (%.1f grados)"), FinalTiltDeg), FinalTiltDeg <= Tuning.GroundFreeRollDeg + 10.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyBuggyRespawnHoldTest,
	"Tortunabo.Rally.Buggy.RespawnHold",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyBuggyRespawnHoldTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	FHold Hold;
	TestFalse(TEXT("a 1 s no pide"), AdvanceHold(Hold, true, 1.f, 1.5f));
	TestTrue(TEXT("a 1,5 s pide"), AdvanceHold(Hold, true, 0.5f, 1.5f));
	TestFalse(TEXT("solo una vez por pulsación"), AdvanceHold(Hold, true, 1.f, 1.5f));
	TestFalse(TEXT("al soltar se reinicia"), AdvanceHold(Hold, false, 0.1f, 1.5f));
	TestEqual(TEXT("tiempo a 0 tras soltar"), Hold.Held, 0.f);
	TestFalse(TEXT("un toque corto no pide"), AdvanceHold(Hold, true, 0.2f, 1.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyBuggyDriftTest,
	"Tortunabo.Rally.Buggy.Drift",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyBuggyDriftTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	TestEqual(TEXT("parado no hay deriva"), SlipAngleDeg(FVector::ForwardVector, FVector(50.f, 0.f, 0.f)), 0.f);
	TestEqual(TEXT("recto no hay deriva"), SlipAngleDeg(FVector::ForwardVector, FVector(1000.f, 0.f, 0.f)), 0.f, 0.01f);
	TestEqual(TEXT("velocidad a la derecha = deriva positiva"), SlipAngleDeg(FVector::ForwardVector, FVector(1000.f, 1000.f, 0.f)), 45.f, 0.01f);
	TestEqual(TEXT("sin deriva la dirección es la pedida"), AssistSteer(0.3f, 0.f, 0.8f, 35.f), 0.3f, 0.001f);
	TestEqual(TEXT("deriva máxima: contravolante completo"), AssistSteer(0.f, 35.f, 0.8f, 35.f), 0.8f, 0.001f);
	TestEqual(TEXT("se limita a 1"), AssistSteer(0.9f, 35.f, 0.8f, 35.f), 1.f, 0.001f);
	TestEqual(TEXT("trompo (más de 90): sin asistencia"), AssistSteer(0.2f, 120.f, 0.8f, 35.f), 0.2f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyBuggyBumpTest,
	"Tortunabo.Rally.Buggy.Bump",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyBuggyBumpTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	const FBumpTuning Tuning;
	FWheelTrack Track;
	// Escalón: sube 20 cm un frame y luego nada; golpea el frame siguiente.
	FBumpStep Step = BumpStep(Track, 20.f, 1000.f, 51.f, true, Tuning);
	TestEqual(TEXT("el frame de la subida no golpea"), Step.Kick, 0.f);
	Step = BumpStep(Step.Track, 0.f, 1000.f, 51.f, true, Tuning);
	TestEqual(TEXT("escalón: golpe un frame después"), Step.Kick, 0.15f * 1000.f * (20.f / 51.f), 0.01f);
	// Rampa: sube varios frames seguidos y no golpea.
	Step = BumpStep(FWheelTrack(), 10.f, 1000.f, 51.f, true, Tuning);
	Step = BumpStep(Step.Track, 10.f, 1000.f, 51.f, true, Tuning);
	TestEqual(TEXT("rampa: no golpea"), Step.Kick, 0.f);
	TestEqual(TEXT("tope del golpe"), KickSpeed(100.f, 5000.f, 51.f, Tuning), Tuning.MaxKick);
	TestFalse(TEXT("aterrizaje (sin contacto) no es subida"), IsRise(20.f, false, Tuning));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyBuggyTintAndPuddleTest,
	"Tortunabo.Rally.Buggy.TintAndPuddle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyBuggyTintAndPuddleTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	TestTrue(TEXT("sin equipo: blanco"), TeamColor(INDEX_NONE).Equals(FLinearColor::White));
	TestFalse(TEXT("equipos 0 y 1 con colores distintos"), TeamColor(0).Equals(TeamColor(1)));
	TestTrue(TEXT("los colores se repiten cada 8"), TeamColor(1).Equals(TeamColor(9)));

	TestEqual(TEXT("por debajo de la velocidad tope no frena"), SpeedCapDecel(1000.f, 1830.f, 3.f), 0.f);
	TestEqual(TEXT("por encima frena en proporción"), SpeedCapDecel(2000.f, 1800.f, 3.f), 600.f, 0.01f);
	TestEqual(TEXT("marcha atrás también"), SpeedCapDecel(-2000.f, 1800.f, 3.f), 600.f, 0.01f);

	TestEqual(TEXT("sin bamboleo al acabar"), SteerWobble(0.f, 0.4f, 0.6f, 5.f), 0.f);
	TestEqual(TEXT("bamboleo máximo al empezar"), SteerWobble(0.4f, 0.4f, 0.6f, 5.f), 0.6f, 0.001f);
	TestTrue(TEXT("el bamboleo se apaga"), FMath::Abs(SteerWobble(0.05f, 0.4f, 0.6f, 5.f)) <= 0.6f * 0.125f + 0.001f);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Torreta
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretHeatTest,
	"Tortunabo.Rally.Turret.Heat",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretHeatTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	const float Interval = SpecFor(ETNRallyAmmo::Coco).FireInterval;
	FHeat Heat;
	for (int32 Shot = 1; Shot <= 5; ++Shot)
	{
		TestTrue(FString::Printf(TEXT("disparo %d permitido"), Shot), CanFireCoco(Heat));
		Heat = Cool(AfterCocoShot(Heat), Interval);
		TestFalse(FString::Printf(TEXT("tras %d disparos seguidos no sobrecalienta"), Shot), IsOverheated(Heat));
	}
	Heat = AfterCocoShot(Heat);
	TestTrue(TEXT("el 6.º disparo seguido sobrecalienta"), IsOverheated(Heat));
	TestEqual(TEXT("calor al máximo"), Heat.Heat, 1.f);
	TestFalse(TEXT("sobrecalentada no dispara"), CanFireCoco(Heat));
	Heat = Cool(Heat, 2.4f);
	TestTrue(TEXT("a los 2,4 s sigue sobrecalentada"), IsOverheated(Heat));
	Heat = Cool(Heat, 0.1f);
	TestFalse(TEXT("a los 2,5 s se recupera"), IsOverheated(Heat));
	TestEqual(TEXT("y el calor vuelve a 0"), Heat.Heat, 0.f);

	FHeat Paced;
	Paced = AfterCocoShot(Paced);
	Paced = Cool(Paced, 0.5f);
	TestEqual(TEXT("antes de 0,6 s sin disparar no enfría"), Paced.Heat, 1.f / ShotsToOverheat, 0.001f);
	Paced = Cool(Paced, 1.f);
	TestTrue(TEXT("después enfría"), Paced.Heat < 1.f / ShotsToOverheat);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretChargesTest,
	"Tortunabo.Rally.Turret.Charges",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretChargesTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	TestEqual(TEXT("Alga da 2 cargas"), SpecFor(ETNRallyAmmo::Alga).BoxCharges, 2);
	TestEqual(TEXT("Burbuja da 1"), SpecFor(ETNRallyAmmo::Burbuja).BoxCharges, 1);
	TestEqual(TEXT("Mortero da 1"), SpecFor(ETNRallyAmmo::Mortero).BoxCharges, 1);
	TestEqual(TEXT("Tinta da 2"), SpecFor(ETNRallyAmmo::Tinta).BoxCharges, 2);

	FSpecial Special = Give(ETNRallyAmmo::Alga, 2);
	TestTrue(TEXT("con cargas se puede disparar"), CanFireSpecial(Special));
	Special = AfterSpecialShot(Special);
	TestEqual(TEXT("queda 1 carga"), Special.Charges, 1);
	Special = AfterSpecialShot(Special);
	TestTrue(TEXT("con la última vuelve a None"), Special.Ammo == ETNRallyAmmo::None);
	TestFalse(TEXT("sin cargas no dispara"), CanFireSpecial(Special));
	TestTrue(TEXT("disparar vacío sigue vacío"), AfterSpecialShot(Special).Ammo == ETNRallyAmmo::None);

	Special = Give(ETNRallyAmmo::Tinta, 2);
	Special = Give(ETNRallyAmmo::Mortero, 1);
	TestTrue(TEXT("una caja sustituye a la anterior"), Special.Ammo == ETNRallyAmmo::Mortero && Special.Charges == 1);
	TestFalse(TEXT("el coco no es munición especial"), CanFireSpecial(Give(ETNRallyAmmo::Coco, 3)));
	TestFalse(TEXT("sin cargas no se da"), CanFireSpecial(Give(ETNRallyAmmo::Alga, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretAimTest,
	"Tortunabo.Rally.Turret.AimClamp",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretAimTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	TestEqual(TEXT("cabeceo limitado a +45"), ClampAim(FRotator(80.f, 0.f, 0.f)).Pitch, 45.0);
	TestEqual(TEXT("cabeceo limitado a -10"), ClampAim(FRotator(-60.f, 0.f, 0.f)).Pitch, -10.0);
	TestEqual(TEXT("cabeceo válido intacto"), ClampAim(FRotator(20.f, 0.f, 0.f)).Pitch, 20.0);
	TestEqual(TEXT("guiñada normalizada"), ClampAim(FRotator(0.f, 270.f, 0.f)).Yaw, -90.0, 0.001);
	TestEqual(TEXT("sin alabeo"), ClampAim(FRotator(0.f, 0.f, 30.f)).Roll, 0.0);
	TestFalse(TEXT("NaN rechazado"), IsAimFinite(std::numeric_limits<float>::quiet_NaN(), 0.f));
	TestFalse(TEXT("infinito rechazado"), IsAimFinite(0.f, std::numeric_limits<float>::infinity()));
	TestTrue(TEXT("valores normales aceptados"), IsAimFinite(170.f, 30.f));

	const FRotator Buggy(0.f, 90.f, 0.f);
	const FVector Back = AimWorldDirection(Buggy, FRotator(0.f, 180.f, 0.f));
	TestTrue(TEXT("apuntar atrás con el buggy mirando a +Y da -Y"), Back.Equals(FVector(0.f, -1.f, 0.f), 0.001f));
	const FRotator Down = RelativeAimFromWorld(FRotator::ZeroRotator, FVector(1.f, 0.f, -1.f));
	TestEqual(TEXT("una dirección a -45 se limita a -10"), Down.Pitch, -10.0, 0.01);

	TestTrue(TEXT("cadencia: a tiempo"), CadenceOk(10.0, 9.7, 0.25f));
	TestFalse(TEXT("cadencia: demasiado pronto"), CadenceOk(10.0, 9.9, 0.25f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretRecoilTest,
	"Tortunabo.Rally.Turret.Recoil",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretRecoilTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	const FVector Forward = RecoilVelocity(FVector(1.f, 0.f, 0.f), 120.f);
	TestTrue(TEXT("disparar hacia delante empuja hacia atrás (frena)"), Forward.Equals(FVector(-120.f, 0.f, 0.f), 0.01f));
	const FVector Backward = RecoilVelocity(FVector(-1.f, 0.f, 0.f), 700.f);
	TestTrue(TEXT("disparar hacia atrás empuja hacia delante (turbo)"), Backward.Equals(FVector(700.f, 0.f, 0.f), 0.01f));
	const FVector Lob = RecoilVelocity(FRotator(45.f, 0.f, 0.f).Vector(), 100.f);
	TestEqual(TEXT("el retroceso es horizontal"), Lob.Z, 0.0);
	TestEqual(TEXT("y conserva su módulo"), Lob.Size(), 100.0, 0.01);

	TestEqual(TEXT("retroceso del coco"), SpecFor(ETNRallyAmmo::Coco).RecoilCms, 120.f);
	TestEqual(TEXT("retroceso del alga"), SpecFor(ETNRallyAmmo::Alga).RecoilCms, 60.f);
	// #629: todas las municiones empujan al buggy; la burbuja, poco (sale lenta para poder cogerla).
	TestEqual(TEXT("retroceso de la burbuja"), SpecFor(ETNRallyAmmo::Burbuja).RecoilCms, 40.f);
	TestEqual(TEXT("retroceso del mortero"), SpecFor(ETNRallyAmmo::Mortero).RecoilCms, 700.f);
	TestEqual(TEXT("retroceso de la tinta"), SpecFor(ETNRallyAmmo::Tinta).RecoilCms, 60.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretEffectsTest,
	"Tortunabo.Rally.Turret.Effects",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretEffectsTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	TestEqual(TEXT("coco: empujón lateral 350 cm/s"), CocoLateralCms, 350.f);
	TestEqual(TEXT("coco: bamboleo 0,4 s"), CocoWobbleSeconds, 0.4f);
	TestEqual(TEXT("charco: 6 m"), AlgaPuddleRadiusCm, 600.f);
	TestEqual(TEXT("charco: 5 s"), AlgaPuddleSeconds, 5.f);
	TestEqual(TEXT("charco: agarre x0,35 (#770)"), PuddleGripMultiplier(true), 0.35f);
	TestEqual(TEXT("fuera del charco: agarre x1"), PuddleGripMultiplier(false), 1.f);
	TestEqual(TEXT("charco: velocidad máxima x0,5 (#770)"), PuddleSpeedCapCms(true), PuddleSpeedCapCms(false) * 0.5f, 0.01f);
	TestEqual(TEXT("burbuja: flota 6 s"), SpecFor(ETNRallyAmmo::Burbuja).LifeSeconds, 6.f);
	TestEqual(TEXT("burbuja: sin gravedad"), SpecFor(ETNRallyAmmo::Burbuja).GravityScale, 0.f);
	TestEqual(TEXT("escudo: 4 s"), ShieldSeconds, 4.f);
	TestEqual(TEXT("mortero: 5 m"), MortarRadiusCm, 500.f);
	TestEqual(TEXT("mortero: 450 cm/s hacia arriba"), MortarUpCms, 450.f);
	TestEqual(TEXT("tinta: 3 s"), InkSeconds, 3.f);
	TestFalse(TEXT("None no es especial"), IsSpecial(ETNRallyAmmo::None));
	TestTrue(TEXT("Tinta es especial"), IsSpecial(ETNRallyAmmo::Tinta));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretShieldTest,
	"Tortunabo.Rally.Turret.Shield",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretShieldTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	const FImpactOutcome Shielded = ResolveImpact(true);
	TestFalse(TEXT("con escudo el impacto no llega"), Shielded.bApplies);
	TestTrue(TEXT("y el escudo se gasta"), Shielded.bShieldConsumed);
	const FImpactOutcome Bare = ResolveImpact(false);
	TestTrue(TEXT("sin escudo el impacto llega"), Bare.bApplies);
	TestFalse(TEXT("y no hay escudo que gastar"), Bare.bShieldConsumed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretAutoAimTest,
	"Tortunabo.Rally.Turret.AutoAim",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretAutoAimTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	const TArray<FVector> Buggies = {
		FVector(3000.f, 0.f, 0.f),     // delante, 30 m
		FVector(1000.f, 200.f, 0.f),   // delante, 10 m: el más cercano
		FVector(1000.f, 3000.f, 0.f),  // a un lado: fuera del cono
		FVector(-500.f, 0.f, 0.f),     // detrás
		FVector(7000.f, 0.f, 0.f),     // delante, 70 m: fuera de alcance
	};
	TestEqual(TEXT("delante: el más cercano dentro del cono"), PickAutoAimTarget(FVector::ZeroVector, FVector::ForwardVector, Buggies), 1);
	TestEqual(TEXT("hacia atrás: el de detrás"), PickAutoAimTarget(FVector::ZeroVector, -FVector::ForwardVector, Buggies), 3);
	const float MaxRange = 2800.f * 2800.f / 980.f;
	TestEqual(TEXT("parábola en llano: 45 grados da el alcance máximo v²/g"), LobRangeCm(45.f, 0.f, 2800.f, 980.f), MaxRange, 1.f);
	TestEqual(TEXT("parábola: fuera de alcance, 45"), LobPitchDeg(100000.f, 0.f, 2800.f, 980.f), 45.f);
	TestEqual(TEXT("parábola en llano: a media distancia, el tiro bajo (15 grados)"), LobPitchDeg(0.5f * MaxRange, 0.f, 2800.f, 980.f), 15.f, 0.5f);
	TestTrue(TEXT("parábola: lanzado desde 2 m más arriba hace falta menos cabeceo"),
		LobPitchDeg(1500.f, 200.f, 3500.f, 980.f) < LobPitchDeg(1500.f, 0.f, 3500.f, 980.f));
	TestTrue(TEXT("parábola: el cabeceo elegido llega al blanco"), LobRangeCm(LobPitchDeg(1500.f, 200.f, 3500.f, 980.f), 200.f, 3500.f, 980.f) >= 1500.f);
	const TArray<FVector> Far = { FVector(7000.f, 0.f, 0.f), FVector(0.f, 2000.f, 0.f) };
	TestEqual(TEXT("sin nadie en el cono de 60 m: ninguno"), PickAutoAimTarget(FVector::ZeroVector, FVector::ForwardVector, Far), INDEX_NONE);
	return true;
}

#endif
