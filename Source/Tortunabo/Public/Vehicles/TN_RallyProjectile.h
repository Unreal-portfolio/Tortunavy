// Proyectiles de la torreta del buggy y sus efectos (charco de alga, explosión, ráfagas cosméticas). El servidor decide
// los impactos; los clientes reciben el proyectil con su velocidad de salida y lo simulan en local (sin movimiento
// replicado), y las ráfagas son cosméticas y locales (multicast no fiable de la torreta).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rally/TN_RallyVehicle.h"
#include "TN_RallyProjectile.generated.h"

class ATN_Buggy;
class UNiagaraSystem;
class USoundBase;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

namespace TNRallyParticles
{
	// Emisores de partículas de una ráfaga (Private/Vehicles/TN_RallyFXParticles.h).
	struct FEmitterSet;
}

/** Colores de la munición (proyectil, cañón de la torreta y pantallita) y tinte de las mallas básicas. */
namespace TNRallyLook
{
	TORTUNABO_API FLinearColor AmmoColor(ETNRallyAmmo Ammo);
	/** Pone Color en el parámetro «Color» del material de Mesh (crea la instancia dinámica si hace falta). */
	TORTUNABO_API void Tint(UStaticMeshComponent* Mesh, const FLinearColor& Color);
}

/** Proyectil de una munición: coco, alga, burbuja, mortero, tinta o ancla (Docs/Rally_MVP.md). */
UCLASS()
class TORTUNABO_API ATN_RallyProjectile : public AActor
{
	GENERATED_BODY()

public:
	ATN_RallyProjectile();

	/** Antes de FinishSpawning: munición, velocidad inicial y buggy que dispara (no se impacta a sí mismo al salir). */
	void Init(ETNRallyAmmo InAmmo, const FVector& Velocity, ATN_Buggy* FiredBy);

	ETNRallyAmmo GetAmmo() const { return Ammo; }

	/**
	 * Solo servidor: la explosión del mortero en Where (radio TNRallyTurret::MortarRadiusCm) a todos los buggies dentro,
	 * también al de Shooter. HitBuggy, si lo hay, recibe el golpe en Where (y en la artillera con bGunnerHit); el resto, en
	 * su centro. La confirmación de impactos dice ReportAmmo. La usan el mortero y el pez globo (#773).
	 */
	static void MortarBlastAt(UWorld* World, ATN_Buggy* Shooter, ATN_Buggy* HitBuggy, const FVector& Where, const FVector& Dir,
		bool bGunnerHit, ETNRallyAmmo ReportAmmo = ETNRallyAmmo::Mortero);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Estela opcional que sigue al proyectil (solo en las máquinas con pantalla). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Efectos")
	TObjectPtr<UNiagaraSystem> TrailFX;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** Fin de la vida sin impacto: el alga deja igualmente su charco en el suelo de debajo (#770). */
	virtual void LifeSpanExpired() override;

private:
	UFUNCTION()
	void OnRep_Ammo();
	void ApplyLook();
	void StartLocalSimulation();

	UFUNCTION()
	void OnSphereHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	UFUNCTION()
	void OnSphereOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	/**
	 * Solo servidor: aplica el efecto de la munición en Where, sobre HitBuggy si lo hay, y destruye el proyectil (salvo la
	 * burbuja contra el suelo). bGunnerHit: ha dado en la artillera (UTN_BuggyHealthComponent::GunnerHitboxTag).
	 */
	void Impact(ATN_Buggy* HitBuggy, const FVector& Where, bool bGunnerHit = false);
	/** Impacto directo de Ammo en un buggy: vida, empujón en el punto y efecto (o el camino antiguo sin componente de vida). */
	void HitBuggyWith(ATN_Buggy* HitBuggy, const FVector& Where, const FVector& Dir, bool bGunnerHit);
	void SpawnAlgaPuddle(ATN_Buggy* HitBuggy, const FVector& Where);
	void MortarBlast(ATN_Buggy* HitBuggy, const FVector& Where, const FVector& Dir, bool bGunnerHit);
	void StopOwnerIgnore();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USphereComponent> Sphere;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> Movement;

	UPROPERTY(ReplicatedUsing = OnRep_Ammo)
	ETNRallyAmmo Ammo = ETNRallyAmmo::Coco;

	/** Velocidad de salida (Init): los clientes simulan el vuelo desde ella, sin movimiento replicado. */
	UPROPERTY(Replicated)
	FVector_NetQuantize10 LaunchVelocity = FVector::ZeroVector;

	UPROPERTY(Transient)
	TWeakObjectPtr<ATN_Buggy> Shooter;

	bool bImpacted = false;
	FTimerHandle OwnerIgnoreTimer;
};

/**
 * Charco de alga: 6 m durante 5 s; agarre y velocidad máxima reducidos y un derrape al entrar a cualquier buggy dentro
 * (servidor). El disco se apoya en el suelo con su inclinación (#770).
 */
UCLASS()
class TORTUNABO_API ATN_RallyAlgaPuddle : public AActor
{
	GENERATED_BODY()

public:
	ATN_RallyAlgaPuddle();

