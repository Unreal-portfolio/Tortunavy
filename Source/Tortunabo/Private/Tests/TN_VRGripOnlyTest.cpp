// Coger con la aleta (#916, VR/TN_VRHandMath.h): con gafas y los mandos con seguimiento, lo del suelo se coge con el agarre
// acercando la aleta y no con el gatillo como en tercera persona. Lógica pura, sin gafas.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.VR; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "VR/TN_VRHandMath.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRGripOnlyTest,
	"Tortunabo.VR.GripOnlyTakes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRGripOnlyTest::RunTest(const FString& Parameters)
{
	// Con gafas y un mando con seguimiento, el gatillo no coge (lo coge el agarre, acercando la aleta).
	TestTrue(TEXT("Gafas, mando con seguimiento, gatillo: no coge"), TNVRHands::GripOnlyTakes(true, false, true));
	TestFalse(TEXT("El agarre sí coge"), TNVRHands::GripOnlyTakes(true, true, true));
	// Sin mandos con seguimiento (apagados), lo de siempre: no se queda sin poder coger nada.
	TestFalse(TEXT("Gafas sin mandos con seguimiento: vale el gatillo"), TNVRHands::GripOnlyTakes(true, false, false));
	// Sin gafas (tercera persona, primera persona o vista simulada), nada cambia.
	TestFalse(TEXT("Sin gafas: nada cambia"), TNVRHands::GripOnlyTakes(false, false, true));
	TestFalse(TEXT("Sin gafas y sin mandos: nada cambia"), TNVRHands::GripOnlyTakes(false, false, false));
	return true;
}

#endif
