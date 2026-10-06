// Buggy biplaza del Rally Tortuga (Docs/Rally_MVP.md). Port de AHYBuggy (HellYeah) sin carga ni fichas, con turbo (#294).
#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "Rally/TN_RallyVehicle.h"
#include "Vehicles/TN_BuggyMath.h"
#include "VR/TN_VRVehicleMath.h"
#include "TN_Buggy.generated.h"

class APlayerState;
class ATN_BuggyGunnerPawn;
class UCameraComponent;
class UChaosWheeledVehicleMovementComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UAudioComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class USoundBase;
class USkeletalMesh;
class USkeletalMeshComponent;
class USpringArmComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTN_BuggyData;
class UTN_BuggyHealthComponent;
class UTN_BuggyEngineAudioComponent;
class UTN_BuggyDustComponent;
class UTN_BuggyInputSet;
class UTN_BuggyLookComponent;
class UTN_BuggyTurretComponent;
class UTN_VRSeatComponent;
struct FInputActionValue;

/**
 * Vehículo Chaos con el modelo de Art/Source/Vehicles/Buggy (#290): SK_TN_BuggyChassis (oculto: solo el cuerpo físico,
 * copia del PhysicsAsset del template, y los huesos de rueda), la carrocería SM_TN_BuggyBody con sus sockets de asientos
 * y de la boca de la torreta, y los neumáticos SM_TN_BuggyTire, que se mueven en C++ con el estado de cada rueda Chaos
 * (sin AnimBP). La conductora posee el buggy; la artillera posee un ATN_BuggyGunnerPawn sujeto al asiento trasero y
 * maneja la torreta (UTN_BuggyTurretComponent). Si va sola, la conductora dispara con apuntado automático.
 *
 * Red: el servidor tiene la autoridad (movimiento replicado de Chaos con PredictiveInterpolation). Los clientes solo
 * mandan entradas: conducción por el movimiento de Chaos, y enderezado, reaparición y disparo por RPC validada. Los
 * impactos (coco, charco, mortero, tinta, escudo) los decide el servidor y los replica como estado con hora de fin.
 *
 * Gafas (Docs/Modo_VR.md, «Vehículos»; TN_Buggy_VR.cpp): la conductora se sienta en DriverVRSeat (vista en los ojos de su
 * tortuga, sin la cámara de persecución), gira el volante con una o dos manos (agarres) y acelera y frena con los gatillos.
 * Las asas de la torreta (TurretHandles) solo se ven con una artillera con gafas. Las subclases (los karts) lo heredan.
 */
UCLASS()
class TORTUNABO_API ATN_Buggy : public AWheeledVehiclePawn, public ITN_RallyVehicle
{
	GENERATED_BODY()

public:
	/** Parámetro de M_TN_Buggy (zona de pintura) que lleva el color del equipo. */
	static const FName TintParameterName;

	/** Sockets de SM_TN_BuggyBody: caderas de las tortugas sentadas y boca de la torreta. */
	static const FName DriverSeatSocket;
	static const FName GunnerSeatSocket;
	static const FName MuzzleSocket;

	/**
	 * Los mismos sockets relativos al chasis (la carrocería va en su origen), copiados del manifest de Art/Source para
	 * el constructor y los peones que no tienen la malla a mano. El test Tortunabo.Rally.Buggy.Assets los compara con
	 * los sockets de la malla importada.
	 */
	static const FVector GunnerSeatLocal;
	static const FVector DriverSeatLocal;
	static const FVector MuzzleLocal;

	/** Huesos de rueda de SK_TN_BuggyChassis en el orden de WheelSetups (delanteras 0 y 1, traseras 2 y 3). */
	static const FName WheelBoneNames[4];

	ATN_Buggy();

	// ── ITN_RallyVehicle ────────────────────────────────────────────────────────
	virtual bool SeatController(AController* InController, ETNRallySeat Seat) override;
	virtual void UnseatController(AController* InController) override;
	virtual AController* GetSeatController(ETNRallySeat Seat) const override;
	virtual bool HasFreeSeat(ETNRallySeat Seat) const override;
	virtual void RallyTeleport(const FTransform& Where, float LockSeconds, float GhostSeconds) override;
	virtual void SetEngineLocked(bool bLocked) override;
	virtual float GetForwardSpeedCms() const override;
	virtual bool IsFlipped() const override;
	virtual float GetMaxSteerAngleDeg() const override;
	virtual int32 GetRallyTeamIndex() const override { return TeamIndex; }
	virtual void SetRallyTeamIndex(int32 Index) override;
	virtual void GiveSpecialAmmo(ETNRallyAmmo Ammo, int32 Charges) override;
	virtual ETNRallyAmmo GetSpecialAmmo() const override;
	virtual void SetAIDriveInput(float Throttle, float Brake, float Steer, bool bHandbrake) override;
	virtual void AIFire(const FVector& AimWorldDir, bool bSpecial) override;
	virtual bool ConsumeRespawnRequest() override;
	virtual bool ConsumeFellOutOfWorld() override;
	virtual bool ConsumeDestroyed() override;

