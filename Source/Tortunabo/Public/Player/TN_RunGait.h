#pragma once

#include "CoreMinimal.h"

/**
 * Cuándo la animación de la tortuga pasa a la carrera (#834). Cuentas puras, sin mundo ni actores: las usa
 * UTN_TurtleAnimInstance y las prueba Tortunabo.RunGait.
 *
 * La carrera entra por la petición de esprintar (en cuanto se pulsa y la tortuga ya anda, sin esperar a que la velocidad
 * suba) o porque la velocidad ya es de carrera (un turbo, una bajada). Esprintar sin fuerzas no cuenta: con la estamina a 0
 * y la tecla pulsada la tortuga va a paso de andar y así se tiene que ver; y al recargar con la tecla pulsada el servidor
 * deja destellos de un fotograma de «puede esprintar» que tampoco son una carrera.
 */
namespace TNRunGait
{
	/** Con menos de esta fracción de la estamina, pedir esprintar no pone la carrera (hay que recuperar algo de aliento). */
	inline constexpr float MinStaminaFraction = 0.03f;

	/** Esprintando, ya se ve como carrera a partir de esta fracción de la velocidad de andar. */
	inline constexpr float SprintMinSpeedFraction = 0.6f;

	/** Sin esprintar, la carrera empieza a esta fracción de la velocidad de andar y es entera 0,3 por encima. */
	inline constexpr float BySpeedStartFraction = 1.08f;
	inline constexpr float BySpeedRampFraction = 0.3f;

	/**
	 * La petición de esprintar cuenta como carrera: pedida, con fuerzas (estamina ilimitada, o sin agotamiento y con más de
	 * MinStaminaFraction) y con la tortuga ya andando deprisa.
	 */
	inline bool IsSprintRun(bool bSprinting, bool bExhausted, float StaminaFraction, bool bUnlimited, float Speed, float WalkSpeed)
	{
		const bool bHasStrength = bUnlimited || (!bExhausted && StaminaFraction >= MinStaminaFraction);
		return bSprinting && bHasStrength && Speed > WalkSpeed * SprintMinSpeedFraction;
	}

	/** Peso objetivo de la carrera (0..1): entera si esprinta de verdad o si la velocidad ya es de carrera. */
	inline float RunTarget(bool bSprintRun, float Speed, float WalkSpeed)
	{
		const float BySpeed = FMath::Clamp((Speed - WalkSpeed * BySpeedStartFraction) / (WalkSpeed * BySpeedRampFraction), 0.f, 1.f);
		return FMath::Max(bSprintRun ? 1.f : 0.f, BySpeed);
	}
}
