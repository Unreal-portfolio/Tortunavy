// Objetos de Todos contra Todos (ATN_TctGameMode): puntos de objetos en la arena, manos vacías al empezar cada ronda y
// comandos de prueba (TN.Tct.Item, TN.Tct.Items).

#include "Game/TN_TctGameMode.h"
#include "Game/TN_TctItemComponent.h"
#include "Game/TN_TctGameState.h"
#include "Game/TN_TctItems.h"
#include "Core/TN_Log.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/TN_TurtleMovementComponent.h"
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
		TEXT("Todos contra Todos: da un objeto a la tortuga N. TN.Tct.Item <objeto: KnockoutPistol, AirBlunderbuss, Grapple, Shovel, BeachBall, Anchor, JellyDart, InkPistol, Ball, ConchTrap, BigHead, SandMine, Frisbee, Cocobomba, Alga, GaviotaLadrona, Flotador, MedusaTrampolin, Cohete, BotasMuelle, Aletas, Cambiazo, Burbuja, Puas, Red, Remolino, TaponMarea, Paraguas o su número> [jugadora = 0]"),
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
	const TArray<float>& Exposure = Arena->GetSpawnExposure();
	// Cada sitio con su altura sobre el agua (0-1) y lo expuesto que está (#830): los más altos y expuestos dan lo mejor.
	const float BaseZ = Arena->GetBaseWaterZ();
	const float Span = FMath::Max(1.f, Arena->GetTopZ() - BaseZ);
	TArray<FTNTctPadSpot> Spots;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		FTNTctPadSpot& Spot = Spots.AddDefaulted_GetRef();
		Spot.Pos = Candidates[Index];
		Spot.HeightFrac = FMath::Clamp((static_cast<float>(Candidates[Index].Z) - BaseZ) / Span, 0.f, 1.f);
		Spot.Exposure = Exposure.IsValidIndex(Index) ? Exposure[Index] : 0.f;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	int32 PerRarity[3] = { 0, 0, 0 };
	for (const FTNTctPadPick& Pick : TNTctItemRules::PlanPads(Spots, ItemPadCount, Avoid, ItemPadMinFromSpawn, ItemPadMinSpacing))
	{
		const FVector Where = Candidates[Pick.Index] + FVector(0.0, 0.0, 2.0);
		if (ATN_TctItemPad* Pad = World->SpawnActor<ATN_TctItemPad>(ATN_TctItemPad::StaticClass(), FTransform(Where), Params))
		{
			Pad->ServerSetRarity(Pick.Rarity);
			ItemPads.Add(Pad);
			++PerRarity[static_cast<int32>(Pick.Rarity)];
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] Puntos de objetos: %d repartidos por la arena (de %d sitios): %d épicos, %d raros, %d comunes."),
		ItemPads.Num(), Candidates.Num(), PerRarity[2], PerRarity[1], PerRarity[0]);
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

void ATN_TctGameMode::RescueFromWater(ATortugaCharacter* Turtle, float WaterZ) const
{
	UCharacterMovementComponent* Movement = Turtle ? Turtle->GetCharacterMovement() : nullptr;
	FVector Dry;
	if (!Movement || !Arena || !TNTctItemRules::NearestDryPoint(Arena->GetSpawnCandidates(), Turtle->GetActorLocation(), WaterZ, Dry))
	{
		return;
	}
	const FVector Target = Dry + FVector(0.0, 0.0, Turtle->GetSimpleCollisionHalfHeight() + 20.0);
	UTN_TurtleMovementComponent::LaunchFromServer(Turtle, TNTctItemRules::RescueLaunch(Turtle->GetActorLocation(), Target, Movement->GetGravityZ()));
	TNTctItems::PlayCue(Turtle, ETNRaceSound::Boing, 1.1f);
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] El flotador lanza a %s a tierra (%.0f, %.0f, %.0f)."), *GetNameSafe(Turtle), Dry.X, Dry.Y, Dry.Z);
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

bool ATN_TctGameMode::DelayFlood(float Seconds)
{
	ATN_TctGameState* State = GetTctState();
	if (!bRoundLive || !State || State->Flood.StartServerTime < 0.f)
	{
		return false;
	}
	const float Now = static_cast<float>(State->GetServerWorldTimeSeconds());
	State->Flood.StartServerTime = TNTctRules::DelayedFloodStart(State->Flood.StartServerTime, Seconds, Now);
	State->ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] El tapón de marea retrasa el agua %.0f s."), Seconds);
	return true;
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
