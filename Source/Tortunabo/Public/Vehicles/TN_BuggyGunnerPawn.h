// Peón de la artillera del buggy biplaza: sin movimiento, sujeto al asiento trasero (Seat_Gunner), con cámara propia
// (por encima del hombro o en primera persona desde el cañón, alternable con V o el clic del stick derecho) y apuntado,
// disparo y cambio de munición por RPC validada (la artillera no es dueña del buggy: sus RPC salen de este peón).
// Con gafas (Docs/Modo_VR.md, «Vehículos»; TN_BuggyGunnerPawn_VR.cpp): vista en los ojos de su tortuga (VRSeat) y brazos
// que siguen a sus manos.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Rally/TN_RallyCrewCalls.h"
#include "Vehicles/TN_BuggyMath.h"
#include "TN_BuggyGunnerPawn.generated.h"

class ATN_Buggy;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UTN_BuggyInputSet;
class UTN_VRSeatComponent;
struct FInputActionValue;

UCLASS()
class TORTUNABO_API ATN_BuggyGunnerPawn : public APawn
{
	GENERATED_BODY()

public:
	ATN_BuggyGunnerPawn();

	/** Solo servidor: el buggy al que va sujeta (lo replica y cada máquina lo engancha al asiento). */
	void SetBuggy(ATN_Buggy* InBuggy);

	UFUNCTION(BlueprintPure, Category = "Rally|Buggy")
	ATN_Buggy* GetBuggy() const { return Buggy; }

	/** Apuntado relativo al buggy que lleva la artillera en esta máquina (cabeceo ya limitado). */
	FRotator GetLocalAim() const { return LocalAim; }

	/**
	 * Artillera local: pide al servidor un disparo con el apuntado de esta máquina (lo usan la entrada y TN.Rally.LocalFire).
	 * bSpecial = false dispara la munición seleccionada; true, la especial.
	 */
	void RequestFire(bool bSpecial);

	/** Artillera local: pide al servidor cambiar la munición seleccionada (UTN_BuggyTurretComponent::CycleAmmo lo usa). */
	void RequestCycleAmmo(int32 Direction);

	/**
	 * Artillera local (#330): pide al servidor cantar a la conductora la próxima nota de copiloto (la primera de los
	 * próximos 600 m). False si no hay ninguna o aún no ha pasado la espera mínima.
	 */
	bool RequestCallNote();

	/** Artillera local (#330): pide al servidor un aviso rápido a la conductora («¡Turbo ya!», «¡Frena!»). */
	bool RequestQuickCall(ETNRallyQuickCall Quick);

	/** Artillera local: empujón de la cámara que se recupera solo (disparo, noqueo, choque). */
	void AddCameraKick(float PitchDeg, float RollDeg, float BackCm);

	/** Alterna la cámara entre por encima del hombro y primera persona desde el cañón. */
	UFUNCTION(BlueprintCallable, Category = "Rally|Torreta")
	void ToggleFirstPerson();

	UFUNCTION(BlueprintPure, Category = "Rally|Torreta")
	bool IsFirstPerson() const { return bFirstPerson; }

	/** Asiento VR de la artillera, en los ojos de su tortuga (UTN_VRSeatComponent). */
	UTN_VRSeatComponent* GetVRSeat() const { return VRSeat; }


	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Sensibilidad del ratón (grados por unidad de delta) y velocidad del stick (grados por segundo). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Torreta")
	float MouseDegreesPerUnit = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Torreta")
	float StickDegreesPerSecond = 160.f;

	/** Envíos del apuntado al servidor por segundo. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Torreta")
	float AimSendRate = 15.f;

	/** Cámara por encima del hombro: largo del brazo (cm) y desplazamiento (a la derecha y arriba). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Cámara")
	float ShoulderArmLengthCm = 150.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Cámara")
	FVector ShoulderSocketOffset = FVector(0.f, 55.f, 35.f);

	/** Retardo suave de la cámara por encima del hombro (posición y giro). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Cámara")
	float CameraLagSpeed = 12.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Cámara")
	float CameraRotationLagSpeed = 18.f;

	/** Velocidad con la que se recupera el empujón de la cámara. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Cámara")
	float CameraKickRecoverSpeed = 9.f;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void OnRep_Buggy();
	void AttachToBuggy();
	void UpdateCamera(float DeltaSeconds);
	/** Artillera local con la vista sentada: manos que ven los demás y cámara del asiento (en vez de UpdateCamera). */
	void UpdateVRGunner(float DeltaSeconds);
	void ApplyCameraMode();
	void WatchKnock();

