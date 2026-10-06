#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_RaceItems.h"

class UStaticMesh;
class UTexture2D;

namespace TNProcMesh
{
	struct FTNProcMeshBuffers;
}

/**
 * Arte en código de los objetos de carrera de la issue #786 (tabla de surf, caña de pescar, remolino y cohete de feria) y de
 * las piezas que se ven al usarlos (la tabla bajo los pies, la ola, el cohete y su llama, el agua del remolino, el sedal).
 * Mismo estilo que TN_RaceItemArt.cpp (kit de juguete del parque y pegatinas del HUD); TNRaceItemArt lo llama para estos
 * objetos (malla de la mano e icono), así que el resto del juego no ve la diferencia. Implementado en TN_RaceItemArtExtra.cpp.
 */
namespace TNRaceItemArtExtra
{
	/** Dibuja en B la malla de la mano de Kind (20-40 cm, centrada y mirando a +X). false si no es uno de estos objetos. */
	bool BuildItemMesh(ETNRaceItem Kind, TNProcMesh::FTNProcMeshBuffers& B);

	/** Icono de la mochila de Kind (128x128, pegatina); null si no es uno de estos objetos. */
	UTexture2D* PaintIcon(ETNRaceItem Kind);

	/** Piezas que se ven al usar los objetos (medidas en cm, pivote indicado en cada una). */
	enum class ERidePiece : uint8
	{
		/** Tabla de surf de 170 x 52 cm, pivote en el centro de la cara de abajo, punta hacia +X. */
		SurfBoard,
		/** Cresta de la ola que empuja: 3 m de ancho (Y), 1,6 m de alto, rompe hacia +X; pivote en el centro de la base. */
		Wave,
		/** Cohete de feria a la espalda: 95 cm, punta hacia +X; pivote en el centro de la tobera (detrás). */
		Rocket,
		/** Llama de la tobera: cono de 70 cm hacia -X desde el pivote. */
		Flame,
		/** Agua del remolino: disco de 3 m de radio con brazos en espiral; pivote en el centro, mirando arriba. */
		WhirlWater,
		/** Anillo de espuma del borde del remolino (radio 3 m). */
		WhirlFoam,
		/** Sedal: cilindro fino de 1 cm de largo hacia +X (se estira con la escala X). */
		FishLine,
		/** Anzuelo con su corcho rojo y blanco (~18 cm); pivote en el anzuelo. */
		Hook,
		/** La caña en la aleta mientras pesca: 120 cm hacia +X desde el mango (pivote). */
		Rod
	};

	/** La malla de la pieza (construida una vez; null en servidor dedicado o sin motor gráfico). */
	UStaticMesh* GetRidePiece(ERidePiece Piece);
}
