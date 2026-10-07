// Frecuencia del forcejeo en el servidor (#896): UTN_BeachTrapStatusComponent::ServerEscapePress ignora las pulsaciones que
// llegan a menos de TNBeachCreatureRules::EscapeMinPressGap de la anterior. El límite del cliente (PressEscape) no basta: un
// cliente modificado mandaba todas las pulsaciones de golpe y se soltaba al instante.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Net.EscapeRate; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_BeachCreatureRules.h"
#include "World/Beach/TN_BeachTrapStatusComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNBeachEscapeRateTest
{
	/** Mundo de juego con BeginPlay ya hecho: los actores que se crean después lo reciben al aparecer. */
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNBeachEscapeRateTestWorld"));
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

	ATortugaCharacter* SpawnTurtle(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator, Params);
	}

	/** ServerEscapePress es un RPC protegido: en un mundo sin red se ejecuta en el acto, como en el servidor. */
	bool CallServerEscapePress(UTN_BeachTrapStatusComponent* Status)
	{
		UFunction* Function = Status ? Status->FindFunction(TEXT("ServerEscapePress")) : nullptr;
		if (!Function) { return false; }
		Status->ProcessEvent(Function, nullptr);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNEscapeRateRuleTest,
	"Tortunabo.Net.EscapeRate.Rule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNEscapeRateRuleTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachCreatureRules;
	const double Below = EscapeMinPressGap * 0.4;
	const double Above = EscapeMinPressGap * 1.2;

	// Por debajo del intervalo: solo cuenta la primera y no se suelta por mucho que machaque.
	FMashCounter Burst;
	int32 Counted = 0;
	for (int32 k = 0; k < 40; ++k) { Counted += Burst.PressAtMostEvery(k * Below, EscapeDecay, EscapeMinPressGap) ? 1 : 0; }
	TestEqual(TEXT("Por debajo: cuenta una de cada tres"), Counted, 14);
	FMashCounter Instant;
	for (int32 k = 0; k < 40; ++k) { Instant.PressAtMostEvery(1.0, EscapeDecay, EscapeMinPressGap); }
	TestFalse(TEXT("Cuarenta pulsaciones en el mismo instante no sueltan"), HasEscaped(Instant, 1.0));

	// Por encima del intervalo: cuentan todas y se suelta como antes.
	FMashCounter Fast;
	Counted = 0;
	for (int32 k = 0; k < 8; ++k) { Counted += Fast.PressAtMostEvery(k * Above, EscapeDecay, EscapeMinPressGap) ? 1 : 0; }
	TestEqual(TEXT("Por encima: cuentan todas"), Counted, 8);
	TestTrue(TEXT("Ocho pulsaciones a ritmo legal sueltan"), HasEscaped(Fast, 7 * Above));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNEscapeRateServerTest,
	"Tortunabo.Net.EscapeRate.ServerEscapePress",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNEscapeRateServerTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachEscapeRateTest;
	FPlayWorld Play;
	ATortugaCharacter* Turtle = SpawnTurtle(Play.World);
	UTN_BeachTrapStatusComponent* Status = Turtle ? UTN_BeachTrapStatusComponent::FindOrAddTo(Turtle) : nullptr;
	if (!TestNotNull(TEXT("Componente de forcejeo"), Status))
	{
		return false;
	}

	// Atrapada (arenas movedizas): el forcejeo queda armado y la red de seguridad no lo desarma.
	Status->ServerTrap(Turtle, Turtle->GetActorLocation());
	if (!TestTrue(TEXT("Forcejeo armado"), Status->IsEscapeArmed()))
	{
		return false;
	}

	// Caso negativo: un cliente modificado manda veinte pulsaciones en el mismo instante.
	for (int32 k = 0; k < 20; ++k)
	{
		if (!TestTrue(TEXT("ServerEscapePress existe"), CallServerEscapePress(Status))) { return false; }
	}
	TestFalse(TEXT("Veinte pulsaciones de golpe no la sueltan"), Status->HasEscaped());

	// Caso positivo: pulsaciones separadas más que el intervalo (sin GameState, el tiempo del servidor es el del mundo).
	const double Step = TNBeachCreatureRules::EscapeMinPressGap * 1.2;
	for (int32 k = 0; k < 8 && !Status->HasEscaped(); ++k)
	{
		Play.World->TimeSeconds += Step;
		CallServerEscapePress(Status);
	}
	TestTrue(TEXT("A ritmo legal se suelta"), Status->HasEscaped());
	Status->ServerRelease(FVector::ZeroVector, 0.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
