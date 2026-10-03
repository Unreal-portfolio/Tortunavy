#pragma once

#include "CoreMinimal.h"

/**
 * Lógica pura del nivel de detalle de los enemigos numerosos de la playa (ATN_BeachEnemy con bThrottleWhenFar: cangrejos,
 * tanques, erizos, lagartos...): cada cuánto se actualiza cada uno según lo cerca que estén las tortugas y la cámara, y el
 * tope de cuántos van a ritmo completo a la vez. Sin mundo ni actores, para que Tortunabo.Beach.EnemyLod cubra la regla real.
 *
 *  - Ritmo completo (Full): una tortuga en su radio de actividad (el servidor lo mueve y ataca) o la cámara muy cerca.
 *  - Cerca (Near, 30 Hz): visible a media distancia, o activo pero fuera del tope de ritmo completo.
 *  - Lejos y visible (FarSeen, 15 Hz) y fuera de la vista (Hidden, 4 Hz).
 *  - Tope: solo los MAX_FULL_RATE más cercanos (por la menor distancia a una tortuga o a la cámara) van a ritmo completo;
 *    el resto de los que lo pedirían baja a Near. Con 200 enemigos alrededor, el coste deja de crecer con el número.
 */
namespace TNBeachEnemyLod
{
	enum class ETier : uint8
	{
		Full,
		Near,
		FarSeen,
		Hidden
	};

	/** Con la cámara a menos de esto (cm) va a ritmo completo aunque no haya tortugas en su radio de actividad. */
	constexpr float FULL_RATE_VIEW_DISTANCE = 4000.f;

	/** Enemigos numerosos que pueden ir a ritmo completo a la vez en un mundo (los más cercanos). */
	constexpr int32 MAX_FULL_RATE = 24;

	constexpr float NEAR_INTERVAL = 1.f / 30.f;
	constexpr float FAR_SEEN_INTERVAL = 1.f / 15.f;
	constexpr float HIDDEN_INTERVAL = 0.25f;

	struct FInput
	{
		/** Servidor: una tortuga dentro de su radio de actividad. */
		bool bActive = false;
		/** Esta máquina dibuja (no es un servidor dedicado). */
		bool bHasScreen = false;
		/** Distancia (cm) a la cámara local más cercana. */
		float ViewDistance = 1.0e9f;
		/** Radio de relevancia visual del enemigo (cm): más lejos no se anima. */
		float VisualRange = 30000.f;
	};

	/** Nivel sin tope: lo que pediría este enemigo por sí solo. */
	inline ETier Classify(const FInput& In)
	{
		if (In.bActive)
		{
			return ETier::Full;
		}
		if (!In.bHasScreen || In.ViewDistance >= In.VisualRange)
		{
			return ETier::Hidden;
		}
		if (In.ViewDistance < FULL_RATE_VIEW_DISTANCE)
		{
			return ETier::Full;
		}
		return In.ViewDistance < In.VisualRange * 0.5f ? ETier::Near : ETier::FarSeen;
	}

	/** Aplica el tope: CloserFull es cuántos enemigos del mismo mundo que piden ritmo completo están más cerca que este. */
	inline ETier ApplyBudget(ETier Tier, int32 CloserFull)
	{
		return Tier == ETier::Full && CloserFull >= MAX_FULL_RATE ? ETier::Near : Tier;
	}

	/** Intervalo de Tick del actor (s) para el nivel; 0 = cada fotograma. */
	inline float TickInterval(ETier Tier)
	{
		switch (Tier)
		{
		case ETier::Near:    return NEAR_INTERVAL;
		case ETier::FarSeen: return FAR_SEEN_INTERVAL;
		case ETier::Hidden:  return HIDDEN_INTERVAL;
		case ETier::Full:
		default:             return 0.f;
		}
	}

	/**
	 * Prioridad para el tope (cm, menor = más cerca): la menor de la distancia a la tortuga más cercana (solo cuenta en el
	 * servidor, que es quien lo simula) y la de la cámara local.
	 */
	inline float Priority(bool bServer, float NearestTurtleDistance, bool bHasScreen, float ViewDistance)
	{
		const float Turtle = bServer ? NearestTurtleDistance : 1.0e9f;
		const float View = bHasScreen ? ViewDistance : 1.0e9f;
		return FMath::Min(Turtle, View);
	}
}
