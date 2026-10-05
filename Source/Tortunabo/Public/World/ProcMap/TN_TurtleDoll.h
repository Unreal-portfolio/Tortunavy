#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_TurtleDoll.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class USoundBase;

/** Reglas puras del muñeco tortuga (#797): se prueban en Tortunabo.ProcMap.Dolls. */
namespace TNTurtleDollRules
{
	/** Cada jugadora coge cada muñeco una vez: hace falta un PlayerId válido, estar en juego y no haberlo cogido ya. */
	inline bool CanCollect(TConstArrayView<int32> CollectorIds, int32 PlayerId, bool bAliveAndPlaying)
	{
		return PlayerId != INDEX_NONE && bAliveAndPlaying && !CollectorIds.Contains(PlayerId);
	}
}

/**
 * Muñeco tortuga (#797): coleccionable del nivel del Coop que cuenta en la puntuación final (TN_CoopScore.h). Lo pone el
 * generador del mapa (TNProcMap::PlanTurtleDolls) solo en el servidor y se replica.
 *
 *  - Uno por recogida y por jugadora: cada una puede coger cada muñeco una vez. El servidor decide (CanCollect), suma el
 *    muñeco a ATN_CoopPlayerState::TurtleDollsCollected y apunta a la jugadora en CollectorIds (replicado).
 *  - Para quien ya lo ha cogido, el muñeco desaparece en su pantalla; las demás lo siguen viendo.
 *  - Arte de código (sin .uasset): una figurita de tortuga sobre una peana dorada que flota y gira (TN_TurtleDollMesh.h).
 *  - No toca la economía de conchas (RaceScore ni el perfil de conchas).
 */
UCLASS()
class TORTUNABO_API ATN_TurtleDoll : public AActor
{
	GENERATED_BODY()

public:
	ATN_TurtleDoll();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** True si la jugadora PlayerId ya ha cogido este muñeco. */
	bool HasCollected(int32 PlayerId) const { return CollectorIds.Contains(PlayerId); }

	/** Muñecos vivos en el mundo (los del mapa actual): el total del nivel para la puntuación. */
	static int32 CountInWorld(const UWorld* World);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TurtleDoll")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TurtleDoll")
	TObjectPtr<USphereComponent> Trigger;

	/** Figurita (malla de código, sin colisión): flota y gira en cada máquina. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TurtleDoll")
	TObjectPtr<UStaticMeshComponent> Visual;

	/** Sonido para quien lo coge (opcional): suena en su máquina al recogerlo. */
	UPROPERTY(EditDefaultsOnly, Category = "TurtleDoll|Audio")
	TObjectPtr<USoundBase> PickupSound;

	/** Vueltas por segundo de la figurita. */
	UPROPERTY(EditDefaultsOnly, Category = "TurtleDoll", meta = (ClampMin = "0.0"))
	float SpinTurnsPerSecond = 0.35f;

	/** Amplitud (cm) del vaivén vertical de la figurita. */
	UPROPERTY(EditDefaultsOnly, Category = "TurtleDoll", meta = (ClampMin = "0.0"))
	float BobAmplitude = 8.f;

private:
	/** PlayerId de las jugadoras que ya lo han cogido. */
	UPROPERTY(ReplicatedUsing = OnRep_CollectorIds)
	TArray<int32> CollectorIds;

	UFUNCTION()
	void OnRep_CollectorIds();

	/** Oculta la figurita si la jugadora de esta máquina ya lo ha cogido (suena PickupSound la primera vez). */
	void ApplyLocalVisibility();

	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	bool bLocallyCollected = false;
	float AnimTime = 0.f;
};
