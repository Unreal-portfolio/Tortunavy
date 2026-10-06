// Aspecto de los objetos de siempre (DT_Items) construido en ejecución (#787). El dibujo está en TN_CatalogItemArt.cpp.

#include "World/TN_CatalogItemVisuals.h"
#include "TN_CatalogItemArt.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "UObject/Package.h"

namespace TNCatalogItemVisualsDetail
{
	/** ItemId de la fila de relleno sin uso. */
	const FName ScoreItemId(TEXT("Score"));

	bool Apply(FTN_InventoryItem& Item, bool bEvenHeadless)
	{
		const ETNCatalogLook Look = TNCatalogItemVisuals::LookOf(Item);
		if (Look == ETNCatalogLook::None)
		{
			return false;
		}
		if (UTexture2D* Icon = TNCatalogItemArt::GetIcon(Look, bEvenHeadless))
		{
			Item.ItemIcon = Icon;
		}
		TNCatalogItemArt::FHeldLook Held;
		if (TNCatalogItemVisuals::ShouldReplaceMesh(Item) && TNCatalogItemArt::GetHeldLook(Look, Held, bEvenHeadless) && Held.Mesh)
		{
			// La escala de la fila era la de la forma del motor (100 cm): la malla de código ya tiene su tamaño.
			Item.EquippedMesh = Held.Mesh;
			Item.EquippedMeshScale = Held.Scale;
			Item.EquippedMeshRotation = Held.Rotation;
		}
		return true;
	}
}

ETNCatalogLook TNCatalogItemVisuals::LookOf(const FTN_InventoryItem& Item)
{
	switch (Item.UseType)
	{
	case ETN_ItemUseType::SelfStaminaBoost: return ETNCatalogLook::StaminaBoost;
	case ETN_ItemUseType::SelfStaminaFull:  return ETNCatalogLook::StaminaFull;
	case ETN_ItemUseType::Throwable:        return ETNCatalogLook::Ball;
	case ETN_ItemUseType::BigHead:          return ETNCatalogLook::BigHead;
	case ETN_ItemUseType::Conch:            return ETNCatalogLook::Conch;
	case ETN_ItemUseType::InkThrower:       return ETNCatalogLook::Ink;
	case ETN_ItemUseType::Totem:            return ETNCatalogLook::Totem;
	case ETN_ItemUseType::None:
		return Item.ItemId == TNCatalogItemVisualsDetail::ScoreItemId ? ETNCatalogLook::Score : ETNCatalogLook::None;
	default:
		// CoopItem: lo resuelve TNCoopItems.
		return ETNCatalogLook::None;
	}
}

const TCHAR* TNCatalogItemVisuals::CodeName(ETNCatalogLook Look)
{
	switch (Look)
	{
	case ETNCatalogLook::StaminaBoost: return TEXT("StaminaBoost");
	case ETNCatalogLook::StaminaFull:  return TEXT("StaminaFull");
	case ETNCatalogLook::Ball:         return TEXT("Ball");
	case ETNCatalogLook::BigHead:      return TEXT("BigHead");
	case ETNCatalogLook::Conch:        return TEXT("Conch");
	case ETNCatalogLook::Ink:          return TEXT("Ink");
	case ETNCatalogLook::Totem:        return TEXT("Totem");
	case ETNCatalogLook::Score:        return TEXT("Score");
	default:                           return TEXT("None");
	}
}

bool TNCatalogItemVisuals::IsEnginePlaceholder(const UObject* Asset)
{
	if (!Asset || Asset->GetOutermost() == GetTransientPackage())
	{
		return false;
	}
	const FString Path = Asset->GetPathName();
	return Path.StartsWith(TEXT("/Engine/")) || Path.StartsWith(TEXT("/VREditor/"));
}

bool TNCatalogItemVisuals::ShouldReplaceMesh(const FTN_InventoryItem& Item)
{
	// El lanzable manda su malla al resto de máquinas por un multicast (ATN_ThrowableItemActor): solo valen las de disco.
	if (Item.UseType == ETN_ItemUseType::Throwable)
	{
		return false;
	}
	return !Item.EquippedMesh || IsEnginePlaceholder(Item.EquippedMesh);
}

bool TNCatalogItemVisuals::ResolveVisuals(FTN_InventoryItem& Item)
{
	return TNCatalogItemVisualsDetail::Apply(Item, false);
}

bool TNCatalogItemVisuals::ResolveVisualsHeadless(FTN_InventoryItem& Item)
{
	return TNCatalogItemVisualsDetail::Apply(Item, true);
}
