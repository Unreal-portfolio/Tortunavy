#include "Core/TN_GameModeSpawnUtils.h"
#include "Core/TN_Log.h"
#include "Core/TN_CoopPlayerState.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace TNSpawnSpreadDetail
{
	/** Etiqueta de actor de los PlayerStart creados en ejecución (solo para reconocerlos en el editor y en el log). */
	const FName ExtraStartTag(TEXT("TNExtraStart"));

	/** Un PlayerStart del que salen los sitios nuevos, con su suelo y sus ejes horizontales. */
	struct FSpreadBase
	{
		const APlayerStart* Start = nullptr;
		/** Donde va el centro de la cápsula sobre su suelo. */
		FVector Spot = FVector::ZeroVector;
		float FloorZ = 0.f;
		float Above = 0.f;
		FVector Right = FVector::RightVector;
		FVector Back = FVector::BackwardVector;
	};

	/** Suelo bajo una columna: traza hacia abajo desde Up cm por encima de Column hasta Down cm por debajo; no cuenta si es una pared. */
	bool TraceFloor(const UWorld* World, const FVector& Column, float Up, float Down, const FCollisionQueryParams& Params, float& OutFloorZ)
	{
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Column + FVector(0.f, 0.f, Up), Column - FVector(0.f, 0.f, Down), ECC_Pawn, Params)
			&& !Hit.bStartPenetrating && Hit.ImpactNormal.Z >= 0.7f)
		{
			OutFloorZ = static_cast<float>(Hit.ImpactPoint.Z);
			return true;
		}
		return false;
	}

	/** Cápsula del peón que va a aparecer (la de su clase por defecto) o, sin clase, la de una tortuga normal. */
	void GetPawnCapsule(const TSubclassOf<APawn>& PawnClass, float& OutRadius, float& OutHalfHeight)
	{
		OutRadius = 34.f;
		OutHalfHeight = 88.f;
		const UClass* PawnUClass = PawnClass.Get();
		const ACharacter* Default = PawnUClass ? Cast<ACharacter>(PawnUClass->GetDefaultObject()) : nullptr;
		if (const UCapsuleComponent* Capsule = Default ? Default->GetCapsuleComponent() : nullptr)
		{
			OutRadius = FMath::Max(1.f, Capsule->GetScaledCapsuleRadius());
			OutHalfHeight = FMath::Max(OutRadius, Capsule->GetScaledCapsuleHalfHeight());
		}
	}
}

AActor* TN_PickUnoccupiedPlayerStart(UWorld* World, TArray<AActor*>& PlayerStarts, AController* Player)
{
	// Barajar para evitar siempre el mismo orden
	for (int32 i = PlayerStarts.Num() - 1; i > 0; --i)
	{
		const int32 j = FMath::RandRange(0, i);
		PlayerStarts.Swap(i, j);
	}

	// Primera pasada: buscar un PlayerStart sin ningún pawn cerca (< 200 cm)
	static constexpr float PlayerStartOccupiedRadiusCm = 200.f;
	for (AActor* Start : PlayerStarts)
	{
		bool bOccupied = false;
		for (TActorIterator<APawn> PawnIt(World); PawnIt; ++PawnIt)
		{
			const APawn* P = *PawnIt;
			if (P && P->Controller != Player &&
			    FVector::DistSquared(Start->GetActorLocation(), P->GetActorLocation()) < FMath::Square(PlayerStartOccupiedRadiusCm))
			{
				bOccupied = true;
				break;
			}
		}
		if (!bOccupied)
		{
			return Start;
		}
	}

	return nullptr;
}

