#pragma once

#include "CoreMinimal.h"

/**
 * Reglas de la tormenta de arena del coop (#790, GDD: «Tormenta de Arena», clima con ráfagas de 8 m/s, visibilidad
 * muy reducida y 0 daño) como funciones puras. No es la tormenta de bañistas (ATN_PathStorm), que avanza por el camino y
 * mata a quien se queda detrás: esta va y viene en todo el mapa, solo frena, empuja y no deja ver.
 *
 * Todo sale de la semilla que fija el servidor y del tiempo desde que empezó el ciclo (en segundos del servidor): cada
 * máquina calcula lo mismo y en el mismo momento, sin réplica de cada ráfaga. Lo prueban Tortunabo.ProcMap.SandStorm.*.
 * Los valores son de partida, para ajustarlos jugando.
 */
namespace TNSandStorm
{
	/** Segundos desde que empieza el ciclo (la ronda) hasta la primera tormenta, de inicio a inicio entre dos y variación por semilla. */
	constexpr double FIRST_DELAY = 75.0;
	constexpr double INTERVAL = 150.0;
	constexpr double JITTER = 30.0;

	/** Segundos que dura cada tormenta y que tarda en llegar y en irse (dentro de la duración). */
	constexpr double DURATION = 35.0;
	constexpr double FADE = 5.0;

	/** Ráfagas: velocidad del viento (cm/s; GDD: 8 m/s), arrastre hacia ella (1/s), cada cuánto llega una y cuánto sopla. */
	constexpr float GUST_SPEED = 800.f;
	constexpr float GUST_DRAG = 5.f;
	constexpr double GUST_PERIOD = 6.0;
	constexpr double GUST_ON = 2.5;

	/** Velocidad máxima andando durante la tormenta, respecto a la normal (ralentización; sin daño). */
	constexpr float SPEED_FACTOR = 0.85f;

	/** Lo mínimo que el ajuste de accesibilidad deja del efecto visual (algo se ve siempre: avisa de que hay tormenta). */
	constexpr float MIN_VISUAL = 0.25f;

	/** Niebla con la tormenta en su punto (densidad de UExponentialHeightFogComponent) y su color. */
	constexpr float FOG_DENSITY = 0.12f;

	inline uint32 Hash(uint32 Seed, uint32 Salt)
	{
		uint32 X = Seed * 0x9E3779B1u + Salt * 0x85EBCA77u + 0x27D4EB2Fu;
		X = (X ^ (X >> 15)) * 0x2C1B3C6Du;
		X = (X ^ (X >> 12)) * 0x297A2D39u;
		return X ^ (X >> 15);
	}

	/** Número en [0, 1) a partir de la semilla y una sal. */
	inline double Rand01(uint32 Seed, uint32 Salt)
	{
		return static_cast<double>(Hash(Seed, Salt) & 0xFFFFFFu) / static_cast<double>(0x1000000u);
	}

	/** Segundos desde el inicio del ciclo a los que empieza la tormenta Index (desde 0). Crecen siempre y no se solapan. */
	inline double EventStart(uint32 Seed, int32 Index)
	{
		return FIRST_DELAY + static_cast<double>(Index) * INTERVAL + (Rand01(Seed, 2u * static_cast<uint32>(Index) + 1u) * 2.0 - 1.0) * JITTER;
	}

	/**
	 * Tormenta en curso a Seconds del inicio del ciclo: true con su índice y los segundos que lleva. false entre tormentas.
	 */
	inline bool EventAt(uint32 Seed, double Seconds, int32& OutIndex, double& OutLocal)
	{
		if (Seconds < FIRST_DELAY - JITTER)
		{
			return false;
		}
		const int32 Guess = FMath::Max(0, FMath::FloorToInt32((Seconds - FIRST_DELAY) / INTERVAL));
		for (int32 Index = FMath::Max(0, Guess - 1); Index <= Guess + 1; ++Index)
		{
			const double Local = Seconds - EventStart(Seed, Index);
			if (Local >= 0.0 && Local < DURATION)
			{
				OutIndex = Index;
				OutLocal = Local;
				return true;
			}
		}
		return false;
	}

	/** Fuerza de la tormenta (0..1) a Local segundos de su inicio: sube en FADE, se mantiene y baja en FADE. */
	inline float Intensity01(double Local)
	{
		if (Local <= 0.0 || Local >= DURATION)
		{
			return 0.f;
		}
		const double Edge = FMath::Min(Local, DURATION - Local) / FADE;
		return static_cast<float>(FMath::SmoothStep(0.0, 1.0, FMath::Min(Edge, 1.0)));
	}

	/** Fuerza de la ráfaga (0..1) a Local segundos de la tormenta Index: pulsos de GUST_ON cada GUST_PERIOD, con desfase por semilla. */
	inline float Gust01(uint32 Seed, int32 Index, double Local)
	{
		const float Storm = Intensity01(Local);
		if (Storm <= 0.f)
		{
			return 0.f;
		}
		const double Phase = Rand01(Seed, 7u * static_cast<uint32>(Index) + 3u) * GUST_PERIOD;
		const double T = FMath::Fmod(Local + Phase, GUST_PERIOD);
		return T < GUST_ON ? Storm * static_cast<float>(FMath::Sin(PI * T / GUST_ON)) : 0.f;
	}

	/** Dirección del viento de la tormenta Index (unitaria, en el plano del mundo). */
	inline FVector2D WindDir(uint32 Seed, int32 Index)
	{
		const double Angle = Rand01(Seed, 13u * static_cast<uint32>(Index) + 5u) * 2.0 * PI;
		return FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
	}

	/**
	 * Aceleración (cm/s²) con la que la ráfaga empuja a quien va a VelAlongWind (cm/s) en la dirección del viento: tira de
	 * ella hacia GUST_SPEED y no empuja más allá (quien ya va a 8 m/s con el viento no se acelera más).
	 */
	inline float PushAcceleration(float VelAlongWind, float Gust)
	{
		return FMath::Clamp(Gust, 0.f, 1.f) * GUST_DRAG * FMath::Max(0.f, GUST_SPEED - VelAlongWind);
	}

	/** Parte de la velocidad normal que queda con la tormenta a esa fuerza. */
	inline float SpeedFactor(float Intensity)
	{
		return FMath::Lerp(1.f, SPEED_FACTOR, FMath::Clamp(Intensity, 0.f, 1.f));
	}

	/** Peso del efecto visual con el ajuste de accesibilidad «Efectos del clima» (Strength en 0..1, nunca menos de MIN_VISUAL). */
	inline float VisualWeight(float Intensity, float Strength)
	{
		return FMath::Clamp(Intensity, 0.f, 1.f) * FMath::Clamp(Strength, MIN_VISUAL, 1.f);
	}
}
