#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_ChunkManager.generated.h"

class UBoxComponent;
class USceneComponent;
class ATN_ProcMapGenerator;
class UTN_ProcMapSettings;

/**
 * Niveles de dificultad de los chunks.
 */
UENUM(BlueprintType)
enum class ETNChunkDifficulty : uint8
{
	Easy   UMETA(DisplayName = "Easy"),
	Medium UMETA(DisplayName = "Medium"),
	Hard   UMETA(DisplayName = "Hard"),
};

/**
 * ATN_ChunkManager — Gestor de generación procedural de mapa por chunks.
 *
 * Colocar una instancia Blueprint de este actor en el mapa de carrera (LVL_Run).
 * Su posición en el mundo define dónde se spawnea el primer chunk.
 *
 * Cada chunk Blueprint debe tener tres componentes con nombres EXACTOS:
 *   - "InSocket"    → SceneComponent en el punto de entrada del chunk
 *   - "OutSocket"   → SceneComponent en el punto de salida del chunk
 *   - "EndTrigger"  → BoxComponent que detecta cuando un jugador cruza el chunk
 *
 * El sistema es completamente server-authoritative:
 *   - El ChunkManager solo existe en el servidor.
 *   - Los actores spawneados replican a todos los clientes automáticamente.
 *   - Todos los jugadores ven los mismos chunks al mismo tiempo.
 *
 * Dificultad progresiva:
 *   chunks 0 .. EasyToMediumThreshold-1         → pool EASY
 *   chunks EasyToMediumThreshold .. MediumToHardThreshold-1 → pool MEDIUM
 *   chunks MediumToHardThreshold+              → pool HARD
 *   Cuando PassedChunkCount >= TotalChunksBeforeFinal → spawn FinalChunkClass
 */
UCLASS()
class TORTUNABO_API ATN_ChunkManager : public AActor
{
	GENERATED_BODY()

public:
	ATN_ChunkManager();

	virtual void BeginPlay() override;

	// ── Pools de chunks por dificultad ───────────────────────────────────────

	/** Chunks fáciles (primeros de la carrera). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chunks|Easy")
	TArray<TSubclassOf<AActor>> EasyChunkClasses;

	/** Chunks de dificultad media. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chunks|Medium")
	TArray<TSubclassOf<AActor>> MediumChunkClasses;

	/** Chunks difíciles (final de la carrera). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chunks|Hard")
	TArray<TSubclassOf<AActor>> HardChunkClasses;

	// ── Modo de generación ───────────────────────────────────────────────────

	/**
	 * Si true, la dificultad de cada chunk se determina automáticamente según los
	 * umbrales EasyToMediumThreshold / MediumToHardThreshold (comportamiento original).
	 *
	 * Si false, se usa CustomChunkSequence: cada posición del array indica la dificultad
	 * del chunk correspondiente. Dentro de esa dificultad se elige un BP aleatorio del pool.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chunks|Difficulty")
	bool bUseRandomGeneration = true;

	/**
	 * Secuencia de dificultades personalizada, usada cuando bUseRandomGeneration = false.
	 * El índice 0 corresponde al primer chunk, el índice 1 al segundo, etc.
	 * Si el número de chunks pasados supera el tamaño del array, se usa Hard como fallback.
	 * Cada valor elige aleatoriamente entre los BPs del pool de esa dificultad.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chunks|Difficulty",
		meta = (EditCondition = "!bUseRandomGeneration", EditConditionHides))
	TArray<ETNChunkDifficulty> CustomChunkSequence;

	// ── Umbrales de dificultad (solo aplican con bUseRandomGeneration = true) ─

	/**
	 * Número de chunks pasados a partir del cual el pool cambia de EASY a MEDIUM.
	 * Ejemplo: 3 → los chunks 0, 1, 2 son EASY; el 3 en adelante es MEDIUM.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chunks|Difficulty",
		meta = (ClampMin = "1", EditCondition = "bUseRandomGeneration", EditConditionHides))
	int32 EasyToMediumThreshold = 3;

	/**
	 * Número de chunks pasados a partir del cual el pool cambia de MEDIUM a HARD.
	 * Debe ser mayor que EasyToMediumThreshold.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chunks|Difficulty",
		meta = (ClampMin = "2", EditCondition = "bUseRandomGeneration", EditConditionHides))
	int32 MediumToHardThreshold = 7;

	// ── Chunk final ──────────────────────────────────────────────────────────

	/**
	 * Chunk especial que se spawnea al final de la carrera.
	 * DEBE contener un ATN_FinishLineVolume colocado dentro del BP.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chunks|Final")
	TSubclassOf<AActor> FinalChunkClass;

	/**
	 * Número total de chunks aleatorios antes de spawnear el chunk final.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chunks|Final",
		meta = (ClampMin = "1"))
	int32 TotalChunksBeforeFinal = 10;

	// ── Buffer ───────────────────────────────────────────────────────────────

	/**
	 * Número de chunks a mantener activos por delante del jugador líder.
	 * Con KeepBehind=1, el buffer total = KeepAhead + KeepBehind.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chunks|Buffer",
		meta = (ClampMin = "1"))
	int32 KeepAhead = 5;

	/**
	 * Número de chunks a mantener activos por detrás (para evitar pop-in abrupto).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chunks|Buffer",
		meta = (ClampMin = "0"))
	int32 KeepBehind = 1;

	// ── Utilidad de debug ────────────────────────────────────────────────────

	/** Si true, dibuja las posiciones de InSocket/OutSocket de cada chunk spawneado. */
	UPROPERTY(EditDefaultsOnly, Category = "Chunks|Debug")
	bool bDebugDrawSockets = false;

