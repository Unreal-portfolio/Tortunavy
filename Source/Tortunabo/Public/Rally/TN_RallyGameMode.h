// Carrera del Rally (Docs/Rally_MVP.md): opciones de URL, emparejado en buggies, fases, puertas en orden, vueltas, puestos a
// 5 Hz, contramano, reaparición, meta y resultados. Todo lo decide el servidor; el estado sale por ATN_RallyGameState.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyGameState.h"
#include "Rally/TN_RallyTrack.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TN_RallyGameMode.generated.h"

class ATN_RallyAIController;
class ATN_RallyPlayerState;
class ATN_RallyTrack;
class ATN_RallyPodium;
class APlayerController;

namespace TNRallyRace
{
	/** Señales de reaparición que un buggy tiene pendientes en este fotograma (la carrera las consume todas a la vez). */
	struct FRespawnSignals
	{
		bool bFellOutOfWorld = false;
		bool bDestroyed = false;
		bool bRequested = false;
	};

	/**
	 * Qué reaparición toca a un equipo (None = ninguna). Caer bajo el KillZ y reventar reaparecen en cualquier fase y sin mirar
	 * la inmunidad (durante ella no recibe daño); la petición de R, solo en carrera, sin haber llegado y fuera de la inmunidad.
	 * Un equipo retirado (buggy vacío) no reaparece al reventar: se queda donde está en vez de ocupar un carril de reaparición.
	 */
	TORTUNABO_API ETNRallyRespawnReason ResolveRespawn(const FRespawnSignals& Signals, bool bRacing, bool bFinished, bool bRetired,
		bool bImmune);
}

/**
 * Carreras terminadas en esta sesión de juego, para ?Races=N. Vive en la GameInstance: sobrevive al ?Restart (que crea otro
 * GameMode) y se pierde al cerrar la sesión de PIE, a diferencia de una variable estática.
 */
UCLASS()
class TORTUNABO_API UTN_RallyRaceCounter : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	int32 RacesRun = 0;
};

