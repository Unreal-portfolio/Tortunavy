// El agua venenosa de Todos contra Todos (#831): el veneno que sube dentro del agua y baja fuera, la cuenta atrás de la próxima
// subida del plan y el veneno en el componente de la tortuga (con el flotador). Reglas puras (TN_TctRules.h) y, en un mundo de
// juego sin ventana, el servidor decidiendo con el reloj del mundo.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Tct.Water; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/TN_TctGameState.h"
#include "Game/TN_TctItemComponent.h"
#include "Game/TN_TctItems.h"
#include "Game/TN_TctRules.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TortugaCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTctWaterTest
{
	FTNTctFloodPlan Plan()
	{
		FTNTctFloodPlan Out;
		Out.BaseZ = -400.f;
		Out.Levels = { -140.f, 60.f, 260.f, 460.f };
		Out.SuddenDeathZ = 1100.f;
		return Out;
	}

	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNTctWaterTestWorld"));
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

		/** Deja pasar Seconds de reloj del mundo en pasos de 0,25 s. */
		void Advance(float Seconds)
		{
			for (float Left = Seconds; Left > 0.f; Left -= 0.25f)
			{
				World->Tick(LEVELTICK_All, FMath::Min(0.25f, Left));
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctWaterPoisonRuleTest,
	"Tortunabo.Tct.Water.Poison",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctWaterPoisonRuleTest::RunTest(const FString& Parameters)
{
	using namespace TNTctRules;

	TestTrue(TEXT("Tocar el agua no mata: hacen falta varios segundos"), 1.f / TNTctPoisonDefaults::Rate >= 3.f);
	TestTrue(TEXT("Dentro del agua, sube"), PoisonRateFor(true) > 0.f);
	TestTrue(TEXT("Fuera, baja"), PoisonRateFor(false) < 0.f);
	TestTrue(TEXT("Con aletas (escala menor), sube más despacio"), PoisonRateFor(true, 0.3f) < PoisonRateFor(true));

	// 0,25 s en el agua: aún lejos del máximo.
	FTNTctPoison Poison;
	SetPoisonRate(Poison, 10.0, PoisonRateFor(true));
	TestTrue(TEXT("Un chapuzón corto, poco veneno"), PoisonLevel(Poison, 10.25) < 0.1f);
	TestTrue(TEXT("Un segundo, un quinto"), FMath::IsNearlyEqual(PoisonLevel(Poison, 11.0), TNTctPoisonDefaults::Rate, 0.001f));
	TestEqual(TEXT("Cinco segundos seguidos llenan el veneno"), PoisonLevel(Poison, 15.0), 1.f);
	TestEqual(TEXT("Y no pasa de 1"), PoisonLevel(Poison, 100.0), 1.f);

	// Sale a los 2 s (0,4) y se recupera despacio hasta quedar limpia.
	SetPoisonRate(Poison, 12.0, PoisonRateFor(false));
	TestTrue(TEXT("Al salir conserva lo acumulado"), FMath::IsNearlyEqual(PoisonLevel(Poison, 12.0), 0.4f, 0.001f));
	TestTrue(TEXT("Fuera, baja"), PoisonLevel(Poison, 14.0) < 0.4f && PoisonLevel(Poison, 14.0) > 0.f);
	TestEqual(TEXT("Y se queda limpia"), PoisonLevel(Poison, 60.0), 0.f);

	// Entrar y salir a ratos no la mata: cada baño de 2 s se recupera en los 6 s siguientes.
	FTNTctPoison Splash;
	for (int32 Dip = 0; Dip < 5; ++Dip)
	{
		const double Start = Dip * 12.0;
		SetPoisonRate(Splash, Start, PoisonRateFor(true));
		TestTrue(TEXT("Un baño de 2 s no llena el veneno"), PoisonLevel(Splash, Start + 2.0) < 1.f);
		SetPoisonRate(Splash, Start + 2.0, PoisonRateFor(false));
		TestEqual(TEXT("Y se recupera antes del siguiente"), PoisonLevel(Splash, Start + 12.0), 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctWaterNextRiseTest,
	"Tortunabo.Tct.Water.NextRise",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctWaterNextRiseTest::RunTest(const FString& Parameters)
{
	using namespace TNTctRules;
	const FTNTctFloodPlan Flood = TNTctWaterTest::Plan();

	// Antes de la primera subida (a los 25 s): cuenta atrás, tramo 1 de 5 (cuatro pisos y la marea final) y su altura.
	FTNTctNextRise Next = NextRise(Flood, 10.f);
	TestTrue(TEXT("Hay subida por venir"), Next.bUpcoming);
	TestEqual(TEXT("Es la primera"), Next.Step, 0);
	TestEqual(TEXT("Cuatro pisos y la marea final: cinco tramos"), Next.Tiers, 5);
	TestEqual(TEXT("Quedan 15 s"), Next.SecondsLeft, 15.f);
	TestEqual(TEXT("Llegará al primer escalón"), Next.TargetZ, -140.f);
	TestFalse(TEXT("Aún no sube"), Next.bRising);
	TestFalse(TEXT("No es la marea final"), Next.bSuddenDeath);

	// Subiendo (de 25 a 32 s): lo dice y la próxima es la siguiente.
	Next = NextRise(Flood, 28.f);
	TestTrue(TEXT("Sube ahora"), Next.bRising);
	TestEqual(TEXT("Hacia el primer escalón"), Next.RisingTargetZ, -140.f);
	TestEqual(TEXT("La próxima es la segunda, a los 49 s"), Next.Step, 1);
	TestEqual(TEXT("Quedan 21 s"), Next.SecondsLeft, 21.f);

	// El aviso (5 s antes) enseña ya la altura a la que llega.
	Next = NextRise(Flood, 44.5f);
	TestTrue(TEXT("En el aviso, queda poco"), Next.SecondsLeft <= TNTctPoisonDefaults::WarnSeconds);
	TestEqual(TEXT("Llegará al segundo escalón"), Next.TargetZ, 60.f);

	// La marea final.
	Next = NextRise(Flood, StepStartSeconds(Flood, 4) - 3.f);
	TestTrue(TEXT("Es la marea final"), Next.bSuddenDeath);
	TestEqual(TEXT("Cubrirá todo"), Next.TargetZ, 1100.f);
	TestEqual(TEXT("Último tramo"), Next.Step + 1, Next.Tiers);

	// Cuando ya no hay más, nada por venir.
	Next = NextRise(Flood, FloodTopSeconds(Flood) + 10.f);
	TestFalse(TEXT("Ya no sube más"), Next.bUpcoming);
	TestFalse(TEXT("Ni está subiendo"), Next.bRising);

	// Los valores del plan de serie son los de #778 (también en el estado que se replica).
	const FTNTctFloodState State;
	TestEqual(TEXT("Estado: empieza a los 25 s"), State.StartDelay, 25.f);
	TestEqual(TEXT("Estado: un escalón cada 24 s"), State.StepSeconds, 24.f);
	TestEqual(TEXT("Estado: cada subida, 7 s"), State.RiseSeconds, 7.f);
	TestEqual(TEXT("Estado: la marea final, 40 s"), State.SuddenDeathRiseSeconds, 40.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctWaterComponentTest,
	"Tortunabo.Tct.Water.Component",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctWaterComponentTest::RunTest(const FString& Parameters)
{
	using namespace TNTctWaterTest;
	FPlayWorld Play;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATortugaCharacter* Turtle = Play.World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator, Params);
	UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOrAddOn(Turtle);
	if (!TestNotNull(TEXT("Tortuga"), Turtle) || !TestNotNull(TEXT("Componente"), Effects))
	{
		return false;
	}

	TestEqual(TEXT("Limpia al empezar"), Effects->GetPoison(), 0.f);
	TestFalse(TEXT("Seca, no se envenena"), Effects->ServerTickWater(false));
	TestFalse(TEXT("Un primer contacto con el agua no mata"), Effects->ServerTickWater(true));
	Play.Advance(2.f);
	const float After2 = Effects->GetPoison();
	TestTrue(TEXT("A los 2 s en el agua, un 40 %"), FMath::IsNearlyEqual(After2, 0.4f, 0.1f));
	TestFalse(TEXT("Sigue viva"), Effects->ServerTickWater(true));

	// Sale: se recupera.
	TestFalse(TEXT("Sale del agua"), Effects->ServerTickWater(false));
	Play.Advance(2.f);
	TestTrue(TEXT("Fuera, el veneno baja"), Effects->GetPoison() < After2);

	// Cinco segundos más dentro: eliminada.
	Effects->ServerTickWater(true);
	Play.Advance(6.f);
	TestTrue(TEXT("Al llenarse el veneno, queda eliminada"), Effects->ServerTickWater(true));

	// Ronda nueva: limpia.
	Effects->ClearEffects();
	TestEqual(TEXT("Ronda nueva: limpia"), Effects->GetPoison(), 0.f);

	// El flotador sin estrenar no se gasta por mojarse los pies: salta al llegar al nivel de aviso.
	TestTrue(TEXT("Coge el flotador"), TNTctItems::GiveItem(Turtle, ETNTctItem::Flotador));
	Effects->ServerTickWater(true);
	Play.Advance(1.f);
	TestFalse(TEXT("Al segundo, sigue sin gastarse"), Effects->ServerTickWater(true) || !Effects->HasFloat());
	Play.Advance(2.f);
	TestFalse(TEXT("Al llegar a la mitad, la salva"), Effects->ServerTickWater(true));
	TestFalse(TEXT("Y se gasta"), Effects->HasFloat());
	TestTrue(TEXT("Flota"), Effects->IsFloating());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
