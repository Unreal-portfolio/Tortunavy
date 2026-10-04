// Karts en el mapa generado del cooperativo (#291): LVL_ProcMap?game=Karts (alias «Karts» en DefaultEngine.ini), un modo
// propio (decisión del 04-10 en #627) con su mapa generado, sus objetos (ATN_KartBuggy con UTN_KartItemComponent) y sus
// bots. Reutiliza la carrera del Rally (ATN_RallyGameMode: emparejado, fases, puertas con la regla del 60 %, puestos,
// contramano, reaparición, podio y, desde #631, bots con la dificultad del lobby, plazas del anfitrión y vuelta al lobby,
// comunes con el Rally de LVL_Rally). Se elige en el menú, en la sala y con el general del lobby (ETNProcGameMode::Karts,
// ATN_HQGameMode::BeginMatchTravel). El servidor genera el mapa del cooperativo con su perfil y la dificultad del lobby,
// con el camino hecho para el kart (TNProcMap::FGenParams::bDrivable), y la pista sale del camino principal
// (ATN_KartTrack). Las tortugas no se sientan hasta que todas tienen el mapa y la colisión del suelo en su máquina. Al
// acabar, todas vuelven al lobby.
//
// Pruebas sin lobby: open LVL_ProcMap?game=Karts?ProcDifficulty=Easy|Normal|Hard?ProcSeed=N?Bots=N?Seats=1|2. Con
// ?AutoStart, ?RaceTimeout=S y ?Races=N, carreras solo de la IA en un servidor sin jugadoras (como en LVL_Rally).
#pragma once

#include "CoreMinimal.h"
#include "Rally/TN_RallyGameMode.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "TN_KartGameMode.generated.h"

class ATN_KartGameState;
class ATN_ProcMapGenerator;
class UTN_ProcMapSettings;

UCLASS()
class TORTUNABO_API ATN_KartGameMode : public ATN_RallyGameMode
{
	GENERATED_BODY()

public:
	ATN_KartGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	/**
	 * Servidor, desde ATN_KartGameState::PrepareTrack (en ATN_RallyGameMode::StartPlay): genera el mapa del cooperativo
	 * para los karts con la dificultad y la semilla de la partida. nullptr si el nivel no tiene ni puede crear generador.
	 */
	ATN_ProcMapGenerator* GenerateMap();

	/** Una tortuga tiene ya en su máquina la pista y la colisión del suelo de esta generación (ATN_KartPlayerController). */
	void NotifyClientTrackReady(APlayerController* Player, int32 Generation);

	UFUNCTION(BlueprintPure, Category = "Karts")
	int32 GetMapSeed() const { return MapSeed; }

	/** Ajustes del mapa si el generador del nivel no trae los suyos. */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Mapa")
	TSoftObjectPtr<UTN_ProcMapSettings> MapSettings;

	/** Semilla fija (0 = aleatoria); ?ProcSeed= manda. */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Mapa")
	int32 FixedSeed = 0;

	/** Tope de espera a que todas tengan el mapa y el suelo (s): luego se sientan las que estén. */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Mapa")
	float PlayersReadyTimeoutSeconds = 40.f;

private:
	virtual int32 GetForcedSeats() const override;
	virtual int32 GetForcedBots() const override;
	ATN_KartGameState* GetKartState() const;
	ATN_ProcMapGenerator* EnsureGenerator();
	/** Todas las tortugas esperadas están conectadas y tienen el mapa y el suelo en su máquina (y el servidor también). */
	bool AreAllPlayersReady() const;
	/** Sienta a las que esperaban (ATN_RallyGameMode::HandleStartingNewPlayer) y deja entrar a las siguientes sin espera. */
	void ReleaseWaitingPlayers(const TCHAR* Why);

	UPROPERTY(Transient)
	TObjectPtr<ATN_ProcMapGenerator> Generator;

	int32 UrlSeed = 0;
	int32 MapSeed = 0;
	/** Tortugas que esperan a que todas tengan el mapa (aún sin kart). */
	TArray<TWeakObjectPtr<APlayerController>> WaitingPlayers;
	bool bPlayersReleased = false;
	/** Hora (s del mundo) en que empezó la espera. */
	double WaitStartTime = -1.0;
	double NextReadyCheckTime = 0.0;

	/** Última generación del mapa con la pista y el suelo listos en cada máquina cliente. */
	TMap<TWeakObjectPtr<APlayerController>, int32> ClientTrackGeneration;

	/** Diagnóstico (LogTNRally Verbose): sitio, velocidad y arco de cada kart cada segundo en los primeros 20 s. */
	void LogStartDiagnostics();
	double NextStartLogTime = 0.0;
};
