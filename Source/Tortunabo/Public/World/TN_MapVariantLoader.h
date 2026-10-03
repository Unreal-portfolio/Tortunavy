#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_MapVariantLoader.generated.h"

class UMaterialInterface;
class UProceduralMeshComponent;
class FJsonObject;
class ATN_DeathZoneVolume;
class ATN_MapPlacementSpawner;

/**
 * Herramienta de disenadores: carga en el nivel abierto una de las variantes de mapa que genera
 * Scripts/terrain_volumes/Variants/<nombre>/ (manifest.json + Chunks/r{fila}c{col}.bin en
 * TNTM2, indexadas en Scripts/terrain_volumes/Variants/index.json). Construye un
 * UProceduralMeshComponent por trozo con TNTerrainMesh::ParseChunk + ToTileMesh, el mismo
 * camino que usa ATN_TerrainMeshTile, sin pasar por un UTN_TerrainMeshAsset ni por el editor.
 *
 * Vive en Source/ pero lee de Scripts/, que NO se empaqueta: esta clase es solo para el editor
 * y para PIE/standalone lanzados desde el editor durante el desarrollo. No usarla en build.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_MapVariantLoader : public AActor
{
	GENERATED_BODY()

public:
	ATN_MapVariantLoader();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Variante elegida: Scripts/terrain_volumes/Variants/<Variant>/manifest.json. */
	UPROPERTY(EditAnywhere, Category = "MapVariant", meta = (GetOptions = "GetVariantNames"))
	FName Variant;

	/** Material del terreno (color de vertice), el mismo que usa ATN_TerrainMeshTile. */
	UPROPERTY(EditAnywhere, Category = "MapVariant")
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	/**
	 * Al empezar la partida, coloca el bloque "placements" del manifest (puzles, enemigos, mecánicas, botín, nidos,
	 * decorado y vegetación de Scripts/place_terrain_path.py, #652) con ATN_MapPlacementSpawner. Lo puesto a mano en el
	 * nivel en su sitio manda: esa entrada no se coloca.
	 */
	UPROPERTY(EditAnywhere, Category = "MapVariant")
	bool bSpawnPlacements = true;

	/** Campo "description" del manifest de la variante cargada. Solo lectura. */
	UPROPERTY(VisibleAnywhere, Category = "MapVariant")
	FString VariantDescription;

	/** Alimenta el desplegable de Variant: lee Variants/index.json o, si falta, las carpetas
	 * con manifest.json bajo Variants/. */
	UFUNCTION(BlueprintPure, Category = "MapVariant")
	TArray<FString> GetVariantNames() const;

	/** Reconstruye la malla desde cero. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "MapVariant")
	void Recargar();

private:
	void LoadVariant();
	void ClearMeshes();
	void MoveStartPlayerStart(const TSharedPtr<FJsonObject>& Manifest) const;
	/** Manifest de la variante elegida, o nullptr si no se puede leer. */
	TSharedPtr<FJsonObject> ReadManifest() const;
	/** Pone un ATN_DeathZoneVolume por cada caja de "kill_boxes_uu" (fondo de los barrancos). */
	void SpawnKillZones();
	/** Coloca el bloque "placements" del manifest (ATN_MapPlacementSpawner, en cada máquina). */
	void SpawnPlacements();
	static FString VariantsDir();

	/** Un UProceduralMeshComponent por trozo del manifest ("cells"). Transitorios: no se guardan en
	 *  el nivel (pesaba 350 MB y, al abrirlo, se veia la malla de la ultima vez que se guardo, no la
	 *  del disco); se reconstruyen desde Scripts/terrain_volumes al cargar el nivel y en BeginPlay. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "MapVariant")
	TArray<TObjectPtr<UProceduralMeshComponent>> ChunkMeshes;

	/** Zonas de muerte creadas en BeginPlay; se destruyen en EndPlay. */
	TArray<TWeakObjectPtr<ATN_DeathZoneVolume>> SpawnedKillZones;

	/** Lo colocado del bloque "placements" en esta máquina; se destruye en EndPlay. */
	UPROPERTY(Transient)
	TObjectPtr<ATN_MapPlacementSpawner> PlacementSpawner;

	/** Variante con la que se construyeron ChunkMeshes, para no reconstruir en balde. */
	UPROPERTY(Transient)
	FName BuiltVariant;
};