	/**
	 * Devuelve la posición del OutSocket del último chunk spawneado + 100z,
	 * que es un área garantizada como activa (nunca destruida por CleanupChunks).
	 * Usado por TN_RunGameMode::RevivePlayer para teleportar pawns muertos
	 * a un área con chunks válidos antes de revivir.
	 * Si aún no hay chunks spawneados, retorna la posición inicial del ChunkManager + 100z
	 * (el punto donde se spawneará el primer chunk — válido como fallback).
	 */
	UFUNCTION(BlueprintPure, Category = "Chunks")
	FVector GetSafeReviveLocation() const;

	// ── Modo por niveles (Supervivencia) ─────────────────────────────────────

	/** Clase del generador del mapa de cada nivel (vacía = ATN_ProcMapGenerator). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chunks|Niveles")
	TSubclassOf<ATN_ProcMapGenerator> LevelGeneratorClass;

	/** Ajustes del mapa de cada nivel (biomas y materiales); por defecto los del mapa procedural. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chunks|Niveles")
	TSoftObjectPtr<UTN_ProcMapSettings> LevelMapSettings;

	/** Desplazamiento del mapa de cada nivel respecto al manager, en sus ejes (X adelante, Z arriba). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chunks|Niveles")
	FVector LevelMapOffset = FVector(0.f, 0.f, -300.f);

	/**
	 * Pasa al modo por niveles: en vez de chunks, cada nivel es un mapa de Supervivencia generado entero (#274).
	 * Lo llama ATN_SurvivalGameMode en StartPlay, antes del BeginPlay del manager; en BeginPlay se genera el nivel 1.
	 * @param InSeed      Semilla de la partida: decide qué mapa del catálogo juega cada nivel (#518).
	 * @param InFirstMap  Semilla de un mapa del catálogo para el nivel 1 (?SurvivalMap=); 0 = elegirlo.
	 * @param InStartDifficulty  Dificultad 1–5 del mapa del nivel 1; cada nivel sube una hasta 5 (#730).
	 */
	void SetLevelMode(bool bEnable, int32 InSeed, uint32 InFirstMap = 0u, int32 InStartDifficulty = 1)
	{
		bLevelMode = bEnable;
		LevelSeed = InSeed;
		FirstLevelMap = InFirstMap;
		LevelStartDifficulty = FMath::Clamp(InStartDifficulty, 1, 5);
		PlayedLevelMaps.Reset();
	}

	bool IsLevelMode() const { return bLevelMode; }

	/**
	 * Genera el mapa del nivel con ATN_ProcMapGenerator (se crea la primera vez, delante del manager): una entrada
	 * del catálogo de su dificultad que no haya salido en la partida (TNSurvivalMapSelection, #518), elegida con la
	 * semilla de la partida. Se replica la semilla del mapa y cada máquina construye el mismo. Server-only.
	 * @return true si hay mapa.
	 */
	bool BuildLevel(int32 Level);

	/** Generador de los niveles (nullptr fuera del modo por niveles). */
	ATN_ProcMapGenerator* GetLevelGenerator() const { return LevelGenerator; }

	/** Distancia (uu) que falta hasta la meta del nivel actual desde Location, a lo largo del camino del mapa. */
	float GetRemainingDistance(const FVector& Location) const;

private:

	/** true en Supervivencia: un mapa generado por nivel en vez de chunks. */
	bool bLevelMode = false;

	/** Semilla de la partida: decide el mapa de cada nivel. */
	int32 LevelSeed = 1;

	/** Mapa del catálogo pedido para el nivel 1 (?SurvivalMap=); 0 = elegirlo. */
	uint32 FirstLevelMap = 0u;

