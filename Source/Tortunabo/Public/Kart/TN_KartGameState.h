// Estado replicado de los karts en el mapa generado del cooperativo (#291): el de ATN_RallyGameState (fase, puestos,
// horas del servidor) más la generación del mapa con la que se hizo la pista, la dificultad y la semilla. La pista no es
// una variante del Rally: PrepareTrack la hace con el camino del generador (ATN_KartTrack). En el servidor la pide
// ATN_RallyGameMode::StartPlay (y antes genera el mapa ATN_KartGameMode); cada cliente la construye con su propio
// generador (que genera el mismo mapa con la semilla replicada) en cuanto lo tiene.
#pragma once

#include "CoreMinimal.h"
#include "Rally/TN_RallyGameState.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "TN_KartGameState.generated.h"

class ATN_KartTrack;
class ATN_ProcMapGenerator;

UCLASS()
class TORTUNABO_API ATN_KartGameState : public ATN_RallyGameState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Servidor: genera el mapa (ATN_KartGameMode::GenerateMap) y hace la pista con él. Cliente: la hace si ya tiene el mapa. */
	virtual ATN_RallyTrack* PrepareTrack(FName InVariant) override;

	/** Al acabar, todas vuelven al lobby (ATN_KartGameMode::ReturnToLobbyNow). */
	virtual bool ReturnsToLobbyAfterResults() const override { return true; }

	/** Generación del mapa (ATN_ProcMapGenerator) con la que el servidor hizo la pista; 0 = aún ninguna. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MapGeneration, Category = "Karts")
	int32 MapGeneration = 0;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Karts")
	ETNProcDifficulty Difficulty = ETNProcDifficulty::Normal;

	/** Semilla del mapa (para el menú de pausa y para repetirlo con ?ProcSeed=). */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Karts")
	int32 MapSeed = 0;

	/** Pista de esta máquina como pista de karts (nullptr si aún no está). */
	ATN_KartTrack* GetKartTrack() const;

	/** El generador del nivel (nullptr si aún no ha llegado en un cliente). */
	ATN_ProcMapGenerator* FindGenerator() const;

	/**
	 * Esta máquina tiene la pista de la generación replicada y colisión del terreno en la parrilla y a lo largo de la pista
	 * (muestras cada 250 m): los karts no atraviesan el suelo. Lo mira el GameMode (servidor) y el PlayerController local de
	 * cada cliente antes de avisar al servidor.
	 */
	bool IsLocalTrackPlayable() const;

private:
	UFUNCTION()
	void OnRep_MapGeneration();

	/** Cliente: si su generador ya tiene la generación que pide el servidor, construye la pista. */
	void TryBuildClientTrack();
	void HandleMapGenerated(int32 Generation);
	ATN_KartTrack* FindOrSpawnKartTrack();

	TWeakObjectPtr<ATN_ProcMapGenerator> BoundGenerator;
	FDelegateHandle MapGeneratedHandle;
	FTimerHandle RetryHandle;
	/** Colisión comprobada en toda la pista (no vuelve a trazar). */
	mutable bool bCollisionConfirmed = false;
	mutable double NextCollisionProbeTime = 0.0;
};
