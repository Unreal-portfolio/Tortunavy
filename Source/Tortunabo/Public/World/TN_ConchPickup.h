#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/TN_InventoryTypes.h"
#include "TN_ConchPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UStaticMesh;
class USoundBase;
class UNiagaraSystem;
class ATortugaCharacter;
class ATN_PickupInteractableBase;

/**
 * Concha marina — trampa pasiva y reciclable (#22, #568).
 *
 * SIN COLOCAR (bIsPlacedTrap = false, estado por defecto):
 *   No hace nada al pisarla. Lo que se recoge del suelo es un ATN_PickupInteractableBase (con E), no esta clase.
 *
 * MODO TRAMPA (bIsPlacedTrap = true):
 *   Al pisar la concha → inmoviliza al jugador durante TrapDurationSeconds.
 *   Tras el timer: restaura MOVE_Walking.
 *   Con bDestroyAfterActivation, al gastarse deja en su sitio un pickup de PickupActorClass con el ítem que se gastó
 *   al colocarla (SetRecycledItem): quien lo coja con E vuelve a tener la concha.
 *
 * PlaceAsTrap(Location) se llama desde el sistema de ítems cuando el jugador
 * equipa la concha y pulsa Interact.
 *
 * Replicación:
 *   bReplicates = true, bAlwaysRelevant = true.
 *   bIsPlacedTrap se replica para que los clientes muestren el visual correcto.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_ConchPickup : public AActor
{
	GENERATED_BODY()

public:
	ATN_ConchPickup();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Coloca la concha como trampa en la posición indicada.
	 * Debe llamarse en el servidor (lo invoca el sistema de ítems).
	 */
	UFUNCTION(BlueprintCallable, Category = "Conch")
	void PlaceAsTrap(const FVector& WorldLocation);

	/**
	 * Servidor: el ítem de inventario que se gastó al colocar esta concha (lo pasa HandleUseConch antes de PlaceAsTrap).
	 * Al gastarse la trampa se devuelve al mundo como un pickup de Item.PickupActorClass; sin ítem o sin esa clase, la
	 * concha no se recicla.
	 */
	void SetRecycledItem(const FTN_InventoryItem& Item);

	/**
	 * Servidor: deja en el sitio de la concha el pickup reciclado (ver SetRecycledItem) y la destruye con SetLifeSpan.
	 * Devuelve el pickup creado, o nullptr si no había nada que reciclar.
	 */
	ATN_PickupInteractableBase* SpawnRecycledPickup();

