// Vida del buggy del Rally y lo que la quita: impactos de munición (con el empujón en el punto de impacto) y choques
// fuertes; humo a media vida; con 0, revienta en una nube de arena y pide la reaparición a la carrera. También noquea a
// la artillera con un impacto directo y lanza a las tortugas a pie que atropella. Todo lo decide el servidor; la vida se
// replica y los efectos son locales (multicast no fiable). Lógica pura en TNRallyCombat (TN_RallyCombatLogic.h).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Rally/TN_RallyVehicle.h"
#include "TN_BuggyHealthComponent.generated.h"

class ATN_Buggy;
class ATortugaCharacter;
class UNiagaraComponent;
class UNiagaraSystem;
class UPrimitiveComponent;
class USoundBase;
class USphereComponent;

UCLASS(ClassGroup = (Rally), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_BuggyHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Etiqueta de la esfera de la artillera: un proyectil que choca con ella la noquea. */
	static const FName GunnerHitboxTag;

	UTN_BuggyHealthComponent();

	// ── HUD (cualquier máquina) ─────────────────────────────────────────────────

	UFUNCTION(BlueprintPure, Category = "Rally|Vida")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "Rally|Vida")
	float GetMaxHealth() const { return MaxHealth; }

	/** Vida en [0, 1]. */
	UFUNCTION(BlueprintPure, Category = "Rally|Vida")
	float GetHealth01() const;

	/** Si echa humo (media vida o menos). */
	UFUNCTION(BlueprintPure, Category = "Rally|Vida")
	bool IsSmoking() const;

	// ── Servidor ───────────────────────────────────────────────────────────────

	/**
	 * Impacto de munición en WorldPoint con dirección ShotDir: escudo, empujón en el punto, efecto de la munición (bamboleo,
	 * tinta, explosión, ancla), noqueo de la artillera si bGunnerHit y daño. False si el escudo lo anula.
	 */
	bool ReceiveAmmoHit(ETNRallyAmmo Ammo, const FVector& WorldPoint, const FVector& ShotDir, bool bGunnerHit);

	/** Quita Amount de vida (nada durante la invulnerabilidad tras reventar). Con 0, revienta. */
	void ApplyDamage(float Amount);

	/** Vida llena (al reaparecer en la pista, #720): quita el humo y la vida de las estadísticas. */
	void RestoreFullHealth();

	/**
	 * Si el contacto del chasis de Owner con Other puede ser un choque. No lo son el propio buggy ni un proyectil (la
	 * munición ya quita su daño en ReceiveAmmoHit) ni nada con movimiento de proyectil (objetos lanzados).
	 */
	static bool IsCrashContact(const AActor* Owner, const AActor* Other);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ── Ajuste y efectos (EditDefaultsOnly; sin asset, solo la ráfaga básica) ─

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Vida")
	float MaxHealth = 100.f;

	/**
	 * Humo continuo a media vida, sujeto a la carrocería. Vacío = humo de malla propia: bocanadas grises que suben desde
	 * el motor (ATN_RallyBurstFX), más seguidas cuanta menos vida.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Efectos")
	TObjectPtr<UNiagaraSystem> SmokeFX;

	/** Radio final de cada bocanada de humo de malla propia (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Efectos", meta = (ClampMin = "1"))
	float SmokePuffRadiusCm = 45.f;

	/** Si está saliendo humo de malla propia (bocanadas programadas). Para los tests. */
	bool IsEmittingSmokePuffs() const;

	/** Nube de arena al reventar. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Efectos")
	TObjectPtr<UNiagaraSystem> ExplosionFX;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Efectos")
	TObjectPtr<USoundBase> ExplosionSound;

	/** Chispas y golpe de un choque fuerte. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Efectos")
	TObjectPtr<UNiagaraSystem> CrashFX;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Efectos")
	TObjectPtr<USoundBase> CrashSound;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	ATN_Buggy* GetBuggy() const;
	/** Semilongitud y semianchura de la carrocería (cm), de la malla o por defecto. */
	FVector2D GetHalfExtents() const;
	void CreateGunnerHitbox();
	void ApplyAmmoPush(ETNRallyAmmo Ammo, const FVector& WorldPoint, const FVector& ShotDir);
	void ApplyAmmoEffect(ETNRallyAmmo Ammo, const FVector& WorldPoint);
	void Explode();
	void UpdateSmoke();
	/** Suelta una bocanada sobre el motor y programa la siguiente según la vida (sin SmokeFX). */
	void SpawnSmokePuff();

	UFUNCTION()
	void OnChassisHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);
	void HandleCrash(AActor* OtherActor, const FVector& NormalImpulse, const FHitResult& Hit);
	void TryRunOver(ATortugaCharacter* Turtle);

	UFUNCTION()
	void OnRep_Health();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastExplode(FVector_NetQuantize Where);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastCrash(FVector_NetQuantize Where);

	UPROPERTY(ReplicatedUsing = OnRep_Health)
	float Health = 100.f;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> SmokeComponent;

	UPROPERTY(Transient)
	TObjectPtr<USphereComponent> GunnerHitbox;

	double InvulnerableUntil = 0.0;
	double LastCrashTime = -1000.0;
	TMap<TWeakObjectPtr<AActor>, double> LastRunOver;
	FTimerHandle RestoreTimer;
	FTimerHandle SmokePuffTimer;
};