AActor* TN_PickSpreadPlayerStart(UWorld* World, TArray<AActor*>& PlayerStarts, AController* Player, TSubclassOf<APawn> PawnClass, const TCHAR* LogTag)
{
	using namespace TNSpawnSpreadDetail;
	if (AActor* Free = TN_PickUnoccupiedPlayerStart(World, PlayerStarts, Player))
	{
		return Free;
	}
	if (!World || PlayerStarts.Num() == 0)
	{
		return nullptr;
	}

	float Radius = 34.f;
	float HalfHeight = 88.f;
	GetPawnCapsule(PawnClass, Radius, HalfHeight);
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Radius, HalfHeight);

	// Los peones que ocupan sitio (todos menos el del propio jugador); ninguno estorba en las comprobaciones de geometría.
	TArray<FVector> Occupants;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNSpreadPlayerStart), false);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
		if (It->Controller != Player)
		{
			Occupants.Add(It->GetActorLocation());
		}
	}

	// Los PlayerStart de los que salir, con el suelo que pisan: el centro de la cápsula queda a la misma altura sobre el suelo.
	TArray<FSpreadBase> Bases;
	for (AActor* Actor : PlayerStarts)
	{
		const APlayerStart* Start = Cast<APlayerStart>(Actor);
		float FloorZ = 0.f;
		if (!Start || !TraceFloor(World, Start->GetActorLocation(), 60.f, 400.f, Params, FloorZ))
		{
			continue;
		}
		FSpreadBase Base;
		Base.Start = Start;
		Base.FloorZ = FloorZ;
		Base.Above = FMath::Max(static_cast<float>(Start->GetActorLocation().Z) - FloorZ, HalfHeight + 2.f);
		Base.Spot = FVector(Start->GetActorLocation().X, Start->GetActorLocation().Y, FloorZ + Base.Above);
		Base.Right = Start->GetActorRightVector().GetSafeNormal2D();
		Base.Back = -Start->GetActorForwardVector().GetSafeNormal2D();
		if (!Base.Right.IsNearlyZero() && !Base.Back.IsNearlyZero())
		{
			Bases.Add(Base);
		}
	}

	// Desplazamientos posibles (pasos a la derecha, pasos hacia atrás), de menos a más lejos; a igual distancia, primero los
	// de los lados (siguen la fila), luego hacia atrás (una fila nueva) y al final hacia delante.
	TArray<FIntPoint> Offsets;
	for (int32 Right = -3; Right <= 3; ++Right)
	{
		for (int32 Back = -2; Back <= 2; ++Back)
		{
			if (Right != 0 || Back != 0)
			{
				Offsets.Add(FIntPoint(Right, Back));
			}
		}
	}
	auto OffsetKey = [](const FIntPoint& Offset)
	{
		return (Offset.X * Offset.X + Offset.Y * Offset.Y) * 16 + FMath::Abs(Offset.Y) * 2 + (Offset.Y < 0 ? 1 : 0);
	};
	Offsets.Sort([&OffsetKey](const FIntPoint& A, const FIntPoint& B)
	{
		const int32 KeyA = OffsetKey(A);
		const int32 KeyB = OffsetKey(B);
		return KeyA != KeyB ? KeyA < KeyB : A.X < B.X;
	});

	// Primero sitios holgados (los mismos 2 m de margen con los que un PlayerStart cuenta como ocupado, así se reutilizan);
	// si no caben, más apretados.
	struct FSpreadPass
	{
		float Step;
		float MinSeparation;
	};
	const FSpreadPass Passes[2] = { { 220.f, 200.f }, { FMath::Max(110.f, 2.f * Radius + 40.f), FMath::Max(90.f, 2.f * Radius + 20.f) } };

	for (const FSpreadPass& Pass : Passes)
	{
		const FSpreadBase* BestBase = nullptr;
		FVector BestSpot = FVector::ZeroVector;
		float BestClearance = -1.f;
		int32 GroupKey = -1;
		for (const FIntPoint& Offset : Offsets)
		{
			const int32 Key = OffsetKey(Offset);
			if (Key != GroupKey)
			{
				if (BestBase)
				{
					break;
				}
				GroupKey = Key;
			}
			for (const FSpreadBase& Base : Bases)
			{
				const FVector Column = Base.Spot + (Base.Right * Offset.X + Base.Back * Offset.Y) * Pass.Step;
				float FloorZ = 0.f;
				if (!TraceFloor(World, FVector(Column.X, Column.Y, Base.FloorZ), 150.f, 400.f, Params, FloorZ) || FMath::Abs(FloorZ - Base.FloorZ) > 45.f)
				{
					continue;
				}
				const FVector Spot(Column.X, Column.Y, FloorZ + Base.Above);
				float Clearance = 100000.f;
				for (const FVector& Occupant : Occupants)
				{
					Clearance = FMath::Min(Clearance, static_cast<float>(FVector::Dist(Spot, Occupant)));
				}
				if (Clearance < Pass.MinSeparation || Clearance <= BestClearance)
				{
					continue;
				}
				FHitResult Sweep;
				if (World->OverlapBlockingTestByChannel(Spot, FQuat::Identity, ECC_Pawn, Capsule, Params)
					|| World->SweepSingleByChannel(Sweep, Base.Spot, Spot, FQuat::Identity, ECC_Pawn, Capsule, Params))
				{
					continue;
				}
				BestBase = &Base;
				BestSpot = Spot;
				BestClearance = Clearance;
			}
		}
		if (!BestBase)
		{
			continue;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FRotator Facing(0.f, BestBase->Start->GetActorRotation().Yaw, 0.f);
		APlayerStart* Extra = World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), FTransform(Facing, BestSpot), SpawnParams);
		if (Extra)
		{
			Extra->PlayerStartTag = BestBase->Start->PlayerStartTag;
			Extra->Tags.AddUnique(ExtraStartTag);
			PlayerStarts.Add(Extra);
			UE_LOG(LogTortunabo, Log, TEXT("[%s] Todos los PlayerStart ocupados: sitio nuevo para %s en %s (a %.0f cm del ocupante más cercano, junto a %s)."),
				LogTag, *GetNameSafe(Player), *BestSpot.ToCompactString(), BestClearance, *GetNameSafe(BestBase->Start));
			return Extra;
		}
	}

	UE_LOG(LogTortunabo, Warning, TEXT("[%s] Todos los PlayerStart ocupados y sin hueco cerca de ellos para %s."), LogTag, *GetNameSafe(Player));
	return nullptr;
}