UCLASS()
class TORTUNABO_API ATN_RallyGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATN_RallyGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override { return false; }
	virtual void Logout(AController* Exiting) override;

	UFUNCTION(BlueprintPure, Category = "Rally")
	FName GetVariant() const { return Variant; }

	/** Buggy (ATN_Buggy, de Vehicles/): clase blanda para no depender de ella al compilar el Rally. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally")
	TSoftClassPtr<APawn> VehicleClass;

	UPROPERTY(EditDefaultsOnly, Category = "Rally")
	TSubclassOf<ATN_RallyAIController> AIControllerClass;

	UPROPERTY(EditDefaultsOnly, Category = "Rally")
	FName DefaultVariant = TEXT("E01B_espana_rally");

	/** Vueltas por defecto en circuito (?Laps= y luego el laps del manifest mandan); en punto a punto siempre 1. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally", meta = (ClampMin = "1", ClampMax = "9"))
	int32 DefaultLaps = 2;

	/** Calentamiento tras la llegada de la última tortuga (s) y tope desde la primera. Solo lo cuenta el servidor: no se enseña. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Tiempos")
	float WarmupSeconds = 5.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Tiempos")
	float WarmupMaxSeconds = 20.f;

	/** Semáforo: motores cortados y buggies frenados en su hueco hasta el verde. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Tiempos")
	float CountdownSeconds = 3.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Tiempos")
	float FinishGraceSeconds = 20.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Tiempos")
	float ResultsSeconds = 15.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Tiempos")
	float RespawnLockSeconds = 3.f;

	/** Fantasma tras reaparecer; nunca menos que RespawnLockSeconds + 0,5 s (TNRallyRace::EffectiveGhostSeconds, #103). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Tiempos")
	float RespawnGhostSeconds = 3.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Tiempos")
	float TurnAroundGhostSeconds = 1.f;

	/** Periodo de los puestos y de las comprobaciones (contramano, atasco, fuera de pista): 5 Hz. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Tiempos")
	float EvaluateInterval = 0.2f;

private:
	static constexpr int32 RespawnReasonCount = static_cast<int32>(ETNRallyRespawnReason::Destroyed) + 1;

	/** Estado del servidor de cada buggy (lo replicado va en FTNRallyStanding). */
	struct FTeamRuntime
	{
		int32 TeamIndex = INDEX_NONE;
		TWeakObjectPtr<APawn> Vehicle;
		bool bBot = false;
		/** Equipo de una jugadora artillera con piloto IA al volante (?BotDriver o TN.Rally.BotDriver 1, #298). */
		bool bBotDriver = false;
		bool bRetired = false;
		int32 GridSlot = 0;
		/** Hueco de la parrilla con el buggy apoyado en el suelo, para la reaparición antes de la salida. */
		FTransform GridTransform;
		/** Altura del origen del buggy sobre el punto más bajo de sus ruedas (medida al crearlo; cm). */
		double OriginAboveBottomCm = TNRallyRace::FallbackOriginAboveBottomCm;
		int32 GatesPassed = 0;
		int32 LastGate = INDEX_NONE;
		/** Recorrido real desde la última puerta validada (regla del 60 %). */
		double OdometerCm = 0.0;
		double Arc = 0.0;
		double SegmentProgressCm = 0.0;
		FVector PrevLocation = FVector::ZeroVector;
		bool bFinished = false;
		double FinishSeconds = 0.0;
		/** Orden de llegada (1 = el primero; 0 sin llegar): su hueco en el podio. */
		int32 FinishOrder = 0;
		/** Hora del servidor a la que se aparca en el podio (tras el plano lateral de la llegada) y si ya está aparcado. */
		double ParkAtTime = 0.0;
		bool bParked = false;
		int32 Place = 0;
		bool bWrongWay = false;
		double RespawnEndTime = 0.0;
		/** Sin comprobaciones de reaparición hasta esta hora (tras reaparecer o girar). */
		double ImmuneUntil = 0.0;
		/** Última reaparición (motivo y hora del servidor), replicada en FTNRallyStanding para el HUD. */
		ETNRallyRespawnReason LastRespawnReason = ETNRallyRespawnReason::None;
		double LastRespawnTime = 0.0;
		TNRally::FWrongWayState WrongWay;
		TNRally::FStuckState Stuck;
		TNRally::FOffTrackState OffTrack;
		/** Estadística de la carrera (línea [RallyStats] al dar los resultados): reapariciones por motivo y vuelcos. */
		int32 Respawns[RespawnReasonCount] = {};
		int32 Flips = 0;
		int32 TurnArounds = 0;
		bool bWasFlipped = false;
	};

	ATN_RallyGameState* GetRallyGameState() const;
	double Now() const;
	TNRally::FLapRules MakeLapRules() const;
	UTN_RallyRaceCounter* GetRaceCounter() const;

	void AssignPlayer(APlayerController* Player);
	/** Biplaza: sienta a la jugadora de artillera en el primer buggy de jugadoras con la torreta libre. */
	bool TrySeatAsGunner(APlayerController* Player, ATN_RallyPlayerState& RallyPlayer);
	/** ?BotDriver o TN.Rally.BotDriver 1: la jugadora entra de artillera y conduce el piloto IA. */
	bool IsBotDriverMode() const;
	/** Sienta un piloto IA al volante del equipo Index y a la jugadora en la torreta. */
	bool SeatWithBotDriver(int32 Index, APlayerController* Player);
	/** Crea el piloto IA y lo sienta al volante del equipo Index; nullptr (y nada creado) si no puede. */
	AController* SpawnPilotFor(int32 Index, const FText& PilotName);
	int32 CreateTeam(bool bBot);
	int32 FindFreeGridSlot() const;
	/** Hueco para un equipo nuevo: las jugadoras delante; si un bot ocupa un hueco anterior, el bot pasa al libre. */
	int32 ClaimGridSlot(bool bBot);
	/** Hueco Slot con el buggy apoyado en el suelo (traza hacia abajo + TNRallyRace::RestingLiftCm). */
	FTransform GetRestingGridTransform(int32 Slot, double OriginAboveBottomCm) const;
	/** Coloca el equipo en el hueco Slot, apoyado en el suelo. */
	void PlaceTeamOnGrid(FTeamRuntime& Team, int32 Slot);
	void SpawnBots();
	FTeamRuntime* FindTeamByController(const AController* Controller);
	FTeamRuntime* FindTeamByVehicle(const AActor* Vehicle);
	const FTeamRuntime* FindTeamByVehicle(const AActor* Vehicle) const;
	void Spectate(APlayerController* Player);
	int32 CountSeatedHumans() const;
	void OnHumanSeated();

	void UpdatePhase();
	void StartCountdown();
	void StartRacing();
	void StartFinishing();
	void StartResults();
	/** Fin de los resultados: carrera nueva con ?Restart o, con ?Races=N cumplido, cierra el juego. */
	void RestartOrQuit();
	void SetAllEnginesLocked(bool bLocked);
	bool IsRaceRunning() const;
	bool IsBeforeStart() const;
	/** Quita (antes de la salida) o retira los equipos sin buggy o con el buggy vacío (TNRally::DecideTeamCleanup). */
	void CleanupTeams();
	/** Torretas activas solo en Racing y Finishing y en equipos sin retirar (TNRally::AreWeaponsLive). */
	void ApplyWeaponLocks();

	/** Calentamiento y semáforo: pone el freno de carrera a cada buggy en su hueco (ITN_RallyVehicle::SetRaceBrakeHeld). */
	void HoldBuggiesOnGrid();
	void TickProgress();
	void HandleGateCrossing(FTeamRuntime& Team, int32 GateIndex, bool bForward, double Alpha, float DeltaSeconds);
	/** Cajas «?» (#629) por las que ha pasado el buggy del equipo entre From y To: le dan munición según su puesto. */
	void CheckItemBoxes(FTeamRuntime& Team, const FVector& From, const FVector& To);
	void EvaluateTeams(double DeltaSeconds);
	void EvaluateTeam(FTeamRuntime& Team, double DeltaSeconds);
	/** Avance dentro del tramo actual (desde la última puerta, o desde la parrilla antes de la salida). */
	void UpdateSegmentProgress(FTeamRuntime& Team) const;
	/** Cuenta un vuelco nuevo para las estadísticas de la carrera. */
	void CountFlip(FTeamRuntime& Team, const APawn& Vehicle, const FVector& Location);
	bool IsInHazard(const FVector& Location) const;
	/** Antes de la salida, su hueco; después, el primer carril libre tras la última puerta, apoyado en el suelo. */
	FTransform ChooseRespawnTransform(const FTeamRuntime& Team) const;
	void RespawnTeam(FTeamRuntime& Team, ETNRallyRespawnReason Reason);
	/** Peticiones de reaparición de las ocupantes (ITN_RallyVehicle::ConsumeRespawnRequest): se atienden en carrera. */
	void ConsumeRespawnRequests(bool bRacing);
	void TurnAround(FTeamRuntime& Team);
	void RebuildStandings();
	void RefreshSeats();
	// TN_RallyGameModePodium.cpp (#306)
	/** Crea el podio sobre la meta (servidor, al tener pista). */
	void SpawnPodium();
	/** Marca la llegada de un equipo: su orden y cuándo se aparca. */
	void NoteTeamFinished(FTeamRuntime& Team);
	/** Aparca en el podio los equipos que ya llegaron y cuya hora de aparcar ha pasado. */
	void ParkFinishedTeams();
	/** Lleva el buggy a su hueco del podio con el motor, el freno y la torreta bloqueados. */
	void ParkTeam(FTeamRuntime& Team);
	/** Una línea [RallyStats] con terminados, reapariciones por motivo, vuelcos y tiempo del ganador (pruebas del piloto IA). */
	void LogRaceStats(bool bTimedOut) const;

	UPROPERTY(Transient)
	TObjectPtr<ATN_RallyTrack> Track;

	UPROPERTY(Transient)
	TObjectPtr<ATN_RallyPodium> Podium;

	TArray<FTeamRuntime> Teams;
	TArray<TWeakObjectPtr<APlayerController>> PendingPlayers;

	FName Variant;
	int32 Seats = 2;
	int32 Bots = 0;
	int32 Laps = 2;
	/** ?Laps= en la URL manda sobre el laps del manifest. */
	bool bLapsFromUrl = false;
	int32 NextTeamIndex = 0;
	double FirstSeatTime = -1.0;
	/** Fin del calentamiento (hora del servidor; 0 = sin fijar). No se replica: el HUD solo enseña «esperando». */
	double WarmupEndTime = 0.0;
	/** ?BotDriver en la URL (la CVar TN.Rally.BotDriver se lee al sentar a cada jugadora). */
	bool bBotDriverFromUrl = false;
	double EvaluateAccumulator = 0.0;
	bool bTrackReady = false;
	bool bLoggedMissingVehicle = false;
	bool bRestartRequested = false;

	// Opciones de prueba (servidor sin jugadoras, carreras de la IA en bucle):
	/** ?AutoStart: el calentamiento empieza solo aunque no se siente ninguna jugadora (carrera solo de bots). */
	bool bAutoStart = false;
	/** ?RaceTimeout=S: la carrera pasa a resultados a los S s del verde (0 = sin tope). */
	float RaceTimeoutSeconds = 0.f;
	/** ?Races=N: tras los resultados de la carrera N del proceso, el juego se cierra en vez de empezar otra (0 = nunca). */
	int32 RaceLimit = 0;
};
