#pragma once

#include "CoreMinimal.h"

class AActor;
class UObject;

/**
 * Golpes que en Supervivencia eliminan (#732, #734, #735): la mina, el erizo de mar, el cangrejo ermitaño, el mazazo del
 * cangrejo gigante y los objetos lanzados (la bola, el disco y la mina de arena). En los demás modos esas piezas derriban o
 * aturden como siempre: cada una llama a KillInSurvival y, si devuelve false, sigue con lo suyo.
 */
namespace TNSurvivalHits
{
	/**
	 * Servidor: true si la partida es de Supervivencia (ATN_SurvivalGameMode o el banco de pruebas, LVL_ProcMap con un mapa
	 * de Supervivencia). En los clientes, siempre false.
	 */
	TORTUNABO_API bool IsSurvival(const UObject* WorldContext);

	/**
	 * Servidor: en Supervivencia elimina a Victim (ATortugaCharacter::RequestKill con Killer, que da la causa del panel de
	 * eliminado) y devuelve true. Fuera de Supervivencia, o si Victim no es una tortuga viva, no hace nada y devuelve false.
	 */
	TORTUNABO_API bool KillInSurvival(AActor* Victim, AActor* Killer);
}
