// Objetos del coop en el juego: catálogo (filas del inventario, mallas e iconos), apilado, tabla de botín y uso. Ver
// TN_CoopItems.h.

#include "Game/TN_CoopItems.h"
#include "TN_CoopItemArt.h"
#include "Game/TN_CoopItemComponent.h"
#include "Core/TN_Log.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Game/TN_ItemRuntime.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "TimerManager.h"
#include "World/TN_FishingPool.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/TN_CoopThrownItem.h"
#include "World/TN_PickupInteractableBase.h"

namespace TNCoopItemsDetail
{
	/** Gasta una unidad o un uso de Item, el equipado: el mismo con uno menos o, si era el último, fuera de la mano. */
	void SpendOne(ATortugaCharacter* Turtle, const FTN_InventoryItem& Item)
	{
		UTN_InventoryComponent* Inventory = Turtle ? Turtle->GetInventoryComponent() : nullptr;
		if (!Inventory)
		{
			return;
		}
		const FName Next = TNCoopItemRules::ItemIdAfterUse(Item.ItemId);
		if (Next.IsNone())
		{
			FTN_InventoryItem Consumed;
			Inventory->TryConsumeEquippedItem(Consumed);
			return;
		}
		FTN_InventoryItem Remaining = Item;
		Remaining.ItemId = Next;
		Remaining.ItemIcon = nullptr;
		Inventory->TryReplaceEquippedItem(Remaining);
	}

	/** Tirón del rescate: Target va hacia Shooter (si ya no está o se ha muerto, nada). */
	void PullTowards(ATortugaCharacter* Target, const ATortugaCharacter* Shooter)
	{
		if (IsValid(Target) && IsValid(Shooter) && !Target->IsDead())
		{
			UTN_TurtleMovementComponent::LaunchFromServer(Target, TNCoopItemRules::RescuePull(Target->GetActorLocation(), Shooter->GetActorLocation()));
		}
	}

	/** El arpón rescata a Target: si está derribada la levanta y, un momento después, la trae; si está en el agua, la trae ya. */
	bool RescueTurtle(ATortugaCharacter* Shooter, ATortugaCharacter* Target)
	{
		if (Target->IsKnockedDown())
		{
			Target->RecoverFromKnockdown();
			// El tirón, cuando ya vuelve a moverse (el dueño estrena el lanzamiento en su movimiento: sin corrección).
			const TWeakObjectPtr<ATortugaCharacter> WeakTarget(Target);
			const TWeakObjectPtr<ATortugaCharacter> WeakShooter(Shooter);
			FTimerHandle Pull;
			Target->GetWorldTimerManager().SetTimer(Pull, FTimerDelegate::CreateWeakLambda(Target, [WeakTarget, WeakShooter]()
			{
				PullTowards(WeakTarget.Get(), WeakShooter.Get());
			}), TNCoopItemTuning::HarpoonRecoverDelay, false);
		}
		else
		{
			PullTowards(Target, Shooter);
		}
		UE_LOG(LogTortunabo, Log, TEXT("[Coop] %s rescata con el arpón a %s."), *GetNameSafe(Shooter), *GetNameSafe(Target));
		return true;
	}

	/** Si Other se puede rescatar ahora con el arpón de Shooter. */
	bool IsRescuable(const ATortugaCharacter* Shooter, const ATortugaCharacter* Other)
	{
		const UCharacterMovementComponent* Move = Other->GetCharacterMovement();
		const UTN_CarryComponent* Carry = Other->GetCarryComponent();
		if (Carry && Carry->IsBeingCarried())
		{
			return false;
		}
		return TNCoopItemRules::CanRescue(Other == Shooter, Other->IsDead(), Other->IsKnockedDown(), Move && Move->IsSwimming());
	}

