#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_ProcEggNest.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UStaticMesh;
class USoundBase;

/**
 * Pila de huevos: punto de respawn del mapa procedural (como la del centro de la
 * lobby). Al pasar junto a ella queda activada para el jugador / equipo, y quien
 * muera después reaparece en la más avanzada que tenga activada.
 *
 * Replicada (el estado activado se ve en todos). La activación la decide el
 * servidor y la registra el GameMode del mapa procedural.
 *
 * Arte de código (#50): sin NestMeshOverride, la base es un nido de arena con un
 * aro de paja y los ocho huevos son el de la pila del lobby (TNCastleKit), con la
 * cáscara IdleColor o ActiveColor y una banda de color distinta en cada uno.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_ProcEggNest : public AActor
{
	GENERATED_BODY()

public:
	ATN_ProcEggNest();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Orden a lo largo del camino (0 = salida). */
	UFUNCTION(BlueprintPure, Category = "EggNest")
	int32 GetNestOrder() const { return NestOrder; }

	/** Progreso (cm) del camino principal en el que está la pila. */
	UFUNCTION(BlueprintPure, Category = "EggNest")
	float GetPathProgress() const { return PathProgress; }

	void InitNest(int32 InOrder, float InProgress) { NestOrder = InOrder; PathProgress = InProgress; }

	/** Punto de reaparición alrededor de la pila (slot 0..N). */
	FTransform GetRespawnTransform(int32 Slot) const;

	/** Servidor: marca la pila como alcanzada (visual replicado). */
	void MarkActivated();

	UFUNCTION(BlueprintPure, Category = "EggNest")
	bool IsActivated() const { return bActivated; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EggNest")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EggNest")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EggNest")
	TObjectPtr<UStaticMeshComponent> NestBase;

	/** Huevos de la pila (arte de código); se ocultan si se asigna NestMeshOverride. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EggNest")
	TArray<TObjectPtr<UStaticMeshComponent>> Eggs;

	/** Colisión de la peana: un cilindro invisible bajo el nido (la malla de código no trae colisión simple). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EggNest")
	TObjectPtr<UStaticMeshComponent> PedestalCollision;

	/** Colisión de la pila de huevos: un cono invisible que los envuelve (sin huecos donde encajarse). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EggNest")
	TObjectPtr<UStaticMeshComponent> EggsCollision;

	/** Mesh definitivo de la pila (el de la lobby). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EggNest")
	TObjectPtr<UStaticMesh> NestMeshOverride;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EggNest")
	TObjectPtr<USoundBase> ActivateSound;

	/** Cáscara (lineal) de los huevos de código antes de alcanzar la pila. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EggNest")
	FLinearColor IdleColor = FLinearColor(0.85f, 0.82f, 0.7f);

	/** Cáscara (lineal) de los huevos de código con la pila activada. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EggNest")
	FLinearColor ActiveColor = FLinearColor(1.f, 0.85f, 0.25f);

	/** Escala de los huevos de código respecto al huevo de 2,4 m de la pila del lobby. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EggNest|Art", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float CodeArtEggScale = 0.3f;

	/** Evento BP al activarse (brillo, partículas...). */
	UFUNCTION(BlueprintImplementableEvent, Category = "EggNest")
	void OnNestActivated();

private:
	UPROPERTY(Replicated)
	int32 NestOrder = 0;

	UPROPERTY(Replicated)
	float PathProgress = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_Activated)
	bool bActivated = false;

	UFUNCTION()
	void OnRep_Activated();

	/** True si la base y los huevos llevan el arte de código (sin NestMeshOverride y fuera del servidor dedicado). */
	UPROPERTY(Transient)
	bool bCodeArt = false;

	/** Pone el nido y los huevos de código en NestBase y Eggs, y coloca los huevos en la pila. */
	void BuildCodeArt();

	void ApplyVisual();

	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
