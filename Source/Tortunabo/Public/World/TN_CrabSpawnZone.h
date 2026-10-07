#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_CrabSpawnZone.generated.h"

class UBoxComponent;
class ATN_CrabActor;

/**
 * Zona de spawn perezoso de cangrejos (#10 — lazy spawn).
 *
 * El cangrejo NO existe en el mundo hasta que un jugador entra al volumen.
 * Al detectar presencia, el servidor spawnea uno y desactiva el volumen (one-shot).
 * El cangrejo spawneado se inicializa desde la posición del spawner,
 * con PatrolPoints en offsets relativos — compatible con chunks.
 *
 * Uso:
 *   Colocar BP_CrabSpawnZone en el nivel.
 *   Asignar CrabClass → BP_CrabActor.
 *   Opcionalmente ajustar SpawnOffset y PatrolPoints del cangrejo en CrabClass.
 *
 * Solo activo en servidor (overlap + spawn).
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_CrabSpawnZone : public AActor
{
	GENERATED_BODY()

public:
	ATN_CrabSpawnZone();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	/** Volumen de proximidad. Cuando un jugador entra, se spawnea el cangrejo. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CrabSpawnZone")
	TObjectPtr<UBoxComponent> ProximityVolume;

	/** Clase del cangrejo a spawnear. Asignar BP_CrabActor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CrabSpawnZone")
	TSubclassOf<ATN_CrabActor> CrabClass;

	/**
	 * Offset de spawn del cangrejo respecto al spawner (espacio local).
	 * (0,0,0) → spawnea en la posición del spawner.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrabSpawnZone")
	FVector SpawnOffset = FVector::ZeroVector;

	/**
	 * Número de cangrejos spawneados cada vez que un jugador entra en la zona.
	 * Se spawnean en posiciones aleatorias dentro del ProximityVolume.
	 * Default 3. Serializable por instancia.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrabSpawnZone", meta = (ClampMin = "1", ClampMax = "20"))
	int32 SpawnCountOnEnter = 5;

	/**
	 * Si true (default), el volumen se desactiva tras el primer spawn.
	 * Evita múltiples oleadas desde la misma zona.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrabSpawnZone")
	bool bOneShot = true;

private:
	bool bAlreadySpawned = false;

	/** Cangrejos vivos spawneados por esta zona — se destruyen con ella (EndPlay)
	 *  para que no queden huérfanos cuando el chunk se recicla. */
	TArray<TWeakObjectPtr<AActor>> SpawnedCrabs;

	UFUNCTION()
	void OnProximityBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	void SpawnCrab();
};
