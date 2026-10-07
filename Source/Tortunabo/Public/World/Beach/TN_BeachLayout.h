#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachTypes.h"

/**
 * Pieza de decorado de la playa ya colocada, tal como la recibe ATN_BeachDecorField: la monta ATN_MapPlacementSpawner con
 * los sitios del mapa (TN_MapPlacementSpawner_Scenery.cpp). Lógica pura, sin mundo ni actores.
 *
 * Orientación (Yaw, grados sobre Z): con Yaw 0 el eje X local del elemento mira a +X. Los alargados (Extent > 0) tienen el
 * largo por su eje X local, centrado en su origen, y la huella es su semigrosor por el eje Y local.
 */
namespace TNBeachLayout
{
	/** Un elemento colocado: una cápsula en planta (un disco si HalfLength = 0). */
	struct FItem
	{
		ETNBeachElement Element = ETNBeachElement::Coconut;
		FVector2D Pos = FVector2D::ZeroVector;
		/** Grados; el eje X local del elemento (su largo, si es alargado). */
		double Yaw = 0.0;
		/** Huella (cm, ya por SizeScale): radio del disco o semigrosor de la cápsula. */
		double Radius = 0.0;
		/** Lo que ocupa de verdad (cm): la huella, salvo en los enemigos (su cuerpo y su sitio). */
		double Core = 0.0;
		/** Medio largo (Extent / 2) a lo largo de su eje X local; 0 = disco. */
		double HalfLength = 0.0;
		FTNBeachElementSpec Spec;

		FVector2D Axis() const
		{
			const double A = FMath::DegreesToRadians(Yaw);
			return FVector2D(FMath::Cos(A), FMath::Sin(A));
		}
	};
}
