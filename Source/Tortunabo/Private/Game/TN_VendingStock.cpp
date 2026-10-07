// Lo que vende una máquina expendedora (#859). Ver TN_VendingStock.h.

#include "Game/TN_VendingStock.h"
#include "Core/TN_GameplayPreload.h"
#include "Core/TN_LocText.h"
#include "Engine/DataTable.h"
#include "Game/TN_CoopItems.h"
#include "Settings/TN_EconomySettings.h"
#include "World/TN_PickupInteractableBase.h"

namespace TNVendingDetail
{
	/** El objeto del cooperativo de ItemId: su código («Harpoon») o con el prefijo de su ItemId («Coop_Harpoon»). */
	bool ParseCoopKind(FName ItemId, ETNCoopItem& OutKind)
	{
		FString Text = ItemId.ToString();
		Text.RemoveFromStart(TEXT("Coop_"), ESearchCase::IgnoreCase);
		// Sin números: «3» sería el objeto 3 para TNCoopItems::ParseKind, y aquí solo valen códigos.
		return !Text.IsNumeric() && TNCoopItems::ParseKind(Text, OutKind);
	}
}

bool TNVending::ResolveOfferItem(FName ItemId, FTN_InventoryItem& OutItem)
{
	if (ItemId.IsNone())
	{
		return false;
	}
	ETNCoopItem Kind = ETNCoopItem::None;
	if (TNVendingDetail::ParseCoopKind(ItemId, Kind))
	{
		OutItem = TNCoopItems::MakeItem(Kind);
		return OutItem.IsValid();
	}
	const UDataTable* Catalog = TNPreload::ItemCatalog();
	if (!Catalog || !Catalog->GetRowStruct() || !Catalog->GetRowStruct()->IsChildOf(FTN_InventoryItem::StaticStruct()))
	{
		return false;
	}
	const FTN_InventoryItem* Row = Catalog->FindRow<FTN_InventoryItem>(ItemId, TEXT("TNVending::ResolveOfferItem"), false);
	if (!Row || !Row->IsValid() || !Row->PickupActorClass)
	{
		return false;
	}
	OutItem = *Row;
	return true;
}

FText TNVending::OfferName(const FTNVendingOffer& Offer)
{
	if (!Offer.DisplayName.IsEmpty())
	{
		return Offer.DisplayName;
	}
	ETNCoopItem Kind = ETNCoopItem::None;
	if (TNVendingDetail::ParseCoopKind(Offer.ItemId, Kind))
	{
		return TNCoopItems::DisplayName(Kind);
	}
	// Las filas de DT_Items no traen nombre: los del Excel (plan maestro §4).
	if (Offer.ItemId == FName(TEXT("StaminaBoost")))
	{
		return NSLOCTEXT("TNEconomy", "OfferJellyfish", "Medusa");
	}
	if (Offer.ItemId == FName(TEXT("Totem")))
	{
		return NSLOCTEXT("TNEconomy", "OfferTotem", "Tótem tortuga");
	}
	return TNLocText::Literal(Offer.ItemId.ToString());
}

const TArray<FTNVendingOffer>& TNVending::OffersOf(const UTN_VendingStockData* Stock)
{
	if (Stock && Stock->Offers.Num() > 0)
	{
		return Stock->Offers;
	}
	return UTN_EconomySettings::Get().DefaultVendingOffers;
}