	/** Solo servidor: el buggy ha reventado; la carrera lo recoge con ConsumeDestroyed y lo hace reaparecer. */
	void NotifyDestroyed();

	/** Fantasma tras reaparecer o girar (hora del servidor): no recibe impactos ni daño. Válido en el servidor. */
	bool IsRespawnProtected() const;
	virtual void SetWeaponsLocked(bool bLocked) override;
	/** Freno de carrera replicado: la conductora local y el servidor cortan el acelerador y frenan a fondo. */
	virtual void SetRaceBrakeHeld(bool bHeld) override;

	// ── Para el HUD, la carrera y la artillera ──────────────────────────────────

	/** Ajuste del buggy: Data o, sin asset, los valores por defecto de C++. Nunca nulo. */
	const UTN_BuggyData* GetData() const;

	UTN_BuggyTurretComponent* GetTurret() const { return Turret; }
	UTN_BuggyLookComponent* GetBuggyLook() const { return BuggyLook; }

	/** El PlayerState de una jugadora ha cambiado de buggy (OnRep): si conduce este, se repinta ya. */
	void NotifyDriverLookChanged(const APlayerState* ChangedPlayerState);
	UTN_BuggyHealthComponent* GetHealthComponent() const { return HealthComponent; }
	ATN_BuggyGunnerPawn* GetGunnerPawn() const { return GunnerPawn; }
	UStaticMeshComponent* GetBody() const { return Body; }
	/** Si el turbo se ve: con Niagara (BoostEffect) o con la llama de malla propia (#294). */
	bool HasBoostVisual() const;
	/** Malla de la llama: BoostFlameMesh si se asigna; si no, el cono emisivo construido en ejecución (TNBuggyFlameMesh). */
	UStaticMesh* GetBoostFlameMesh() const;
	/** Material de la llama: BoostFlameMaterial si se asigna; si no, el emisivo del cono (M_ProcGlow). */
	UMaterialInterface* GetBoostFlameMaterial() const;
	UChaosWheeledVehicleMovementComponent* GetWheeledMovement() const;

	/**
	 * Dirección, acelerador y freno que aplica Chaos en esta máquina: los de la conductora local o, en el servidor y en las
	 * demás máquinas, los que ella manda (ReplicatedState). GetSteeringInput, GetThrottleInput y GetBrakeInput de Chaos dan la
	 * entrada cruda, que solo existe en la máquina que conduce: en el servidor, la de una conductora cliente era siempre 0 y la
	 * balsa de Karts no remaba (el cliente se quedaba atascado en el agua, #710).
	 */
	float GetAppliedSteering() const;
	float GetAppliedThrottle() const;
	float GetAppliedBrake() const;

	/** Nombres de esas propiedades (protegidas) en UChaosVehicleMovementComponent; el test Tortunabo.Rally.Buggy.AppliedInputs los comprueba. */
	static const FName AppliedSteeringProperty;
	static const FName AppliedThrottleProperty;
	static const FName AppliedBrakeProperty;

	/** Segundos de tinta en pantalla que quedan (0 = limpia). Vale en cualquier máquina. */
	UFUNCTION(BlueprintPure, Category = "Rally|Buggy")
	float GetInkSecondsLeft() const;

	/** Si el escudo de la burbuja está activo. */
	UFUNCTION(BlueprintPure, Category = "Rally|Buggy")
	bool IsShielded() const;

	/** Si el buggy pisa un charco de alga (agarre y velocidad máxima reducidos). */
	UFUNCTION(BlueprintPure, Category = "Rally|Buggy")
	bool IsInPuddle() const { return bInPuddle; }

	/** Si va por el agua (#719): WadeMinWheels ruedas metidas. Lo calcula cada máquina con la cota del agua. */
	UFUNCTION(BlueprintPure, Category = "Rally|Buggy")
	bool IsWading() const { return bWading; }

	/** Fracción de una estadística con la vida que le queda (#720): 1 con la vida llena, MinScale a 0. */
	float GetDamageStatScale(float MinScale) const;

