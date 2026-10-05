#pragma once

#include "CoreMinimal.h"

class AActor;
class UAudioComponent;
class USynthComponent;

/**
 * Reparto de las voces del mezclador (#737).
 *
 * El motor da a cada máquina MaxChannels voces (32 en Windows) y, cuando hay más sonidos activos que voces, deja mudos los
 * de menor «prioridad × volumen». USynthComponent nace con bAlwaysPlay (prioridad máxima, sin mirar el volumen) y casi
 * todo el juego suena por síntesis: con más de 32 sintetizadores en marcha a la vez, todos empataban arriba, el motor
 * repartía las voces entre empates como le salía (la música, de las primeras en arrancar, se quedaba muda) y los sonidos
 * de archivo, con prioridad 1, ya no sonaban nunca.
 *
 * Cada sintetizador entra en uno de tres rangos:
 *  - Reserved: la música, la interfaz, el ambiente del jugador y lo que suena de su propia tortuga. bAlwaysPlay: nada les
 *    quita la voz. Son pocos a la vez (la música y unos pocos por jugador local).
 *  - World: el resto (enemigos, trampas, objetos, las otras tortugas). Prioridad 1 × volumen, como los sonidos de archivo:
 *    si faltan voces, calla antes lo lejano y flojo.
 *  - Background: fondos 3D (burbujas, enjambres, fuentes de ambiente). Prioridad 0,5: ceden antes que World.
 *
 * El rango se copia al componente de audio y vale desde el siguiente Start (el motor lo lee al empezar a sonar).
 */
namespace TNAudioVoices
{
	enum class ERank : uint8
	{
		Reserved,
		World,
		Background,
	};

	/** Prioridad (0-100) que el motor multiplica por el volumen. Reserved no la usa: va con bAlwaysPlay. */
	TORTUNABO_API float PriorityFor(ERank Rank);

	/** Solo Reserved lleva bAlwaysPlay. */
	TORTUNABO_API bool IsAlwaysPlay(ERank Rank);

	TORTUNABO_API const TCHAR* RankName(ERank Rank);

	/**
	 * Rango de un sonido que sale de InOwner: Reserved si es la tortuga (peón controlado por un jugador de esta máquina) o
	 * el mando de un jugador local; World en cualquier otro caso (otras tortugas, bots, actores del mapa).
	 */
	TORTUNABO_API ERank RankForOwner(const AActor* InOwner);

	/** Aplica el rango al sintetizador y, si ya lo tiene, a su componente de audio. */
	TORTUNABO_API void Apply(USynthComponent& Synth, ERank Rank);

	/** Aplica el rango a un componente de audio suelto (sonidos de archivo). Antes de Play. */
	TORTUNABO_API void Apply(UAudioComponent& Audio, ERank Rank);
}
