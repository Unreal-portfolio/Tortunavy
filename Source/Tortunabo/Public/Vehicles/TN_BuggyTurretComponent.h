// Torreta del buggy: apuntado replicado, calentamiento del coco, munición especial, munición seleccionada, noqueo de la
// artillera y disparo con retroceso (horizontal y vertical). Todo lo decide el servidor; los clientes solo piden por RPC
// validada (ATN_Buggy para la conductora sola, ATN_BuggyGunnerPawn para la artillera, o la de esta torreta si la pide la
// dueña del buggy). Los efectos de cada disparo y las ráfagas de impacto llegan por multicast no fiable. Lógica pura en
// TNRallyTurret (TN_RallyTurretLogic.h).
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Rally/TN_RallyVehicle.h"
#include "Vehicles/TN_RallyProjectile.h"
#include "Vehicles/TN_RallyTurretLogic.h"
#include "TN_BuggyTurretComponent.generated.h"

class ATN_Buggy;
class UNiagaraSystem;
class USoundBase;

UCLASS(ClassGroup = (Rally), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_BuggyTurretComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	/**
	 * Altura del socket Muzzle_Gunner sobre el asiento de la artillera (cm): Muzzle_Gunner (169,38) menos Seat_Gunner
	 * (127,38) en SM_TN_BuggyBody, a la altura de su cabeza. La cámara de la artillera sigue aquí. Lo comprueba
	 * Tortunabo.Rally.Buggy.Assets.
	 */
	static constexpr float MuzzleSocketAboveSeatCm = 42.f;
	/**
	 * Lo que sube el pivote de la torreta sobre Muzzle_Gunner (cm, #435): el cañón pasa por encima de la cabeza de la
	 * artillera y de su mano derecha levantada y, apuntando atrás con cualquier cabeceo, por encima del travesaño del arco
	 * trasero (medido con
	 * TN.Rally.DebugTurretFit; Tortunabo.Rally.Turret.MeshClearance).
	 */
	static constexpr float PivotRaiseCm = 45.f;
	/** Altura del pivote de la torreta sobre el asiento de la artillera (cm). */
	static constexpr float PivotAboveSeatCm = MuzzleSocketAboveSeatCm + PivotRaiseCm;
	/**
	 * Distancia de la boca al pivote (cm): apuntando al frente, la boca cae PivotRaiseCm por encima de Muzzle_Gunner
	 * (64,58 cm por delante del asiento, 10 cm por delante del hocico de la artillera). El proyectil sale de ahí.
	 */
	static constexpr float MuzzleDistanceCm = 64.58f;
	/**
	 * Desplazamiento de la boca a la derecha del eje de apuntado (cm): el cañón va a la derecha, sobre la mano derecha
	 * levantada de la artillera (#435). El proyectil sale de la boca visible (TNRallyTurret::MuzzleWorldLocation).
	 */
	static constexpr float MuzzleSideCm = 25.f;
	/** Etiqueta de las mallas que toman el color de la munición seleccionada (la caña del cañón). */
	static const FName TintTag;

	UTN_BuggyTurretComponent();

	// ── HUD (cualquier máquina) ─────────────────────────────────────────────────

	/** Calor del coco en [0, 1]. */
	UFUNCTION(BlueprintPure, Category = "Rally|Torreta")
	float GetHeat01() const { return Heat01; }

	UFUNCTION(BlueprintPure, Category = "Rally|Torreta")
	bool IsOverheated() const { return bOverheated; }

	UFUNCTION(BlueprintPure, Category = "Rally|Torreta")
	ETNRallyAmmo GetSpecialAmmo() const { return SpecialAmmo; }

	UFUNCTION(BlueprintPure, Category = "Rally|Torreta")
	int32 GetSpecialCharges() const { return SpecialCharges; }

	/** Munición que dispara el botón principal: el coco o la especial cargada. */
	UFUNCTION(BlueprintPure, Category = "Rally|Torreta")
	ETNRallyAmmo GetSelectedAmmo() const { return SelectedAmmo; }

	/** Si la artillera está noqueada (la torreta no dispara). */
	UFUNCTION(BlueprintPure, Category = "Rally|Torreta")
	bool IsGunnerKnocked() const;

	/** Segundos de noqueo que quedan (0 = despierta). */
	UFUNCTION(BlueprintPure, Category = "Rally|Torreta")
	float GetGunnerKnockSecondsLeft() const;

	/** Apuntado relativo al buggy que se ve en esta máquina (el local para la artillera, el replicado para el resto). */
	FRotator GetDisplayAim() const;

	/** Dirección en mundo del apuntado replicado. */
	FVector GetAimWorldDirection() const;

	/** Componente que sigue solo la guiñada de la torreta (la base del cañón); va enganchado al chasis en el pivote. */
	void SetYawFollower(USceneComponent* InFollower) { YawFollower = InFollower; }

	/** Vuelve a teñir las mallas con TintTag (p. ej. tras darles malla en ejecución). */
	void RefreshSelectedLook() { ApplySelectedLook(); }

	// ── Cualquier máquina ──────────────────────────────────────────────────────

	/**
	 * Cambia la munición seleccionada a la siguiente (Direction > 0) o a la anterior (Direction < 0) entre las que tiene:
	 * el coco siempre y la especial si le quedan cargas. En un cliente lo pide al servidor por la artillera local
	 * (ATN_BuggyGunnerPawn) o, si es la dueña del buggy, por la RPC de esta torreta.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rally|Torreta")
	void CycleAmmo(int32 Direction);

	// ── Servidor ───────────────────────────────────────────────────────────────

	/** Apuntado relativo (se limita a -10..+45 de cabeceo). */
	void SetAimRelative(const FRotator& RelativeAim);

	/** Dispara la munición básica (coco) o la especial hacia WorldDir. False si la cadencia, el calor, las cargas o el noqueo no lo permiten. */
	bool TryFire(bool bSpecial, const FVector& WorldDir);

	/** Dispara la munición seleccionada (botón principal) hacia WorldDir. */
	bool TryFireSelected(const FVector& WorldDir);

	/** Cambia la selección (validado ya el que lo pide). */
	void ApplyCycle(int32 Direction);

	/** Carga de una caja: sustituye a la que hubiera. */
	void GiveSpecial(ETNRallyAmmo Ammo, int32 Charges);

	/** Noquea a la artillera Seconds: la torreta no dispara hasta entonces. */
	void KnockGunner(float Seconds);

	/** Ráfaga cosmética en todas las máquinas (ATN_RallyBurstFX::Broadcast). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBurst(ETNRallyBurstKind Kind, FVector_NetQuantize Where, float RadiusCm);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Torreta")
	TSubclassOf<ATN_RallyProjectile> ProjectileClass;

	/** Sonido y fogonazo de cada disparo (sin asset, solo la ráfaga básica del fogonazo). FireSound, si la munición no tiene el suyo. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Efectos")
	TObjectPtr<USoundBase> FireSound;

	/** Disparo de cada munición; la que falte usa FireSound. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Efectos")
	TMap<ETNRallyAmmo, TObjectPtr<USoundBase>> FireSoundByAmmo;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Efectos")
	TObjectPtr<UNiagaraSystem> MuzzleFlashFX;

protected:
	virtual void BeginPlay() override;

private:
	ATN_Buggy* GetBuggy() const;
	void SyncReplicatedState();
	/** Comprueba bloqueo, noqueo, cadencia, calor y cargas de Ammo. */
	bool CanFireAmmo(ETNRallyAmmo Ammo, double Now) const;
	ATN_RallyProjectile* SpawnProjectile(ETNRallyAmmo Ammo, const FVector& Dir, const FVector& Muzzle);
	void CommitShot(ETNRallyAmmo Ammo, double Now);
	void ApplyRecoil(ETNRallyAmmo Ammo, const FVector& Dir);
	/** Color del cañón según la munición seleccionada (en cada máquina). */
	void ApplySelectedLook();

	UFUNCTION()
	void OnRep_SelectedAmmo();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerCycleAmmo(int32 Direction);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFired(ETNRallyAmmo Ammo, FVector_NetQuantize Muzzle, FVector_NetQuantizeNormal Dir);

	UPROPERTY(Replicated)
	float AimYaw = 0.f;

	UPROPERTY(Replicated)
	float AimPitch = 0.f;

	UPROPERTY(Replicated)
	float Heat01 = 0.f;

	UPROPERTY(Replicated)
	bool bOverheated = false;

	UPROPERTY(Replicated)
	ETNRallyAmmo SpecialAmmo = ETNRallyAmmo::None;

	UPROPERTY(Replicated)
	int32 SpecialCharges = 0;

	UPROPERTY(ReplicatedUsing = OnRep_SelectedAmmo)
	ETNRallyAmmo SelectedAmmo = ETNRallyAmmo::Coco;

	/** Hora del servidor en que la artillera se despierta (0 = no está noqueada). */
	UPROPERTY(Replicated)
	float GunnerKnockEndServerTime = 0.f;

	TWeakObjectPtr<USceneComponent> YawFollower;

	TNRallyTurret::FHeat HeatState;
	TNRallyTurret::FSpecial Special;
	double LastCocoShot = -1000.0;
	double LastSpecialShot = -1000.0;
};
