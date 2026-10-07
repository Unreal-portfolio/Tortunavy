// Estamina en la salida del huevo (#520): con el sprint pedido (shift + W pulsados desde la carga), la tortuga no gasta
// estamina mientras sale del huevo: ni en la pausa (quieta) ni en el vuelo del lanzamiento, que lleva velocidad horizontal
// y la da el juego, no el jugador. Al tocar el suelo vuelve a gastar sin soltar la tecla.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Movement.Stamina.EggStart; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TN_StaminaComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/TN_EggHatch.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNStaminaEggTest
{
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNStaminaEggTestWorld"));
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNStaminaEggStartTest,
	"Tortunabo.Movement.Stamina.EggStart",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNStaminaEggStartTest::RunTest(const FString& Parameters)
{
	TNStaminaEggTest::FPlayWorld Play;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATortugaCharacter* Turtle = Play.World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Tortuga creada"), Turtle))
	{
		return false;
	}
	UTN_StaminaComponent* Stamina = Turtle->GetStaminaComponent();
	UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
	if (!TestNotNull(TEXT("Con estamina"), Stamina) || !TestNotNull(TEXT("Con movimiento"), Move))
	{
		return false;
	}
	const auto TickStamina = [Stamina](float Seconds) { Stamina->TickComponent(Seconds, LEVELTICK_All, nullptr); };
	const FVector HopVelocity(380.0, 0.0, 620.0);
	const float Full = Stamina->GetCurrentStamina();
	Stamina->SetSprintRequested(true);
	TestTrue(TEXT("Esprinta con el sprint pedido"), Stamina->IsSprinting());

	// Pausa en el huevo roto: quieta (MOVE_None) hasta el lanzamiento.
	TNEggHatch::Begin(Turtle, 0.0, 10.0, HopVelocity, 0.f, 0xFFFFFF);
	TestTrue(TEXT("En la pausa del huevo"), TNEggHatch::IsHatching(Turtle) && TNEggHatch::IsLeavingEgg(Turtle));
	TickStamina(1.f);
	TestEqual(TEXT("En la pausa no gasta"), Stamina->GetCurrentStamina(), Full);

	// Lanzamiento ya debido: sale despedida (MOVE_Falling) con velocidad horizontal que no ha pedido el jugador.
	TNEggHatch::Begin(Turtle, -1.0, 0.0, HopVelocity, 0.f, 0xFFFFFF);
	Move->Velocity = HopVelocity;
	TestTrue(TEXT("Lanzada y en el aire"), Move->IsFalling() && !TNEggHatch::IsHatching(Turtle));
	TestTrue(TEXT("En el vuelo sigue saliendo del huevo"), TNEggHatch::IsLeavingEgg(Turtle));
	TickStamina(1.f);
	TestEqual(TEXT("En el vuelo del lanzamiento no gasta"), Stamina->GetCurrentStamina(), Full);
	TestTrue(TEXT("El sprint sigue pedido"), Stamina->IsSprinting());

	// Toca el suelo: el vuelo acaba y, con la tecla aún pulsada, gasta al avanzar.
	Move->SetMovementMode(MOVE_Walking);
	if (UTN_EggHatchComponent* Hatch = Turtle->FindComponentByClass<UTN_EggHatchComponent>())
	{
		Hatch->TickComponent(0.016f, LEVELTICK_All, nullptr);
	}
	TestFalse(TEXT("En el suelo ya no sale del huevo"), TNEggHatch::IsLeavingEgg(Turtle));
	Move->Velocity = FVector(600.0, 0.0, 0.0);
	TickStamina(1.f);
	TestTrue(TEXT("Al avanzar en el suelo vuelve a gastar sin soltar la tecla"), Stamina->GetCurrentStamina() < Full);

	Turtle->Destroy();
	return true;
}

#endif
