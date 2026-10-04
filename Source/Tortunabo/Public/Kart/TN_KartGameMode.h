// Karts en el mapa generado del cooperativo (#291): LVL_ProcMap?game=Karts (alias «Karts» en DefaultEngine.ini), un modo
// aparte del Rally de LVL_Rally que reutiliza su carrera (ATN_RallyGameMode: emparejado, fases, puertas con la regla del
// 60 %, puestos, contramano, reaparición, podio) y el buggy de SkiTemplar (ATN_Buggy) sin tocarlos. Se elige en el menú,
// en la sala y con el general del lobby (ETNProcGameMode::Karts, ATN_HQGameMode::BeginMatchTravel). El servidor genera el
// mapa del cooperativo con su perfil y la dificultad del lobby, con el camino hecho para el kart (TNProcMap::FGenParams::
// bDrivable), y la pista sale del camino principal (ATN_KartTrack). 1 o 2 tortugas por kart (?Seats=, TN.Kart.Seats): la
// segunda va de artillera. Las tortugas no se sientan hasta que todas tienen el mapa y la colisión del suelo en su máquina;
// los bots completan la parrilla hasta MinKarts. Al acabar, todas vuelven al lobby.
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
class ATN_RallyAIController;
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

	/** Acaba la carrera y lleva a todas al lobby del que salieron (fin de los resultados o menú de pausa del anfitrión). */
	void ReturnToLobbyNow();

	/** Ajusta un piloto IA a la dificultad (ATN_KartAIController al nacer): velocidad con algo de variedad entre bots. */
	void ConfigureBot(ATN_RallyAIController& Pilot);

	UFUNCTION(BlueprintPure, Category = "Karts")
	ETNProcDifficulty GetProcDifficulty() const { return Difficulty; }

	UFUNCTION(BlueprintPure, Category = "Karts")
	int32 GetMapSeed() const { return MapSeed; }

	/** Karts mínimos en la parrilla: si faltan tortugas, los completan bots (sin ?Bots= en la URL). */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Bots", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MinKarts = 4;

	/** Velocidad máxima de los bots en recta por dificultad (km/h): fácil, normal y difícil. */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Bots")
	FVector BotMaxSpeedKmh = FVector(74.f, 84.f, 94.f);

	/** Probabilidad de que un bot gaste su munición especial al disparar, por dificultad. */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Bots")
	FVector BotSpecialFireChance = FVector(0.15f, 0.3f, 0.45f);

	/** Segundos entre disparos de la torreta de los bots y su alcance (cm), por dificultad: menos tiroteo que en el Rally. */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Bots")
	FVector BotFireIntervalSeconds = FVector(2.5f, 1.6f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category = "Karts|Bots")
	FVector BotFireRangeCm = FVector(2500.f, 3000.f, 3500.f);

	/** Plazas por kart por defecto (1 = cada tortuga conduce el suyo; 2 = la segunda va de artillera). */
	UPROPERTY(EditDefaultsOnly, Category = "Karts", meta = (ClampMin = "1", ClampMax = "2"))
	int32 DefaultSeats = 2;

	/** Ajustes del mapa si el generador del nivel no trae los suyos. */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Mapa")
	TSoftObjectPtr<UTN_ProcMapSettings> MapSettings;

	/** Semilla fija (0 = aleatoria); ?ProcSeed= manda. */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Mapa")
	int32 FixedSeed = 0;

	/** Tope de espera a que todas tengan el mapa y el suelo (s): luego se sientan las que estén. */
	UPROPERTY(EditDefaultsOnly, Category = "Karts|Mapa")
	float PlayersReadyTimeoutSeconds = 40.f;

	/** Margen antes del fin de los resultados en que se viaja al lobby (s): se adelanta a la carrera nueva del Rally. */
	UPROPERTY(EditDefaultsOnly, Category = "Karts")
	float LobbyTravelLeadSeconds = 0.25f;

private:
	ATN_KartGameState* GetKartState() const;
	ATN_ProcMapGenerator* EnsureGenerator();
	/** Todas las tortugas esperadas están conectadas y tienen el mapa y el suelo en su máquina (y el servidor también). */
	bool AreAllPlayersReady() const;
	/** Sienta a las que esperaban (ATN_RallyGameMode::HandleStartingNewPlayer) y deja entrar a las siguientes sin espera. */
	void ReleaseWaitingPlayers(const TCHAR* Why);

	UPROPERTY(Transient)
	TObjectPtr<ATN_ProcMapGenerator> Generator;

	ETNProcDifficulty Difficulty = ETNProcDifficulty::Normal;
	int32 UrlSeed = 0;
	int32 MapSeed = 0;
	int32 ExpectedHumans = 1;
	int32 NextBotOrdinal = 0;
	/** Tortugas que esperan a que todas tengan el mapa (aún sin kart). */
	TArray<TWeakObjectPtr<APlayerController>> WaitingPlayers;
	bool bPlayersReleased = false;
	/** Hora (s del mundo) en que empezó la espera. */
	double WaitStartTime = -1.0;
	double NextReadyCheckTime = 0.0;
	bool bReturning = false;
	/** Con ?Races=N (pruebas de la IA) no se vuelve al lobby: el Rally repite o cierra el juego. */
	bool bReturnToLobbyAfterResults = true;

	/** Última generación del mapa con la pista y el suelo listos en cada máquina cliente. */
	TMap<TWeakObjectPtr<APlayerController>, int32> ClientTrackGeneration;

	/** Diagnóstico (LogTNRally Verbose): sitio, velocidad y arco de cada kart cada segundo en los primeros 20 s. */
	void LogStartDiagnostics();
	double NextStartLogTime = 0.0;
};