	/**
	 * Arpón (servidor): disparo de HarpoonRange hacia la mira que da a lo primero que puede: una compañera que rescatar, un
	 * objeto suelto (a la mochila) o un charco que pescar. Sin nada a lo que dar, el cable se ve pero no gasta el uso.
	 */
	bool UseHarpoon(ATortugaCharacter* Turtle)
	{
		UWorld* World = Turtle->GetWorld();
		FRotator Aim = Turtle->GetTurtleAimRotation();
		Aim.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Aim.Pitch), -30.f, 25.f);
		Aim.Roll = 0.f;
		const FVector Dir = Aim.Vector();
		const FVector Start = Turtle->GetActorLocation() + FVector(0.0, 0.0, 35.0) + FVector(Dir.X, Dir.Y, 0.0).GetSafeNormal() * 45.0;
		float Range = TNCoopItemTuning::HarpoonRange;
		FHitResult WorldHit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CoopHarpoon), false, Turtle);
		if (World && World->LineTraceSingleByObjectType(WorldHit, Start, Start + Dir * Range, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			// Lo que está en el suelo donde da (un objeto, un charco) también cuenta.
			Range = FMath::Min(Range, static_cast<float>(WorldHit.Distance) + TNCoopItemTuning::PoolRadius);
		}

		TArray<FTNHarpoonCandidate> Candidates;
		TArray<AActor*> Actors;
		const auto Consider = [&](AActor* Actor, ETNHarpoonTarget Type, float ExtraRadius, bool bValid)
		{
			float Along = 0.f;
			if (TNCoopItemRules::IsInShot(Start, Dir, Range, Actor->GetActorLocation(), ExtraRadius, Along))
			{
				Candidates.Add({ Type, Along, bValid });
				Actors.Add(Actor);
			}
		};
		for (TActorIterator<ATortugaCharacter> It(World); It; ++It)
		{
			if (*It != Turtle)
			{
				Consider(*It, ETNHarpoonTarget::Rescue, It->GetSimpleCollisionRadius(), IsRescuable(Turtle, *It));
			}
		}
		for (TActorIterator<ATN_PickupInteractableBase> It(World); It; ++It)
		{
			// Un arpón no pesca otro arpón (al cogerlo recargaría el que se está usando).
			const bool bHarpoon = TNCoopItems::KindOf(It->GetPickupItem()) == ETNCoopItem::Harpoon;
			Consider(*It, ETNHarpoonTarget::Pickup, 30.f, !It->IsTaken() && !bHarpoon && It->CanInteract(Turtle));
		}
		for (TActorIterator<ATN_FishingPool> It(World); It; ++It)
		{
			Consider(*It, ETNHarpoonTarget::Pool, TNCoopItemTuning::PoolRadius, It->CanFishNow());
		}

		const int32 Pick = TNCoopItemRules::PickHarpoonTarget(Candidates);
		FVector RopeEnd = Start + Dir * Range;
		bool bDone = false;
		if (Pick != INDEX_NONE)
		{
			AActor* Target = Actors[Pick];
			RopeEnd = Target->GetActorLocation();
			switch (Candidates[Pick].Type)
			{
			case ETNHarpoonTarget::Rescue:
				bDone = RescueTurtle(Turtle, CastChecked<ATortugaCharacter>(Target));
				break;
			case ETNHarpoonTarget::Pickup:
			{
				ATN_PickupInteractableBase* Pickup = CastChecked<ATN_PickupInteractableBase>(Target);
				Pickup->Interact(Turtle);
				bDone = Pickup->IsTaken();
				break;
			}
			case ETNHarpoonTarget::Pool:
				bDone = CastChecked<ATN_FishingPool>(Target)->ServerHarpoonCatch(Turtle);
				break;
			default:
				break;
			}
		}
		// El cable del arpón (cosmético, en todas las máquinas).
		TNItemRuntime::ShowHarpoonRope(Turtle, Start, RopeEnd);
		if (bDone)
		{
			TNItemRuntime::PlayCue(Turtle, ETNRaceSound::Catch, 0.9f);
		}
		return bDone;
	}

	/** Lo que hace Kind al usarlo (servidor). false si ahora no se puede (el objeto se queda). */
	bool UseKind(ATortugaCharacter* Turtle, ETNCoopItem Kind, const FTN_InventoryItem& Item)
	{
		switch (Kind)
		{
		case ETNCoopItem::PufferFish:
		{
			// No se apila: con la protección del anterior aún puesta, el «nop» y se queda en la mano.
			UTN_CoopItemComponent* Effects = UTN_CoopItemComponent::FindOrAddOn(Turtle);
			return Effects && Effects->GrantPuffer();
		}
		case ETNCoopItem::Harpoon:
			return UseHarpoon(Turtle);
		case ETNCoopItem::StunShell:
			if (!ATN_CoopThrownItem::ServerThrowShell(Turtle))
			{
				return false;
			}
			TNItemRuntime::PlayCue(Turtle, ETNRaceSound::Throw, 1.3f);
			return true;
		case ETNCoopItem::SlipperyPeel:
			if (!ATN_CoopThrownItem::ServerThrow(Turtle, static_cast<uint8>(Kind), TNCoopItemTuning::PeelRange))
			{
				return false;
			}
			TNItemRuntime::PlayCue(Turtle, ETNRaceSound::Throw, 1.1f);
			return true;
		case ETNCoopItem::None:
		default:
			return false;
		}
	}
}

