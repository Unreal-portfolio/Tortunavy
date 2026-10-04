#pragma once

#include "CoreMinimal.h"
#include "Game/TN_RunGameMode.h"
#include "Game/TN_SurvivalRules.h"
#include "TN_SurvivalGameMode.generated.h"

class ATN_ChunkManager;

/**
 * @brief Modo Supervivencia: niveles cortos uno tras otro hasta que queda una tortuga.
 *
 * Se juega en LVL_Run con ?game=Survival (alias en DefaultEngine.ini). ATN_ChunkManager pasa al modo por niveles: cada
 * nivel es un mapa del catálogo de Supervivencia (#515) generado entero (ATN_ProcMapGenerator, #274): el nivel N juega
 * uno de dificultad min(N, 5) que no haya salido en la partida (TN_SurvivalMapSelection.h, #518). La semilla de la
 * partida es al azar o la de ?SurvivalSeed=N; ?SurvivalMap=<semilla> fija el mapa del nivel 1.
 *  - La espera del lobby es en el corral de LVL_Run; al empezar, todos salen desde la salida del mapa del nivel 1.
 *  - Quien llega a la meta espera como espectador; cuando todos los vivos han llegado, se genera el siguiente
 *    nivel y, en cuanto su suelo tiene colisión, vuelven a salir desde la salida del mapa nuevo.
 *  - Morir es definitivo (sin DBNO ni rescate; el tótem sí salva) y los muertos espectan.
 *  - En grupo gana la última viva; si las últimas mueren en el mismo nivel, la que murió más cerca de la meta.
 *    En solitario dura hasta que muere.
 * Las reglas están en TN_SurvivalRules.h (tests Tortunabo.Survival).
 */
UCLASS()
class TORTUNABO_API ATN_SurvivalGameMode : public ATN_RunGameMode
{
	GENERATED_BODY()

public:
	ATN_SurvivalGameMode();

	/** @brief Pone el ChunkManager en modo por niveles antes de que los actores hagan BeginPlay (genera el nivel 1). */
	virtual void StartPlay() override;

	virtual void Logout(AController* Exiting) override;

	/** La salida del mapa del nivel (los PlayerStart del generador); sin mapa, los del corral de LVL_Run. */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	virtual void MarkPlayerFinished(APlayerController* PlayerController) override;

	virtual void MarkPlayerDead(APlayerController* PlayerController) override;

	/** Nivel que se está jugando (1 el primero). */
	int32 GetCurrentLevel() const { return CurrentLevel; }

protected:
	/** Segundos entre que llega el último vivo y sale el siguiente nivel. */
	UPROPERTY(EditDefaultsOnly, Category = "Survival", meta = (ClampMin = "0.5"))
	float LevelTransitionSeconds = 3.f;

	/** Espera máxima a que el suelo del mapa nuevo tenga colisión antes de soltar a los jugadores en él. */
	UPROPERTY(EditDefaultsOnly, Category = "Survival", meta = (ClampMin = "1.0"))
	float LevelReadyTimeoutSeconds = 10.f;

	virtual void OnWaitingTimeout() override;

	/** Quien no estaba al empezar, o se fue, no puede ganar: espera como espectador (#345). */
	virtual ETNLateJoinPolicy GetLateJoinPolicy() const override { return ETNLateJoinPolicy::SpectateUntilMatchEnds; }

	/** «?game=» vacío: sin él, el lobby heredaría ?game=Survival y cargaría con este GameMode en vez del HQ (#156). */
	virtual FString GetLobbyTravelOptions() const override { return TEXT("?game="); }

	/** Aplica las reglas de TNSurvivalLogic: seguir, siguiente nivel o fin de partida. */
	virtual void UpdateRoundProgressAndMaybeFinish() override;

private:
	/** Lo que se apunta de cada muerto para desempatar y ordenar. Key = PlayerId. */
	struct FDeathRecord
	{
		int32 Level = 0;
		float Time = 0.f;
		float Remaining = 0.f;
	};
	TMap<int32, FDeathRecord> DeathRecords;

	/** Pawn oculto de quien llegó a la meta: se reutiliza al empezar el siguiente nivel. Key = PlayerId. */
	TMap<int32, TWeakObjectPtr<APawn>> FinishedPawns;

	/** Jugadores que se han ido durante la partida (su PlayerState puede seguir un momento en PlayerArray). */
	TSet<int32> LeftPlayerIds;

	int32 CurrentLevel = 1;
	int32 StartingPlayers = 1;
	bool bMatchOver = false;
	/** Entre que se genera un mapa y se suelta a los jugadores en él: el nivel aún no se evalúa. */
	bool bLevelLoading = false;
	float LevelLoadStartTime = 0.f;
	FTimerHandle LevelTransitionTimerHandle;
	FTimerHandle LevelReadyPollHandle;

	ATN_ChunkManager* FindChunkManager() const;

	/** Semilla de ?SurvivalMap= si es un mapa del catálogo (o el de pruebas); 0 si no se pide o no es válida. */
	uint32 ParseFirstLevelMap() const;

	/** Estado de los jugadores que siguen en la partida, para TNSurvivalLogic. */
	TArray<FTNSurvivalPlayer> GatherPlayers() const;

	/** Espera a que el suelo de la salida del mapa tenga colisión (o LevelReadyTimeoutSeconds) y suelta a los vivos. */
	void BeginLevelWhenReady();
	void PollLevelReady();

	/** Lleva a los vivos a la salida del mapa del nivel, con el pawn que tenían o uno nuevo. */
	void SendSurvivorsToLevelStart();

	/** Siguiente nivel: lo genera y devuelve a los vivos a la salida. */
	void AdvanceLevel();

	/** Copia CurrentLevel al GameState, que lo replica a los invitados. */
	void PublishLevel();

	/** Fin de partida: puestos en el marcador y Resultados. */
	void FinishSurvival(int32 WinnerId);
};
