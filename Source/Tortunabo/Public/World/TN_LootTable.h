#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Math/RandomStream.h"
#include "TN_LootTable.generated.h"

/** Registro del botín de las cajas de suministros y del airdrop (qué sale y cuántas chapas). */
DECLARE_LOG_CATEGORY_EXTERN(LogTNLoot, Log, All);

/**
 * Tabla de botín de una fuente de objetos y chapas (plan maestro §1 decisión 8 y §4, hoja Economía del Excel): cuántos
 * objetos saca y la probabilidad de dar 1, 2 o 3 chapas. La usan las cajas del mapa (ATN_SupplyCrate, #861) y el airdrop
 * (ATN_SupplyDrop, #860); otra fuente (la exploración por zona) puede usar la misma estructura.
 *
 * Probabilidades de las chapas: ChapaChances[i] es la probabilidad de dar AL MENOS i + 1 chapas. Es la única lectura que
 * cuadra con las tres filas de la hoja (el airdrop suma 0,7 + 0,3 + 0,15 > 1, así que no son excluyentes): con una sola
 * tirada R en [0, 1), salen tantas chapas como umbrales cumplan R < ChapaChances[i]. Un umbral mayor que el anterior se
 * recorta al anterior (nunca es más fácil sacar 2 que 1).
 */
USTRUCT(BlueprintType)
struct TORTUNABO_API FTNLootTableDef
{
	GENERATED_BODY()

	/** Objetos que saca como mínimo (de la tabla del coop: TNCoopItems::RollLoot). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Objetos", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MinItems = 1;

	/** Objetos que saca como máximo (si es menor que MinItems, cuenta MinItems). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Objetos", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MaxItems = 1;

	/**
	 * Peso de cada fila de DT_Items en el sorteo, por nombre de fila o ItemId (0 la quita). Las filas que no salen aquí usan
	 * el peso de la caja (el de los rebuscables: 1, el tótem 0,3). Los objetos del coop definidos en código llevan el peso
	 * del Excel (TNCoopItemRules).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Objetos")
	TMap<FName, float> ItemWeights;

	/** Probabilidad de dar al menos 1, 2, 3... chapas (hoja Economía). Ver la nota de la estructura. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Chapas")
	TArray<float> ChapaChances;

	/**
	 * Actor de la chapa que se suelta por cada chapa (la crea el lote de #858). Vacío: el reparto solo registra el número
	 * (LogTNLoot) hasta que exista.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Chapas")
	TSoftClassPtr<AActor> ChapaClass;
};

/** Tabla de botín reutilizable como asset (Content Browser > Miscellaneous > Data Asset > TN_LootTable). */
UCLASS(BlueprintType)
class TORTUNABO_API UTN_LootTable : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ShowOnlyInnerProperties))
	FTNLootTableDef Loot;
};

/** Lo que da una apertura: cuántos objetos y cuántas chapas. */
struct FTNLootRoll
{
	int32 ItemCount = 0;
	int32 ChapaCount = 0;

	bool IsEmpty() const { return ItemCount <= 0 && ChapaCount <= 0; }
};

/** Reglas puras del botín (sin mundo ni red): las recorren las pruebas Tortunabo.Supply.Loot.*. */
namespace TNLootRules
{
	/** Tope de chapas por apertura (tamaño útil de ChapaChances). */
	inline constexpr int32 MaxChapaTiers = 8;

	/** Umbrales saneados: cada uno en [0, 1] y nunca mayor que el anterior; como mucho MaxChapaTiers. */
	TORTUNABO_API TArray<float> SanitizeChances(TConstArrayView<float> AtLeast);

	/** Chapas de una tirada Roll en [0, 1): cuántos umbrales (saneados) cumplen Roll < umbral. */
	TORTUNABO_API int32 ChapasFromRoll(TConstArrayView<float> AtLeast, float Roll);

	/** Probabilidad de dar exactamente Count chapas con esos umbrales (0 = ninguna). */
	TORTUNABO_API float ChanceOfExactly(TConstArrayView<float> AtLeast, int32 Count);

	/** Objetos de una apertura con Roll en [0, 1): reparto uniforme entre MinItems y MaxItems. */
	TORTUNABO_API int32 ItemsFromRoll(int32 MinItems, int32 MaxItems, float Roll);

	/** Cuántos objetos y chapas da una apertura con Def, sacando dos tiradas de Stream (objetos y chapas, en ese orden). */
	TORTUNABO_API FTNLootRoll Roll(const FTNLootTableDef& Def, FRandomStream& Stream);

	/** Caja de suministros del mapa (#861): 1 objeto; chapas 0,25 / 0,07 / 0 (hoja Economía). */
	TORTUNABO_API FTNLootTableDef SupplyCrateDefaults();

	/** Airdrop (#860): 2 objetos; chapas 0,7 / 0,3 / 0,15 (hoja Economía). */
	TORTUNABO_API FTNLootTableDef AirdropDefaults();
}
