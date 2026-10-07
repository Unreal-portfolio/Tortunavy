#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Core/TN_CosmeticsTypes.h"
#include "TN_MysteryBox.generated.h"

/**
 * Valores de la caja sorpresa de la tienda (#873): precio, peso de cada rareza y lo que se devuelve por una repetida. Viven
 * en el DataAsset UTN_PointsEconomy (DA_PointsEconomy); los de aquí son los de la decisión del 07-10.
 */
USTRUCT(BlueprintType)
struct TORTUNABO_API FTN_MysteryBoxRules
{
	GENERATED_BODY()

	/** Precio de una caja, en puntos. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|MysteryBox", meta = (ClampMin = "0"))
	int32 BoxPrice = 150;

	/** Peso de cada rareza al elegir (no hace falta que sumen 100; una rareza sin filas no sale). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|MysteryBox", meta = (ClampMin = "0.0"))
	float CommonWeight = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|MysteryBox", meta = (ClampMin = "0.0"))
	float RareWeight = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|MysteryBox", meta = (ClampMin = "0.0"))
	float EpicWeight = 5.f;

	/** Tanto por ciento del precio de la caja que se devuelve si sale una que ya tenía. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|MysteryBox", meta = (ClampMin = "0", ClampMax = "100"))
	int32 DuplicateRefundPercent = 50;

	float WeightOf(ETNSkinRarity Rarity) const
	{
		switch (Rarity)
		{
		case ETNSkinRarity::Rare: return FMath::Max(0.f, RareWeight);
		case ETNSkinRarity::Epic: return FMath::Max(0.f, EpicWeight);
		default:                  return FMath::Max(0.f, CommonWeight);
		}
	}
};

/** Resultado de abrir una caja (lo que enseña la animación de la tienda). */
USTRUCT(BlueprintType)
struct TORTUNABO_API FTN_MysteryBoxResult
{
	GENERATED_BODY()

	/** False si no se ha abierto (sin saldo o sin nada que pueda salir): no se ha cobrado nada. */
	UPROPERTY(BlueprintReadOnly, Category = "Shop|MysteryBox")
	bool bOpened = false;

	UPROPERTY(BlueprintReadOnly, Category = "Shop|MysteryBox")
	FName SkinId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Shop|MysteryBox")
	ETNCosmeticCategory Category = ETNCosmeticCategory::Body;

	UPROPERTY(BlueprintReadOnly, Category = "Shop|MysteryBox")
	ETNSkinRarity Rarity = ETNSkinRarity::Common;

	/** Ya la tenía: no se desbloquea nada y se devuelven RefundPoints. */
	UPROPERTY(BlueprintReadOnly, Category = "Shop|MysteryBox")
	bool bDuplicate = false;

	UPROPERTY(BlueprintReadOnly, Category = "Shop|MysteryBox")
	int32 RefundPoints = 0;

	/** Saldo después de pagar la caja (y cobrar la devolución). */
	UPROPERTY(BlueprintReadOnly, Category = "Shop|MysteryBox")
	int32 BalanceAfter = 0;
};

/** Reglas puras de la caja sorpresa (Tortunabo.Shop.MysteryBox): sin mundo ni perfil, con la semilla que se le pase. */
namespace TNMysteryBox
{
	/** Una fila de DT_Skins que puede salir. */
	struct FCandidate
	{
		FName Id = NAME_None;
		ETNCosmeticCategory Category = ETNCosmeticCategory::Body;
		ETNSkinRarity Rarity = ETNSkinRarity::Common;
	};

	inline bool CanAfford(int32 Balance, int32 Price)
	{
		return Price >= 0 && Balance >= Price;
	}

	/** Lo que se devuelve por una repetida: el tanto por ciento del precio, redondeado hacia abajo. */
	inline int32 RefundFor(const FTN_MysteryBoxRules& Rules)
	{
		const int32 Percent = FMath::Clamp(Rules.DuplicateRefundPercent, 0, 100);
		return FMath::Max(0, Rules.BoxPrice) * Percent / 100;
	}

	/**
	 * Índice en Candidates de la que sale: primero la rareza (con su peso, solo entre las que tienen filas) y luego una
	 * fila de esa rareza, todas con la misma probabilidad. INDEX_NONE si no hay ninguna o todos los pesos son 0.
	 */
	inline int32 Roll(TConstArrayView<FCandidate> Candidates, const FTN_MysteryBoxRules& Rules, FRandomStream& Stream)
	{
		constexpr ETNSkinRarity Order[] = { ETNSkinRarity::Common, ETNSkinRarity::Rare, ETNSkinRarity::Epic };
		float TotalWeight = 0.f;
		for (const ETNSkinRarity Rarity : Order)
		{
			const bool bPresent = Candidates.ContainsByPredicate([Rarity](const FCandidate& C) { return C.Rarity == Rarity; });
			TotalWeight += bPresent ? Rules.WeightOf(Rarity) : 0.f;
		}
		if (TotalWeight <= 0.f) { return INDEX_NONE; }

		float Pick = Stream.FRandRange(0.f, TotalWeight);
		ETNSkinRarity Chosen = ETNSkinRarity::Common;
		for (const ETNSkinRarity Rarity : Order)
		{
			const bool bPresent = Candidates.ContainsByPredicate([Rarity](const FCandidate& C) { return C.Rarity == Rarity; });
			const float Weight = bPresent ? Rules.WeightOf(Rarity) : 0.f;
			if (Weight <= 0.f) { continue; }
			Chosen = Rarity;
			if (Pick < Weight) { break; }
			Pick -= Weight;
		}

		TArray<int32, TInlineAllocator<32>> Pool;
		for (int32 i = 0; i < Candidates.Num(); ++i)
		{
			if (Candidates[i].Rarity == Chosen) { Pool.Add(i); }
		}
		return Pool.Num() > 0 ? Pool[Stream.RandRange(0, Pool.Num() - 1)] : INDEX_NONE;
	}

	/**
	 * Abre una caja con el saldo Balance: cobra BoxPrice, elige con Roll y, si ya la tenía (IsOwned), devuelve RefundFor.
	 * Sin saldo o sin candidatas no se abre ni se cobra (bOpened = false, BalanceAfter = Balance).
	 */
	inline FTN_MysteryBoxResult Open(int32 Balance, TConstArrayView<FCandidate> Candidates, const FTN_MysteryBoxRules& Rules,
		TFunctionRef<bool(FName)> IsOwned, FRandomStream& Stream)
	{
		FTN_MysteryBoxResult Out;
		Out.BalanceAfter = Balance;
		const int32 Price = FMath::Max(0, Rules.BoxPrice);
		if (!CanAfford(Balance, Price)) { return Out; }
		const int32 Index = Roll(Candidates, Rules, Stream);
		if (!Candidates.IsValidIndex(Index)) { return Out; }

		const FCandidate& Won = Candidates[Index];
		Out.bOpened = true;
		Out.SkinId = Won.Id;
		Out.Category = Won.Category;
		Out.Rarity = Won.Rarity;
		Out.bDuplicate = IsOwned(Won.Id);
		Out.RefundPoints = Out.bDuplicate ? RefundFor(Rules) : 0;
		Out.BalanceAfter = Balance - Price + Out.RefundPoints;
		return Out;
	}
}
