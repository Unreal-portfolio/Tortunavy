#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"

class AActor;
class UPrimitiveComponent;
class UWorld;
struct FHitResult;

/**
 * Superficie que pisa (o sobre la que se arrastra) la tortuga, como pesos de cinco materiales: arena, tierra (con hierba,
 * hojarasca y fango), roca, madera y agua poco profunda. La comparten los pasos y el arrastre sintetizados
 * (UTN_TurtleFoleyComponent), el rozamiento del arrastre del panzazo (UTN_TurtleMovementComponent) y su polvo
 * (UTN_TurtleDustComponent).
 *
 * Por palabras en los nombres del componente, la clase y el nombre del actor, la malla y el material (agua, arena,
 * madera, tierra y roca, por ese orden); sin palabras conocidas, roca. Encima de otra tortuga, su caparazón cuenta como
 * madera.
 *
 * Funciones puras del hilo de juego: sin estado propio (las cachés las pasa quien llama).
 */
namespace TNTurtleSurface
{
	/** Índices de los pesos (los mismos que TNTurtleFoley::Surface del motor de sonido). */
	constexpr int32 Sand = 0;
	constexpr int32 Soil = 1;
	constexpr int32 Rock = 2;
	constexpr int32 Wood = 3;
	constexpr int32 Water = 4;
	constexpr int32 Num = 5;

	/** Preajuste de superficie sin palabras conocidas en los nombres (cuenta como roca: el «pat» neutro). */
	constexpr uint8 PresetUnknown = 255;

	/** Superficie por nombres de cada componente ya visto (índice de preajuste). */
	using FNameCache = TMap<TObjectKey<UPrimitiveComponent>, uint8>;

	/** Pesos de un preajuste: uno a 1 y el resto a 0 (el desconocido, roca). */
	TORTUNABO_API void PresetWeights(uint8 Preset, float OutWeights[Num]);

	/** Superficie por los nombres del componente, su malla, su material y su actor (con la caché, si se pasa). */
	TORTUNABO_API uint8 ClassifyByNames(const UPrimitiveComponent* Comp, FNameCache* Cache);

	/**
	 * Pesos normalizados de la superficie en un contacto.
	 * @param Hit          Impacto de una traza hacia abajo o suelo del movimiento; null si no se ha tocado nada.
	 * @param Cache        Caché de superficie por nombres (o null).
	 */
	TORTUNABO_API void Resolve(const FHitResult* Hit, FNameCache* Cache, float OutWeights[Num]);

	/**
	 * Traza de línea compleja (con el índice de cara) desde From hasta 60 cm por debajo de FootLocation y Resolve con lo
	 * que toque. Devuelve si ha tocado algo.
	 */
	TORTUNABO_API bool Probe(UWorld* World, const AActor* Ignore, const FVector& From, const FVector& FootLocation,
		FNameCache* Cache, float OutWeights[Num]);

	/** Índice del peso más alto. */
	inline int32 Dominant(const float Weights[Num])
	{
		int32 Best = 0;
		for (int32 s = 1; s < Num; ++s)
		{
			if (Weights[s] > Weights[Best]) { Best = s; }
		}
		return Best;
	}
}
