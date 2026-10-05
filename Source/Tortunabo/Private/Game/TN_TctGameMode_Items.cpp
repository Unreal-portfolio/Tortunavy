// Objetos de Todos contra Todos (ATN_TctGameMode): puntos de objetos en la arena, manos vacías al empezar cada ronda y
// comandos de prueba (TN.Tct.Item, TN.Tct.Items).

#include "Game/TN_TctGameMode.h"
#include "Game/TN_TctItemComponent.h"
#include "Game/TN_TctItems.h"
#include "Core/TN_Log.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_RaceItemComponent.h"
#include "World/TN_TctArena.h"
#include "World/TN_TctItemPad.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"

namespace TNTctGameModeItemsDetail
{
	/** El objeto escrito en la consola: su nombre (Shovel, grapple...) o su número; None si no es ninguno. */
	ETNTctItem ParseKind(const FString& Text)
	{
		if (Text.IsNumeric())
		{
			const int32 Index = FCString::Atoi(*Text);
			return (Index > 0 && Index < static_cast<int32>(ETNTctItem::Count)) ? static_cast<ETNTctItem>(Index) : ETNTctItem::None;
		}
		for (const ETNTctItem Kind : TNTctItemRules::AllKinds())
		{
			if (Text.Equals(TNTctItemRules::Spec(Kind).Code, ESearchCase::IgnoreCase))
			{
				return Kind;
			}
		}
		return ETNTctItem::None;
	}

	void WithGameMode(const UWorld* World, const TCHAR* Command, TFunctionRef<void(ATN_TctGameMode&)> Action)
	{
		if (ATN_TctGameMode* GM = World ? World->GetAuthGameMode<ATN_TctGameMode>() : nullptr)
		{
			Action(*GM);
			return;
		}
		UE_LOG(LogTortunabo, Warning, TEXT("[TcT] %s: solo en el anfitrión y en Todos contra Todos (?game=Tct)."), Command);
	}

	FAutoConsoleCommandWithWorldAndArgs CmdItem(TEXT("TN.Tct.Item"),
		TEXT("Todos contra Todos: da un objeto a la tortuga N. TN.Tct.Item <objeto: KnockoutPistol, AirBlunderbuss, Grapple, Shovel, BeachBall, Anchor, JellyDart, InkPistol, Ball, ConchTrap, BigHead, SandMine, Frisbee, Cocobomba, Alga, GaviotaLadrona o su número> [jugadora = 0]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const ETNTctItem Kind = Args.Num() > 0 ? ParseKind(Args[0]) : ETNTctItem::None;
			if (Kind == ETNTctItem::None)
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[TcT] TN.Tct.Item: objeto desconocido."));
				return;
			}
			const int32 Player = Args.IsValidIndex(1) ? FCString::Atoi(*Args[1]) : 0;
			WithGameMode(World, TEXT("TN.Tct.Item"), [Kind, Player](ATN_TctGameMode& GM) { GM.DebugGiveItem(Player, Kind); });
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdItems(TEXT("TN.Tct.Items"),
		TEXT("Todos contra Todos: todos los puntos de objetos sacan ya uno nuevo."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			WithGameMode(World, TEXT("TN.Tct.Items"), [](ATN_TctGameMode& GM) { GM.DebugRespawnItems(); });
		}));
}

void ATN_TctGameMode::CreateItemPads()
{
	UWorld* World = GetWorld();
	ItemPads.Reset();
	if (!World)
	{
		return;
	}
	// Los colocados a mano en el nivel mandan.
	for (TActorIterator<ATN_TctItemPad> It(World); It; ++It)
	{
		ItemPads.Add(*It);
	}
	if (ItemPads.Num() > 0 || !Arena || ItemPadCount <= 0)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[TcT] Puntos de objetos: %d del nivel."), ItemPads.Num());
		return;
	}
	TArray<FVector> Avoid;
	for (const APlayerStart* Start : SpawnPoints)
	{
		if (IsValid(Start))
		{
			Avoid.Add(Start->GetActorLocation());
		}
	}
	const TArray<FVector>& Candidates = Arena->GetSpawnCandidates();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const int32 Index : TNTctItemRules::PickPadPoints(Candidates, ItemPadCount, Arena->GetGroundBox().GetCenter(), Avoid, ItemPadMinFromSpawn))
	{
		const FVector Where = Candidates[Index] + FVector(0.0, 0.0, 2.0);
		if (ATN_TctItemPad* Pad = World->SpawnActor<ATN_TctItemPad>(ATN_TctItemPad::StaticClass(), FTransform(Where), Params))
		{
			ItemPads.Add(Pad);
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] Puntos de objetos: %d repartidos por la arena (de %d sitios)."), ItemPads.Num(), Candidates.Num());
}

void ATN_TctGameMode::StartItemPads()
{
	const UWorld* World = GetWorld();
	const int32 Active = TNTctItemRules::ActivePadCount(StartingPlayers, ItemPads.Num());
	for (int32 Index = 0; Index < ItemPads.Num(); ++Index)
	{
		ATN_TctItemPad* Pad = ItemPads[Index];
		if (!IsValid(Pad))
		{
			continue;
		}
		if (Index < Active && World)
		{
			Pad->ServerStartRound(World->GetTimeSeconds(), Index);
		}
		else
		{
			Pad->ServerStopRound();
		}
	}
}

void ATN_TctGameMode::StopItemPads()
{
	for (ATN_TctItemPad* Pad : ItemPads)
	{
		if (IsValid(Pad))
		{
			Pad->ServerStopRound();
		}
	}
}

void ATN_TctGameMode::ResetItemsForRound(APawn* Pawn) const
{
	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Pawn);
	if (!Turtle)
	{
		return;
	}
	TNTctItems::ClearInventory(Turtle);
	// Los componentes de los objetos, ya creados (y replicados) antes del primer disparo: si nacieran con él, el multicast de
	// la estela o del sonido llegaría a los clientes antes que el componente y se perdería.
	if (UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOrAddOn(Turtle))
	{
		Effects->ClearEffects();
	}
	UTN_RaceItemComponent::FindOrAddOn(Turtle);
}

void ATN_TctGameMode::DebugGiveItem(int32 PlayerIndex, ETNTctItem Kind)
{
	const TArray<APlayerController*> Controllers = GetPlayingControllers();
	ATortugaCharacter* Turtle = Controllers.IsValidIndex(PlayerIndex) ? Cast<ATortugaCharacter>(Controllers[PlayerIndex]->GetPawn()) : nullptr;
	if (!Turtle)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[TcT] TN.Tct.Item: no hay tortuga %d (hay %d jugadoras)."), PlayerIndex, Controllers.Num());
		return;
	}
	const bool bGiven = TNTctItems::GiveItem(Turtle, Kind);
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] TN.Tct.Item: %s a %s%s."), TNTctItemRules::Spec(Kind).Code, *GetNameSafe(Turtle),
		bGiven ? TEXT("") : TEXT(" (no se ha podido: falta su fila en DT_Items)"));
}

void ATN_TctGameMode::DebugRespawnItems()
{
	if (!bRoundLive)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[TcT] TN.Tct.Items: sin ronda en juego."));
		return;
	}
	// La ronda vuelve a empezar en los puntos: se llevan lo que tengan y sacan uno nuevo al momento.
	StartItemPads();
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] TN.Tct.Items: %d puntos de objetos reiniciados."), ItemPads.Num());
}
