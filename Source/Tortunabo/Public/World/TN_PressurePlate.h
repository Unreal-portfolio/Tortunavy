#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/TN_ButtonGroupManager.h"   // FTN_TransformAction
#include "TN_PressurePlate.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class USoundBase;
class ATortugaCharacter;

/**
 * Modo de comportamiento de la placa.
 * Momentary: bOccupied=true sólo mientras haya alguien encima (default).
 * Latched:   tras la primera activación queda "pegada" hasta que se llame ResetLatch().
 */
UENUM(BlueprintType)
enum class EPressurePlateMode : uint8
{
	Momentary UMETA(DisplayName = "Momentary"),
	Latched   UMETA(DisplayName = "Latched")
};

/**
 * Placa de activación (#30).
 *
 * Cada placa detecta si tiene un jugador vivo encima.
 * Úsalas junto con ATN_PressurePlateGroupManager (o el BacklogItem #30 gestor)
 * para activar eventos cuando TODOS los jugadores vivos estén sobre placas.
 *
 * La lógica de "todos activados → evento" reside en TN_PressurePlateGroupManager.
 *
 * Uso:
 *   1. Crear BP_PressurePlate, asignar mesh, ajustar TriggerBox.
 *   2. Implementar OnPlatePressed / OnPlateReleased en BP para VFX.
 *   3. Referenciar las placas desde TN_PressurePlateGroupManager.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_PressurePlate : public AActor
{
	GENERATED_BODY()

public:
	ATN_PressurePlate();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** True si la placa cuenta como activa (en modo Latched puede ser sin nadie encima). */
	UFUNCTION(BlueprintPure, Category = "PressurePlate")
	bool IsOccupied() const { return bOccupied; }

	/**
	 * True si AHORA MISMO hay un jugador VIVO sobre la placa (recalculado en el acto).
	 * A diferencia de IsOccupied(), no depende del bOccupied replicado, que puede quedar
	 * pegado si un jugador muere encima sin generar EndOverlap (el ragdoll sigue solapando).
	 * La lógica de grupo debe usar esto para no dejar que un muerto mantenga la condición.
	 */
	bool HasLiveOccupant() const;

	/**
	 * Resetea manualmente una placa Latched. Solo authority.
	 * No-op en modo Momentary.
	 */
	UFUNCTION(BlueprintCallable, Category = "PressurePlate")
	void ResetLatch();

	/** Delegate servidor-only para que el GroupManager escuche cambios. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnOccupancyChanged, ATN_PressurePlate*, bool);
	FOnOccupancyChanged OnOccupancyChanged;

	/**
	 * Tag del ATN_PressurePlateGroupManager al que esta placa debe auto-registrarse.
	 * Útil cuando la placa está en un chunk y el manager está en el nivel.
	 * Dejar en NAME_None si el manager la referencia directamente por eyedropper.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PressurePlate")
	FName ManagerTag = NAME_None;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PressurePlate")
	TObjectPtr<UStaticMeshComponent> PlateMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PressurePlate")
	TObjectPtr<UBoxComponent> TriggerBox;

	/** Modo de comportamiento: Momentary (default) o Latched. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PressurePlate")
	EPressurePlateMode Mode = EPressurePlateMode::Momentary;

	/** Sonido al pisar la placa. */
	UPROPERTY(EditDefaultsOnly, Category = "PressurePlate|Audio")
	TObjectPtr<USoundBase> PressSound;

	/** Sonido al liberar la placa. */
	UPROPERTY(EditDefaultsOnly, Category = "PressurePlate|Audio")
	TObjectPtr<USoundBase> ReleaseSound;

protected:
	// Deferred self-registration with manager (run one tick after BeginPlay)
	void DeferredSelfRegisterWithManager();

private:
	UPROPERTY(ReplicatedUsing = OnRep_bOccupied)
	bool bOccupied = false;

	UFUNCTION()
	void OnRep_bOccupied();

	/** Conjunto de personajes vivos actualmente sobre la placa. */
	TSet<TWeakObjectPtr<ATortugaCharacter>> OccupyingCharacters;

	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void RefreshOccupancy();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnOccupancyChanged(bool bNewOccupied);
};

/**
 * Gestor de grupo de placas (#30).
 *
 * Monitorea un array de ATN_PressurePlate.
 * Condición de activación: PlayersOnPlates == NumAlivePlayers.
 *   (Todos los jugadores vivos deben estar encima de UNA placa — se permiten
 *    varias en la misma placa si hay menos vivos que placas.)
 *
 * Si HoldDurationRequired > 0: todos deben mantener la condición ese tiempo.
 * Al cumplirse: dispara TriggerActions (igual que TN_ButtonGroupManager).
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_PressurePlateGroupManager : public AActor
{
	GENERATED_BODY()

public:
	ATN_PressurePlateGroupManager();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlateGroup")
	TArray<TObjectPtr<ATN_PressurePlate>> ManagedPlates;

	/**
	 * Tags para descubrir placas automáticamente en BeginPlay.
	 * Complementa ManagedPlates para cubrir placas en chunks que no se pueden
	 * asignar por eyedropper. Combinado con auto-registro desde la propia placa.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlateGroup")
	TArray<FName> ManagedPlateTags;

	/**
	 * Acciones aplicadas cuando la condición se cumple.
	 * Reusa el struct FTN_TransformAction del ButtonGroupManager.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlateGroup")
	TArray<FTN_TransformAction> TriggerActions;

	/** Segundos que los jugadores deben mantener todas las placas activas para disparar. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PlateGroup",
		meta = (ClampMin = "0.0"))
	float HoldDurationRequired = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PlateGroup")
	bool bOneShot = true;

	/**
	 * Nº mínimo de placas ocupadas para disparar. -1 (default) = tantas como
	 * jugadores vivos (comportamiento original).
	 * Ej: 4 placas, Threshold=2 → basta con 2 ocupadas aunque haya 4 jugadores vivos.
	 * Se clampea a [1, ManagedPlates.Num()].
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PlateGroup")
	int32 TriggerThreshold = -1;

	/** Sonido al cumplirse la condición de todas las placas. */
	UPROPERTY(EditDefaultsOnly, Category = "PlateGroup|Audio")
	TObjectPtr<USoundBase> ConditionMetSound;

	/** Sonido al perderse la condición. */
	UPROPERTY(EditDefaultsOnly, Category = "PlateGroup|Audio")
	TObjectPtr<USoundBase> ConditionLostSound;

public:
	/**
	 * Registra una placa con este gestor en runtime (llamado por ATN_PressurePlate
	 * cuando tiene ManagerTag configurado). Idempotente: ignora duplicados.
	 * Solo tiene efecto en el servidor.
	 */
	void RegisterPlate(ATN_PressurePlate* Plate);

private:
	bool bTriggered    = false;
	bool bConditionMet = false;

	FTimerHandle HoldTimerHandle;

	void OnOccupancyChanged(ATN_PressurePlate* Plate, bool bOccupied);
	void EvaluateAndHandleCondition();
	bool EvaluateCondition() const;
	void OnHoldTimerExpired();
	void ApplyTriggerActions();

	int32 GetEffectiveThreshold(int32 AlivePlayers) const;

	/** Scan diferido (un tick después de BeginPlay) para registrar placas por tag. */
	void DeferredTagPlateScan();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastNotifyConditionChange(bool bMet);
};
