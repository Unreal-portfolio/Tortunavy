#pragma once

#include "CoreMinimal.h"

/**
 * Reglas puras de los objetos de carrera de la issue #786 (tabla de surf, caña de pescar, remolino y cohete de feria), sin
 * mundo ni actores: valores, rumbos del movimiento, a quién engancha la caña, el remolque, la espiral del remolino y a quién
 * atrapa. Las usan TNRaceItems::ServerUse, UTN_RaceItemComponent, UTN_TurtleMovementComponent, ATN_RaceFishingHook y
 * ATN_RaceWhirlpool, y las prueba Tortunabo.Race.Items (Docs/Modo_Carrera.md, «Objetos de carrera»).
 */
namespace TNRaceItemRules
{
	// ── Tabla de surf ────────────────────────────────────────────────────────────────────────────────────────────────

	/** Lo que dura la ola (s) y cuánto multiplica la velocidad sobre la de correr (800 → 1400 cm/s). */
	constexpr float SurfSeconds = 3.f;
	constexpr float SurfMultiplier = 1.75f;
	/** Parte del giro que deja la ola: la tortuga se puede apartar a los lados, pero la ola va siempre hacia el mar. */
	constexpr float SurfSteerShare = 0.45f;
	/** Radio (cm) y altura en que la ola derriba a las tortugas que encuentra, segundos de derribo y empujón (cm/s). */
	constexpr float SurfKnockRadius = 230.f;
	constexpr float SurfKnockHeight = 200.f;
	constexpr float SurfKnockSeconds = 1.8f;
	constexpr float SurfKnockForward = 700.f;
	constexpr float SurfKnockSide = 380.f;
	constexpr float SurfKnockLift = 320.f;
	/** Pared de frente: se mira desde este tiempo (s), este tanto por delante (cm) y con esta tolerancia de frente. */
	constexpr float SurfWallMinAge = 0.25f;
	constexpr float SurfWallProbe = 60.f;
	constexpr float SurfHeadOnCos = 0.6f;
	/** Suelo que se puede pisar (componente Z de la normal): eso no es una pared. */
	constexpr float WalkableNormalZ = 0.7f;

	// ── Cohete de feria ──────────────────────────────────────────────────────────────────────────────────────────────

	/** Lo que dura el acelerón (s) y cuánto multiplica (800 → 2080 cm/s). */
	constexpr float RocketSeconds = 2.f;
	constexpr float RocketMultiplier = 2.6f;
	/** Giro máximo (grados por segundo) con el cohete: muy reducido. */
	constexpr float RocketTurnRateDeg = 45.f;
	/** Voltereta al acabar: salto (cm/s), parte de la velocidad horizontal que conserva y lo que dura la vuelta (s). */
	constexpr float FlipLaunchUp = 900.f;
	constexpr float FlipKeepHorizontal = 0.45f;
	constexpr float FlipSeconds = 0.85f;

	// ── Caña de pescar ───────────────────────────────────────────────────────────────────────────────────────────────

	/** Alcance del anzuelo (cm), lo que tarda en llegar (s) y lo que dura el remolque (s). */
	constexpr float RodRange = 2500.f;
	constexpr float HookFlightSeconds = 0.35f;
	constexpr float TowSeconds = 1.5f;
	/** Al soltarla queda este tanto por delante de la enganchada (cm, hacia el mar) y a este lado (cm). */
	constexpr float TowOvertake = 350.f;
	constexpr float TowSide = 130.f;
	/** Tope de la velocidad del remolque (cm/s). */
	constexpr float TowMaxSpeed = 2600.f;

	// ── Remolino ─────────────────────────────────────────────────────────────────────────────────────────────────────

	/** Lo que dura en el suelo (s), radio en que atrapa (cm) y a qué distancia por detrás se deja (cm). */
	constexpr float WhirlLifeSeconds = 12.f;
	constexpr float WhirlRadius = 260.f;
	constexpr float WhirlDropBehind = 320.f;
	/** Lo que la hace girar y la atrae (s), las vueltas por segundo y lo que se hunde en el centro (cm). */
	constexpr float WhirlPullSeconds = 1.5f;
	constexpr float WhirlTurnsPerSecond = 1.6f;
	constexpr float WhirlSink = 25.f;
	/** Mareo al soltarla: segundos y fracción de la velocidad que le queda. */
	constexpr float WhirlDizzySeconds = 1.f;
	constexpr float WhirlDizzySpeedFactor = 0.4f;
	/** Salida al soltarla (cm/s): hacia fuera y hacia arriba. */
	constexpr float WhirlFlingOut = 650.f;
	constexpr float WhirlFlingUp = 520.f;
	/** Quien lo suelta no cae en él estos segundos; la que acaba de salir, tampoco en el mismo. */
	constexpr float WhirlOwnerImmunity = 2.f;
	constexpr float WhirlRecatchSeconds = 3.f;
	/** Remolinos a la vez en la playa como mucho. */
	constexpr int32 MaxWhirlpools = 8;

	// ── Movimiento con estilo (va en la predicción, issue #22) ───────────────────────────────────────────────────────

