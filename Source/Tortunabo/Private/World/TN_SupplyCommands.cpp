// Consola de prueba de las cajas de suministros (Docs/Comandos_Prueba.md, «Cajas y airdrop»). Se escriben en la ventana del
// anfitrión (o de un cliente del PIE: actúan en el mundo del servidor del mismo proceso). Los índices de jugador empiezan
// en 0 (0 = el anfitrión):
//
//   TN.Supply.Crate [jugador=0 | clear]   una caja de suministros 3 m delante de esa tortuga (o quita las de prueba)
//   TN.Airdrop.Force [here] [jugador=0]   un airdrop ya: en un punto de airdrop libre o, si no hay (o con «here»),
//                                         8 m delante de esa tortuga. No cuenta para el máximo de la partida.

#include "World/TN_AirdropSubsystem.h"
#include "World/TN_SupplyCrate.h"
#include "World/TN_SupplyDrop.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"

namespace TNSupplyCmd
{
	const FName DebugTag(TEXT("TNSupplyDebug"));

	/** El mundo con autoridad del mismo proceso (el propio si no es un cliente; en PIE, el del servidor del mismo mapa). */
	UWorld* FindAuthorityWorld(UWorld* InWorld)
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

	/** El pawn del jugador Index (0 = el anfitrión) en el mundo con autoridad. */
	APawn* PawnOf(UWorld* AuthWorld, int32 Index)
	{
		const AGameStateBase* GameState = AuthWorld ? AuthWorld->GetGameState() : nullptr;
		if (!GameState || !GameState->PlayerArray.IsValidIndex(Index))
		{
			return nullptr;
		}
		const APlayerState* PlayerState = GameState->PlayerArray[Index];
		return PlayerState ? PlayerState->GetPawn() : nullptr;
	}

	int32 IndexArg(const TArray<FString>& Args, int32 Position)
	{
		return Args.IsValidIndex(Position) ? FMath::Max(0, FCString::Atoi(*Args[Position])) : 0;
	}

	/** Punto en el suelo Distance cm delante de Pawn (el de su altura si no hay suelo a mano). */
	FVector GroundAhead(const APawn* Pawn, float Distance)
	{
		const FVector Ahead = Pawn->GetActorLocation() + Pawn->GetActorForwardVector().GetSafeNormal2D() * Distance;
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(TN_SupplyCmdGround), false, Pawn);
		if (Pawn->GetWorld()->LineTraceSingleByObjectType(Hit, Ahead + FVector(0.f, 0.f, 300.f), Ahead - FVector(0.f, 0.f, 3000.f),
			FCollisionObjectQueryParams(ECC_WorldStatic), Query))
		{
			return Hit.ImpactPoint;
		}
		return Ahead - FVector(0.f, 0.f, 90.f);
	}

	template <typename TActor>
	int32 ClearDebugActors(UWorld* AuthWorld)
	{
		int32 Removed = 0;
		for (TActorIterator<TActor> It(AuthWorld); It; ++It)
		{
			if (It->Tags.Contains(DebugTag))
			{
				It->Destroy();
				++Removed;
			}
		}
		return Removed;
	}

	void RunCrate(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* AuthWorld = FindAuthorityWorld(InWorld);
		if (!AuthWorld)
		{
			UE_LOG(LogTNLoot, Warning, TEXT("TN.Supply.Crate: sin mundo con autoridad (escríbelo en el anfitrión)."));
			return;
		}
		if (Args.Num() > 0 && Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogTNLoot, Log, TEXT("%d cajas de prueba quitadas."), ClearDebugActors<ATN_SupplyCrate>(AuthWorld));
			return;
		}
		const APawn* Pawn = PawnOf(AuthWorld, IndexArg(Args, 0));
		if (!Pawn)
		{
			UE_LOG(LogTNLoot, Warning, TEXT("TN.Supply.Crate: no hay tortuga con ese índice."));
			return;
		}
		const float Yaw = Pawn->GetActorRotation().Yaw + 180.f;
		if (ATN_SupplyCrate* Crate = ATN_SupplyCrate::ServerSpawn(AuthWorld, GroundAhead(Pawn, 300.f), Yaw))
		{
			Crate->Tags.Add(DebugTag);
		}
	}

	void RunAirdrop(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* AuthWorld = FindAuthorityWorld(InWorld);
		UTN_AirdropSubsystem* Airdrops = AuthWorld ? AuthWorld->GetSubsystem<UTN_AirdropSubsystem>() : nullptr;
		if (!Airdrops)
		{
			UE_LOG(LogTNLoot, Warning, TEXT("TN.Airdrop.Force: sin mundo de juego con autoridad (escríbelo en el anfitrión, en partida)."));
			return;
		}
		const bool bHere = Args.Num() > 0 && Args[0].Equals(TEXT("here"), ESearchCase::IgnoreCase);
		const APawn* Pawn = PawnOf(AuthWorld, IndexArg(Args, bHere ? 1 : 0));
		const FVector Ahead = Pawn ? GroundAhead(Pawn, 800.f) : FVector::ZeroVector;
		ATN_SupplyDrop* Drop = bHere ? (Pawn ? Airdrops->LaunchAt(Ahead) : nullptr) : Airdrops->ForceDrop(Pawn ? &Ahead : nullptr);
		if (!Drop)
		{
			UE_LOG(LogTNLoot, Warning, TEXT("TN.Airdrop.Force: no se ha podido lanzar (sin puntos libres ni tortuga con ese índice)."));
			return;
		}
		Drop->Tags.Add(DebugTag);
	}

	FAutoConsoleCommandWithWorldAndArgs AirdropCommand(
		TEXT("TN.Airdrop.Force"),
		TEXT("Lanza un airdrop ya: en un punto libre o, si no hay (o con here), 8 m delante de una tortuga. TN.Airdrop.Force [here] [jugador=0]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunAirdrop));

	FAutoConsoleCommandWithWorldAndArgs CrateCommand(
		TEXT("TN.Supply.Crate"),
		TEXT("Caja de suministros de prueba 3 m delante de una tortuga: TN.Supply.Crate [jugador=0 | clear]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunCrate));
}
