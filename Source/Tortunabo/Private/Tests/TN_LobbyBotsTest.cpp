// Bots en el lobby (#694): al volver de una partida con bots, los pilotos IA viajaban al lobby con el seamless travel
// (AController::ShouldParticipateInSeamlessTravel es true con PlayerState) y su PlayerState contaba como un jugador más
// que nunca se pone listo: la cuenta atrás no arrancaba y PendingTravelPlayerCount los sumaba a la partida siguiente.
// La regla de «todos listos» y el recuento de conectados solo cuentan humanos.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Lobby.Bots; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_GameModeSpawnUtils.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/WorldSettings.h"
#include "Rally/TN_RallyAIController.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNLobbyBotsTestDetail
{
	/** Mundo de juego mínimo con BeginPlay ya hecho (el mismo arnés que TN_PlayerLeavingTest). */
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

	/** PlayerState coop cuyo dueño es Owner (como en AController::InitPlayerState), apuntado en el GameState. */
	ATN_CoopPlayerState* AddState(UWorld* World, AGameStateBase* GameState, AActor* Owner, bool bReady)
	{
		FActorSpawnParameters Params;
		Params.Owner = Owner;
		ATN_CoopPlayerState* State = World->SpawnActor<ATN_CoopPlayerState>(ATN_CoopPlayerState::StaticClass(), FTransform::Identity, Params);
		if (State)
		{
			State->bIsInReadyZone = bReady;
			GameState->AddPlayerState(State);
		}
		return State;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNLobbyBotsReadyTest,
	"Tortunabo.Lobby.Bots.AllReady",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNLobbyBotsReadyTest::RunTest(const FString& Parameters)
{
	using namespace TNLobbyBotsTestDetail;
	UWorld* World = CreateGameWorld();
	AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
	if (!TestNotNull(TEXT("GameState"), GameState))
	{
		DestroyGameWorld(World);
		return false;
	}

	// Sin bots: dos humanos listos arrancan la cuenta atrás.
	ATN_CoopPlayerState* A = AddState(World, GameState, World->SpawnActor<AActor>(), true);
	ATN_CoopPlayerState* B = AddState(World, GameState, World->SpawnActor<AActor>(), true);
	if (!TestTrue(TEXT("Dos humanos creados"), A && B))
	{
		DestroyGameWorld(World);
		return false;
	}
	const FTNLobbyReadyCount Humans = TN_CountLobbyReady(GameState);
	TestEqual(TEXT("Sin bots: 2 conectados"), Humans.Connected, 2);
	TestTrue(TEXT("Sin bots: 2 de 2 listos arrancan"), Humans.AllReady(1));

	// Con bots: uno marcado con SetIsABot y otro cuyo dueño es un piloto IA (como el que llegaba al lobby con el viaje,
	// HandleSeamlessTravelPlayer → InitPlayerState). Ninguno se pone listo nunca.
	ATN_CoopPlayerState* MarkedBot = AddState(World, GameState, World->SpawnActor<AActor>(), false);
	ATN_RallyAIController* Pilot = World->SpawnActor<ATN_RallyAIController>();
	ATN_CoopPlayerState* TravelledBot = AddState(World, GameState, Pilot, false);
	if (!TestTrue(TEXT("Bots creados"), MarkedBot && Pilot && TravelledBot))
	{
		DestroyGameWorld(World);
		return false;
	}
	MarkedBot->SetIsABot(true);
	TestTrue(TEXT("El marcado es un bot"), TN_IsBotPlayerState(MarkedBot));
	TestTrue(TEXT("El del piloto IA es un bot"), TN_IsBotPlayerState(TravelledBot));
	TestFalse(TEXT("Un humano no es un bot"), TN_IsBotPlayerState(A));

	// El piloto IA con PlayerState no viaja con el seamless travel (vuelta al lobby o ?Restart): AController lo haría.
	Pilot->SetPlayerState(TravelledBot);
	TestFalse(TEXT("El piloto IA no viaja al lobby"), Pilot->ShouldParticipateInSeamlessTravel());

	const FTNLobbyReadyCount WithBots = TN_CountLobbyReady(GameState);
	TestEqual(TEXT("Con bots: siguen 2 conectados"), WithBots.Connected, 2);
	TestEqual(TEXT("Con bots: 2 listos"), WithBots.Ready, 2);
	TestTrue(TEXT("Con bots: los humanos listos arrancan sin esperar a los bots (#694)"), WithBots.AllReady(1));
	TestEqual(TEXT("PendingTravelPlayerCount no suma bots"), TN_CountConnectedCoopPlayers(GameState), 2);

	// Un humano sale de la zona: con bots o sin ellos, no hay cuenta atrás.
	A->bIsInReadyZone = false;
	TestFalse(TEXT("Con bots y un humano sin listo no arranca"), TN_CountLobbyReady(GameState).AllReady(1));

	DestroyGameWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
