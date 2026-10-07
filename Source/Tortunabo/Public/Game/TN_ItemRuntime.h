#pragma once

#include "CoreMinimal.h"
#include "Core/TN_InventoryTypes.h"
#include "World/Beach/TN_RaceItemSynth.h"

class AActor;
class ACharacter;
class ATortugaCharacter;
class UWorld;

/**
 * Lo que comparten en partida los objetos que se usan (los de DT_Items y los del cooperativo, TN_CoopItems.h) y lo que los
 * mira (enemigos, guantazo, tormenta): si la tortuga puede usar un objeto o recibir un golpe, el reloj del servidor, la
 * malla y el icono de cada máquina, y los sonidos y el cable del arpón, que se ven en todas las máquinas a través de
 * UTN_CoopItemComponent.
 *
 * También el turbo en la predicción del movimiento (UTN_TurtleMovementComponent): cuánto acepta el servidor de un
 * movimiento del dueño marcado con turbo.
 */
namespace TNItemRuntime
{
	// ── Turbo en la predicción del movimiento (FTNSavedMove_Turtle, issue #22) ─────────────────────────────────────────

	/**
	 * Margen (s) en que el servidor sigue aceptando movimientos del dueño marcados con turbo cuando aquí el efecto ya se ha
	 * acabado: el dueño lo ve acabar más tarde (su reloj del servidor va medio ping por detrás) y sus movimientos tardan otro
	 * medio en llegar. Un ping de ida y vuelta más un cuarto de segundo, entre 0,3 y 1 s.
	 */
	inline float BoostGraceSeconds(float RoundTripSeconds)
	{
		return FMath::Clamp(FMath::Max(0.f, RoundTripSeconds) + 0.25f, 0.3f, 1.f);
	}

	/**
	 * Servidor: multiplicador con el que simula un movimiento del dueño marcado con turbo. Current, el de ahora; Recent, el
	 * último mayor que 1 que tuvo, hace SecondsSinceRecent (negativo si nunca). Sin turbo que lo justifique (nunca lo tuvo o
	 * pasó el margen), 1: se simula sin turbo y el dueño recibe la corrección.
	 */
	inline float ResolveClaimedBoost(float Current, float Recent, double SecondsSinceRecent, float GraceSeconds)
	{
		if (Current > 1.f)
		{
			return Current;
		}
		if (Recent > 1.f && SecondsSinceRecent >= 0.0 && SecondsSinceRecent <= static_cast<double>(GraceSeconds))
		{
			return Recent;
		}
		return 1.f;
	}

	// ── Estado de la tortuga ───────────────────────────────────────────────────────────────────────────────────────────

	/** Reloj del servidor en esta máquina (s): el replicado del GameState o, sin él, el del mundo. */
	TORTUNABO_API double ServerNow(const UWorld* World);

	/** true si nada la puede aturdir ni derribar ahora: protegida por el pez globo (UTN_CoopItemComponent). Cualquier máquina. */
	TORTUNABO_API bool IsInvulnerable(const AActor* Turtle);

	/** Servidor: se la puede aturdir o derribar (válida y sin protección). */
	TORTUNABO_API bool CanBeHurt(const ATortugaCharacter* Turtle);

	/**
	 * true si la tortuga puede usar un objeto ahora: viva, de pie, fuera del caparazón, sin aturdir, sin que la sujete un
	 * enemigo, sin que la recoloquen y sin llevar ni ir en brazos.
	 */
	TORTUNABO_API bool CanUseNow(const ATortugaCharacter* Turtle);

	/** true si un golpe de un objeto puede mover ahora a Turtle (viva, sin protección, sin ir en brazos; bPush: además, fuera del caparazón). */
	TORTUNABO_API bool CanAffect(const ATortugaCharacter* Turtle, bool bPush);

	// ── Lo que se ve y se oye ──────────────────────────────────────────────────────────────────────────────────────────

	/**
	 * Le pone a Item la malla y el icono de esta máquina: los de DT_Items (TNCatalogItemVisuals) o los del cooperativo
	 * (TNCoopItems). Lo llaman el inventario (al recibir el objeto en el servidor o al replicarse en los clientes) y los
	 * pickups.
	 */
	TORTUNABO_API void ResolveVisuals(FTN_InventoryItem& Item);

	/** Servidor: un sonido de los objetos (sintetizado, UTN_RaceItemSynthComponent) en la tortuga, para todas las máquinas. */
	TORTUNABO_API void PlayCue(ACharacter* Turtle, ETNRaceSound Sound, float Pitch = 1.f);

	/** Servidor: el cable del arpón de From a To (cosmético, en todas las máquinas). */
	TORTUNABO_API void ShowHarpoonRope(ACharacter* Turtle, const FVector& From, const FVector& To);
}
