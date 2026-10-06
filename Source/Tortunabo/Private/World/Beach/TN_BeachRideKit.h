#pragma once

#include "CoreMinimal.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_DizzyBirdsComponent.h"
#include "World/Beach/TN_BeachStun.h"
#include "TN_BeachTrapKit.h"

/**
 * Kit de las piezas «de montar» de la playa (ATN_BeachClamTrap y ATN_BeachTrampoline): quién puede subirse, el mareo de
 * los pajaritos y dónde quedan los pies de quien monta.
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

	/** Pies de un personaje en el espacio de Xf (centro de la cápsula menos su semialtura). */
	inline FVector FeetIn(const FTransform& Xf, const ACharacter* Character)
	{
		const FVector Center = Xf.InverseTransformPosition(Character->GetActorLocation());
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		return Center - FVector(0.0, 0.0, Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0);
	}
}