	/** Dificultad 1–5 del mapa del nivel 1 (la elegida con el general, #730); cada nivel sube una hasta 5. */
	int32 LevelStartDifficulty = 1;

	/** Mapas del catálogo que han salido en la partida, en orden (se olvidan al agotar los de una dificultad). */
	TArray<uint32> PlayedLevelMaps;

	UPROPERTY(Transient)
	TObjectPtr<ATN_ProcMapGenerator> LevelGenerator;

	/** Crea el generador de los niveles: la salida del mapa (su borde sur) donde empezarían los chunks. */
	ATN_ProcMapGenerator* EnsureLevelGenerator();

	/** Elige un chunk del pool de esa dificultad (con fallbacks), lo spawnea en NextSpawnTransform y avanza al OutSocket. */
	AActor* SpawnChunkOfDifficulty(ETNChunkDifficulty Difficulty);

	// Lista de chunks actualmente activos (FIFO: [0] = más viejo).
	TArray<TWeakObjectPtr<AActor>> ActiveChunks;

	// Transform donde se alineará el InSocket del siguiente chunk a spawnear.
	FTransform NextSpawnTransform;

	// Índice del último chunk seleccionado (para evitar repetición inmediata).
	int32 LastSelectedIndex = -1;

	// Dificultad del pool del que salió LastSelectedIndex. Sin esto, el anti-repeat
	// compararía índices de arrays distintos al cruzar de tier o caer a un fallback.
	ETNChunkDifficulty LastSelectedPoolDifficulty = ETNChunkDifficulty::Easy;

	// Cuántos chunks ha cruzado el jugador líder.
	int32 PassedChunkCount = 0;

	// Si ya se spawneó el chunk final.
	bool bFinalSpawned = false;

	// El único EndTrigger activo en cada momento.
	// Solo el trigger del chunk más adelantado está conectado → imposible doble-spawn.
	TWeakObjectPtr<UBoxComponent> ActiveEndTrigger;

	// ── Spawning ─────────────────────────────────────────────────────────────

	/** Spawnea el siguiente chunk aleatorio del pool de dificultad actual. */
	void SpawnNextChunk();

	/** Spawnea el chunk final de la carrera. */
	void SpawnFinalChunk();

	/**
	 * Spawnea un actor de chunk y lo alinea para que su InSocket quede en TargetTransform.
	 * Devuelve el actor spawneado, o nullptr si falla.
	 */
	AActor* SpawnAlignedChunk(TSubclassOf<AActor> ChunkClass, const FTransform& TargetTransform);

	/** Elimina los chunks más antiguos para mantener el buffer. */
	void CleanupChunks();

	/** Devuelve el número máximo de chunks activos permitidos. */
	int32 GetKeepAliveCount() const { return KeepAhead + KeepBehind; }

	/** Devuelve el pool de chunks correspondiente a la dificultad actual. */
	ETNChunkDifficulty GetCurrentDifficulty() const;

	/** Selecciona una clase de chunk aleatoria del pool dado, evitando el índice que
	 *  entra en OutSelectedIndex (in/out; INDEX_NONE = sin exclusión).
	 *  Devuelve nullptr si el pool está vacío. */
	TSubclassOf<AActor> SelectRandomFromPool(const TArray<TSubclassOf<AActor>>& Pool,
	                                          int32& OutSelectedIndex) const;

	// ── Componente helpers ───────────────────────────────────────────────────

	/** Busca un USceneComponent hijo por nombre exacto. */
	static USceneComponent* FindSceneComponentByName(AActor* Actor, FName Name);

	/** Busca un UBoxComponent hijo por nombre exacto. */
	static UBoxComponent* FindBoxComponentByName(AActor* Actor, FName Name);

	/**
	 * Caché de InSocket transform por clase de chunk.
	 * El offset de InSocket es fijo para cada BP — no cambia entre instancias.
	 * Calculamos una vez spawneando un temporal y reutilizamos para el resto.
	 */
	TMap<UClass*, FTransform> InSocketCache;

	/**
	 * Obtiene o calcula el InSocket relative transform para una clase de chunk.
	 * Si no está en caché, spawnea un temporal en Identity para leerlo.
	 */
	FTransform GetOrComputeInSocketTransform(TSubclassOf<AActor> ChunkClass);

	// ── Callbacks de overlap ─────────────────────────────────────────────────

	UFUNCTION()
	void OnChunkEndOverlap(UPrimitiveComponent* OverlappedComp,
	                       AActor* OtherActor,
	                       UPrimitiveComponent* OtherComp,
	                       int32 OtherBodyIndex,
	                       bool bFromSweep,
	                       const FHitResult& SweepResult);
};
