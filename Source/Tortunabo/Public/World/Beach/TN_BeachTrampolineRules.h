#pragma once

#include "CoreMinimal.h"

/**
 * Reglas puras del rebote de una tortuga en el trampolín de la playa (ATN_BeachTrampoline), sin mundo ni actores. Las
 * aplica el movimiento de la tortuga (UTN_TurtleMovementComponent) al empezar cada paso, en el servidor y en el cliente
 * dueño, también al repetir pasos tras una corrección (#21), y las prueba Tortunabo.Beach.Trampoline.
 *
 * Solo dependen del estado del paso (la velocidad con la que empieza): ni relojes del mundo ni esperas. Antes el rebote
 * llegaba por el golpe o el solape del sensor (fuera del paso o en el siguiente) y una espera con la hora del mundo, que no
 * es la misma en las dos máquinas ni al repetir pasos: el cliente y el servidor rebotaban en pasos distintos y había
 * corrección.
 */
namespace TNTrampolineRules
{
	/**
	 * Velocidad vertical (cm/s) por encima de la cual aún sube del rebote anterior o de un salto: no rebota. Es lo que impide
	 * dos rebotes seguidos en el mismo contacto (el rebote sale siempre más rápido que esto hacia arriba).
	 */
	constexpr double MaxRisingSpeed = 150.0;

	/** Ajustes del rebote, ya con los de la variante y los del potenciado. */
	struct FBounceTuning
	{
		/** Velocidad vertical sin caída (cm/s). */
		double BaseUp = 1250.0;
		/** Velocidad vertical extra por cada cm/s de caída por encima de 300. */
		double FallGain = 0.55;
		/** Tope de la velocidad vertical. */
		double MaxUp = 2000.0;
		/** Fracción de la velocidad horizontal que se conserva. */
		double KeepHorizontal = 0.75;
		/** Empujón hacia el mar (cm/s) que se suma a la horizontal conservada. */
		double Push = 320.0;
		/** Tope de la velocidad horizontal. */
		double MaxHorizontal = 1100.0;
	};

	/** Con esta velocidad al empezar el paso, tocando el trampolín, rebota. */
	inline bool CanBounce(const FVector& Velocity)
	{
		return Velocity.Z <= MaxRisingSpeed;
	}

	/** Velocidad vertical del rebote: caer de más alto rebota más, con tope (y nunca menos que la de base, si cabe). */
	inline double BounceUp(double FallSpeed, const FBounceTuning& Tuning)
	{
		const double Extra = Tuning.FallGain * FMath::Max(0.0, FallSpeed - 300.0);
		return FMath::Clamp(Tuning.BaseUp + Extra, FMath::Min(Tuning.BaseUp, Tuning.MaxUp), Tuning.MaxUp);
	}

	/**
	 * Velocidad con la que sale del trampolín: hacia arriba BounceUp y, en horizontal, KeepHorizontal de la que llevaba
	 * más el empujón hacia el mar (SeaDir: horizontal y unitario), con tope.
	 */
	inline FVector BounceVelocity(const FVector& Velocity, const FVector& SeaDir, const FBounceTuning& Tuning)
	{
		const double Up = BounceUp(FMath::Max(0.0, -Velocity.Z), Tuning);
		const FVector Horizontal = (FVector(Velocity.X, Velocity.Y, 0.0) * Tuning.KeepHorizontal + SeaDir * Tuning.Push)
			.GetClampedToMaxSize(Tuning.MaxHorizontal);
		return FVector(Horizontal.X, Horizontal.Y, Up);
	}

	/** Distancia (cm) bajo la cápsula hasta la que un trampolín deja una caída larga sin bola automática. */
	constexpr double AutoShellLookDown = 3000.0;

	/** Lo que devuelve la búsqueda del trampolín bajo la cápsula cuando no hay ninguno al alcance. */
	constexpr double NoTrampolineBelow = -1.0;

	/**
	 * La caída larga (ATortugaCharacter::TickFallRules, solo en el servidor) no mete a la tortuga en el caparazón si cae
	 * sobre un trampolín a DropToTrampoline cm o menos (NoTrampolineBelow si no hay ninguno debajo). Con la bola, el
	 * servidor rebotaba un caparazón con física (o nada) mientras el cliente dueño, que aún no sabía de la bola, rebotaba
	 * como tortuga: correcciones de 50-110 cm en las caídas de más de 5 m (#21). Se mira en cada fotograma: si al final no
	 * cae sobre él, la bola llega igual.
	 */
	inline bool HoldsAutoShell(double DropToTrampoline)
	{
		return DropToTrampoline >= 0.0 && DropToTrampoline <= AutoShellLookDown;
	}

	/** Fuerza de la deformación y del boing (de 0,35 a 1) para un rebote de velocidad vertical Up. */
	inline float BounceStrength(double Up, const FBounceTuning& Tuning)
	{
		return static_cast<float>(FMath::Clamp(Up / FMath::Max(1.0, Tuning.MaxUp), 0.35, 1.0));
	}

	// ── Tentáculos de la medusa (#683) ──

	/** Qué toca una tortuga alrededor de una medusa trampolín. */
	enum class ETentacleContact : uint8
	{
		/** Ni la campana ni los tentáculos. */
		None,
		/** Encima de la campana: rebota (CanBounce y BounceVelocity), sin picar. */
		Bell,
		/** Los tentáculos desde la arena: aturdimiento corto y ralentización (nada de daño ni veneno). */
		Sting,
	};

	/**
	 * Contacto de una tortuga con los pies a FeetZ (cm sobre la arena del centro) a Rho cm del eje de la medusa. La campana
	 * mide BellR de radio; los tentáculos llegan hasta ReachR por la arena y pican hasta TentacleTopZ de alto: más arriba
	 * dentro de la campana es estar encima de ella.
	 */
	inline ETentacleContact TentacleContact(double Rho, double FeetZ, double BellR, double ReachR, double TentacleTopZ)
	{
		if (Rho > ReachR)
		{
			return ETentacleContact::None;
		}
		if (FeetZ > TentacleTopZ)
		{
			return Rho <= BellR ? ETentacleContact::Bell : ETentacleContact::None;
		}
		return ETentacleContact::Sting;
	}

	/** Lo que hace un picotazo de tentáculo: aturdimiento corto, fracción de la velocidad que se conserva y cuánto dura. */
	constexpr float StingStunSeconds = 0.8f;
	constexpr float StingSpeedFactor = 0.6f;
	constexpr float StingSlowSeconds = 3.f;
	/** Sin volver a picar a la misma tortuga en estos segundos. */
	constexpr float StingCooldown = 2.5f;
}
