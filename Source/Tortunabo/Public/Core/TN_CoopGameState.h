#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "Core/TN_MatchFlowTypes.h"
#include "Core/TN_EndTitles.h"
#include "TN_CoopGameState.generated.h"

/** @brief Disparado en clientes cuando MatchFlowState replica, y manualmente en server vía BroadcastFlowStateChange(). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMatchFlowStateChanged, ETNMatchFlowState, NewState);

/** @brief Disparado en todas las máquinas cuando llega un nuevo mensaje de Quick Chat. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuickChatReceived, const FTN_QuickChatEntry&, Entry);

/** @brief Disparado en todas las máquinas cuando el array global RaceResults se actualiza. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRaceResultsUpdated);

/**
 * @brief GameState replicado: fuente de verdad del estado de partida.
 *
 * Centraliza datos que todos los clientes necesitan ver:
 *  - MatchFlowState (Waiting/Countdown/Cinematic/InProgress/Results).
 *  - Conteos de jugadores (ConnectedPlayers, ReadyPlayers, FinishedPlayers, etc.).
 *  - ServerMatchElapsedTime para el cronómetro de carrera.
 *  - RaceResults: scoreboard global completo replicado al pasar a Results.
 *  - QuickChatHistory: últimos N mensajes mantenidos localmente vía multicast.
 *
 * @note OnRep no dispara en la máquina dueña (server) — los GameModes llaman
 *       BroadcastFlowStateChange() manualmente tras cambiar MatchFlowState
 *       para que el listen-server reciba el delegate igual que un cliente.
 */
UCLASS()
class TORTUNABO_API ATN_CoopGameState : public AGameState
{
	GENERATED_BODY()

public:
	ATN_CoopGameState();

	/** Sin huecos nulos en el PlayerArray antes de que el motor los marque: viaja al mapa de transición y vuelve (#711). */
	virtual void SeamlessTravelTransitionCheckpoint(bool bToTransitionMap) override;

	/**
	 * @brief Notifica a los listeners locales que MatchFlowState ha cambiado.
	 * @note Llamado por los GameModes tras cambiar la variable, porque OnRep
	 *       no dispara en la máquina que es dueña de la variable replicada.
	 */
	void BroadcastFlowStateChange();

	/**
	 * @brief Persiste el RaceScore del jugador local en el save game durante Results.
	 * @note Persistencia POR DELTA: acumula en PersistedScoreThisRace lo ya guardado y
	 *       suma solo la diferencia. Idempotente ante llamadas repetidas, y cubre la race
	 *       de replicación en clientes (Results puede llegar antes que el último update
	 *       de RaceScore): OnRep_RaceScore reinvoca este método y el delta pendiente se
	 *       persiste al llegar. Público porque lo llama ATN_CoopPlayerState::OnRep_RaceScore.
	 *       También suma al perfil los muñecos tortuga del jugador local (#797) y su puntuación final del Coop (#789),
	 *       por diferencia igual que los puntos.
	 */
	void PersistLocalPlayerScoreIfResults();

	/**
	 * @brief Añade una entrada de Quick Chat en el servidor y notifica a todos los clientes vía Multicast.
	 * @param SenderPlayerId Id del PlayerState emisor.
	 * @param MessageID Id del mensaje en el catálogo.
	 * @param ServerTimeSeconds Tiempo de mundo del servidor cuando se envió.
	 */
	void AddQuickChatEntry(int32 SenderPlayerId, uint8 MessageID, float ServerTimeSeconds);

	/**
	 * @brief Envía una entrada individual de Quick Chat a todos los clientes.
	 * @note Más eficiente que replicar el array completo (ahorro ~130 bytes por mensaje).
	 */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastNewChatEntry(FTN_QuickChatEntry Entry);

	/**
	 * @brief Resuelve el nombre del emisor de un Quick Chat dado su PlayerId.
	 * @param SenderPlayerId Id compacto del emisor.
	 * @return FText con el PlayerName o un fallback si no se encuentra.
	 */
	UFUNCTION(BlueprintPure, Category = "QuickChat")
	FText ResolveQuickChatSenderName(int32 SenderPlayerId) const;

