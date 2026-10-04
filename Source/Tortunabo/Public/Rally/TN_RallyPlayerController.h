// PlayerController del Rally, sin el lobby ni el HUD de la tortuga a pie: crea el HUD del Rally solo en los jugadores
// locales y manda al servidor los cosméticos guardados (las tortugas sentadas en el buggy los pintan desde el
// PlayerState). Los controles (conducir, disparar, enderezar y pedir la reaparición) los pone el buggy o la artillera.
// Voz (#329): la misma voz por proximidad del juego (UProximityVoiceComponent en el peón, «pulsar para hablar» de los
// ajustes) con interfono entre las dos ocupantes del buggy (ATN_RallyPlayerState::GetVoiceIntercomGroup).
#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "GameFramework/PlayerController.h"
#include "Player/TN_CosmeticsSync.h"
#include "Rally/TN_RallyCrewCalls.h"
#include "Rally/TN_RallyHitReport.h"
#include "Voice/TN_VoiceRouting.h"
#include "TN_RallyPlayerController.generated.h"

class APostProcessVolume;
class ATN_Buggy;
class UTN_RallyCameraDirector;
class UTN_RallyCopilotComponent;
class UTN_RallyHUDWidget;
class USoundBase;

UCLASS()
class TORTUNABO_API ATN_RallyPlayerController : public APlayerController, public ITN_VoiceListener
{
	GENERATED_BODY()

public:
	ATN_RallyPlayerController();

#if !UE_BUILD_SHIPPING
	/**
	 * Depuración (TN.Rally.DebugCosmetics): manda al servidor el aspecto guardado con el color, el caparazón y los ojos
	 * cambiados (NAME_None = el guardado), como si estuvieran desbloqueados. No toca el save.
	 */
	void DebugSendCosmetics(FName SkinId, FName ShellId, FName EyesId);
#endif

	/** HUD del Rally de este jugador (solo en el jugador local; nullptr en el resto). Lee calor, munición y tinta del buggy. */
	UFUNCTION(BlueprintPure, Category = "Rally")
	UTN_RallyHUDWidget* GetRallyHUD() const { return RallyHUD; }

	UPROPERTY(EditDefaultsOnly, Category = "Rally")
	TSubclassOf<UTN_RallyHUDWidget> HUDWidgetClass;

	/** Cámara de llegada, podio y espectador de este jugador (solo en el jugador local; nullptr en el resto). */
	UTN_RallyCameraDirector* GetCameraDirector() const { return CameraDirector; }

	/**
	 * Servidor → ocupante (#332): un impacto que ha dado o recibido su buggy. Línea en el registro de la pantallita y, si
	 * lo ha dado, marca en la mira y sonido corto. Fiable: son pocos y la confirmación no se puede perder.
	 */
	UFUNCTION(Client, Reliable)
	void ClientRallyHitReport(const FTNRallyHitReport& Report);

	/**
	 * Servidor → ocupante (#330): lo que canta la artillera. Las dos lo oyen con la señal del copiloto (al lado de la curva,
	 * tantos pitidos como el grado) y la conductora ve la placa 1,5 s. Fiable: un canto perdido es una curva sin avisar.
	 */
	UFUNCTION(Client, Reliable)
	void ClientRallyCrewCall(const FTNRallyCrewCall& Call);

	/** Notas cantadas por la artillera que han llegado a esta máquina (para las pruebas sin editor). */
	int32 GetCrewCallsReceived() const { return CrewCallsReceived; }

	/** Sonido corto de acierto (2D, solo en el buggy que dispara). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Sonido")
	TObjectPtr<USoundBase> HitConfirmSound;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Sonido", meta = (ClampMin = "0"))
	float HitConfirmVolume = 0.7f;

	/**
	 * Desenfoque de movimiento en el Rally (#605): 0 = ninguno. Con la cámara de persecución, el buggy y sus paneles 3D
	 * dejaban estela (sobre todo a 30 fps). Lo aplica un volumen de postproceso local sin límites en cada jugador local.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Imagen", meta = (ClampMin = "0", ClampMax = "1"))
	float MotionBlurAmount = 0.f;

	/** Ajustes de postproceso del Rally: solo MotionBlurAmount (lo demás, el del nivel). */
	static FPostProcessSettings MakeRallyPostProcess(float InMotionBlurAmount);

	// ITN_VoiceListener
	virtual void SendVoiceToOwningClient(const TArray<uint8>& CompressedData, int32 SenderSampleRate, AActor* SpeakerActor,
		bool bIntercom) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** Espectador: anterior (A, ←, LB) y siguiente (D, →, RB) sin quitárselas al buggy (UTN_RallyCameraDirector). */
	virtual void SetupInputComponent() override;
	/** Jugador local: pone el salpicadero y el cartel del arco en el buggy que conduce (UTN_RallyDashboardComponent). */
	virtual void PlayerTick(float DeltaTime) override;
	/** Servidor: el peón nuevo (buggy de la conductora o peón de la artillera) lleva voz por proximidad. */
	virtual void OnPossess(APawn* InPawn) override;
	/** Jugador local: deja en el log qué peón posee (buggy de conductora o peón de artillera) y con qué roles. */
	virtual void AcknowledgePossession(APawn* InPawn) override;
	/**
	 * Servidor, al desconectarse: el motor destruiría el peón (el buggy de la conductora o el peón de la artillera) antes de
	 * Logout, y la carrera ya no sabría de qué equipo era. Sentada en un buggy, se deja tal cual: ATN_RallyGameMode::Logout,
	 * que llega justo después, la saca de su plaza y pasa la artillera al volante o retira el equipo.
	 */
	virtual void PawnLeavingGame() override;

private:
	/** Buggy en que va el jugador: el que conduce o el de su peón de artillera; nullptr si no va en ninguno. */
	ATN_Buggy* FindLocalBuggy() const;

	float DashboardCheckAccumulator = 0.f;

	/** Jugador local: manda al servidor el casco, el color, el caparazón y los ojos de su save (TNCosmeticsSync). */
	void SyncCosmeticsToServer();

	/** Validado contra DT_Helmets y DT_Skins del servidor (lo mismo que AMP_GamePlayerController). */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSyncCosmetics(const FTNCosmeticLoadout& Loadout);

	/** Voz de otra tortuga que ha filtrado el servidor (TNVoiceRouting); bIntercom = de su mismo buggy. */
	UFUNCTION(Client, Unreliable)
	void ClientReceiveVoice(const TArray<uint8>& CompressedData, int32 SenderSampleRate, AActor* SpeakerActor, bool bIntercom);

	/** Buggy en que va el jugador local (el que conduce o el de su peón de artillera). */
	ATN_Buggy* FindSeatedBuggy() const;

	int32 CrewCallsReceived = 0;

	UPROPERTY(Transient)
	TObjectPtr<UTN_RallyHUDWidget> RallyHUD;

	UPROPERTY(Transient)
	TObjectPtr<UTN_RallyCameraDirector> CameraDirector;

	/** Copiloto automático de la conductora sin artillera humana (#331): solo en esta máquina. */
	UPROPERTY(Transient)
	TObjectPtr<UTN_RallyCopilotComponent> Copilot;

	/** Volumen de postproceso local del Rally (MakeRallyPostProcess): solo en el jugador local. */
	UPROPERTY(Transient)
	TObjectPtr<APostProcessVolume> RallyPostProcess;

	/** Crea RallyPostProcess (sin límites y con prioridad alta) si no existe. */
	void ApplyRallyPostProcess();
};
