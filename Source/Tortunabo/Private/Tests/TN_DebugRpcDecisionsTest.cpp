// RPC de pruebas (TNBooth): solo el anfitrión y nunca en Shipping.
// Se testea la función de TN_DebugRpcDecisions.h que usa AMP_GamePlayerController.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.DebugRpc; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_DebugRpcDecisions.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNDebugRpcHostOnlyTest,
	"Tortunabo.DebugRpc.HostOnly",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNDebugRpcHostOnlyTest::RunTest(const FString& Parameters)
{
	using namespace TNDebugRpcLogic;

	TestTrue(TEXT("Anfitrión en servidor escucha: se atiende"),
		CanRunHostOnlyDebugRpc(false, NM_ListenServer, true));
	TestFalse(TEXT("Invitado en servidor escucha: se rechaza"),
		CanRunHostOnlyDebugRpc(false, NM_ListenServer, false));
	TestTrue(TEXT("Standalone: se atiende"),
		CanRunHostOnlyDebugRpc(false, NM_Standalone, true));
	TestFalse(TEXT("Servidor dedicado: nadie es anfitrión"),
		CanRunHostOnlyDebugRpc(false, NM_DedicatedServer, false));
	TestFalse(TEXT("Shipping: ni el anfitrión"),
		CanRunHostOnlyDebugRpc(true, NM_ListenServer, true));
	TestFalse(TEXT("Shipping: ni en Standalone"),
		CanRunHostOnlyDebugRpc(true, NM_Standalone, true));
	TestFalse(TEXT("Esta build de tests no es Shipping"), IsShippingBuild());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
