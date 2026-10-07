#pragma once

#include "CoreMinimal.h"

/**
 * Estadísticas de la tortuga de la hoja Stats del Excel (#856, plan maestro del modo único §2) y las cuentas que las sacan
 * de los ajustes del movimiento. La escala del proyecto es 1 uu = 1 cm: la cápsula de pie mide 140 cm (semialtura 70), la
 * tortuga de 1,4 m de la hoja. Lo prueba Tortunabo.Stats.Turtle sobre BP_TortugaCharacter, que es la que se juega.
 *
 * Los valores viven en los ajustes de siempre (EditDefaultsOnly): UTN_StaminaComponent (WalkSpeed, SprintSpeed, MaxStamina,
 * SprintDrainPerSecond), el JumpZVelocity del movimiento y DiveForwardSpeed/DiveDownwardSpeed de ATortugaCharacter. Aquí
 * solo están los objetivos de la hoja para comprobarlos.
 */
namespace TNTurtleStats
{
	/** Hoja Stats: velocidades (cm/s), salto (cm y s), estamina corriendo (s) y lo que añade el panzazo (cm). */
	inline constexpr float WalkSpeed = 200.f;
	inline constexpr float RunSpeed = 400.f;
	inline constexpr float JumpHeight = 120.f;
	inline constexpr float JumpAirSeconds = 0.99f;
	inline constexpr float WalkJumpDistance = 198.f;
	inline constexpr float RunJumpDistance = 396.f;
	inline constexpr float SprintSeconds = 10.f;
	inline constexpr float BellyFlopExtraDistance = 200.f;
	inline constexpr float TurtleHeight = 140.f;

	/** Altura del vértice de un salto con velocidad vertical JumpZ (cm/s) y gravedad Gravity (cm/s², en valor absoluto). */
	inline float JumpApex(float JumpZ, float Gravity)
	{
		return Gravity > 0.f ? JumpZ * JumpZ / (2.f * Gravity) : 0.f;
	}

	/** Tiempo en el aire de un salto que cae a la misma altura de la que sale (s). */
	inline float AirSeconds(float JumpZ, float Gravity)
	{
		return Gravity > 0.f ? 2.f * JumpZ / Gravity : 0.f;
	}

	/** Distancia de un salto a velocidad horizontal constante (cm): sin control en el aire, la velocidad no cambia. */
	inline float JumpDistance(float HorizontalSpeed, float JumpZ, float Gravity)
	{
		return HorizontalSpeed * AirSeconds(JumpZ, Gravity);
	}

	/** Segundos corriendo desde la estamina llena hasta vaciarla, sin peso. */
	inline float SprintDuration(float MaxStamina, float DrainPerSecond)
	{
		return DrainPerSecond > 0.f ? MaxStamina / DrainPerSecond : TNumericLimits<float>::Max();
	}

	/** Tiempo (s) de caer Height cm saliendo hacia abajo a DownSpeed cm/s. */
	inline float FallSeconds(float Height, float DownSpeed, float Gravity)
	{
		if (Gravity <= 0.f)
		{
			return DownSpeed > 0.f ? Height / DownSpeed : 0.f;
		}
		return (-DownSpeed + FMath::Sqrt(DownSpeed * DownSpeed + 2.f * Gravity * Height)) / Gravity;
	}

	/**
	 * Lo que alarga el panzazo el salto en el aire (cm): tirado en el vértice hacia donde salta, sale a DiveSpeed (la base
	 * más la inercia del salto) y DownSpeed hacia abajo, frente a seguir el salto normal hasta el suelo. El arrastre por la
	 * tripa al tocar el suelo va aparte (BellySlideDistance).
	 */
	inline float BellyFlopAirGain(float HorizontalSpeed, float DiveSpeed, float DownSpeed, float JumpZ, float Gravity)
	{
		const float HalfAir = 0.5f * AirSeconds(JumpZ, Gravity);
		const float DiveAir = FallSeconds(JumpApex(JumpZ, Gravity), DownSpeed, Gravity);
		return DiveSpeed * DiveAir - HorizontalSpeed * HalfAir;
	}
}
