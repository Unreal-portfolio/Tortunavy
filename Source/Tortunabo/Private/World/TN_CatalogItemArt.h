#pragma once

#include "CoreMinimal.h"
#include "World/TN_CatalogItemVisuals.h"

class UStaticMesh;
class UTexture2D;

/**
 * Arte de los objetos de siempre (DT_Items) dibujado en código: el icono de la mochila (pegatina de 128x128 con el pintor del
 * HUD) y, para los que traían una forma básica del motor, la malla de juguete (kit del parque de pruebas, color de vértice).
 * Todo se construye una vez por ejecución y queda en caché, fuera del recolector. Implementado en TN_CatalogItemArt.cpp.
 *
 * Las mallas están hechas al tamaño con que se ven (cm, entre 20 y 35 de lado), con el pivote en el centro y mirando a +X,
 * como las de TNRaceItemArt.
 */
namespace TNCatalogItemArt
{
	/** Cómo se ve un objeto en el suelo y en la mano (EquippedMesh, EquippedMeshScale y EquippedMeshRotation de la fila). */
	struct FHeldLook
	{
		UStaticMesh* Mesh = nullptr;
		FVector Scale = FVector::OneVector;
		FRotator Rotation = FRotator::ZeroRotator;
	};

	/** true si el aspecto tiene malla construida en código (los que traían una forma del motor). */
	bool HasCodeMesh(ETNCatalogLook Look);

	/** La malla de Look. false si no tiene o no se ha podido construir; sin bEvenHeadless, también en máquinas sin pantalla. */
	bool GetHeldLook(ETNCatalogLook Look, FHeldLook& OutLook, bool bEvenHeadless = false);

	/** El icono de Look (128x128, pegatina del HUD), o null; sin bEvenHeadless, también en máquinas sin pantalla. */
	UTexture2D* GetIcon(ETNCatalogLook Look, bool bEvenHeadless = false);
}
