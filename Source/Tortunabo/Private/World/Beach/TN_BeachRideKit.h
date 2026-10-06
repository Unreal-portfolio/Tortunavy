#pragma once

#include "CoreMinimal.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_DizzyBirdsComponent.h"
#include "Player/TN_ShellComponent.h"
#include "World/Beach/TN_BeachStun.h"
#include "TN_BeachTrapKit.h"

/**
 * Kit de las piezas «de montar» de la playa (ATN_BeachClamTrap, ATN_BeachMovingPlatform, ATN_BeachCatapult y
 * ATN_BeachTrampoline): hacia dónde queda el mar, si la carrera está en marcha, quién puede subirse, lanzar a una tortuga
 * como bola de caparazón, el mareo de los pajaritos y el vaivén determinista con el reloj del servidor.
 */
namespace TNBeachRideKit
{
	/**
	 * Tortuga libre para montar o caer en una pieza: viva, fuera del caparazón, sin aturdir (TNBeachTrapKit::IsFreeTurtle),
	 * sin derribar, sin que la lleve nadie y sin llevar a nadie.
	 */
	inline bool IsFreeRider(const ACharacter* Character)
	{
		if (!TNBeachTrapKit::IsFreeTurtle(Character))
		{
			return false;
		}
		const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Character);
		if (!Turtle || Turtle->IsKnockedDown())
		{
			return false;
		}
		const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
		return !(Carry && (Carry->GetCarrier() != nullptr || Carry->IsCarrying()));
	}

	/**
	 * Servidor: lanza a la tortuga como bola de caparazón con física (vuela, rebota y rueda; la caja se replica sola, sin
	 * predicción del movimiento) y sale sola del caparazón cuando la bola se para. Suelta antes a quien lleve. No aturde.
	 */
	inline bool LaunchAsBall(ATortugaCharacter* Turtle, const FVector& Velocity)
	{
		// Ni a la que recoloca la tormenta o la red de seguridad (lo suyo manda: nada la relanza en cadena).
		if (!Turtle || !Turtle->HasAuthority() || Turtle->IsDead() || Turtle->IsKnockedDown() || TNBeach::IsTurtleRelocating(Turtle))
		{
			return false;
		}
		UTN_ShellComponent* Shell = Turtle->GetShellComponent();
		if (!Shell)
		{
			return false;
		}
		if (UTN_CarryComponent* Carry = Turtle->GetCarryComponent())
		{
			if (Carry->GetCarrier() != nullptr)
			{
				return false;
			}
			if (Carry->IsCarrying())
			{
				Carry->ForceRelease(false);
			}
		}
		if (!Shell->IsInShell())
		{
			// Sin cuerpo aquí: StartBody lo crea tumbado donde está el actor, con la velocidad pedida.
			Shell->ForceEnterShell(false, false);
		}
		// Lanzada: no se sale en el aire; ForceExitShell (al pararse la bola o caer al agua) la desbloquea.
		Shell->SetExitLocked(true);
		Shell->StartBody(Velocity, true, true);
		if (!Shell->GetBody())
		{
			// Sin caja (no se ha podido crear): no se queda metida en el caparazón, bloqueada y sin moverse.
			Shell->ForceExitShell();
			return false;
		}
		return true;
	}

	/**
	 * Pajaritos del mareo en esta máquina (cosmético). Al apagarlos, los deja si la tortuga sigue aturdida o derribada:
	 * esos estados también los encienden.
	 */
	inline void SetDizzyBirds(ACharacter* Character, bool bOn)
	{
		UTN_DizzyBirdsComponent* Birds = Character ? Character->FindComponentByClass<UTN_DizzyBirdsComponent>() : nullptr;
		if (!Birds)
		{
			return;
		}
		if (!bOn)
		{
			const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Character);
			if (TNBeach::IsTurtleStunned(Character) || (Turtle && Turtle->IsKnockedDown()))
			{
				return;
			}
		}
		Birds->SetDizzy(bOn);
	}

	/** Media vuelta suave de 0 a 1 (arranca y frena sin tirones). */
	inline double EaseInOut(double U)
	{
		const double C = FMath::Clamp(U, 0.0, 1.0);
		return 0.5 - 0.5 * FMath::Cos(C * TNPlaygroundKit::KitPi);
	}

	/**
	 * Vaivén determinista (0 = un extremo, 1 = el otro) a la hora Time: espera DwellA en 0, va en Travel, espera DwellB en
	 * 1 y vuelve en Travel. Mismo resultado en todas las máquinas con la misma hora del servidor.
	 */
	inline double ShuttleAlpha(double Time, double DwellA, double Travel, double DwellB)
	{
		const double Leg = FMath::Max(0.05, Travel);
		const double Period = FMath::Max(0.1, DwellA + DwellB + 2.0 * Leg);
		double Phase = FMath::Fmod(Time, Period);
		if (Phase < 0.0)
		{
			Phase += Period;
		}
		if (Phase < DwellA)
		{
			return 0.0;
		}
		Phase -= DwellA;
		if (Phase < Leg)
		{
			return EaseInOut(Phase / Leg);
		}
		Phase -= Leg;
		if (Phase < DwellB)
		{
			return 1.0;
		}
		Phase -= DwellB;
		return 1.0 - EaseInOut(Phase / Leg);
	}

	/** Pies de un personaje en el espacio de Xf (centro de la cápsula menos su semialtura). */
	inline FVector FeetIn(const FTransform& Xf, const ACharacter* Character)
	{
		const FVector Center = Xf.InverseTransformPosition(Character->GetActorLocation());
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		return Center - FVector(0.0, 0.0, Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0);
	}
}
