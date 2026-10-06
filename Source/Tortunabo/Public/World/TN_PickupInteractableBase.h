#pragma once

#include "CoreMinimal.h"
#include "Core/TN_InventoryTypes.h"
#include "World/TN_InteractableBase.h"
#include "TN_PickupInteractableBase.generated.h"

class UTN_InventoryComponent;
class UTN_PickupGlowComponent;
class UDataTable;

/**
 * @brief Base de todos los pickups del mundo que añaden un ítem al inventario del jugador.
 *
 * Configurable vía DataTable (ItemDataTable + ItemRowName) o inline (PickupItem directo).
 * Cuando un jugador interactúa: TryAddOrReplaceEquipped en su UTN_InventoryComponent y destruye el pickup.
 * Subclases especializadas pueden override Interact para lógica adicional (ej. concha trampa, tótem).
 *
 * Todos llevan la marca de «esto se coge» (UTN_PickupGlowComponent: anillo dorado que gira en el suelo, columna de luz
 * tenue, chispitas que suben, luz suave cerca y el objeto que flota y gira), en todos los modos y Blueprints.
 */
UCLASS()
class TORTUNABO_API ATN_PickupInteractableBase : public ATN_InteractableBase
{
	GENERATED_BODY()

public:
	ATN_PickupInteractableBase();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool CanInteract(APawn* Interactor) const override;
	virtual void Interact(APawn* Interactor) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Pickup")
	void InitializeFromInventoryItem(const FTN_InventoryItem& NewPickupItem);

	/**
	 * true si Interactor no puede cogerlo solo porque tiene las dos ranuras llenas: el pickup está activo, sin coger y con
	 * un ítem válido. Con E se usa entonces el objeto de la mano (ServerTryInteract); en cualquier otro caso (cogido por
	 * otra jugadora hace un instante, desactivado, sin ítem) no se gasta nada (#570).
	 */
	bool IsBlockedOnlyByFullInventory(APawn* Interactor) const;

protected:
	/**
	 * [Data-driven] DataTable con filas de tipo FTN_InventoryItem.
	 * Asigna DT_Items en BP_GenericPickup → Class Defaults.
	 * Si está relleno junto con ItemRowName, el pickup se auto-configura en BeginPlay.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pickup|DataTable")
	TObjectPtr<UDataTable> ItemDataTable;

	/**
	 * Nombre de la fila en ItemDataTable que define este ítem.
	 * Ejemplo: "Item_StaminaBoost", "Item_ThrowableBall".
	 * Si se deja vacío, usa los datos de PickupItem directamente (modo manual).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|DataTable")
	FName ItemRowName = NAME_None;

	/**
	 * Datos del ítem. ReplicatedUsing: clientes aplican la mesh correcta cuando
	 * el pickup se spawnea dinámicamente (ej. bola que aterriza).
	 * Para pickups pre-colocados en nivel, el BP default ya lleva el mesh.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_PickupItem, Category = "Pickup")
	FTN_InventoryItem PickupItem;

	UPROPERTY(ReplicatedUsing = OnRep_Taken, BlueprintReadOnly, Category = "Pickup")
	bool bTaken = false;

	/**
	 * Marca de «esto se coge» (solo visual, en las máquinas con pantalla). Hace flotar y girar Mesh; sus ajustes
	 * (tamaño del anillo, columna, luz, distancias) se pueden cambiar en cada Blueprint.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<UTN_PickupGlowComponent> PickupGlow;

	UFUNCTION(BlueprintImplementableEvent, Category = "Pickup")
	void OnPickedUp(APawn* Interactor);

private:
	UFUNCTION()
	void OnRep_Taken();

	UFUNCTION()
	void OnRep_PickupItem();

	void ApplyTakenState();
	void HandleDeferredDestroy();
	void HandleRestoreDormancy();

	/**
	 * Aplica el mesh y la escala de PickupItem a Mesh (con SafeScale para evitar
	 * ejes en 0), recalcula MeshFloorOffset y compensa la escala inversa en
	 * PromptWidgetComponent. Compartido por OnRep_PickupItem e
	 * InitializeFromInventoryItem — ambos operan sobre el mismo estado.
	 */
	void ApplyPickupMeshAndScale();

	FTimerHandle DormancyTimerHandle;
};