	/** Si el motor está cortado (semáforo, salida anticipada, reaparición o fin). */
	UFUNCTION(BlueprintPure, Category = "Rally|Buggy")
	bool IsEngineLocked() const;

	/** Si la carrera tiene la torreta bloqueada (calentamiento, semáforo, resultados o equipo retirado). */
	UFUNCTION(BlueprintPure, Category = "Rally|Buggy")
	bool AreWeaponsLocked() const { return bWeaponsLockedByRace; }

	/** Si la plaza de artillera está ocupada (estado replicado; vale en cualquier máquina). */
	UFUNCTION(BlueprintPure, Category = "Rally|Buggy")
	bool HasGunner() const { return bGunnerSeated; }

	/** PlayerState de la jugadora sentada en esa plaza (replicado; null si está vacía o la ocupa la IA). */
	const APlayerState* GetSeatPlayerState(ETNRallySeat Seat) const
	{
		return Seat == ETNRallySeat::Gunner ? GunnerPlayerState.Get() : DriverPlayerState.Get();
	}

	/** Si la carrera tiene el freno puesto (parrilla durante el semáforo): sin acelerador ni turbo. */
	UFUNCTION(BlueprintPure, Category = "Rally|Buggy")
	bool IsRaceBrakeHeld() const { return bRaceBrakeHeld; }

	/** Carga del turbo en [0, 1] (la decide el servidor; vale en cualquier máquina). */
	UFUNCTION(BlueprintPure, Category = "Rally|Buggy")
	float GetBoost01() const { return BoostCharge01; }

	/** Si el turbo empuja ahora: la conductora local lo predice con su botón; el resto lee el estado del servidor. */
	UFUNCTION(BlueprintPure, Category = "Rally|Buggy")
	bool IsBoosting() const;

	/**
	 * Fuerza del turbo en [0, 1] (#630): sube poco a poco mientras se pisa (UTN_BuggyData::BoostRampUpSeconds y su curva) y
	 * baja suave al soltarlo. La avanza cada máquina con IsBoosting; el empuje, el par, el FOV, la llama y el sonido la siguen.
	 */
	UFUNCTION(BlueprintPure, Category = "Rally|Buggy")
	float GetBoostStrength() const { return BoostStrength01; }

	/**
	 * Turbo regalado durante Seconds (#742, el mini-turbo del derrape de los karts): cuenta como IsBoosting sin pisar el botón
	 * ni gastar la barra, con la misma rampa, el mismo empuje, la llama y el sonido. Lo pide el servidor (los karts no lo
	 * predicen en el cliente); la hora de fin se replica a todas las máquinas. Un turbo ya regalado no se acorta.
	 */
	void GrantTimedBoost(float Seconds);

	/** Segundos que le quedan al turbo regalado (0 = ninguno). */
	float GetTimedBoostSecondsLeft() const;

	/** Freno de mano puesto (el de la conductora o el de la IA) y ninguna rueda en el suelo: para el derrape de los karts. */
	bool IsHandbrakeHeld() const { return bHandbrakeHeld; }
	bool IsAirborne() const { return bAirborne; }

	/** Sacudida (0..1) para la cámara de la conductora local; en otras máquinas no hace nada. Para impactos y disparos. */
	UFUNCTION(BlueprintCallable, Category = "Rally|Buggy")
	void AddCameraTrauma(float Amount);

	/** Hora del servidor (vale en cualquier máquina). */
	double GetServerNow() const;

	// ── Gafas (TN_Buggy_VR.cpp) ─────────────────────────────────────────────────

	/** Asiento VR de una plaza: el de la conductora es de este buggy; el de la artillera, de su peón (nullptr sin él). */
	UTN_VRSeatComponent* GetVRSeat(ETNRallySeat Seat) const;

	/** Volante de la conductora en los ejes del chasis (el de SM_TN_BuggyBody y las carrocerías tortuga). */
	static TNVRVehicle::FWheelFrame GetWheelFrame();

	/** Centro del puño izquierdo o derecho de las asas de la torreta, en mundo (giran con el carro). */
	FVector GetTurretHandleWorld(bool bRight) const;

	/** Conductora local con gafas: giro del volante (grados) y si alguna mano lo tiene cogido. */
	float GetVRWheelDeg() const { return static_cast<float>(VRWheel.WheelDeg); }
	bool IsVRWheelHeld() const { return VRWheel.Mask != 0; }

