#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TN_MapBlockerRelevance.generated.h"

class AActor;

/**
 * Servidor (#828): todo actor replicado del mapa que bloquea el paso (trampas, estructuras, muros, compuertas, puentes,
 * plataformas) lo tiene cada cliente desde que aparece. Con la relevancia por distancia, un actor así llegaba al cliente al
 * acercarse y, mientras, el servidor ya chocaba con él: un muro invisible. Vale para todos los modos y mapas.
 *
 * Al final del fotograma en que aparece (con su colisión ya puesta), un actor replicado creado en partida, quieto (sin
 * movimiento replicado ni física), que no es un pawn ni lo lleva nadie y con algún componente que bloquea, pasa a ser
 * siempre relevante. Los quietos de verdad además duermen (los elementos de la playa, ATN_BeachElement), así que no cuestan
 * red; el resto se queda con su frecuencia adaptativa. Los del nivel ya los carga cada máquina con el mapa.
 */
UCLASS()
class TORTUNABO_API UTN_MapBlockerRelevanceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** ¿Hay que hacerlo siempre relevante? (replicado, creado en partida, quieto, del mapa y bloquea). */
	static bool ShouldBeAlwaysRelevant(const AActor* Actor);

	/** Lo hace siempre relevante si ShouldBeAlwaysRelevant; devuelve si lo ha cambiado. */
	static bool KeepRelevant(AActor* Actor);

private:
	void OnActorSpawned(AActor* Actor);
	void OnPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	TArray<TWeakObjectPtr<AActor>> Pending;
	FDelegateHandle SpawnedHandle;
	FDelegateHandle PostTickHandle;
	int32 MadeRelevant = 0;
};
