#pragma once

#include "CoreMinimal.h"
#include "Game/TN_RunGameMode.h"
#include "Game/TN_ChampionChoiceHandler.h"
#include "Game/TN_TctItemRules.h"
#include "Game/TN_TctRules.h"
#include "TN_TctGameMode.generated.h"

class APlayerStart;
class ATN_CoopPlayerState;
class ATN_TctArena;
class ATN_TctGameState;
class ATN_TctItemPad;
class ATortugaCharacter;

/**
 * @brief Todos contra Todos (#651, plan maestro §3.4 y F7): rondas de supervivencia de 2 a 8 tortugas en una arena inventada.
 *
 * Se juega en LVL_Tct (o con ?game=Tct en cualquier mapa: si no hay ATN_TctArena, se crea). La arena es una variante de
 * Scripts/terrain_volumes/Variants: A01_diana por defecto, otra con ?Arena=<variante> (P01_plataformas, A02_donut...).
 *  - Ronda: todas salen repartidas por la arena; gana la última en pie. Caer al agua, a una zona de muerte o fuera del mapa
 *    elimina: la tortuga queda como fantasma espectador hasta la ronda siguiente (morir es definitivo dentro de la ronda).
 *  - Presión: el mar sube un piso de la arena cada FloodStepSeconds desde FloodStartDelay (TNTctFloodDefaults) y, tras el
 *    último piso, lo cubre todo despacio (muerte súbita). Con el tiempo de la ronda agotado y dos o más en pie, empate.
 *  - Partida: mejor de N (la primera con WinsToWin rondas ganadas). Cada ronda ganada es una concha entera (RaceShellHalves):
 *    entre rondas, el recuento de la carrera; al final, la pantalla de la campeona con su podio (UTN_RaceScreensSubsystem).
 *  - Desconexión: quien se va deja de contar; si en la ronda solo queda una en pie, gana; si en la partida solo queda una,
 *    es la campeona.
 *  - Objetos: puntos de objetos (ATN_TctItemPad) repartidos por la arena, con un objeto de combate que reaparece al rato de
 *    cogerlo (TN_TctItems.h); cada ronda se empieza con las manos vacías.
 * Las reglas están en TN_TctRules.h y TN_TctItemRules.h (tests Tortunabo.Tct). Comandos de prueba: TN.Tct.Eliminate,
 * TN.Tct.WinRound, TN.Tct.Flood, TN.Tct.Item y TN.Tct.Items.
 */
UCLASS()
class TORTUNABO_API ATN_TctGameMode : public ATN_RunGameMode, public ITN_ChampionChoiceHandler
{
	GENERATED_BODY()

public:
	ATN_TctGameMode();

	/**
	 * Antes del BeginPlay de los actores: carga la arena, la mide y prepara los sitios de salida y el plan del agua. Si no hay
	 * arena (build cocinada: la variante no está en Scripts/), la partida no arranca y se vuelve al lobby al llegar todas.
	 */
	virtual void StartPlay() override;

	/**
	 * true si existe la arena por defecto del modo (DefaultArenaVariant de la clase, en Scripts/terrain_volumes/Variants). El
	 * lobby no viaja a este modo ni lo ofrece sin ella (TNLobbyMission::IsModePlayable).
	 */
	static bool HasDefaultArena();

	/** true si la arena se cargó y se midió (sitios de salida, límites y plan del agua listos). */
	bool IsArenaReady() const { return bArenaReady; }

	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** Eliminación: solo con la ronda en juego (entre rondas, el agua y las zonas de muerte no cuentan). */
	virtual void MarkPlayerDead(APlayerController* PlayerController) override;

	/** En este modo no hay meta: no hace nada. */
	virtual void MarkPlayerFinished(APlayerController* PlayerController) override {}

	// ITN_ChampionChoiceHandler
	virtual bool HandleChampionChoice(ETNBeachChampionChoice Choice) override;
	virtual bool CanChooseChampion() const override { return bMatchOver && !bLeaving; }

	int32 GetCurrentRound() const { return CurrentRound; }
	bool IsRoundLive() const { return bRoundLive; }

	// Pruebas (TN.Tct.*): solo en el servidor.
	/** Elimina a la tortuga PlayerIndex (orden de llegada) como si hubiera caído al agua. */
	void DebugEliminate(int32 PlayerIndex);
	/** Cierra la ronda con PlayerIndex como ganadora. */
	void DebugWinRound(int32 PlayerIndex);
	/** El agua empieza ya su siguiente subida. */
	void DebugFloodNow();
	/** Da el objeto Kind a la tortuga PlayerIndex. */
	void DebugGiveItem(int32 PlayerIndex, ETNTctItem Kind);
	/** Todos los puntos de objetos sacan ya uno nuevo. */
	void DebugRespawnItems();

protected:
	virtual void OnWaitingTimeout() override;
	virtual void UpdateRoundProgressAndMaybeFinish() override;
	/** «?game=» vacío: sin él, el lobby heredaría ?game=Tct y cargaría con este GameMode (#156). */
	virtual FString GetLobbyTravelOptions() const override { return TEXT("?game="); }

