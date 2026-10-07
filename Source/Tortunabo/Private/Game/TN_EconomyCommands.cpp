// Consola de prueba de la economía (Docs/Comandos_Prueba.md, «Economía»). Se escriben en la ventana del anfitrión (o de un
// cliente del PIE: actúan en el mundo del servidor del mismo proceso). Los índices de jugador empiezan en 0 (el anfitrión):
//
//   TN.Chapas.Give <n> [jugador=0]     da n chapas a esa tortuga
//   TN.Chapas.Spawn <n> [jugador=0]    suelta n chapas 3 m delante de esa tortuga

#include "Core/TN_Log.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/TN_Chapa.h"

#if !UE_BUILD_SHIPPING

namespace TNEconomyCmd
{
	/** El mundo con autoridad del mismo proceso (el propio si no es un cliente; en PIE, el del servidor del mismo mapa). */
	UWorld* AuthorityWorld(UWorld* InWorld)
	{
		if (!InWorld || InWorld->GetNetMode() != NM_Client)
		{
			return InWorld;
		}
		if (!GEngine)
		{
			return nullptr;
		}
		const FString MapName = UWorld::RemovePIEPrefix(InWorld->GetMapName());
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* Candidate = Context.World();
			if (Candidate && Candidate != InWorld && Candidate->IsGameWorld() && Candidate->GetNetMode() != NM_Client
				&& UWorld::RemovePIEPrefix(Candidate->GetMapName()) == MapName)
			{
				return Candidate;
			}
		}
		return nullptr;
	}

	/** La tortuga del jugador Index (0 = el anfitrión) en el mundo con autoridad. */
	ATortugaCharacter* PlayerTurtle(UWorld* AuthWorld, int32 Index)
	{
		const AGameStateBase* GameState = AuthWorld ? AuthWorld->GetGameState() : nullptr;
		if (!GameState || !GameState->PlayerArray.IsValidIndex(Index))
		{
			return nullptr;
		}
		const APlayerState* PlayerState = GameState->PlayerArray[Index];
		return PlayerState ? Cast<ATortugaCharacter>(PlayerState->GetPawn()) : nullptr;
	}

	int32 IntArg(const TArray<FString>& Args, int32 Position, int32 Default)
	{
		return Args.IsValidIndex(Position) ? FMath::Max(0, FCString::Atoi(*Args[Position])) : Default;
	}

	/** Delante de la tortuga, en planta. */
	FVector InFront(const ATortugaCharacter& Turtle, double Distance)
	{
		const FVector Forward = FVector(Turtle.GetActorForwardVector().X, Turtle.GetActorForwardVector().Y, 0.0).GetSafeNormal();
		return Turtle.GetActorLocation() + Forward * Distance;
	}

	void RunGive(const TArray<FString>& Args, UWorld* InWorld)
	{
		ATortugaCharacter* Turtle = PlayerTurtle(AuthorityWorld(InWorld), IntArg(Args, 1, 0));
		UTN_InventoryComponent* Inventory = Turtle ? Turtle->GetInventoryComponent() : nullptr;
		if (!Inventory)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Chapas] TN.Chapas.Give: no hay tortuga con ese índice (escríbelo en el anfitrión)."));
			return;
		}
		const int32 Added = Inventory->AddChapas(IntArg(Args, 0, 5));
		UE_LOG(LogTortunabo, Log, TEXT("[Chapas] %s recibe %d chapas (lleva %d)."), *GetNameSafe(Turtle), Added, Inventory->GetChapaCount());
	}

	void RunSpawn(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* AuthWorld = AuthorityWorld(InWorld);
		const ATortugaCharacter* Turtle = PlayerTurtle(AuthWorld, IntArg(Args, 1, 0));
		if (!Turtle)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Chapas] TN.Chapas.Spawn: no hay tortuga con ese índice (escríbelo en el anfitrión)."));
			return;
		}
		const int32 Spawned = ATN_Chapa::SpawnChapas(AuthWorld, InFront(*Turtle, 300.0), IntArg(Args, 0, 5));
		UE_LOG(LogTortunabo, Log, TEXT("[Chapas] %d chapas sueltas delante de %s."), Spawned, *GetNameSafe(Turtle));
	}

	FAutoConsoleCommandWithWorldAndArgs GiveCmd(
		TEXT("TN.Chapas.Give"),
		TEXT("TN.Chapas.Give <n=5> [jugador=0]: da n chapas a esa tortuga."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunGive), ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs SpawnCmd(
		TEXT("TN.Chapas.Spawn"),
		TEXT("TN.Chapas.Spawn <n=5> [jugador=0]: suelta n chapas 3 m delante de esa tortuga."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSpawn), ECVF_Cheat);
}

#endif // !UE_BUILD_SHIPPING
