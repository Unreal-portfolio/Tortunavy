#pragma once

#include "CoreMinimal.h"

class AActor;
class UPrimitiveComponent;
class UWorld;

/**
 * Huella del mapa (#828): un hash de todo lo que BLOQUEA en el mundo (terreno, estructuras, rocas, decorado con colisión),
 * con las posiciones redondeadas, para comparar el anfitrión con cada cliente. Lo que ve el anfitrión tiene que ser
 * exactamente lo que ven todos: dos máquinas con el mismo mapa dan la misma huella, sea cual sea su calidad gráfica, sus FPS
 * o el orden en que cargaron las cosas.
 *
 * Qué cuenta: cada componente con colisión que bloquea a la tortuga o a un vehículo (canales Pawn, PhysicsBody y Vehicle),
 * con su forma: las instancias de un ISM, los vértices de una malla procedural y sus convexos, la malla y la transformada de
 * una malla estática y el tamaño de una caja, esfera o cápsula. No cuenta lo que se mueve por su cuenta (pawns, actores con
 * movimiento replicado o con física) ni lo que lleva un jugador. Va agrupado por clase de actor para ver qué difiere.
 *
 * Consola: TN.Map.Fingerprint [retraso_s] [all] (all = también lo que no tiene colisión, que puede depender de la calidad).
 */
namespace TNMapFingerprint
{
	struct FCategory
	{
		/** Clase del actor; « (rep)» si lo crea el servidor y replica (si no, lo genera cada máquina por su cuenta). */
		FString Name;
		bool bReplicated = false;
		/** Componentes, instancias y vértices contados. */
		int32 Pieces = 0;
		uint64 Hash = 0;
	};

	struct FResult
	{
		/** Todo junto, lo generado en cada máquina (actores sin replicar) y lo replicado. */
		uint64 Total = 0;
		uint64 Local = 0;
		uint64 Replicated = 0;
		int32 Pieces = 0;
		/** Por nombre. */
		TArray<FCategory> Categories;
	};

	struct FOptions
	{
		/** También lo que no tiene colisión que bloquee (visual): puede depender de la calidad gráfica. */
		bool bIncludeVisual = false;
		/** Redondeo de las posiciones (cm). */
		double Quantum = 1.0;
		/** Si se da, solo los actores que lo cumplen (los tests miden lo de un generador). */
		TFunction<bool(const AActor*)> Filter;
	};

	/** Huella del mundo. */
	TORTUNABO_API FResult Compute(const UWorld* World, const FOptions& Options = FOptions());

	/** Huella de los componentes de un actor (0 si no tiene nada que contar). */
	TORTUNABO_API uint64 HashActor(const AActor* Actor, const FOptions& Options, int32* OutPieces = nullptr);

	/** ¿Cuenta este actor? No: pawns, controladores, información de partida, lo que se mueve solo y lo que lleva un jugador. */
	TORTUNABO_API bool IsMapActor(const AActor* Actor);

	/** ¿Bloquea a la tortuga o a un vehículo? (canales Pawn, PhysicsBody o Vehicle; un disparador solo solapa). */
	TORTUNABO_API bool BlocksMovement(const UPrimitiveComponent* Prim);

	/** ¿Algún componente registrado del actor bloquea? */
	TORTUNABO_API bool ActorBlocksMovement(const AActor* Actor);

	/** Al registro, una línea con el total y una por clase. */
	TORTUNABO_API void LogResult(const FResult& Result, const FString& Context);

	TORTUNABO_API FString ToHex(uint64 Hash);
}