FText TNCoopItems::DisplayName(ETNCoopItem Kind)
{
	switch (Kind)
	{
	case ETNCoopItem::PufferFish: return NSLOCTEXT("TNCoop", "ItemPufferFish", "Pez globo");
	case ETNCoopItem::SlipperyPeel: return NSLOCTEXT("TNCoop", "ItemSlipperyPeel", "Cáscara resbaladiza");
	case ETNCoopItem::StunShell: return NSLOCTEXT("TNCoop", "ItemStunShell", "Concha");
	case ETNCoopItem::Harpoon: return NSLOCTEXT("TNCoop", "ItemHarpoon", "Arpón");
	case ETNCoopItem::None:
	default:
		return NSLOCTEXT("TNRace", "ItemUnknown", "Objeto");
	}
}

ETNCoopItem TNCoopItems::KindOf(const FTN_InventoryItem& Item)
{
	ETNCoopItem Kind = ETNCoopItem::None;
	int32 Count = 0;
	return (Item.UseType == ETN_ItemUseType::CoopItem && TNCoopItemRules::ParseItemId(Item.ItemId, Kind, Count)) ? Kind : ETNCoopItem::None;
}

int32 TNCoopItems::CountOf(const FTN_InventoryItem& Item)
{
	ETNCoopItem Kind = ETNCoopItem::None;
	int32 Count = 0;
	return (Item.UseType == ETN_ItemUseType::CoopItem && TNCoopItemRules::ParseItemId(Item.ItemId, Kind, Count)) ? Count : 0;
}

bool TNCoopItems::IsAimed(ETNCoopItem Kind)
{
	return Kind == ETNCoopItem::SlipperyPeel || Kind == ETNCoopItem::StunShell || Kind == ETNCoopItem::Harpoon;
}

bool TNCoopItems::ParseKind(const FString& Text, ETNCoopItem& OutKind)
{
	const FString Wanted = Text.TrimStartAndEnd();
	if (Wanted.IsEmpty())
	{
		return false;
	}
	if (Wanted.IsNumeric())
	{
		const int32 Value = FCString::Atoi(*Wanted);
		if (Value > static_cast<int32>(ETNCoopItem::None) && Value < static_cast<int32>(ETNCoopItem::Count))
		{
			OutKind = static_cast<ETNCoopItem>(Value);
			return true;
		}
		return false;
	}
	for (const ETNCoopItem Kind : TNCoopItemRules::AllKinds())
	{
		if (Wanted.Equals(TNCoopItemRules::Spec(Kind).Code, ESearchCase::IgnoreCase))
		{
			OutKind = Kind;
			return true;
		}
	}
	return false;
}

FTN_InventoryItem TNCoopItems::MakeItem(ETNCoopItem Kind, int32 Count)
{
	FTN_InventoryItem Out;
	const int32 Wanted = Count > 0 ? Count : TNCoopItemRules::InitialCount(Kind);
	Out.ItemId = TNCoopItemRules::MakeItemId(Kind, Wanted);
	if (Out.ItemId.IsNone())
	{
		return FTN_InventoryItem();
	}
	Out.UseType = ETN_ItemUseType::CoopItem;
	Out.ItemWeight = 0.f;
	Out.PickupActorClass = ATN_PickupInteractableBase::StaticClass();
	ResolveVisuals(Out);
	return Out;
}

