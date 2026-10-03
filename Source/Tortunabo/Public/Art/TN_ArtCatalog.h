#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TN_ArtCatalog.generated.h"

class UMaterialInterface;
class UStaticMesh;

/**
 * Sustituto de arte de una pieza generada desde C++ (Docs/Arte_Assets.md). Sin malla no sustituye nada: la pieza se sigue
 * generando como siempre y los demás campos se ignoran.
 */
USTRUCT(BlueprintType)
struct TORTUNABO_API FTNArtOverride
{
	GENERATED_BODY()

	/** Malla final de la pieza. Vacía: se dibuja la generada. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arte")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Materiales por ranura de la malla final (vacío o ranura vacía: los de la propia malla). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arte")
	TArray<TSoftObjectPtr<UMaterialInterface>> Materials;

	/**
	 * Desplazamiento, giro y escala de la malla final respecto al pivote de la pieza generada (cm y grados, ejes de la
	 * pieza). Sirve para encajar un pivote o una escala distintos sin volver a exportar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arte")
	FTransform Adjust;

	/**
	 * Falso (lo normal): la pieza conserva la colisión generada, invisible, y el juego no cambia. Cierto: choca la malla
	 * final con su propia colisión (la tiene que traer) y la generada se quita; cambia cómo se juega, pruébalo.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arte")
	bool bUseArtCollision = false;

	/**
	 * Solo piezas de la tortuga (Turtle.*): hueso o socket de su malla al que va pegada y cuya animación sigue. Vacío: el
	 * de la tabla (Docs/Arte_Assets.md, «La tortuga»). La pieza se coloca igual con cualquier hueso: se exporta en el
	 * espacio del cuerpo y el hueso solo dice a qué parte del cuerpo acompaña.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arte")
	FName Bone;

	/** Qué es la pieza, de qué fichero sale, su tamaño y su pivote. Solo informa: lo reescribe Scripts/arte/rellenar_catalogos.py. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arte", meta = (MultiLine = true))
	FString Info;

	bool HasMesh() const { return !Mesh.IsNull(); }
};

/**
 * Catálogo de arte de una zona (DA_Arte_Lobby, DA_Arte_ProcMap y DA_Arte_Tortuga en /Game/Art): el nombre de cada pieza
 * generada desde C++ («Lobby.Castle.Tower», «ProcMap.Rock.RoundBoulder»...) o pegada a la tortuga («Turtle.Shell») y, si Arte
 * la ha hecho, su malla final. La lista de
 * nombres la da TN.Art.Slots y Scripts/arte/rellenar_catalogos.py la mete entera, vacía. Los catálogos que se usan están en
 * Ajustes del proyecto > Tortunavy > Arte (UTN_ArtSettings).
 */
UCLASS(BlueprintType)
class TORTUNABO_API UTN_ArtCatalog : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Pieza → sustituto. Las piezas sin malla no cambian nada. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arte", meta = (ForceInlineRow))
	TMap<FName, FTNArtOverride> Pieces;

	/**
	 * Botón del catálogo: vuelve a leer los catálogos, avisa de las piezas con malla que no existen y rehace en el editor lo
	 * que se ve sin jugar (castillo, valle). Editar en el panel ya lo hace solo y cada Play vuelve a leerlos; sirve tras
	 * cambiar el catálogo con Python (catalog.apply_changes()) o recargarlo.
	 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Arte", meta = (DisplayName = "Aplicar cambios"))
	void ApplyChanges();

#if WITH_EDITOR
	/** Un cambio en el catálogo vacía la caché de sustitutos y rehace en el editor lo que se ve sin jugar (ApplyChanges). */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
