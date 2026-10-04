// Jugador que se va (#558, #559): AController::Destroyed llama a GameMode->Logout antes de CleanupPlayerState, así que
// durante Logout su PlayerState sigue en GameState->PlayerArray. Los recuentos de la ronda (Clásico y Coop) y del lobby
// no deben contarlo: si no, cuando se iba el único que faltaba por llegar o por ponerse listo, la ronda o la cuenta
// atrás no se reevaluaban nunca. Se reproduce ese momento con un mundo de juego: el manejador de OnActorDestroyed corre
// con el dueño del PlayerState ya marcado en destrucción y el PlayerState todavía en el PlayerArray, igual que Logout.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Net.PlayerLeaving; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_GameModeSpawnUtils.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPlayerLeavingRulesTest,
	"Tortunabo.Net.PlayerLeaving.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPlayerLeavingRulesTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Sin jugadores la ronda no acaba"), FTNCoopRoundCount{ 0, 0, 0 }.IsRoundOver());
	TestFalse(TEXT("1 de 2 en la meta y alguien vivo: sigue"), FTNCoopRoundCount{ 2, 1, 2 }.IsRoundOver());
	TestTrue(TEXT("Todos en la meta: acaba"), FTNCoopRoundCount{ 1, 1, 1 }.IsRoundOver());
	TestTrue(TEXT("Nadie vivo: acaba"), FTNCoopRoundCount{ 2, 0, 0 }.IsRoundOver());

	TestFalse(TEXT("Lobby vacío: no hay cuenta atrás"), FTNLobbyReadyCount{ 0, 0 }.AllReady(1));
	TestFalse(TEXT("2 de 3 listos: no hay cuenta atrás"), FTNLobbyReadyCount{ 3, 2 }.AllReady(1));
	TestTrue(TEXT("2 de 2 listos: cuenta atrás"), FTNLobbyReadyCount{ 2, 2 }.AllReady(1));
	TestFalse(TEXT("Por debajo del mínimo no arranca"), FTNLobbyReadyCount{ 1, 1 }.AllReady(2));
	TestTrue(TEXT("Un PlayerState nulo cuenta como ido"), TN_IsPlayerStateLeaving(nullptr));
	return true;
}

namespace TNPlayerLeavingTestDetail
{
	/** Mundo de juego mínimo con BeginPlay ya hecho (el mismo arnés que TN_BeachTickWakeTest). */
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

	/** Un jugador: un actor que hace de controlador (dueño, como en AController::InitPlayerState) y su PlayerState. */
	struct FPlayer
	{
		AActor* Controller = nullptr;
		ATN_CoopPlayerState* State = nullptr;
	};

	FPlayer AddPlayer(UWorld* World, AGameStateBase* GameState)
	{
		FPlayer Player;
		Player.Controller = World->SpawnActor<AActor>();
		FActorSpawnParameters Params;
		Params.Owner = Player.Controller;
		Player.State = World->SpawnActor<ATN_CoopPlayerState>(ATN_CoopPlayerState::StaticClass(), FTransform::Identity, Params);
		if (Player.State)
		{
			// APlayerState::PostInitializeComponents ya lo apunta (el GameState del mundo es este); AddUnique no lo repite.
			GameState->AddPlayerState(Player.State);
		}
		return Player;
	}

