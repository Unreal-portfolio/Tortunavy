#pragma once

#include "CoreMinimal.h"

class AActor;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Arte de código en lugar de los marcadores de posición de los Blueprints (Docs/Inventario-Objetos-Arte-2026-09-29.md,
 * §2): una malla del motor (/Engine/: BasicShapes, EditorMeshes, VREditor) en un componente visible es un marcador, no
 * arte del juego. Los actores que lo detectan ocultan esos componentes y montan su malla de código (la medusa de la
 * playa, el huevo, la gaviota y el quad de la fauna, la tortuga en su peana). Si arte pone una malla del proyecto en el
 * Blueprint, se respeta y no se monta nada: así se sustituye sin tocar código.
 */
namespace TNPlaceholderArt
{
	/** Prefijo de los paquetes del motor: ninguna malla de ahí es arte del juego. */
	inline const TCHAR* EnginePrefix() { return TEXT("/Engine/"); }

	/** True si la malla es del motor (marcador). Null no es marcador: no se ve nada. */
	TORTUNABO_API bool IsPlaceholderMesh(const UStaticMesh* Mesh);

	/** True si el componente se ve en el juego y lleva un marcador. */
	TORTUNABO_API bool IsVisiblePlaceholder(const UStaticMeshComponent* Component);

	/** True si el componente es null, está vacío o lleva un marcador: se puede poner el arte de código. */
	TORTUNABO_API bool NeedsCodeArt(const UStaticMeshComponent* Component);

	/** Caja en el mundo que ocupan los marcadores visibles del actor (vacía si no tiene). */
	TORTUNABO_API FBox VisiblePlaceholderBounds(const AActor* Actor);

	/**
	 * Oculta los marcadores visibles del actor sin propagar a los hijos (los avisos cuelgan a veces de ellos). Con
	 * bDisableCollision, además les quita la colisión: un marcador escondido que bloquea es una pared invisible.
	 * Devuelve cuántos ha ocultado.
	 */
	TORTUNABO_API int32 HidePlaceholders(AActor* Actor, bool bDisableCollision);

	/** Marcadores que se siguen viendo en el actor (0 = sin contenido de editor a la vista). */
	TORTUNABO_API int32 CountVisiblePlaceholders(const AActor* Actor);

	/**
	 * Escala uniforme para que una malla con medidas ArtSize ocupe TargetSize en planta (X e Y) sin pasar de la altura
	 * TargetSize.Z. Con medidas nulas o negativas, 1.
	 */
	TORTUNABO_API float FitScale(const FVector& ArtSize, const FVector& TargetSize);

	/** M_CosmeticVertexColor (color de vértice; el alfa es el brillo) o, si faltara, el material por defecto del motor. */
	TORTUNABO_API UMaterialInterface* VertexColorMaterial();
}
