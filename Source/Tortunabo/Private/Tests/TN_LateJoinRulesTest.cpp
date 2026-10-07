// Reglas puras de reconexión (TN_LateJoinRules.h, #345). Sin mundo ni actores. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Net.Reconnect; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Game/TN_LateJoinRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNReconnectRulesTest,
	"Tortunabo.Net.Reconnect",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNReconnectRulesTest::RunTest(const FString& Parameters)
{
	// Derribado al irse = muerto al volver; vivo sigue vivo.
	FTNReconnectState Downed;
	Downed.bIsDBNO = true;
	const FTNReconnectState Saved = TNLateJoinLogic::SanitizeForReconnect(Downed);
	TestFalse(TEXT("DBNO al irse: vuelve muerto"), Saved.bIsAlive);
	TestFalse(TEXT("DBNO al irse: sin derribo"), Saved.bIsDBNO);
	TestTrue(TEXT("Vivo al irse: vuelve vivo"), TNLateJoinLogic::SanitizeForReconnect(FTNReconnectState()).bIsAlive);
	return true;
}

#endif
