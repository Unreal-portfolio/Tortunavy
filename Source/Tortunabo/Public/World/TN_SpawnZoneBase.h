#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_SpawnZoneBase.generated.h"

class UBoxComponent;
class ATortugaCharacter;

/**
 * Base común para zonas de spawn periódico dirigidas a jugadores dentro de un
 * volumen (gaviotas #11, cacas #12). Server-only, sin replicación propia.
 *
 * Centraliza lo que antes estaba DUPLICADO (y ya divergiendo) entre
 * ATN_SeagullSpawnZone y ATN_DroppingSpawnZone: el volumen de detección, el
 * ciclo del timer, la selección de jugadores vivos dentro de la caja, el clamp
 * al AABB y el tracking de instancias activas con tope MaxConcurrent.
 *
 * Las subclases solo implementan TrySpawn(): eligen qué spawnear y sobre quién,
 * apoyándose en GetLivingPlayersInside/ClampXYToVolume/RegisterActive/GetActiveCount.
 */
UCLASS(Abstract)
class TORTUNABO_API ATN_SpawnZoneBase : public AActor
{
	GENERATED_BODY()

public:
	ATN_SpawnZoneBase();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	/** Volumen que define el área de detección de jugadores para el spawn. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpawnZone")
	TObjectPtr<UBoxComponent> SpawnVolume;

	/** Intervalo entre intentos de spawn (s). Editable por instancia. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpawnZone", meta = (ClampMin = "1.0"))
	float SpawnInterval = 10.f;

	/** Retardo inicial antes del primer intento (s). Editable por instancia. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpawnZone", meta = (ClampMin = "0.0"))
	float InitialDelay = 3.f;

	/** Número máximo de instancias activas simultáneas originadas por esta zona. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnZone", meta = (ClampMin = "1", ClampMax = "20"))
	int32 MaxConcurrent = 2;

	/**
	 * @brief Intento de spawn. Lo llama el timer cada SpawnInterval en el servidor.
	 *        La subclase decide qué spawnear y sobre quién usando los helpers de esta base.
	 */
	virtual void TrySpawn() PURE_VIRTUAL(ATN_SpawnZoneBase::TrySpawn, );

	/** @brief Rellena OutPlayers con los jugadores vivos (no eliminados) dentro del volumen. */
	void GetLivingPlayersInside(TArray<ATortugaCharacter*>& OutPlayers) const;

	/** @brief Clampea WorldLoc al AABB XY del volumen. Preserva la Z original. */
	FVector ClampXYToVolume(const FVector& WorldLoc) const;

	/** @brief Registra una instancia recién spawneada para el conteo de MaxConcurrent. */
	void RegisterActive(AActor* Instance);

	/** @brief Nº de instancias activas vivas de esta zona (limpia punteros caducados). */
	int32 GetActiveCount();

private:
	FTimerHandle SpawnTimerHandle;

	/** Instancias vivas spawneadas por esta zona (punteros débiles — se limpian solos). */
	TArray<TWeakObjectPtr<AActor>> ActiveInstances;
};