	/**
	 * Pruebas (TN.VR.SeatPose): la conductora local con la vista sentada coge el volante con las dos manos, lo gira WheelDeg
	 * y lo mantiene; a los Seconds escribe en el registro el giro del volante, la dirección y el de las ruedas.
	 */
	void DebugVRWheelPose(float WheelDeg, float Seconds);

	// ── Pruebas de la torreta (TN_Buggy_TurretFit.cpp; sin efecto en Shipping) ──

	/** Sienta a la artillera visual aunque la plaza esté vacía, para medir y fotografiar la torreta (servidor o standalone). */
	void DebugShowGunner();
	/** Solapes de la torreta con la artillera y con la carrocería con el apuntado actual, en una línea para el registro. */
	FString DebugMeasureTurretFit() const;
	/** Turbo pisado (o suelto) con la barra llena, para ver la llama sin mando (servidor o standalone; TN.Rally.DebugEffects). */
	void DebugHoldBoost(bool bHold);

	// ── Impactos (solo servidor) ────────────────────────────────────────────────

	/** Coco: impulso lateral y bamboleo de la dirección. HitDir es la dirección del proyectil. */
	void ApplyCocoHit(const FVector& HitDir);
	/** Mortero: impulso vertical sin vuelco forzado. */
	void ApplyMortarBlast();
	/** Tinta en la pantalla de las dos ocupantes. */
	void ApplyInk();
	/** Escudo de la burbuja. */
	void GrantShield();
	/** Si hay escudo, lo gasta y devuelve true (el impacto o el charco no llegan). */
	bool TryConsumeShield();
	/** El charco de alga avisa cada vez que comprueba que el buggy está dentro. */
	void NotePuddleContact();
	/** Impulso de velocidad (cm/s) al chasis en el servidor, con ForceNetUpdate. */
	void ApplyVelocityImpulse(const FVector& DeltaVelocity);

	/** Pide el enderezado (lo revalida el servidor) y lleva la cuenta del botón mantenido para reaparecer. */
	void HandleSelfRightInput(bool bPressed);

	/** Disparo de la conductora sola (servidor): apuntado automático hacia delante o hacia atrás. */
	void DriverFireAuto(bool bSpecial, bool bBackward);

	/** Conductora local sin artillera: pide al servidor un disparo con apuntado automático (entrada y TN.Rally.LocalFire). */
	void RequestDriverFire(bool bSpecial, bool bBackward);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSelfRight();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestRespawn();

	virtual void PostInitializeComponents() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** Bajo el KillZ no se destruye (AActor lo haría): se queda quieto y pide a la carrera volver a la pista. */
	virtual void FellOutOfWorld(const UDamageType& DmgType) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Cota del agua en Location: la de la pista del Rally (los karts añaden el mar y las pozas). False si ahí no hay agua. */
	virtual bool FindWaterSurfaceZ(const FVector& Location, double& OutZ) const;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Buggy")
	TObjectPtr<UTN_BuggyData> Data;

	// Rutas de los assets (Art/Source/Vehicles/Buggy importado en /Game). El constructor pone los de por defecto.
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Assets")
	TSoftObjectPtr<USkeletalMesh> ChassisMeshAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Assets")
	TSoftObjectPtr<UStaticMesh> BodyMeshAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Assets")
	TSoftObjectPtr<UStaticMesh> TireMeshAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Assets")
	TSoftObjectPtr<USkeletalMesh> TurtleMeshAsset;

