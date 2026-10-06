// Consola de prueba de los objetos del coop y del charco de pesca (Docs/Comandos_Prueba.md, «Objetos del coop»). Se escriben
// en la ventana del anfitrión (o de un cliente del PIE: actúan en el mundo del servidor del mismo proceso). Los índices de
// jugador empiezan en 0 (0 = el anfitrión):
//
//   TN.Coop.Item <objeto|list> [jugador=0]     da el objeto a esa tortuga (en la mano; si está llena, sustituye)
//   TN.Coop.FishingPool [jugador=0 | clear]    un charco de pesca 4 m delante de esa tortuga (o quita los de prueba)

#include "Game/TN_CoopItems.h"
#include "Core/TN_Log.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Player/TortugaCharacter.h"
#include "World/TN_FishingPool.h"

namespace TNCoopItemCmd
{
	const FName DebugTag(TEXT("TNCoopDebug"));

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

	/** La tortuga del jugador Index (0 = el anfitrión) en el mundo con autoridad. */
	ATortugaCharacter* TurtleOf(UWorld* AuthWorld, int32 Index)
	{
		const AGameStateBase* GameState = AuthWorld ? AuthWorld->GetGameState() : nullptr;
		if (!GameState || !GameState->PlayerArray.IsValidIndex(Index))
		{
			return nullptr;
		}
		const APlayerState* PlayerState = GameState->PlayerArray[Index];
		return PlayerState ? Cast<ATortugaCharacter>(PlayerState->GetPawn()) : nullptr;
	}

	int32 IndexArg(const TArray<FString>& Args, int32 Position)
	{
		return Args.IsValidIndex(Position) ? FMath::Max(0, FCString::Atoi(*Args[Position])) : 0;
	}

	void ListItems()
	{
		for (const ETNCoopItem Kind : TNCoopItemRules::AllKinds())
		{
			const FTNCoopItemSpec& Spec = TNCoopItemRules::Spec(Kind);
			UE_LOG(LogTortunabo, Display, TEXT("[Coop] %d  %s  (%s): apilado %d, usos %d, peso %.0f"), static_cast<int32>(Kind), Spec.Code,
				*TNCoopItems::DisplayName(Kind).ToString(), Spec.MaxStack, Spec.Uses, Spec.LootWeight);
		}
	}

	void RunItem(const TArray<FString>& Args, UWorld* InWorld)
	{
		if (Args.Num() < 1 || Args[0].Equals(TEXT("list"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogTortunabo, Display, TEXT("[Coop] Uso: TN.Coop.Item <objeto|número> [jugador=0]. Objetos:"));
			ListItems();
			return;
		}
		UWorld* AuthWorld = FindAuthorityWorld(InWorld);
		ETNCoopItem Kind = ETNCoopItem::None;
		if (!AuthWorld || !TNCoopItems::ParseKind(Args[0], Kind))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Coop] TN.Coop.Item: objeto desconocido o sin mundo con autoridad. TN.Coop.Item list los enseña."));
			return;
		}
		ATortugaCharacter* Turtle = TurtleOf(AuthWorld, IndexArg(Args, 1));
		if (!Turtle || !TNCoopItems::GiveItem(Turtle, Kind))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Coop] TN.Coop.Item: no hay tortuga con ese índice o no ha podido recibirlo (¿ya lleva el máximo?)."));
			return;
		}
		UE_LOG(LogTortunabo, Log, TEXT("[Coop] %s recibe %s."), *GetNameSafe(Turtle), TNCoopItemRules::Spec(Kind).Code);
	}

	void RunFishingPool(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* AuthWorld = FindAuthorityWorld(InWorld);
		if (!AuthWorld)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Coop] TN.Coop.FishingPool: sin mundo con autoridad (escríbelo en el anfitrión)."));
			return;
		}
		if (Args.Num() > 0 && Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
		{
			int32 Removed = 0;
			for (TActorIterator<ATN_FishingPool> It(AuthWorld); It; ++It)
			{
				if (It->Tags.Contains(DebugTag))
				{
					It->Destroy();
					++Removed;
				}
			}
			UE_LOG(LogTortunabo, Log, TEXT("[Coop] %d charcos de prueba quitados."), Removed);
			return;
		}
		const ATortugaCharacter* Turtle = TurtleOf(AuthWorld, IndexArg(Args, 0));
		if (!Turtle)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Coop] TN.Coop.FishingPool: no hay tortuga con ese índice."));
			return;
		}
		const FVector Forward = FVector(Turtle->GetActorForwardVector().X, Turtle->GetActorForwardVector().Y, 0.0).GetSafeNormal();
		FVector Where = Turtle->GetActorLocation() + Forward * 400.0;
		// Al suelo de debajo (sin suelo a mano, a la altura de los pies).
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CoopFishingPoolCmd), false, Turtle);
		if (AuthWorld->LineTraceSingleByObjectType(Hit, Where + FVector(0.0, 0.0, 200.0), Where - FVector(0.0, 0.0, 1000.0),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			Where = Hit.ImpactPoint;
		}
		else
		{
			Where.Z -= Turtle->GetSimpleCollisionHalfHeight();
		}
		if (ATN_FishingPool* Pool = ATN_FishingPool::ServerSpawn(AuthWorld, Where, static_cast<float>(Turtle->GetActorRotation().Yaw)))
		{
			Pool->Tags.Add(DebugTag);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs ItemCmd(
		TEXT("TN.Coop.Item"),
		TEXT("TN.Coop.Item <objeto|list> [jugador=0]: da un objeto del coop (pez globo, cáscara, concha, arpón...) a esa tortuga."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunItem), ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs FishingPoolCmd(
		TEXT("TN.Coop.FishingPool"),
		TEXT("TN.Coop.FishingPool [jugador=0 | clear]: un charco de pesca 4 m delante de esa tortuga (clear quita los de prueba)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunFishingPool), ECVF_Cheat);
}
