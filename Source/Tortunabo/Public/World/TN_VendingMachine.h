#pragma once

#include "CoreMinimal.h"
#include "World/TN_InteractableBase.h"
#include "Game/TN_ChapaRules.h"
#include "Game/TN_VendingStock.h"
#include "TN_VendingMachine.generated.h"

class ATortugaCharacter;
class UBoxComponent;
class USoundBase;
class UTextRenderComponent;
enum class ETNRaceSound : uint8;

/** Crédito de un jugador en una máquina (PlayerKey: el PlayerId de su PlayerState). */
USTRUCT()
struct FTNVendingCredit
{
	GENERATED_BODY()

	UPROPERTY()
	int32 PlayerKey = 0;

	UPROPERTY()
	int32 Credit = 0;
};

/**
 * @brief Máquina expendedora (#859, plan maestro §1 decisión 6 y hoja Economía): se colocan a mano en el mapa. Se compra
 * lanzando chapas (ATN_Chapa) por su ranura.
 *
 *  - La ranura es una caja delante de la cara de la máquina (+X). El servidor ve qué chapa la cruza (ATN_Chapa la busca con
 *    FindSlotCrossed mientras vuela) y suma su valor al crédito de quien la lanzó. El crédito es por jugador y por máquina.
 *  - Vende 4 objetos (Stock, por instancia; sin él, UTN_EconomySettings::DefaultVendingOffers, en Config/DefaultGame.ini).
 *    La pantalla enseña el elegido, su precio y el crédito de quien mira.
 *  - Con la tecla de interactuar: pulsarla cambia de objeto; mantenerla UTN_EconomySettings::VendingBuyHoldSeconds compra el
 *    elegido si llega el crédito. El objeto sale por la bandeja como pickup.
 *
 * Arte: cubos del motor con color (faltan la malla y la textura de la máquina).
 */
UCLASS()
class TORTUNABO_API ATN_VendingMachine : public ATN_InteractableBase
{
	GENERATED_BODY()

public:
	ATN_VendingMachine();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual bool CanInteract(APawn* Interactor) const override;
	/** Pulsar sin mantener (si algo llama a Interact directamente): cambia de objeto. */
	virtual void Interact(APawn* Interactor) override;
	virtual FVector GetInteractionPoint() const override;
	virtual float GetHoldDuration() const override;
	virtual void BeginHoldInteract(APawn* Interactor) override;
	virtual void EndHoldInteract(APawn* Interactor) override;
	virtual float GetHoldProgress(const APawn* Interactor) const override;

	/** La máquina cuya ranura cruza el segmento de A a B (null si ninguna). */
	static ATN_VendingMachine* FindSlotCrossed(const UWorld* World, const FVector& A, const FVector& B);

	/** true si el segmento de A a B entra en la ranura. */
	bool DoesSegmentCrossSlot(const FVector& A, const FVector& B) const;

	/** Servidor: una chapa de Thrower de valor Value ha entrado por la ranura. false si no hay a quién apuntársela. */
	bool ServerInsertChapa(ATortugaCharacter* Thrower, int32 Value);

	/** Servidor: Buyer compra el objeto elegido con su crédito; sale por la bandeja. */
	TNChapaRules::EBuy ServerTryBuy(ATortugaCharacter* Buyer);

	/** Servidor: pasa al objeto siguiente. */
	void ServerCycleOffer();

	/** Crédito de Pawn en esta máquina (cualquier máquina: está replicado). */
	int32 GetCreditFor(const APawn* Pawn) const;

	int32 GetSelectedIndex() const { return SelectedIndex; }
	const TArray<FTNVendingOffer>& GetOffers() const;

	/** Centro de la ranura en el mundo (para apuntar). */
	FVector GetSlotLocation() const;

	/** Dónde sale lo comprado. */
	FVector GetTrayLocation() const;

	/** Lista de objetos de esta máquina (servidor o diseño; null = la de la economía). */
	void SetStock(UTN_VendingStockData* InStock);

protected:
	/** Lo que vende esta máquina (4 objetos con su precio). Vacío = UTN_EconomySettings::DefaultVendingOffers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Máquina")
	TObjectPtr<UTN_VendingStockData> Stock;

	/** La ranura: solo es una forma (sin colisión); la chapa que la cruza en el servidor cuenta. */
	UPROPERTY(VisibleAnywhere, Category = "Máquina")
	TObjectPtr<UBoxComponent> SlotBox;

	UPROPERTY(VisibleAnywhere, Category = "Máquina")
	TObjectPtr<UStaticMeshComponent> SlotMesh;

	UPROPERTY(VisibleAnywhere, Category = "Máquina")
	TObjectPtr<UStaticMeshComponent> TrayMesh;

	/** Por dónde sale lo comprado. */
	UPROPERTY(VisibleAnywhere, Category = "Máquina")
	TObjectPtr<USceneComponent> TrayPoint;

	/** Pantalla: objeto elegido, precio y crédito de quien mira. */
	UPROPERTY(VisibleAnywhere, Category = "Máquina")
	TObjectPtr<UTextRenderComponent> Display;

	/** Sonidos: entra una chapa, sale un objeto, no llega el crédito. Nulo = el sintetizado de los objetos. */
	UPROPERTY(EditDefaultsOnly, Category = "Máquina|Audio")
	TObjectPtr<USoundBase> InsertSound;

	UPROPERTY(EditDefaultsOnly, Category = "Máquina|Audio")
	TObjectPtr<USoundBase> VendSound;

	UPROPERTY(EditDefaultsOnly, Category = "Máquina|Audio")
	TObjectPtr<USoundBase> DenySound;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Machine)
	TArray<FTNVendingCredit> Credits;

	UPROPERTY(ReplicatedUsing = OnRep_Machine)
	int32 SelectedIndex = 0;

	/** Quién mantiene la tecla y desde cuándo (hora del servidor): el aro de progreso en todas las máquinas. */
	UPROPERTY(Replicated)
	TObjectPtr<APawn> HoldPawn;

	UPROPERTY(Replicated)
	float HoldStart = 0.f;

	UFUNCTION()
	void OnRep_Machine();

	/** Servidor: un sonido de la máquina para todas las máquinas. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSound(USoundBase* Sound);

	/** Suena Sound en la máquina o, sin él, Fallback en la tortuga. */
	void PlayMachineSound(USoundBase* Sound, ATortugaCharacter* Turtle, ETNRaceSound Fallback, float Pitch);

	/** Rehace la pantalla para el jugador local. */
	void RefreshDisplay();

	/** Pone color a las piezas de cubo del motor (no en el servidor dedicado). */
	void ApplyCodeArt();

	/** Servidor: deja de contar el mantener la tecla. */
	void ClearHold();

	/** Servidor: deja el pickup de Item en la bandeja. false si no se ha podido. */
	bool SpawnTrayPickup(const FTN_InventoryItem& Item) const;

	static int32 KeyOf(const APawn* Pawn);
	int32 FindCreditIndex(int32 Key) const;

	float DisplayClock = 0.f;
	bool bHoldResolved = false;
};
