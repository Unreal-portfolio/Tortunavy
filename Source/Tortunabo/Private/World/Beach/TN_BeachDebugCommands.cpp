#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachTypes.h"
#include "World/Beach/TN_BeachTrampoline.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_ShellComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"
#include "Core/TN_Log.h"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

/**
 * Consola de prueba de los elementos de la playa (cualquiera de ETNBeachElement: decorado, trampas o enemigos), en
 * cualquier mapa:
 *
 *   TN.Beach.Place <Elemento|número> [Tamaño=1] [Extent=0] [Semilla=al azar]
 *   TN.Beach.Place clear
 *
 * Lo crea el servidor (ATN_BeachElement::SpawnElement, replicado) delante de la tortuga local, apoyado en el suelo y
 * mirando hacia donde mira ella (su X local = la dirección de la carrera). Desde la ventana de un cliente del PIE se crea
 * en el mundo del servidor del mismo proceso; en un cliente remoto de verdad no hace nada (hay que escribirlo en el
 * anfitrión). «clear» borra todo lo creado así (etiqueta TNBeachDebug).
 *
 * TN.Beach.Spawn (TN_BeachEnemyDebug.cpp) hace lo mismo más simple: siempre a 30 m, con tamaño 1 y sin apoyarlo en el
 * suelo. Este se llama distinto para no registrar dos veces el mismo comando.
 */
namespace TNBeachDebugCommands
{
	const FName DebugTag(TEXT("TNBeachDebug"));

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

	bool ParseElement(const FString& Arg, ETNBeachElement& OutElement)
	{
		if (Arg.IsNumeric())
		{
			const int32 Value = FCString::Atoi(*Arg);
			if (Value >= 0 && Value < static_cast<int32>(ETNBeachElement::Count))
			{
				OutElement = static_cast<ETNBeachElement>(Value);
				return true;
			}
			return false;
		}
		const UEnum* Enum = StaticEnum<ETNBeachElement>();
		if (!Enum)
		{
			return false;
		}
		const int64 Value = Enum->GetValueByNameString(Arg);
		if (Value == INDEX_NONE || Value >= static_cast<int64>(ETNBeachElement::Count))
		{
			return false;
		}
		OutElement = static_cast<ETNBeachElement>(Value);
		return true;
	}