	/** Rondas ganadas para ser campeona. */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Rounds", meta = (ClampMin = "1"))
	int32 WinsToWin = 3;

	/** Tiempo máximo de una ronda (s); al acabarse con dos o más en pie, empate. */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Rounds", meta = (ClampMin = "10.0"))
	float RoundTimeLimitSeconds = TNTctFloodDefaults::RoundTimeLimitSeconds;

	/** Entre ronda y ronda: tiempo con las tortugas ya colocadas antes del 3, 2, 1. */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Rounds", meta = (ClampMin = "0.25"))
	float PrepSeconds = 1.f;

	/** Cuenta de salida entre rondas (la primera la hace el huevo de la pantalla de carga). */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Rounds", meta = (ClampMin = "0.0"))
	float CountdownSeconds = 3.f;

	/** Recuento de conchas tras cada ronda. */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Rounds", meta = (ClampMin = "1.0"))
	float RoundResultsSeconds = 6.f;

	/** Espera tras una eliminación antes de decidir: dos caídas casi a la vez son un empate, no una ganadora. */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Rounds", meta = (ClampMin = "0.0"))
	float DecisionGraceSeconds = 0.35f;

	/** Pausa tras elegir en la pantalla de la campeona (fundido de la música y del huevo). */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Rounds", meta = (ClampMin = "0.1"))
	float ChampionLeaveDelaySeconds = 1.2f;

	/** Arena por defecto (?Arena=<variante> la cambia). */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Arena")
	FName DefaultArenaVariant = TEXT("A01_diana");

	/** Separación de las muestras al medir la arena (uu). */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Arena", meta = (ClampMin = "50.0"))
	float SurveySpacing = 250.f;

	/** Altura de la cápsula sobre el suelo en la salida (uu). */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Arena", meta = (ClampMin = "0.0"))
	float SpawnLift = 120.f;

	/** Fuera de la caja de la arena, a más de esto en horizontal, es fuera del mapa (uu). */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Arena", meta = (ClampMin = "500.0"))
	float OutOfBoundsMargin = 4000.f;

	/** Cuánto pueden quedar los pies bajo el agua sin caer (vadear la orilla, uu). */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Arena", meta = (ClampMin = "0.0"))
	float WadeDepth = 20.f;

	/** Segundos de la salida a la primera subida del agua. */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Flood", meta = (ClampMin = "0.0"))
	float FloodStartDelay = TNTctFloodDefaults::StartDelay;

	/** Segundos entre subidas. */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Flood", meta = (ClampMin = "5.0"))
	float FloodStepSeconds = TNTctFloodDefaults::StepSeconds;

	/** Lo que tarda cada subida. */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Flood", meta = (ClampMin = "0.5"))
	float FloodRiseSeconds = TNTctFloodDefaults::RiseSeconds;

	/** Lo que tarda la muerte súbita en cubrir la arena entera. */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Flood", meta = (ClampMin = "1.0"))
	float SuddenDeathRiseSeconds = TNTctFloodDefaults::SuddenDeathRiseSeconds;

	/** Escalones como mucho (sin contar la muerte súbita). */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Flood", meta = (ClampMin = "1"))
	int32 FloodMaxSteps = TNTctFloodDefaults::MaxSteps;

	/** Parte del suelo que queda seca tras el último escalón (la cubre la muerte súbita). */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Flood", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float FloodKeepFraction = 0.08f;

	/** Alto de cada franja al buscar los pisos de la arena (uu). */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Flood", meta = (ClampMin = "10.0"))
	float FloodTierTolerance = 150.f;

	/** Cuánto pasa el agua por encima de un piso al inundarlo (uu). */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Flood", meta = (ClampMin = "0.0"))
	float FloodMargin = 60.f;

	/** La muerte súbita sube hasta el punto más alto de la arena más esto (uu). */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Flood", meta = (ClampMin = "0.0"))
	float SuddenDeathMargin = 300.f;

	/** Puntos de objetos que se crean en la arena (si el nivel no trae ninguno); en cada ronda se usan según cuántas juegan. */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Items", meta = (ClampMin = "0"))
	int32 ItemPadCount = 10;

