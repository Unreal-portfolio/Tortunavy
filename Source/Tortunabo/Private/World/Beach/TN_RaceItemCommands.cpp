#include "World/Beach/TN_RaceItems.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/Beach/TN_RaceItemActor.h"
#include "World/Beach/TN_RaceItemBox.h"
#include "World/Beach/TN_RaceItemComponent.h"
#include "World/Beach/TN_RaceWhirlpool.h"
#include "Core/TN_Log.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TortugaCharacter.h"

/**
 * Consola de prueba de los objetos de carrera (Docs/Comandos_Prueba.md, «Objetos de carrera»). Se escriben en la ventana
 * del anfitrión (o de un cliente del PIE: actúan en el mundo del servidor del mismo proceso). Los índices de jugador
 * empiezan en 0 (0 = el anfitrión):
 *
 *   TN.Race.Item <objeto|list> [jugador=0]          da el objeto a esa tortuga (en la mano; si está llena, sustituye)
 *   TN.Race.ItemUse <objeto> [jugador=0]            se lo da y lo usa en el acto
 *   TN.Race.ItemBox [n=1 | clear]                    n cajas de objetos delante de la tortuga local (o las quita)
 *   TN.Race.ItemRank [jugador=0]                     puesto en la carrera y pesos de cada objeto para ese puesto
 *   TN.Race.Boost [segundos=3] [multiplicador=2] [jugador=0]
 *   TN.Race.Star [segundos=8] [jugador=0]
 *   TN.Race.ItemClear                                quita lo lanzado (cangrejos, minas...) y los efectos de todas
 */
namespace TNRaceItemCmd
{
	const FName DebugTag(TEXT("TNRaceDebug"));

	/** El mundo con autoridad del mismo proceso (el propio si no es un cliente; en PIE, el del servidor del mismo mapa). */
	UWorld* FindAuthorityWorld(UWorld* InWorld)
	{
		if (!InWorld)
		{
			return nullptr;
		}
		if (InWorld->GetNetMode() != NM_Client)
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
		for (int32 Index = static_cast<int32>(ETNRaceItem::Box); Index < static_cast<int32>(ETNRaceItem::Count); ++Index)
		{
			const ETNRaceItem Kind = static_cast<ETNRaceItem>(Index);
			UE_LOG(LogTortunabo, Display, TEXT("[Carrera] %d  %s  (%s)"), Index, *TNRaceItems::CodeName(Kind), *TNRaceItems::DisplayName(Kind).ToString());
		}
	}