	/**
	 * Solo servidor: charco en el suelo bajo Where (#770). Busca solo el escenario (canal de objetos WorldStatic: ni
	 * buggies ni tortugas) bajo Where y, si ahí no hay suelo (pared, barrera), un poco más atrás en BackDir; ajusta el disco
	 * al plano del suelo. Dropper (Karts) no lo pisa durante TNRallyTurret::AlgaDropperGraceSeconds. Null sin suelo.
	 */
	static ATN_RallyAlgaPuddle* SpawnOnGround(UWorld* World, const FVector& Where, const FVector& BackDir, ATN_Buggy* Dropper = nullptr);

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Chapoteo al caer el charco, en cada máquina con audio. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Efectos")
	TObjectPtr<USoundBase> SplashSound;

	/** Buggies cuyo escudo ya ha anulado este charco. */
	TSet<TWeakObjectPtr<ATN_Buggy>> Immune;
	float CheckAccumulator = 0.f;

	/** Quien lo ha soltado en Karts (servidor): no lo pisa al principio. */
	TWeakObjectPtr<ATN_Buggy> Dropper;
};

UENUM()
enum class ETNRallyBurstKind : uint8
{
	CocoHit,
	Explosion,
	Ink,
	BubblePop,
	Shield,
	/** Nube de arena del buggy que revienta. */
	Sand,
	/** Fogonazo del cañón. */
	MuzzleFlash,
	/** Chispas de un choque. */
	Sparks,
	/** Bocanada de humo de un buggy a media vida: sube mientras crece. */
	Smoke
};

/**
 * Ráfaga cosmética corta de un impacto (#301): partículas propias de cada munición (cáscara y arena del coco, brasas y humo
 * de la explosión, gotas de tinta, gotas y onda de la burbuja, chispas, nube de arena) y su sonido. El escudo y el fogonazo
 * conservan la esfera que crece. No se replica: cada máquina crea la suya.
 */
UCLASS()
class TORTUNABO_API ATN_RallyBurstFX : public AActor
{
	GENERATED_BODY()

public:
	ATN_RallyBurstFX();

	/** Crea la ráfaga en esta máquina (nada en un servidor dedicado). */
	static ATN_RallyBurstFX* Spawn(UWorld* World, ETNRallyBurstKind Kind, const FVector& Where, float RadiusCm);

	/**
	 * Solo servidor: la ráfaga en todas las máquinas por el multicast no fiable de la torreta de Via (el buggy implicado).
	 * Sin torreta, solo en esta máquina.
	 */
	static void Broadcast(ATN_Buggy* Via, ETNRallyBurstKind Kind, const FVector& Where, float RadiusCm);

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	void ApplyLook();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Sonido de cada impacto (coco, mortero, tinta, burbuja, escudo). El fogonazo, la arena y las chispas suenan en su dueño. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Efectos")
	TMap<ETNRallyBurstKind, TObjectPtr<USoundBase>> ImpactSounds;

	ETNRallyBurstKind Kind = ETNRallyBurstKind::CocoHit;
	float RadiusCm = 100.f;
	float Age = 0.f;

	/** Partículas de la ráfaga (vacío en un servidor dedicado). */
	TSharedPtr<TNRallyParticles::FEmitterSet> Particles;
};

/** Peligros de la pista que un bot puede saltar con la medusa (#771). */
namespace TNRallyHazards
{
	/** Servidor: una concha teledirigida persigue a Buggy de cerca o tiene un charco de alga justo delante. */
	TORTUNABO_API bool HopThreatNear(const ATN_Buggy& Buggy);
}
