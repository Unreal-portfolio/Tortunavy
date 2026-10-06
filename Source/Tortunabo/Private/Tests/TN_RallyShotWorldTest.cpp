// Salida del proyectil de la torreta con física y sin ventana (#717, «va donde quiere y no donde apunta»): un disparo lateral con
// el buggy en marcha vuela hacia donde se disparó (la lógica pura está en TN_RallyShotAimTest.cpp). Headless:
//   UnrealEditor-Win64-DebugGame-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Measure.LateralShot; Quit" -nullrhi -unattended -NoSteam -nosound

#include "Misc/AutomationTest.h"
#include "TN_RallyPhysicsTestKit.h"
#include "EngineUtils.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Vehicles/TN_RallyProjectile.h"
#include "Vehicles/TN_RallyTurretLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyShotWorldLateralTest, "Tortunabo.Rally.Measure.LateralShot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyShotWorldLateralTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyPhysicsMeasure;
	// Un disparo hacia la derecha con el buggy a 70 km/h: el proyectil vuela hacia la derecha, no 20-30 grados hacia delante.
	FPhysicsWorld Test(TEXT("TNRallyShotWorld"));
	if (!TestNotNull(TEXT("Mundo de prueba"), Test.World) || !TestNotNull(TEXT("Suelo llano"), SpawnFlatGround(*Test.World)))
	{
		return false;
	}
	ATN_Buggy* Buggy = SpawnBuggy(*Test.World, FTransform(FVector(-120000.0, 0.0, SpawnLiftCm)));
	if (!TestNotNull(TEXT("Buggy"), Buggy))
	{
		return false;
	}
	Settle(Test, *Buggy);
	for (int32 Step = 0; Step < 25 * StepsPerSecond && Kmh(*Buggy) < 70.f; ++Step)
	{
		Buggy->SetAIDriveInput(1.f, 0.f, HoldHeadingSteer(*Buggy), false);
		Test.Step();
	}
	if (!TestTrue(TEXT("llega a 70 km/h"), Kmh(*Buggy) >= 70.f))
	{
		return false;
	}
	const FVector Right = Buggy->GetActorRightVector();
	const FVector BuggyVelocity = Buggy->GetVelocity();
	if (!TestTrue(TEXT("la torreta dispara"), Buggy->GetTurret() && Buggy->GetTurret()->TryFire(false, Right)))
	{
		return false;
	}
	ATN_RallyProjectile* Projectile = nullptr;
	for (TActorIterator<ATN_RallyProjectile> It(Test.World); It; ++It)
	{
		Projectile = *It;
	}
	if (!TestNotNull(TEXT("sale un proyectil"), Projectile))
	{
		return false;
	}
	for (int32 Step = 0; Step < 3; ++Step)
	{
		Buggy->SetAIDriveInput(1.f, 0.f, HoldHeadingSteer(*Buggy), false);
		Test.Step();
	}
	const FVector A = Projectile->GetActorLocation();
	for (int32 Step = 0; Step < 6; ++Step)
	{
		Buggy->SetAIDriveInput(1.f, 0.f, HoldHeadingSteer(*Buggy), false);
		Test.Step();
	}
	const FVector Flight = (Projectile->GetActorLocation() - A).GetSafeNormal();
	const double ErrorDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Flight, Right), -1.0, 1.0)));
	const FVector Legacy = (Right * TNRallyTurret::SpecFor(ETNRallyAmmo::Coco).SpeedCms + BuggyVelocity).GetSafeNormal();
	const double LegacyErrorDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Legacy, Right), -1.0, 1.0)));
	AddInfo(FString::Printf(TEXT("Disparo lateral a %.0f km/h: el proyectil vuela a %.1f° de la mira (con la suma de antes, %.1f°)"),
		TNRally::CmsToKmh(BuggyVelocity.Size()), ErrorDeg, LegacyErrorDeg));
	TestTrue(*FString::Printf(TEXT("el proyectil vuela hacia donde se disparó (%.1f° de error)"), ErrorDeg), ErrorDeg < 3.0);
	TestTrue(TEXT("lo de antes lo desviaba más de 15°"), LegacyErrorDeg > 15.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
