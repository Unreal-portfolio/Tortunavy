#include "World/TN_ChunkManager.h"
#include "World/TN_ChunkDecisions.h"
#include "World/TN_FinishLineVolume.h"
#include "Game/TN_SurvivalRules.h"
#include "Game/TN_SurvivalMapSelection.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "World/ProcMap/TN_ProcMapSurvival.h"
#include "Core/TN_Log.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

ATN_ChunkManager::ATN_ChunkManager()
{
	PrimaryActorTick.bCanEverTick = false;

	// El ChunkManager en sí no se replica — sólo existe en el servidor.
	// Los chunks que spawnea tienen SetReplicates(true) activado en SpawnAlignedChunk,
	// por lo que UE los envía automáticamente a todos los clientes.
	bReplicates = false;

	LevelMapSettings = TSoftObjectPtr<UTN_ProcMapSettings>(FSoftObjectPath(TEXT("/Game/ProcMap/DA_ProcMapSettings.DA_ProcMapSettings")));
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginPlay
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ChunkManager::BeginPlay()
{
	Super::BeginPlay();

	// El ChunkManager sólo funciona en el servidor.
	// En clientes no hace nada — los actores spawneados llegan por replicación.
	if (!HasAuthority())
	{
		return;
	}

	// Supervivencia: un mapa generado por nivel, sin chunks. ATN_SurvivalGameMode genera los siguientes.
	if (bLevelMode)
	{
		BuildLevel(1);
		return;
	}

	// Validar que hay al menos un pool configurado
	if (EasyChunkClasses.Num() == 0 && MediumChunkClasses.Num() == 0 && HardChunkClasses.Num() == 0)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[ChunkManager] No hay chunks configurados en ningún pool. "
			"Asigna BPs en EasyChunkClasses / MediumChunkClasses / HardChunkClasses."));
		return;
	}

	// Validar secuencia personalizada si está activa
	if (!bUseRandomGeneration && CustomChunkSequence.Num() < TotalChunksBeforeFinal)
	{
		UE_LOG(LogTortunabo, Warning,
			TEXT("[ChunkManager] CustomChunkSequence tiene %d entradas pero TotalChunksBeforeFinal=%d. "
			     "Los chunks restantes usarán Hard como fallback."),
			CustomChunkSequence.Num(), TotalChunksBeforeFinal);
	}

	// El punto de spawn del primer chunk = la posición del ChunkManager en el mapa.
	NextSpawnTransform = GetActorTransform();

	// Pre-llenar el buffer inicial: spawnear (KeepAhead + KeepBehind - 1) chunks
	// al inicio para que el jugador ya tenga camino desde el primer frame.
	const int32 InitialCount = FMath::Max(1, GetKeepAliveCount() - 1);
	for (int32 i = 0; i < InitialCount; ++i)
	{
		SpawnNextChunk();
	}

	UE_LOG(LogTortunabo, Log, TEXT("[ChunkManager] Inicializado. Buffer inicial: %d chunks."), InitialCount);
}

// ─────────────────────────────────────────────────────────────────────────────
// Dificultad actual
// ─────────────────────────────────────────────────────────────────────────────

ETNChunkDifficulty ATN_ChunkManager::GetCurrentDifficulty() const
{
	return TNChunkLogic::ComputeDifficultyFromProgress(
		PassedChunkCount, EasyToMediumThreshold, MediumToHardThreshold);
}

// ─────────────────────────────────────────────────────────────────────────────
// Selección aleatoria del pool
// ─────────────────────────────────────────────────────────────────────────────

TSubclassOf<AActor> ATN_ChunkManager::SelectRandomFromPool(
	const TArray<TSubclassOf<AActor>>& Pool,
	int32& OutSelectedIndex) const
{
	// Evitar repetir el mismo índice consecutivamente (RNG inyectado como functor).
	// El índice a evitar entra por OutSelectedIndex (in/out): el llamante solo lo
	// pasa cuando el pool es el mismo que la última vez; si no, INDEX_NONE.
	const int32 NewIndex = TNChunkLogic::SelectIndexAvoidingRepeat(
		Pool.Num(), OutSelectedIndex,
		[](int32 Min, int32 Max) { return FMath::RandRange(Min, Max); });

	if (NewIndex == INDEX_NONE)
	{
		return nullptr;
	}

	OutSelectedIndex = NewIndex;
	return Pool[NewIndex];
}

