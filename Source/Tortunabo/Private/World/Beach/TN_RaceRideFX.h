#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcMapAmbientFX.h"

class ACharacter;
class UStaticMeshComponent;

/**
 * Lo que se ve en la tortuga con la tabla de surf y con el cohete de feria (#786), solo en máquinas con pantalla: la tabla
 * bajo los pies, la ola que empuja por detrás y su espuma; el cohete a la espalda con su llama, chispas y humo; y la
 * voltereta del final del cohete (la malla gira sobre el centro de la cápsula). Lo usa UTN_RaceItemComponent con su estado
 * replicado (todo sale de las horas del servidor), así que todas las máquinas ven lo mismo. Implementado en
 * TN_RaceRideFX.cpp.
 *
 * Las piezas son componentes de la tortuga (los mantiene vivos el actor); aquí solo hay punteros débiles.
 */
class FTNRaceRideFX
{
public:
	/** Lo que se ve este fotograma. */
	struct FState
	{
		bool bSurf = false;
		bool bRocket = false;
		/** Segundos de voltereta (< 0 si no hay). */
		float FlipAge = -1.f;
	};

	/** Un fotograma: crea, coloca, anima o esconde las piezas. */
	void Tick(ACharacter* Turtle, float DeltaTime, const FState& State);

	/** Quita todo (y devuelve la malla a su sitio si estaba dando la voltereta). */
	void Stop(ACharacter* Turtle);

	/** Queda algo por ver o por devolver (partículas vivas, la malla girada). */
	bool IsBusy() const;

private:
	void EnsurePieces(ACharacter* Turtle);
	void EnsureEmitters(ACharacter* Turtle);
	void TickSurf(ACharacter* Turtle, float HalfHeight, bool bOn);
	void TickRocket(ACharacter* Turtle, float HalfHeight, bool bOn);
	void TickFlip(ACharacter* Turtle, float FlipAge);
	void RestoreFlip(ACharacter* Turtle);

	TWeakObjectPtr<UStaticMeshComponent> Board;
	TWeakObjectPtr<UStaticMeshComponent> Wave;
	TWeakObjectPtr<UStaticMeshComponent> Rocket;
	TWeakObjectPtr<UStaticMeshComponent> Flame;

	TNAmbientFX::FEmitter Spray;
	TNAmbientFX::FEmitter Sparks;
	TNAmbientFX::FEmitter Smoke;
	bool bEmittersReady = false;
	bool bPiecesReady = false;
	bool bFlipApplied = false;

	/** Reloj propio de la animación (vaivén de la ola, parpadeo de la llama) y lo que va apareciendo la ola (0-1). */
	float Clock = 0.f;
	float WaveGrow = 0.f;
};