	/**
	 * Cómo mueve un objeto a la tortuga. Va codificado en el multiplicador de velocidad del movimiento (la ola y el cohete
	 * tienen un valor propio y exacto): así viaja con él en FTNSavedMove_Turtle, con el mismo margen del servidor
	 * (TNRaceItems::ResolveClaimedBoost) y al repetir movimientos, sin campos nuevos en la red.
	 */
	enum class EMoveStyle : uint8
	{
		Normal,
		Surf,
		Rocket
	};

	/** Margen para reconocer el valor exacto de un estilo en el multiplicador. */
	constexpr float StyleTolerance = 0.002f;

	inline EMoveStyle MoveStyleOf(float Multiplier)
	{
		if (FMath::Abs(Multiplier - RocketMultiplier) <= StyleTolerance)
		{
			return EMoveStyle::Rocket;
		}
		if (FMath::Abs(Multiplier - SurfMultiplier) <= StyleTolerance)
		{
			return EMoveStyle::Surf;
		}
		return EMoveStyle::Normal;
	}

	/** Un multiplicador de turbo normal que no se confunde con un estilo (se aparta un poco si cae justo en uno). */
	inline float AvoidStyleValues(float Multiplier)
	{
		return MoveStyleOf(Multiplier) == EMoveStyle::Normal ? Multiplier : Multiplier + 4.f * StyleTolerance;
	}

	/** Vector en el plano y unitario (cero si no tiene dirección en planta). */
	inline FVector Flat(const FVector& V)
	{
		return FVector(V.X, V.Y, 0.0).GetSafeNormal();
	}

	/**
	 * Rumbo de la ola: siempre hacia Course (hacia el mar) con una parte del lado que pide el jugador (Input, la aceleración
	 * de sus teclas en el plano; cero si no pulsa nada). Unitario.
	 */
	inline FVector SurfHeading(const FVector& Course, const FVector& Input, float SteerShare)
	{
		FVector Forward = Flat(Course);
		if (Forward.IsNearlyZero())
		{
			Forward = FVector::ForwardVector;
		}
		const FVector Right(-Forward.Y, Forward.X, 0.0);
		const double Lateral = FMath::Clamp(FVector::DotProduct(Flat(Input), Right), -1.0, 1.0);
		return (Forward + Right * (Lateral * static_cast<double>(SteerShare))).GetSafeNormal();
	}

	/**
	 * Rumbo del cohete: desde Current (hacia donde va) gira hacia Wanted (lo que pide el jugador; cero = recto) como mucho
	 * MaxTurnRadians. Sin Current, Wanted; sin ninguno de los dos, +X. Unitario.
	 */
	inline FVector RocketHeading(const FVector& Current, const FVector& Wanted, float MaxTurnRadians)
	{
		const FVector From = Flat(Current);
		const FVector To = Flat(Wanted);
		if (From.IsNearlyZero())
		{
			return To.IsNearlyZero() ? FVector::ForwardVector : To;
		}
		if (To.IsNearlyZero())
		{
			return From;
		}
		const double Angle = FMath::Atan2(From.X * To.Y - From.Y * To.X, FVector::DotProduct(From, To));
		const double Limit = FMath::Max(0.0, static_cast<double>(MaxTurnRadians));
		const double Turn = FMath::Clamp(Angle, -Limit, Limit);
		const double C = FMath::Cos(Turn);
		const double S = FMath::Sin(Turn);
		return FVector(From.X * C - From.Y * S, From.X * S + From.Y * C, 0.0).GetSafeNormal();
	}

	/** true si un golpe con normal Normal es una pared (no se pisa) de frente al ir hacia Heading: la ola se acaba. */
	inline bool IsHeadOnWall(const FVector& Normal, const FVector& Heading, float WalkableZ, float HeadOnCos)
	{
		if (Normal.Z >= WalkableZ)
		{
			return false;
		}
		const FVector Wall = Flat(Normal);
		const FVector Dir = Flat(Heading);
		return !Wall.IsNearlyZero() && !Dir.IsNearlyZero() && FVector::DotProduct(-Wall, Dir) >= HeadOnCos;
	}

	// ── Caña de pescar ───────────────────────────────────────────────────────────────────────────────────────────────

	/** Una tortuga a la que se puede lanzar el anzuelo: cuánto ha avanzado (0-1) y a qué distancia está (cm). */
	struct FRodCandidate
	{
		float Progress = 0.f;
		float Distance = 0.f;
	};