// ─────────────────────────────────────────────────────────────────────────────
// SpawnNextChunk
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ChunkManager::SpawnNextChunk()
{
	if (bFinalSpawned)
	{
		return;
	}

	// Determinar dificultad: modo aleatorio (umbrales) o secuencia personalizada
	ETNChunkDifficulty Difficulty;
	if (!bUseRandomGeneration)
	{
		Difficulty = TNChunkLogic::ResolveCustomSequenceDifficulty(CustomChunkSequence, PassedChunkCount);
		if (!CustomChunkSequence.IsValidIndex(PassedChunkCount))
		{
			// Fuera del rango de la secuencia → Hard como fallback
			UE_LOG(LogTortunabo, Warning,
				TEXT("[ChunkManager] SpawnNextChunk: PassedChunkCount=%d supera CustomChunkSequence.Num()=%d — usando Hard."),
				PassedChunkCount, CustomChunkSequence.Num());
		}
	}
	else
	{
		Difficulty = GetCurrentDifficulty();
	}

	AActor* SpawnedChunk = SpawnChunkOfDifficulty(Difficulty);
	if (!SpawnedChunk)
	{
		return;
	}

	// Desconectar el trigger anterior y conectar solo el del nuevo chunk.
	// Tener un único trigger activo garantiza que un solo SpawnNextChunk se llame
	// por chunk cruzado, independientemente de cuántos jugadores haya.
	if (ActiveEndTrigger.IsValid())
	{
		ActiveEndTrigger->OnComponentBeginOverlap.RemoveDynamic(this, &ATN_ChunkManager::OnChunkEndOverlap);
	}

	if (UBoxComponent* EndTrigger = FindBoxComponentByName(SpawnedChunk, TEXT("EndTrigger")))
	{
		EndTrigger->OnComponentBeginOverlap.AddDynamic(this, &ATN_ChunkManager::OnChunkEndOverlap);
		ActiveEndTrigger = EndTrigger;
	}
	else
	{
		ActiveEndTrigger = nullptr;
		UE_LOG(LogTortunabo, Warning, TEXT("[ChunkManager] Chunk '%s' no tiene BoxComponent 'EndTrigger'. "
			"El jugador no podrá avanzar."), *GetNameSafe(SpawnedChunk));
	}

	ActiveChunks.Add(SpawnedChunk);
	CleanupChunks();

	UE_LOG(LogTortunabo, Log, TEXT("[ChunkManager] Spawneado chunk %s (dificultad=%d, total pasados=%d)."),
		*GetNameSafe(SpawnedChunk), (int32)Difficulty, PassedChunkCount);
}

