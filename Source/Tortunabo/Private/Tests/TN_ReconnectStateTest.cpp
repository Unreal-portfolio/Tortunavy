// Lo que recupera quien vuelve a la sala (#345): el PlayerState inactivo que AGameMode::AddInactivePlayer crea con
// APlayerState::Duplicate y que ATN_CoopPlayerState::CopyProperties rellena. Mundo de juego mínimo, sin red. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Net.ReconnectState; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Core/TN_CoopPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Mundo de juego vacío que se destruye al salir del ámbito. */
	struct FTNScopedGameWorld
	{
		UWorld* World = nullptr;

		FTNScopedGameWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
		}

		~FTNScopedGameWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	ATN_CoopPlayerState* SpawnRacer(UWorld* World, int32 Score, bool bAlive, bool bDBNO, bool bFinished)
	{
		ATN_CoopPlayerState* PS = World->SpawnActor<ATN_CoopPlayerState>();
		PS->RaceScore = Score;
		PS->bIsAlive = bAlive;
		PS->bIsDBNO = bDBNO;
		PS->bHasFinishedRun = bFinished;
		PS->FinishRank = bFinished ? 2 : 0;
		PS->TurtleDollsCollected = 1;
		PS->EquippedHelmetId = FName(TEXT("Casco"));
		return PS;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNReconnectStateTest,
	"Tortunabo.Net.ReconnectState",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNReconnectStateTest::RunTest(const FString& Parameters)
{
	FTNScopedGameWorld Scope;
	if (!TestNotNull(TEXT("Mundo de prueba"), Scope.World))
	{
		return false;
	}

	// Al desconectarse (Duplicate, como AddInactivePlayer): vuelve con sus puntos, su meta y su casco.
	const ATN_CoopPlayerState* Finished = Cast<ATN_CoopPlayerState>(SpawnRacer(Scope.World, 350, true, false, true)->Duplicate());
	if (!TestNotNull(TEXT("Copia del que llegó a la meta"), Finished))
	{
		return false;
	}
	TestEqual(TEXT("Conserva los puntos"), Finished->RaceScore, 350);
	TestTrue(TEXT("Conserva la meta"), Finished->bHasFinishedRun && Finished->FinishRank == 2);
	TestEqual(TEXT("Conserva los muñecos"), Finished->TurtleDollsCollected, 1);
	TestEqual(TEXT("Conserva el casco"), Finished->EquippedHelmetId, FName(TEXT("Casco")));

	// Muerto sigue muerto; derribado vuelve muerto.
	const ATN_CoopPlayerState* Dead = Cast<ATN_CoopPlayerState>(SpawnRacer(Scope.World, 40, false, false, false)->Duplicate());
	TestFalse(TEXT("Muerto al irse: muerto al volver"), Dead && Dead->bIsAlive);
	const ATN_CoopPlayerState* Downed = Cast<ATN_CoopPlayerState>(SpawnRacer(Scope.World, 40, true, true, false)->Duplicate());
	TestTrue(TEXT("Derribado al irse: muerto y sin derribo"), Downed && !Downed->bIsAlive && !Downed->bIsDBNO);

	// El viaje sin cortes no arrastra la carrera anterior.
	ATN_CoopPlayerState* Traveled = Scope.World->SpawnActor<ATN_CoopPlayerState>();
	SpawnRacer(Scope.World, 500, false, false, true)->SeamlessTravelTo(Traveled);
	TestTrue(TEXT("Viaje: carrera a cero"), Traveled->RaceScore == 0 && Traveled->bIsAlive && !Traveled->bHasFinishedRun);
	return true;
}

#endif
