// Vuelta al lobby sin cortes (#711): el anfitrión crasheaba al volver al lobby desde el Rally y los Karts. Los bots no viajan
// (#694), pero sus PlayerState seguían en el PlayerArray del GameState, que sí viaja al mapa de transición; tras el GC del mapa
// viejo quedaban huecos nulos y AGameStateBase::SeamlessTravelTransitionCheckpoint los leía al salir hacia el lobby.
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
#include "Rally/TN_RallyAIController.h"
#include "Rally/TN_RallyGameState.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSeamlessTravelBotsStayBehindTest,
	"Tortunabo.Multiplayer.SeamlessTravel.BotsStayBehind",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSeamlessTravelBotsStayBehindTest::RunTest(const FString& Parameters)
{
	using namespace TNSeamlessTravelTestDetail;
	UWorld* World = CreateGameWorld();
	AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
	APlayerState* Human = AddState(World, World->SpawnActor<AActor>());
	ATN_RallyAIController* Pilot = World->SpawnActor<ATN_RallyAIController>();
	APlayerState* PilotBot = AddState(World, Pilot);
	APlayerState* MarkedBot = AddState(World, World->SpawnActor<AActor>());
	if (!TestTrue(TEXT("Mundo, GameState, humano y bots creados"), GameState && Human && Pilot && PilotBot && MarkedBot))
	{
		DestroyGameWorld(World);
		return false;
	}
	MarkedBot->SetIsABot(true);
	TestEqual(TEXT("Los tres en el PlayerArray"), GameState->PlayerArray.Num(), 3);

	// Como AGameModeBase::GetSeamlessTravelActorList hacia la transición: el PlayerArray entero y el GameState.
	TArray<AActor*> ActorList;
	ActorList.Append(GameState->PlayerArray);
	ActorList.Add(GameState);
	TestEqual(TEXT("Se quedan los dos bots"), TN_DropBotsFromSeamlessTravel(GameState, ActorList), 2);
	TestTrue(TEXT("El humano viaja"), ActorList.Contains(Human));
	TestTrue(TEXT("El GameState viaja"), ActorList.Contains(GameState));
	TestFalse(TEXT("El bot del piloto IA no viaja (#694)"), ActorList.Contains(PilotBot));
	TestFalse(TEXT("El bot marcado no viaja (#694)"), ActorList.Contains(MarkedBot));

	// Lo que se rompía (#711): el GameState que viaja solo lleva PlayerState que viajan con él.
	bool bAllTravel = true;
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		bAllTravel &= ActorList.Contains(PlayerState);
	}
	TestTrue(TEXT("Todo el PlayerArray del GameState que viaja está en la lista del viaje"), bAllTravel);
	TestEqual(TEXT("En el PlayerArray solo queda el humano"), GameState->PlayerArray.Num(), 1);

	// Los bots mueren con el mapa viejo; en la transición, el motor marca a los que viajaron.
	PilotBot->Destroy();
	MarkedBot->Destroy();
	GameState->SeamlessTravelTransitionCheckpoint(false);
	TestTrue(TEXT("El humano llega marcado como del mapa anterior"), Human->IsFromPreviousLevel());

	// Sin GameState (no viaja), solo se filtra la lista.
	TArray<AActor*> OnlyList = { Human };
	TestEqual(TEXT("Sin GameState y sin bots en la lista, no se queda nadie"), TN_DropBotsFromSeamlessTravel(nullptr, OnlyList), 0);
	TestEqual(TEXT("La lista sigue con el humano"), OnlyList.Num(), 1);

	DestroyGameWorld(World);
	return true;
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

	// Los GameState del juego limpian antes de que el motor marque el PlayerArray (Rally y Karts, y los del cooperativo).
	for (UClass* StateClass : { ATN_RallyGameState::StaticClass(), ATN_CoopGameState::StaticClass() })
	{
		UWorld* World = CreateGameWorld();
		AGameStateBase* GameState = World->SpawnActor<AGameStateBase>(StateClass);
		APlayerState* Alive = AddState(World, World->SpawnActor<AActor>());
		APlayerState* Gone = AddState(World, World->SpawnActor<AActor>());
		if (TestTrue(FString::Printf(TEXT("%s: GameState y PlayerState creados"), *GetNameSafe(StateClass)), GameState && Alive && Gone))
		{
			Gone->Destroy();
			GameState->PlayerArray.Add(Gone);
			GameState->SeamlessTravelTransitionCheckpoint(true);
			TestTrue(FString::Printf(TEXT("%s: en el PlayerArray solo queda el vivo"), *GetNameSafe(StateClass)),
				GameState->PlayerArray.Num() == 1 && GameState->PlayerArray[0] == Alive);
			TestTrue(FString::Printf(TEXT("%s: el vivo queda marcado como del mapa anterior"), *GetNameSafe(StateClass)),
				Alive->IsFromPreviousLevel());
		}
		DestroyGameWorld(World);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
