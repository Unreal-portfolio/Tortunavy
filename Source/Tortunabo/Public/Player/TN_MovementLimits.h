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

	/**
	 * Velocidad de andar con el turbo de los objetos de carrera (Multiplier > 1): al menos la de correr, por el multiplicador
	 * y con el tope. Sin turbo, la base con el tope. Es el último paso de ResolveWalkSpeed, que usa
	 * UTN_TurtleMovementComponent::GetMaxSpeed con el turbo del movimiento que simula (issue #22).
	 */
	inline float RaceBoostWalkSpeed(float BaseSpeed, float SprintSpeed, float Multiplier, float Cap)
	{
		const float Speed = Multiplier > 1.f ? FMath::Max(BaseSpeed, SprintSpeed) * Multiplier : BaseSpeed;
		return FMath::Min(Speed, Cap);
	}

	/** Lo que decide la velocidad máxima andando (UTN_StaminaComponent::ComputeMaxWalkSpeed). */
	struct FWalkSpeedInputs
	{
		float WalkSpeed = 0.f;
		float SprintSpeed = 0.f;
		bool bSprinting = false;
		/** Multiplicador de la penalización tras la estamina ilimitada (1 = ninguna). */
		float PostBoostMultiplier = 1.f;
		/** Multiplicador del entorno: el vadeo (1 = ninguno). */
		float EnvironmentMultiplier = 1.f;
		/** Turbo de los objetos de carrera (1 = ninguno). */
		float RaceMultiplier = 1.f;
		/** El tope que manda (ResolveSpeedCap). */
		float Cap = NoCap;
	};

	/**
	 * Velocidad máxima andando: la de andar o correr, por las penalizaciones y el entorno; con turbo, al menos la de correr
	 * por el turbo (sin la penalización); y siempre recortada por el tope.
	 */
	inline float ResolveWalkSpeed(const FWalkSpeedInputs& In)
	{
		const float Speed = (In.bSprinting ? In.SprintSpeed : In.WalkSpeed) * In.PostBoostMultiplier * In.EnvironmentMultiplier;
		return RaceBoostWalkSpeed(Speed, In.SprintSpeed, In.RaceMultiplier, In.Cap);
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

	/** Aceleración con el turbo: sube con él (el doble de rápido, el triple de aceleración) para que el empujón sea casi inmediato. */
	inline float RaceBoostAcceleration(float BaseAcceleration, float Multiplier)
	{
		return Multiplier > 1.f ? BaseAcceleration * (1.f + (Multiplier - 1.f) * 2.f) : BaseAcceleration;
	}

	// ── Topes predichos en el movimiento (#575, #574) ─────────────────────────
	// Los pone el servidor (coger a otra tortuga, el mareo) y el dueño se entera media ida y vuelta después. Antes cada
	// máquina los aplicaba al enterarse: durante ese rato el cliente y el servidor andaban a velocidades distintas y el
	// servidor corregía. Ahora el dueño pide en cada movimiento los que conoce (un bit cada uno en FTNTurtleNetworkMoveData)
	// y el servidor simula ese movimiento con lo que pide mientras el cambio sea reciente (PredictedCapGraceSeconds); pasada
	// la gracia manda lo que diga el servidor, pida lo que pida el cliente.

	/** Topes predichos: uno por bit, en este orden. */
	inline constexpr int32 NumPredictedCaps = 2;
	inline constexpr uint8 PredictedCapMareoBit = 1 << 0;
	inline constexpr uint8 PredictedCapCarryBit = 1 << 1;
	inline constexpr uint8 PredictedCapAllBits = (1 << NumPredictedCaps) - 1;

	/** Margen tras poner o quitar un tope en el servidor en que vale lo que pida el cliente (s). */
	inline constexpr float PredictedCapGraceSeconds = 0.5f;

	/** Bit del tope de Source si es de los predichos (0 si no). */
	inline uint8 PredictedCapBit(FName Source)
	{
		if (Source == MareoSource()) { return PredictedCapMareoBit; }
		if (Source == CarrySource()) { return PredictedCapCarryBit; }
		return 0;
	}

	/** Posición (0..NumPredictedCaps-1) del bit Bit, que tiene que ser uno solo de los predichos. */
	inline int32 PredictedCapIndex(uint8 Bit)
	{
		return Bit == PredictedCapCarryBit ? 1 : 0;
	}

	/**
	 * Servidor, movimiento de un cliente: si aplica un tope predicho. Si el cliente pide lo mismo que tiene el servidor, eso;
	 * si no, lo que pide el cliente solo mientras el cambio del servidor sea reciente (el cliente aún no se ha enterado);
	 * pasada la gracia, lo del servidor.
	 */
	inline bool ShouldApplyPredictedCap(bool bClientClaims, bool bServerActive, double SecondsSinceServerChange,
		float GraceSeconds = PredictedCapGraceSeconds)
	{
		if (bClientClaims == bServerActive)
		{
			return bServerActive;
		}
		return SecondsSinceServerChange < GraceSeconds ? bClientClaims : bServerActive;
	}

	/** Servidor: los topes predichos con que simula el movimiento de un cliente que pide ClaimedMask. */
	inline uint8 ResolvePredictedCaps(uint8 ClaimedMask, uint8 ServerActiveMask, const double (&SecondsSinceChange)[NumPredictedCaps],
		float GraceSeconds = PredictedCapGraceSeconds)
	{
		uint8 Result = 0;
		for (int32 Index = 0; Index < NumPredictedCaps; ++Index)
		{
			const uint8 Bit = static_cast<uint8>(1 << Index);
			if (ShouldApplyPredictedCap((ClaimedMask & Bit) != 0, (ServerActiveMask & Bit) != 0, SecondsSinceChange[Index], GraceSeconds))
			{
				Result |= Bit;
			}
		}
		return Result;
	}

	/** Tope de un movimiento: el de los topes sin predecir y el de cada tope predicho de MoveMask (con su valor). */
	inline float ResolveMoveSpeedCap(float UnpredictedCap, uint8 MoveMask, const float (&PredictedValues)[NumPredictedCaps])
	{
		float Result = UnpredictedCap;
		for (int32 Index = 0; Index < NumPredictedCaps; ++Index)
		{
			if ((MoveMask & (1 << Index)) != 0)
			{
				Result = FMath::Min(Result, PredictedValues[Index]);
			}
		}
		return Result;
	}
}
