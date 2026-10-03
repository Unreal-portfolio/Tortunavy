#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_EnemySeagull.generated.h"

class UStaticMeshComponent;
class UDecalComponent;
class USoundBase;
class UNiagaraSystem;
class ATortugaCharacter;
class APlayerController;
class APlayerState;

/**
 * Gaviota dinámica enemiga (#11) — versión unificada.
 *
 * Ciclo de vida:
 *   1. ATN_SeagullSpawnZone spawnea esta gaviota encima del jugador objetivo
 *      que está dentro de la zona, y llama InitializeWithTarget.
 *   2. La gaviota sigue al jugador a FollowSpeed, proyectando un decal (círculo)
 *      que se encoge con el tiempo.
 *   3. ESCAPE: si el jugador permanece fuera del círculo EscapeSeconds seguidos
 *      → la gaviota sube y desaparece (AbortAndRetreat).
 *   4. CUBIERTA: cada RoofCheckInterval el servidor lanza un LineTrace desde la
 *      gaviota hacia el jugador. Si hay geometría entre ambos → AbortAndRetreat.
 *   5. ATAQUE: cuando el cronómetro llega a 0 y el jugador está dentro de
 *      MinKillRadius → picotazo físico (dive hacia abajo, aplica kill, sube y se destruye).
 *      Sombrilla (#29) y BigHead (#2) abortan el kill según sus reglas habituales.
 *
 * Red:
 *   Servidor controla todo (movimiento, escape, kill).
 *   bReplicateMovement=true → el dive es visible en todos los clientes.
 *   CountdownRemaining y TargetPlayerState replicados para feedback visual de clientes.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_EnemySeagull : public AActor
{
	GENERATED_BODY()

public:
	ATN_EnemySeagull();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Inicializa la gaviota con un objetivo. Llamar desde ATN_SeagullSpawnZone tras spawnear.
	 * Solo servidor.
	 */
	UFUNCTION(BlueprintCallable, Category = "EnemySeagull")
	void InitializeWithTarget(ATortugaCharacter* Target);

	/** Cronómetro restante (s), derivado del timestamp replicado. Para HUD/VFX. */
	UFUNCTION(BlueprintPure, Category = "EnemySeagull")
	float GetCountdownRemaining() const { return ComputeCountdownRemaining(); }

	/** Radio de peligro actual interpolado con el tiempo. */
	UFUNCTION(BlueprintPure, Category = "EnemySeagull")
	float GetCurrentDangerRadius() const;

	/**
	 * Gaviota viva que tiene marcado a este jugador en el mundo dado (nullptr si ninguna).
	 * Para que el HUD avise a quien tiene una encima (#164). No cuenta la que ya pica o se retira.
	 */
	static const ATN_EnemySeagull* FindMarking(const UWorld* World, const APlayerState* Player);

