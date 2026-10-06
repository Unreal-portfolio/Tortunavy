#pragma once

#include "CoreMinimal.h"
#include "Game/TN_TctItemRules.h"

class UStaticMesh;

/**
 * Mallas en ejecución (color de vértice, cacheadas) de los objetos de Todos contra Todos que no tienen malla IA: la
 * cocobomba, el alga, el flotador y la medusa trampolín, y lo que dejan en el suelo (el charco y la medusa plantada). La
 * gaviota ladrona en la mano es la figurita de la gaviota de la carrera. null en máquinas sin pantalla.
 */
namespace TNTctItemMeshes
{
	/** La malla en la mano, en el suelo y como proyectil de Kind; null si Kind no tiene malla propia de aquí. */
	UStaticMesh* ForKind(ETNTctItem Kind);

	/** Coco con mecha (radio ~14 uu). */
	UStaticMesh* Coconut();

	/** Puñado de algas (radio ~14 uu). */
	UStaticMesh* AlgaClump();

	/** Charco de algas plano de radio 100 uu (se escala al radio del charco). */
	UStaticMesh* AlgaPuddle();

	/** Flotador rojo y blanco (radio exterior ~52 uu, en el plano XY). */
	UStaticMesh* FloatRing();

	/** Medusa trampolín: cúpula rosa de radio 100 uu y ~45 uu de alto, con los tentáculos recogidos. */
	UStaticMesh* JellyDome();
}