	/** Ningún punto de objetos a menos de esto de una salida (uu), si caben. */
	UPROPERTY(EditDefaultsOnly, Category = "Tct|Items", meta = (ClampMin = "0.0"))
	float ItemPadMinFromSpawn = 700.f;

private:
	UPROPERTY(Transient)
	TObjectPtr<ATN_TctArena> Arena;

	/** Sitios de salida creados al medir la arena (uno por jugadora posible). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<APlayerStart>> SpawnPoints;

	/** Puntos de objetos (los del nivel o los creados al medir la arena), el del centro primero. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ATN_TctItemPad>> ItemPads;

	FTNTctFloodPlan FloodPlan;
	FTNTctArenaBounds ArenaBounds;
	TSet<int32> LeftPlayerIds;
	int32 CurrentRound = 0;
	int32 StartingPlayers = 0;
	/** Partida de prueba con una sola tortuga: ninguna ronda tiene ganadora y las rondas se encadenan hasta salir al lobby (a propósito). */
	bool bSoloMatch = false;
	bool bRoundLive = false;
	bool bMatchOver = false;
	bool bLeaving = false;
	bool bTimeUp = false;
	/** La arena se cargó y se midió (SetUpArena); sin ella no se juega ninguna ronda. */
	bool bArenaReady = false;
	float PhaseEndTime = 0.f;

	FTimerHandle PhaseClockHandle;
	FTimerHandle PhaseEndHandle;
	FTimerHandle WatchHandle;
	FTimerHandle DecisionHandle;
	FTimerHandle RoundLimitHandle;
	FTimerHandle LeaveHandle;

	// ── Arena (TN_TctGameMode.cpp) ──
	/** Carga y mide la arena; false si no se pudo (sin variante o sin suelo). */
	bool SetUpArena();
	/** Sin arena: congela a las tortugas, devuelve la misión del anfitrión al cooperativo y vuelve al lobby. */
	void AbortWithoutArena();
	void BuildFloodPlan();
	void CreateSpawnPoints();
	ATN_TctGameState* GetTctState() const;

	// ── Jugadoras ──
	TArray<FTNTctFighter> GatherFighters() const;
	TArray<APlayerController*> GetPlayingControllers() const;
	ATN_CoopPlayerState* FindPlayerState(int32 PlayerId) const;
	void FreezePlayer(APlayerController* PlayerController) const;
	void UnfreezePlayers() const;
	/** Quien entra con la ronda en juego espera como fantasma a la siguiente. */
	void SitOutRound(APlayerController* PlayerController);
	/** Pone a la tortuga en su sitio de salida de esta ronda (viva o recién creada) y la deja quieta. */
	void PlaceForRound(APlayerController* PlayerController, const FTransform& Spawn);
	/** Todas a sus salidas de esta ronda (rotan cada ronda). */
	void PlaceAllForRound();
	void ResetMatchScores() const;

	// ── Objetos (TN_TctGameMode_Items.cpp) ──
	void CreateItemPads();
	/** Empieza la ronda en los puntos de objetos que tocan según cuántas juegan; los demás, parados. */
	void StartItemPads();
	void StopItemPads();
	/** Manos vacías y sin lastre para la ronda nueva. */
	void ResetItemsForRound(APawn* Pawn) const;
	/** El flotador ha salvado a Turtle del agua y ya no flota: la lanza al punto seco más cercano de la arena. */
	void RescueFromWater(ATortugaCharacter* Turtle, float WaterZ) const;

	// ── Rondas (TN_TctGameMode_Round.cpp) ──
	void PrepareRound();
	void StartCountdown();
	/** Salida de la ronda. bFromLoading: la primera, que sale del huevo de la pantalla de carga (la base pasa a InProgress). */
	void BeginRound(bool bFromLoading);
	void WatchFighters();
	void EvaluateRound();
	void OnRoundTimeLimit();
	void EndRound(ATN_CoopPlayerState* Winner);
	void AfterRoundResults();
	/**
	 * Decide la partida tras una ronda (o una salida entre rondas): con campeona, a su pantalla; sin nadie, al lobby. true si
	 * la partida se ha acabado.
	 */
	bool ContinueOrFinish();
	void EnterChampion(ATN_CoopPlayerState* Champion);
	void PlayAgain();
	void LeaveAfterDelay(TFunction<void()> Action);
	void CancelRoundTimers();
	void SetPhase(ETNBeachRacePhase Phase) const;
	void BeginPhaseClock(float Seconds);
	void TickPhaseClock();
	void HoldWater(float Z) const;
	void SyncRoundInfo() const;
};
