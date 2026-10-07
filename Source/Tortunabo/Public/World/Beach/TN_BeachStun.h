#pragma once

#include "CoreMinimal.h"

class ACharacter;
class UObject;

/**
 * Aturdimiento del modo carrera (en la playa no se muere: lo que mataría, aturde). Lo usan trampas, enemigos y las
 * reglas (ATN_BeachRaceGameMode). Implementación: Private/World/Beach/TN_BeachStun.cpp (agente de reglas).
 *
 * También el árbitro de quién mueve a la tortuga (ETNBeachMover, Docs/Modo_Carrera.md, «Quién mueve a la tortuga»): la
 * patada de la tormenta y la red de seguridad se reservan a la tortuga mientras la recolocan (ClaimTurtle) y, mientras,
 * ni StunTurtle, ni KnockDownTurtle, ni los enemigos la tocan; así nadie la relanza en cadena.
 */
namespace TNBeach
{
	/**
	 * Quién mueve a la tortuga ahora por encima de su propio control, de menos a más prioridad. Lo de más prioridad manda:
	 * lo de menos no la relanza mientras tanto.
	 */
	enum class ETNBeachMover : uint8
	{
		/** Su propio movimiento. */
		None,
		/** Lanzada por el aire con su cápsula (trampolín, pala, huevos...): inmune a la caída hasta aterrizar. */
		Launch,
		/** En su bola del caparazón (aturdida o no). */
		Ball,
		/** Derribada, en ragdoll. */
		Knockdown,
		/** En brazos de otra tortuga. */
		Carried,
		/** En la boca o el pico de un enemigo. */
		Held,
		/** Patada de la tormenta en vuelo: la tormenta la reserva hasta que aterriza donde tocaba. */
		StormKick,
		/** Recién recolocada por la red de seguridad (reservada un momento). */
		SafetyNet,
	};

	/**
	 * Servidor: mete a la tortuga en su caparazón como bola (UTN_ShellComponent: cuerpo físico, salida bloqueada) con
	 * la velocidad Launch (cero = cae donde está), la deja temblando y mareada (pájaros y estrellas) durante Seconds y
	 * después la suelta para que pueda salir. Si ya estaba aturdida, alarga el aturdimiento hasta el mayor de los dos
	 * finales. No hace nada en clientes, con tortugas muertas, con Seconds <= 0 ni si ahora la mueve algo que manda más
	 * (CanStunOver: la sujeción de un enemigo, la patada de la tormenta o la red de seguridad). El enemigo que
	 * la sujeta la suelta él antes de aturdirla (EndHoldTurtle y después StunTurtle): nunca hay una bola que alguien
	 * sigue colocando en su pico o en su boca.
	 */
	TORTUNABO_API void StunTurtle(ACharacter* Turtle, float Seconds, const FVector& Launch = FVector::ZeroVector);

	/**
	 * Servidor: derribo con ragdoll y mareo, como el de la piel de plátano (ATortugaCharacter::ApplyKnockdown), con el
	 * empujón Impulse, durante Seconds. Para golpes secos (erizo, cagada de gaviota, rueda de quad…): no todo es la bola.
	 * No hace nada en clientes, con tortugas muertas, con Seconds <= 0 ni si ahora la mueve algo que manda más
	 * (CanStunOver).
	 */
	TORTUNABO_API void KnockDownTurtle(ACharacter* Turtle, float Seconds, const FVector& Impulse = FVector::ZeroVector);

	/**
	 * Lógica pura del árbitro: si aturdir o derribar (StunTurtle, KnockDownTurtle) puede con quien la mueve ahora (Mover,
	 * de GetTurtleMover). No pueden con la sujeción de un enemigo (la suelta él antes), la patada de la tormenta ni la red de
	 * seguridad; con lo demás, sí (la bola se alarga, el derribo se levanta, quien la lleva la suelta, un
	 * lanzamiento acaba).
	 */
	inline bool CanStunOver(ETNBeachMover Mover)
	{
		return Mover != ETNBeachMover::Held && Mover != ETNBeachMover::StormKick && Mover != ETNBeachMover::SafetyNet;
	}

	/**
	 * Servidor: la tortuga se mete en su caparazón mientras la sujeta un enemigo (el pico de una gaviota, la boca de un
	 * lagarto...): se escurre. Quien la sujeta la suelta ya, antes de que nazca la bola (ATN_BeachEnemy::ServerSlipHeldTurtle:
	 * la gaviota, aturdida en bola como al acabar el vuelo; los demás, sin más). true si alguien la sujetaba.
	 */
	TORTUNABO_API bool SlipFromHolder(ACharacter* Turtle);

	/** true si la tortuga está aturdida por StunTurtle (en cualquier máquina, con estado replicado). */
	TORTUNABO_API bool IsTurtleStunned(const ACharacter* Turtle);

