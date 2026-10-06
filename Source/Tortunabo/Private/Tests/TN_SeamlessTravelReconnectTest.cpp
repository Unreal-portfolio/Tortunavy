// Viaje sin cortes y reconexión (#560): HQ y Run viajan sin cortes, y el motor en ese viaje solo emite
// FWorldDelegates::OnSeamlessTravelStart (nunca PreLoadMap). Sin escucharlo, bIsPendingTravel no se activaba y un corte de
// red durante la carga mandaba al invitado al menú («el anfitrión se ha ido») en vez de reconectarlo.
// Se emite el aviso real del motor y se comprueba la marca de viaje de UMP_GameInstance.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Multiplayer.SeamlessTravel; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Multiplayer/MP_GameInstance.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSeamlessTravelPendingTest,
	"Tortunabo.Multiplayer.SeamlessTravel.MarksPendingTravel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSeamlessTravelPendingTest::RunTest(const FString& Parameters)
{
	UMP_GameInstance* GameInstance = NewObject<UMP_GameInstance>(GetTransientPackage());
	UMP_GameInstance* OtherGameInstance = NewObject<UMP_GameInstance>(GetTransientPackage());
	UWorld* OwnWorld = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNSeamlessTravelOwnWorld"));
	UWorld* OtherWorld = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNSeamlessTravelOtherWorld"));
	if (!TestNotNull(TEXT("mundo propio"), OwnWorld) || !TestNotNull(TEXT("mundo ajeno"), OtherWorld))
	{
		return false;
	}
	OwnWorld->SetGameInstance(GameInstance);
	OtherWorld->SetGameInstance(OtherGameInstance);

	const FString MapName = TEXT("/Game/Maps/Run/LVL_Demo01");
	GameInstance->BindTravelDelegates();
	TestFalse(TEXT("sin viaje, no hay viaje pendiente"), GameInstance->IsPendingTravel());

	// Viaje sin cortes de otra GameInstance (PIE con varias ventanas): no es asunto de esta.
	FWorldDelegates::OnSeamlessTravelStart.Broadcast(OtherWorld, MapName);
	TestFalse(TEXT("el viaje de otra GameInstance no marca esta"), GameInstance->IsPendingTravel());

	// El caso del fallo: el viaje lobby→partida es sin cortes y solo emite OnSeamlessTravelStart.
	FWorldDelegates::OnSeamlessTravelStart.Broadcast(OwnWorld, MapName);
	TestTrue(TEXT("el viaje sin cortes propio marca bIsPendingTravel (reconexión automática)"), GameInstance->IsPendingTravel());

	// Tras Shutdown (UnbindTravelDelegates) ya no escucha.
	GameInstance->ClearPendingTravel();
	GameInstance->UnbindTravelDelegates();
	FWorldDelegates::OnSeamlessTravelStart.Broadcast(OwnWorld, MapName);
	TestFalse(TEXT("sin los avisos enganchados, no marca nada"), GameInstance->IsPendingTravel());

	OwnWorld->DestroyWorld(false);
	OtherWorld->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