AActor* ATN_ChunkManager::SpawnChunkOfDifficulty(ETNChunkDifficulty Difficulty)
{
	// Intentar el pool primario, luego los otros como fallback
	const TNChunkLogic::FPoolFallbackOrder Order = TNChunkLogic::GetPoolFallbackOrder(Difficulty);

	auto PoolFor = [this](ETNChunkDifficulty InDifficulty) -> const TArray<TSubclassOf<AActor>>*
	{
		switch (InDifficulty)
		{
			case ETNChunkDifficulty::Easy:   return &EasyChunkClasses;
			case ETNChunkDifficulty::Medium: return &MediumChunkClasses;
			default:                         return &HardChunkClasses;
		}
	};

	const TArray<TSubclassOf<AActor>>* PrimaryPool   = PoolFor(Order.Primary);
	const TArray<TSubclassOf<AActor>>* Fallback1Pool = PoolFor(Order.Fallback1);
	const TArray<TSubclassOf<AActor>>* Fallback2Pool = PoolFor(Order.Fallback2);

	// LastSelectedIndex solo es comparable si viene del mismo pool: si el tier
	// cambió (o caemos a un fallback), no tiene sentido evitar ese índice en un array distinto.
	int32 SelectedIndex = (LastSelectedPoolDifficulty == Order.Primary) ? LastSelectedIndex : INDEX_NONE;
	TSubclassOf<AActor> ChunkClass = SelectRandomFromPool(*PrimaryPool, SelectedIndex);
	ETNChunkDifficulty UsedPoolDifficulty = Order.Primary;

	if (!ChunkClass && Fallback1Pool)
	{
		SelectedIndex = (LastSelectedPoolDifficulty == Order.Fallback1) ? LastSelectedIndex : INDEX_NONE;
		ChunkClass = SelectRandomFromPool(*Fallback1Pool, SelectedIndex);
		UsedPoolDifficulty = Order.Fallback1;
		UE_LOG(LogTortunabo, Warning, TEXT("[ChunkManager] Pool primario vacío para dificultad %d — usando fallback 1."),
			(int32)Difficulty);
	}
	if (!ChunkClass && Fallback2Pool)
	{
		SelectedIndex = (LastSelectedPoolDifficulty == Order.Fallback2) ? LastSelectedIndex : INDEX_NONE;
		ChunkClass = SelectRandomFromPool(*Fallback2Pool, SelectedIndex);
		UsedPoolDifficulty = Order.Fallback2;
		UE_LOG(LogTortunabo, Warning, TEXT("[ChunkManager] Fallback 1 vacío — usando fallback 2."));
	}

	if (!ChunkClass)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[ChunkManager] SpawnNextChunk: Todos los pools están vacíos."));
		return nullptr;
	}

	AActor* SpawnedChunk = SpawnAlignedChunk(ChunkClass, NextSpawnTransform);
	if (!SpawnedChunk)
	{
		return nullptr;
	}

	LastSelectedIndex = SelectedIndex;
	LastSelectedPoolDifficulty = UsedPoolDifficulty;

	// Actualizar NextSpawnTransform al OutSocket de este chunk
	if (USceneComponent* OutSocket = FindSceneComponentByName(SpawnedChunk, TEXT("OutSocket")))
	{
		NextSpawnTransform = OutSocket->GetComponentTransform();

		if (bDebugDrawSockets)
		{
			DrawDebugSphere(GetWorld(), NextSpawnTransform.GetLocation(), 30.f, 8,
				FColor::Green, false, 10.f);
		}
	}
	else
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[ChunkManager] Chunk '%s' no tiene SceneComponent 'OutSocket'. "
			"Los chunks siguientes se superpondrán."), *GetNameSafe(SpawnedChunk));
	}

	return SpawnedChunk;
}

// ─────────────────────────────────────────────────────────────────────────────
// SpawnFinalChunk
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ChunkManager::SpawnFinalChunk()
{
	if (bFinalSpawned)
	{
		return;
	}
	bFinalSpawned = true;

	if (!FinalChunkClass)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[ChunkManager] FinalChunkClass no asignado. "
			"La carrera no tendrá meta. Asigna un BP con ATN_FinishLineVolume en BP_TN_ChunkManager."));
		return;
	}

	AActor* FinalChunk = SpawnAlignedChunk(FinalChunkClass, NextSpawnTransform);
	if (!FinalChunk)
	{
		return;
	}

	ActiveChunks.Add(FinalChunk);
	// El chunk final no necesita EndTrigger — la FinishLineVolume dentro de él
	// ya se encarga de notificar el fin de carrera al RunGameMode.

	UE_LOG(LogTortunabo, Log, TEXT("[ChunkManager] Chunk FINAL spawneado: '%s'."), *GetNameSafe(FinalChunk));
}

// ─────────────────────────────────────────────────────────────────────────────
// GetOrComputeInSocketTransform — caché de InSocket por clase
// ─────────────────────────────────────────────────────────────────────────────

FTransform ATN_ChunkManager::GetOrComputeInSocketTransform(TSubclassOf<AActor> ChunkClass)
{
	// Buscar en caché
	if (FTransform* Cached = InSocketCache.Find(ChunkClass.Get()))
	{
		return *Cached;
	}

	// No está en caché → spawnar un temporal lejos del área de juego para leer InSocket.
	// Se spawnea en una posición remota para que los Child Actors no puedan
	// causar overlaps con jugadores en (0,0,0). Se destruye inmediatamente.
	FTransform InSocketTransform = FTransform::Identity;

	UWorld* World = GetWorld();
	if (!World)
	{
		InSocketCache.Add(ChunkClass.Get(), InSocketTransform);
		return InSocketTransform;
	}

	// Spawn far away from gameplay — prevents false overlaps with pawns at/near origin
	const FTransform TempSpawnTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, -50000.f));

	FActorSpawnParameters TempParams;
	TempParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TempParams.bNoFail = true;

	AActor* Temp = World->SpawnActor<AActor>(ChunkClass, TempSpawnTransform, TempParams);
	if (Temp)
	{
		if (USceneComponent* InSocket = FindSceneComponentByName(Temp, TEXT("InSocket")))
		{
			// InSocket's world transform at TempSpawnTransform minus the spawn location
			// gives us the relative offset (same as spawning at Identity).
			// Since we only care about the relative transform from actor root to InSocket,
			// compute it explicitly.
			InSocketTransform = InSocket->GetComponentTransform().GetRelativeTransform(Temp->GetActorTransform());
		}
		else
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[ChunkManager] Chunk '%s' no tiene 'InSocket'. "
				"Se usará la posición del actor como entrada."), *ChunkClass->GetName());
		}
		Temp->Destroy();
	}

	InSocketCache.Add(ChunkClass.Get(), InSocketTransform);
	return InSocketTransform;
}

