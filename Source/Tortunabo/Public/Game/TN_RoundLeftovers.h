#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Objetos sueltos que dejan las jugadoras al acabar una ronda o un nivel (#71, #569): los pickups que se sueltan o salen
 * de una bola parada, las conchas trampa (armadas o la reciclada que dejan al gastarse) y las bolas en el aire. La
 * carrera de la playa, el mapa procedural (Coop, FFA y 2v2) y la Supervivencia los quitan con el mismo código antes de
 * preparar la ronda o el nivel siguiente.
 */
namespace TNRoundLeftovers
{
	/**
	 * Servidor: destruye los ATN_ThrowableItemActor, ATN_PickupInteractableBase y ATN_ConchPickup creados jugando (lo
	 * colocado en el nivel, AActor::IsNetStartupActor, se queda; lo que ya se destruye se deja en paz). Junta antes de
	 * destruir, sin tocar la lista mientras se recorre. Una bola en el aire destruida así no suelta su pickup.
	 * @return Cuántos objetos ha quitado (0 sin mundo o en un cliente).
	 */
	TORTUNABO_API int32 DestroyPlayerLeftovers(UWorld* World);
}
