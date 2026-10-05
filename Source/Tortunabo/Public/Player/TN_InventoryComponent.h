#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/TN_InventoryTypes.h"
#include "TN_InventoryComponent.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class USoundBase;

/** Cómo se lleva un objeto en las aletas delanteras. */
UENUM(BlueprintType)
enum class ETNItemHold : uint8
{
	/** Según su tamaño (y lo que se muestra: nada en las aletas). */
	Auto       UMETA(DisplayName = "Automático (por tamaño)"),
	/** Pequeño: en la aleta derecha, apoyado encima. */
	OneFlipper UMETA(DisplayName = "En una aleta"),
	/** Grande o pesado: abrazado con las dos aletas delante de la tripa. */
	Hug        UMETA(DisplayName = "Abrazado con las dos"),
	/** Alargado: cogido por un extremo con la aleta derecha, apuntando hacia delante y arriba. */
	ByEnd      UMETA(DisplayName = "Cogido por un extremo"),
};

/**
 * @brief Inventario de dos slots (equipado + guardado) replicado por jugador.
 *
 *  - Equipado: se ve en las aletas delanteras (cosmético y local en cada máquina, a partir de lo replicado): pequeño en
 *    la derecha, grande o pesado abrazado con las dos, alargado cogido por un extremo (ETNItemHold; lo decide su tamaño
 *    o HoldOverrides). UTN_TurtleAnimInstance pone los brazos que lo sujetan. Se consume con el input "Usar".
 *  - Guardado: dentro del caparazón. Al cambiar de ranura (RotateItems) o al sacar el guardado porque se ha gastado el
 *    de la mano, la aleta va a la espalda, el objeto encoge y entra o sale del caparazón, con un «toc» corto.
 *  - El peso total afecta al StaminaComponent (reduce MaxStaminaEffective).
 *  - Server-authoritative: todas estas operaciones exigen HasAuthority en el llamador
 *    (pickups, ServerUseEquippedItem, GameMode). Única excepción: RotateItems, que
 *    reenvía al servidor vía ServerRotateItems cuando se llama en un cliente.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TORTUNABO_API UTN_InventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_InventoryComponent();

	/** @brief Refresca el visual del ítem equipado tras possess (cliente y servidor). */
	virtual void BeginPlay() override;

	/** Coloca el objeto de las aletas cada fotograma, después de la animación (no en servidor dedicado). */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ── Lo que ve la animación (cosmético, local) ───────────────────────────

	/** Cómo lleva ahora el objeto que se ve en las aletas (Auto = nada). Mientras lo guarda o lo saca, en una aleta. */
	ETNItemHold GetShownHold() const;

	/** Abrazando: cuánto abre las aletas (0 = juntas, 1 = muy abiertas), según el ancho del objeto. */
	float GetHugOpen() const { return HugOpen; }

	/** Aleta derecha hacia la espalda para guardar o sacar algo del caparazón (0..1). */
	float GetStashReach() const;

	/**
	 * @brief Intenta añadir un ítem al primer slot disponible (equipado > guardado).
	 * @return true si se añadió.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool TryAddItem(const FTN_InventoryItem& NewItem);

	/**
	 * @brief Añade el ítem como equipado, opcionalmente sustituyendo el actual.
	 * @param bReplaceIfFull Si true y ya hay equipado, lo reemplaza tirando el viejo.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool TryAddOrReplaceEquipped(const FTN_InventoryItem& NewItem, bool bReplaceIfFull = true);

	/** @brief Devuelve true si el ítem puede recibirse (slot libre o reemplazo permitido). */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool CanReceiveItem(const FTN_InventoryItem& NewItem, bool bAllowReplaceIfFull = true) const;

	/**
	 * Servidor: cambia el objeto de la mano por NewItem sin gastarlo ni tocar lo guardado (el triple coco pasa a tener un uso
	 * menos). false si no hay nada en la mano.
	 */
	bool TryReplaceEquippedItem(const FTN_InventoryItem& NewItem);

	/** @brief Rota equipado ↔ guardado. RPC al servidor desde cliente. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RotateItems();

	/** @brief Consume el ítem equipado (lo elimina del slot) y lo devuelve por referencia. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool TryConsumeEquippedItem(FTN_InventoryItem& OutConsumedItem);

	/** @brief Extrae el ítem equipado sin consumirlo (lo saca del slot pero permite reusarlo). */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool TryExtractEquippedItem(FTN_InventoryItem& OutExtractedItem);

	/**
	 * Busca en equipado y almacenado un ítem con UseType == InUseType.
	 * Si lo encuentra, lo consume (igual que TryConsumeEquippedItem) y lo devuelve.
	 * Útil para auto-consumo en eventos externos (ej: Tótem al morir).
	 * Solo funciona en autoridad.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool TryConsumeItemByUseType(ETN_ItemUseType InUseType, FTN_InventoryItem& OutConsumedItem);

	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool HasEquippedItem() const { return bHasEquippedItem; }

	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool HasStoredItem() const { return bHasStoredItem; }

	UFUNCTION(BlueprintPure, Category = "Inventory")
	FTN_InventoryItem GetEquippedItem() const { return EquippedItem; }

	UFUNCTION(BlueprintPure, Category = "Inventory")
	FTN_InventoryItem GetStoredItem() const { return StoredItem; }

	/** @brief Devuelve la suma de ItemWeight de todos los ítems llevados (equipado + guardado). */
	UFUNCTION(BlueprintPure, Category = "Inventory|Weight")
	float GetTotalCarriedWeight() const;

