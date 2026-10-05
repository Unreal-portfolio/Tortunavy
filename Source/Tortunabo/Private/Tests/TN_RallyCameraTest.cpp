// Cámara de llegada, podio y espectador del Rally (#306): qué plano toca (con y sin VR), cuándo hay cámara lenta, el ciclo del
// espectador con el dron y la geometría del podio. Correr con "Automation RunTests Tortunabo.Rally.Camera".

#include "Kart/TN_KartBuggy.h"
#include "Misc/AutomationTest.h"
#include "Rally/TN_RallyCameraLogic.h"
#include "Rally/TN_RallyKartBuggy.h"
#include "Vehicles/TN_Buggy.h"

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
	TestEqual(TEXT("Calentamiento sin buggy: a un buggy de la parrilla (#756)"), DecideShot(Warmup), EShot::Spectate);
	Warmup.bSeated = true;
	TestEqual(TEXT("Calentamiento sentada: su cámara"), DecideShot(Warmup), EShot::Own);
	TNRallyCamera::FShotInput Countdown = Racing(true, false, -1.0, false);
	Countdown.Phase = ETNRallyPhase::Countdown;
	TestEqual(TEXT("Semáforo sentada: su cámara"), DecideShot(Countdown), EShot::Own);
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
	// Tres buggies corriendo (0..2) y el dron (3).
	TestEqual(TEXT("Siguiente del 0: el 1"), CycleSpectate(0, 1, 3, true), 1);
	TestEqual(TEXT("Siguiente del 2: el dron"), CycleSpectate(2, 1, 3, true), 3);
	TestEqual(TEXT("Siguiente del dron: el 0"), CycleSpectate(3, 1, 3, true), 0);
	TestEqual(TEXT("Anterior del 0: el dron"), CycleSpectate(0, -1, 3, true), 3);
	TestEqual(TEXT("VR, anterior del 0: el 2 (sin dron)"), CycleSpectate(0, -1, 3, false), 2);
	TestEqual(TEXT("Nadie corriendo y sin dron: nada"), CycleSpectate(0, 1, 0, false), static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCameraSpectateFollowTest, "Tortunabo.Rally.Camera.SpectateFollow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyCameraSpectateFollowTest::RunTest(const FString& Parameters)
{
	using TNRallyCamera::FSpectatePick;
	using TNRallyCamera::FollowSpectate;
	using TNRallyCamera::StepSpectate;

	// #781: con cuatro equipos en carrera (anfitrión, cliente y dos bots; el espectador ya llegó), pulsar «siguiente» pasa
	// por todos y por el dron, y vuelve al primero.
	const TArray<int32> Racing = { 4, 0, 2, 1 };
	FSpectatePick Pick = FollowSpectate(Racing, FSpectatePick(), true);
	TestEqual(TEXT("Al entrar: el líder"), Pick.Team, 4);
	TArray<int32> Seen = { Pick.Team };
	for (int32 Press = 0; Press < Racing.Num(); ++Press)
	{
		Pick = StepSpectate(Racing, Pick, 1, true);
		Seen.Add(Pick.bDrone ? INDEX_NONE : Pick.Team);
	}
	TestTrue(TEXT("Recorre los cuatro equipos y el dron"), Seen == TArray<int32>{ 4, 0, 2, 1, INDEX_NONE });
	TestEqual(TEXT("Del dron, al líder"), StepSpectate(Racing, Pick, 1, true).Team, 4);
	TestEqual(TEXT("Del líder hacia atrás, al dron"), StepSpectate(Racing, FollowSpectate(Racing, FSpectatePick(), true), -1, true).bDrone,
		true);

	// Se sigue al mismo equipo aunque adelante o le adelanten.
	FSpectatePick OnTeam2 = FollowSpectate(Racing, FSpectatePick{ 2, 2, false }, true);
	const TArray<int32> Overtaken = { 2, 4, 0, 1 };
	OnTeam2 = FollowSpectate(Overtaken, OnTeam2, true);
	TestEqual(TEXT("Adelanta: el mismo equipo"), OnTeam2.Team, 2);
	TestEqual(TEXT("Adelanta: su nuevo puesto"), OnTeam2.Slot, 0);
	TestEqual(TEXT("Siguiente del equipo seguido tras adelantar"), StepSpectate(Overtaken, OnTeam2, 1, true).Team, 4);

	// El dron sigue siendo el dron aunque llegue alguien (antes saltaba al líder al bajar cuántos corren).
	FSpectatePick Drone = StepSpectate(Racing, FSpectatePick{ 3, 1, false }, 1, true);
	TestTrue(TEXT("Del último, al dron"), Drone.bDrone);
	Drone = FollowSpectate(TArray<int32>{ 4, 0, 1 }, Drone, true);
	TestTrue(TEXT("Llega uno: sigue el dron"), Drone.bDrone);
	TestEqual(TEXT("Llega uno: hueco del dron al final"), Drone.Slot, 3);

	// El equipo seguido llega a meta: el mismo hueco, o el primero si era el último.
	TestEqual(TEXT("Llega el seguido: el del mismo hueco"), FollowSpectate(TArray<int32>{ 4, 2, 1 }, FSpectatePick{ 1, 0, false }, true).Team, 2);
	TestEqual(TEXT("Llega el último seguido: el líder"), FollowSpectate(TArray<int32>{ 4, 0, 2 }, FSpectatePick{ 3, 1, false }, true).Team, 4);

	// Sin dron (VR) y sin nadie corriendo.
	TestEqual(TEXT("VR: del último, al primero"), StepSpectate(Racing, FSpectatePick{ 3, 1, false }, 1, false).Team, 4);
	TestFalse(TEXT("VR: un dron anterior pasa a un buggy"), FollowSpectate(Racing, Drone, false).bDrone);
	TestFalse(TEXT("Nadie corriendo: nada que mirar"), FollowSpectate(TArray<int32>(), Drone, true).HasTarget());
	TestFalse(TEXT("Nadie corriendo: siguiente tampoco"), StepSpectate(TArray<int32>(), FSpectatePick(), 1, true).HasTarget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCameraSpectateRelevancyTest, "Tortunabo.Rally.Camera.SpectateRelevancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyCameraSpectateRelevancyTest::RunTest(const FString& Parameters)
{
	// #781: el espectador solo puede seguir los buggies que existen en su máquina. Con la relevancia por distancia, en un
	// cliente un buggy a más de 150 m de su peón (o de la parrilla) no se replicaba y desaparecía de la lista.
	TestTrue(TEXT("Buggy del Rally siempre relevante"), GetDefault<ATN_RallyKartBuggy>()->bAlwaysRelevant);
	TestTrue(TEXT("Kart siempre relevante"), GetDefault<ATN_KartBuggy>()->bAlwaysRelevant);
	TestTrue(TEXT("Buggy base siempre relevante"), GetDefault<ATN_Buggy>()->bAlwaysRelevant);
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
