// Fallos de red en el anfitrión (#657): un invitado colgado (ConnectionTimeout en su conexión del servidor) cerraba la partida de
// todos. Se testea la decisión de TN_NetworkFailureDecisions.h que usa UMP_GameInstance::OnNetworkFailure: con un driver de
// servidor, solo sale ese invitado; en un invitado, la vuelta al menú con su motivo sigue igual.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Multiplayer.NetworkFailure; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Multiplayer/TN_NetworkFailureDecisions.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNNetworkFailureGuestOnHostTest,
	"Tortunabo.Multiplayer.NetworkFailure.GuestOnHost",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNNetworkFailureGuestOnHostTest::RunTest(const FString& Parameters)
{
	using namespace TNNetFailure;

	const ENetworkFailure::Type ConnectionFailures[] = {
		ENetworkFailure::ConnectionTimeout, ENetworkFailure::ConnectionLost, ENetworkFailure::FailureReceived,
		ENetworkFailure::PendingConnectionFailure, ENetworkFailure::NetChecksumMismatch, ENetworkFailure::NetGuidMismatch };

	for (const ENetworkFailure::Type Failure : ConnectionFailures)
	{
		const FString Name = ENetworkFailure::ToString(Failure);
		// El caso de la QA del 04-10: un invitado colgado da ConnectionTimeout en el driver del anfitrión.
		TestTrue(FString::Printf(TEXT("%s en el anfitrión: solo sale el invitado"), *Name), IsGuestFailureOnHost(Failure, NM_ListenServer));
		TestTrue(FString::Printf(TEXT("%s en el dedicado: solo sale el invitado"), *Name), IsGuestFailureOnHost(Failure, NM_DedicatedServer));
		// En el invitado que pierde al anfitrión, todo sigue como antes (menú con su motivo o reconexión durante el viaje).
		TestFalse(FString::Printf(TEXT("%s en un invitado: lo trata como hasta ahora"), *Name), IsGuestFailureOnHost(Failure, NM_Client));
		// Sin driver no se sabe de quién es: como hasta ahora.
		TestFalse(FString::Printf(TEXT("%s sin driver: como hasta ahora"), *Name), IsGuestFailureOnHost(Failure, NM_Standalone));
	}

	// Los fallos del propio driver son del anfitrión (reintentar el listen o destruir la sesión zombi): no se ignoran.
	for (const ENetworkFailure::Type Failure : { ENetworkFailure::NetDriverListenFailure, ENetworkFailure::NetDriverCreateFailure,
		ENetworkFailure::NetDriverAlreadyExists })
	{
		const FString Name = ENetworkFailure::ToString(Failure);
		TestTrue(FString::Printf(TEXT("%s es un fallo del driver"), *Name), IsDriverFailure(Failure));
		TestFalse(FString::Printf(TEXT("%s en el anfitrión no es de un invitado"), *Name), IsGuestFailureOnHost(Failure, NM_ListenServer));
	}
	TestFalse(TEXT("ConnectionTimeout no es un fallo del driver"), IsDriverFailure(ENetworkFailure::ConnectionTimeout));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
