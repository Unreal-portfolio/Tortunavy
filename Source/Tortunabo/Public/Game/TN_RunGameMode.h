#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "Core/TN_MatchFlowTypes.h"
#include "Game/TN_LateJoinRules.h"
#include "TN_RunGameMode.generated.h"

class APlayerController;
class APlayerStart;
class ATN_RescuePickup;
class ATN_CollectionZone;
class ATN_CoopPlayerState;

/**
 * @brief GameMode de la fase Run (carrera). Orquesta el ciclo Countdown -> Race -> Results -> retorno al lobby.
 *
 * Responsabilidades:
 *  - Spawnea jugadores en LVL_Run tras Seamless Travel desde LVL_HQ.
 *  - Gestiona la línea de meta (MarkPlayerFinished) y las muertes (MarkPlayerDead).
 *  - Sistema DBNO (Down But Not Out) con bleedout timer e inmunidad post-revive.
 *  - Reloj de carrera replicado y countdown del pantalla de Resultados.
 *  - Sistema de puntuación: RankScore (podio) + ScorePickups + TimeBonus.
 *  - Retorno a LVL_HQ vía Seamless Travel cuando expira el timer de resultados.
 */
UCLASS()
class TORTUNABO_API ATN_RunGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	ATN_RunGameMode();

	/** @brief Inicializa timers, reloj de carrera y bindings con CollectionZones presentes en el mapa. */
	virtual void BeginPlay() override;

	/** @brief Tras Super, devuelve al jugador su nombre completo (el motor lo corta a 20 caracteres). */
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options,
		const FString& Portal = TEXT("")) override;

	/** @brief Llamado por UE cuando un PlayerController NUEVO entra (no por seamless travel). */
	virtual void PostLogin(APlayerController* NewPlayer) override;

	/** @brief Limpieza al salir de un jugador: libera timers de DBNO/inmunidad y rescata pickups. */
	virtual void Logout(AController* Exiting) override;

	/**
	 * @brief Guarda también el PlayerState de los muertos y de los que llegaron a la meta (esperan como espectadores y
	 *        AGameMode no los guardaría): al volver no pueden entrar como nuevos y vivos (#345).
	 */
	virtual void AddInactivePlayer(APlayerState* PlayerState, APlayerController* PC) override;

	/**
	 * @brief Tras Super (que devuelve su PlayerState a quien vuelve), decide cómo entra el jugador según el modo y deja
	 *        su estado listo. Es el único punto de AGameMode::PostLogin entre la reactivación y HandleStartingNewPlayer.
	 */
	virtual bool FindInactivePlayer(APlayerController* PC) override;

	/** @brief Selecciona un PlayerStart libre evitando reusar el mismo en spawn paralelo. */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** @brief Hook de UE post-spawn del pawn: aplica cosméticos y reglas iniciales del jugador. */
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	/**
	 * @brief Seamless travel: limpia estado de espectador del PC ANTES de que UE intente respawnearlo.
	 * @note Evita que un jugador eliminado en la run anterior llegue al lobby ya en modo espectador.
	 */
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;

	/** Un ServerTravel a un mapa que no existe no empieza (UTN_TravelFailureSubsystem::CanServerTravelTo): los invitados no lo reciben. */
	virtual bool CanServerTravel(const FString& URL, bool bAbsolute) override;

	/**
	 * @brief Setup final tras Seamless Travel cuando todos los jugadores han llegado.
	 *        Lanza countdown si ya estamos completos, o espera con timeout.
	 */
	virtual void PostSeamlessTravel() override;

	/**
	 * @brief Marca a un jugador como finalizado (cruzó la meta) y le asigna el siguiente FinishRank.
	 * @param PlayerController Controller del jugador que terminó la carrera.
	 * @note Server-only. Si era el último jugador activo, dispara el flujo de Resultados.
	 */
	UFUNCTION(BlueprintCallable, Category = "Run")
	virtual void MarkPlayerFinished(APlayerController* PlayerController);

	/**
	 * @brief Marca a un jugador como muerto, lo pasa a espectador y spawnea su RescuePickup.
	 * @param PlayerController Controller del jugador eliminado.
	 * @note Server-only. Guarda el pawn en DeadPlayerPawns para que el rescate lo pueda teletransportar.
	 */
	UFUNCTION(BlueprintCallable, Category = "Run")
	virtual void MarkPlayerDead(APlayerController* PlayerController);

	/**
	 * @brief Callback server-side cuando una CollectionZone alcanza su RequiredCount.
	 *        Bindeado en BeginPlay vía OnZoneGoalReached. Suma GoalReachedBonusScore
	 *        al RaceScore de todos los jugadores activos (no eliminados).
	 * @param Zone CollectionZone que dispara el evento.
	 */
	void HandleCollectionZoneGoal(ATN_CollectionZone* Zone);

	/**
	 * @brief Pone a un jugador en estado DBNO (knockdown + bleedout timer).
	 * @param PlayerController Jugador que entra en DBNO.
	 * @note Server-only. Si todos los vivos están en DBNO, dispara CheckAllAliveDBNO -> muerte global.
	 */
	UFUNCTION(BlueprintCallable, Category = "Run|DBNO")
	void EnterDBNO(APlayerController* PlayerController);

	/**
	 * @brief Revive a un jugador que está actualmente en DBNO.
	 *        Llamado por el servidor cuando un compañero completa el canal de revive.
	 * @param PlayerController Jugador a revivir.
	 */
	UFUNCTION(BlueprintCallable, Category = "Run|DBNO")
	void RevivePlayer(APlayerController* PlayerController);

	/**
	 * @brief Devuelve el pawn cacheado de un jugador muerto (guardado antes del UnPossess).
	 * @param PlayerId ID del PlayerState del jugador eliminado.
	 * @return Pawn cacheado o nullptr si no hay registro.
	 * @note Usado por TN_RescuePickup::Interact para teletransportar el pawn al lugar del rescate.
	 */
	APawn* GetDeadPlayerPawn(int32 PlayerId) const;

	/**
	 * @brief Indica si el jugador tiene inmunidad post-revive activa.
	 * @param PC PlayerController a consultar.
	 * @return true mientras dure ReviveImmunitySeconds desde el revive.
	 * @note Consultado por TN_DeathZoneVolume para no iniciar el countdown
	 *       mientras el jugador recién revivido está protegido.
	 */
	bool IsPlayerReviveImmune(APlayerController* PC) const;

	/**
	 * @brief El anfitrión corta la partida desde el menú de pausa (UTN_PauseMenuWidget): todos vuelven al lobby con la
	 *        misma limpieza y el mismo viaje sin cortes que al acabar la ronda (FinishRoundAndReturnToLobby).
	 * @note Server-only.
	 */
	void ReturnToLobbyNow() { if (HasAuthority()) { FinishRoundAndReturnToLobby(); } }

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Run")
	float ResultsDurationSeconds = 8.0f;

	/**
	 * Lobby al que se vuelve al acabar la ronda si la GameInstance no sabe de cuál se salió
	 * (UMP_GameInstance::LobbyReturnMapPath, que manda): el castillo de arena.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Run")
	FString LobbyMapPath = TEXT("/Game/Maps/Lobby/LVL_Lobby");

	/** Segundos máximos esperando a que todos los jugadores reconecten tras el travel. */
	UPROPERTY(EditDefaultsOnly, Category = "Run|Staging")
	float WaitingForPlayersTimeoutSeconds = 15.0f;

	/** Segundos que un jugador DBNO tiene antes de morir definitivamente por bleedout. */
	UPROPERTY(EditDefaultsOnly, Category = "Run|DBNO", meta = (ClampMin = "3.0"))
	float DBNOBleedoutSeconds = 8.f;

	/** Invulnerabilidad breve tras un revive (evita re-muerte inmediata en death zones). */
	UPROPERTY(EditDefaultsOnly, Category = "Run|DBNO", meta = (ClampMin = "0.0"))
	float ReviveImmunitySeconds = 2.f;

	/**
	 * Clase de pickup de rescate que se spawnea al morir un jugador.
	 * Cuando un compañero interactúa con él, revive al muerto en esa posición.
	 * Asignar en BP_RunGameMode → Class Defaults.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Run|Death")
	TSubclassOf<ATN_RescuePickup> RescuePickupClass;

	/**
	 * false = morir es definitivo: EnterDBNO mata directamente y no se deja RescuePickup. El tótem sigue salvando.
	 * Lo apaga ATN_SurvivalGameMode.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Run|Death")
	bool bAllowRevive = true;

	// ── Sistema de puntuación final ─────────────────────────────────────────
	// RaceScore final = RankScore (podio) + ScorePickups recogidos en la run + TimeBonus.
	// RankScore: 1º=400, 2º=300, 3º=200, 4º=100, resto=50. Eliminados=0.
	// TimeBonus = max(0, (TimeBonusBaselineSeconds − FinishTime) × PointsPerSecond).

	/** Segundos baseline para el time bonus. Tiempos por debajo otorgan puntos. */
	UPROPERTY(EditDefaultsOnly, Category = "Run|Scoring", meta = (ClampMin = "10.0"))
	float TimeBonusBaselineSeconds = 120.f;

	/** Puntos por cada segundo bajo el baseline. 0 = sin time bonus. */
	UPROPERTY(EditDefaultsOnly, Category = "Run|Scoring", meta = (ClampMin = "0.0"))
	float TimeBonusPointsPerSecond = 5.f;

	// Protegido (no privado) para que los modos derivados (ATN_ProcMapGameMode)
	// reutilicen el flujo de muerte, rescate, resultados y vuelta al lobby.
	FTimerHandle ResultsTimerHandle;
	FTimerHandle ResultsCountdownTimerHandle;
	FTimerHandle WaitingTimeoutTimerHandle;
	FTimerHandle DBNOBleedoutTimerHandle;
	FTimerHandle RaceClockTimerHandle;
	float MatchStartServerTime = 0.f;
	int32 NextFinishRank = 1;
	int32 ResultsCountdownValue = 0;

	/** Cuántos jugadores esperamos del lobby (leído de GameInstance). */
	int32 ExpectedPlayersFromLobby = 1;

	/** true cuando ya transicionamos a InProgress. */
	bool bMatchStarted = false;

	/** true desde el BeginPlay del GameMode: hasta entonces TryStartMatch no arranca (ver TN_MatchStartRules.h). */
	bool bStagingBegun = false;

	/** Jugadores actualmente en DBNO con su tiempo de bleedout restante. */
	TMap<TWeakObjectPtr<APlayerController>, float> DBNOPlayers;

	/** Jugadores con inmunidad post-revive activa. */
	TSet<TWeakObjectPtr<APlayerController>> ReviveImmunePlayers;

	/** Handle de timer de inmunidad por jugador — permite cancelar en re-revive rápido. */
	TMap<TWeakObjectPtr<APlayerController>, FTimerHandle> ImmunityTimers;

	/** Rescue pickups spawnados para jugadores muertos. Key = PlayerId. */
	TMap<int32, TWeakObjectPtr<ATN_RescuePickup>> RescuePickups;

	/**
	 * Pawns de jugadores muertos. Key = PlayerId.
	 * Guardamos la referencia ANTES de que EnterSpectateMode haga UnPossess,
	 * porque tras entrar en espectador, PC->GetPawn() devuelve nullptr.
	 * RevivePlayer y TN_RescuePickup usan esto para recuperar el pawn.
	 */
	TMap<int32, TWeakObjectPtr<APawn>> DeadPlayerPawns;

	// ── Entrada tardía y reconexión (#345, TN_LateJoinRules.h) ─────────────

	/** @brief Cómo trata el modo a quien entra con la partida en marcha. Clásico y Carrera: como siempre. */
	virtual ETNLateJoinPolicy GetLateJoinPolicy() const { return ETNLateJoinPolicy::FreshStart; }

	/** @brief La partida (o la ronda) ya está en juego para quien entra ahora. */
	virtual bool IsMatchInProgressForJoin() const { return bMatchStarted; }

	/**
	 * @brief Lleva a un punto seguro del camino ya recorrido el pawn de quien entra a mitad (ETNJoinRole::PlayOnPath).
	 * @return false si no hay sitio: entonces mira como espectador.
	 */
	virtual bool PlaceMidMatchJoiner(APlayerController* PlayerController) { return false; }

	/** PlayerId de quien entró con la partida en marcha y no la juega (FTNJoinDecision::bSitsOut). */
	TSet<int32> SitOutPlayerIds;

	/** @brief Deja a un jugador fuera de la partida en curso: muerto a efectos de reglas y mirando a los demás. */
	void SitOutAsSpectator(APlayerController* PlayerController);

	/** @brief Garantiza que el jugador tenga un pawn vivo en el mapa (spawnea si falta). */
	void EnsurePlayerSpawned(APlayerController* PlayerController);

	/** @brief Devuelve un PlayerStart cualquiera como fallback si no hay otros disponibles. */
	APlayerStart* EnsureFallbackPlayerStart();

	/** @brief Tick (1Hz) que decrementa el contador de Resultados y dispara FinishRoundAndReturnToLobby al llegar a 0. */
	void TickResultsCountdown();

	/** @brief Comprueba si la ronda terminó (todos los vivos cruzaron meta o murieron) y arranca Resultados. */
	virtual void UpdateRoundProgressAndMaybeFinish();

	/** @brief Pasa a Resultados y arma la cuenta atrás que acaba en FinishRoundAndReturnToLobby (una sola vez). */
	void StartResults();

	/** @brief Mueve a un jugador a modo espectador (UnPossess + spectate next alive). */
	void MovePlayerToSpectator(APlayerController* PlayerController) const;

	/** @brief Cierra la ronda: persiste scores, dispara Seamless Travel de vuelta a LVL_HQ. */
	virtual void FinishRoundAndReturnToLobby();

	/**
	 * @brief Opciones que se añaden a la URL de vuelta al lobby (con su «?» delante). Vacío por defecto.
	 * @note Las opciones de la URL del viaje anterior (p. ej. ?game=Survival) se heredan en el siguiente: un modo que
	 *       viaja con «game» debe limpiarlo aquí, o el lobby carga con el GameMode de la partida y nadie arranca la cuenta atrás.
	 */
	virtual FString GetLobbyTravelOptions() const { return FString(); }

	/** @brief Cambia el MatchFlowState en el GameState replicado + broadcast a listen-server. */
	void SetFlowState(ETNMatchFlowState NewState) const;

	/** @brief Tick compartido (0.1s) de todos los bleedouts DBNO activos. */
	void TickDBNOBleedout();

	/** @brief Si todos los jugadores vivos están en DBNO, los mata a todos (nadie puede revivir). */
	void CheckAllAliveDBNO();

	/** @brief Comprueba si llegaron todos los jugadores esperados y, si sí, arranca el countdown. */
	void TryStartMatch();

	/** @brief Timeout de staging: arranca la carrera aunque no hayan llegado todos. */
	virtual void OnWaitingTimeout();

	/** @brief Tick periódico (0.5s) que actualiza ServerMatchElapsedTime en el GameState. */
	void TickRaceClock();

	/**
	 * @brief Consume un tótem del inventario del pawn moribundo si lleva uno, cancelando la muerte.
	 * @return true si se consumió un tótem (la muerte queda cancelada y MarkPlayerDead debe retornar).
	 */
	bool TryTotemAutoRevive(APlayerController* PlayerController);

	/** @brief Aplica el ragdoll/ocultación de muerte sobre el pawn (RecoverFromKnockdown → StopMovementImmediately → DisableInput → bAlwaysRelevant/DORM_Awake → SetDeadVisual). */
	void ApplyDeathVisuals(APawn* Pawn, APlayerController* PlayerController);

	/** @brief Spawnea (si hay clase asignada) el RescuePickup en la posición de muerte y lo registra en RescuePickups. */
	void SpawnRescuePickupForDeath(ATN_CoopPlayerState* TNPS, const FVector& DeathLocation, APlayerController* PlayerController);

	/** @brief Concede inmunidad post-revive y arma el timer que la retira tras ReviveImmunitySeconds. */
	void GrantReviveImmunity(APlayerController* PlayerController);

	/** @brief Restaura visual, colisión, posesión e input del pawn revivido. Solo se llama cuando Pawn es válido. */
	void RestorePossessionAfterRevive(APlayerController* PlayerController, APawn* Pawn, const FVector& ReviveTargetLocation, bool bHasReviveTargetLocation);

private:
	/** Decisión de entrada de cada PostLogin, de FindInactivePlayer a HandleStartingNewPlayer (el viaje sin cortes no pasa por aquí). */
	TMap<TWeakObjectPtr<APlayerController>, FTNJoinDecision> PendingJoins;

	/** @brief Quien vuelve antes de que caduque su conexión anterior: la cierra para que su PlayerState quede inactivo. */
	void DropStaleConnectionOf(const APlayerController* NewPlayer);

	/** @brief Aplica al PlayerState la decisión de entrada (de cero, conserva o fuera de la partida). */
	void ApplyJoinDecision(APlayerController* PlayerController, const FTNJoinDecision& Decision);

	/** @brief Pawn y cámara de quien entra según su decisión. true si ya está resuelto y no hay que seguir el arranque normal. */
	bool StartJoiningPlayer(APlayerController* PlayerController, const FTNJoinDecision& Decision);
};