protected:
	/**
	 * Solo para mallas sin los huesos de las aletas (RightHand, LeftHand...): el objeto va en este socket (o en la raíz de
	 * la malla con EquippedRelativeLocation/Rotation). Con TotugaDemo_Rig no se usa: el Blueprint guarda «EspaldaSocket»,
	 * que esa malla no tiene, y por eso los objetos salían en los pies.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Visual")
	FName EquippedAttachSocket = NAME_None;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Visual")
	FVector EquippedRelativeLocation = FVector(20.f, 0.f, 40.f);

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Visual")
	FRotator EquippedRelativeRotation = FRotator::ZeroRotator;

	// ── Objetos en las aletas ────────────────────────────────────────────────
	// El tamaño es el del objeto en el mundo (su malla por EquippedMeshScale, con el giro EquippedMeshRotation).

	/** Largo (cm) a partir del cual un objeto delgado se coge por un extremo... */
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Hold", meta = (ClampMin = "0.0"))
	float ByEndMinLength = 30.f;

	/** ...si además es al menos estas veces más largo que ancho. */
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Hold", meta = (ClampMin = "1.0"))
	float ByEndMinRatio = 2.2f;

	/** Tamaño (cm, su lado mayor) a partir del cual se abraza con las dos aletas. */
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Hold", meta = (ClampMin = "0.0"))
	float HugMinSize = 34.f;

	/** Peso (ItemWeight) a partir del cual se abraza con las dos aletas aunque sea pequeño. */
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Hold", meta = (ClampMin = "0.0"))
	float HugMinWeight = 3.f;

	/** Cómo se lleva cada objeto (por ItemId) si no vale el automático por tamaño. */
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Hold")
	TMap<FName, ETNItemHold> HoldOverrides;

	/** Segundos de guardar o sacar algo del caparazón (la aleta va a la espalda y vuelve). */
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Hold", meta = (ClampMin = "0.2", ClampMax = "1.5"))
	float StashSeconds = 0.5f;

