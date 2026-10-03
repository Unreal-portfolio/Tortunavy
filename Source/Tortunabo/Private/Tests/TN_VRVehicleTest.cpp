// Vehículos con gafas (#529, Docs/Modo_VR.md «Vehículos»), sin mundo ni actores: el volante que se gira con una o dos
// manos (VR/TN_VRVehicleMath.h), el apuntado de la torreta con la mano, la inclinación de la artillera con la cabeza y que
// el volante y las asas queden a mano desde los ojos de la tortuga sentada. Se pueden correr sin gafas:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.VR.Vehicle; Quit" -nullrhi -nosound -unattended

#include "Misc/AutomationTest.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Vehicles/TN_RallyTurretLogic.h"
#include "VR/TN_VRSeatComponent.h"
#include "VR/TN_VRVehicleMath.h"
#include "../Vehicles/TN_BuggyTurretMesh.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNVRVehicleTest
{
	/** Un fotograma a 90 Hz. */
	constexpr float Dt = 1.f / 90.f;

	/** Avanza el volante Frames fotogramas con las manos quietas en Left y Right. */
	TNVRVehicle::FWheelState Hold(TNVRVehicle::FWheelState State, bool bLeft, bool bRight, const FVector& Left, const FVector& Right,
		const TNVRVehicle::FWheelFrame& Wheel, int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			State = TNVRVehicle::StepWheel(State, bLeft, bRight, Left, Right, Wheel, Dt);
		}
		return State;
	}

	/** Gira las dos manos de From a To grados en Steps fotogramas (como unas manos de verdad, poco a poco). */
	TNVRVehicle::FWheelState TurnBoth(TNVRVehicle::FWheelState State, const TNVRVehicle::FWheelFrame& Wheel, double From, double To, int32 Steps)
	{
		for (int32 Step = 1; Step <= Steps; ++Step)
		{
			FVector Left;
			FVector Right;
			TNVRVehicle::HandsOnWheel(Wheel, FMath::Lerp(From, To, static_cast<double>(Step) / Steps), Left, Right);
			State = TNVRVehicle::StepWheel(State, true, true, Left, Right, Wheel, Dt);
		}
		return State;
	}

	/** Mano en el aro a Degrees del reloj (0 arriba, 90 a la derecha). */
	FVector OnRim(const TNVRVehicle::FWheelFrame& Wheel, double Degrees)
	{
		const double Rad = FMath::DegreesToRadians(Degrees);
		return Wheel.Center + (Wheel.Right() * FMath::Sin(Rad) + Wheel.Up.GetSafeNormal() * FMath::Cos(Rad)) * Wheel.Radius;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Volante con las manos
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRVehicleWheelAngleTest,
	"Tortunabo.VR.Vehicle.WheelAngle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRVehicleWheelAngleTest::RunTest(const FString& Parameters)
{
	using namespace TNVRVehicleTest;
	using namespace TNVRVehicle;
	const FWheelFrame Wheel = ATN_Buggy::GetWheelFrame();
	const FWheelTuning Tuning;

	// El volante del buggy: su derecha es la del chasis y su eje mira a la conductora.
	TestTrue(TEXT("la derecha del volante es la del buggy (+Y)"), Wheel.Right().Equals(FVector(0.0, 1.0, 0.0), 1e-3));
	TestTrue(TEXT("el eje del volante mira hacia la conductora (atrás y arriba)"), Wheel.Axis.X < 0.0 && Wheel.Axis.Z > 0.0);

	// Ángulos sueltos.
	bool bValid = false;
	TestEqual(TEXT("una mano a las 3: 90°"), HandAngleDeg(OnRim(Wheel, 90.0), Wheel, 3.0, bValid), 90.0, 1e-3);
	TestEqual(TEXT("una mano a las 9: -90°"), HandAngleDeg(OnRim(Wheel, -90.0), Wheel, 3.0, bValid), -90.0, 1e-3);
	HandAngleDeg(Wheel.Center + Wheel.Right() * 1.0, Wheel, 3.0, bValid);
	TestFalse(TEXT("una mano casi en el centro no da ángulo"), bValid);
	for (const double Degrees : { 0.0, 30.0, -45.0, 90.0 })
	{
		FVector Left;
		FVector Right;
		HandsOnWheel(Wheel, Degrees, Left, Right);
		TestEqual(*FString::Printf(TEXT("dos manos giradas %.0f°: la recta entre ellas da %.0f°"), Degrees, Degrees),
			TwoHandAngleDeg(Left, Right, Wheel, 6.0, bValid), Degrees, 1e-3);
		TestTrue(TEXT("las manos simuladas están en el aro"), DistanceToRim(Left, Wheel) < 1e-3 && DistanceToRim(Right, Wheel) < 1e-3);
	}
	// Una mano fuera del plano del volante (más cerca o más lejos) da el mismo ángulo y su punto del aro es el suyo.
	const FVector Off = OnRim(Wheel, 60.0) + Wheel.Axis * 8.0;
	TestEqual(TEXT("fuera del plano, el mismo ángulo"), HandAngleDeg(Off, Wheel, 3.0, bValid), 60.0, 1e-3);
	TestTrue(TEXT("su punto del aro, a 8 cm"), FMath::IsNearlyEqual(DistanceToRim(Off, Wheel), 8.0, 1e-3));

	// Coger el volante recto con las dos manos no lo mueve; girarlas 30° lo gira 30° y la dirección llega a 30/90.
	FWheelState State;
	FVector Left;
	FVector Right;
	HandsOnWheel(Wheel, 0.0, Left, Right);
	State = Hold(State, true, true, Left, Right, Wheel, 3);
	TestEqual(TEXT("cogido recto: 0°"), State.WheelDeg, 0.0, 1e-6);
	State = TurnBoth(State, Wheel, 0.0, 30.0, 30);
	TestEqual(TEXT("girado 30° con las dos manos"), State.WheelDeg, 30.0, 1e-3);
	HandsOnWheel(Wheel, 30.0, Left, Right);
	State = Hold(State, true, true, Left, Right, Wheel, 90);
	TestEqual(TEXT("dirección suavizada hasta 30/90"), State.Steer, 30.f / 90.f, 1e-3f);

	// Soltar la derecha no hace saltar el volante; la izquierda sola sigue girándolo.
	State = Hold(State, true, false, Left, Right, Wheel, 2);
	TestEqual(TEXT("al soltar una mano, sin salto"), State.WheelDeg, 30.0, 1e-3);
	const double LeftDeg = HandAngleDeg(Left, Wheel, 3.0, bValid);
	State = Hold(State, true, false, OnRim(Wheel, LeftDeg + 20.0), Right, Wheel, 1);
	TestEqual(TEXT("una mano sola gira 20° más"), State.WheelDeg, 50.0, 1e-3);
	// Volver a coger con la derecha en otro sitio tampoco lo mueve.
	State = Hold(State, true, true, OnRim(Wheel, LeftDeg + 20.0), OnRim(Wheel, 150.0), Wheel, 2);
	TestEqual(TEXT("al coger con la otra mano, sin salto"), State.WheelDeg, 50.0, 1e-3);

	// Tope a ±90° y, de vuelta, cuenta desde el tope (el giro de más no se acumula).
	FWheelState Limited;
	Limited = TurnBoth(Limited, Wheel, 0.0, 0.0, 1);
	Limited = TurnBoth(Limited, Wheel, 0.0, 120.0, 60);
	TestEqual(TEXT("tope del volante"), Limited.WheelDeg, Tuning.MaxWheelDeg, 1e-3);
	Limited = TurnBoth(Limited, Wheel, 120.0, 100.0, 10);
	TestEqual(TEXT("de vuelta desde el tope"), Limited.WheelDeg, 70.0, 1e-3);
	TestEqual(TEXT("dirección a tope = 1"), SteerFromWheel(Tuning.MaxWheelDeg, Tuning.MaxWheelDeg), 1.f);
	TestEqual(TEXT("dirección a la izquierda"), SteerFromWheel(-45.0, Tuning.MaxWheelDeg), -0.5f);

	// Una mano que pasa por abajo (de 170° a -170°) sigue girando hacia el mismo lado (sin dar la vuelta).
	FWheelState Wrap;
	Wrap.WheelDeg = 0.0;
	Wrap = Hold(Wrap, false, true, Left, OnRim(Wheel, 170.0), Wheel, 1);
	Wrap = Hold(Wrap, false, true, Left, OnRim(Wheel, -170.0), Wheel, 1);
	TestEqual(TEXT("pasar por las 6 suma 20°"), Wrap.WheelDeg, 20.0, 1e-3);

	// Sin manos vuelve al centro (270°/s).
	FWheelState Free = State;
	Free = Hold(Free, false, false, Left, Right, Wheel, 9);
	TestEqual(TEXT("sin manos vuelve hacia el centro"), Free.WheelDeg, 50.0 - Tuning.ReturnDegPerSec * 9.0 * Dt, 1e-3);
	Free = Hold(Free, false, false, Left, Right, Wheel, 200);
	TestEqual(TEXT("y se queda recto"), Free.WheelDeg, 0.0, 1e-6);

	// Una mano casi en el centro no mueve el volante.
	FWheelState Center;
	Center = Hold(Center, true, false, OnRim(Wheel, -90.0), Right, Wheel, 1);
	Center = Hold(Center, true, false, Wheel.Center + Wheel.Up * 0.5, Right, Wheel, 1);
	TestEqual(TEXT("mano en el centro: sin giro"), Center.WheelDeg, 0.0, 1e-6);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Apuntado con la mano
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRVehicleHandAimTest,
	"Tortunabo.VR.Vehicle.HandAim",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRVehicleHandAimTest::RunTest(const FString& Parameters)
{
	using namespace TNVRVehicle;
	const FQuat Straight = FQuat::Identity;
	const FQuat Turned = FRotator(0.0, 90.0, 0.0).Quaternion();

	FRotator Aim = AimFromHands(Straight, FVector(1.0, 0.0, 0.0));
	TestTrue(TEXT("buggy recto, mano al frente: al frente"), Aim.Equals(FRotator::ZeroRotator, 1e-3));
	Aim = AimFromHands(Turned, FVector(1.0, 0.0, 0.0));
	TestEqual(TEXT("buggy girado 90°, mano a +X del mundo: 90° a su izquierda"), Aim.Yaw, -90.0, 1e-3);
	Aim = AimFromHands(Straight, FRotator(20.0, 135.0, 0.0).Vector());
	TestTrue(TEXT("hacia atrás y arriba: tal cual"), FMath::IsNearlyEqual(Aim.Yaw, 135.0, 1e-3) && FMath::IsNearlyEqual(Aim.Pitch, 20.0, 1e-3));
	Aim = AimFromHands(Straight, FRotator(-35.0, 10.0, 0.0).Vector());
	TestEqual(TEXT("mano hacia abajo: cabeceo limitado a -10 (ClampAim)"), Aim.Pitch, static_cast<double>(TNRallyTurret::MinPitchDeg), 1e-3);
	Aim = AimFromHands(Straight, FRotator(70.0, -30.0, 0.0).Vector());
	TestEqual(TEXT("mano muy arriba: cabeceo limitado a 45"), Aim.Pitch, static_cast<double>(TNRallyTurret::MaxPitchDeg), 1e-3);
	TestEqual(TEXT("y la guiñada se queda"), Aim.Yaw, -30.0, 1e-3);
	// Buggy en cuesta (morro arriba 20°): la mano horizontal apunta 20° por debajo de su eje, limitado a -10.
	Aim = AimFromHands(FRotator(20.0, 0.0, 0.0).Quaternion(), FVector(1.0, 0.0, 0.0));
	TestEqual(TEXT("en cuesta, en los ejes del buggy"), Aim.Pitch, static_cast<double>(TNRallyTurret::MinPitchDeg), 1e-3);
	TestTrue(TEXT("siempre finito"), TNRallyTurret::IsAimFinite(Aim.Yaw, Aim.Pitch));
	TestTrue(TEXT("dirección nula: al frente"), AimFromHands(Straight, FVector::ZeroVector).Equals(FRotator::ZeroRotator));

	// Dos manos: la media; una: la suya; opuestas: la derecha.
	const FVector A = FRotator(0.0, 20.0, 0.0).Vector();
	const FVector B = FRotator(0.0, -20.0, 0.0).Vector();
	TestTrue(TEXT("dos manos a ±20°: al frente"), AverageAimDir(A, true, B, true).Equals(FVector(1.0, 0.0, 0.0), 1e-3));
	TestTrue(TEXT("solo la izquierda"), AverageAimDir(A, true, B, false).Equals(A, 1e-3));
	TestTrue(TEXT("opuestas: la derecha"), AverageAimDir(FVector(1.0, 0.0, 0.0), true, FVector(-1.0, 0.0, 0.0), true).Equals(FVector(-1.0, 0.0, 0.0), 1e-3));
	TestTrue(TEXT("ninguna: nula"), AverageAimDir(A, false, B, false).IsZero());
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Inclinación con la cabeza
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRVehicleHeadLeanTest,
	"Tortunabo.VR.Vehicle.HeadLean",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRVehicleHeadLeanTest::RunTest(const FString& Parameters)
{
	using namespace TNVRVehicle;
	TestEqual(TEXT("cabeza en el centro: nada"), LeanFromHead(0.0), 0.f);
	TestEqual(TEXT("3 cm: dentro de la zona muerta"), LeanFromHead(3.0), 0.f);
	TestEqual(TEXT("12 cm a la derecha: media inclinación"), LeanFromHead(12.0), 0.5f, 1e-4f);
	TestEqual(TEXT("12 cm a la izquierda: media hacia la izquierda"), LeanFromHead(-12.0), -0.5f, 1e-4f);
	TestEqual(TEXT("20 cm: a tope"), LeanFromHead(20.0), 1.f, 1e-4f);
	TestEqual(TEXT("40 cm: a tope, sin pasarse"), LeanFromHead(40.0), 1.f);
	TestEqual(TEXT("stick y cabeza se suman"), CombineLean(-0.3f, 0.5f), 0.2f, 1e-4f);
	TestEqual(TEXT("y se limitan"), CombineLean(0.8f, 0.5f), 1.f);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Volante y asas a mano desde los ojos
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRVehicleReachTest,
	"Tortunabo.VR.Vehicle.Reach",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRVehicleReachTest::RunTest(const FString& Parameters)
{
	// La conductora: el volante, delante y por debajo de los ojos, a un brazo.
	const TNVRVehicle::FWheelFrame Wheel = ATN_Buggy::GetWheelFrame();
	const FVector DriverEye = ATN_Buggy::DriverSeatLocal + UTN_VRSeatComponent::EyeAboveHip;
	const FVector ToWheel = Wheel.Center - DriverEye;
	TestTrue(*FString::Printf(TEXT("volante delante de los ojos (%.0f cm)"), ToWheel.X), ToWheel.X > 20.0 && ToWheel.X < 60.0);
	TestTrue(*FString::Printf(TEXT("volante por debajo de los ojos (%.0f cm)"), ToWheel.Z), ToWheel.Z < -10.0 && ToWheel.Z > -50.0);

	// La artillera: los puños, delante y por debajo de sus ojos, a un brazo y a los dos lados (ejes del carro sin giro).
	const FVector GunnerEye = FVector(0.0, 0.0, TNBuggyTurretMesh::SeatZ) + UTN_VRSeatComponent::EyeAboveHip;
	for (const bool bRight : { false, true })
	{
		const FVector Grip = TNBuggyTurretMesh::HandleGripPoint(bRight);
		const FVector ToGrip = Grip - GunnerEye;
		TestTrue(*FString::Printf(TEXT("puño %s delante de los ojos (%.0f cm)"), bRight ? TEXT("derecho") : TEXT("izquierdo"), ToGrip.X),
			ToGrip.X > 20.0 && ToGrip.X < 60.0);
		TestTrue(TEXT("y por debajo"), ToGrip.Z < -10.0 && ToGrip.Z > -50.0);
		TestTrue(TEXT("a su lado"), bRight ? ToGrip.Y > 10.0 : ToGrip.Y < -10.0);
		// Por encima del aro y dentro de las barandillas al girar.
		TestTrue(TEXT("por encima del aro"), Grip.Z > TNBuggyTurretMesh::RingZ + 20.0);
	}
	TNProcMesh::FTNProcMeshBuffers Handles;
	TNBuggyTurretMesh::BuildHandles(Handles);
	TestTrue(TEXT("las asas tienen malla"), Handles.Verts.Num() > 0 && Handles.Normals.Num() == Handles.Verts.Num());
	return true;
}

#endif
