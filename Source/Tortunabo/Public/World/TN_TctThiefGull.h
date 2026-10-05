#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/TN_InventoryTypes.h"
#include "TN_TctThiefGull.generated.h"

class ATortugaCharacter;
class USceneComponent;
class UStaticMeshComponent;

/** Por dónde va la gaviota ladrona. */
UENUM()
enum class ETNTctGullPhase : uint8
{
	/** Vuela hacia la víctima. */
	Outbound,
	/** Vuelve con lo robado a quien la lanzó. */
	Return,
	/** Se va (sin nada que entregar). */
	Leave
};

/**
 * Gaviota ladrona de Todos contra Todos (#714): la gaviota de la fauna (la de las zonas de gaviotas, pequeña) que vuela hasta
 * la tortuga más cercana a menos de 20 m (TNTctItemRules::PickThiefVictim), le quita el objeto de la mano y se lo trae a quien
 * la lanzó (en la mano si la tiene libre; si no, lo deja en el suelo a sus pies). Si la víctima no lleva nada en la mano, la
 * marea TNTctItemTuning::ThiefDizzySeconds y se va.
 *
 * Red: el servidor decide a quién va, mueve la gaviota (movimiento replicado) y hace el robo y la entrega con el inventario de
 * siempre (replicado). Lo que lleva en el pico (Carried) se replica para que todas las máquinas lo vean colgando.
 */
UCLASS()
class TORTUNABO_API ATN_TctThiefGull : public AActor
{
	GENERATED_BODY()

public:
	ATN_TctThiefGull();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Servidor: Thief lanza una gaviota ladrona. false (sin crear nada) si no hay nadie a quien robar. */
	static bool ServerLaunch(ATortugaCharacter* Thief);

	ETNTctGullPhase GetPhase() const { return Phase; }

private:
	/** Lo que lleva en el pico (vacío si nada). */
	UPROPERTY(ReplicatedUsing = OnRep_Carried)
	FTN_InventoryItem Carried;

	UFUNCTION()
	void OnRep_Carried();

	UPROPERTY(VisibleAnywhere, Category = "Tct")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> GullRoot;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> GullParts;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> CarriedMesh;

	// ── Servidor ──
	void ServerTick(float DeltaSeconds);
	/** Llega a la víctima: le quita el objeto de la mano o, si no lleva, la marea. */
	void ServerSteal(ATortugaCharacter* Target);
	/** Llega a quien la lanzó: le da lo robado. */
	void ServerDeliver(ATortugaCharacter* Launcher);
	/** Vuela hacia Target (llega más baja cuanto más cerca); true si ya está a su alcance. */
	bool FlyTowards(const FVector& Target, float DeltaSeconds);
	void StartLeaving();

	// ── Máquinas con pantalla ──
	void BuildVisuals();
	void PoseVisuals();
	void ShowCarried();

	TWeakObjectPtr<ATortugaCharacter> Thief;
	TWeakObjectPtr<ATortugaCharacter> Victim;
	ETNTctGullPhase Phase = ETNTctGullPhase::Outbound;
	float Age = 0.f;
	float LeaveAge = 0.f;
	FVector LeaveDirection = FVector::ForwardVector;
	TArray<FVector> PartPivots;
	int32 WingLeftPart = INDEX_NONE;
	int32 WingRightPart = INDEX_NONE;
};
