// Estado replicado de la carrera del Rally: fase, horas del servidor (semáforo, cierre y resultados), variante y puestos.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "TN_RallyGameState.generated.h"

class APawn;
class APlayerState;
class ATN_RallyTrack;

UENUM(BlueprintType)
enum class ETNRallyPhase : uint8
{
	/** Llegan las tortugas y se sientan; motores cortados. Sin cuenta en pantalla: el HUD enseña «esperando» (IsWaitingForPlayers). */
	Warmup,
	/** Semáforo de 3 s, el único temporizador visible de la salida: motores cortados y buggies frenados en su hueco. */
	Countdown,
	Racing,
	/** El primero ya ha llegado: quedan 20 s para los demás. */
	Finishing,
	/** Tabla de resultados 15 s y carrera nueva en el mismo mapa. */
	Results
};

/** Por qué ha reaparecido un buggy (lo decide el servidor en ATN_RallyGameMode::RespawnTeam). */
UENUM(BlueprintType)
enum class ETNRallyRespawnReason : uint8
{
	None,
	/** Agua, zona de muerte del manifest o bajo el KillZ del mundo. */
	Hazard,
	/** A más de 40 m del eje durante 1 s. */
	OffTrack,
	/** 8 s sin salir de un radio de 5 m. */
	Stuck,
	/** Una ocupante ha mantenido R (o Y) 1,5 s. */
	Request,
	/** El buggy ha reventado (vida 0, UTN_BuggyHealthComponent). */
	Destroyed
};

/** Un registro por buggy (equipo): ocupantes, progreso y puesto. Lo rellena el servidor a 5 Hz, ordenado por puesto. */
USTRUCT(BlueprintType)
struct FTNRallyStanding
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Rally")
	int32 TeamIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Rally")
	TObjectPtr<APawn> Vehicle = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Rally")
	TObjectPtr<APlayerState> Driver = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Rally")
	TObjectPtr<APlayerState> Gunner = nullptr;

	/** 1 = primero. */
	UPROPERTY(BlueprintReadOnly, Category = "Rally")
	int32 Place = 0;

	/** Vuelta en curso (1..Laps); 0 antes de cruzar la salida. */
	UPROPERTY(BlueprintReadOnly, Category = "Rally")
	int32 Lap = 0;

	/** Siguiente puerta que tiene que cruzar (índice en la pista). */
	UPROPERTY(BlueprintReadOnly, Category = "Rally")
	int32 NextGate = 0;

	/** Segundos desde la salida al cruzar la meta. */
	UPROPERTY(BlueprintReadOnly, Category = "Rally")
	float FinishSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Rally")
	bool bFinished = false;

	/** Sin ocupantes en plena carrera. */
	UPROPERTY(BlueprintReadOnly, Category = "Rally")
	bool bRetired = false;

	UPROPERTY(BlueprintReadOnly, Category = "Rally")
	bool bWrongWay = false;

	UPROPERTY(BlueprintReadOnly, Category = "Rally")
	bool bBot = false;

	/** Hora del servidor a la que acaba la espera de la reaparición en curso (0 = no reaparece). */
	UPROPERTY(BlueprintReadOnly, Category = "Rally")
	float RespawnEndServerTime = 0.f;

	/** Motivo de la última reaparición del buggy (None si no ha reaparecido): el HUD lo explica. */
	UPROPERTY(BlueprintReadOnly, Category = "Rally")
	ETNRallyRespawnReason LastRespawnReason = ETNRallyRespawnReason::None;

	/** Hora del servidor de la última reaparición (0 = ninguna): sirve para saber si es nueva. */
	UPROPERTY(BlueprintReadOnly, Category = "Rally")
	float LastRespawnServerTime = 0.f;

	/** Puntos de copa por el puesto (10-8-6-5-4-3-2-1; 0 sin llegar). Definitivos en Results. */
	UPROPERTY(BlueprintReadOnly, Category = "Rally")
	int32 Points = 0;
};

DECLARE_MULTICAST_DELEGATE(FTNRallyTrackReady);
/** Un equipo acaba de reaparecer: índice del equipo y motivo. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FTNRallyTeamRespawned, int32 /*TeamIndex*/, ETNRallyRespawnReason /*Reason*/);

