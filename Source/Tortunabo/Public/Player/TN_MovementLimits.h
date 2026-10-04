#pragma once

#include "CoreMinimal.h"

/**
 * Límites de movimiento con nombre (UTN_StaminaComponent): cada sistema pone y quita el suyo sin pisar a los demás.
 *
 * Antes había un solo hueco para el tope de velocidad (acabar el mareo quitaba el de llevar a otra, que volvía a correr a
 * toda velocidad) y cada sistema guardaba y restauraba el salto por su cuenta (salir de dos zonas lentas solapadas en
 * otro orden dejaba el salto del sirope). Aquí solo se decide qué valor manda; lógica pura, tests Tortunabo.Movement.Limits.
 */
namespace TNMovementLimits
{
	/** Sin tope. */
	inline constexpr float NoCap = TNumericLimits<float>::Max();

	/** Quién pone los límites fijos. Los de actores que puede haber varios a la vez (zonas lentas, algas) usan su nombre. */
	inline FName MareoSource()  { static const FName Name(TEXT("Mareo"));  return Name; }
	inline FName CarrySource()  { static const FName Name(TEXT("Carry"));  return Name; }
	inline FName ShellSource()  { static const FName Name(TEXT("Shell"));  return Name; }
	inline FName WadingSource() { static const FName Name(TEXT("Wading")); return Name; }

	/** Límite de salto con nombre: velocidad máxima (cm/s) y multiplicador sobre el salto de base. */
	struct FJumpLimit
	{
		float Cap = NoCap;
		float Multiplier = 1.f;
	};

	/** El tope de velocidad que manda: el menor de todos (NoCap si no hay ninguno). */
	inline float ResolveSpeedCap(const TMap<FName, float>& Caps)
	{
		float Result = NoCap;
		for (const TPair<FName, float>& Pair : Caps)
		{
			Result = FMath::Min(Result, Pair.Value);
		}
		return Result;
	}

	/** El salto: la base por todos los multiplicadores, recortada por el menor tope (nunca negativo). Sin límites, la base. */
	inline float ResolveJumpZ(float BaseJumpZ, const TMap<FName, FJumpLimit>& Limits)
	{
		float Multiplier = 1.f;
		float Cap = NoCap;
		for (const TPair<FName, FJumpLimit>& Pair : Limits)
		{
			Multiplier *= Pair.Value.Multiplier;
			Cap = FMath::Min(Cap, Pair.Value.Cap);
		}
		return FMath::Max(0.f, FMath::Min(BaseJumpZ * Multiplier, Cap));
	}

	/** La escala de gravedad: la menor de las impuestas (el sirope más espeso); sin ninguna, la base. */
	inline float ResolveGravityScale(float BaseScale, const TMap<FName, float>& Overrides)
	{
		if (Overrides.Num() == 0)
		{
			return BaseScale;
		}
		float Result = NoCap;
		for (const TPair<FName, float>& Pair : Overrides)
		{
			Result = FMath::Min(Result, Pair.Value);
		}
		return Result;
	}

	/**
	 * Velocidad de andar con el turbo de los objetos de carrera (Multiplier > 1): al menos la de correr, por el multiplicador
	 * y con el tope. Sin turbo, la base con el tope (lo que UTN_StaminaComponent pone en MaxWalkSpeed). La usa
	 * UTN_TurtleMovementComponent::GetMaxSpeed con el multiplicador del movimiento que simula (issue #22).
	 */
	inline float RaceBoostWalkSpeed(float BaseSpeed, float SprintSpeed, float Multiplier, float Cap)
	{
		const float Speed = Multiplier > 1.f ? FMath::Max(BaseSpeed, SprintSpeed) * Multiplier : BaseSpeed;
		return FMath::Min(Speed, Cap);
	}

	/** Aceleración con el turbo: sube con él (el doble de rápido, el triple de aceleración) para que el empujón sea casi inmediato. */
	inline float RaceBoostAcceleration(float BaseAcceleration, float Multiplier)
	{
		return Multiplier > 1.f ? BaseAcceleration * (1.f + (Multiplier - 1.f) * 2.f) : BaseAcceleration;
	}
}