private:
	/** @brief Server RPC para que un cliente solicite rotar slots. */
	UFUNCTION(Server, Reliable)
	void ServerRotateItems();

	UPROPERTY(ReplicatedUsing = OnRep_EquippedItem)
	FTN_InventoryItem EquippedItem;

	UPROPERTY(Replicated)
	bool bHasEquippedItem = false;

	UPROPERTY(ReplicatedUsing = OnRep_StoredItem)
	FTN_InventoryItem StoredItem;

	UPROPERTY(Replicated)
	bool bHasStoredItem = false;

	/**
	 * Número de la última vez que algo entró o salió del caparazón (cambiar de ranura, o sacar el guardado al gastar el de
	 * la mano; 1-255, 0 = ninguna). Cada máquina anima con él la aleta que va a la espalda.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_StashSerial)
	uint8 StashSerial = 0;

	/** Qué pasó esa vez (bits): 1 = entró el de la mano, 2 = salió el guardado, 4 = salió tras gastar el de la mano. */
	UPROPERTY(Replicated)
	uint8 StashKind = 0;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> EquippedVisualMesh;

	/** Componente padre del visual equipado (la malla de la tortuga). */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> VisualMeshParent;

	/** @brief OnRep: refresca el mesh visual del slot equipado tras una replicación. */
	UFUNCTION()
	void OnRep_EquippedItem();

	/** @brief OnRep: hook para HUD al cambiar el guardado (no tiene visual en mundo). */
	UFUNCTION()
	void OnRep_StoredItem();

	UFUNCTION()
	void OnRep_StashSerial();

	/** @brief Sincroniza lo que se ve en las aletas con el EquippedItem actual (y anima si ha entrado o salido algo del caparazón). */
	void RefreshEquippedVisual();

	/** Servidor: algo ha entrado (bit 1) o salido (bit 2; con el 4, tras gastar lo de la mano) del caparazón. */
	void NoteStash(uint8 Kind);

	// ── Objeto en las aletas (cosmético, local en cada máquina) ─────────────

	/** Pasa a mostrar el objeto equipado ahora (o nada), con un pequeño «pop» si bPop. */
	void ShowEquippedNow(bool bPop);

	/** Empieza a guardar o sacar del caparazón (Kind como StashKind); Delay segundos de espera antes. */
	void StartStash(uint8 Kind, float Delay);

	/** Clasifica y mide el objeto que se muestra (ShownHold, ShownHalfSize, ShownLongAxis, ShownCenter). */
	void MeasureShownItem();

	/** Coloca el objeto respecto a la mano (o el pecho) según cómo se lleva; bSnap sin suavizar. */
	void PlaceShownItem(USkeletalMeshComponent* Body, float DeltaTime, bool bSnap);

	/** Suena el «toc» de guardar (bIn) o sacar del caparazón en esta máquina. */
	void PlayStashSound(bool bIn) const;

	/** La tortuga no puede llevarlo en las aletas ahora (metida en el caparazón, llevando o llevada, muerta). */
	bool ShouldHideShownItem() const;

	/** Lo que se ve en las aletas: malla, escala en el mundo, giro fino, ItemId y peso. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> ShownMesh;
	FVector ShownScale = FVector::OneVector;
	FRotator ShownRotation = FRotator::ZeroRotator;
	FName ShownItemId = NAME_None;
	float ShownWeight = 0.f;

	/** Cómo se lleva, medio tamaño en el mundo (cm, en sus ejes tras el giro fino), eje largo (0 X, 1 Y, 2 Z) y su centro. */
	ETNItemHold ShownHold = ETNItemHold::Auto;
	FVector ShownHalfSize = FVector::ZeroVector;
	int32 ShownLongAxis = 2;
	FVector ShownCenter = FVector::ZeroVector;

	/** Hueso al que va enganchado ahora y su transformación relativa (se suaviza hacia la buena). */
	FName ShownBone = NAME_None;
	FTransform ShownRelative = FTransform::Identity;
	bool bShownPlaced = false;

	/** Escala de aparecer y desaparecer (0..1): pop al coger, encoger al guardar, esconderse en el caparazón. */
	float PopTime = -1.f;
	float VisibleAlpha = 1.f;

	/** Guardar o sacar del caparazón: segundos desde que empezó (negativo = esperando; muy negativo = nada). */
	float StashTime = -100.f;
	bool bStashIn = false;
	bool bStashOut = false;
	bool bStashSwitched = false;
	bool bStashInSounded = false;
	bool bStashOutSounded = false;

	/** Abrazando: apertura de las aletas (se ajusta sola al ancho del objeto midiendo las manos). */
	float HugOpen = 0.5f;

	/** Ya ha mostrado algo (lo primero que llega no se anima) y el último StashSerial visto. */
	bool bVisualSynced = false;
	uint8 SeenStashSerial = 0;

	/** @brief Añade un ítem en el primer slot libre (server-side internal). */
	bool AddItemInternal(const FTN_InventoryItem& NewItem);

	/** @brief Reemplaza el equipado si bReplaceIfFull, si no usa el guardado (server-side internal). */
	bool AddOrReplaceEquippedInternal(const FTN_InventoryItem& NewItem, bool bReplaceIfFull);

	/** @brief Consume y devuelve el equipado (server-side internal). */
	bool ConsumeEquippedInternal(FTN_InventoryItem& OutItem);

	/**
	 * Objetos del coop que se apilan (Game/TN_CoopItems.h): qué pasa si llega NewItem con lo que ya hay en los huecos. Separate
	 * = va a otro hueco como siempre; Merge = se suma al hueco que ya lo tiene (OutSlot: 0 la mano, 1 el caparazón; OutMerged:
	 * la fila con la cuenta nueva); Full = ya tiene el máximo y no se coge. Devuelve un ETNCoopStack.
	 */
	uint8 DecideCoopStack(const FTN_InventoryItem& NewItem, int32& OutSlot, FTN_InventoryItem& OutMerged) const;

	/** @brief Intercambia equipado ↔ guardado (server-side internal). */
	void SwapSlotsInternal();

	/** @brief Cast a ATortugaCharacter del owner + null-check de Sound + MulticastPlaySfx. */
	void PlayInventorySfx(USoundBase* Sound) const;
};
