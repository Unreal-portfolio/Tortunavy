#pragma once

#include "CoreMinimal.h"
#include "Game/TN_TctItemRules.h"

class UTexture2D;

/**
 * Iconos del HUD de los objetos de código de Todos contra Todos, dibujados en código como los de la carrera (pegatina con
 * borde crema, TNHUDArt), con un punto por carga que queda. Uno por objeto y carga, en caché. null en máquinas sin pantalla.
 */
namespace TNTctItemArt
{
	UTexture2D* GetIcon(ETNTctItem Kind, int32 Charges);
}
