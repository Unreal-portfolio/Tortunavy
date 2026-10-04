// Trampas de la playa: solo miran a los personajes que tienen al alcance en planta (#60).
// Se testea la regla de TN_BeachNearby.h que usa TNBeachNearby::Gather.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Beach.Nearby; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/Beach/TN_BeachNearby.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachNearbyTest,
	"Tortunabo.Beach.Nearby.Within2D",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachNearbyTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachNearby;

	const FVector Center(1000.0, -500.0, 200.0);
	TestTrue(TEXT("Encima"), IsWithin2D(Center, Center, 0.0));
	TestTrue(TEXT("Dentro del radio"), IsWithin2D(Center + FVector(300.0, 400.0, 0.0), Center, 500.0));
	TestTrue(TEXT("Justo en el borde"), IsWithin2D(Center + FVector(500.0, 0.0, 0.0), Center, 500.0));
	TestFalse(TEXT("Fuera del radio"), IsWithin2D(Center + FVector(300.0, 401.0, 0.0), Center, 500.0));
	// La altura la deciden las reglas de cada trampa (una tortuga que cae encima de la mina también cuenta).
	TestTrue(TEXT("La altura no cuenta"), IsWithin2D(Center + FVector(0.0, 0.0, 5000.0), Center, 10.0));
	TestFalse(TEXT("Radio negativo: nadie"), IsWithin2D(Center, Center, -1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachNearbyGatherTest,
	"Tortunabo.Beach.Nearby.Gather",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachNearbyGatherTest::RunTest(const FString& Parameters)
{
	// Mundo de juego mínimo con personajes de verdad: lo que ven las trampas con TNBeachNearby::Gather.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNNearbyTestWorld"));
	if (!TestNotNull(TEXT("Mundo de prueba"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Close = World->SpawnActor<ACharacter>(FVector(300.0, 0.0, 0.0), FRotator::ZeroRotator, Params);
	ACharacter* High = World->SpawnActor<ACharacter>(FVector(0.0, 200.0, 4000.0), FRotator::ZeroRotator, Params);
	ACharacter* Far = World->SpawnActor<ACharacter>(FVector(5000.0, 0.0, 0.0), FRotator::ZeroRotator, Params);

	++GFrameCounter;
	TArray<ACharacter*> Near;
	TNBeachNearby::Gather(World, FVector::ZeroVector, 1000.0, Near);
	TestEqual(TEXT("Dos al alcance en planta"), Near.Num(), 2);
	TestTrue(TEXT("El cercano"), Near.Contains(Close));
	TestTrue(TEXT("El que está encima (la altura la miran las reglas de la trampa)"), Near.Contains(High));
	TestFalse(TEXT("El lejano no"), Near.Contains(Far));

	// Mismo fotograma: la lista no se rehace (un personaje nuevo entra en el siguiente).
	ACharacter* Late = World->SpawnActor<ACharacter>(FVector(-200.0, 0.0, 0.0), FRotator::ZeroRotator, Params);
	TNBeachNearby::Gather(World, FVector::ZeroVector, 1000.0, Near);
	TestFalse(TEXT("Mismo fotograma: aún no"), Near.Contains(Late));
	++GFrameCounter;
	TNBeachNearby::Gather(World, FVector::ZeroVector, 1000.0, Near);
	TestTrue(TEXT("Siguiente fotograma: ya está"), Near.Contains(Late));

	// Uno destruido deja de salir aunque siga en la lista del fotograma.
	Close->Destroy();
	TNBeachNearby::Gather(World, FVector::ZeroVector, 1000.0, Near);
	TestFalse(TEXT("Destruido: fuera"), Near.Contains(Close));

	TNBeachNearby::Gather(nullptr, FVector::ZeroVector, 1000.0, Near);
	TestEqual(TEXT("Sin mundo: nadie"), Near.Num(), 0);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
