// Objetos sueltos entre rondas y niveles (#569, TN_RoundLeftovers.h): lo que dejan las jugadoras (pickups soltados, bolas,
// conchas trampa) desaparece con TNRoundLeftovers::DestroyPlayerLeftovers, la misma llamada que hacen la carrera de la playa
// (CleanupRoundLeftovers), el mapa procedural (CleanupRoundActors, desde StartNextRound) y la Supervivencia (AdvanceLevel,
// antes de BuildLevel); lo colocado en el nivel se queda. Y una concha destruida con una tortuga atrapada la deja andar.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Items.RoundLeftovers; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/TN_RoundLeftovers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TortugaCharacter.h"
#include "World/TN_ConchPickup.h"
#include "World/TN_PickupInteractableBase.h"
#include "World/TN_ThrowableItemActor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRoundLeftoversTest
{
	/** Mundo de juego con BeginPlay ya hecho: los actores que se crean después lo reciben al aparecer. */
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNRoundLeftoversTestWorld"));
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

	template <typename TActor>
	TActor* Spawn(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<TActor>(TActor::StaticClass(), Where, FRotator::ZeroRotator, Params);
	}

	/** Actores de TActor que siguen en el mundo (válidos y sin destruirse). */
	template <typename TActor>
	int32 CountAlive(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<TActor> It(World); It; ++It)
		{
			Count += (IsValid(*It) && !It->IsActorBeingDestroyed()) ? 1 : 0;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRoundLeftoversDestroyTest,
	"Tortunabo.Items.RoundLeftovers.DestroysPlayerLeftovers",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRoundLeftoversDestroyTest::RunTest(const FString& Parameters)
{
	using namespace TNRoundLeftoversTest;
	FPlayWorld Play;
	UWorld* World = Play.World;

	// Lo que deja una ronda: un objeto soltado, el pickup de una bola parada, una bola en el aire y una concha trampa.
	Spawn<ATN_PickupInteractableBase>(World, FVector(0.0, 0.0, 0.0));
	Spawn<ATN_PickupInteractableBase>(World, FVector(300.0, 0.0, 0.0));
	Spawn<ATN_ThrowableItemActor>(World, FVector(600.0, 0.0, 300.0));
	ATN_ConchPickup* Trap = Spawn<ATN_ConchPickup>(World, FVector(900.0, 0.0, 0.0));
	if (Trap) { Trap->PlaceAsTrap(Trap->GetActorLocation()); }
	// Y uno colocado a mano en el nivel (IsNetStartupActor): no es de ninguna ronda.
	ATN_PickupInteractableBase* Placed = Spawn<ATN_PickupInteractableBase>(World, FVector(0.0, 900.0, 0.0));
	if (!TestNotNull(TEXT("Pickup del nivel"), Placed) || !TestNotNull(TEXT("Concha trampa"), Trap))
	{
		return false;
	}
	Placed->bNetStartup = true;

	TestEqual(TEXT("Se quitan los cuatro objetos de la ronda"), TNRoundLeftovers::DestroyPlayerLeftovers(World), 4);
	TestEqual(TEXT("Solo queda el pickup colocado en el nivel"), CountAlive<ATN_PickupInteractableBase>(World), 1);
	TestTrue(TEXT("El del nivel sigue en el mundo"), IsValid(Placed) && !Placed->IsActorBeingDestroyed());
	TestEqual(TEXT("Ninguna bola suelta"), CountAlive<ATN_ThrowableItemActor>(World), 0);
	TestEqual(TEXT("Ninguna concha trampa"), CountAlive<ATN_ConchPickup>(World), 0);
	TestEqual(TEXT("Una segunda limpieza no encuentra nada"), TNRoundLeftovers::DestroyPlayerLeftovers(World), 0);
	TestEqual(TEXT("Sin mundo no hace nada"), TNRoundLeftovers::DestroyPlayerLeftovers(nullptr), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRoundLeftoversTrappedVictimTest,
	"Tortunabo.Items.RoundLeftovers.TrappedVictimWalks",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRoundLeftoversTrappedVictimTest::RunTest(const FString& Parameters)
{
	using namespace TNRoundLeftoversTest;
	FPlayWorld Play;
	UWorld* World = Play.World;

	ATortugaCharacter* Turtle = Spawn<ATortugaCharacter>(World, FVector(0.0, 0.0, 200.0));
	ATN_ConchPickup* Trap = Spawn<ATN_ConchPickup>(World, FVector(0.0, 0.0, 120.0));
	USphereComponent* Sphere = Trap ? Trap->FindComponentByClass<USphereComponent>() : nullptr;
	if (!TestNotNull(TEXT("Tortuga"), Turtle) || !TestNotNull(TEXT("Esfera de la concha"), Sphere))
	{
		return false;
	}
	Turtle->GetCharacterMovement()->SetMovementMode(MOVE_Walking);

	// La tortuga pisa la trampa (el camino real del solape): queda inmovilizada mientras corre el temporizador.
	Trap->PlaceAsTrap(Trap->GetActorLocation());
	Sphere->OnComponentBeginOverlap.Broadcast(Sphere, Turtle, nullptr, 0, false, FHitResult());
	if (!TestEqual(TEXT("Atrapada: sin movimiento"), Turtle->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None))
	{
		return false;
	}

	// Fin de ronda o de nivel: la limpieza destruye la concha antes de que salte su temporizador.
	TestEqual(TEXT("La limpieza quita la concha"), TNRoundLeftovers::DestroyPlayerLeftovers(World), 1);
	TestEqual(TEXT("La tortuga vuelve a andar"), Turtle->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Walking);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