	/**
	 * true si la tortuga va tirada en plancha en el momento justo: pose de panzazo en el aire o arrastrándose aún deprisa
	 * (TNBeachGullTuning::DodgesByBellyDive). Lo que cae desde arriba (cagadas de gaviota) le pasa por encima. Servidor y
	 * dueño lo saben al momento; las demás máquinas, con el panzazo replicado.
	 */
	TORTUNABO_API bool IsDodgingByBellyDive(const ACharacter* Turtle);

	// ── Quién mueve a la tortuga (servidor) ──

	/** Lo que se ve de la tortuga para decidir quién la mueve (ResolveMover). */
	struct FTNMoverView
	{
		/** Reserva vigente (ClaimTurtle), None si no hay. */
		ETNBeachMover Claim = ETNBeachMover::None;
		bool bHeld = false;
		bool bCarried = false;
		bool bKnockedDown = false;
		bool bInShell = false;
		bool bFallImmune = false;
	};

	/**
	 * Lógica pura del árbitro: quién la mueve según lo que se ve. La reserva vigente, lo primero; luego
	 * un enemigo que la sujeta, otra tortuga que la lleva, el derribo, su bola y un lanzamiento por el aire.
	 */
	inline ETNBeachMover ResolveMover(const FTNMoverView& View)
	{
		if (View.Claim != ETNBeachMover::None)
		{
			return View.Claim;
		}
		if (View.bHeld)
		{
			return ETNBeachMover::Held;
		}
		if (View.bCarried)
		{
			return ETNBeachMover::Carried;
		}
		if (View.bKnockedDown)
		{
			return ETNBeachMover::Knockdown;
		}
		if (View.bInShell)
		{
			return ETNBeachMover::Ball;
		}
		return View.bFallImmune ? ETNBeachMover::Launch : ETNBeachMover::None;
	}

	/** Quién la mueve ahora: la reserva vigente o, si no hay, lo que se ve de su estado (enemigo, brazos, derribo...). */
	TORTUNABO_API ETNBeachMover GetTurtleMover(const ACharacter* Turtle);

	/** Nombre para el registro. */
	TORTUNABO_API const TCHAR* GetMoverName(ETNBeachMover Mover);

	/** Servidor: Mover (StormKick, SafetyNet o Launch) se reserva la tortuga Seconds (sustituye a la reserva que hubiera). */
	TORTUNABO_API void ClaimTurtle(ACharacter* Turtle, ETNBeachMover Mover, float Seconds);

	/** Servidor: suelta la reserva de Mover (si es la suya; la de otro se queda). */
	TORTUNABO_API void ReleaseTurtle(ACharacter* Turtle, ETNBeachMover Mover);

	/** Servidor: la reserva vigente (None si no hay o ya ha caducado). */
	TORTUNABO_API ETNBeachMover GetTurtleClaim(const ACharacter* Turtle);

	/** true si la patada de la tormenta o la red de seguridad la están recolocando (tiene reserva vigente). */
	TORTUNABO_API bool IsTurtleRelocating(const ACharacter* Turtle);

	/** Servidor: la tormenta no patea a la tortuga durante Seconds (tras aterrizar de una patada o tras un rescate). */
	TORTUNABO_API void GrantStormGrace(ACharacter* Turtle, float Seconds);

	/** Servidor: true mientras dura la gracia de la tormenta. */
	TORTUNABO_API bool HasStormGrace(const ACharacter* Turtle);

	/**
	 * Servidor: teletransporte limpio. La suelta del enemigo que la sujete (que deja el ataque:
	 * ATN_BeachEnemy::ServerReleaseHeldTurtle), de lo que lleve y de quien la lleve en brazos, la levanta del derribo, le
	 * quita el aturdimiento y el caparazón (sin bola), para su movimiento y la pone en Where (la cápsula de pie; si queda
	 * algo en el aire, cae). Al salir del caparazón y del derribo se le devuelven la colisión de la cápsula, el movimiento,
	 * su suavizado y la réplica del movimiento. La caída se empieza a contar en Where (el teletransporte no cuenta como
	 * caída: sin eso, desde lo alto se metía sola en una bola al ponerla de pie).
	 */
	TORTUNABO_API void RelocateTurtle(ACharacter* Turtle, const FTransform& Where);

	/**
	 * Servidor: sitio de arena abierta donde poner de pie a la tortuga, en Desired o alrededor (anillos cada 2,5 m hasta
	 * SearchRadius; 0 = solo ese punto): la primera superficie desde arriba, llana, con la cápsula de pie cabiendo, fuera del
	 * agua y a más de AvoidRadius de cada punto de Avoid. OutTransform: la cápsula de pie encima del
	 * suelo, con el giro de la tortuga.
	 */
	TORTUNABO_API bool FindOpenSandSpot(const ACharacter* Turtle, const FVector& Desired, float SearchRadius, FTransform& OutTransform,
		const TArray<FVector>* Avoid = nullptr, float AvoidRadius = 0.f);

}
