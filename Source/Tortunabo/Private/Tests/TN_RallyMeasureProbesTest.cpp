// Comprobaciones del buggy del Rally que pedía la revisión de la PR #240, con física y sin ventana.
//  - RampHold: TN.Rally.RampHold 15 en un mundo vacío (#611: frenado en la parrilla, < 5 cm por la rampa en 5 s).
// TN.Rally.DebugTurretFit (#435) no se puede medir así: la pose sentada de la artillera solo se escribe si la malla se
// ha pintado (UTN_BuggyRiderAnimComponent::ApplyPose) y, sin ventana, mide a la tortuga de pie. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Measure; Quit" -nullrhi -unattended -NoSteam

#include "Misc/AutomationTest.h"
#include "TN_RallyPhysicsTestKit.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyMeasureRampHoldTest, "Tortunabo.Rally.Measure.RampHold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyMeasureRampHoldTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyPhysicsMeasure;
	FPhysicsWorld Test(TEXT("TNRallyRampHoldWorld"));
	if (!TestNotNull(TEXT("Mundo de prueba"), Test.World))
	{
		return false;
	}
	FLineCapture Capture(TEXT("[Rampa]"));
	IConsoleManager::Get().ProcessUserConsoleInput(TEXT("TN.Rally.RampHold 15"), *GLog, Test.World);
	// Asentado 3 s, frenado 5 s y suelto 2 s.
	Test.Advance(10.5f);
	const TArray<FString> Lines = Capture.Take();
	const FString* Result = Lines.FindByPredicate([](const FString& Line) { return Line.Contains(TEXT("frenado se desplaza")); });
	if (!TestNotNull(TEXT("TN.Rally.RampHold da resultado"), Result))
	{
		return false;
	}
	AddInfo(*Result);
	TestTrue(TEXT("en la rampa de 15 grados, frenado, se desplaza menos de 5 cm por la rampa"), Result->Contains(TEXT("BIEN")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
