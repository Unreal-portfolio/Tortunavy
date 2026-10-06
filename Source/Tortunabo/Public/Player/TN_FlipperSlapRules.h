#pragma once

#include "CoreMinimal.h"

/**
 * Cuentas puras del guantazo con la aleta (#832, #709): tiempos, alcance, cono, a quién da y cuánto empuja. Sin mundo ni
 * actores; las usan UTN_FlipperSlapComponent y UTN_TurtleAnimInstance y las prueba Tortunabo.FlipperSlap.
 *
 * El botón de ataque sin objeto ni arma en las aletas da un guantazo muy rápido (SwingSeconds) que no corta el movimiento.
 * El servidor decide a quién da: la tortuga más cercana dentro de un cono delante de la que mira (cm, centro a centro).
 * A quien da lo deja mareado DizzySeconds (el mareo de siempre: lento un momento) y le da un empujoncito, sin derribo.
 */
namespace TNFlipperSlap
{
	/** Lo que dura el golpe de aleta entero (ida, impacto y vuelta). */
	inline constexpr float SwingSeconds = 0.25f;

	/** Fase del golpe (0..1) en que la aleta llega al otro lado: el momento del impacto. */
	inline constexpr float StrikePhase = 0.4f;

	/** Entre el inicio de un guantazo y el del siguiente (el golpe más una pequeña espera, para que no se abuse). */
	inline constexpr float CooldownSeconds = 0.5f;

	/** El servidor acepta el siguiente guantazo con esta parte de la espera ya cumplida (el ping y los fotogramas no son exactos). */
	inline constexpr float ServerCooldownTolerance = 0.75f;

	/** Segundos de mareo del golpeado. */
	inline constexpr float DizzySeconds = 0.5f;

	/** Alcance (cm, centro a centro, en horizontal) y semiángulo del cono delante de quien golpea. */
	inline constexpr float Reach = 200.f;
	inline constexpr float HalfAngleDeg = 55.f;

	/** A menos de esta distancia (pegada a quien golpea) se da aunque no esté justo delante. */
	inline constexpr float CloseRange = 60.f;

	/** Diferencia de altura máxima entre las dos tortugas (cm). */
	inline constexpr float MaxHeightDifference = 150.f;

	/** Empujoncito: velocidad horizontal y saltito (cm/s). Sin derribo. */
	inline constexpr float PushSpeed = 480.f;
	inline constexpr float PushUp = 150.f;

	/** Cuánto pesa el ángulo (cm por grado) al elegir entre dos tortugas en el cono: gana la más cercana y mejor centrada. */
	inline constexpr float AngleWeight = 1.5f;

	/** El mismo vector sin la componente vertical, normalizado; hacia delante si no tiene horizontal. */
	inline FVector FlatForward(const FVector& Forward)
	{
		const FVector Flat = Forward.GetSafeNormal2D();
		return Flat.IsNearlyZero() ? FVector::ForwardVector : Flat;
	}

	/**
	 * Si Victim está al alcance del guantazo de quien está en Origin y mira hacia Forward. OutScore: cuanto menor, mejor
	 * objetivo (distancia más el ángulo ponderado). false fuera de alcance, de altura o del cono.
	 */
	inline bool InArc(const FVector& Origin, const FVector& Forward, const FVector& Victim, float& OutScore)
	{
		OutScore = TNumericLimits<float>::Max();
		const FVector Fwd = FlatForward(Forward);
		const FVector Rel(Victim.X - Origin.X, Victim.Y - Origin.Y, 0.0);
		const float Dist = static_cast<float>(Rel.Size());
		if (Dist > Reach || FMath::Abs(Victim.Z - Origin.Z) > MaxHeightDifference)
		{
			return false;
		}
		float AngleDeg = 0.f;
		if (Dist > 1.f)
		{
			const float Cos = FMath::Clamp(static_cast<float>(FVector::DotProduct(Rel / Dist, Fwd)), -1.f, 1.f);
			AngleDeg = FMath::RadiansToDegrees(FMath::Acos(Cos));
		}
		if (Dist > CloseRange && AngleDeg > HalfAngleDeg)
		{
			return false;
		}
		OutScore = Dist + AngleDeg * AngleWeight;
		return true;
	}

	/** Índice de la mejor tortuga de Victims (posiciones) para el guantazo de Origin hacia Forward; INDEX_NONE si ninguna. */
	inline int32 PickTarget(const FVector& Origin, const FVector& Forward, TConstArrayView<FVector> Victims)
	{
		int32 Best = INDEX_NONE;
		float BestScore = TNumericLimits<float>::Max();
		for (int32 i = 0; i < Victims.Num(); ++i)
		{
			float Score = 0.f;
			if (InArc(Origin, Forward, Victims[i], Score) && Score < BestScore)
			{
				BestScore = Score;
				Best = i;
			}
		}
		return Best;
	}

	/** Velocidad del empujoncito: horizontal, entre la dirección de quien golpea a la golpeada y hacia donde mira, y un saltito. */
	inline FVector PushVelocity(const FVector& Origin, const FVector& Forward, const FVector& Victim)
	{
		const FVector Fwd = FlatForward(Forward);
		FVector Away = FVector(Victim.X - Origin.X, Victim.Y - Origin.Y, 0.0).GetSafeNormal();
		if (Away.IsNearlyZero())
		{
			Away = Fwd;
		}
		FVector Push = (Away + Fwd).GetSafeNormal2D();
		if (Push.IsNearlyZero())
		{
			Push = Away;
		}
		return Push * PushSpeed + FVector::UpVector * PushUp;
	}

	/** Si ya ha pasado la espera desde el inicio del último guantazo (Tolerance: la parte de la espera que basta). */
	inline bool IsCooledDown(double Now, double LastStart, float Tolerance = 1.f)
	{
		return Now - LastStart >= static_cast<double>(CooldownSeconds * Tolerance);
	}

	/** Fase (0..1) del golpe que empezó en Start; -1 si aún no ha empezado o ya acabó. */
	inline float SwingPhase(double Now, double Start)
	{
		const double Phase = (Now - Start) / static_cast<double>(SwingSeconds);
		return (Phase >= 0.0 && Phase < 1.0) ? static_cast<float>(Phase) : -1.f;
	}

	/** Cuánto manda el golpe sobre la aleta (0..1) en la fase Phase: entra deprisa, se queda en el golpe y suelta en la vuelta. */
	inline float SwingWeight(float Phase)
	{
		if (Phase < 0.f || Phase >= 1.f)
		{
			return 0.f;
		}
		const float In = FMath::Clamp(Phase / 0.15f, 0.f, 1.f);
		const float Out = FMath::Clamp((1.f - Phase) / 0.3f, 0.f, 1.f);
		return (In * In * (3.f - 2.f * In)) * (Out * Out * (3.f - 2.f * Out));
	}

	/** Segundos que faltan desde Now hasta el impacto del golpe que empezó en Start (0 si ya pasó). */
	inline float ImpactDelay(double Now, double Start)
	{
		return static_cast<float>(FMath::Max(0.0, Start + static_cast<double>(StrikePhase * SwingSeconds) - Now));
	}
}