	/** Skins de la carrocería y las ruedas (Mar, Alga, Medusa): cada equipo lleva la de su índice, en ciclo. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Assets")
	TArray<TSoftObjectPtr<UMaterialInterface>> SkinMaterials;

	/** La zona de pintura de la skin toma el color del equipo (el mismo del HUD y del mapa de la artillera). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Assets")
	bool bPaintWithTeamColor = true;

	/** Escala de la tortuga sentada: la de BP_TortugaCharacter, para la que están medidos los asientos de Art/Source. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Assets", meta = (ClampMin = "0.1"))
	float SeatedTurtleScale = 2.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Buggy")
	TSubclassOf<ATN_BuggyGunnerPawn> GunnerPawnClass;

	/** Llama del turbo en el escape: se crea en cada máquina con pantalla mientras el turbo empuja. Vacío = sin efecto. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Turbo")
	TObjectPtr<UNiagaraSystem> BoostEffect;

	/** Sonido del turbo (en bucle mientras empuja), en cada máquina con pantalla. Vacío = sin sonido. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Turbo")
	TObjectPtr<USoundBase> BoostSound;

	/** Golpe de arranque del turbo (una vez, al empezar a empujar). Vacío = sin sonido. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Turbo")
	TObjectPtr<USoundBase> BoostStartSound;

	/** Escape relativo a la carrocería (cm, entre los dos tubos de SM_TN_BuggyBody): de ahí salen la llama y el sonido. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Turbo")
	FVector BoostEffectOffset = FVector(-200.f, 0.f, 111.f);

	/**
	 * Llama de malla propia (#294) cuando no hay BoostEffect: un cono en cada tubo de escape que parpadea mientras el turbo
	 * empuja. Vacío = el cono emisivo de BoostFlameColor construido en ejecución (TNBuggyFlameMesh); asignada, esta malla
	 * con BoostFlameMaterial.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Turbo")
	TObjectPtr<UStaticMesh> BoostFlameMesh;

	/** Material de BoostFlameMesh, con el parámetro vectorial «Color»; sin BoostFlameMesh no se usa. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Turbo")
	TObjectPtr<UMaterialInterface> BoostFlameMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Turbo")
	FLinearColor BoostFlameColor = FLinearColor(1.f, 0.42f, 0.06f);

	/** Separación lateral (cm) de cada tubo respecto a BoostEffectOffset: hay una llama por tubo. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Turbo")
	float BoostFlameSideOffsetCm = 31.f;

	/** Dirección de la llama en la carrocería (la de los tubos, hacia atrás y arriba); la Y se refleja en cada lado. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Turbo")
	FVector BoostFlameDirection = FVector(-0.91f, 0.13f, 0.40f);

	/** Largo (cm) y diámetro de la base (cm) de cada llama. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Turbo", meta = (ClampMin = "1"))
	float BoostFlameLengthCm = 80.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Turbo", meta = (ClampMin = "1"))
	float BoostFlameDiameterCm = 16.f;

	/** Cuánto varía el largo al parpadear (fracción). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Turbo", meta = (ClampMin = "0", ClampMax = "0.9"))
	float BoostFlameFlickerAmount = 0.3f;

private:
	// ── Física ─────────────────────────────────────────────────────────────────
	/** Fricción, par, frenos de carrera, golpes, charco, antivuelco, estabilidad, turbo y dirección de cada fotograma. */
	void TickDrivePhysics();
	void ApplyWheelFriction();
	void ApplyEngineTorque();
	void ApplySteeringAssist();
	void ApplyBumpKicks();
	/** Tope de velocidad del charco, del agua (#719) y de la vida perdida (#720), en cada máquina que simula el chasis. */
	void ApplySpeedCaps();
	/** Recalcula bWading con la cota del agua y el borde de abajo de cada rueda, en cada máquina. */
	void UpdateWading();
	/** Antivuelco (TNBuggy::AntiRollAccel) en cada máquina que simula el chasis. */
	void ApplyAntiRoll();
	void HoldLockedInPlace();
	void UpdateSelfRight(float DeltaSeconds);
	void DoSelfRight();
	void UpdateCamera(float DeltaSeconds);
	void UpdateServerTimers();

	// ── Gafas (TN_Buggy_VR.cpp) ────────────────────────────────────────────────
	/** Conductora local con la vista sentada: el volante con las manos (antes de la física, que lee SteerRequest). */
	void UpdateVRDriving(float DeltaSeconds);
	/** Cabeza propia oculta en la vista sentada y asas de la torreta solo con una artillera con gafas (máquinas con pantalla). */
	void UpdateVRVisuals();
	/** Vuelve a dejar el volante VR recto y sin manos. */
	void ResetVRDriving();

	// ── Estabilidad y turbo (TN_Buggy_Drive.cpp) ───────────────────────────────
	/** Recalcula bAirborne (ninguna rueda en contacto) en cada máquina. */
	void UpdateAirborne();
	/** Control de estabilidad sin freno de mano (TNBuggy::StabilityYawAccel), en cada máquina que simula el chasis. */
	void ApplyStability();
	/** Gasto y recarga de la barra del turbo (solo servidor). */
	void UpdateBoost(float DeltaSeconds);
	/** Rampa del turbo (#630) en cada máquina: avanza con IsBoosting y da GetBoostStrength. */
	void UpdateBoostRamp(float DeltaSeconds);
	/** Empuje del turbo hacia la punta aumentada, por su fuerza, en cada máquina que simula el chasis. */
	void ApplyBoostPush();
	/** Llama y sonido del turbo mientras empuja o se apaga (solo en máquinas con pantalla). */
	void RefreshBoostEffects();
	/** Enseña u oculta las llamas de malla propia (las crea la primera vez). */
	void ShowBoostFlames(bool bShow);
	/** Coloca y hace parpadear las llamas visibles y ajusta el sonido a la fuerza del turbo; cada fotograma con el turbo. */
	void UpdateBoostFlames();
	void SetBoostHeld(bool bHeld);
	/** Freno de carrera en el servidor y en la conductora local: acelerador a 0 y freno a fondo cada fotograma. */
	void ApplyRaceBrake();
	/** Al soltar el freno de carrera, quita el freno que puso (servidor y conductora local). */
	void ReleaseRaceBrake();
	/**
	 * Con el freno de carrera y apoyado, deja el buggy en su sitio aunque esté en cuesta (#611): sin velocidad en el plano del
	 * suelo salvo la que lo devuelve a su ancla (TNBuggy::GridHoldVelocity) y sin guiñada. Servidor y conductora local.
	 */
	void HoldOnGrid();

