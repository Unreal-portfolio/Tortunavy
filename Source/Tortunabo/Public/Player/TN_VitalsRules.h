#pragma once

#include "CoreMinimal.h"

/**
 * Cuentas de los vitales de la tortuga (#855, plan maestro del modo único §1, decisión 9): vida, veneno e hidratación.
 * Puras, sin mundo ni actores: las usa UTN_VitalsComponent en el servidor y las prueba Tortunabo.Vitals.
 *
 * Cada función devuelve un estado nuevo y no toca el de entrada.
 *  - Vida entre 0 y MaxHealth. A 0, la tortuga muere por el flujo de muerte de ATN_RunGameMode.
 *  - Veneno: DamagePerSecond mientras le queden segundos. Uno nuevo se combina con el que ya hubiera: manda el daño por
 *    segundo mayor y la duración más larga (no se suman, para que dos picaduras seguidas no se disparen).
 *  - Hidratación entre 0 y MaxHydration; baja HydrationDrainPerSecond. A 0 desgasta la vida DehydratedHealthDrainPerSecond
 *    (supuesto anotado en la issue: 1/s por defecto).
 */
namespace TNVitals
{
	/** Ajustes de un juego de vitales (los EditDefaultsOnly de UTN_VitalsComponent). */
	struct FParams
	{
		float MaxHealth = 100.f;
		float MaxHydration = 100.f;
		float HydrationDrainPerSecond = 0.4f;
		float DehydratedHealthDrainPerSecond = 1.f;
	};

	/** Estado de los vitales de una tortuga. */
	struct FState
	{
		float Health = 100.f;
		float Hydration = 100.f;
		float PoisonDamagePerSecond = 0.f;
		float PoisonSecondsLeft = 0.f;

		bool IsPoisoned() const { return PoisonDamagePerSecond > 0.f && PoisonSecondsLeft > 0.f; }
		bool IsDepleted() const { return Health <= 0.f; }
	};

	/** Qué ha dejado la vida a cero en un paso de tiempo (Step). */
	enum class EDepletedBy : uint8
	{
		None,
		Poison,
		Dehydration,
	};

	/** Resultado de un paso de tiempo: el estado nuevo y, si la vida ha llegado a cero en él, por qué. */
	struct FStepResult
	{
		FState State;
		EDepletedBy DepletedBy = EDepletedBy::None;
	};

	/** Vitales llenos y sin veneno. */
	inline FState Full(const FParams& Params)
	{
		FState State;
		State.Health = Params.MaxHealth;
		State.Hydration = Params.MaxHydration;
		return State;
	}

	/** Quita Amount de vida (negativos y cero no hacen nada). */
	inline FState Damage(const FState& In, float Amount)
	{
		FState Out = In;
		if (Amount > 0.f)
		{
			Out.Health = FMath::Max(0.f, In.Health - Amount);
		}
		return Out;
	}

	/** Suma Amount de vida hasta MaxHealth. No cura a quien ya tiene la vida a cero: eso es revivir (Full). */
	inline FState Heal(const FState& In, const FParams& Params, float Amount)
	{
		FState Out = In;
		if (Amount > 0.f && In.Health > 0.f)
		{
			Out.Health = FMath::Min(Params.MaxHealth, In.Health + Amount);
		}
		return Out;
	}

	/** Envenena: manda el daño por segundo mayor y la duración más larga de los dos venenos. */
	inline FState Poison(const FState& In, float DamagePerSecond, float Seconds)
	{
		FState Out = In;
		if (DamagePerSecond <= 0.f || Seconds <= 0.f)
		{
			return Out;
		}
		const bool bWasPoisoned = In.IsPoisoned();
		Out.PoisonDamagePerSecond = bWasPoisoned ? FMath::Max(In.PoisonDamagePerSecond, DamagePerSecond) : DamagePerSecond;
		Out.PoisonSecondsLeft = bWasPoisoned ? FMath::Max(In.PoisonSecondsLeft, Seconds) : Seconds;
		return Out;
	}

	/** Quita el veneno. */
	inline FState CurePoison(const FState& In)
	{
		FState Out = In;
		Out.PoisonDamagePerSecond = 0.f;
		Out.PoisonSecondsLeft = 0.f;
		return Out;
	}

	/** Suma Amount de hidratación hasta MaxHydration. */
	inline FState Hydrate(const FState& In, const FParams& Params, float Amount)
	{
		FState Out = In;
		if (Amount > 0.f)
		{
			Out.Hydration = FMath::Min(Params.MaxHydration, In.Hydration + Amount);
		}
		return Out;
	}

	/**
	 * Avanza DeltaSeconds: el veneno hace su daño mientras le queden segundos (solo la parte del paso en que dura), la
	 * hidratación baja y, en la parte del paso con la hidratación a cero, la vida se desgasta. Con la vida ya a cero no
	 * hace nada.
	 */
	inline FStepResult Step(const FState& In, const FParams& Params, float DeltaSeconds)
	{
		FStepResult Result;
		Result.State = In;
		if (DeltaSeconds <= 0.f || In.IsDepleted())
		{
			return Result;
		}
		FState& Out = Result.State;

		float PoisonDamage = 0.f;
		if (In.IsPoisoned())
		{
			const float PoisonedSeconds = FMath::Min(DeltaSeconds, In.PoisonSecondsLeft);
			PoisonDamage = In.PoisonDamagePerSecond * PoisonedSeconds;
			Out.PoisonSecondsLeft = In.PoisonSecondsLeft - PoisonedSeconds;
			if (Out.PoisonSecondsLeft <= 0.f)
			{
				Out = CurePoison(Out);
			}
		}

		const float Drain = FMath::Max(0.f, Params.HydrationDrainPerSecond);
		const float SecondsWithWater = Drain > 0.f ? FMath::Min(DeltaSeconds, In.Hydration / Drain) : DeltaSeconds;
		Out.Hydration = FMath::Max(0.f, In.Hydration - Drain * DeltaSeconds);
		const float DryDamage = FMath::Max(0.f, DeltaSeconds - SecondsWithWater) * FMath::Max(0.f, Params.DehydratedHealthDrainPerSecond);

		Out = Damage(Out, PoisonDamage + DryDamage);
		if (Out.IsDepleted())
		{
			Result.DepletedBy = PoisonDamage > 0.f ? EDepletedBy::Poison : EDepletedBy::Dehydration;
		}
		return Result;
	}
}
