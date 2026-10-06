// Vuelta al lobby sin cortes (#711): un PlayerState que no viaja seguía en el PlayerArray del GameState, que sí viaja al
// mapa de transición; tras el GC del mapa viejo quedaban huecos nulos y AGameStateBase::SeamlessTravelTransitionCheckpoint
// los leía al salir hacia el lobby. Los GameState del juego los quitan antes (TN_RemoveStalePlayerStates).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Multiplayer.SeamlessTravel; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Core/TN_CoopGameState.h"
#include "Core/TN_GameModeSpawnUtils.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNSeamlessTravelTestDetail
{
	/** Mundo de juego mínimo con BeginPlay ya hecho (el mismo arnés que TN_LobbyBotsTest). */
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

	/** PlayerState cuyo dueño es Owner (como en AController::InitPlayerState); se apunta solo en el GameState del mundo. */
	APlayerState* AddState(UWorld* World, AActor* Owner)
	{
		FActorSpawnParameters Params;
		Params.Owner = Owner;
		return World->SpawnActor<APlayerState>(APlayerState::StaticClass(), FTransform::Identity, Params);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSeamlessTravelStalePlayerStatesTest,
	"Tortunabo.Multiplayer.SeamlessTravel.StalePlayerStates",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSeamlessTravelStalePlayerStatesTest::RunTest(const FString& Parameters)
{
	using namespace TNSeamlessTravelTestDetail;

	// La función: fuera los huecos nulos y los PlayerState en destrucción; los vivos se quedan en su orden.
	{
		UWorld* World = CreateGameWorld();
		AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
		APlayerState* First = AddState(World, World->SpawnActor<AActor>());
		APlayerState* Gone = AddState(World, World->SpawnActor<AActor>());
		APlayerState* Second = AddState(World, World->SpawnActor<AActor>());
		if (TestTrue(TEXT("GameState y PlayerState creados"), GameState && First && Gone && Second))
		{
			Gone->Destroy();
			GameState->PlayerArray.Add(Gone); // En destrucción y todavía en el PlayerArray.
			GameState->PlayerArray.Add(nullptr); // Lo que deja el GC de un PlayerState que no viajó.
			TestEqual(TEXT("Quita el nulo y el que se destruye"), TN_RemoveStalePlayerStates(GameState), 2);
			TestTrue(TEXT("Quedan los vivos en su orden"), GameState->PlayerArray.Num() == 2
				&& GameState->PlayerArray[0] == First && GameState->PlayerArray[1] == Second);
			TestEqual(TEXT("Sin nada que quitar, 0"), TN_RemoveStalePlayerStates(GameState), 0);
		}
		TestEqual(TEXT("Sin GameState, 0"), TN_RemoveStalePlayerStates(nullptr), 0);
		DestroyGameWorld(World);
	}

	// El GameState del juego limpia antes de que el motor marque el PlayerArray.
	{
		UWorld* World = CreateGameWorld();
		AGameStateBase* GameState = World->SpawnActor<ATN_CoopGameState>();
		APlayerState* Alive = AddState(World, World->SpawnActor<AActor>());
		APlayerState* Gone = AddState(World, World->SpawnActor<AActor>());
		if (TestTrue(TEXT("ATN_CoopGameState: GameState y PlayerState creados"), GameState && Alive && Gone))
		{
			Gone->Destroy();
			GameState->PlayerArray.Add(Gone);
			GameState->SeamlessTravelTransitionCheckpoint(true);
			TestTrue(TEXT("ATN_CoopGameState: en el PlayerArray solo queda el vivo"),
				GameState->PlayerArray.Num() == 1 && GameState->PlayerArray[0] == Alive);
			TestTrue(TEXT("ATN_CoopGameState: el vivo queda marcado como del mapa anterior"), Alive->IsFromPreviousLevel());
		}
		DestroyGameWorld(World);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