	/**
	 * A quién engancha la caña de quien va en MyProgress: la más cercana de las que van por delante a Range o menos. INDEX_NONE
	 * si no hay ninguna (la que va la primera nunca tiene a nadie: el objeto no se gasta).
	 */
	inline int32 PickRodTarget(float MyProgress, const TArray<FRodCandidate>& Candidates, float Range)
	{
		int32 Best = INDEX_NONE;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const FRodCandidate& Candidate = Candidates[Index];
			if (Candidate.Progress <= MyProgress || Candidate.Distance > Range)
			{
				continue;
			}
			if (Best == INDEX_NONE || Candidate.Distance < Candidates[Best].Distance)
			{
				Best = Index;
			}
		}
		return Best;
	}

	/**
	 * Dónde la deja el remolque: donde estará la enganchada al acabar (TargetAt más su velocidad en planta durante Seconds),
	 * Overtake por delante hacia Course y SideOffset al lado de la pescadora (Side > 0: a la derecha de la carrera).
	 */
	inline FVector TowLanding(const FVector& TargetAt, const FVector& TargetVelocity, const FVector& Course, float Side, float Seconds,
		float Overtake, float SideOffset)
	{
		FVector Forward = Flat(Course);
		if (Forward.IsNearlyZero())
		{
			Forward = FVector::ForwardVector;
		}
		const FVector Right(-Forward.Y, Forward.X, 0.0);
		const FVector Drift(TargetVelocity.X * Seconds, TargetVelocity.Y * Seconds, 0.0);
		return TargetAt + Drift + Forward * Overtake + Right * (Side >= 0.f ? SideOffset : -SideOffset);
	}

	/**
	 * Velocidad de salida para ir de From a To en Seconds por el aire con la gravedad GravityZ (negativa, cm/s²), con la
	 * horizontal recortada a MaxHorizontal. Es lo que lanza el servidor con LaunchFromServer.
	 */
	inline FVector BallisticVelocity(const FVector& From, const FVector& To, float Seconds, float GravityZ, float MaxHorizontal)
	{
		const double T = FMath::Max(0.1, static_cast<double>(Seconds));
		FVector Horizontal((To.X - From.X) / T, (To.Y - From.Y) / T, 0.0);
		Horizontal = Horizontal.GetClampedToMaxSize(MaxHorizontal);
		const double Vz = (To.Z - From.Z) / T - 0.5 * static_cast<double>(GravityZ) * T;
		return FVector(Horizontal.X, Horizontal.Y, Vz);
	}

	// ── Remolino ─────────────────────────────────────────────────────────────────────────────────────────────────────

	/**
	 * Donde va la atrapada T segundos después de entrar (en planta, respecto al centro): de su sitio de entrada (Entry) al
	 * centro en PullSeconds, girando TurnsPerSecond vueltas por segundo. Igual en todas las máquinas.
	 */
	inline FVector WhirlOffset(const FVector& Entry, float T, float PullSeconds, float TurnsPerSecond)
	{
		const double Alpha = FMath::Clamp(static_cast<double>(T) / FMath::Max(0.1, static_cast<double>(PullSeconds)), 0.0, 1.0);
		// Se acerca deprisa al principio y despacio al final (cae hacia el ojo del remolino).
		const double Radius = Flat(Entry).IsNearlyZero() ? 0.0 : FVector(Entry.X, Entry.Y, 0.0).Size() * (1.0 - Alpha) * (1.0 - 0.35 * Alpha);
		const double Angle0 = FMath::Atan2(Entry.Y, Entry.X);
		const double Angle = Angle0 + 2.0 * PI * static_cast<double>(TurnsPerSecond) * static_cast<double>(FMath::Max(0.f, T));
		return FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.0);
	}

	/** Giro (grados) de la atrapada T segundos después de entrar: da vueltas sobre sí misma, más deprisa hacia el centro. */
	inline float WhirlSpinYaw(float EntryYaw, float T, float PullSeconds, float TurnsPerSecond)
	{
		const float Alpha = FMath::Clamp(T / FMath::Max(0.1f, PullSeconds), 0.f, 1.f);
		const float Turns = TurnsPerSecond * T * (1.f + 0.6f * Alpha);
		return FRotator::NormalizeAxis(EntryYaw + 360.f * Turns);
	}

	/** Lo que decide si el remolino atrapa a una tortuga. */
	struct FWhirlCatchView
	{
		/** Distancia en planta (cm) y diferencia de altura (cm) al centro. */
		float Distance2D = 0.f;
		float HeightGap = 0.f;
		/** Es quien lo soltó y cuántos segundos tiene el remolino. */
		bool bIsOwner = false;
		float WhirlAge = 0.f;
		/** Segundos desde que salió de este mismo remolino (< 0 si nunca). */
		float SinceReleased = -1.f;
		/** Se le puede dar (viva, sin aturdir, sin derribar, sin ir en el pico ni el protector puesto). */
		bool bCanBeHit = true;
		/** El remolino ya tiene a otra. */
		bool bBusy = false;
		/** La carrera está en marcha. */
		bool bRaceLive = true;
	};

	inline bool CanWhirlCatch(const FWhirlCatchView& View)
	{
		if (!View.bRaceLive || View.bBusy || !View.bCanBeHit)
		{
			return false;
		}
		if (View.bIsOwner && View.WhirlAge < WhirlOwnerImmunity)
		{
			return false;
		}
		if (View.SinceReleased >= 0.f && View.SinceReleased < WhirlRecatchSeconds)
		{
			return false;
		}
		return View.Distance2D <= WhirlRadius && FMath::Abs(View.HeightGap) <= 160.f;
	}
}
