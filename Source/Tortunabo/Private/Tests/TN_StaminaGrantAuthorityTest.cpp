// Estamina ilimitada solo desde el servidor (#895): UTN_StaminaComponent no expone ningún RPC de servidor (antes
// ServerGrantUnlimitedStamina dejaba a cualquier cliente concedérsela sin gastar objeto) y GrantUnlimitedStamina no hace
// nada sin autoridad. Con autoridad, como al usar la barrita en ServerUseEquippedItem, se sigue concediendo.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Net.StaminaGrant; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TN_StaminaComponent.h"
#include "Player/TortugaCharacter.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNStaminaGrantAuthorityTest
{
	/** Mundo de juego con BeginPlay ya hecho: los actores que se crean después lo reciben al aparecer. */
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNStaminaGrantAuthorityTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
		}

		~FPlayWorld()
		{
			if (World->HasBegunPlay()) { World->EndPlay(EEndPlayReason::Quit); }
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	ATortugaCharacter* SpawnTurtle(UWorld* World, double X)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), FVector(X, 0.0, 200.0), FRotator::ZeroRotator, Params);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNStaminaNoServerRpcTest,
	"Tortunabo.Net.StaminaGrant.NoServerRpc",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNStaminaNoServerRpcTest::RunTest(const FString& Parameters)
{
	// Ningún cliente puede pedir nada al componente de estamina: el sprint viaja con los movimientos (#250) y los objetos
	// los aplica ServerUseEquippedItem.
	for (TFieldIterator<UFunction> It(UTN_StaminaComponent::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		TestFalse(FString::Printf(TEXT("%s no es un RPC de servidor"), *It->GetName()), It->HasAnyFunctionFlags(FUNC_NetServer));
	}
	TestNull(TEXT("ServerGrantUnlimitedStamina ya no existe"),
		UTN_StaminaComponent::StaticClass()->FindFunctionByName(TEXT("ServerGrantUnlimitedStamina")));

	const UFunction* Grant = UTN_StaminaComponent::StaticClass()->FindFunctionByName(TEXT("GrantUnlimitedStamina"));
	if (TestNotNull(TEXT("GrantUnlimitedStamina"), Grant))
	{
		TestTrue(TEXT("Blueprint solo la llama con autoridad"), Grant->HasAnyFunctionFlags(FUNC_BlueprintAuthorityOnly));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNStaminaGrantAuthorityTest,
	"Tortunabo.Net.StaminaGrant.AuthorityOnly",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNStaminaGrantAuthorityTest::RunTest(const FString& Parameters)
{
	using namespace TNStaminaGrantAuthorityTest;
	FPlayWorld Play;
	ATortugaCharacter* Server = SpawnTurtle(Play.World, 0.0);
	ATortugaCharacter* Client = SpawnTurtle(Play.World, 400.0);
	UTN_StaminaComponent* ServerStamina = Server ? Server->GetStaminaComponent() : nullptr;
	UTN_StaminaComponent* ClientStamina = Client ? Client->GetStaminaComponent() : nullptr;
	if (!TestNotNull(TEXT("Estamina del servidor"), ServerStamina) || !TestNotNull(TEXT("Estamina del cliente"), ClientStamina))
	{
		return false;
	}

	// Con autoridad (el uso del objeto en el servidor) se concede.
	ServerStamina->GrantUnlimitedStamina(5.f);
	TestTrue(TEXT("Con autoridad se concede"), ServerStamina->HasUnlimitedStamina());

	// Sin autoridad (la copia del dueño en un cliente) no se concede ni se pide nada.
	Client->SetRole(ROLE_AutonomousProxy);
	ClientStamina->GrantUnlimitedStamina(5.f);
	TestFalse(TEXT("Sin autoridad no se concede"), ClientStamina->HasUnlimitedStamina());
	Client->SetRole(ROLE_Authority);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
