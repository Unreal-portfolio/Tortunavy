#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/TN_MapPlacements.h"
#include "TN_MapPlacementSpawner.generated.h"

class ATN_BeachDecorField;
class UHierarchicalInstancedStaticMeshComponent;
class UPrimitiveComponent;
class UStaticMesh;

/** Recuento de lo que ha colocado un ATN_MapPlacementSpawner en esta máquina (log y tests). */
struct FTNMapPlacementStats
{
	/** Actores creados por clase nativa (sin el sufijo de un Blueprint: «TN_BeachBarbedWire», «TN_ScorePickup»...). */
	TMap<FName, int32> ActorsByClass;
	/** Entradas colocadas por tipo de pieza (TNMapPlacements::SpawnName). */
	TMap<FName, int32> PlacedBySpawn;
	int32 DecorItems = 0;
	int32 VegetationInstances = 0;
	/** Trazas de cota que han dado con el terreno y que no (se queda la cota del manifest). */
	int32 GroundHits = 0;
	int32 GroundMisses = 0;
	/** Saltadas porque hay algo puesto a mano en el nivel en su sitio. */
	int32 SkippedByLevel = 0;
	/** Sin pieza en el juego todavía (puzles pendientes, kinds desconocidos) o que no se han podido crear. */
	int32 Unsupported = 0;
	int32 Failed = 0;
	/** Piezas de un puzle que faltan en el juego (la puerta N4 de plate_balance). */
	int32 MissingPieces = 0;

	int32 Actors(const TCHAR* NativeClassName) const
	{
		const int32* Count = ActorsByClass.Find(FName(NativeClassName));
		return Count ? *Count : 0;
	}
};

/**
 * Coloca al cargar el mapa el bloque "placements" del manifest de una variante de terreno fijo (#652): lo crea
 * ATN_MapVariantLoader en BeginPlay en cada máquina, con el terreno ya construido para ajustar la cota con una traza.
 *
 * Red: lo que tiene estado (trampas, enemigos, lanzadores, puzles, botín) lo crea solo el servidor y se
 * replica; lo que cada clase quiere local (el decorado de ATN_BeachDecorField, la vegetación instanciada y el géiser) lo
 * monta cada máquina igual a partir del mismo manifest, sin red. La vegetación no se monta en un servidor dedicado.
 *
 * Lo puesto a mano en el nivel manda: una entrada con una pieza de juego del nivel (o un actor con la etiqueta
 * TN_Manual) a menos de su radio no se coloca. Ni se mueve ni se borra nada del nivel.
 */
UCLASS(NotPlaceable, Transient)
class TORTUNABO_API ATN_MapPlacementSpawner : public AActor
{
	GENERATED_BODY()

public:
	ATN_MapPlacementSpawner();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Componentes contra los que se ajusta la cota (los trozos del terreno). Sin ninguno, se usa la del manifest. */
	void SetGround(const TArray<UPrimitiveComponent*>& InGround);

	/**
	 * Coloca las entradas: bServer, lo replicado (servidor o partida sin red); bLocal, lo que cada máquina monta para sí
	 * (siempre, salvo en pruebas). Se llama una vez; una segunda llamada no hace nada.
	 */
	void Populate(const TNMapPlacements::FParseResult& Parsed, bool bServer, bool bLocal);

	const FTNMapPlacementStats& GetStats() const { return Stats; }

	/** Etiqueta con la que un actor del nivel reserva su sitio aunque no sea una pieza de juego. */
	static const FName ManualTag;

	/** Distancia (cm) por encima y por debajo de la cota del manifest en la que se busca el suelo. */
	static constexpr double TraceUp = 300.0;
	static constexpr double TraceDown = 1500.0;

protected:
	UPROPERTY(VisibleAnywhere, Category = "MapPlacements")
	TObjectPtr<USceneComponent> SceneRoot;

private:
	/** Cota del suelo bajo At (z del manifest si la traza no da con el terreno). */
	FVector Grounded(const FVector& At);

	/** Lo puesto a mano en el nivel antes de colocar: sitios que no se pisan. */
	void CollectLevelActors(bool bServer);
	bool IsTakenByLevel(const TNMapPlacements::FPlacement& P) const;

	bool SpawnOne(const TNMapPlacements::FPlacement& P, bool bServer, bool bLocal);
	AActor* SpawnBeachElement(ETNBeachElement Element, const FVector& At, double YawDeg, const FString& SeedKey,
		float SizeScale, double ExtentCm);
	AActor* SpawnClass(UClass* Class, const FVector& At, double YawDeg);
	void Track(AActor* Actor);

	// ── Puzles (TN_MapPlacementSpawner_Puzzles.cpp) ──
	bool SpawnThrowWall(const TNMapPlacements::FPlacement& P);
	bool SpawnPlateBalance(const TNMapPlacements::FPlacement& P);
	bool SpawnBreakableChain(const TNMapPlacements::FPlacement& P);
	bool SpawnElementRow(const TNMapPlacements::FPlacement& P, ETNBeachElement Element, int32 Count, double LiftCm);

	// ── Local: decorado, vegetación y géiser (TN_MapPlacementSpawner_Scenery.cpp) ──
	void QueueDecor(const TNMapPlacements::FPlacement& P);
	void QueueVegetation(const TNMapPlacements::FPlacement& P);
	bool SpawnGeyser(const TNMapPlacements::FPlacement& P);
	void BuildDecor();
	void BuildVegetation();

	FTNMapPlacementStats Stats;
	bool bPopulated = false;

	TArray<TWeakObjectPtr<UPrimitiveComponent>> Ground;
	/** Sitios (mundo) de lo puesto a mano en el nivel. */
	TArray<FVector> LevelSpots;
	TArray<TWeakObjectPtr<AActor>> SpawnedActors;

	/** Decorado y vegetación pendientes de montar (se montan juntos al final de Populate). */
	TArray<TNMapPlacements::FPlacement> PendingDecor;
	TArray<TNMapPlacements::FPlacement> PendingVegetation;

	UPROPERTY(Transient)
	TObjectPtr<ATN_BeachDecorField> DecorField;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> VegetationComps;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> VegetationMeshes;
};
