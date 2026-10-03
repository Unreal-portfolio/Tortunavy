#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "TN_TutorialPlayerComponent.generated.h"

class APlayerController;
class ATN_TutorialCourse;
class ATortugaCharacter;
class UTN_TutorialWidget;

/**
 * @brief Tutorial de cada jugador (Docs/Tutorial.md): va en su PlayerController (lo engancha el servidor del lobby,
 * ATN_HQGameMode) y se replica a su dueño.
 *
 * - En la máquina del jugador (cliente o anfitrión): al llegar al lobby mira su guardado local (UMP_GameInstance,
 *   UTN_TutorialSaveGame) y, si no ha hecho el tutorial, se lo pide al servidor. Mientras lo hace, sigue lo que hace la
 *   tortuga para tachar las tareas de cada estación (moverse, correr, el caparazón, los objetos... con las teclas que
 *   tenga puestas, de teclado o de mando) y enseña el cartel del HUD (UTN_TutorialWidget). Al acabar (o al saltarlo),
 *   lo apunta en su guardado.
 * - En el servidor: recibe las peticiones (empezar, saltar, ir a una estación) y se las pasa al recorrido
 *   (ATN_TutorialCourse), que decide y avisa con bInTutorial y los RPC de cliente.
 *
 * El progreso de las tareas es solo local (para el HUD): el servidor solo necesita saber quién está dentro, por dónde va
 * (lo mira él mismo) y cuándo acaba.
 */
UCLASS(ClassGroup = (Custom))
class TORTUNABO_API UTN_TutorialPlayerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_TutorialPlayerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Servidor: el componente del jugador (lo crea y lo replica si no lo tiene). */
	static UTN_TutorialPlayerComponent* EnsureFor(APlayerController* PC);

	static UTN_TutorialPlayerComponent* FindFor(const APlayerController* PC);

	/** El del jugador local de este mundo (consola, menú de pausa). */
	static UTN_TutorialPlayerComponent* FindLocal(const UObject* WorldContext);

	/** true mientras el jugador está en el tutorial (lo dice el servidor). */
	bool IsInTutorial() const { return bInTutorial; }

	// ── Jugador local ────────────────────────────────────────────────────────

	/** Pide empezar el tutorial (bForce: aunque ya lo haya hecho, desde el principio). */
	void RequestStart(bool bForce);

	/** Salta el tutorial (menú de pausa, consola): vuelve al lobby y queda apuntado como hecho. */
	void RequestSkip();

	/** Va a la estación N (0 = la primera; consola). */
	void RequestStation(int32 StationIndex);

	/** Estación en la que está la tortuga local y si cada tarea está hecha (para la consola). */
	int32 GetCurrentStation() const { return CurrentStation; }

	// ── Servidor ─────────────────────────────────────────────────────────────

	/** El recorrido mete o saca al jugador. */
	void SetInTutorialOnServer(bool bIn);

	/** Tarea que decide el servidor (el cangrejo alcanzado). */
	UFUNCTION(Client, Reliable)
	void ClientTaskDone(uint8 StationIndex, uint8 TaskIndex);

	/** Fin del tutorial: se apunta en el guardado local. bSkipped: saltado; si no, por la cascada. */
	UFUNCTION(Client, Reliable)
	void ClientTutorialFinished(bool bSkipped);

	/**
	 * Repetir el tutorial estando ya dentro (volver a la salida): el HUD empieza de cero. bInTutorial no cambia, así que
	 * no hay nada que replicar y el cliente no se enteraría (ShouldResetProgress, #85).
	 */
	UFUNCTION(Client, Reliable)
	void ClientResetProgress();

protected:
	UFUNCTION(Server, Reliable)
	void ServerRequestTutorial(bool bForce);

	UFUNCTION(Server, Reliable)
	void ServerSkipTutorial();

	UFUNCTION(Server, Reliable)
	void ServerGoToStation(int32 StationIndex);

	UPROPERTY(ReplicatedUsing = OnRep_InTutorial)
	bool bInTutorial = false;

	UFUNCTION()
	void OnRep_InTutorial();

private:
	UPROPERTY(Transient)
	TObjectPtr<UTN_TutorialWidget> Widget;

	/** Lo que se ha visto de bInTutorial en esta máquina (para reaccionar solo al cambio). */
	bool bLocalInTutorial = false;

	/** Mundo en el que ya se ha mirado el guardado (una vez por lobby). */
	TWeakObjectPtr<UWorld> CheckedWorld;
	float PawnSeconds = 0.f;

	/** Tras la cascada: cayendo hacia el lobby (sin mover la tortuga en el aire) y cuánto queda el cartel final. */
	bool bFalling = false;
	bool bIgnoringMove = false;
	float FallSeconds = 0.f;
	float FinalMessageSeconds = 0.f;

	// ── Progreso local ───────────────────────────────────────────────────────

	int32 CurrentStation = INDEX_NONE;
	/** Tareas hechas: NumStations × MaxTasks. */
	TArray<bool> TaskDone;
	/** Última estación contada como aprendida (para la felicitación). */
	TArray<bool> StationCelebrated;

	// Medidas de lo que hace la tortuga en la estación de ahora.
	FVector LastPawnLocation = FVector::ZeroVector;
	bool bHasLastLocation = false;
	float MovedDistance = 0.f;
	float LastYaw = 0.f;
	bool bHasLastYaw = false;
	float TurnedDegrees = 0.f;
	float SprintSeconds = 0.f;
	float StationSeconds = 0.f;
	float ShellDistance = 0.f;
	bool bWasInShell = false;
	bool bWasCarrying = false;
	bool bWasCarried = false;
	float StruggleSeconds = 0.f;
	bool bWasSwimming = false;
	bool bSwamThisVisit = false;
	bool bCatapultBall = false;
	float ChatWheelHeld = 0.f;
	bool bDropKeyWasDown = false;
	FName LastEquipped = NAME_None;
	FName LastStored = NAME_None;

	/** Teclas de cada acción (se leen de los ajustes una vez por segundo) y si el último aparato usado es un mando. */
	float KeyRefreshTimer = 0.f;
	TMap<uint8, FText> KeyTexts;
	TMap<uint8, TArray<FKey>> KeyKeys;
	bool bGamepad = false;

	APlayerController* GetPC() const;
	bool IsLocal() const;
	ATN_TutorialCourse* GetCourse() const;

	void HandleInTutorialChanged();
	void ResetProgress();
	void TickLocal(float DeltaTime);
	void TickFinale(float DeltaTime, ATortugaCharacter* Turtle);
	/** Pasado el borde de la cascada: cae recta hacia el castillo, sin mover la tortuga en el aire. */
	void StartLocalFall(ATortugaCharacter* Turtle);
	void TickTasks(float DeltaTime, ATortugaCharacter* Turtle, ATN_TutorialCourse* Course);
	void EnterStation(int32 NewStation);
	void MarkTask(int32 StationIndex, int32 TaskIndex);
	bool IsTaskDone(int32 StationIndex, int32 TaskIndex) const;
	bool IsKeyDown(uint8 Key) const;
	void RefreshKeys();
	void RefreshWidget();
	void EnsureWidget();
	void RemoveWidget();
	void SaveCompleted(bool bSkipped);
};
