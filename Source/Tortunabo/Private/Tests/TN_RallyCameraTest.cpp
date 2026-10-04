// Cámara de llegada, podio y espectador del Rally (#306): qué plano toca (con y sin VR), cuándo hay cámara lenta, el ciclo del
// espectador con el dron y la geometría del podio. Correr con "Automation RunTests Tortunabo.Rally.Camera".

#include "Misc/AutomationTest.h"
#include "Rally/TN_RallyCameraLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyCameraTest
{
	using TNRallyCamera::EShot;

	TNRallyCamera::FShotInput Racing(bool bSeated, bool bFinished, double Since, bool bVR)
	{
		TNRallyCamera::FShotInput In;
		In.Phase = ETNRallyPhase::Racing;
		In.bSeated = bSeated;
		In.bFinished = bFinished;
		In.SecondsSinceFinish = Since;
		In.bVR = bVR;
		return In;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCameraFlatShotsTest, "Tortunabo.Rally.Camera.FinishShotsFlat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyCameraFlatShotsTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCameraTest;
	using TNRallyCamera::DecideShot;
	TestEqual(TEXT("Corriendo: su cámara"), DecideShot(Racing(true, false, -1.0, false)), EShot::Own);
	TestEqual(TEXT("Recién llegada: plano lateral"), DecideShot(Racing(true, true, 0.2, false)), EShot::FinishSide);
	TestEqual(TEXT("A 1,4 s: aún el lateral"), DecideShot(Racing(true, true, 1.4, false)), EShot::FinishSide);
	TestEqual(TEXT("A 1,6 s: el podio"), DecideShot(Racing(true, true, 1.6, false)), EShot::Podium);
	TestEqual(TEXT("Pasado el podio: espectador"), DecideShot(Racing(true, true, TNRallyCamera::FinishSideSeconds
		+ TNRallyCamera::PodiumHoldSeconds + 0.1, false)), EShot::Spectate);
	TestEqual(TEXT("Llegó sin verlo (entró tarde): espectador"), DecideShot(Racing(true, true, -1.0, false)), EShot::Spectate);
	TestEqual(TEXT("Esperando sin buggy: espectador"), DecideShot(Racing(false, false, -1.0, false)), EShot::Spectate);
	TNRallyCamera::FShotInput Results = Racing(true, false, -1.0, false);
	Results.Phase = ETNRallyPhase::Results;
	TestEqual(TEXT("Resultados: el podio"), DecideShot(Results), EShot::Podium);
	TNRallyCamera::FShotInput Warmup = Racing(false, false, -1.0, false);
	Warmup.Phase = ETNRallyPhase::Warmup;
	TestEqual(TEXT("Calentamiento: su cámara"), DecideShot(Warmup), EShot::Own);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCameraVRShotsTest, "Tortunabo.Rally.Camera.FinishShotsVR",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyCameraVRShotsTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCameraTest;
	using TNRallyCamera::DecideShot;
	// VR: corte seco al podio fijo, sin plano lateral ni cámara lenta.
	TestEqual(TEXT("VR, recién llegada: podio directamente"), DecideShot(Racing(true, true, 0.0, true)), EShot::Podium);
	TestEqual(TEXT("VR, a 1 s: podio"), DecideShot(Racing(true, true, 1.0, true)), EShot::Podium);
	TestEqual(TEXT("VR, pasado el podio: espectador"), DecideShot(Racing(true, true, TNRallyCamera::PodiumHoldSeconds + 0.1, true)),
		EShot::Spectate);
	TestFalse(TEXT("VR: nunca cámara lenta"), TNRallyCamera::CanUseSlowMotion(true, true, 1));
	TestTrue(TEXT("Sola en el PC: cámara lenta"), TNRallyCamera::CanUseSlowMotion(false, true, 1));
	TestTrue(TEXT("Anfitriona sin más jugadores (con bots): cámara lenta"), TNRallyCamera::CanUseSlowMotion(false, false, 1));
	TestFalse(TEXT("Con otra jugadora: sin cámara lenta"), TNRallyCamera::CanUseSlowMotion(false, false, 2));
	// Sin VR la cámara del podio gira; con VR, fija.
	const TNRallyCamera::FPodiumFrame Frame = TNRallyCamera::PodiumFrameFromFinish(FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("VR: podio fijo"), TNRallyCamera::PodiumCameraLocation(Frame, true, 0.0)
		.Equals(TNRallyCamera::PodiumCameraLocation(Frame, true, 7.0), 0.1));
	TestFalse(TEXT("Sin VR: travelling"), TNRallyCamera::PodiumCameraLocation(Frame, false, 0.0)
		.Equals(TNRallyCamera::PodiumCameraLocation(Frame, false, 3.0), 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCameraSpectateTest, "Tortunabo.Rally.Camera.SpectateCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyCameraSpectateTest::RunTest(const FString& Parameters)
{
	using TNRallyCamera::CycleSpectate;
	using TNRallyCamera::ClampSpectate;
	// Tres buggies corriendo (0..2) y el dron (3).
	TestEqual(TEXT("Siguiente del 0: el 1"), CycleSpectate(0, 1, 3, true), 1);
	TestEqual(TEXT("Siguiente del 2: el dron"), CycleSpectate(2, 1, 3, true), 3);
	TestEqual(TEXT("Siguiente del dron: el 0"), CycleSpectate(3, 1, 3, true), 0);
	TestEqual(TEXT("Anterior del 0: el dron"), CycleSpectate(0, -1, 3, true), 3);
	TestEqual(TEXT("VR, anterior del 0: el 2 (sin dron)"), CycleSpectate(0, -1, 3, false), 2);
	TestEqual(TEXT("Nadie corriendo y sin dron: nada"), CycleSpectate(0, 1, 0, false), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Nadie corriendo: el dron"), ClampSpectate(2, 0, true), 0);
	TestEqual(TEXT("El que se miraba ya no corre: el primero"), ClampSpectate(5, 2, true), 0);
	TestEqual(TEXT("Sigue valiendo: el mismo"), ClampSpectate(1, 2, true), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCameraPodiumTest, "Tortunabo.Rally.Camera.PodiumLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyCameraPodiumTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCamera;
	const FVector Finish(1000.0, 2000.0, 300.0);
	const FPodiumFrame Frame = PodiumFrameFromFinish(Finish, FVector(0.0, 1.0, 0.0));
	TestTrue(TEXT("Flota sobre la meta"), FMath::IsNearlyEqual(Frame.Origin.Z, Finish.Z + PodiumHeightCm));
	TestTrue(TEXT("Mira contra el sentido de la carrera"), Frame.Facing.Equals(FVector(0.0, -1.0, 0.0), 1e-4));
	const FVector First = PodiumSlotLocation(Frame, 1);
	const FVector Second = PodiumSlotLocation(Frame, 2);
	const FVector Third = PodiumSlotLocation(Frame, 3);
	TestTrue(TEXT("El 1.º, el más alto"), First.Z > Second.Z && Second.Z > Third.Z);
	TestTrue(TEXT("2.º y 3.º a los lados del 1.º"), FMath::IsNearlyEqual(FVector::Dist2D(First, Second), PodiumStepWidthCm, 1.0)
		&& FMath::IsNearlyEqual(FVector::Dist2D(First, Third), PodiumStepWidthCm, 1.0));
	// Desde la cámara del podio, el 2.º queda a la izquierda.
	const FVector Camera = PodiumCameraLocation(Frame, true, 0.0);
	const FVector CameraRight = FVector::CrossProduct(FVector::UpVector, (Frame.Origin - Camera).GetSafeNormal2D());
	TestTrue(TEXT("2.º a la izquierda de la cámara"), FVector::DotProduct(Second - First, CameraRight) < 0.0);
	const FVector Fourth = PodiumSlotLocation(Frame, 4);
	TestTrue(TEXT("El 4.º, detrás de los escalones y en la losa"), FVector::DotProduct(Fourth - Frame.Origin, Frame.Facing) < 0.0
		&& FMath::IsNearlyEqual(Fourth.Z, Frame.Origin.Z));
	TestTrue(TEXT("La fila de detrás cabe en la losa"), FVector::DotProduct(Frame.Origin - PodiumSlotLocation(Frame, 8), Frame.Facing)
		< PodiumSlabSizeCm.X);
	TestEqual(TEXT("Sin escalón del 4.º en adelante"), PodiumStepHeightCm(5), 0.0);
	const FVector Side = FinishSideLocation(FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("Plano lateral: delante, a un lado y por encima"), Side.X > 0.0 && FMath::Abs(Side.Y) > 300.0 && Side.Z > 0.0);
	const FVector Drone = DroneLocation(FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("Dron: detrás y por encima del líder"), Drone.X < 0.0 && Drone.Z > 500.0);
	TestTrue(TEXT("El dron salta si el líder reaparece lejos"), SmoothFollow(FVector::ZeroVector, FVector(1e6, 0.0, 0.0), 0.016f, 2.5f)
		.Equals(FVector(1e6, 0.0, 0.0)));
	return true;
}

#endif