	/** Destruye el controlador y llama a Measure en el momento de Logout: ya en destrucción y su PlayerState aún en la lista. */
	void DestroyControllerAndMeasure(UWorld* World, AActor* Controller, TFunctionRef<void()> Measure)
	{
		bool bMeasured = false;
		const FDelegateHandle Handle = World->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateLambda(
			[Controller, &Measure, &bMeasured](AActor* Destroyed)
			{
				if (Destroyed == Controller && !bMeasured)
				{
					bMeasured = true;
					Measure();
				}
			}));
		Controller->Destroy();
		World->RemoveOnActorDestroyedHandler(Handle);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPlayerLeavingRoundTest,
	"Tortunabo.Net.PlayerLeaving.Round",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPlayerLeavingRoundTest::RunTest(const FString& Parameters)
{
	using namespace TNPlayerLeavingTestDetail;
	UWorld* World = CreateGameWorld();
	AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
	if (!TestNotNull(TEXT("GameState"), GameState))
	{
		DestroyGameWorld(World);
		return false;
	}

	// Coop con 2: A llega a la meta y B, vivo, se desconecta.
	const FPlayer A = AddPlayer(World, GameState);
	const FPlayer B = AddPlayer(World, GameState);
	if (!TestTrue(TEXT("Dos jugadores creados"), A.State && B.State && A.Controller && B.Controller))
	{
		DestroyGameWorld(World);
		return false;
	}
	A.State->bHasFinishedRun = true;

	const FTNCoopRoundCount Before = TN_CountCoopRound(GameState);
	TestEqual(TEXT("Antes cuentan los dos"), Before.Total, 2);
	TestFalse(TEXT("Con B en carrera la ronda sigue"), Before.IsRoundOver());
	TestFalse(TEXT("B no se está yendo"), TN_IsPlayerStateLeaving(B.State));

	FTNCoopRoundCount During;
	int32 ConnectedDuring = -1;
	int32 InPlayerArray = -1;
	bool bLeavingDuring = false;
	DestroyControllerAndMeasure(World, B.Controller, [&]()
	{
		During = TN_CountCoopRound(GameState);
		ConnectedDuring = TN_CountConnectedCoopPlayers(GameState);
		InPlayerArray = GameState->PlayerArray.Num();
		bLeavingDuring = TN_IsPlayerStateLeaving(B.State);
	});

	TestEqual(TEXT("En Logout el PlayerState de B sigue en el PlayerArray (la causa de #558)"), InPlayerArray, 2);
	TestTrue(TEXT("En Logout B se reconoce como ido"), bLeavingDuring);
	TestEqual(TEXT("En Logout la ronda ya no cuenta a B"), During.Total, 1);
	TestEqual(TEXT("A sigue resuelto"), During.Resolved, 1);
	TestTrue(TEXT("En Logout la ronda acaba: StartResults (#558)"), During.IsRoundOver());
	TestEqual(TEXT("ConnectedPlayers ya no cuenta a B"), ConnectedDuring, 1);
	TestFalse(TEXT("A no se está yendo"), TN_IsPlayerStateLeaving(A.State));

	DestroyGameWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPlayerLeavingLobbyTest,
	"Tortunabo.Net.PlayerLeaving.Lobby",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPlayerLeavingLobbyTest::RunTest(const FString& Parameters)
{
	using namespace TNPlayerLeavingTestDetail;
	UWorld* World = CreateGameWorld();
	AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
	if (!TestNotNull(TEXT("GameState"), GameState))
	{
		DestroyGameWorld(World);
		return false;
	}

	// Lobby con 3: A y B listos, C sin estar listo cierra el juego.
	const FPlayer A = AddPlayer(World, GameState);
	const FPlayer B = AddPlayer(World, GameState);
	const FPlayer C = AddPlayer(World, GameState);
	if (!TestTrue(TEXT("Tres jugadores creados"), A.State && B.State && C.State && C.Controller))
	{
		DestroyGameWorld(World);
		return false;
	}
	A.State->bIsInReadyZone = true;
	B.State->bIsInReadyZone = true;

	const FTNLobbyReadyCount Before = TN_CountLobbyReady(GameState);
	TestEqual(TEXT("Antes: 3 conectados"), Before.Connected, 3);
	TestEqual(TEXT("Antes: 2 listos"), Before.Ready, 2);
	TestFalse(TEXT("Con C sin listo no hay cuenta atrás"), Before.AllReady(1));

	// ATN_HQGameMode::Logout pone bIsInReadyZone a false y llama a RefreshLobbyState.
	C.State->bIsInReadyZone = false;
	FTNLobbyReadyCount During;
	DestroyControllerAndMeasure(World, C.Controller, [&]() { During = TN_CountLobbyReady(GameState); });

	TestEqual(TEXT("En Logout ya no cuenta a C: 2 conectados"), During.Connected, 2);
	TestEqual(TEXT("2 listos"), During.Ready, 2);
	TestTrue(TEXT("En Logout arranca la cuenta atrás (#559)"), During.AllReady(1));

	DestroyGameWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
