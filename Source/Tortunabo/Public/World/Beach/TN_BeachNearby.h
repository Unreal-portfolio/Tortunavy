#pragma once

#include "CoreMinimal.h"

class ACharacter;
class UWorld;

/**
 * Personajes cerca de una trampa de la playa, sin recorrer el mundo en cada una (#60).
 *
 * Las trampas (algas, minas, portones...) miraban cada fotograma a todos los personajes del mundo con un TActorIterator y,
 * para cada uno, las reglas caras (IsFreeTurtle recorre las listas de tortugas sujetas y comidas, InverseTransformPosition...).
 * Con 8 tortugas y unas 100 trampas eran ~1200 iteradores por fotograma y el coste crecía con cada tortuga. Ahora la lista
 * de personajes se hace una vez por fotograma y mundo, y cada trampa solo mira a los que tiene al alcance en planta: las
 * reglas de siempre, sobre los mismos personajes que podían cumplirlas.
 */
namespace TNBeachNearby
{
	/** Personajes del mundo en este fotograma (en el orden de TActorIterator); la lista se rehace una vez por fotograma y mundo. */
	TORTUNABO_API const TArray<TWeakObjectPtr<ACharacter>>& FrameCharacters(const UWorld* World);

	/** Los personajes válidos de FrameCharacters a menos de Radius (cm, en planta) de Center, en el mismo orden. */
	TORTUNABO_API void Gather(const UWorld* World, const FVector& Center, double Radius, TArray<ACharacter*>& Out);

	/** Lógica pura: Point está a menos de Radius (cm) de Center en planta (la altura la deciden las reglas de cada trampa). */
	inline bool IsWithin2D(const FVector& Point, const FVector& Center, double Radius)
	{
		return Radius >= 0.0 && FVector::DistSquared2D(Point, Center) <= Radius * Radius;
	}
}