	UFUNCTION()
	void OnRep_RaceBrake();

	// ── Asientos y tortugas visuales ────────────────────────────────────────────
	void SetDriverSeat(AController* NewDriver);
	void SetGunnerSeat(AController* NewGunner);
	ATN_BuggyGunnerPawn* SpawnGunnerPawn();
	void DestroyGunnerPawn();
	void RefreshSeatVisuals(bool bForce);
	void ApplySeatLook(int32 SeatIndex, bool bForce);
	/** Escala la tortuga y la coloca con la cadera (hueso Hips) en el socket de su asiento. */
	void FitTurtle(int32 SeatIndex);
	/** Tortuga de la artillera caída hacia atrás mientras está noqueada (cosmético, en cada máquina con pantalla). */
	void UpdateGunnerKnockPose(float DeltaSeconds);

	UFUNCTION()
	void OnRep_Seats();

	UFUNCTION()
	void OnRep_TeamIndex();
	/** Skin del equipo en la carrocería y las ruedas, con la pintura del color del equipo (TN_Buggy_Visuals.cpp). */
	void ApplyTint();

	/**
	 * Aspecto comprado en la tienda (TN_Buggy_Look.cpp): el FTN_BuggyLook del PlayerState de la conductora. Sin trabajo
	 * si no ha cambiado (salvo bForce).
	 */
	void RefreshBuggyLook(bool bForce);
	/** El de serie con la pintura de serie: la carrocería y los neumáticos llevan la skin del equipo de ApplyTint. */
	bool UsesTeamSkin() const;
	/** Escape del modelo puesto (cm, espacio de la carrocería): de ahí salen la llama y el sonido del turbo. */
	FVector GetExhaustLocal() const;

	// ── Modelo (TN_Buggy_Visuals.cpp) ──────────────────────────────────────────
	/** Mallas, sockets y posiciones de reposo de los neumáticos según los assets de Rally|Assets. */
	void ApplyModelAssets();
	/** Neumáticos en el eje de su rueda Chaos: suspensión, giro de la dirección y rodadura (solo con pantalla). */
	void UpdateWheelVisuals();
	/** Mallas de la torreta (TN_BuggyTurretMesh) construidas en ejecución; nada en el servidor dedicado (#435). */
	void BuildTurretVisuals();
	/** Socket de la carrocería relativo al chasis, o Fallback si la malla no lo tiene. */
	FVector GetBodySocketLocal(FName Socket, const FVector& Fallback) const;

	UFUNCTION()
	void OnRep_Ghost();
	void ApplyGhost();

	// ── Input de la conductora ─────────────────────────────────────────────────
	UTN_BuggyInputSet* GetInputSet();
	void OnThrottle(const FInputActionValue& Value);
	void OnBrake(const FInputActionValue& Value);
	void OnSteer(const FInputActionValue& Value);
	void OnHandbrakePressed(const FInputActionValue& Value);
	void OnHandbrakeReleased(const FInputActionValue& Value);
	void OnSelfRightPressed(const FInputActionValue& Value);
	void OnSelfRightReleased(const FInputActionValue& Value);
	void OnFireCoco(const FInputActionValue& Value);
	void OnFireCocoReleased(const FInputActionValue& Value);
	void OnFireSpecial(const FInputActionValue& Value);
	void OnFireBackPressed(const FInputActionValue& Value);
	void OnFireBackReleased(const FInputActionValue& Value);
	void OnBoostPressed(const FInputActionValue& Value);
	void OnBoostReleased(const FInputActionValue& Value);
	void OnCycleAmmo(const FInputActionValue& Value);
	void SetHandbrakeHeld(bool bHeld);

