#pragma once

#include "CoreMinimal.h"
#include "Core/TN_CoopGameState.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "TN_ProcMapGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnProcRoundInfoChanged);

/**
 * @brief GameState del mapa procedural: lo de ATN_CoopGameState más el estado de
 *        rondas que el HUD necesita (modo, ronda, objetivo, resultado de la ronda).
 *
 * Las victorias por jugador viven en ATN_CoopPlayerState::RoundWins y la pareja
 * de cada ronda del 2vs2 en ATN_CoopPlayerState::TeamIndex.
 */
UCLASS()
class TORTUNABO_API ATN_ProcMapGameState : public ATN_CoopGameState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RoundInfo, Category = "ProcMap")
	ETNProcGameMode ProcMode = ETNProcGameMode::Coop;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RoundInfo, Category = "ProcMap")
	ETNProcDifficulty ProcDifficulty = ETNProcDifficulty::Normal;

	/** Ronda en curso, desde 1. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RoundInfo, Category = "ProcMap")
	int32 CurrentRound = 0;

	/** Coop: rondas que se juegan. Carrera y 2vs2: victorias para ganar la partida. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RoundInfo, Category = "ProcMap")
	int32 RoundTarget = 1;

	/** true mientras se juega la ronda (no durante la generación ni el reparto). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RoundInfo, Category = "ProcMap")
	bool bRoundInProgress = false;

	/** Resultado de la última ronda ("Gana X", "Gana la pareja A y B"...). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RoundInfo, Category = "ProcMap")
	FString RoundResultText;

	/** Semilla del mapa actual (para reproducir un mapa concreto). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RoundInfo, Category = "ProcMap")
	int32 MapSeed = 0;

	/** Minutos estimados para recorrer el camino principal del mapa actual. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RoundInfo, Category = "ProcMap")
	float EstimatedMinutes = 0.f;

	/**
	 * Coop: muñecos tortuga puestos en la partida (#797), sumando los mapas de todas las rondas. Con
	 * ATN_CoopPlayerState::TurtleDollsCollected da el «recogidos / total» de la puntuación final.
	 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RoundInfo, Category = "ProcMap")
	int32 TurtleDollsTotal = 0;

	/** Se dispara en todas las máquinas cuando cambia cualquiera de los datos de ronda. */
	UPROPERTY(BlueprintAssignable, Category = "ProcMap")
	FOnProcRoundInfoChanged OnRoundInfoChanged;

	/** Servidor: el OnRep no corre en el host, así que el GameMode avisa a mano. */
	void NotifyRoundInfoChanged() { OnRoundInfoChanged.Broadcast(); }

protected:
	UFUNCTION()
	void OnRep_RoundInfo() { OnRoundInfoChanged.Broadcast(); }
};
