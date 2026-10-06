#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/TN_TctItemRules.h"
#include "World/TN_PickupInteractableBase.h"
#include "TN_TctItemPad.generated.h"

class ATN_TctItemPad;
class UStaticMeshComponent;

/**
 * Objeto en el suelo de Todos contra Todos (#651): el pickup de siempre (se coge con interactuar, con su marca dorada) que
 * avisa a su punto de objetos (ATN_TctItemPad) cuando alguien lo coge. El aviso dice el nombre del objeto («Coger Garfio»):
 * el objeto va replicado y cada máquina monta el texto. El flotador (#777) no va a la mano: se cuelga del caparazón
 * (UTN_TctItemComponent::ServerGrantFloat), así que se coge aunque las manos estén llenas y no si ya se lleva uno.
 */
UCLASS()
class TORTUNABO_API ATN_TctItemPickup : public ATN_PickupInteractableBase
{
	GENERATED_BODY()

public:
	ATN_TctItemPickup();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool CanInteract(APawn* Interactor) const override;
	virtual void Interact(APawn* Interactor) override;

	/** Servidor, antes de FinishSpawning: el objeto y su punto. */
	void SetUp(ATN_TctItemPad* InPad, ETNTctItem InKind, const FTN_InventoryItem& Item);

	ETNTctItem GetKind() const { return static_cast<ETNTctItem>(Kind); }
	bool IsTaken() const { return bTaken; }

private:
	/** ETNTctItem, para el aviso con el nombre. */
	UPROPERTY(ReplicatedUsing = OnRep_Kind)
	uint8 Kind = 0;

	UFUNCTION()
	void OnRep_Kind();

	void RefreshPrompt();

	/** Servidor: el flotador se cuelga del caparazón de Interactor y el pickup se va. */
	void TakeFloat(APawn* Interactor);

	TWeakObjectPtr<ATN_TctItemPad> Pad;
};

/**
 * Punto de objetos de Todos contra Todos (#651): un disco en el suelo de la arena donde aparece un objeto de combate al
 * empezar cada ronda y, cuando alguien lo coge, otro pasado RespawnSeconds (sorteado con los pesos de TNTctItemRules, nunca
 * el mismo dos veces seguidas en el mismo punto). Entre rondas está vacío. Si el agua lo cubre, deja de sacar objetos hasta la
 * ronda siguiente (el mar se lleva el que hubiera).
 *
 * Los crea ATN_TctGameMode repartidos por la arena (TNTctItemRules::PlanPads: los más altos o expuestos, épicos; lejos de las
 * salidas); si el nivel ya trae alguno colocado a mano, se usan esos. Cada punto tiene una rareza (#830): decide qué objetos
 * saca (TNTctItemRules::PadItemWeight, mejor según avanza la ronda) y cómo se ve de lejos: el disco y un haz de luz del color
 * de su rareza (más alto cuanto más raro) mientras hay un objeto puesto. Replicado solo para que se vea el disco; el reloj
 * (FTNTctPadClock) y el sorteo son del servidor. ServerUpdate es la misma vuelta que hace su temporizador, a mano (pruebas).
 */
UCLASS()
class TORTUNABO_API ATN_TctItemPad : public AActor
{
	GENERATED_BODY()

public:
	ATN_TctItemPad();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Servidor: empieza la ronda a la hora Now; PadIndex escalona la primera aparición. */
	void ServerStartRound(double Now, int32 PadIndex);

	/** Servidor: fin de la ronda: se lleva el objeto que hubiera y para el reloj. */
	void ServerStopRound();

	/** Servidor: una vuelta del reloj a la hora Now con el agua en WaterZ (saca, retira o espera). */
	void ServerUpdate(double Now, float WaterZ);

	/** Servidor: su pickup avisa de que lo han cogido. */
	void NotifyTaken(ATN_TctItemPickup* Pickup);

	/** Servidor: la rareza del punto (al crearlo). */
	void ServerSetRarity(ETNTctRarity NewRarity);
	ETNTctRarity GetRarity() const { return static_cast<ETNTctRarity>(Rarity); }

	/** El haz de luz se ve (hay un objeto puesto), en cualquier máquina. */
	bool IsBeamOn() const { return bBeamOn; }

	/** Altura del haz de luz de cada rareza (uu). */
	static float BeamHeight(ETNTctRarity Rarity);

	/** Color del disco y del haz de cada rareza. */
	static FLinearColor RarityColor(ETNTctRarity Rarity);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	const FTNTctPadClock& GetClock() const { return Clock; }
	ATN_TctItemPickup* GetCurrentPickup() const { return Current.Get(); }
	ETNTctItem GetLastKind() const { return LastKind; }

	/** Segundos hasta el siguiente objeto tras cogerlo. */
	UPROPERTY(EditAnywhere, Category = "Tct", meta = (ClampMin = "1.0"))
	float RespawnSeconds = TNTctItemTuning::PadRespawnSeconds;

	/** Altura del pie del objeto sobre el centro del disco (uu): el pickup flota solo sobre su marca. */
	UPROPERTY(EditAnywhere, Category = "Tct", meta = (ClampMin = "0.0"))
	float ItemLift = 3.f;

	/** Color del disco. */
	UPROPERTY(EditAnywhere, Category = "Tct")
	FLinearColor DiscColor = FLinearColor(1.f, 0.62f, 0.18f);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Tct")
	TObjectPtr<UStaticMeshComponent> Disc;

	UPROPERTY(VisibleAnywhere, Category = "Tct")
	TObjectPtr<UStaticMeshComponent> Beam;

private:
	/** ETNTctRarity del punto. */
	UPROPERTY(ReplicatedUsing = OnRep_Look)
	uint8 Rarity = 0;

	/** Hay un objeto puesto: el haz de luz se ve. */
	UPROPERTY(ReplicatedUsing = OnRep_Look)
	bool bBeamOn = false;

	UFUNCTION()
	void OnRep_Look();

	/** Pinta el disco y el haz según la rareza y si hay objeto. */
	void RefreshLook();

	/** Servidor: cómo va la ronda por el agua (0-1) para sortear mejores objetos según avanza. */
	float RoundProgress() const;

	/** El temporizador del servidor: ServerUpdate con la hora y el agua de ahora. */
	void UpdateFromTimer();

	/** Saca un objeto sorteado. false si no ha podido. */
	bool SpawnItem();

	void RemoveCurrent();

	FTNTctPadClock Clock;
	ETNTctItem LastKind = ETNTctItem::None;
	TWeakObjectPtr<ATN_TctItemPickup> Current;
	/** Los objetos que se pueden dar en esta ronda (se miran al empezarla). */
	TArray<ETNTctItem> Available;
	FTimerHandle UpdateHandle;
};