	// ── Match Flow ────────────────────────────────────────────────────────────
	UPROPERTY(BlueprintAssignable, Category = "Coop")
	FOnMatchFlowStateChanged OnMatchFlowStateChanged;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MatchFlowState, Category = "Coop")
	ETNMatchFlowState MatchFlowState = ETNMatchFlowState::WaitingForPlayers;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop")
	int32 ReadyPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop")
	int32 ConnectedPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop")
	int32 PlayersInStartZone = 0;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop")
	int32 ExpectedPlayers = 8;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop")
	int32 CountdownValue = 0;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop")
	float ServerMatchElapsedTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop")
	int32 FinishedPlayers = 0;

	// ── Scoreboard global ─────────────────────────────────────────────────────

	/**
	 * Resultados de carrera replicados a todos los clientes.
	 * Se actualiza desde TN_RunGameMode tras MarkPlayerFinished/MarkPlayerDead.
	 * El widget Results lee esto para mostrar el ranking completo de todos los
	 * jugadores ordenado (1º, 2º, 3º, 4º, eliminados al final).
	 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RaceResults, Category = "Coop|Score")
	TArray<FTN_RaceResultEntry> RaceResults;

	UPROPERTY(BlueprintAssignable, Category = "Coop|Score")
	FOnRaceResultsUpdated OnRaceResultsUpdated;

	/**
	 * Server-only: añade o actualiza la entrada de un jugador.
	 * El array se mantiene ordenado por FinishRank (eliminados al final con rank=0).
	 */
	void Server_UpsertRaceResult(int32 InPlayerId, const FString& InPlayerName,
		int32 InFinishRank, float InFinishTime, int32 InRaceScore, bool bInEliminated);

	/**
	 * Título Saltarín de la partida (#798): quien más ha saltado (empate: quien entró antes en la sala). Lo decide el
	 * servidor al entrar en Results (BroadcastFlowStateChange) y lo enseña la pantalla de resultados.
	 */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop|Titles")
	FTN_EndTitle JumperTitle;

	/** Título Tesorero (#873): quien más chapas ha recogido (ATN_CoopPlayerState::ChapasCollected). Como JumperTitle. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop|Titles")
	FTN_EndTitle TreasurerTitle;

	/** Título Curandero (#873): quien más jugadoras ha curado (ATN_CoopPlayerState::PlayersHealed). Como JumperTitle. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop|Titles")
	FTN_EndTitle HealerTitle;

	// ── Quick Chat ────────────────────────────────────────────────────────────
	/** Disparado en todas las máquinas cuando llega un nuevo mensaje de Quick Chat. Bindear en BP o C++. */
	UPROPERTY(BlueprintAssignable, Category = "QuickChat")
	FOnQuickChatReceived OnQuickChatReceived;

	/**
	 * Historial local de los últimos MaxQuickChatEntries mensajes.
	 * Mantenido localmente en todas las máquinas via MulticastNewChatEntry.
	 * Ya no se replica como array completo — ahorro de ~130 bytes por mensaje.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "QuickChat")
	TArray<FTN_QuickChatEntry> QuickChatHistory;

	/** Máximo de entradas conservadas en QuickChatHistory. */
	static constexpr int32 MaxQuickChatEntries = 10;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** @brief OnRep de MatchFlowState: dispara el delegate OnMatchFlowStateChanged. */
	UFUNCTION()
	void OnRep_MatchFlowState();

	/** @brief OnRep de RaceResults: dispara OnRaceResultsUpdated. */
	UFUNCTION()
	void OnRep_RaceResults();

	/**
	 * Parte del RaceScore local ya persistida en este ciclo de Results.
	 * PersistLocalPlayerScoreIfResults suma solo RaceScore - PersistedScoreThisRace.
	 * Se resetea al salir de Results (y el GameState es nuevo en cada nivel).
	 */
	int32 PersistedScoreThisRace = 0;

	/** Muñecos tortuga del jugador local ya sumados al perfil en este ciclo de Results (#797), igual que los puntos. */
	int32 PersistedDollsThisRace = 0;

	/** Puntuación final del Coop del jugador local ya sumada al perfil en este ciclo de Results (#789). */
	int32 PersistedCoopScoreThisRace = 0;

	int32 NextQuickChatSequence = 0;

	/** Servidor: decide los títulos de fin de partida con los PlayerState de la sala (TN_CoopGameState_EndScore.cpp). */
	void AwardEndTitles();

	/**
	 * Servidor: puntos de final de partida de cada jugadora (#873, TNCoopScore::Compute con UTN_PointsEconomy), después de
	 * los títulos. Los pone en su CoopScore y en la columna de puntos del marcador (RaceResults).
	 */
	void AwardEndScores();
};
