// Cámara de la tortuga que se lleva un ave por el aire (#739, Player/TN_CarriedCamera.h): se aleja sin saltos al cogerla,
// vuelve sin saltos al soltarla y solo la zona de gaviotas (gaviotas y pelícano) cuenta como llevar por el aire.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Camera.Carried; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_CarriedCamera.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_BeachDragCrab.h"
#include "World/Beach/TN_BeachGullZone.h"
#include "World/Beach/TN_BeachLizard.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNCarriedCameraTest
{
	/** Ajuste de la cámara de la tortuga (propiedad protegida: por reflexión, el de la clase de C++). */
	float TurtleSetting(const TCHAR* Name)
	{
		const FFloatProperty* Property = FindFProperty<FFloatProperty>(ATortugaCharacter::StaticClass(), Name);
		return Property ? Property->GetPropertyValue_InContainer(GetDefault<ATortugaCharacter>()) : -1.f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCarriedCameraPullTest,
	"Tortunabo.Camera.Carried.SmoothPull",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCarriedCameraPullTest::RunTest(const FString& Parameters)
{
	using namespace TNCarriedCameraTest;
	const float Rise = TurtleSetting(TEXT("CameraCarriedRiseSeconds"));
	const float Return = TurtleSetting(TEXT("CameraCarriedReturnSeconds"));
	const float Extra = TurtleSetting(TEXT("CameraCarriedExtraArm"));
	if (!TestTrue(TEXT("Ajustes de la cámara llevada"), Rise > 0.f && Return > 0.f)) { return false; }
	TestTrue(TEXT("Se aleja: brazo y altura de más"), Extra > 100.f && TurtleSetting(TEXT("CameraCarriedLift")) > 0.f);

	TestEqual(TEXT("Sin tirón, nada"), TNCarriedCamera::Ease(0.f), 0.f);
	TestEqual(TEXT("Con el tirón entero, todo"), TNCarriedCamera::Ease(1.f), 1.f);
	TestTrue(TEXT("Arranca despacio (sin salto al cogerla)"), TNCarriedCamera::Ease(0.05f) < 0.01f);
	TestTrue(TEXT("Llega despacio (sin salto al acabar)"), TNCarriedCamera::Ease(0.95f) > 0.99f);

	// A 60 fps: el brazo de más nunca cambia más de lo que permite la curva (1,5 veces la pendiente media) en un fotograma.
	const float Dt = 1.f / 60.f;
	auto MaxStep = [Extra, Dt](float Seconds) { return Extra * 1.5f * Dt / Seconds + 0.01f; };
	float Pull = 0.f;
	float Arm = 0.f;
	float Time = 0.f;
	bool bSmooth = true;
	while (Pull < 1.f && Time < 10.f)
	{
		Pull = TNCarriedCamera::StepPull(Pull, true, Dt, Rise, Return);
		const float NewArm = Extra * TNCarriedCamera::Ease(Pull);
		bSmooth &= NewArm >= Arm && NewArm - Arm <= MaxStep(Rise);
		Arm = NewArm;
		Time += Dt;
	}
	TestTrue(FString::Printf(TEXT("Llevada: se aleja del todo en %.2f s (%.2f)"), Rise, Time), FMath::IsNearlyEqual(Time, Rise, 2.f * Dt));
	TestTrue(TEXT("Llevada: se aleja sin saltos y sin volver atrás"), bSmooth);

	// Soltada a medias (un pelotazo la libera enseguida): vuelve desde donde estaba, sin saltar.
	Pull = 0.f;
	Arm = 0.f;
	for (int32 Frame = 0; Frame < 20; ++Frame)
	{
		Pull = TNCarriedCamera::StepPull(Pull, true, Dt, Rise, Return);
	}
	Arm = Extra * TNCarriedCamera::Ease(Pull);
	const float ArmAtRelease = Arm;
	Time = 0.f;
	bSmooth = true;
	while (Pull > 0.f && Time < 10.f)
	{
		Pull = TNCarriedCamera::StepPull(Pull, false, Dt, Rise, Return);
		const float NewArm = Extra * TNCarriedCamera::Ease(Pull);
		bSmooth &= NewArm <= Arm && Arm - NewArm <= MaxStep(Return);
		Arm = NewArm;
		Time += Dt;
	}
	TestTrue(TEXT("Soltada a medias: vuelve sin saltos"), bSmooth && ArmAtRelease > 0.f);
	TestTrue(FString::Printf(TEXT("Soltada a medias: vuelve antes de %.2f s (%.2f)"), Return, Time), Time <= Return + Dt);
	TestEqual(TEXT("Soltada: la distancia de siempre"), Arm, 0.f);

	// Del todo y soltada: tarda lo que dice el ajuste.
	Pull = 1.f;
	Time = 0.f;
	while (Pull > 0.f && Time < 10.f)
	{
		Pull = TNCarriedCamera::StepPull(Pull, false, Dt, Rise, Return);
		Time += Dt;
	}
	TestTrue(FString::Printf(TEXT("Soltada: vuelve en %.2f s (%.2f)"), Return, Time), FMath::IsNearlyEqual(Time, Return, 2.f * Dt));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCarriedCameraWhoTest,
	"Tortunabo.Camera.Carried.OnlyThroughAir",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCarriedCameraWhoTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("La zona de gaviotas se la lleva por el aire"), GetDefault<ATN_BeachGullZone>()->CarriesHeldTurtleThroughAir());
	TestFalse(TEXT("El cangrejo que arrastra, no"), GetDefault<ATN_BeachDragCrab>()->CarriesHeldTurtleThroughAir());
	TestFalse(TEXT("El lagarto que la tiene en la boca, no"), GetDefault<ATN_BeachLizard>()->CarriesHeldTurtleThroughAir());
	TestFalse(TEXT("Sin tortuga, nadie la lleva"), ATN_BeachEnemy::IsTurtleCarriedThroughAir(nullptr));
	return true;
}

#endif
