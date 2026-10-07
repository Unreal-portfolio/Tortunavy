#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/TN_InventoryTypes.h"
#include "TN_VendingStock.generated.h"

/**
 * Un objeto a la venta en una máquina expendedora (ATN_VendingMachine) y su precio en chapas (plan maestro §4, hoja Economía).
 */
USTRUCT(BlueprintType)
struct TORTUNABO_API FTNVendingOffer
{
	GENERATED_BODY()

	/**
	 * Qué se vende: el código de un objeto del cooperativo («Harpoon», «PufferFish»...; ver TN.Coop.Item list) o una fila de
	 * DT_Items («StaminaBoost», «Totem»...). Los objetos nuevos del Excel (#846, #847) se añaden aquí con su código en cuanto
	 * existan.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Máquina")
	FName ItemId = NAME_None;

	/** Precio en chapas. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Máquina", meta = (ClampMin = "0"))
	int32 Price = 1;

	/** Nombre en la pantalla de la máquina. Vacío = el del objeto (TNVending::OfferName). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Máquina")
	FText DisplayName;
};

/**
 * Los objetos de una máquina expendedora concreta (4 según la hoja Economía) con sus precios. Se asigna por instancia en
 * ATN_VendingMachine::Stock; sin asignar, la máquina vende la lista por defecto de UTN_EconomySettings
 * (Config/DefaultGame.ini).
 */
UCLASS(BlueprintType)
class TORTUNABO_API UTN_VendingStockData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Máquina")
	TArray<FTNVendingOffer> Offers;
};

/** Lo que la máquina necesita saber de lo que vende. */
namespace TNVending
{
	/** Objetos a la venta en cada máquina (hoja Economía). */
	inline constexpr int32 OffersPerMachine = 4;

	/**
	 * La fila del inventario de ItemId: un objeto del cooperativo con su cuenta inicial o la fila de DT_Items con ese nombre.
	 * false si no es ninguno de los dos.
	 */
	TORTUNABO_API bool ResolveOfferItem(FName ItemId, FTN_InventoryItem& OutItem);

	/** Nombre que se ve del objeto en venta (localizable). */
	TORTUNABO_API FText OfferName(const FTNVendingOffer& Offer);

	/** La lista que vende una máquina con Stock (o, si es nulo o está vacío, la lista por defecto de la economía). */
	TORTUNABO_API const TArray<FTNVendingOffer>& OffersOf(const UTN_VendingStockData* Stock);
}