protected:
	// ── Componentes ──────────────────────────────────────────────────────────

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Conch")
	TObjectPtr<UStaticMeshComponent> ConchMesh;

	/** Zona de detección de jugadores. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Conch")
	TObjectPtr<USphereComponent> OverlapSphere;

	// ── Config ────────────────────────────────────────────────────────────────

	/** Segundos que el jugador queda inmovilizado al pisar la trampa. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Conch",
		meta = (ClampMin = "0.1"))
	float TrapDurationSeconds = 2.5f;

	/** Radio de la esfera de detección (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Conch",
		meta = (ClampMin = "10.0"))
	float TrapRadius = 60.f;

	/**
	 * Segundos tras liberar a la víctima antes de que la trampa vuelva a poder atrapar.
	 * Solo se aplica si bDestroyAfterActivation=false. Evita que la misma víctima
	 * se re-atrape inmediatamente al salir del inmovilizado.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Conch",
		meta = (ClampMin = "0.0", EditCondition = "!bDestroyAfterActivation"))
	float ResetCooldownSeconds = 1.0f;

	/**
	 * true  → la trampa se consume tras atrapar a su primera víctima (one-shot).
	 *         La concha se destruye cuando expira el immobilize → nunca más podrá
	 *         atrapar ni ser confundida con un pickup por el sistema externo.
	 * false → la trampa se re-arma tras ResetCooldownSeconds y persiste en el mapa.
	 *
	 * Default true: respeta el balance original del ítem (un uso por concha equipada).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Conch")
	bool bDestroyAfterActivation = true;

	// ── Estado replicado ──────────────────────────────────────────────────────

	/**
	 * true  → la concha está en el suelo como trampa activa.
	 * false → la concha está en modo ítem recogible.
	 * Se replica para que los clientes puedan actualizar el visual.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_IsPlacedTrap, BlueprintReadOnly, Category = "Conch")
	bool bIsPlacedTrap = false;

	// ── Meshes de estado ──────────────────────────────────────────────────────
	// Si se dejan vacíos el ConchMesh no tendrá mesh visual por defecto.
	// Asignar en el BP hijo para ver la concha en el nivel.

	/**
	 * Mesh cuando la concha está en modo ítem o trampa sin activar.
	 * Se aplica al colocar la trampa y al rearmarse.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Conch|Mesh")
	TObjectPtr<UStaticMesh> MeshNormal;

	/**
	 * Mesh cuando la trampa ha capturado a alguien (trampa activada).
	 * Se aplica vía Multicast cuando un jugador pisa la concha.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Conch|Mesh")
	TObjectPtr<UStaticMesh> MeshTriggered;

	// ── Audio / VFX ───────────────────────────────────────────────────────────

	/** Sonido al colocar la trampa. */
	UPROPERTY(EditDefaultsOnly, Category = "Conch|Audio")
	TObjectPtr<USoundBase> PlaceSound;

	/** Sonido al atrapar a un jugador. */
	UPROPERTY(EditDefaultsOnly, Category = "Conch|Audio")
	TObjectPtr<USoundBase> TrapSound;

	/** VFX al colocar la trampa. */
	UPROPERTY(EditDefaultsOnly, Category = "Conch|VFX")
	TObjectPtr<UNiagaraSystem> PlaceVFX;

	/** VFX al atrapar a un jugador. */
	UPROPERTY(EditDefaultsOnly, Category = "Conch|VFX")
	TObjectPtr<UNiagaraSystem> TrapVFX;

private:
	// ── Overlap ───────────────────────────────────────────────────────────────

	UFUNCTION()
	void OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	// ── Replicación ───────────────────────────────────────────────────────────

	UFUNCTION()
	void OnRep_IsPlacedTrap();

	// ── Trampa ────────────────────────────────────────────────────────────────

	/** Envía el evento VFX a todos los clientes cuando la trampa se activa. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastOnTrapped(APawn* Victim);

	/** Restaura el movimiento del jugador tras TrapDurationSeconds. */
	void RestoreMovement(TWeakObjectPtr<ATortugaCharacter> WeakCharacter);

	/** Devuelve a andar a la víctima inmovilizada (MOVE_None → MOVE_Walking). No recicla ni rearma la concha. */
	static void ReleaseVictim(ATortugaCharacter* Victim);

	/** Reproduce sonido y VFX de colocación de trampa en la máquina local. */
	void PlayPlaceEffects();

	/** Re-arma la trampa (bTrapUsed=false) una vez expira el cooldown. */
	void RearmTrap();

	/**
	 * Modo persistente: re-arma tras ResetCooldownSeconds (o inmediatamente si
	 * es 0). Compartido por el modo trampa-vs-enemigo y por RestoreMovement
	 * cuando bDestroyAfterActivation es false.
	 */
	void ScheduleRearm();

	FTimerHandle TrapTimerHandle;
	FTimerHandle RearmTimerHandle;

	/**
	 * Servidor: la tortuga inmovilizada mientras corre TrapTimerHandle. Si la concha desaparece antes (fin de ronda o de
	 * nivel, #569), EndPlay la suelta: sin esto se quedaba en MOVE_None para siempre.
	 */
	TWeakObjectPtr<ATortugaCharacter> TrappedVictim;

	/** Evita que la trampa se active dos veces mientras el personaje sigue en overlap. */
	bool bTrapUsed = false;

	/** Ítem que vuelve al mundo como pickup al gastarse la trampa (solo servidor, no se replica). */
	UPROPERTY(Transient)
	FTN_InventoryItem RecycledItem;
};