	void RunItem(const TArray<FString>& Args, UWorld* InWorld)
	{
		if (Args.Num() < 1 || Args[0].Equals(TEXT("list"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogTortunabo, Display, TEXT("[Carrera] Uso: TN.Race.Item <objeto|número> [jugador=0]. Objetos:"));
			ListItems();
			return;
		}
		UWorld* AuthWorld = FindAuthorityWorld(InWorld);
		ETNRaceItem Kind = ETNRaceItem::None;
		if (!AuthWorld || !TNRaceItems::ParseKind(Args[0], Kind) || Kind == ETNRaceItem::Box)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] TN.Race.Item: objeto desconocido o sin mundo con autoridad (escríbelo en el anfitrión). TN.Race.Item list los enseña."));
			return;
		}
		ATortugaCharacter* Turtle = TurtleOf(AuthWorld, IndexArg(Args, 1));
		if (!Turtle || !TNRaceItems::GiveItem(Turtle, Kind))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] TN.Race.Item: no hay tortuga con ese índice o no ha podido recibirlo."));
			return;
		}
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s recibe %s."), *GetNameSafe(Turtle), *TNRaceItems::CodeName(Kind));
	}

	void RunItemUse(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* AuthWorld = FindAuthorityWorld(InWorld);
		ETNRaceItem Kind = ETNRaceItem::None;
		if (Args.Num() < 1 || !AuthWorld || !TNRaceItems::ParseKind(Args[0], Kind) || Kind == ETNRaceItem::Box)
		{
			UE_LOG(LogTortunabo, Display, TEXT("[Carrera] Uso: TN.Race.ItemUse <objeto|número> [jugador=0] (lo da y lo usa)."));
			return;
		}
		ATortugaCharacter* Turtle = TurtleOf(AuthWorld, IndexArg(Args, 1));
		if (!Turtle || !TNRaceItems::GiveItem(Turtle, Kind))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] TN.Race.ItemUse: no hay tortuga con ese índice."));
			return;
		}
		UTN_InventoryComponent* Inventory = Turtle->GetInventoryComponent();
		if (Inventory && Inventory->HasEquippedItem() && TNRaceItems::KindOf(Inventory->GetEquippedItem()) != Kind)
		{
			// Con la aleta ocupada ha ido al caparazón: se saca a la aleta para usar ese y no lo que ya llevaba.
			Inventory->RotateItems();
		}
		if (Inventory && Inventory->HasEquippedItem() && TNRaceItems::KindOf(Inventory->GetEquippedItem()) == Kind)
		{
			TNRaceItems::ServerUse(Turtle, Inventory->GetEquippedItem());
		}
	}

	void RunItemBox(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* AuthWorld = FindAuthorityWorld(InWorld);
		if (!AuthWorld)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] TN.Race.ItemBox: solo en el servidor (escríbelo en el anfitrión)."));
			return;
		}
		if (Args.Num() > 0 && Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
		{
			int32 Removed = 0;
			for (TActorIterator<ATN_RaceItemBox> It(AuthWorld); It; ++It)
			{
				if (It->ActorHasTag(DebugTag))
				{
					It->Destroy();
					++Removed;
				}
			}
			UE_LOG(LogTortunabo, Log, TEXT("[Carrera] TN.Race.ItemBox clear: %d cajas quitadas."), Removed);
			return;
		}
		const int32 Count = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 12) : 1;
		// Delante de la tortuga local de la ventana donde se escribe (el mismo sitio en el mundo del servidor).
		const APlayerController* PC = InWorld ? InWorld->GetFirstPlayerController() : nullptr;
		const APawn* Viewer = PC ? PC->GetPawn() : nullptr;
		if (!Viewer)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] TN.Race.ItemBox: sin tortuga local."));
			return;
		}
		const ATN_BeachRaceGenerator* Generator = ATN_BeachRaceGenerator::Find(AuthWorld);
		const FRotator Facing(0.0, Viewer->GetActorRotation().Yaw, 0.0);
		const FVector Forward = Facing.Vector();
		const FVector Side = FVector(-Forward.Y, Forward.X, 0.0);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Offset = (static_cast<double>(Index) - 0.5 * static_cast<double>(Count - 1)) * 350.0;
			FVector At = Viewer->GetActorLocation() + Forward * 600.0 + Side * Offset;
			At.Z = Generator ? static_cast<double>(Generator->GetGroundHeightAt(At)) + 3.0 : Viewer->GetActorLocation().Z - 80.0;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (ATN_RaceItemBox* Box = AuthWorld->SpawnActor<ATN_RaceItemBox>(ATN_RaceItemBox::StaticClass(), At, Facing, Params))
			{
				Box->Tags.AddUnique(DebugTag);
			}
		}
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] TN.Race.ItemBox: %d cajas delante de ti (TN.Race.ItemBox clear las quita)."), Count);
	}

	void RunItemRank(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* AuthWorld = FindAuthorityWorld(InWorld);
		ATortugaCharacter* Turtle = AuthWorld ? TurtleOf(AuthWorld, IndexArg(Args, 0)) : nullptr;
		if (!Turtle)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] TN.Race.ItemRank: no hay tortuga con ese índice."));
			return;
		}
		const FTNRaceRank Rank = TNRaceItems::GetRank(Turtle);
		UE_LOG(LogTortunabo, Display, TEXT("[Carrera] %s va %d.ª de %d (norma %.2f)."), *GetNameSafe(Turtle), Rank.Place + 1, Rank.Count, Rank.Norm);
		for (int32 Index = static_cast<int32>(ETNRaceItem::Coconut); Index < static_cast<int32>(ETNRaceItem::Count); ++Index)
		{
			const ETNRaceItem Kind = static_cast<ETNRaceItem>(Index);
			UE_LOG(LogTortunabo, Display, TEXT("[Carrera]   %-16s rebuscar %.2f · caja %.2f · cofre %.2f · cofre de cima %.2f"), *TNRaceItems::CodeName(Kind),
				TNRaceItems::PositionWeight(Kind, Rank.Norm, Rank.Count, ETNRaceLootSource::Search),
				TNRaceItems::PositionWeight(Kind, Rank.Norm, Rank.Count, ETNRaceLootSource::Box),
				TNRaceItems::PositionWeight(Kind, Rank.Norm, Rank.Count, ETNRaceLootSource::Chest),
				TNRaceItems::PositionWeight(Kind, Rank.Norm, Rank.Count, ETNRaceLootSource::Summit));
		}
	}

	void RunBoost(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* AuthWorld = FindAuthorityWorld(InWorld);
		ATortugaCharacter* Turtle = AuthWorld ? TurtleOf(AuthWorld, IndexArg(Args, 2)) : nullptr;
		UTN_RaceItemComponent* Effects = Turtle ? UTN_RaceItemComponent::FindOrAddOn(Turtle) : nullptr;
		if (!Effects)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] TN.Race.Boost: no hay tortuga con ese índice."));
			return;
		}
		const float Seconds = Args.Num() > 0 ? FMath::Clamp(FCString::Atof(*Args[0]), 0.5f, 30.f) : TNRaceItems::TurboSeconds;
		const float Multiplier = Args.Num() > 1 ? FMath::Clamp(FCString::Atof(*Args[1]), 1.f, 4.f) : TNRaceItems::TurboMultiplier;
		Effects->GrantBoost(Multiplier, Seconds, false);
		Effects->MulticastCue(ETNRaceSound::Turbo, 1.f);
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s: turbo x%.2f durante %.1f s."), *GetNameSafe(Turtle), Multiplier, Seconds);
	}

	void RunStar(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* AuthWorld = FindAuthorityWorld(InWorld);
		ATortugaCharacter* Turtle = AuthWorld ? TurtleOf(AuthWorld, IndexArg(Args, 1)) : nullptr;
		UTN_RaceItemComponent* Effects = Turtle ? UTN_RaceItemComponent::FindOrAddOn(Turtle) : nullptr;
		if (!Effects)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Carrera] TN.Race.Star: no hay tortuga con ese índice."));
			return;
		}
		const float Seconds = Args.Num() > 0 ? FMath::Clamp(FCString::Atof(*Args[0]), 0.5f, 60.f) : TNRaceItems::StarSeconds;
		Effects->GrantStar(Seconds);
		Effects->MulticastCue(ETNRaceSound::StarUp, 1.f);
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s: protector solar durante %.1f s."), *GetNameSafe(Turtle), Seconds);
	}

	void RunClear(const TArray<FString>& /*Args*/, UWorld* InWorld)
	{
		UWorld* AuthWorld = FindAuthorityWorld(InWorld);
		if (!AuthWorld)
		{
			return;
		}
		int32 Removed = 0;
		for (TActorIterator<ATN_RaceItemActor> It(AuthWorld); It; ++It)
		{
			It->Destroy();
			++Removed;
		}
		int32 Cleared = 0;
		for (TActorIterator<ATortugaCharacter> It(AuthWorld); It; ++It)
		{
			if (UTN_RaceItemComponent* Effects = UTN_RaceItemComponent::FindOn(*It))
			{
				Effects->CancelEffects();
				++Cleared;
			}
		}
		// Los remolinos (#786) derivan de ATN_BeachEnemy, no de ATN_RaceItemActor: se acaban aparte.
		for (TActorIterator<ATN_RaceWhirlpool> It(AuthWorld); It; ++It)
		{
			It->ServerEnd();
			++Removed;
		}
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] TN.Race.ItemClear: %d actores de objetos quitados y efectos cancelados en %d tortugas."), Removed, Cleared);
	}

	static FAutoConsoleCommandWithWorldAndArgs CmdItem(TEXT("TN.Race.Item"),
		TEXT("Da un objeto de carrera a una tortuga: TN.Race.Item <objeto|número|list> [jugador=0]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunItem), ECVF_Cheat);
	static FAutoConsoleCommandWithWorldAndArgs CmdItemUse(TEXT("TN.Race.ItemUse"),
		TEXT("Da y usa un objeto de carrera en el acto: TN.Race.ItemUse <objeto|número> [jugador=0]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunItemUse), ECVF_Cheat);
	static FAutoConsoleCommandWithWorldAndArgs CmdItemBox(TEXT("TN.Race.ItemBox"),
		TEXT("Pone n cajas de objetos delante de ti (TN.Race.ItemBox [n=1]) o las quita (TN.Race.ItemBox clear)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunItemBox), ECVF_Cheat);
	static FAutoConsoleCommandWithWorldAndArgs CmdItemRank(TEXT("TN.Race.ItemRank"),
		TEXT("Puesto en la carrera de un jugador y pesos de cada objeto para ese puesto: TN.Race.ItemRank [jugador=0]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunItemRank), ECVF_Cheat);
	static FAutoConsoleCommandWithWorldAndArgs CmdBoost(TEXT("TN.Race.Boost"),
		TEXT("Turbo de carrera: TN.Race.Boost [segundos=3] [multiplicador=2] [jugador=0]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunBoost), ECVF_Cheat);
	static FAutoConsoleCommandWithWorldAndArgs CmdStar(TEXT("TN.Race.Star"),
		TEXT("Protector solar de carrera: TN.Race.Star [segundos=8] [jugador=0]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunStar), ECVF_Cheat);
	static FAutoConsoleCommandWithWorldAndArgs CmdClear(TEXT("TN.Race.ItemClear"),
		TEXT("Quita los actores de los objetos de carrera (cangrejos, minas, discos...) y cancela sus efectos en todas las tortugas."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunClear), ECVF_Cheat);
}