	UTN_BuggyInputSet* GetInputSet();
	void EnsureCameraInput();
	void OnAimMouse(const FInputActionValue& Value);
	void OnAimStick(const FInputActionValue& Value);
	void OnFireCoco(const FInputActionValue& Value);
	void OnFireCocoReleased(const FInputActionValue& Value);
	void OnFireSpecial(const FInputActionValue& Value);
	void OnCycleAmmo(const FInputActionValue& Value);
	void OnToggleCamera(const FInputActionValue& Value);
	void OnSelfRightPressed(const FInputActionValue& Value);
	void OnSelfRightReleased(const FInputActionValue& Value);
	void AddAim(float DeltaYaw, float DeltaPitch);

	UFUNCTION(Server, Unreliable, WithValidation)
	void ServerSetAim(float Yaw, float Pitch);

	/**
	 * Disparo con el apuntado relativo (Yaw, Pitch) y la dirección en mundo que vio el cliente (#333): el servidor usa la
	 * del cliente si se separa como mucho TNRallyTurret::MaxClientAimErrorDeg de la que calcula con su orientación.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerFire(bool bSpecial, float Yaw, float Pitch, FVector_NetQuantizeNormal WorldDir);

	/** Cliente: trazador local inmediato hacia WorldDir si la torreta, tal como se ve aquí, puede disparar (#333). */
	void SpawnLocalTracer(bool bSpecial, const FRotator& Aim, const FVector& WorldDir) const;

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerCycleAmmo(int32 Direction);

	void OnCallNote(const FInputActionValue& Value);
	void OnQuickCall(const FInputActionValue& Value);

	/** Canto de la nota que empieza en NoteArcCm (eje de las notas): el servidor la busca y la valida (#330). */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerCallNote(float NoteArcCm);

	UFUNCTION(Server, Reliable)
	void ServerQuickCall(ETNRallyQuickCall Quick);

	/** Solo servidor: lleva Call a las ocupantes si la espera mínima lo permite. */
	void BroadcastCall(const FTNRallyCrewCall& Call);

	UFUNCTION(Server, Reliable)
	void ServerSelfRight();

	UFUNCTION(Server, Reliable)
	void ServerRequestRespawn();

	/** Solo servidor: la que habla es de verdad la artillera de este buggy. */
	bool IsSeatedGunner() const;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UCameraComponent> Camera;

	/** Asiento VR (con gafas, el origen del seguimiento; su cámara sustituye a la del hombro). */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UTN_VRSeatComponent> VRSeat;

	UPROPERTY(ReplicatedUsing = OnRep_Buggy)
	TObjectPtr<ATN_Buggy> Buggy;

	UPROPERTY(Transient)
	TObjectPtr<UTN_BuggyInputSet> InputSet;

	/** Acción y contexto propios de la cámara de la artillera (V y clic del stick derecho). */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CameraToggleAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> CameraContext;

	FRotator LocalAim = FRotator::ZeroRotator;
	FRotator LastSentAim = FRotator(1000.f, 0.f, 0.f);
	float AimSendAccumulator = 0.f;
	double LastFireRequest = -1000.0;
	/** Notas de la pista: en la artillera local, para elegir la próxima; en el servidor, para validarla (#330). */
	TNRallyCrewCalls::FNoteFollower NoteFollower;
	/** Último canto pedido (artillera local) y último aceptado (servidor). */
	double LastCallRequest = -1000.0;
	double LastServerCall = -1000.0;
	bool bSelfRightHeld = false;
	TNBuggy::FHold RespawnHold;

	/** Con una especial seleccionada, un disparo por pulsación (no se gastan las cargas manteniendo el botón). */
	bool bMainFireLatched = false;
	bool bFirstPerson = false;
	bool bWasKnocked = false;

	float KickPitchDeg = 0.f;
	float KickRollDeg = 0.f;
	float KickBackCm = 0.f;
};
