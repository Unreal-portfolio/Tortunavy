// Lo que comparten en partida los objetos que se usan. Ver TN_ItemRuntime.h.

#include "Game/TN_ItemRuntime.h"
#include "Game/TN_CoopItemComponent.h"
#include "Game/TN_CoopItems.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/TN_CatalogItemVisuals.h"

double TNItemRuntime::ServerNow(const UWorld* World)
{
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

bool TNItemRuntime::IsInvulnerable(const AActor* Turtle)
{
	return UTN_CoopItemComponent::IsTurtleProtected(Turtle);
}

bool TNItemRuntime::CanBeHurt(const ATortugaCharacter* Turtle)
{
	return IsValid(Turtle) && !IsInvulnerable(Turtle);
}

bool TNItemRuntime::CanUseNow(const ATortugaCharacter* Turtle)
{
	if (!IsValid(Turtle) || Turtle->IsDead() || Turtle->IsKnockedDown() || Turtle->IsInShell())
	{
		return false;
	}
	if (TNBeach::IsTurtleStunned(Turtle) || ATN_BeachEnemy::IsTurtleHeld(Turtle)
		|| TNBeach::IsTurtleRelocating(Turtle))
	{
		return false;
	}
	const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
	return !(Carry && (Carry->IsCarrying() || Carry->IsBeingCarried()));
}

bool TNItemRuntime::CanAffect(const ATortugaCharacter* Turtle, bool bPush)
{
	if (!IsValid(Turtle) || Turtle->IsDead() || !CanBeHurt(Turtle))
	{
		return false;
	}
	if (bPush && Turtle->IsInShell())
	{
		return false;
	}
	const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
	return !(Carry && Carry->IsBeingCarried());
}

void TNItemRuntime::ResolveVisuals(FTN_InventoryItem& Item)
{
	// Los objetos de siempre de DT_Items: icono (y malla, si la fila trae una del motor) dibujados en código (#787).
	if (TNCatalogItemVisuals::ResolveVisuals(Item))
	{
		return;
	}
	// Los del cooperativo se definen en código (TN_CoopItems.h).
	if (Item.UseType == ETN_ItemUseType::CoopItem)
	{
		TNCoopItems::ResolveVisuals(Item);
	}
}

void TNItemRuntime::PlayCue(ACharacter* Turtle, ETNRaceSound Sound, float Pitch)
{
	if (UTN_CoopItemComponent* Comp = UTN_CoopItemComponent::FindOrAddOn(Turtle))
	{
		Comp->MulticastCue(Sound, Pitch);
	}
}

void TNItemRuntime::ShowHarpoonRope(ACharacter* Turtle, const FVector& From, const FVector& To)
{
	if (UTN_CoopItemComponent* Comp = UTN_CoopItemComponent::FindOrAddOn(Turtle))
	{
		Comp->MulticastRope(From, To);
	}
}
