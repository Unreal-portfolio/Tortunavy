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
#include "Game/TN_BeachRaceGameState.h"
#include "Game/TN_TctItems.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_RaceItems.h"
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

bool TNCoopItems::RollModeLoot(const APawn* Picker, const UDataTable* Catalog, TFunctionRef<float(FName, const FTN_InventoryItem&)> CatalogWeight,
	FTN_InventoryItem& OutItem)
{
	const UWorld* World = Picker ? Picker->GetWorld() : nullptr;
	if (World && World->GetGameState<ATN_BeachRaceGameState>())
	{
		return TNRaceItems::RollLoot(Picker, ETNRaceLootSource::Search, Catalog, OutItem);
	}
	return RollLoot(Catalog, CatalogWeight, FMath::FRand(), OutItem);
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
	if (!TNRaceItems::CanUseNow(Turtle) || !TNCoopItemsDetail::UseKind(Turtle, Kind, Item))
	{
		TNTctItems::PlayCue(Turtle, ETNRaceSound::Nope);
		UE_LOG(LogTortunabo, Log, TEXT("[Coop] %s no puede usar %s ahora."), *GetNameSafe(Turtle), TNCoopItemRules::Spec(Kind).Code);
		return;
	}
	TNCoopItemsDetail::SpendOne(Turtle, Item);
	Turtle->MulticastItemThrowAnim();
	UE_LOG(LogTortunabo, Log, TEXT("[Coop] %s usa %s."), *GetNameSafe(Turtle), TNCoopItemRules::Spec(Kind).Code);
}