	UFUNCTION(Server, Reliable)
	void ServerSetHandbrake(bool bHeld);

	UFUNCTION(Server, Reliable)
	void ServerSetBoostHeld(bool bHeld);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerDriverFire(bool bSpecial, bool bBackward);

	// ── Componentes ────────────────────────────────────────────────────────────
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TArray<TObjectPtr<UStaticMeshComponent>> Tires;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UTN_BuggyTurretComponent> Turret;

	/** Carrocerías tortuga de la tienda y pintura del buggy (solo visual). */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UTN_BuggyLookComponent> BuggyLook;

	/** Vida del buggy: sus efectos (humo, explosión, choque) se asignan en el Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTN_BuggyHealthComponent> HealthComponent;

	/** Motor en tres capas por RPM y derrape en bucle: local y cosmético (los sonidos, en el componente). */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UTN_BuggyEngineAudioComponent> EngineAudio;

	/** Polvo, salpicaduras, marcas de las ruedas y golpe de aterrizaje (#301), locales en cada máquina con pantalla. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UTN_BuggyDustComponent> DustFX;

	/** Caña y boca del cañón: gira y cabecea con el apuntado y toma el color de la munición (UTN_BuggyTurretComponent::TintTag). */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> TurretBarrel;

	/** Cuerpo, escudo y brazo del cañón: giran y cabecean con el apuntado. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> TurretGun;

	/** Carro y horquilla de la torreta: solo giran en guiñada (UTN_BuggyTurretComponent::SetYawFollower). */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> TurretMount;

	/** Aro fijo sobre las barandillas en el que gira el carro. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> TurretRing;

	/** Asas de la artillera con gafas (TNBuggyTurretMesh::BuildHandles): cuelgan del carro; ocultas sin una artillera con gafas. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> TurretHandles;

	/** Asiento VR de la conductora, en los ojos de su tortuga (UTN_VRSeatComponent). */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UTN_VRSeatComponent> DriverVRSeat;

	/** Tortugas visuales: 0 = conductora, 1 = artillera. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TArray<TObjectPtr<USkeletalMeshComponent>> SeatTurtles;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TArray<TObjectPtr<UStaticMeshComponent>> SeatHelmets;

	UPROPERTY(Transient)
	TObjectPtr<UTN_BuggyInputSet> InputSet;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TintMaterial;

	/** Materiales originales de cada tortuga (UTN_CosmeticLook::ApplyLook parte de ellos). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> DriverTurtleDefaults;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> GunnerTurtleDefaults;

	// ── Estado replicado ───────────────────────────────────────────────────────
	UPROPERTY(ReplicatedUsing = OnRep_TeamIndex)
	int32 TeamIndex = INDEX_NONE;

	/** Freno de mano de la conductora, para que todas las máquinas simulen la misma fricción. */
	UPROPERTY(Replicated)
	bool bHandbrakeHeld = false;

	UPROPERTY(ReplicatedUsing = OnRep_Seats)
	bool bDriverSeated = false;

	UPROPERTY(ReplicatedUsing = OnRep_Seats)
	bool bGunnerSeated = false;

	/** PlayerState de cada ocupante (null para la IA): su aspecto viste a la tortuga del asiento. */
	UPROPERTY(ReplicatedUsing = OnRep_Seats)
	TObjectPtr<APlayerState> DriverPlayerState;

	UPROPERTY(ReplicatedUsing = OnRep_Seats)
	TObjectPtr<APlayerState> GunnerPlayerState;

	UPROPERTY(Replicated)
	TObjectPtr<ATN_BuggyGunnerPawn> GunnerPawn;

	UPROPERTY(Replicated)
	bool bEngineLockedByRace = false;

	/** Torreta bloqueada por la carrera; fuera del Rally (sin carrera) dispara siempre. */
	UPROPERTY(Replicated)
	bool bWeaponsLockedByRace = false;

	/** Freno de carrera (parrilla durante el semáforo); lo pone y lo quita el servidor. */
	UPROPERTY(ReplicatedUsing = OnRep_RaceBrake)
	bool bRaceBrakeHeld = false;

