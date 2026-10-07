// Elementos de la playa con el Tick dormido de lejos (#59): despiertan con una tortuga, un caparazón o una cámara a menos de
// su distancia, se duermen un poco más allá (sin parpadeo en el borde) y no se duermen con algo en marcha. Se testean las
// funciones de TN_BeachTickWakeSubsystem.h y, con un mundo de juego, unas algas de verdad.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Perf.BeachTickWake; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "World/Beach/TN_BeachSeaweed.h"
#include "World/Beach/TN_BeachTickWakeSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachTickWakeTest,
	"Tortunabo.Perf.BeachTickWake.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachTickWakeTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachTickWake;
	constexpr float Wake = 4000.f;
	const auto Sq = [](double D) { return D * D; };

	// Vigilante más cercano.
	const TArray<FVector> Watchers = { FVector(10000.0, 0.0, 0.0), FVector(0.0, 3000.0, 0.0) };
	TestEqual(TEXT("El más cercano de varios"), NearestDistSquared(FVector::ZeroVector, Watchers), Sq(3000.0));
	TestTrue(TEXT("Sin vigilantes: infinitamente lejos"), NearestDistSquared(FVector::ZeroVector, TArray<FVector>()) > Sq(1.0e9));
	TestFalse(TEXT("Sin vigilantes ni nada en marcha → dormido"), ShouldBeAwake(NearestDistSquared(FVector::ZeroVector, TArray<FVector>()), Wake, true, false));

	// Distancia con margen.
	TestTrue(TEXT("Dormido y cerca → despierta"), ShouldBeAwake(Sq(3000.0), Wake, false, false));
	TestFalse(TEXT("Dormido y lejos → sigue dormido"), ShouldBeAwake(Sq(6000.0), Wake, false, false));
	TestFalse(TEXT("Dormido justo pasado el borde → no despierta"), ShouldBeAwake(Sq(4100.0), Wake, false, false));
	TestTrue(TEXT("Despierto justo pasado el borde → sigue (margen)"), ShouldBeAwake(Sq(4100.0), Wake, true, false));
	TestFalse(TEXT("Despierto más allá del margen → se duerme"), ShouldBeAwake(Sq(Wake * SleepFactor + 10.0), Wake, true, false));

	// En marcha o sin distancia: nunca se duerme.
	TestTrue(TEXT("En marcha y lejos → despierto"), ShouldBeAwake(Sq(1.0e6), Wake, false, true));
	TestTrue(TEXT("Distancia 0 (no se duerme) → despierto"), ShouldBeAwake(Sq(1.0e6), 0.f, false, false));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Con mundo: unas algas de verdad se apuntan en su BeginPlay, se duermen sin nadie cerca, despiertan cuando se acerca un
// personaje y vuelven a dormirse cuando se aleja; con TN.Perf.BeachTickWake 0, despierta siempre (como antes de #59).
// ─────────────────────────────────────────────────────────────────────────────

namespace TNBeachTickWakeTestDetail
{
	/** Mundo de juego mínimo con BeginPlay ya hecho (los actores que se crean después lo reciben al aparecer). */
	UWorld* CreateGameWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		World->GetWorldSettings()->NotifyBeginPlay();
		return World;
	}

	void DestroyGameWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	/** Una mirada del subsistema (pasa el intervalo entero). */
	void Check(UWorld* World)
	{
		World->GetSubsystem<UTN_BeachTickWakeSubsystem>()->Tick(TNBeachTickWake::CheckInterval + 0.01f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachTickWakeWorldTest,
	"Tortunabo.Perf.BeachTickWake.World",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachTickWakeWorldTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachTickWakeTestDetail;
	UWorld* World = CreateGameWorld();
	if (!TestNotNull(TEXT("Subsistema creado en un mundo de juego"), World->GetSubsystem<UTN_BeachTickWakeSubsystem>()))
	{
		DestroyGameWorld(World);
		return false;
	}
	const FVector Far(100000.0, 0.0, 0.0);
	const FVector Near(1000.0, 0.0, 0.0);
	ACharacter* Walker = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(Far));
	ATN_BeachSeaweed* Weed = World->SpawnActor<ATN_BeachSeaweed>(ATN_BeachSeaweed::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("Personaje"), Walker) || !TestNotNull(TEXT("Algas"), Weed))
	{
		DestroyGameWorld(World);
		return false;
	}
	TestFalse(TEXT("Nacen dormidas con el personaje a 1 km"), Weed->IsActorTickEnabled());

	Walker->SetActorLocation(Near);
	Check(World);
	TestTrue(TEXT("Despiertan con el personaje a 10 m"), Weed->IsActorTickEnabled());

	Walker->SetActorLocation(Far);
	Check(World);
	TestFalse(TEXT("Se duermen cuando se aleja"), Weed->IsActorTickEnabled());

	IConsoleVariable* Toggle = IConsoleManager::Get().FindConsoleVariable(TEXT("TN.Perf.BeachTickWake"));
	if (TestNotNull(TEXT("TN.Perf.BeachTickWake existe"), Toggle))
	{
		Toggle->Set(0, ECVF_SetByCode);
		Check(World);
		TestTrue(TEXT("Con TN.Perf.BeachTickWake 0, despiertan aunque esté lejos"), Weed->IsActorTickEnabled());
		Toggle->Set(1, ECVF_SetByCode);
	}

	Weed->Destroy();
	Check(World);
	DestroyGameWorld(World);
	return true;
}

#endif