void TN_EnsurePlayerSpawned(AGameModeBase* GameMode, APlayerController* PlayerController, TFunctionRef<APlayerStart* ()> FallbackProvider, const TCHAR* LogTag)
{
	if (!GameMode || !GameMode->HasAuthority() || !PlayerController || PlayerController->GetPawn())
	{
		return;
	}

	GameMode->RestartPlayer(PlayerController);
	if (PlayerController->GetPawn())
	{
		return;
	}

	AActor* PlayerStart = GameMode->FindPlayerStart(PlayerController);
	if (!PlayerStart)
	{
		PlayerStart = FallbackProvider();
	}

	if (!PlayerStart)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[%s] Could not find or create a PlayerStart for %s"), LogTag, *GetNameSafe(PlayerController));
		return;
	}

	APawn* SpawnedPawn = GameMode->SpawnDefaultPawnFor(PlayerController, PlayerStart);
	if (!SpawnedPawn)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[%s] Failed to spawn default pawn for %s at %s"), LogTag, *GetNameSafe(PlayerController), *GetNameSafe(PlayerStart));
		return;
	}

	PlayerController->Possess(SpawnedPawn);
	GameMode->SetPlayerDefaults(SpawnedPawn);
	UE_LOG(LogTortunabo, Log, TEXT("[%s] Spawned and possessed pawn %s for %s"), LogTag, *GetNameSafe(SpawnedPawn), *GetNameSafe(PlayerController));
}

APlayerStart* TN_EnsureFallbackPlayerStart(UWorld* World, FName SpawnActorName, const TCHAR* LogTag, const TCHAR* MapDescriptor)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		if (APlayerStart* Existing = *It)
		{
			return Existing;
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Name = SpawnActorName;

	APlayerStart* Spawned = World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), FVector(0.f, 0.f, 150.f), FRotator::ZeroRotator, SpawnParams);
	if (Spawned)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[%s] No PlayerStart found in %s. Spawned fallback PlayerStart at world origin."), LogTag, MapDescriptor);
	}

	return Spawned;
}