protected:
	// ── Componentes ────────────────────────────────────────────────────────────

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EnemySeagull")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EnemySeagull")
	TObjectPtr<UStaticMeshComponent> SeagullMesh;

	/** Decal proyectado hacia abajo. Radio se actualiza cada tick. Asignar material de círculo en el BP. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EnemySeagull")
	TObjectPtr<UDecalComponent> DangerDecal;

	// ── Seguimiento ────────────────────────────────────────────────────────────

	/** Velocidad de seguimiento (cm/s). Debe ser menor que WalkSpeed (450) para que el jugador pueda escapar. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Follow", meta = (ClampMin = "50.0"))
	float FollowSpeed = 350.f;

	/** Altura de vuelo sobre el objetivo (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Follow", meta = (ClampMin = "100.0"))
	float FollowHeight = 400.f;

	/**
	 * Amplitud (cm) del bobbing natural mientras la gaviota sigue al objetivo.
	 * 0 = movimiento estático rígido. Valores altos = más caricaturesco.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Follow", meta = (ClampMin = "0.0"))
	float IdleBobAmplitude = 25.f;

	/** Frecuencia (Hz) del bob principal en Z. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Follow", meta = (ClampMin = "0.1"))
	float IdleBobFrequency = 1.4f;

	// ── Ataque ─────────────────────────────────────────────────────────────────

	/** Duración total del cronómetro antes del ataque (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Attack", meta = (ClampMin = "1.0"))
	float AttackTimerSeconds = 8.f;

	/** Radio inicial del círculo de peligro (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Attack", meta = (ClampMin = "50.0"))
	float MaxDangerRadius = 500.f;

	/** Radio mínimo al expirar el cronómetro. El jugador debe estar fuera de este radio para sobrevivir. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Attack", meta = (ClampMin = "10.0"))
	float MinKillRadius = 150.f;

	// ── Escape ─────────────────────────────────────────────────────────────────

	/** Segundos fuera del círculo de peligro para que la gaviota se retire. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Escape", meta = (ClampMin = "0.5"))
	float EscapeSeconds = 3.f;

	// ── Cubierta ───────────────────────────────────────────────────────────────

	/** Intervalo entre comprobaciones de techo (s). Menor = más reactivo pero más caro. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Cover", meta = (ClampMin = "0.05"))
	float RoofCheckInterval = 0.25f;

	// ── Picotazo físico ────────────────────────────────────────────────────────

	/** Duración del dive hacia abajo (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Strike", meta = (ClampMin = "0.05"))
	float StrikeDuration = 0.15f;

	/** Duración de la subida tras el picotazo (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Strike", meta = (ClampMin = "0.1"))
	float RiseDuration = 0.5f;

	/** Altura adicional sobre el spawn point a la que sube al retirarse (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Strike", meta = (ClampMin = "100.0"))
	float RetreatHeight = 800.f;

	// ── Visual ─────────────────────────────────────────────────────────────────

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Visual", meta = (ClampMin = "50.0"))
	float DecalDepth = 800.f;

	/**
	 * Escala de la gaviota de la fauna (TNFaunaBuildSpecies, 80 cm de envergadura a escala 1) que se monta en la posición
	 * real del actor si SeagullMesh no lleva una malla del proyecto. Con una malla propia no se monta nada.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Visual", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float CodeArtScale = 6.f;

	/** Aleteo de la gaviota de código: amplitud (grados) y frecuencia (Hz). En el picado lleva las alas recogidas. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Visual", meta = (ClampMin = "0.0", ClampMax = "80.0"))
	float CodeArtFlapDegrees = 35.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Visual", meta = (ClampMin = "0.1"))
	float CodeArtFlapHz = 2.4f;

	/** Velocidad de bajada (cm/s) a partir de la cual la gaviota de código pliega las alas: está picando. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EnemySeagull|Visual", meta = (ClampMin = "50.0"))
	float CodeArtDiveSpeed = 600.f;

	// ── Audio / VFX ────────────────────────────────────────────────────────────

	/** Sonido al hacer el picotazo. Reproducido en el punto del objetivo en todas las máquinas. */
	UPROPERTY(EditDefaultsOnly, Category = "EnemySeagull|Audio")
	TObjectPtr<USoundBase> StrikeSound;

	/** Sonido al retirarse (escape o cubierta detectada). */
	UPROPERTY(EditDefaultsOnly, Category = "EnemySeagull|Audio")
	TObjectPtr<USoundBase> RetreatSound;

	/** Sonido al matar al jugador. */
	UPROPERTY(EditDefaultsOnly, Category = "EnemySeagull|Audio")
	TObjectPtr<USoundBase> KillSound;

	/** Sonido al absorber el BigHead en lugar de matar. */
	UPROPERTY(EditDefaultsOnly, Category = "EnemySeagull|Audio")
	TObjectPtr<USoundBase> BigHeadAbsorbSound;

	/** VFX spawneado en el punto del objetivo al hacer el picotazo. */
	UPROPERTY(EditDefaultsOnly, Category = "EnemySeagull|VFX")
	TObjectPtr<UNiagaraSystem> StrikeVFX;