// ─────────────────────────────────────────────────────────────────────────────
// SpawnAlignedChunk — núcleo de alineación InSocket→Target con replicación
// ─────────────────────────────────────────────────────────────────────────────

AActor* ATN_ChunkManager::SpawnAlignedChunk(TSubclassOf<AActor> ChunkClass, const FTransform& TargetTransform)
{
	UWorld* World = GetWorld();
	if (!World || !ChunkClass)
	{
		return nullptr;
	}

	// ── Paso 1: Calcular la posición final ANTES de spawnar ─────────────────
	// El enfoque anterior (spawn en Identity + teleport) rompía los Child Actors:
	// BeginPlay de los hijos (SeagullActor, ButtonInteractable, ItemSpawnZone,
	// DeathZone, etc.) corría en (0,0,0) y cacheaba posiciones erróneas.
	//
	// Solución: obtener el InSocket offset (cacheado por clase) y calcular la
	// posición final. Spawnar el chunk real directamente en esa posición.
	// BeginPlay de TODOS los actores y Child Actors corre en la posición correcta.

	const FTransform InSocketTransform = GetOrComputeInSocketTransform(ChunkClass);
	const FTransform FinalTransform = InSocketTransform.Inverse() * TargetTransform;

	if (bDebugDrawSockets)
	{
		DrawDebugSphere(World, TargetTransform.GetLocation(), 25.f, 8,
			FColor::Red, false, 10.f);
	}

	// ── Paso 2: Spawnar el chunk REAL en la posición final ───────────────────
	// BeginPlay de todos los componentes y Child Actors corre en la posición
	// world correcta → InitialLocation, Waypoints, SpawnZone, todo bien.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Chunk = World->SpawnActor<AActor>(ChunkClass, FinalTransform, Params);
	if (!Chunk)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[ChunkManager] SpawnActor falló para '%s'."),
			*ChunkClass->GetName());
		return nullptr;
	}

	// ── Paso 3: Activar replicación ──────────────────────────────────────────
	// La red envía el actor ya en la posición final al primer tick de red.
	Chunk->SetReplicates(true);
	Chunk->SetReplicateMovement(false); // chunks son estáticos

	// bAlwaysRelevant: los chunks deben permanecer relevantes incluso cuando el
	// cliente cambia de ViewTarget (muerte → spectator). Sin esto, al morir el
	// cliente su relevancy se recalculaba desde la posición del spectador; los
	// chunks lejos del spectador se descargaban y al respawnear las puertas,
	// zonas de spawn y triggers llegaban desincronizados. NO usamos DORM_Initial
	// porque los ChildActorComponents (puertas, botones) cambian de estado en
	// runtime y necesitan replicar esos cambios — DORM_Initial los congelaría.
	Chunk->bAlwaysRelevant = true;

	return Chunk;
}

