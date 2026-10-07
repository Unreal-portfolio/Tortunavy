// Salto de salida de una trampa sin corrección (#892): el servidor lo concede con LaunchFromServer y lo estrena el dueño en
// su movimiento. Con un dueño remoto, el servidor no la saca de MOVE_None hasta ese movimiento (si la pasaba a MOVE_Falling
// y la lanzaba ya, el dueño seguía mandando pasos quietos en el ancla y el servidor lo corregía por discrepar en el modo).
// El anfitrión salta al momento, como antes.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Beach.TrapRelease; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_BeachTrapStatusComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTrapReleaseLaunchTest
{
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNTrapReleaseTestWorld"));
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

	const FVector Hop(0.0, 0.0, 500.0);

	/** Atrapa y suelta a una tortuga recién creada; devuelve su movimiento (nullptr si algo falla). */
	UCharacterMovementComponent* TrapAndRelease(FAutomationTestBase& Test, UWorld* World, bool bRemoteOwner)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATortugaCharacter* Turtle = World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), FVector(0.0, 0.0, 200.0),
			FRotator::ZeroRotator, Params);
		if (!Test.TestNotNull(TEXT("Se crea la tortuga"), Turtle)) { return nullptr; }
		if (bRemoteOwner)
		{
			// Como en el servidor la tortuga de un cliente remoto: la controla otra máquina.
			Turtle->SetAutonomousProxy(true);
		}
		UTN_BeachTrapStatusComponent* Trap = UTN_BeachTrapStatusComponent::FindOrAddTo(Turtle);
		UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
		if (!Test.TestNotNull(TEXT("Tiene estado de trampa"), Trap) || !Test.TestNotNull(TEXT("Tiene movimiento"), Move)) { return nullptr; }

		Trap->ServerTrap(Turtle, Turtle->GetActorLocation());
		Test.TestEqual(TEXT("Atrapada, sin movimiento"), static_cast<int32>(Move->MovementMode.GetValue()), static_cast<int32>(MOVE_None));
		Trap->ServerRelease(Hop, 0.f);
		Test.TestFalse(TEXT("Suelta"), Trap->IsTrapped());
		return Move;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTrapReleaseRemoteOwnerTest,
	"Tortunabo.Beach.TrapRelease.RemoteOwnerWaitsForOwnerMove",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTrapReleaseRemoteOwnerTest::RunTest(const FString& Parameters)
{
	TNTrapReleaseLaunchTest::FPlayWorld Play;
	const UCharacterMovementComponent* Move = TNTrapReleaseLaunchTest::TrapAndRelease(*this, Play.World, true);
	if (!Move) { return false; }
	// El servidor no la lanza por su cuenta ni cambia de modo: lo hace en el movimiento del dueño que estrena el salto.
	TestEqual(TEXT("Dueño remoto: el servidor sigue en MOVE_None hasta su movimiento"),
		static_cast<int32>(Move->MovementMode.GetValue()), static_cast<int32>(MOVE_None));
	TestTrue(TEXT("Dueño remoto: el servidor no lanza por su cuenta"), Move->PendingLaunchVelocity.IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTrapReleaseHostTest,
	"Tortunabo.Beach.TrapRelease.HostHopsAtOnce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTrapReleaseHostTest::RunTest(const FString& Parameters)
{
	TNTrapReleaseLaunchTest::FPlayWorld Play;
	const UCharacterMovementComponent* Move = TNTrapReleaseLaunchTest::TrapAndRelease(*this, Play.World, false);
	if (!Move) { return false; }
	TestEqual(TEXT("Sin dueño remoto: sale de MOVE_None al momento"),
		static_cast<int32>(Move->MovementMode.GetValue()), static_cast<int32>(MOVE_Falling));
	TestTrue(TEXT("Y con el salto de salida"), Move->PendingLaunchVelocity.Equals(TNTrapReleaseLaunchTest::Hop, 0.01));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
