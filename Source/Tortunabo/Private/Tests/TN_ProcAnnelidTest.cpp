// Anélido poliqueto (#792): alcance de 3 m, una sola caza y estamina llena. Correr desde Session Frontend (categoría "Tortunabo.ProcMap.Annelid") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.ProcMap.Annelid; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_StaminaComponent.h"
#include "UObject/UnrealType.h"
#include "World/ProcMap/TN_ProcAnnelid.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FAnnelidPlayWorld
	{
		UWorld* World = nullptr;

		FAnnelidPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNAnnelidTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
		}

		~FAnnelidPlayWorld()
		{
			if (World->HasBegunPlay()) { World->EndPlay(EEndPlayReason::Quit); }
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	/** Escribe CurrentStamina (protegida) por reflexión, como la dejaría una carrera. */
	bool SetStamina(UTN_StaminaComponent* Stamina, float Value)
	{
		const FFloatProperty* Prop = FindFProperty<FFloatProperty>(UTN_StaminaComponent::StaticClass(), TEXT("CurrentStamina"));
		if (!Stamina || !Prop)
		{
			return false;
		}
		Prop->SetPropertyValue_InContainer(Stamina, Value);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNAnnelidRulesTest,
	"Tortunabo.ProcMap.Annelid.Reglas",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNAnnelidRulesTest::RunTest(const FString& Parameters)
{
	using namespace TNAnnelid;
	TestTrue(TEXT("Encima de la boca, se caza"), InHuntRange(0.f));
	TestTrue(TEXT("A 3 m justos, se caza"), InHuntRange(300.f));
	TestFalse(TEXT("A 3,1 m, no"), InHuntRange(310.f));
	TestEqual(TEXT("A 3 m el punto de interacción queda al alcance de la tortuga"), 300.f - InteractionPointShift(300.f), TURTLE_REACH);
	TestEqual(TEXT("Encima de la boca el punto es el centro"), InteractionPointShift(0.f), 0.f);

	FHuntState State;
	TestFalse(TEXT("Desde lejos no se caza ni se gasta"), State.TryHunt(500.f));
	TestFalse(TEXT("Sigue sin consumir"), State.bConsumed);
	TestTrue(TEXT("Primera caza"), State.TryHunt(120.f));
	TestFalse(TEXT("Un solo uso"), State.TryHunt(0.f));

	TestEqual(TEXT("Escondido antes de la caza"), EmergeHeight01(0.f), 0.f);
	TestEqual(TEXT("Fuera del todo al acabar de salir"), EmergeHeight01(EMERGE_SECONDS), 1.f);
	TestEqual(TEXT("Fuera mientras se consume"), EmergeHeight01(EMERGE_SECONDS + HOLD_SECONDS * 0.5f), 1.f);
	TestEqual(TEXT("Escondido otra vez al final"), EmergeHeight01(EMERGE_SECONDS + HOLD_SECONDS + SINK_SECONDS), 0.f);
	TestTrue(TEXT("El actor dura hasta que acaba la animación"), LIFE_AFTER_HUNT > EMERGE_SECONDS + HOLD_SECONDS + SINK_SECONDS);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNAnnelidStaminaTest,
	"Tortunabo.ProcMap.Annelid.RellenaEstamina",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNAnnelidStaminaTest::RunTest(const FString& Parameters)
{
	FAnnelidPlayWorld Play;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATortugaCharacter* Turtle = Play.World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator, Params);
	ATN_ProcAnnelid* Annelid = Play.World->SpawnActor<ATN_ProcAnnelid>(ATN_ProcAnnelid::StaticClass(), FVector(200.0, 0.0, 100.0), FRotator::ZeroRotator, Params);
	UTN_StaminaComponent* Stamina = Turtle ? Turtle->FindComponentByClass<UTN_StaminaComponent>() : nullptr;
	if (!TestNotNull(TEXT("Tortuga"), Turtle) || !TestNotNull(TEXT("Anélido"), Annelid) || !TestNotNull(TEXT("Estamina"), Stamina))
	{
		return false;
	}
	TestEqual(TEXT("Se caza desde 3 m"), Annelid->GetInteractionDistance(), TNAnnelid::HUNT_RADIUS);
	if (!TestTrue(TEXT("Estamina vaciada"), SetStamina(Stamina, 0.f)))
	{
		return false;
	}
	TestTrue(TEXT("Se puede cazar"), Annelid->CanInteract(Turtle));
	Annelid->Interact(Turtle);
	TestEqual(TEXT("Estamina llena tras cazarlo"), Stamina->GetCurrentStamina(), Stamina->GetEffectiveMaxStamina());
	TestTrue(TEXT("Consumido"), Annelid->IsConsumed());
	TestFalse(TEXT("No se caza dos veces"), Annelid->CanInteract(Turtle));

	SetStamina(Stamina, 0.f);
	Annelid->Interact(Turtle);
	TestEqual(TEXT("La segunda vez no da estamina"), Stamina->GetCurrentStamina(), 0.f);
	return true;
}

#endif