// ─────────────────────────────────────────────────────────────────────────────
// CleanupChunks
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ChunkManager::CleanupChunks()
{
	const int32 MaxChunks = GetKeepAliveCount();

	// Limpiar entradas inválidas (chunks que ya fueron destruidos)
	ActiveChunks.RemoveAll([](const TWeakObjectPtr<AActor>& Ptr)
	{
		return !Ptr.IsValid();
	});

	// Destruir los más antiguos mientras se supere el máximo
	while (ActiveChunks.Num() > MaxChunks)
	{
		TWeakObjectPtr<AActor> OldestChunk = ActiveChunks[0];
		ActiveChunks.RemoveAt(0);

		if (OldestChunk.IsValid())
		{
			OldestChunk->Destroy();
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// OnChunkEndOverlap — el jugador cruzó el EndTrigger de un chunk
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ChunkManager::OnChunkEndOverlap(
	UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	// Solo en servidor
	if (!HasAuthority())
	{
		return;
	}

	// Solo contar Pawns (no proyectiles, objetos físicos, etc.)
	if (!OtherActor || !OtherActor->IsA<APawn>())
	{
		return;
	}

	// El chunk que contiene este EndTrigger
	// (no hace falta dedup: solo hay un trigger activo a la vez)
	AActor* OwnerChunk = OverlappedComp ? OverlappedComp->GetOwner() : nullptr;
	if (!OwnerChunk)
	{
		return;
	}

	// Desconectar inmediatamente para que ningún otro jugador vuelva a dispararlo.
	if (ActiveEndTrigger.IsValid())
	{
		ActiveEndTrigger->OnComponentBeginOverlap.RemoveDynamic(this, &ATN_ChunkManager::OnChunkEndOverlap);
		ActiveEndTrigger = nullptr;
	}

	// Avanzar el contador de progreso
	++PassedChunkCount;

	UE_LOG(LogTortunabo, Log, TEXT("[ChunkManager] Jugador '%s' cruzó EndTrigger del chunk '%s'. "
		"Chunks pasados: %d / %d."),
		*GetNameSafe(OtherActor), *GetNameSafe(OwnerChunk),
		PassedChunkCount, TotalChunksBeforeFinal);

	// ¿Ya hemos pasado suficientes chunks? → Spawn del chunk final
	if (TNChunkLogic::ShouldSpawnFinalChunk(PassedChunkCount, TotalChunksBeforeFinal))
	{
		SpawnFinalChunk();
	}
	else
	{
		SpawnNextChunk();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers de búsqueda de componentes
// ─────────────────────────────────────────────────────────────────────────────

USceneComponent* ATN_ChunkManager::FindSceneComponentByName(AActor* Actor, FName Name)
{
	if (!Actor)
	{
		return nullptr;
	}

	for (UActorComponent* Comp : Actor->GetComponents())
	{
		if (Comp && Comp->GetFName() == Name)
		{
			return Cast<USceneComponent>(Comp);
		}
	}

	// Fallback: comparación case-insensitive por si el BP tiene sufijo "_0" etc.
	const FString NameStr = Name.ToString();
	for (UActorComponent* Comp : Actor->GetComponents())
	{
		if (Comp)
		{
			const FString CompName = Comp->GetName();
			if (CompName.Equals(NameStr, ESearchCase::IgnoreCase))
			{
				return Cast<USceneComponent>(Comp);
			}
		}
	}

	return nullptr;
}

UBoxComponent* ATN_ChunkManager::FindBoxComponentByName(AActor* Actor, FName Name)
{
	USceneComponent* Found = FindSceneComponentByName(Actor, Name);
	return Found ? Cast<UBoxComponent>(Found) : nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetSafeReviveLocation
// ─────────────────────────────────────────────────────────────────────────────

FVector ATN_ChunkManager::GetSafeReviveLocation() const
{
	// NextSpawnTransform es el OutSocket del último chunk spawneado.
	// Siempre apunta a un área activa (el próximo chunk se spawneará aquí).
	// Elevamos 100 cm para evitar que el pawn aparezca dentro del suelo.
	return NextSpawnTransform.GetLocation() + FVector(0.f, 0.f, 100.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Modo por niveles (Supervivencia)
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_ChunkManager::BuildLevel(int32 Level)
{
	if (!HasAuthority())
	{
		return false;
	}

	ATN_ProcMapGenerator* Generator = EnsureLevelGenerator();
	if (!Generator)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[ChunkManager] No se pudo crear el generador del nivel %d."), Level);
		return false;
	}

	// Un mapa del catálogo de la dificultad del nivel que no haya salido en la partida (#518); el nivel 1 puede venir
	// fijado con ?SurvivalMap=. En el servidor la construcción es síncrona: al volver ya se sabe si hubo mapa.
	const FTNSurvivalMapPick Forced = Level <= 1 && FirstLevelMap != 0u
		? TNSurvivalMapSelection::PickForcedMap(FirstLevelMap) : FTNSurvivalMapPick();
	const FTNSurvivalMapPick Pick = Forced.IsValid() ? Forced : TNSurvivalMapSelection::PickLevelMap(LevelSeed, Level, PlayedLevelMaps);
	if (Pick.IsValid())
	{
		Generator->ServerGenerateSurvival(static_cast<int32>(Pick.Seed), Pick.Difficulty);
		if (Generator->IsMapReady())
		{
			PlayedLevelMaps = TNSurvivalMapSelection::RecordPlayed(PlayedLevelMaps, Pick);
			const TNSurvivalCatalog::FMapEntry* Entry = TNSurvivalCatalog::FindMap(Pick.Seed);
			UE_LOG(LogTortunabo, Log, TEXT("[ChunkManager] Nivel %d: mapa del catálogo «%s» (semilla %u, dificultad %d, camino de %.0f m)%s."),
				Level, Entry ? Entry->Name : TEXT("?"), Pick.Seed, Pick.Difficulty, Generator->GetMainPathLength() / 100.f,
				Pick.bForgotPlayed ? TEXT("; ya habían salido todos los de su dificultad: se olvidan los jugados") : TEXT(""));
			return true;
		}
		UE_LOG(LogTortunabo, Warning, TEXT("[ChunkManager] Nivel %d: el mapa del catálogo %u no se generó; se usa uno fuera del catálogo."),
			Level, Pick.Seed);
	}

	// Respaldo fuera del catálogo (sin trampas): semillas deterministas a partir de la de la partida.
	const int32 Difficulty = TNSurvivalLogic::LevelMapDifficulty(Level);
	const int32 BaseSeed = LevelSeed + FMath::Max(1, Level) - 1;
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		const int32 Seed = BaseSeed + Attempt * 100003;
		Generator->ServerGenerateSurvival(Seed, Difficulty);
		if (Generator->IsMapReady())
		{
			UE_LOG(LogTortunabo, Log, TEXT("[ChunkManager] Nivel %d: mapa de Supervivencia con semilla %d y dificultad %d (camino de %.0f m)."),
				Level, Seed, Difficulty, Generator->GetMainPathLength() / 100.f);
			return true;
		}
		UE_LOG(LogTortunabo, Warning, TEXT("[ChunkManager] Nivel %d: la semilla %d no dio mapa, probando otra."), Level, Seed);
	}
	UE_LOG(LogTortunabo, Error, TEXT("[ChunkManager] Nivel %d sin mapa."), Level);
	return false;
}

ATN_ProcMapGenerator* ATN_ChunkManager::EnsureLevelGenerator()
{
	if (LevelGenerator)
	{
		return LevelGenerator;
	}

	// El mapa avanza en +Y (salida al sur) y su ancho va en X: girado para que avance como los chunks (X del manager)
	// y centrado a lo ancho, con la salida donde empezaría el primer chunk.
	const TNProcMap::FGenParams Params = TNProcMap::MakeSurvivalParams(0u, TNProcMap::SurvivalMinDifficulty);
	const double Width = Params.GridSizeX * Params.ModuleSize;
	const FRotator Rotation(0.0, GetActorRotation().Yaw - 90.0, 0.0);
	const FVector Location = GetActorLocation() + FRotator(0.0, GetActorRotation().Yaw, 0.0).RotateVector(LevelMapOffset)
		- Rotation.RotateVector(FVector(Width * 0.5, 0.0, 0.0));

	UClass* Class = LevelGeneratorClass ? LevelGeneratorClass.Get() : ATN_ProcMapGenerator::StaticClass();
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	LevelGenerator = GetWorld()->SpawnActor<ATN_ProcMapGenerator>(Class, FTransform(Rotation, Location), SpawnParams);
	if (LevelGenerator)
	{
		LevelGenerator->SetSettingsIfMissing(LevelMapSettings.LoadSynchronous());
	}
	return LevelGenerator;
}

float ATN_ChunkManager::GetRemainingDistance(const FVector& Location) const
{
	if (!LevelGenerator || !LevelGenerator->IsMapReady())
	{
		return 0.f;
	}
	return FMath::Max(0.f, LevelGenerator->GetMainPathLength() - LevelGenerator->GetPathProgress(Location));
}
