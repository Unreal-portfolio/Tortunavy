#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "TN_InventoryTypes.generated.h"

class UStaticMesh;
class UTexture2D;
class USoundBase;
class ATN_PickupInteractableBase;
class ATN_ThrowableItemActor;

// ─────────────────────────────────────────────────────────────────────────────
// Enum de tipos de uso
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Tipo de acción al usar un ítem del inventario.
 *        Determina qué sub-struct de FTN_InventoryItem se interpreta en el dispatch.
 */
UENUM(BlueprintType)
enum class ETN_ItemUseType : uint8
{
	None             UMETA(DisplayName = "None"),
	/** Stamina ilimitada durante X segundos + penalización post-boost. */
	SelfStaminaBoost UMETA(DisplayName = "Self Stamina Boost"),
	/** Barrita Energética: restaura stamina al máximo instantáneamente. Sin penalización post-uso. */
	SelfStaminaFull  UMETA(DisplayName = "Self Stamina Full Restore"),
	/** Tótem: si está en el inventario al morir → auto-revive.
	 *  Si se usa manualmente → revive a un jugador eliminado. */
	Totem            UMETA(DisplayName = "Totem"),
	/**
	 * Objeto del cooperativo definido solo desde código (TNCoopItems, Game/TN_CoopItems.h): el objeto
	 * y su cuenta (apilados o usos) salen del ItemId («Coop_Harpoon_15»...). No tiene fila en DT_Items.
	 */
	CoopItem         UMETA(DisplayName = "Coop Item"),
};

// ─────────────────────────────────────────────────────────────────────────────
// Sub-structs por tipo de ítem — solo aparecen campos relevantes en el editor
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Parámetros específicos del Stamina Boost (#3, #1).
 * Solo relevantes cuando UseType == SelfStaminaBoost.
 */
USTRUCT(BlueprintType)
struct FTN_StaminaBoostParams
{
	GENERATED_BODY()

	/** Segundos de stamina ilimitada al usar el boost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StaminaBoost",
		meta = (ClampMin = "0.1"))
	float DurationSeconds = 4.f;

	/**
	 * Segundos de agotamiento forzado al expirar el boost.
	 * 0 = sin penalización. Sobreescribe el default global del StaminaComponent.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StaminaBoost",
		meta = (ClampMin = "0.0"))
	float PostBoostExhaustionSeconds = 2.f;
};

/**
 * Parámetros del antiguo lanzable genérico (#6 Bola). Ningún UseType los lee: se conservan porque las filas de
 * DT_Items guardan el campo ThrowableData.
 */
USTRUCT(BlueprintType)
struct FTN_ThrowableParams
{
	GENERATED_BODY()

	/** Clase del proyectil ATN_ThrowableItemActor a spawnear. Apuntar al BP hijo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throwable")
	TSubclassOf<ATN_ThrowableItemActor> ActorClass;

	/** Velocidad de lanzamiento (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throwable",
		meta = (ClampMin = "0.0"))
	float ThrowSpeed = 1800.f;

	/**
	 * Duración de vida del proyectil (s). 0 = usar MaxLifeSeconds del BP.
	 * Permite ítems distintos con proyectiles de distinta duración.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throwable",
		meta = (ClampMin = "0.0"))
	float LifeSpanSeconds = 0.f;
};

// ─────────────────────────────────────────────────────────────────────────────
// Definición del ítem
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Definición completa de un ítem del juego.
 * Hereda FTableRowBase → se usa directamente como fila de DataTable (DT_Items).
 * Para añadir un ítem nuevo: añadir fila en DT_Items, sin crear clases C++ nuevas.
 *
 * Patrón de sub-structs:
 *   Los campos genéricos se definen aquí directamente.
 *   Los campos específicos de cada UseType viven en su propio sub-struct;
 *   solo configura el sub-struct correspondiente a tu UseType.
 */
USTRUCT(BlueprintType)
struct FTN_InventoryItem : public FTableRowBase
{
	GENERATED_BODY()

	// ── Campos genéricos ──────────────────────────────────────────────────────

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	FName ItemId = NAME_None;

	/**
	 * Icono del ítem para la UI de inventario (HUD).
	 * Si es null, el slot del HUD aparecerá vacío.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|UI")
	TObjectPtr<UTexture2D> ItemIcon = nullptr;

	/** Mesh mostrado cuando el ítem está equipado en el jugador Y como pickup en el suelo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	TObjectPtr<UStaticMesh> EquippedMesh = nullptr;

	/**
	 * Escala del mesh equipado.
	 * Se aplica al visual en mano y al pickup en suelo.
	 * Ejemplo: (0.25, 0.25, 0.25) para bola pequeña.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	FVector EquippedMeshScale = FVector(1.f, 1.f, 1.f);

	/**
	 * Rotación relativa del mesh equipado.
	 * Permite corregir la orientación del asset en la mano del jugador sin
	 * modificar el mesh original. Ejemplo: (0, 90, 0) para rotar 90° en Yaw.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	FRotator EquippedMeshRotation = FRotator::ZeroRotator;

	/** Tipo de acción al pulsar Usar. Determina qué sub-struct es relevante. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Use")
	ETN_ItemUseType UseType = ETN_ItemUseType::None;

	/**
	 * Peso del ítem en unidades arbitrarias.
	 * Reduce la stamina máxima efectiva (StaminaPerWeightUnit por unidad).
	 * 0 = sin penalización.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Weight",
		meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float ItemWeight = 0.f;

	/** Sonido al recoger el ítem. Nulo = sin sonido. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Audio")
	TObjectPtr<USoundBase> PickupSound = nullptr;

	/** Sonido al usar/consumir el ítem. Nulo = sin sonido. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Audio")
	TObjectPtr<USoundBase> UseSound = nullptr;

	/**
	 * Clase del actor pickup que aparece en el mundo.
	 * Apuntar siempre a BP_GenericPickup salvo que el pickup tenga lógica BP especial.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|World")
	TSubclassOf<ATN_PickupInteractableBase> PickupActorClass;

	// ── Parámetros por tipo de uso ────────────────────────────────────────────
	// Solo configura el sub-struct que corresponde a tu UseType.
	// Los demás sub-structs se ignoran en el dispatch.

	/** UseType == SelfStaminaBoost → duración del boost + penalización post-boost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|StaminaBoost")
	FTN_StaminaBoostParams StaminaBoostData;

	/** Sin uso: lo guardan las filas de DT_Items (ver FTN_ThrowableParams). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Throwable")
	FTN_ThrowableParams ThrowableData;

	bool IsValid() const
	{
		return ItemId != NAME_None;
	}
};
