#pragma once

#include "CoreMinimal.h"
#include "Core/TN_MatchStartTypes.h"
#include "Game/TN_RunGameMode.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "TN_ProcMapGameMode.generated.h"

class ATN_ProcMapGenerator;
class ATN_ProcEggNest;
class ATN_PathStorm;
class ATN_ProcMapGameState;
class UTN_ProcMapSettings;
class ATortugaCharacter;

/**
 * @brief GameMode de LVL_ProcMap: partidas por rondas sobre el mapa procedural.
 *
 * Reutiliza de ATN_RunGameMode el staging, la meta, la muerte con rescate, los
 * resultados y la vuelta al lobby. Añade:
 *  - Generación: el mapa se genera en el servidor al empezar cada ronda y la ronda
 *    no arranca hasta que todos los clientes avisan de que lo tienen construido
 *    (AMP_GamePlayerController::ServerReportProcMapReady) o vence la espera.
 *  - Reaparición en pilas de huevos: morir no elimina mientras haya una pila
 *    alcanzada (Coop: la más lejana del equipo; Carrera/2vs2: la del jugador) que
 *    quede por delante de la tormenta. Sin pila válida, muerte normal con rescate.
 *  - Coop: tormenta que avanza por el camino. Por defecto 1 ronda = la partida.
 *  - Carrera: todos contra todos, el primero en la meta gana la ronda; gana la
 *    partida quien llega a WinsToWinMatch.
 *  - 2vs2: exige 4 jugadores. Parejas que rotan cada ronda (AB|CD, AC|BD, AD|BC);
 *    gana la ronda la pareja cuyos DOS miembros llegan antes. Las victorias cuentan
 *    por jugador; gana quien llega a WinsToWinMatch.
 *  - Salida: al fondo del claro, la estructura con la que se pusieron listos en el lobby (puerta doble o huevos,
 *    ATN_ProcStartStructure). Se aparece dentro y se abre StartStructureOpenDelaySeconds después de empezar la ronda.
 *
 * Pruebas sin lobby: opciones de URL ?ProcMode=Coop|Race|2v2 ?ProcDifficulty=Easy|Normal|Hard ?ProcSeed=N
 * ?ProcStart=Gate|Eggs. Consola: TN.Proc.StartStyle (-1 = lo del lobby, 0 = puerta doble, 1 = huevos).
 */
UCLASS()
class TORTUNABO_API ATN_ProcMapGameMode : public ATN_RunGameMode
{
	GENERATED_BODY()

public:
	ATN_ProcMapGameMode();

	virtual void BeginPlay() override;
	virtual void Logout(AController* Exiting) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void MarkPlayerFinished(APlayerController* PlayerController) override;
	virtual void MarkPlayerDead(APlayerController* PlayerController) override;

	/** Una tortuga pisó una pila de huevos (servidor). */
	void NotifyEggNestReached(APlayerController* PlayerController, ATN_ProcEggNest* Nest);

	/** Un cliente terminó de construir la generación indicada del mapa. */
	void NotifyClientMapReady(APlayerController* PlayerController, int32 Generation);

	UFUNCTION(BlueprintPure, Category = "ProcMap")
	ETNProcGameMode GetProcMode() const { return Mode; }

	UFUNCTION(BlueprintPure, Category = "ProcMap")
	ETNProcDifficulty GetProcDifficulty() const { return Difficulty; }

	UFUNCTION(BlueprintPure, Category = "ProcMap")
	ATN_ProcMapGenerator* GetGenerator() const { return Generator; }

protected:
	virtual void OnWaitingTimeout() override;
	virtual void UpdateRoundProgressAndMaybeFinish() override;

	/** Coop: se vuelve (o se entra) al camino; Carrera y 2vs2: se mira hasta la ronda siguiente (#345). */
	virtual ETNLateJoinPolicy GetLateJoinPolicy() const override;
	/** La ronda está en juego: antes, entre rondas o generando, se entra como al empezar y PlacePlayersAtStart coloca. */
	virtual bool IsMatchInProgressForJoin() const override { return bRoundActive && !bMatchOver; }
	/** La pila de huevos más lejana del equipo que siga por delante de la tormenta, o la salida (FindRespawnTransform). */
	virtual bool PlaceMidMatchJoiner(APlayerController* PlayerController) override;

