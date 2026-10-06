#pragma once

#include "CoreMinimal.h"

/**
 * Brinco desde el agua (#573): nadando, el salto es un brinco hacia arriba y hacia delante para salir a orillas e isletas.
 *
 * Va dentro del movimiento, como el salto de serie: la petición viaja en la marca de salto del movimiento guardado
 * (FLAG_JumpPressed) y UTN_TurtleMovementComponent lo decide en CanAttemptJump/DoJump con el estado de ese movimiento. La
 * espera entre brincos se mide con el tiempo de simulación de los movimientos (la suma de sus DeltaTime) y se guarda en
 * cada movimiento del cliente: el dueño y el servidor deciden lo mismo en el mismo movimiento, también al repetirlos tras
 * una corrección. Antes el servidor la medía con su reloj al llegar una RPC aparte y rechazaba parte de los brincos.
 *
 * Lógica pura; tests Tortunabo.Movement.SwimHop.
 */
namespace TNSwimHop
{
	/** Espera mínima entre dos brincos (s de simulación). */
	inline constexpr float CooldownSeconds = 0.6f;

	/**
	 * Margen con el que la espera se da por acabada. El servidor suma los DeltaTime de los movimientos a partir de sus
	 * marcas de tiempo y puede quedarse unas millonésimas por encima del cliente: sin margen, en el movimiento justo en que
	 * acaba la espera uno brincaría y el otro no.
	 */
	inline constexpr float ReadyTolerance = 1.e-3f;

	/** La espera que queda (s) permite brincar. */
	inline bool IsReady(float RemainingSeconds)
	{
		return RemainingSeconds <= ReadyTolerance;
	}

	/** La espera tras simular DeltaSeconds (nunca negativa). */
	inline float Advance(float RemainingSeconds, float DeltaSeconds)
	{
		return FMath::Max(0.f, RemainingSeconds - FMath::Max(0.f, DeltaSeconds));
	}

	/** Velocidad del brinco: hacia delante en horizontal (según Forward) y hacia arriba. Sustituye a la que llevaba. */
	inline FVector HopVelocity(const FVector& Forward, float ForwardSpeed, float UpSpeed)
	{
		const FVector Flat = FVector(Forward.X, Forward.Y, 0.0).GetSafeNormal();
		return Flat * ForwardSpeed + FVector::UpVector * UpSpeed;
	}

	/** Resultado de un movimiento: si brinca y la espera que queda al acabarlo. */
	struct FStep
	{
		bool bHop = false;
		float RemainingAfter = 0.f;
	};

	/**
	 * Un movimiento completo, en el orden del motor: primero el salto (con la espera del principio del movimiento) y luego
	 * la simulación, que descuenta su DeltaSeconds. Es lo que hacen el dueño, el servidor y la repetición de movimientos.
	 */
	inline FStep Step(float RemainingAtStart, bool bJumpPressed, bool bSwimming, float DeltaSeconds)
	{
		FStep Out;
		Out.bHop = bJumpPressed && bSwimming && IsReady(RemainingAtStart);
		Out.RemainingAfter = Advance(Out.bHop ? CooldownSeconds : RemainingAtStart, DeltaSeconds);
		return Out;
	}
}