UCLASS()
class TORTUNABO_API ATN_RallyGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Sin huecos nulos en el PlayerArray antes de que el motor los marque: viaja al mapa de transición y vuelve (#711). */
	virtual void SeamlessTravelTransitionCheckpoint(bool bToTransitionMap) override;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Rally")
	ETNRallyPhase Phase = ETNRallyPhase::Warmup;

	/** Hora del servidor del verde del semáforo (StartServerTime). */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Rally")
	float StartServerTime = 0.f;

	/**
	 * Fin de la fase en curso (semáforo, cierre tras el primero o resultados); 0 = sin cuenta. En el calentamiento siempre es
	 * 0: su espera la lleva el servidor y no se enseña (#289).
	 */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Rally")
	float PhaseEndServerTime = 0.f;

	/** Calentamiento: se espera a que lleguen y se sienten las tortugas, sin cuenta atrás en pantalla. */
	UFUNCTION(BlueprintPure, Category = "Rally")
	bool IsWaitingForPlayers() const { return Phase == ETNRallyPhase::Warmup; }

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Rally")
	int32 Laps = 1;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Rally")
	int32 NumGates = 0;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Rally")
	bool bCircuit = false;

	/** Variante del mapa: los clientes cargan la misma en su ATN_MapVariantLoader y construyen su pista. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Variant, Category = "Rally")
	FName Variant;

	/** Ordenado por puesto. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Standings, Category = "Rally")
	TArray<FTNRallyStanding> Standings;

	/**
	 * Se dispara en cada máquina cuando un equipo reaparece: en el servidor al momento (NotifyTeamRespawned) y en los
	 * clientes al llegar unos puestos con una reaparición nueva. Lo usa el HUD para explicar el motivo.
	 */
	FTNRallyTeamRespawned OnTeamRespawned;

	/** Solo servidor (lo llama el GameMode): avisa de una reaparición en esta máquina. */
	void NotifyTeamRespawned(int32 TeamIndex, ETNRallyRespawnReason Reason, float ServerTime);

	/** Registro del buggy en que va este jugador (conductora o artillera); nullptr si no va en ninguno. */
	const FTNRallyStanding* FindStandingForPlayer(const APlayerState* Player) const;
	const FTNRallyStanding* FindStandingForVehicle(const APawn* Vehicle) const;

	/** Pista de esta máquina (la construye el GameMode en el servidor y OnRep_Variant en los clientes). */
	ATN_RallyTrack* GetTrack() const { return Track; }

	/**
	 * Fija Variant en el ATN_MapVariantLoader del nivel (lo crea si no hay), recarga el terreno si hace falta y construye
	 * la pista (ATN_RallyTrack del nivel o una nueva). Lo usan el GameMode (servidor) y OnRep_Variant (clientes). Virtual
	 * para los modos que hacen su pista de otra forma (los karts del mapa del cooperativo, ATN_KartGameState).
	 */
	virtual ATN_RallyTrack* PrepareTrack(FName InVariant);

	/** Se dispara al tener pista en esta máquina. */
	FTNRallyTrackReady OnTrackReady;

	/**
	 * Al acabar los resultados se vuelve al lobby (los karts, ATN_KartGameState) en vez de empezar otra carrera en el mismo
	 * mapa. Solo cambia el texto del pie de los resultados (UTN_RallyHUDWidget).
	 */
	virtual bool ReturnsToLobbyAfterResults() const { return false; }

	/** Texto de una línea con la fase y los puestos (TN.Rally.Status). */
	FString DescribeStatus() const;

protected:
	UFUNCTION()
	void OnRep_Variant();

	UFUNCTION()
	void OnRep_Standings();

	/** Pista de esta máquina (protegida para los modos que la construyen de otra forma). */
	UPROPERTY(Transient)
	TObjectPtr<ATN_RallyTrack> Track;

private:
	/** Última reaparición ya avisada de cada equipo (índice → hora del servidor), para no repetir el aviso. */
	TMap<int32, float> NotifiedRespawnTimes;
};