	/** Clase del generador que se crea si el nivel no tiene uno colocado. */
	UPROPERTY(EditDefaultsOnly, Category = "ProcMap")
	TSubclassOf<ATN_ProcMapGenerator> GeneratorClass;

	/** Ajustes del mapa para un generador que no tenga los suyos. */
	UPROPERTY(EditDefaultsOnly, Category = "ProcMap")
	TObjectPtr<UTN_ProcMapSettings> MapSettings;

	/** Clase de la tormenta del Coop (si los ajustes del mapa no definen otra). */
	UPROPERTY(EditDefaultsOnly, Category = "ProcMap")
	TSubclassOf<ATN_PathStorm> PathStormClass;

	/** Modo si se abre LVL_ProcMap sin pasar por el lobby. */
	UPROPERTY(EditDefaultsOnly, Category = "ProcMap|Testing")
	ETNProcGameMode ModeWithoutLobby = ETNProcGameMode::Coop;

	/** Dificultad si se abre LVL_ProcMap sin pasar por el lobby. */
	UPROPERTY(EditDefaultsOnly, Category = "ProcMap|Testing")
	ETNProcDifficulty DifficultyWithoutLobby = ETNProcDifficulty::Normal;

	/** Semilla fija para repetir mapas (0 = aleatoria). Cada ronda suma 1. */
	UPROPERTY(EditDefaultsOnly, Category = "ProcMap|Testing")
	int32 FixedSeed = 0;

	/** Coop: rondas por partida. Por defecto una ronda es la partida entera. */
	UPROPERTY(EditDefaultsOnly, Category = "ProcMap|Rounds", meta = (ClampMin = "1"))
	int32 CoopRounds = 1;

	/** Carrera y 2vs2: rondas ganadas para llevarse la partida. */
	UPROPERTY(EditDefaultsOnly, Category = "ProcMap|Rounds", meta = (ClampMin = "1"))
	int32 WinsToWinMatch = 3;

	/** Mapa nuevo (semilla nueva) en cada ronda. */
	UPROPERTY(EditDefaultsOnly, Category = "ProcMap|Rounds")
	bool bRegenerateEachRound = true;

	/** Segundos entre rondas mostrando el resultado. */
	UPROPERTY(EditDefaultsOnly, Category = "ProcMap|Rounds", meta = (ClampMin = "1.0"))
	float BetweenRoundsSeconds = 6.f;

	/** Carrera y 2vs2: límite de la ronda; al agotarse gana quien va más adelantado. 0 = sin límite. */
	UPROPERTY(EditDefaultsOnly, Category = "ProcMap|Rounds", meta = (ClampMin = "0.0"))
	float CompetitiveRoundTimeLimitSeconds = 900.f;

	/** Espera mínima tras generar (colisión del terreno cocinada, jugadores colocados). */
	UPROPERTY(EditDefaultsOnly, Category = "ProcMap|Rounds", meta = (ClampMin = "0.0"))
	float MinPreRoundSeconds = 2.f;

	/** Espera máxima a que todos los clientes construyan el mapa. */
	UPROPERTY(EditDefaultsOnly, Category = "ProcMap|Rounds", meta = (ClampMin = "1.0"))
	float MapReadyTimeoutSeconds = 30.f;

	/**
	 * Segundos desde que empieza la ronda hasta que se abre la estructura de salida (gira la puerta 2 o se rompen los
	 * huevos): coincide con el «¡ADELANTE!» de la pantalla de carga.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "ProcMap|Rounds", meta = (ClampMin = "0.0"))
	float StartStructureOpenDelaySeconds = 1.2f;

	/** Segundos fuera de juego antes de reaparecer en la pila de huevos. */
	UPROPERTY(EditDefaultsOnly, Category = "ProcMap|Respawn", meta = (ClampMin = "0.0"))
	float RespawnDelaySeconds = 2.5f;

