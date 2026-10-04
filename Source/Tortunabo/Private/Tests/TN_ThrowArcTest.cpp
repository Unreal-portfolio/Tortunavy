// Arco de los lanzamientos al punto de mira (issue #264): lógica pura de TN_ThrowArc.h, la misma que usa
// ATortugaCharacter::GetThrowDirectionToCrosshair. Correr desde Session Frontend (categoría "Tortunabo.ThrowArc") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.ThrowArc; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_ThrowArc.h"
#include "Player/TN_ShellBody.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNThrowArcReachesTargetTest,
	"Tortunabo.ThrowArc.ReachesTarget",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNThrowArcReachesTargetTest::RunTest(const FString& Parameters)
{
	constexpr double G = 980.0;
	constexpr double Speed = 1800.0;
	const double Dampings[] = {0.0, static_cast<double>(ATN_ShellBody::BoxLinearDamping)};
	const double Distances[] = {300.0, 800.0, 1500.0};
	const double Heights[] = {-200.0, 0.0, 150.0};
	for (const double Damping : Dampings)
	{
		for (const double Dist : Distances)
		{
			for (const double DeltaZ : Heights)
			{
				const double Theta = TNThrowArc::LaunchPitch(Dist, DeltaZ, Speed, G, Damping);
				double Height = 0.0;
				const bool bReaches = TNThrowArc::HeightAtDistance(Dist, Speed, Theta, G, Damping, Height);
				TestTrue(FString::Printf(TEXT("Llega (D=%.0f, Z=%.0f, C=%.2f)"), Dist, DeltaZ, Damping), bReaches);
				TestTrue(FString::Printf(TEXT("Cae en el punto (D=%.0f, Z=%.0f, C=%.2f): %.1f cm"), Dist, DeltaZ, Damping, Height),
					FMath::IsNearlyEqual(Height, DeltaZ, 5.0));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNThrowArcDampingTest,
	"Tortunabo.ThrowArc.DampingNeedsMoreAngle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNThrowArcDampingTest::RunTest(const FString& Parameters)
{
	// La concha frena en el aire: con el ángulo sin amortiguación se queda corta (el ~15 % de la revisión), y el
	// ángulo compensado es mayor.
	constexpr double G = 980.0;
	constexpr double Speed = 1800.0;
	constexpr double Dist = 1200.0;
	const double C = ATN_ShellBody::BoxLinearDamping;
	const double Plain = TNThrowArc::LaunchPitch(Dist, 0.0, Speed, G, 0.0);
	double Height = 0.0;
	TestTrue(TEXT("Con el ángulo sin compensar llega"), TNThrowArc::HeightAtDistance(Dist, Speed, Plain, G, C, Height));
	TestTrue(FString::Printf(TEXT("Sin compensar cae por debajo del punto (%.1f cm)"), Height), Height < -10.0);
	TestTrue(TEXT("Compensado, el ángulo es mayor"), TNThrowArc::LaunchPitch(Dist, 0.0, Speed, G, C) > Plain);

	// Sin alcance: no se rompe y sale un ángulo razonable.
	const double Far = TNThrowArc::LaunchPitch(100000.0, 0.0, Speed, G, C);
	TestTrue(TEXT("Sin alcance, un ángulo entre 0° y 45°"), Far > 0.0 && Far <= PI / 4.0 + KINDA_SMALL_NUMBER);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
