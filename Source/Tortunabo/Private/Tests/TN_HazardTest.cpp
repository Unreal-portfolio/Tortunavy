// Enemigos y obstáculos del Excel (#871): el daño, el veneno y el tipo de muerte de cada uno, leídos de UTN_HazardTuning
// (Config/DefaultGame.ini), y su aplicación sobre los vitales de una tortuga de verdad.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Hazards; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TN_VitalsComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/TN_HazardEffects.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNHazardTest
{
	/** Tras el efecto y Seconds de veneno: la vida que queda y si muere. */
	TNHazard::FOutcome After(const FTNHazardEffect& Effect, float Seconds, float StartHealth = 100.f)
	{
		const TNVitals::FParams Params;
		TNVitals::FState Start = TNVitals::Full(Params);
		Start.Health = StartHealth;
		TNHazard::FOutcome Out = TNHazard::Resolve(Effect, Start);
		for (float Time = 0.f; Time < Seconds && !Out.bDies; Time += 0.25f)
		{
			const TNVitals::FStepResult Step = TNVitals::Step(Out.State, Params, 0.25f);
			Out.State = Step.State;
			Out.bDies = Step.DepletedBy != TNVitals::EDepletedBy::None;
		}
		return Out;
	}

	/** Mundo de juego con BeginPlay ya hecho. */
	struct FHazardWorld
	{
		UWorld* World = nullptr;

		FHazardWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNHazardTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
		}

		~FHazardWorld()
		{
			if (World->HasBegunPlay()) { World->EndPlay(EEndPlayReason::Quit); }
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNHazardTuningTest,
	"Tortunabo.Hazards.Tuning",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNHazardTuningTest::RunTest(const FString& Parameters)
{
	// Los valores de la hoja EnemyAndObstacleData, tal como llegan del .ini.
	const UTN_HazardTuning& T = UTN_HazardTuning::Get();
	TestTrue(TEXT("Gaviota 1: caída mortal"), T.GullDrop.bKills);
	TestEqual(TEXT("Gaviota 2: caca, 30"), T.SeagullDropping.Damage, 30.f);
	TestEqual(TEXT("Cangrejo 1: 20 por contacto"), T.CrabContact.Damage, 20.f);
	TestTrue(TEXT("Cangrejo 2: arrastre mortal"), T.DragCrab.bKills);
	TestEqual(TEXT("Tentáculos: 5/s de veneno"), T.JellyfishTentacles.PoisonDamagePerSecond, 5.f);
	TestEqual(TEXT("Tentáculos: sin daño de golpe"), T.JellyfishTentacles.Damage, 0.f);
	TestEqual(TEXT("Erizo: 15"), T.Urchin.Damage, 15.f);
	TestTrue(TEXT("Erizo: más veneno"), T.Urchin.PoisonDamagePerSecond > 0.f && T.Urchin.PoisonSeconds > 0.f);
	TestEqual(TEXT("Cangrejo 3: 40"), T.BurrowCrab.Damage, 40.f);
	TestTrue(TEXT("Arenas movedizas: mortales"), T.Quicksand.bKills);
	TestTrue(TEXT("Quad: instantánea"), T.Quad.bKills);
	TestEqual(TEXT("Erizos checos: 15 al chocar"), T.TankTrap.Damage, 15.f);
	TestEqual(TEXT("Anélido: cura 25"), T.AnnelidHeal, 25.f);
	// Caso negativo: lo que daña no mata de un golpe.
	for (const FTNHazardEffect* Effect : { &T.SeagullDropping, &T.CrabContact, &T.JellyfishTentacles, &T.Urchin, &T.BurrowCrab, &T.TankTrap })
	{
		TestFalse(TEXT("Lo que quita vida no mata sin más"), Effect->bKills);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNHazardElementsTest,
	"Tortunabo.Hazards.Elements",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNHazardElementsTest::RunTest(const FString& Parameters)
{
	using TNHazardTest::After;
	const UTN_HazardTuning& T = UTN_HazardTuning::Get();

	// Gaviota 1: soltada desde lo alto, muere; si se escurre cerca del suelo, no.
	TestTrue(TEXT("Gaviota 1: la mata"), After(T.GullDrop, 0.f).bDies);
	TestTrue(TEXT("Gaviota 1: desde 26 m, caída mortal"), TNHazard::GullDropKills(2600.f, T.GullDropFatalHeight, T.GullDrop.bKills));
	TestFalse(TEXT("Gaviota 1: desde 1 m, no"), TNHazard::GullDropKills(100.f, T.GullDropFatalHeight, T.GullDrop.bKills));

	// Caca: 30 cada una; a la cuarta, muerta.
	TNHazard::FOutcome Poop = After(T.SeagullDropping, 0.f);
	TestEqual(TEXT("Caca: deja 70"), Poop.State.Health, 70.f);
	TestFalse(TEXT("Caca: no mata con la vida llena"), Poop.bDies);
	TestTrue(TEXT("Caca: la cuarta mata"), After(T.SeagullDropping, 0.f, 10.f).bDies);

	TestEqual(TEXT("Cangrejo 1: deja 80"), After(T.CrabContact, 0.f).State.Health, 80.f);

	// Cangrejo 2: el arrastre completo mata; soltarse o marearlo, no.
	using TNBeachCreatureRules::DragCrab::EDragEnd;
	TestTrue(TEXT("Cangrejo 2: la mata"), After(T.DragCrab, 0.f).bDies);
	TestTrue(TEXT("Cangrejo 2: hasta el final"), TNHazard::DragCompletes(EDragEnd::Distance));
	TestTrue(TEXT("Cangrejo 2: hasta la zona de muerte"), TNHazard::DragCompletes(EDragEnd::Unsafe));
	TestFalse(TEXT("Cangrejo 2: se escapa"), TNHazard::DragCompletes(EDragEnd::Escaped));
	TestFalse(TEXT("Cangrejo 2: lo marean"), TNHazard::DragCompletes(EDragEnd::HitStunned));

	// Tentáculos: veneno sin daño de golpe; 4 s a 5/s quitan 20.
	const TNHazard::FOutcome Sting = After(T.JellyfishTentacles, 0.f);
	TestEqual(TEXT("Tentáculos: sin daño de golpe"), Sting.State.Health, 100.f);
	TestTrue(TEXT("Tentáculos: envenenada"), Sting.State.IsPoisoned());
	TestTrue(TEXT("Tentáculos: 5/s durante 4 s"), FMath::IsNearlyEqual(After(T.JellyfishTentacles, 5.f).State.Health, 80.f, 0.01f));

	// Erizo: 15 de golpe más el veneno.
	const TNHazard::FOutcome Urchin = After(T.Urchin, 0.f);
	TestEqual(TEXT("Erizo: deja 85"), Urchin.State.Health, 85.f);
	TestTrue(TEXT("Erizo: envenenada"), Urchin.State.IsPoisoned());
	TestTrue(TEXT("Erizo: con el veneno, 65"), FMath::IsNearlyEqual(After(T.Urchin, 5.f).State.Health, 65.f, 0.01f));

	TestEqual(TEXT("Cangrejo 3: deja 60"), After(T.BurrowCrab, 0.f).State.Health, 60.f);
	TestTrue(TEXT("Cangrejo 3: con 40 o menos, mata"), After(T.BurrowCrab, 0.f, 40.f).bDies);

	// Arenas movedizas: al acabar el tiempo sin soltarse, muere; soltándose, no.
	TestTrue(TEXT("Arenas: la matan"), After(T.Quicksand, 0.f).bDies);
	TestTrue(TEXT("Arenas: hundida hasta el final"), TNHazard::QuicksandKills(true, false, T.Quicksand.bKills));
	TestFalse(TEXT("Arenas: se suelta machacando salto"), TNHazard::QuicksandKills(true, true, T.Quicksand.bKills));
	TestFalse(TEXT("Arenas: aún atrapada"), TNHazard::QuicksandKills(false, false, T.Quicksand.bKills));

	TestTrue(TEXT("Quad: la mata"), After(T.Quad, 0.f).bDies);
	TestEqual(TEXT("Erizos checos: deja 85"), After(T.TankTrap, 0.f).State.Health, 85.f);

	// Anélido: cura 25 sin pasar del máximo.
	const TNVitals::FParams Params;
	TNVitals::FState Hurt = TNVitals::Full(Params);
	Hurt.Health = 50.f;
	TestEqual(TEXT("Anélido: de 50 a 75"), TNVitals::Heal(Hurt, Params, T.AnnelidHeal).Health, 75.f);
	TestEqual(TEXT("Anélido: no pasa de 100"), TNVitals::Heal(TNVitals::Full(Params), Params, T.AnnelidHeal).Health, 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNHazardApplyTest,
	"Tortunabo.Hazards.Apply",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNHazardApplyTest::RunTest(const FString& Parameters)
{
	TNHazardTest::FHazardWorld Play;
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATortugaCharacter* Turtle = Play.World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator, SpawnParams);
	UTN_VitalsComponent* Vitals = Turtle ? Turtle->GetVitalsComponent() : nullptr;
	if (!TestNotNull(TEXT("Tortuga con vitales"), Vitals))
	{
		return false;
	}
	const UTN_HazardTuning& T = UTN_HazardTuning::Get();
	TNHazard::Apply(T.CrabContact, Turtle, nullptr);
	TestEqual(TEXT("El cangrejo quita 20"), Vitals->GetHealth(), 80.f);
	TNHazard::Apply(T.Urchin, Turtle, nullptr);
	TestEqual(TEXT("El erizo quita 15"), Vitals->GetHealth(), 65.f);
	TestTrue(TEXT("Y envenena"), Vitals->IsPoisoned());
	TNHazard::Heal(Turtle, T.AnnelidHeal);
	TestEqual(TEXT("El anélido cura 25"), Vitals->GetHealth(), 90.f);
	// Caso negativo: sin tortuga no hace nada (ni se cae).
	TNHazard::Apply(T.BurrowCrab, nullptr, nullptr);
	TNHazard::Apply(T.BurrowCrab, Turtle, nullptr);
	TestEqual(TEXT("La pinza quita 40"), Vitals->GetHealth(), 50.f);
	TNHazard::Apply(T.BurrowCrab, Turtle, nullptr);
	TestFalse(TEXT("Con 10 sigue viva"), Vitals->IsDepleted());
	TNHazard::Apply(T.BurrowCrab, Turtle, nullptr);
	TestTrue(TEXT("Otra pinza la deja a cero y pide la muerte como cangrejo"),
		Vitals->IsDepleted() && Vitals->GetDepletionCause() == ETNDeathCause::Crab);
	return true;
}

#endif