private:
	// ── Estado replicado ───────────────────────────────────────────────────────

	/**
	 * Instante del reloj sincronizado del servidor (GetServerWorldTimeSeconds) en
	 * que arrancó el countdown. Se replica UNA VEZ en vez de replicar el countdown
	 * cada tick: cada máquina deriva el restante localmente → menos tráfico y el
	 * decal del cliente se anima suave (tick local) en vez de a saltos de red.
	 * -1 = aún sin inicializar (InitializeWithTarget no llamado).
	 */
	UPROPERTY(Replicated)
	float AttackStartServerTime = -1.f;

	/** Countdown restante derivado de AttackStartServerTime (clamp [0, AttackTimerSeconds]). */
	float ComputeCountdownRemaining() const;

	/**
	 * PlayerState del objetivo — replicado para que clientes resuelvan la referencia.
	 * Puntero (NetGUID) en lugar de índice de PlayerArray: el array se reordena con
	 * disconnects/JIP y un índice replicado apuntaría al jugador equivocado.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_TargetPlayerState)
	TObjectPtr<APlayerState> TargetPlayerState;

	UFUNCTION()
	void OnRep_TargetPlayerState();

	/** Gaviotas en juego (todas las máquinas), para FindMarking sin recorrer el mundo cada frame. */
	static TArray<TWeakObjectPtr<ATN_EnemySeagull>> Active;

	// ── Estado solo servidor ──────────────────────────────────────────────────

	TWeakObjectPtr<ATortugaCharacter> TargetCharacter;

	float TimeOutsideShadow = 0.f;
	float NextRoofCheckTime = 0.f;
	bool  bAttackResolved   = false;

	// ── Strike state machine ───────────────────────────────────────────────────

	bool  bIsStriking       = false;
	bool  bIsRetreating     = false;
	bool  bStrikeGoingDown  = true;
	float StrikeAlpha       = 0.f;
	float StrikeStartZ      = 0.f;
	float StrikeTargetZ     = 0.f;
	float RetreatStartZ     = 0.f;
	float RetreatEndZ       = 0.f;

	// ── Lógica ────────────────────────────────────────────────────────────────

	void TickFollowTarget(float DeltaTime);
	void TickCountdown(float DeltaTime);
	void TickEscapeCheck(float DeltaTime);
	void TickRoofCheck(float DeltaTime);
	void TickStrike(float DeltaTime);
	void TickRetreat(float DeltaTime);

	void ResolveAttack();
	void AbortAndRetreat();
	bool HasRoofBetweenSeagullAndTarget() const;
	bool IsTargetUnderUmbrella() const;

	void UpdateDecalSize();

	// ── Arte de código (#50; local en cada máquina) ───────────────────────────

	/** Raíz de la gaviota de código (en el origen del actor; gira hacia donde vuela). */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> CodeArtRoot;

	/** Piezas de la gaviota (cuerpo primero) y su pivote de reposo. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> CodeArtParts;

	TArray<FVector> CodeArtPivots;
	int32 CodeArtWingLeft = INDEX_NONE;
	int32 CodeArtWingRight = INDEX_NONE;
	int32 CodeArtLegLeft = INDEX_NONE;
	int32 CodeArtLegRight = INDEX_NONE;
	float CodeArtClock = 0.f;
	FVector CodeArtLastLocation = FVector::ZeroVector;

	/** Monta la gaviota de la fauna y oculta el marcador del motor. */
	void BuildCodeArt();

	/** Aleteo, patas recogidas y rumbo; alas plegadas al picar. */
	void AnimateCodeArt(float DeltaTime);

	void PoseCodeArtPart(int32 Index, const FRotator& Rotation);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayStrikeEffects(FVector TargetLocation);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayRetreatEffect();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayKillEffect(ATortugaCharacter* Victim);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayBigHeadAbsorbEffect(ATortugaCharacter* Target);
};
