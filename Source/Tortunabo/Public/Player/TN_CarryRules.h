#pragma once

#include "CoreMinimal.h"

/**
 * Reglas puras de coger y llevar a otra tortuga (UTN_CarryComponent) frente a lo que le pasa a la llevada. Sin mundo ni
 * actores; las usa el código de verdad y las prueba Tortunabo.Carry.
 */
namespace TNCarryRules
{
	/**
	 * Un derribo (ATortugaCharacter::ApplyKnockdown) a una tortuga que va en brazos de otra: el lanzable de un tercero, la
	 * piel de plátano, el DBNO. Decisión (#68): quien la lleva la suelta antes del derribo y la llevada cae derribada como en
	 * el suelo. El derribo es un ragdoll tumbado en el suelo, y sin soltarla empezaba con el movimiento apagado y enganchada
	 * encima del portador; al levantarse andaba (MOVE_Walking) pegada a él con CarriedBy puesto. Ignorar el golpe no vale: el
	 * DBNO también llega por aquí y tiene que quedar derribada.
	 *
	 * El aturdimiento de la carrera (TNBeach::StunTurtle) es otra cosa: es la bola del caparazón, que ya lleva puesta, y
	 * sigue en sus brazos.
	 */
	inline bool KnockdownDropsFromCarrier(bool bBeingCarried)
	{
		return bBeingCarried;
	}
}
