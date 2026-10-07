#pragma once

#include "CoreMinimal.h"
#include "World/TN_InteractableBase.h"
#include "TN_RescuePickup.generated.h"

/**
 * Pickup de rescate: spawnea cuando un jugador muere.
 * Al interactuar, respawnea al jugador muerto en la ubicación del pickup.
 * Hereda de TN_InteractableBase (mesh + prompt 3D + distancia configurable).
 *
 * Revivir con chapas (#862, plan maestro §1 decisión 10; provisional, el sitio puede cambiar): con
 * UTN_EconomySettings::ReviveChapaCost > 0 (4, hoja Economía), junto al cuerpo hay que mantener la tecla de interactuar
 * ReviveHoldSeconds y el servidor cobra las chapas a quien revive; sin chapas suficientes no empieza. Con coste 0 es el
 * rescate gratis de pulsar de siempre. Se suma a la reanimación con baile y al desangrado, que no cambian.
 */
UCLASS()
class TORTUNABO_API ATN_RescuePickup : public ATN_InteractableBase
{
	GENERATED_BODY()

public:
	ATN_RescuePickup();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool CanInteract(APawn* Interactor) const override;
	virtual void Interact(APawn* Interactor) override;
	virtual float GetHoldDuration() const override;
	virtual void BeginHoldInteract(APawn* Interactor) override;
	virtual void EndHoldInteract(APawn* Interactor) override;
	virtual float GetHoldProgress(const APawn* Interactor) const override;

	/**
	 * Servidor: Payer paga Cost chapas por revivir. Solo si las tiene se llama a Revive, y solo si Revive dice que ha revivido
	 * se cobran. true si ha revivido. Con Cost 0, Revive sin cobrar nada.
	 */
	static bool ChargeAndRevive(APawn* Payer, int32 Cost, TFunctionRef<bool()> Revive);

	/**
	 * Inicializa el pickup con el PlayerId del jugador muerto.
	 * Solo llamar en el servidor.
	 */
	void SetDeadPlayerId(int32 InPlayerId);

	/** Makes the invisible rescue trigger follow the dead pawn/ragdoll on the server. */
	void FollowDeadPawn(APawn* InDeadPawn, FName InTrackingBone = TEXT("pelvis"));

	/** Devuelve el PlayerId del jugador muerto asociado a este pickup. */
	int32 GetDeadPlayerId() const { return DeadPlayerId; }

protected:
	/**
	 * PlayerId (APlayerState::GetPlayerId()) del jugador muerto.
	 * Replicado para que todos los clientes puedan mostrarlo en la UI.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Rescue")
	int32 DeadPlayerId = -1;

	/**
	 * Huevo de la reaparición (el de TNCastleKit) que flota sobre el cuerpo mientras Mesh no lleve una malla
	 * del proyecto. Escala respecto al huevo de 2,4 m de la salida.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rescue|Art", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float CodeArtEggScale = 0.28f;

	/** Altura (cm) de la base del huevo sobre el punto que sigue al cuerpo. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rescue|Art", meta = (ClampMin = "0.0"))
	float CodeArtLift = 45.f;

	/** Vaivén vertical del huevo: amplitud (cm) y frecuencia (Hz). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rescue|Art", meta = (ClampMin = "0.0"))
	float CodeArtBobAmplitude = 8.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rescue|Art", meta = (ClampMin = "0.0"))
	float CodeArtBobHz = 0.7f;

	/** Giro del huevo sobre sí mismo (grados por segundo). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rescue|Art")
	float CodeArtSpinDegreesPerSecond = 40.f;

private:
	/** Huevo de código (null si el Blueprint trae su propia malla o en el servidor dedicado). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> CodeArtEgg;

	/** Reloj del vaivén y del giro (local en cada máquina). */
	float CodeArtClock = 0.f;

	/** Monta el huevo y oculta el marcador del motor, que conserva su colisión: es lo que encuentra el escaneo (#49). */
	void BuildCodeArt();

	void AnimateCodeArt(float DeltaSeconds);

	UPROPERTY()
	TWeakObjectPtr<APawn> FollowedDeadPawn;

	FName TrackingBone = TEXT("pelvis");

	FVector ResolveFollowLocation() const;

	/** Quién mantiene la tecla para revivir pagando y desde cuándo (hora del servidor): el aro de progreso en todas las máquinas. */
	UPROPERTY(Replicated)
	TObjectPtr<APawn> HoldPawn;

	UPROPERTY(Replicated)
	float HoldStart = 0.f;

	/** Chapas que cuesta revivir aquí (UTN_EconomySettings). */
	static int32 ReviveCost();

	/** Servidor: revive al muerto (cobrando a Interactor si cuesta chapas) y se va. false si no se ha podido. */
	bool ServerRevive(APawn* Interactor);

	/** Servidor: deja de contar el mantener la tecla. */
	void ClearHold();
};

