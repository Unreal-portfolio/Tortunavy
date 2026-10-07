#pragma once

#include "CoreMinimal.h"
#include "World/TN_DirectInteractableBase.h"
#include "TN_UmbrellaInteractable.generated.h"

class ATortugaCharacter;
class USoundBase;
class UParticleSystem;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Sombrilla interactuable (#29).
 *
 * Comportamiento de toggle:
 *   - Pulsar E → abre la sombrilla (activa protección, inicia timer 8s).
 *   - Pulsar E con la sombrilla abierta → la cierra manualmente.
 *   - Al cerrarse (manual o por timer) → puede reabrirse instantáneamente.
 *   - El timer de 8s siempre corre mientras está abierta.
 *
 * Mesh visual (compound sin assets personalizados):
 *   - Palo (Mesh heredado): cilindro fino 0→200 cm.
 *   - Mango (HandleMeshComp): cilindro más grueso 100→240 cm.
 *   - Cúpula (CanopyMeshComp): cono achatado invertido, r=150 cm, solo visible abierta.
 *   - Si se asignan MeshClosed + MeshOpen en el BP hijo, se usan y se ocultan
 *     los componentes compound.
 *
 * Protección activa (bHasUmbrellaProtection en TortugaCharacter):
 *   - La gaviota dinámica (TN_EnemySeagull) ya comprueba el flag.
 *   - La caca (TN_SeagullDroppingActor) también lo comprueba en ResolveImpact.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_UmbrellaInteractable : public ATN_DirectInteractableBase
{
	GENERATED_BODY()

public:
	ATN_UmbrellaInteractable();

	virtual void BeginPlay() override;
	virtual void Interact(APawn* Interactor) override;
	virtual bool CanInteract(APawn* Interactor) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** Segundos de protección contra gaviota tras abrir la sombrilla. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Umbrella", meta = (ClampMin = "1.0"))
	float UmbrellaDurationSeconds = 8.f;

	// ── Componentes compound (activos cuando NO se asignan assets personalizados) ─

	/** Mango: cilindro más grueso, desde mitad del palo hasta arriba. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Umbrella|Mesh")
	TObjectPtr<UStaticMeshComponent> HandleMeshComp;

	/** Cúpula: cono achatado invertido en la punta del palo, solo visible abierta. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Umbrella|Mesh")
	TObjectPtr<UStaticMeshComponent> CanopyMeshComp;

	// ── Assets opcionales de override ────────────────────────────────────────────
	// Si ambos se asignan en el BP hijo se usan los assets personalizados y los
	// componentes compound se ocultan. Si alguno falta → modo compound.

	/** Mesh cuando la sombrilla está plegada/cerrada. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Umbrella|Mesh")
	TObjectPtr<UStaticMesh> MeshClosed;

	/** Mesh cuando la sombrilla está desplegada/abierta. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Umbrella|Mesh")
	TObjectPtr<UStaticMesh> MeshOpen;

	// ── Audio/VFX ─────────────────────────────────────────────────────────────────

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Umbrella|Audio")
	TObjectPtr<USoundBase> SoundOpen;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Umbrella|Audio")
	TObjectPtr<USoundBase> SoundClose;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Umbrella|FX")
	TObjectPtr<UParticleSystem> VFXOpen;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Umbrella|FX")
	TObjectPtr<UParticleSystem> VFXClose;

	// ── Eventos BP opcionales ─────────────────────────────────────────────────────

	/** Llamado en TODAS las máquinas cuando la sombrilla se abre. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Umbrella")
	void OnUmbrellaOpened(APawn* User);

	/** Llamado en TODAS las máquinas cuando la sombrilla se cierra. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Umbrella")
	void OnUmbrellaClosed();

private:
	/** Solo efectos (sonido, VFX, evento BP): el estado visual lo lleva bIsOpen replicado. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastOnUmbrellaOpened(APawn* User);

	/** Solo efectos (sonido, VFX, evento BP): el estado visual lo lleva bIsOpen replicado. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastOnUmbrellaClosed();

	/** Cliente: aplica el estado abierto/cerrado replicado (también a quien entra tarde, #893). */
	UFUNCTION()
	void OnRep_IsOpen();

	/** Servidor: despierta el actor durmiente, cambia bIsOpen y aplica el estado en el anfitrión. */
	void SetOpenState(bool bOpen);

	/** Timer de auto-cierre (8 s). */
	void HandleUmbrellaExpired();

	/**
	 * Aplica el estado visual (abierto/cerrado) en la máquina local.
	 * — Si MeshClosed && MeshOpen están asignados: single-mesh mode.
	 * — En caso contrario: compound mode (palo + mango + cúpula).
	 */
	void ApplyUmbrellaState(bool bOpen);

	FTimerHandle UmbrellaActiveTimerHandle;
	TWeakObjectPtr<ATortugaCharacter> ProtectedCharacter;

	/** true mientras la protección está activa (sombrilla abierta). Replicado (#893). */
	UPROPERTY(ReplicatedUsing = OnRep_IsOpen)
	bool bIsOpen = false;
};
