// Vitales de la tortuga (#855): vida, veneno e hidratación. Las cuentas puras (TN_VitalsRules.h) y el componente sobre una
// tortuga de verdad en un mundo de juego sin modo de juego (pide la muerte, pero nadie la mata: se mira la causa pedida).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Vitals; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TN_VitalsComponent.h"
#include "Player/TN_VitalsRules.h"
#include "Player/TortugaCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNVitalsTest
{
	/** Mundo de juego con BeginPlay ya hecho. */
	struct FVitalsWorld
	{
		UWorld* World = nullptr;

		FVitalsWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNVitalsTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
		}

		~FVitalsWorld()
		{
			if (World->HasBegunPlay()) { World->EndPlay(EEndPlayReason::Quit); }
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		UTN_VitalsComponent* SpawnTurtleVitals() const
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const ATortugaCharacter* Turtle = World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator, Params);
			return Turtle ? Turtle->GetVitalsComponent() : nullptr;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVitalsRulesTest,
	"Tortunabo.Vitals.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVitalsRulesTest::RunTest(const FString& Parameters)
{
	using namespace TNVitals;
	const FParams Params;
	const FState Full = TNVitals::Full(Params);
	TestEqual(TEXT("Empieza con la vida llena"), Full.Health, 100.f);
	TestEqual(TEXT("Empieza con la hidratación llena"), Full.Hydration, 100.f);

	// Daño: resta y no baja de cero; el estado de entrada no cambia.
	const FState Hurt = Damage(Full, 30.f);
	TestEqual(TEXT("30 de daño deja 70"), Hurt.Health, 70.f);
	TestEqual(TEXT("El daño no toca el estado de entrada"), Full.Health, 100.f);
	TestEqual(TEXT("El daño no baja de cero"), Damage(Hurt, 500.f).Health, 0.f);
	// Caso negativo: un daño negativo no cura.
	TestEqual(TEXT("Un daño negativo no hace nada"), Damage(Hurt, -20.f).Health, 70.f);

	// Curar: hasta el máximo, y nunca a quien ya tiene la vida a cero (eso es revivir).
	TestEqual(TEXT("Curar 25 desde 70 deja 95"), Heal(Hurt, Params, 25.f).Health, 95.f);
	TestEqual(TEXT("Curar no pasa del máximo"), Heal(Hurt, Params, 100.f).Health, 100.f);
	TestEqual(TEXT("Curar no levanta a quien está a cero"), Heal(Damage(Full, 100.f), Params, 50.f).Health, 0.f);

	// Veneno: 5/s durante 3 s quita 15 y se acaba solo.
	FState Poisoned = Poison(Full, 5.f, 3.f);
	TestTrue(TEXT("Envenenada"), Poisoned.IsPoisoned());
	for (int32 i = 0; i < 16; ++i)
	{
		Poisoned = Step(Poisoned, Params, 0.25f).State;
	}
	TestTrue(TEXT("El veneno se acaba"), !Poisoned.IsPoisoned());
	TestTrue(TEXT("5/s durante 3 s quita 15 (más 1,6 de hidratación, que no daña)"), FMath::IsNearlyEqual(Poisoned.Health, 85.f, 0.01f));

	// Dos venenos: manda el más fuerte y el más largo, no se suman.
	const FState Twice = Poison(Poison(Full, 5.f, 2.f), 3.f, 6.f);
	TestEqual(TEXT("Manda el daño por segundo mayor"), Twice.PoisonDamagePerSecond, 5.f);
	TestEqual(TEXT("Manda la duración más larga"), Twice.PoisonSecondsLeft, 6.f);
	TestTrue(TEXT("Curar el veneno lo quita"), !CurePoison(Twice).IsPoisoned());
	TestTrue(TEXT("Un veneno de 0/s no envenena"), !Poison(Full, 0.f, 5.f).IsPoisoned());

	// Hidratación: baja con el tiempo y, a cero, desgasta la vida.
	FState Thirsty = Full;
	Thirsty.Hydration = 0.2f;
	const FStepResult Dry = Step(Thirsty, Params, 1.f);
	TestEqual(TEXT("La hidratación no baja de cero"), Dry.State.Hydration, 0.f);
	TestTrue(TEXT("Sin agua 0,5 s del paso: quita 0,5 de vida"), FMath::IsNearlyEqual(Dry.State.Health, 99.5f, 0.01f));
	TestTrue(TEXT("Con agua no se desgasta la vida"), FMath::IsNearlyEqual(Step(Full, Params, 1.f).State.Health, 100.f));
	TestTrue(TEXT("Hidratar suma hasta el máximo"), FMath::IsNearlyEqual(Hydrate(Thirsty, Params, 500.f).Hydration, 100.f));

	// Qué deja la vida a cero en un paso.
	FState Dying = Damage(Full, 99.f);
	TestTrue(TEXT("El veneno mata con su causa"), Step(Poison(Dying, 5.f, 4.f), Params, 0.5f).DepletedBy == EDepletedBy::Poison);
	Dying.Hydration = 0.f;
	TestTrue(TEXT("La sed mata con su causa"), Step(Dying, Params, 1.5f).DepletedBy == EDepletedBy::Dehydration);
	TestTrue(TEXT("Sin llegar a cero no hay causa"), Step(Full, Params, 1.f).DepletedBy == EDepletedBy::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVitalsComponentTest,
	"Tortunabo.Vitals.Component",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVitalsComponentTest::RunTest(const FString& Parameters)
{
	TNVitalsTest::FVitalsWorld Play;
	UTN_VitalsComponent* Vitals = Play.SpawnTurtleVitals();
	if (!TestNotNull(TEXT("La tortuga trae el componente de vitales"), Vitals))
	{
		return false;
	}
	TestTrue(TEXT("Se replica"), Vitals->GetIsReplicated());
	TestEqual(TEXT("Vida llena"), Vitals->GetHealth(), Vitals->GetMaxHealth());

	TestEqual(TEXT("ApplyDamage devuelve lo quitado"), Vitals->ApplyDamage(20.f), 20.f);
	TestEqual(TEXT("Vida tras 20 de daño"), Vitals->GetHealth(), 80.f);
	TestEqual(TEXT("Un daño de cero no quita nada"), Vitals->ApplyDamage(0.f), 0.f);
	TestFalse(TEXT("Con vida no ha pedido la muerte"), Vitals->IsDepleted());

	Vitals->ApplyPoison(5.f, 2.f);
	TestTrue(TEXT("Envenenada"), Vitals->IsPoisoned());
	Vitals->AdvanceVitals(1.f);
	TestTrue(TEXT("Un segundo de veneno quita 5"), FMath::IsNearlyEqual(Vitals->GetHealth(), 75.f, 0.01f));
	Vitals->CurePoison();
	TestFalse(TEXT("CurePoison quita el veneno"), Vitals->IsPoisoned());
	Vitals->AdvanceVitals(1.f);
	TestTrue(TEXT("Sin veneno ya no quita vida"), FMath::IsNearlyEqual(Vitals->GetHealth(), 75.f, 0.01f));

	TestTrue(TEXT("Heal suma"), FMath::IsNearlyEqual(Vitals->Heal(10.f), 10.f));
	const float Hydration = Vitals->GetHydration();
	TestTrue(TEXT("La hidratación baja con el tiempo"), Hydration < Vitals->GetMaxHydration());
	TestTrue(TEXT("Hydrate la sube"), Vitals->Hydrate(100.f) > 0.f && FMath::IsNearlyEqual(Vitals->GetHydration(), Vitals->GetMaxHydration()));

	// Vida a cero: pide la muerte con la causa del golpe (y una sola vez).
	Vitals->ApplyDamage(500.f, nullptr, ETNDeathCause::Crab);
	TestTrue(TEXT("Vida a cero"), Vitals->IsDepleted());
	TestTrue(TEXT("Pide la muerte con la causa del golpe"), Vitals->GetDepletionCause() == ETNDeathCause::Crab);
	Vitals->ApplyDamage(10.f, nullptr, ETNDeathCause::Poison);
	TestTrue(TEXT("Ya a cero, otro golpe no cambia la causa"), Vitals->GetDepletionCause() == ETNDeathCause::Crab);
	TestEqual(TEXT("A cero, curar no la levanta"), Vitals->Heal(50.f), 0.f);

	// Revivir (o el tótem) los llena y el veneno, si mata, mata como veneno.
	Vitals->RestoreAll();
	TestEqual(TEXT("RestoreAll llena la vida"), Vitals->GetHealth(), Vitals->GetMaxHealth());
	Vitals->ApplyDamage(98.f);
	Vitals->ApplyPoison(5.f, 3.f);
	Vitals->AdvanceVitals(1.f);
	TestTrue(TEXT("El veneno la deja a cero"), Vitals->IsDepleted());
	TestTrue(TEXT("Y pide la muerte por veneno"), Vitals->GetDepletionCause() == ETNDeathCause::Poison);
	return true;
}

#endif