	/** Horas del servidor en que acaban los efectos (0 = sin efecto). */
	UPROPERTY(Replicated)
	float LockEndServerTime = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_Ghost)
	bool bGhost = false;

	UPROPERTY(Replicated)
	float ShieldEndServerTime = 0.f;

	UPROPERTY(Replicated)
	float InkEndServerTime = 0.f;

	UPROPERTY(Replicated)
	float WobbleEndServerTime = 0.f;

	UPROPERTY(Replicated)
	bool bInPuddle = false;

	/** Carga del turbo [0, 1]; la gasta y la recarga el servidor. */
	UPROPERTY(Replicated)
	float BoostCharge01 = 0.f;

	/** Turbo empujando según el servidor. La conductora no lo recibe: lo predice con su botón (IsBoosting). */
	UPROPERTY(Replicated)
	bool bBoostActive = false;

	/** Hora del servidor en que acaba el turbo regalado (GrantTimedBoost); 0 = ninguno. */
	UPROPERTY(Replicated)
	float TimedBoostEndServerTime = 0.f;

	// ── Estado local o de servidor ─────────────────────────────────────────────
	UPROPERTY(Transient)
	TObjectPtr<AController> DriverController;

	UPROPERTY(Transient)
	TObjectPtr<AController> GunnerController;

	float GhostEndServerTime = 0.f;
	float PuddleUntilServerTime = 0.f;
	bool bRespawnRequested = false;
	bool bFellOutOfWorld = false;
	bool bDestroyedPending = false;
	bool bSelfRightRequested = false;

	bool bHandbrakeFrictionApplied = false;
	bool bWheelFrictionApplied = false;
	float AppliedGripMultiplier = 1.f;
	bool bEngineTorqueLockedApplied = false;
	/** Fracción del par por la vida perdida con la que se puso el par del motor por última vez (#720). */
	float AppliedDamageTorqueScale = 1.f;
	/** Ruedas metidas en el agua (#719). */
	bool bWading = false;
	/** Fuerza del turbo con la que se puso el par del motor por última vez. */
	float AppliedBoostStrength = 0.f;
	/** Avance lineal de la rampa del turbo [0, 1] y fuerza que da (UTN_BuggyData::EvaluateBoostRamp). */
	float BoostRampProgress01 = 0.f;
	float BoostStrength01 = 0.f;
	/** Botón del turbo: el de la conductora local y, en el servidor, el último que pidió por RPC. */
	bool bBoostHeld = false;
	bool bAirborne = false;
	bool bBoostEffectsOn = false;
	/** Sitio en que se ancla el buggy con el freno de carrera (HoldOnGrid): lo toma al apoyarse y lo suelta con el freno. */
	FVector GridAnchor = FVector::ZeroVector;
	bool bHasGridAnchor = false;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> BoostEffectComponent;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> BoostFlames;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BoostSoundComponent;

	/** Cámara dinámica de la conductora (#298) y sacudida pedida desde fuera para el siguiente fotograma. */
	TNBuggy::FDriverCameraState CameraState;
	float PendingCameraTrauma = 0.f;
	bool bAimBackward = false;
	double LastDriverFireRequest = -1000.0;
	/** Con una especial seleccionada, la conductora sola dispara una vez por pulsación (como la artillera). */
	bool bDriverFireLatched = false;
	/** Caída de la tortuga de la artillera noqueada: 0 sentada, 1 tumbada hacia atrás. */
	float GunnerKnockLean01 = 0.f;

	float SteerRequest = 0.f;
	/** Lo que pide el stick o el teclado; con el volante VR en las manos manda el volante. */
	float StickSteer = 0.f;
	/** Volante VR de la conductora local y qué manos lo tienen cogido. */
	TNVRVehicle::FWheelState VRWheel;
	bool bVRWheelHand[2] = { false, false };
	/** El volante VR manda en la dirección (cogido o volviendo solo al centro). */
	bool bVRSteering = false;
	bool bTurretHandlesShown = false;
	float FlippedSeconds = 0.f;
	TNBuggy::FHold RespawnHold;
	bool bSelfRightHeld = false;
	float SeatLookCheckAccumulator = 0.f;

	/** Aspecto aplicado a cada tortuga (para no reaplicarlo cada vez). */
	TArray<FString> AppliedLookKeys;

	/** Sitio de cada tortuga relativo al chasis (cadera en su socket), lo calcula FitTurtle. */
	FVector SeatTurtleBase[2] = { FVector::ZeroVector, FVector::ZeroVector };

	/** Eje de cada neumático en reposo relativo al chasis (hueso de su rueda). */
	TArray<FVector> TireRestLocal;
	/** Skin aplicada (índice en SkinMaterials) para no recrear el material dinámico. */
	int32 AppliedSkinIndex = INDEX_NONE;

	TArray<FVector> PrevContactPoint;
	TArray<bool> bPrevWheelContact;
	TArray<TNBuggy::FWheelTrack> BumpTracks;
};
