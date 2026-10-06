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
	// y el servidor simula ese movimiento con lo que pide solo dentro de la ventana que abrió su último cambio
	// (FPredictedCapGrace); fuera de ella manda lo que diga el servidor, pida lo que pida el cliente.
	//
	// La ventana se mide con el reloj de movimientos del dueño (la suma de los DeltaTime de sus movimientos validados por el
	// servidor), no con la hora de llegada: un movimiento que llega tarde por un retraso o una pérdida se juzga por cuándo se
	// hizo. Se cierra en cuanto el dueño pide lo mismo que el servidor (ya se ha enterado) o al pasar
	// PredictedCapGraceSeconds, y un cambio mientras está abierta no la alarga. Además, el tiempo concedido sale de un
	// presupuesto (PredictedCapGraceBudgetSeconds) que se recarga despacio: coger y soltar sin parar no encadena exenciones.

	/** Topes predichos: uno por bit, en este orden. */
	inline constexpr int32 NumPredictedCaps = 2;
	inline constexpr uint8 PredictedCapMareoBit = 1 << 0;
	inline constexpr uint8 PredictedCapCarryBit = 1 << 1;
	inline constexpr uint8 PredictedCapAllBits = (1 << NumPredictedCaps) - 1;

	/** Duración máxima de una ventana, en tiempo de movimientos del dueño (s): cubre ida y vuelta de hasta 0,5 s. */
	inline constexpr float PredictedCapGraceSeconds = 0.5f;

	/** Presupuesto máximo de tiempo concedido (s de movimientos): dos ventanas enteras seguidas. */
	inline constexpr float PredictedCapGraceBudgetSeconds = 1.f;

	/** Recarga del presupuesto por segundo de movimientos sin conceder: una ventana entera cada 2,5 s. */
	inline constexpr float PredictedCapGraceRefillPerSecond = 0.2f;

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

	/** Servidor, un tope predicho de un dueño: la ventana de su último cambio y el presupuesto que le queda. */
	struct FPredictedCapGrace
	{
		bool bOpen = false;
		/** Reloj de movimientos del dueño cuando se abrió (s). */
		double OpenedAt = 0.0;
		float Budget = PredictedCapGraceBudgetSeconds;
	};

	/**
	 * Servidor: pone o quita el tope cuando el reloj de movimientos del dueño va por MoveClock. Abre la ventana si no hay
	 * ninguna abierta; si ya la hay (el dueño aún no ha reconocido el cambio anterior), sigue la misma, sin alargarla.
	 */
	inline FPredictedCapGrace OpenPredictedCapGrace(const FPredictedCapGrace& Grace, double MoveClock)
	{
		FPredictedCapGrace Next = Grace;
		if (!Grace.bOpen)
		{
			Next.bOpen = true;
			Next.OpenedAt = MoveClock;
		}
		return Next;
	}

	/** Resultado de un movimiento: si lleva el tope y cómo queda la ventana. */
	struct FPredictedCapStep
	{
		FPredictedCapGrace Grace;
		bool bApply = false;
	};

	/**
	 * Servidor, un movimiento validado del dueño que empieza en MoveClock y dura MoveDeltaSeconds: si aplica el tope.
	 * Si el dueño pide lo mismo que el servidor, eso, y la ventana se cierra (lo ha reconocido). Si pide otra cosa, lo que
	 * pide solo dentro de la ventana abierta (MoveClock - OpenedAt < PredictedCapGraceSeconds) y con presupuesto, que gasta
	 * el tiempo del movimiento; si no, lo del servidor y la ventana se cierra. Sin conceder, el presupuesto se recarga.
	 */
	inline FPredictedCapStep StepPredictedCap(const FPredictedCapGrace& Grace, bool bClientClaims, bool bServerActive, double MoveClock,
		float MoveDeltaSeconds)
	{
		const float Delta = FMath::Max(0.f, MoveDeltaSeconds);
		FPredictedCapStep Out;
		Out.Grace = Grace;
		Out.bApply = bServerActive;
		const bool bInWindow = Grace.bOpen && MoveClock - Grace.OpenedAt < PredictedCapGraceSeconds && Grace.Budget > 0.f;
		if (bClientClaims != bServerActive && bInWindow)
		{
			Out.bApply = bClientClaims;
			Out.Grace.Budget = FMath::Max(0.f, Grace.Budget - Delta);
			return Out;
		}
		Out.Grace.bOpen = false;
		Out.Grace.Budget = FMath::Min(PredictedCapGraceBudgetSeconds, Grace.Budget + Delta * PredictedCapGraceRefillPerSecond);
		return Out;
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