bool TN_IsPlayerStateLeaving(const APlayerState* PlayerState)
{
	if (!IsValid(PlayerState) || PlayerState->IsActorBeingDestroyed() || PlayerState->IsInactive())
	{
		return true;
	}
	// Durante Logout el controlador ya está en destrucción (UWorld::DestroyActor lo marca antes de llamar a Destroyed).
	const AActor* Owner = PlayerState->GetOwner();
	return Owner && Owner->IsActorBeingDestroyed();
}

bool TN_IsBotPlayerState(const APlayerState* PlayerState)
{
	if (!PlayerState)
	{
		return false;
	}
	if (PlayerState->IsABot())
	{
		return true;
	}
	const AController* Owner = Cast<AController>(PlayerState->GetOwner());
	return Owner && !Owner->IsA<APlayerController>();
}

int32 TN_DropBotsFromSeamlessTravel(AGameStateBase* GameState, TArray<AActor*>& ActorList)
{
	TArray<APlayerState*> Bots;
	ActorList.RemoveAll([&Bots](AActor* Actor)
	{
		APlayerState* PlayerState = Cast<APlayerState>(Actor);
		if (!TN_IsBotPlayerState(PlayerState))
		{
			return false;
		}
		Bots.AddUnique(PlayerState);
		return true;
	});
	if (GameState)
	{
		for (APlayerState* PlayerState : GameState->PlayerArray)
		{
			if (TN_IsBotPlayerState(PlayerState))
			{
				Bots.AddUnique(PlayerState);
			}
		}
		// Si el GameState viaja (hacia el mapa de transición), no puede llevarse a quien se queda (#711).
		for (APlayerState* Bot : Bots)
		{
			GameState->RemovePlayerState(Bot);
		}
	}
	return Bots.Num();
}

int32 TN_RemoveStalePlayerStates(AGameStateBase* GameState)
{
	if (!GameState)
	{
		return 0;
	}
	return GameState->PlayerArray.RemoveAll([](const TObjectPtr<APlayerState>& PlayerState) { return !IsValid(PlayerState); });
}

int32 TN_CountConnectedCoopPlayers(const AGameStateBase* GameState)
{
	if (!GameState)
	{
		return 0;
	}
	int32 Count = 0;
	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		if (Cast<ATN_CoopPlayerState>(BasePS) && !TN_IsPlayerStateLeaving(BasePS) && !TN_IsBotPlayerState(BasePS))
		{
			++Count;
		}
	}
	return Count;
}

FTNCoopRoundCount TN_CountCoopRound(const AGameStateBase* GameState)
{
	FTNCoopRoundCount Count;
	if (!GameState)
	{
		return Count;
	}
	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		const ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS);
		if (!PS || TN_IsPlayerStateLeaving(PS))
		{
			continue;
		}
		++Count.Total;
		Count.Alive += PS->bIsAlive ? 1 : 0;
		Count.Resolved += PS->bHasFinishedRun ? 1 : 0;
	}
	return Count;
}

FTNLobbyReadyCount TN_CountLobbyReady(const AGameStateBase* GameState)
{
	FTNLobbyReadyCount Count;
	if (!GameState)
	{
		return Count;
	}
	for (APlayerState* BasePS : GameState->PlayerArray)
	{
		const ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS);
		if (!PS || TN_IsPlayerStateLeaving(PS) || TN_IsBotPlayerState(PS))
		{
			continue;
		}
		++Count.Connected;
		Count.Ready += PS->bIsInReadyZone ? 1 : 0;
	}
	return Count;
}

void TN_RestoreFullPlayerName(AGameModeBase* GameMode, APlayerController* PlayerController, const FString& Options)
{
	if (!GameMode || !PlayerController || !PlayerController->PlayerState)
	{
		return;
	}
	const FString FullName = UGameplayStatics::ParseOption(Options, TEXT("Name")).Left(TN_MaxPlayerNameLength);
	if (FullName.Len() > 20 && FullName != PlayerController->PlayerState->GetPlayerName())
	{
		GameMode->ChangeName(PlayerController, FullName, false);
	}
}