void TNCoopItems::ResolveVisuals(FTN_InventoryItem& Item)
{
	const ETNCoopItem Kind = KindOf(Item);
	if (Kind == ETNCoopItem::None)
	{
		return;
	}
	TNCoopItemArt::FHeldLook Look;
	if (TNCoopItemArt::GetHeldLook(Kind, Look))
	{
		Item.EquippedMesh = Look.Mesh;
		Item.EquippedMeshScale = Look.Scale;
		Item.EquippedMeshRotation = Look.Rotation;
	}
	if (UTexture2D* Icon = TNCoopItemArt::GetIcon(Kind, CountOf(Item)))
	{
		Item.ItemIcon = Icon;
	}
}

ETNCoopStack TNCoopItems::DecideStack(const FTN_InventoryItem& Held, const FTN_InventoryItem& Incoming, FTN_InventoryItem& OutMerged)
{
	int32 Merged = 0;
	const ETNCoopStack Decision = TNCoopItemRules::DecideStack(KindOf(Held), CountOf(Held), KindOf(Incoming), CountOf(Incoming), Merged);
	if (Decision == ETNCoopStack::Merge)
	{
		OutMerged = Held;
		OutMerged.ItemId = TNCoopItemRules::MakeItemId(KindOf(Held), Merged);
		OutMerged.ItemIcon = nullptr;
	}
	return Decision;
}

bool TNCoopItems::GiveItem(ATortugaCharacter* Turtle, ETNCoopItem Kind)
{
	if (!Turtle || !Turtle->HasAuthority())
	{
		return false;
	}
	const FTN_InventoryItem Item = MakeItem(Kind);
	UTN_InventoryComponent* Inventory = Turtle->GetInventoryComponent();
	return Item.IsValid() && Inventory && Inventory->TryAddOrReplaceEquipped(Item, true);
}

bool TNCoopItems::RollLoot(const UDataTable* Catalog, TFunctionRef<float(FName, const FTN_InventoryItem&)> CatalogWeight, float Roll,
	FTN_InventoryItem& OutItem)
{
	// Las filas de DT_Items que se pueden recoger y usar (como ATN_ProcSearchSpot::PickCatalogItem), con su peso del coop.
	TArray<const FTN_InventoryItem*> Rows;
	TArray<float> Weights;
	if (Catalog && Catalog->GetRowStruct() && Catalog->GetRowStruct()->IsChildOf(FTN_InventoryItem::StaticStruct()))
	{
		Catalog->ForeachRow<FTN_InventoryItem>(TEXT("TNCoopItems::RollLoot"), [&](const FName& RowName, const FTN_InventoryItem& Row)
		{
			if (!Row.IsValid() || !Row.PickupActorClass)
			{
				return;
			}
			const float Weight = TNCoopItemRules::CatalogWeight(Row.UseType, CatalogWeight(RowName, Row));
			if (Weight <= 0.f)
			{
				return;
			}
			Rows.Add(&Row);
			Weights.Add(Weight);
		});
	}
	const FTNCoopLootPick Pick = TNCoopItemRules::PickLoot(Weights, Roll);
	if (Pick.CatalogIndex != INDEX_NONE)
	{
		OutItem = *Rows[Pick.CatalogIndex];
		return true;
	}
	if (Pick.Kind != ETNCoopItem::None)
	{
		OutItem = MakeItem(Pick.Kind);
		return OutItem.IsValid();
	}
	return false;
}

void TNCoopItems::ServerUse(ATortugaCharacter* Turtle, const FTN_InventoryItem& Item)
{
	if (!Turtle || !Turtle->HasAuthority())
	{
		return;
	}
	const ETNCoopItem Kind = KindOf(Item);
	if (Kind == ETNCoopItem::None || !Turtle->GetInventoryComponent())
	{
		return;
	}
	if (!TNItemRuntime::CanUseNow(Turtle) || !TNCoopItemsDetail::UseKind(Turtle, Kind, Item))
	{
		TNItemRuntime::PlayCue(Turtle, ETNRaceSound::Nope);
		UE_LOG(LogTortunabo, Log, TEXT("[Coop] %s no puede usar %s ahora."), *GetNameSafe(Turtle), TNCoopItemRules::Spec(Kind).Code);
		return;
	}
	TNCoopItemsDetail::SpendOne(Turtle, Item);
	Turtle->MulticastItemThrowAnim();
	UE_LOG(LogTortunabo, Log, TEXT("[Coop] %s usa %s."), *GetNameSafe(Turtle), TNCoopItemRules::Spec(Kind).Code);
}