	/** Altura del suelo bajo XY (sin personajes), o Fallback si no hay nada debajo. */
	double GroundZ(UWorld* World, const FVector& At, double Fallback)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TNBeachDebugGround), false);
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		if (World->LineTraceSingleByObjectType(Hit, At + FVector(0.0, 0.0, 3000.0), At - FVector(0.0, 0.0, 6000.0), Objects, Params))
		{
			return Hit.ImpactPoint.Z;
		}
		return Fallback;
	}

	void RunSpawn(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* AuthWorld = FindAuthorityWorld(InWorld);
		if (!AuthWorld)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.Place: solo en el servidor (escríbelo en la ventana del anfitrión)."));
			return;
		}
		if (Args.Num() > 0 && Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
		{
			int32 Removed = 0;
			for (TActorIterator<ATN_BeachElement> It(AuthWorld); It; ++It)
			{
				if (It->ActorHasTag(DebugTag))
				{
					It->Destroy();
					++Removed;
				}
			}
			UE_LOG(LogTortunabo, Log, TEXT("[Playa] TN.Beach.Place clear: %d elementos borrados."), Removed);
			return;
		}
		ETNBeachElement Element = ETNBeachElement::Coconut;
		if (Args.Num() < 1 || !ParseElement(Args[0], Element))
		{
			UE_LOG(LogTortunabo, Display, TEXT("[Playa] Uso: TN.Beach.Place <Elemento|número> [Tamaño=1] [Extent=0] [Semilla] | TN.Beach.Place clear. Elementos: los de ETNBeachElement (Coconut ... Bunker; p. ej. Seaweed, DragCrab)."));
			return;
		}
		const float Size = Args.Num() > 1 ? FMath::Clamp(FCString::Atof(*Args[1]), 0.3f, 2.f) : 1.f;
		const float Extent = Args.Num() > 2 ? FMath::Max(0.f, FCString::Atof(*Args[2])) : 0.f;
		const int32 Seed = Args.Num() > 3 ? FCString::Atoi(*Args[3]) : FMath::Rand();

		// Delante de la tortuga local de la ventana donde se escribe (la misma posición en el mundo del servidor).
		const APlayerController* PC = InWorld->GetFirstPlayerController();
		const APawn* Viewer = PC ? PC->GetPawn() : nullptr;
		FVector From = FVector::ZeroVector;
		FRotator Facing = FRotator::ZeroRotator;
		if (Viewer)
		{
			From = Viewer->GetActorLocation();
			Facing = FRotator(0.0, Viewer->GetActorRotation().Yaw, 0.0);
		}
		else if (PC)
		{
			FVector ViewLoc;
			FRotator ViewRot;
			PC->GetPlayerViewPoint(ViewLoc, ViewRot);
			From = ViewLoc;
			Facing = FRotator(0.0, ViewRot.Yaw, 0.0);
		}
		const double Radius = TNBeach::FootprintRadius(Element) * Size;
		FVector At = From + Facing.Vector() * (Radius + 400.0);
		At.Z = GroundZ(AuthWorld, At, From.Z - 90.0);

		FTNBeachElementSpec ElementSpec;
		ElementSpec.Element = Element;
		ElementSpec.Seed = Seed;
		ElementSpec.SizeScale = Size;
		ElementSpec.Extent = Extent;
		ATN_BeachElement* Spawned = ATN_BeachElement::SpawnElement(AuthWorld, FTransform(Facing, At), ElementSpec);
		if (!Spawned)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.Place: no se ha podido crear %s (¿aún no existe su clase %s?)."),
				*UEnum::GetValueAsString(Element), TNBeach::ClassNameOf(Element));
			return;
		}
		Spawned->Tags.AddUnique(DebugTag);
		UE_LOG(LogTortunabo, Log, TEXT("[Playa] TN.Beach.Place: %s (%s) en %s, tamaño %.2f, extent %.0f, semilla %d."), *UEnum::GetValueAsString(Element),
			*Spawned->GetName(), *At.ToString(), Size, Extent, Seed);
	}

	static FAutoConsoleCommandWithWorldAndArgs SpawnCommand(
		TEXT("TN.Beach.Place"),
		TEXT("Crea un elemento de la playa delante de la tortuga local: TN.Beach.Place <Elemento|número> [Tamaño=1] [Extent=0] [Semilla]. «TN.Beach.Place clear» borra los creados así."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSpawn),
		ECVF_Cheat);

	// ── TN.Beach.Drop: caídas sobre el trampolín (#21) ───────────────────────

	/** Una tanda de caídas de TN.Beach.Drop, en el mundo con autoridad. */
	struct FDropRun
	{
		TArray<double> HeightsCm;
		int32 Times = 1;
		int32 Done = 0;
		int32 PlayerIndex = INDEX_NONE;
		/** La tortuga ya estaba en la vuelta anterior del temporizador (la primera caída espera una vuelta: que acabe de entrar). */
		bool bTargetSeen = false;
		FTimerHandle Timer;
	};

	/** La tortuga del jugador PlayerIndex del GameState (INDEX_NONE: la del anfitrión). */
	ACharacter* DropTarget(UWorld& World, int32 PlayerIndex)
	{
		if (PlayerIndex == INDEX_NONE)
		{
			const APlayerController* PC = World.GetFirstPlayerController();
			return PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
		}
		const AGameStateBase* GS = World.GetGameState();
		if (!GS || !GS->PlayerArray.IsValidIndex(PlayerIndex) || !GS->PlayerArray[PlayerIndex])
		{
			return nullptr;
		}
		return Cast<ACharacter>(GS->PlayerArray[PlayerIndex]->GetPawn());
	}

	/** El trampolín de TN.Beach.Place más cercano a la tortuga; si no hay ninguno, crea uno de gelatina delante de ella. */
	ATN_BeachTrampoline* DropTrampoline(UWorld& World, const ACharacter& Turtle)
	{
		ATN_BeachTrampoline* Best = nullptr;
		double BestDist = TNumericLimits<double>::Max();
		for (TActorIterator<ATN_BeachTrampoline> It(&World); It; ++It)
		{
			const double Dist = FVector::DistSquared(It->GetActorLocation(), Turtle.GetActorLocation());
			if (It->ActorHasTag(DebugTag) && Dist < BestDist)
			{
				Best = *It;
				BestDist = Dist;
			}
		}
		if (Best)
		{
			return Best;
		}
		const FRotator Facing(0.0, Turtle.GetActorRotation().Yaw, 0.0);
		FVector At = Turtle.GetActorLocation() + Facing.Vector() * (TNBeach::FootprintRadius(ETNBeachElement::Trampoline) + 400.0);
		At.Z = GroundZ(&World, At, Turtle.GetActorLocation().Z - 90.0);
		FTNBeachElementSpec ElementSpec;
		ElementSpec.Element = ETNBeachElement::Trampoline;
		// Semilla múltiplo de 4: la variante de gelatina (una cúpula, sin agujero en medio como el donut).
		ElementSpec.Seed = 4;
		ATN_BeachTrampoline* Spawned = Cast<ATN_BeachTrampoline>(ATN_BeachElement::SpawnElement(&World, FTransform(Facing, At), ElementSpec));
		if (Spawned)
		{
			Spawned->Tags.AddUnique(DebugTag);
		}
		return Spawned;
	}

	/** Lo más alto del trampolín (cuerpo o sensor) en una rejilla de líneas de arriba abajo sobre su caja: sirve para todas las variantes. */
	bool TrampolineTop(UWorld& World, const ATN_BeachTrampoline& Trampoline, const AActor* Ignore, FVector& OutTop)
	{
		FVector Origin;
		FVector Extent;
		Trampoline.GetActorBounds(true, Origin, Extent);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TNBeachDropTop), false, Ignore);
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		constexpr int32 Grid = 9;
		bool bFound = false;
		for (int32 i = 0; i < Grid; ++i)
		{
			for (int32 j = 0; j < Grid; ++j)
			{
				const FVector At = Origin + FVector(Extent.X * (2.0 * i / (Grid - 1) - 1.0), Extent.Y * (2.0 * j / (Grid - 1) - 1.0), 0.0) * 0.8;
				FHitResult Hit;
				if (World.LineTraceSingleByObjectType(Hit, At + FVector(0.0, 0.0, Extent.Z + 2000.0), At - FVector(0.0, 0.0, Extent.Z + 500.0), Objects, Params)
					&& Hit.GetActor() == &Trampoline && (!bFound || Hit.ImpactPoint.Z > OutTop.Z))
				{
					OutTop = Hit.ImpactPoint;
					bFound = true;
				}
			}
		}
		return bFound;
	}

	/** Un paso de la tanda: deja caer la tortuga desde la altura que toca sobre lo más alto del trampolín. */
	bool DropOnce(UWorld& World, FDropRun& Run)
	{
		ACharacter* Turtle = DropTarget(World, Run.PlayerIndex);
		const bool bWasSeen = Run.bTargetSeen;
		Run.bTargetSeen = Turtle != nullptr;
		ATN_BeachTrampoline* Trampoline = Turtle ? DropTrampoline(World, *Turtle) : nullptr;
		FVector Top;
		if (!Turtle || !bWasSeen || !Trampoline || !TrampolineTop(World, *Trampoline, Turtle, Top))
		{
			return false;
		}
		const double TopZ = Top.Z;
		const double Height = Run.HeightsCm[Run.Done % Run.HeightsCm.Num()];
		const FVector Where(Top.X, Top.Y, TopZ + Height + Turtle->GetSimpleCollisionHalfHeight());
		if (ATortugaCharacter* Tortuga = Cast<ATortugaCharacter>(Turtle))
		{
			if (Tortuga->IsKnockedDown())
			{
				Tortuga->RecoverFromKnockdown();
			}
			if (UTN_ShellComponent* Shell = Tortuga->GetShellComponent())
			{
				Shell->SetExitLocked(false);
				Shell->ForceExitShell();
			}
		}
		UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
		if (Move)
		{
			Move->StopMovementImmediately();
		}
		Turtle->SetActorLocation(Where, false, nullptr, ETeleportType::TeleportPhysics);
		if (Move)
		{
			Move->SetMovementMode(MOVE_Falling);
		}
		++Run.Done;
		UE_LOG(LogTortunabo, Log, TEXT("[Playa] TN.Beach.Drop %d/%d: %s cae desde %.1f m sobre %s (lo más alto a z=%.0f)."), Run.Done, Run.Times,
			*Turtle->GetName(), Height / 100.0, *Trampoline->GetName(), TopZ);
		return true;
	}

	void RunDrop(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* AuthWorld = FindAuthorityWorld(InWorld);
		if (!AuthWorld)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.Drop: solo en el servidor (escríbelo en la ventana del anfitrión)."));
			return;
		}
		TSharedRef<FDropRun> Run = MakeShared<FDropRun>();
		if (Args.Num() > 0)
		{
			TArray<FString> Parts;
			// Con «/» también: en -ExecCmds la coma separa comandos.
			const TCHAR* Separators[] = { TEXT(","), TEXT("/") };
			Args[0].ParseIntoArray(Parts, Separators, UE_ARRAY_COUNT(Separators));
			for (const FString& Part : Parts)
			{
				const double Meters = FCString::Atod(*Part);
				if (Meters > 0.0)
				{
					Run->HeightsCm.Add(FMath::Min(Meters, 60.0) * 100.0);
				}
			}
		}
		if (Run->HeightsCm.IsEmpty())
		{
			UE_LOG(LogTortunabo, Display, TEXT("[Playa] Uso: TN.Beach.Drop <metros>[/<metros>...] [veces=1] [cada=3 s] [jugador=anfitrión]."));
			return;
		}
		Run->Times = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 1, 200) : 1;
		const float Every = Args.Num() > 2 ? FMath::Clamp(FCString::Atof(*Args[2]), 0.5f, 30.f) : 3.f;
		Run->PlayerIndex = Args.Num() > 3 && Args[3].IsNumeric() ? FCString::Atoi(*Args[3]) : INDEX_NONE;
		TWeakObjectPtr<UWorld> WeakWorld(AuthWorld);
		// Cada Every segundos, una caída; si la tortuga aún no está (el cliente no ha entrado), espera a la siguiente.
		AuthWorld->GetTimerManager().SetTimer(Run->Timer, FTimerDelegate::CreateLambda([WeakWorld, Run]()
		{
			UWorld* World = WeakWorld.Get();
			if (!World)
			{
				return;
			}
			DropOnce(*World, *Run);
			if (Run->Done >= Run->Times)
			{
				World->GetTimerManager().ClearTimer(Run->Timer);
			}
		}), Every, true);
	}

	static FAutoConsoleCommandWithWorldAndArgs DropCommand(
		TEXT("TN.Beach.Drop"),
		TEXT("Deja caer una tortuga sobre el trampolín más cercano (lo crea si no hay): TN.Beach.Drop <metros>[/<metros>...] [veces=1] [cada=3 s] [jugador=anfitrión]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunDrop),
		ECVF_Cheat);
}
