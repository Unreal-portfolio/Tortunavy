#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_PhysicsObjectActor.generated.h"

class UStaticMeshComponent;
class USphereComponent;

/**
 * Actor físico replicado con gestión automática de dormancia.
 *
 * Diseño:
 *   - Servidor simula la física (autoridad). Clientes reciben la posición
 *     replicada vía bReplicateMovement y NO simulan localmente.
 *   - En reposo: DORM_DormantAll — 0 bytes enviados.
 *   - Al recibir un golpe: FlushNetDormancy despierta el actor y empieza
 *     a replicar hasta que la velocidad baja del umbral → vuelve a dormir.
 *
 * Uso:
 *   Crear un BP hijo, asignar el StaticMesh y el material.
 *   No es necesario tocar la lógica en Blueprint.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_PhysicsObjectActor : public AActor
{
	GENERATED_BODY()

public:
	ATN_PhysicsObjectActor();

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Physics")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Sphere overlap usada para detectar tortugas en contacto cuando el modo
	 *  kinematic-push está activo. Radio = collision del mesh + extra. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Physics|KinematicPush")
	TObjectPtr<USphereComponent> PushDetector;

	// ── Modo kinematic-push (cubo empujable sin físicas reales) ──────────────
	// Default: true. Cubo NO simula físicas — se mueve solo cuando una tortuga
	// está en contacto y caminando hacia él. Sweep cancela componente normal
	// vs paredes/colliders → el cubo nunca atraviesa geometría ni se queda
	// "tieso" por simulación divergente entre máquinas (server-auth puro).
	//
	// Hijos (BouncePhysicsObject) deben establecer bUseKinematicPush=false en
	// su ctor para mantener simulación física real (balón rebotando).

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physics|KinematicPush")
	bool bUseKinematicPush = true;

	/** Radio extra (cm) del PushDetector más allá del bounding del mesh.
	 *  Margen para que el overlap detecte la tortuga antes de que la collision
	 *  del mesh la bloquee — sino el cubo nunca empieza a moverse.
	 *  60cm = la cápsula del jugador (radio ~34) entra al detector con margen
	 *  cómodo (~26cm de overlap interior) antes de tocar la superficie del
	 *  Mesh (Block to Pawn). Subir si el cubo es muy grande / cápsula no detecta. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physics|KinematicPush",
		meta = (ClampMin = "5.0", EditCondition = "bUseKinematicPush"))
	float PushDetectionRadiusExtra = 60.f;

	/** Velocidad máxima (cm/s) que el cubo puede alcanzar siendo empujado.
	 *  Cap de seguridad — incluso con varios jugadores empujando, el cubo no
	 *  vuela. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physics|KinematicPush",
		meta = (ClampMin = "50.0", EditCondition = "bUseKinematicPush"))
	float MaxKinematicPushSpeed = 600.f;

	/** Multiplicador aplicado a la velocity proyectada de la tortuga. Si la
	 *  tortuga camina a 450 cm/s contra el cubo y el factor es 0.6 → cubo se
	 *  mueve a 270 cm/s (más lento que la tortuga, como empujar peso real). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physics|KinematicPush",
		meta = (ClampMin = "0.1", ClampMax = "1.0", EditCondition = "bUseKinematicPush"))
	float PushSpeedFactor = 0.6f;

	/**
	 * Aceleración (cm/s²) aplicada hacia abajo en modo kinematic-push para que
	 * el cubo caiga al suelo bajo gravedad simulada. El sweep cancela el movimiento
	 * vertical al tocar suelo. Default 980 (~gravedad terrestre).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physics|KinematicPush",
		meta = (ClampMin = "0.0", EditCondition = "bUseKinematicPush"))
	float KinematicGravity = 980.f;

	/** Velocidad terminal de caída en modo kinematic-push (cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physics|KinematicPush",
		meta = (ClampMin = "100.0", EditCondition = "bUseKinematicPush"))
	float KinematicMaxFallSpeed = 1500.f;

	/** Velocidad (UU/s) por debajo de la cual el actor vuelve a dormancy. */
	UPROPERTY(EditDefaultsOnly, Category = "Physics|Network", meta = (ClampMin = "1.0"))
	float SleepVelocityThreshold = 15.f;

	/** Intervalo (s) entre comprobaciones de velocidad tras un impacto. */
	UPROPERTY(EditDefaultsOnly, Category = "Physics|Network", meta = (ClampMin = "0.1"))
	float DormancyCheckInterval = 1.5f;

	/** Velocidad máxima (cm/s) que puede alcanzar el objeto tras un impacto de fuente externa.
	 *  Cap de seguridad contra bolas lanzadas u otros physics actors de alta velocidad. */
	UPROPERTY(EditDefaultsOnly, Category = "Physics|Network", meta = (ClampMin = "100.0"))
	float MaxPushVelocity = 1200.f;

	/** Bloquear rotación alrededor del eje X (roll). Evita que el actor ruede de lado. */
	UPROPERTY(EditDefaultsOnly, Category = "Physics|Constraints")
	bool bLockRotationX = true;

	/** Bloquear rotación alrededor del eje Y (pitch). Evita que el actor vuelque hacia delante/atrás. */
	UPROPERTY(EditDefaultsOnly, Category = "Physics|Constraints")
	bool bLockRotationY = true;

	/**
	 * Bloquear rotación alrededor del eje Z (yaw). Evita que el actor spinee
	 * verticalmente cuando el jugador lo empuja oblicuo. Default: true → el
	 * cubo de empuje se comporta como caja slide-only.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Physics|Constraints")
	bool bLockRotationZ = true;

	/**
	 * Detecta clipping contra geometría estática: si el objeto queda atrapado
	 * entre dos superficies (penetración en direcciones opuestas en ≥2 ejes),
	 * se destruye con un "poof" en lugar de quedarse vibrando dentro de paredes.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Physics|Crush")
	bool bEnableCrushDetection = true;

	// ── Anti phase-through (2026-04-24) ──────────────────────────────────────
	// Cuando la tortuga (u otro physics object) empuja horizontalmente un objeto
	// contra una pared estática, la fuerza aplicada por frame puede superar lo
	// que CCD compensa → el objeto atraviesa la pared (phase-through).
	//
	// Solución: cada tick server-authoritative, proyectamos un sweep en la
	// dirección de la velocidad horizontal. Si impacta con geometría estática
	// vertical (una pared), cancelamos la componente de velocidad normal a la
	// pared — equivale a la fuerza normal que una pared real aplicaría. El
	// componente tangencial (slide) queda intacto. La gravedad y los contactos
	// con suelo/techo los sigue manejando la física nativa.

	/** Habilita el sistema anti phase-through. Default ON: en modo físico
	 *  (BouncePhysicsObject) la bola con velocidad alta atravesaba suelos a
	 *  pesar del CCD nativo de Chaos. El sweep manual cancela el componente
	 *  normal contra paredes para que la bola "salga lateralmente" en vez
	 *  de perforar. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Physics|AntiPhaseThrough")
	bool bEnableAntiPhaseThrough = true;

	/** Velocidad horizontal mínima (cm/s) para que el sweep se ejecute. Por
	 *  debajo no hay riesgo de tunneling — ahorro de queries. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Physics|AntiPhaseThrough", meta = (ClampMin = "10.0"))
	float AntiPhaseMinSpeed = 50.f;

	/** Padding extra (cm) añadido a la distancia del sweep — anticipa la pared
	 *  antes de impactarla. Valor alto = detección temprana + más overhead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Physics|AntiPhaseThrough", meta = (ClampMin = "0.0"))
	float AntiPhaseProbePadding = 8.f;

	/** Radio (cm) de la esfera del sweep. Fijo pequeño para que no dependa del
	 *  tamaño del mesh. Valor alto = más propenso a false positives. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Physics|AntiPhaseThrough", meta = (ClampMin = "1.0"))
	float AntiPhaseSweepRadius = 10.f;

	/** Tolerancia de verticalidad: |Normal.Z| ≤ este valor → pared.
	 *  0.4 descarta suelos (~66°) y techos. Ajustable para rampas. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Physics|AntiPhaseThrough", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AntiPhaseMaxNormalZ = 0.4f;

	/** Intervalo (s) entre chequeos de crush. 0 = solo al detenerse. */
	UPROPERTY(EditDefaultsOnly, Category = "Physics|Crush", meta = (ClampMin = "0.2"))
	float CrushCheckInterval = 2.0f;

	/** VFX opcional al destruirse por crush. */
	UPROPERTY(EditDefaultsOnly, Category = "Physics|Crush")
	TObjectPtr<class UNiagaraSystem> CrushPoofVFX;

	/** Sonido opcional al destruirse por crush. */
	UPROPERTY(EditDefaultsOnly, Category = "Physics|Crush")
	TObjectPtr<class USoundBase> CrushPoofSound;

private:
	UFUNCTION()
	void OnMeshHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
	               UPrimitiveComponent* OtherComp, FVector NormalImpulse,
	               const FHitResult& Hit);

	/** Diagnóstico: log al primer overlap de tortuga con el PushDetector. */
	UFUNCTION()
	void OnPushDetectorBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	                                UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	                                bool bFromSweep, const FHitResult& SweepResult);

	void TryEnterDormancy();
	void CheckForCrush();
	void TickKinematicPush(float DeltaTime);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastCrushPoof();

	FTimerHandle DormancyCheckTimer;
	FTimerHandle CrushCheckTimer;

	/** Velocidad vertical acumulada en modo kinematic-push. Reset al tocar suelo. */
	float KinematicFallSpeed = 0.f;

	/** Throttle per-instance para los logs [CUBE-PUSH]. Antes era `static` →
	 *  compartido entre todos los cubos del mundo (solo loggeaba uno). */
	double LastPushLogTime = 0.0;
	double LastZeroPushLogTime = 0.0;
};