	/** Distancia de camino (cm) que la pila debe sacar a la tormenta para reaparecer en ella. */
	UPROPERTY(EditDefaultsOnly, Category = "ProcMap|Respawn", meta = (ClampMin = "0.0"))
	float StormRespawnMargin = 3000.f;

private:
	UPROPERTY(Transient)
	TObjectPtr<ATN_ProcMapGenerator> Generator;

	UPROPERTY(Transient)
	TObjectPtr<ATN_PathStorm> Storm;

	ETNProcGameMode Mode = ETNProcGameMode::Coop;
	ETNProcDifficulty Difficulty = ETNProcDifficulty::Normal;
	/** Cómo se sale en cada ronda: lo del lobby, la opción de URL ProcStart o TN.Proc.StartStyle. */
	ETNMatchStartStyle StartStyle = ETNMatchStartStyle::Gate;
	int32 UrlSeed = 0;
	int32 CurrentRound = 0;
	bool bPlayersArrived = false;
	bool bWaitingForMap = false;
	bool bRoundActive = false;
	bool bMatchOver = false;
	bool bSuppressRoundCheck = false;
	float WaitStartTime = 0.f;

	/** Última generación del mapa que cada cliente tiene construida. */
	TMap<TWeakObjectPtr<APlayerController>, int32> ClientReadyGeneration;

	/** Pila más lejana alcanzada por cada jugador (PlayerId → NestOrder). */
	TMap<int32, int32> NestReachedByPlayer;

	/** Coop: pila más lejana alcanzada por cualquiera del equipo. */
	int32 TeamBestNest = -1;

	TMap<TWeakObjectPtr<APlayerController>, FTimerHandle> PendingRespawns;
	/** Jugadores con el movimiento bloqueado a la espera de la ronda, con el pawn que tenían. */
	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<APawn>> FrozenControllers;

	FTimerHandle MapReadyPollHandle;
	FTimerHandle BetweenRoundsHandle;
	FTimerHandle RoundTimeLimitHandle;
	FTimerHandle StartStructureOpenHandle;

	ATN_ProcMapGameState* GetProcGameState() const;
	void ResolveModeAndDifficulty();
	/** Estilo de la salida de la ronda (GameInstance ← lobby, URL ?ProcStart=, consola TN.Proc.StartStyle). */
	void ResolveStartStyle();
	/** Abre la estructura de salida de la ronda en curso (puerta 2 o huevos). */
	void OpenStartStructure();
	/**
	 * Dónde empieza la ronda el jugador: su sitio en la estructura de salida (a la altura de la cápsula de su peón) o,
	 * sin estructura o si sobra, el anillo del claro.
	 */
	FTransform GetRoundStartTransform(const AController* Controller, const APawn* Pawn) const;
	void EnsureGenerator();
	void GenerateRoundMap();
	void PollMapReady();
	void BeginRoundPlay();
	void PlacePlayersAtStart();
	void RespawnControllerFresh(APlayerController* PlayerController);
	void FreezeWaitingPlayers();
	void UnfreezeAllPlayers();
	void StartStormIfNeeded();

	/** Velocidad de andar (cm/s) de la tortuga más lenta en juego, o la del peón por defecto; 0 si no se sabe. */
	float GetTurtleWalkSpeed() const;
	void EndRound(const TArray<APlayerController*>& Winners, const FString& ResultText);
	void OnRoundTimeLimit();
	void StartNextRound();
	void CleanupRoundActors();
	void EnterFinalResults();
	void AssignTwoVsTwoTeams();
	void ReleaseCarry(ATortugaCharacter* Turtle) const;
	bool FindRespawnTransform(APlayerController* PlayerController, FTransform& OutTransform) const;
	void BeginRespawn(APlayerController* PlayerController, ATortugaCharacter* Turtle);
	void FinishRespawn(TWeakObjectPtr<APlayerController> WeakPC);
	void SyncGameState() const;
	int32 GetPlayerSlot(const AController* Controller) const;
	float GetPlayerProgress(const APlayerController* PlayerController) const;
	TArray<APlayerController*> GetTeamMembers(int32 Team) const;
	int32 CountConnectedPlayers() const;
	static FString DescribePlayers(const TArray<APlayerController*>& Players);
};
