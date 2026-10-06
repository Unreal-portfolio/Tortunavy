// Mareo con estado replicado y tope predicho (#574): el servidor pone bMareo (replicado, con OnRep) y el tope de velocidad
// del mareo es de los predichos en el movimiento; dura lo pedido y vuelve a empezar si la marean otra vez. Antes iba en una
// multicast no fiable y, si se perdía, el dueño andaba sin tope mientras el servidor lo aplicaba.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Movement.Mareo; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TN_MovementLimits.h"
#include "Player/TN_StaminaComponent.h"
#include "Player/TortugaCharacter.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNMareoTest
{
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNMareoTestWorld"));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMareoReplicatedTest,
	"Tortunabo.Movement.Mareo.Replicated",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMareoReplicatedTest::RunTest(const FString& Parameters)
{
	// El estado del mareo es una propiedad replicada con OnRep (llega siempre, también a quien entra tarde).
	const FProperty* Property = ATortugaCharacter::StaticClass()->FindPropertyByName(TEXT("bMareo"));
	if (!TestNotNull(TEXT("ATortugaCharacter tiene bMareo"), Property))
	{
		return false;
	}
	TestTrue(TEXT("bMareo se replica"), Property->HasAnyPropertyFlags(CPF_Net));
	TestTrue(TEXT("bMareo tiene OnRep"), Property->HasAnyPropertyFlags(CPF_RepNotify));
	TestEqual(TEXT("El OnRep es OnRep_Mareo"), Property->RepNotifyFunc, FName(TEXT("OnRep_Mareo")));
	// Y su tope va en el movimiento.
	TestEqual(TEXT("El tope del mareo es predicho"), TNMovementLimits::PredictedCapBit(TNMovementLimits::MareoSource()), TNMovementLimits::PredictedCapMareoBit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMareoDurationTest,
	"Tortunabo.Movement.Mareo.Duration",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMareoDurationTest::RunTest(const FString& Parameters)
{
	TNMareoTest::FPlayWorld Play;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATortugaCharacter* Turtle = Play.World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Tortuga creada"), Turtle))
	{
		return false;
	}
	const UTN_StaminaComponent* Stamina = Turtle->GetStaminaComponent();
	if (!TestNotNull(TEXT("Con estamina"), Stamina))
	{
		return false;
	}
	FTimerManager& Timers = Play.World->GetTimerManager();
	// El gestor de temporizadores solo avanza una vez por fotograma.
	const auto Advance = [&Timers](float Seconds) { ++GFrameCounter; Timers.Tick(Seconds); };
	const auto HasMareoCap = [Stamina]() { return (Stamina->GetPredictedCapMask() & TNMovementLimits::PredictedCapMareoBit) != 0; };

	// Un fotograma ya en marcha: un temporizador puesto antes del primer Tick no empieza a contar hasta él.
	Advance(0.f);
	Turtle->ApplyMareoEffect(3.f);
	TestTrue(TEXT("Mareada"), Turtle->IsMareoActive());
	TestTrue(TEXT("Con el tope del mareo"), HasMareoCap());

	Advance(2.9f);
	TestTrue(TEXT("A los 2,9 s sigue mareada"), Turtle->IsMareoActive() && HasMareoCap());
	Advance(0.2f);
	TestFalse(TEXT("A los 3,1 s ya no"), Turtle->IsMareoActive());
	TestFalse(TEXT("Y sin el tope"), HasMareoCap());

	// Mareada otra vez mientras dura: la cuenta vuelve a empezar.
	Turtle->ApplyMareoEffect(3.f);
	Advance(2.f);
	Turtle->ApplyMareoEffect(3.f);
	Advance(2.f);
	TestTrue(TEXT("Remareada a los 2 s: a los 4 s sigue"), Turtle->IsMareoActive() && HasMareoCap());
	Advance(1.2f);
	TestFalse(TEXT("A los 5,2 s acaba"), Turtle->IsMareoActive() || HasMareoCap());

	Turtle->ApplyMareoEffect(0.f);
	TestFalse(TEXT("Duración 0: nada"), Turtle->IsMareoActive());
	Turtle->Destroy();
	return true;
}

#endif
