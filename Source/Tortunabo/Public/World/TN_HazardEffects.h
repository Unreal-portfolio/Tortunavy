#pragma once

#include "CoreMinimal.h"
#include "Player/TN_VitalsRules.h"
#include "Settings/TN_HazardTuning.h"
#include "World/Beach/TN_BeachCreatureRules.h"

class AActor;
class ACharacter;

/**
 * Aplicar el efecto de un enemigo u obstáculo del Excel (#871) a una tortuga: daño, veneno o muerte, con los valores de
 * UTN_HazardTuning. Resolve es la cuenta pura (la prueban Tortunabo.Hazards.*); Apply la hace en el servidor sobre
 * UTN_VitalsComponent o pide la muerte (ATortugaCharacter::RequestKillBy). Implementación: Private/World/TN_HazardEffects.cpp.
 */
namespace TNHazard
{
	/** Lo que le pasa a la tortuga con un efecto. */
	struct FOutcome
	{
		TNVitals::FState State;
		/** Muere: el efecto mata sin más o deja la vida a cero. */
		bool bDies = false;
	};

	/** Cuenta pura: el estado de los vitales tras el efecto y si muere. */
	inline FOutcome Resolve(const FTNHazardEffect& Effect, const TNVitals::FState& Vitals)
	{
		FOutcome Out;
		if (Effect.bKills)
		{
			Out.State = TNVitals::Damage(Vitals, Vitals.Health);
			Out.bDies = true;
			return Out;
		}
		Out.State = TNVitals::Poison(TNVitals::Damage(Vitals, Effect.Damage), Effect.PoisonDamagePerSecond, Effect.PoisonSeconds);
		Out.bDies = Out.State.IsDepleted();
		return Out;
	}

	/**
	 * Gaviota 1: la caída desde la altura a la que la suelta mata si es al menos FatalHeight (cm). Si se escurre pronto, cerca
	 * del suelo, solo cae aturdida.
	 */
	inline bool GullDropKills(float DropHeight, float FatalHeight, bool bEffectKills)
	{
		return bEffectKills && DropHeight >= FatalHeight;
	}

	/**
	 * Arenas movedizas: al acabar el tiempo atrapada sin soltarse machacando salto, muere. Si se suelta, sale como siempre.
	 */
	inline bool QuicksandKills(bool bShouldRelease, bool bEscaped, bool bEffectKills)
	{
		return bEffectKills && bShouldRelease && !bEscaped;
	}

	/** Cangrejo 2: el arrastre se completa (hasta el final o hasta el borde de la zona de muerte); soltarse o marearlo, no. */
	inline bool DragCompletes(TNBeachCreatureRules::DragCrab::EDragEnd End)
	{
		using TNBeachCreatureRules::DragCrab::EDragEnd;
		return End == EDragEnd::Distance || End == EDragEnd::Unsafe;
	}

	/** Algas: con Hits golpes ya están cortadas (HitsToCut, como mínimo uno). */
	inline bool SeaweedCut(int32 Hits, int32 HitsToCut)
	{
		return Hits >= FMath::Max(1, HitsToCut);
	}

	/** Servidor: aplica Effect a la tortuga Turtle de parte de Source. Nada en clientes ni con tortugas que no lo son. */
	TORTUNABO_API void Apply(const FTNHazardEffect& Effect, ACharacter* Turtle, AActor* Source);

	/** Servidor: cura Amount de vida (el anélido). */
	TORTUNABO_API void Heal(ACharacter* Turtle, float Amount);
}
