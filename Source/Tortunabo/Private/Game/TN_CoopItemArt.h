#pragma once

#include "CoreMinimal.h"
#include "Game/TN_CoopItemRules.h"

class UStaticMesh;
class UTexture2D;

/**
 * Arte de los objetos del coop dibujado en código (sin assets), como el de los objetos de carrera: la malla con la que se ven
 * en el suelo y en la aleta, el icono de la mochila (con la cuenta pintada si es más de uno) y la malla del charco de pesca.
 * Todo se construye una vez por ejecución y queda en caché (fuera del recolector). Null en máquinas sin pantalla.
 *
 * Las mallas van al tamaño con que se ven (cm), con el pivote en el centro de la malla y mirando a +X (como las de carrera);
 * la del charco, con el pivote en el suelo y en el centro del agua.
 */
namespace TNCoopItemArt
{
	/** Cómo se ve un objeto en el suelo y en la mano de la tortuga. */
	struct FHeldLook
	{
		UStaticMesh* Mesh = nullptr;
		FVector Scale = FVector::OneVector;
		FRotator Rotation = FRotator::ZeroRotator;
	};

	/** La malla del objeto. false si no se ha podido construir (sin pantalla, objeto sin malla). */
	bool GetHeldLook(ETNCoopItem Kind, FHeldLook& OutLook);

	/** El icono de la mochila (128x128, pegatina del HUD) con la cuenta abajo a la derecha si pasa de uno. */
	UTexture2D* GetIcon(ETNCoopItem Kind, int32 Count);

	/** El charco de pesca: agua, borde de arena mojada, piedras y una caña clavada. */
	UStaticMesh* GetPoolMesh();
}
