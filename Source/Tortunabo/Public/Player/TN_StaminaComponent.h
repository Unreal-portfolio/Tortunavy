#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/TN_MovementLimits.h"
#include "TN_StaminaComponent.generated.h"

class UTN_InventoryComponent;

/**
 * @brief Componente que gobierna la stamina del personaje (sprint, recarga, agotamiento, boosts).
 *
 * Reglas principales:
 *  - MaxStamina base reducida por peso del inventario (StaminaPerWeightUnit × TotalWeight).
 *  - Sprint drena SprintDrainPerSecond. Al llegar a 0 entra en Exhausted (ExhaustionPenaltySeconds).
 *  - Recarga tras RechargeDelaySeconds con curva exponencial.
 *  - GrantUnlimitedStamina activa boost temporal seguido de PostBoostExhaustion (velocidad reducida + drenaje ×N).
 *  - SetSpeedCap(Source, Cap) limita MaxWalkSpeed con topes con nombre (zonas lentas, mareo, llevar a otra, algas,
 *    caparazón): manda el menor y cada sistema quita el suyo. SetJumpLimit y SetGravityScaleOverride hacen lo mismo con
 *    el salto y la gravedad, y devuelven la base al quitar el último (TN_MovementLimits.h).
 *
 * Replicación: CurrentStamina (float) solo al dueño; a los demás (espectadores, caras del HUD, foley), StaminaShared, un
 * byte con la fracción de MaxStamina que solo se manda cuando cambia. bIsSprinting, bUnlimitedStamina y bIsExhausted to all.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TORTUNABO_API UTN_StaminaComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_StaminaComponent();

	/** @brief Inicializa CurrentStamina al máximo y registra delegate de speed-cap si aplica. */
	virtual void BeginPlay() override;

	/** @brief Tick: gestiona timers de boost, ticks de drenaje/recarga y refresca movement speed. */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * @brief Pide activar o desactivar el sprint en esta máquina. No va por RPC: la petición del cliente dueño viaja en sus
	 *        movimientos (UTN_TurtleMovementComponent, predicha), y el servidor la recibe de ellos al mismo tiempo que el
	 *        movimiento que la usa. Antes llegaba por su RPC a destiempo y cada cambio de velocidad era una corrección.
	 * @param bRequested true = mantener sprint activo si hay stamina; false = soltar.
	 */
	UFUNCTION(BlueprintCallable, Category = "Stamina")
	void SetSprintRequested(bool bRequested);

	/** Con esta petición de sprint, si de verdad corre: hace falta estamina (o tenerla ilimitada). */
	bool CanSprint(bool bRequested) const;

	/**
	 * Velocidad máxima andando (cm/s) corriendo o no, con este multiplicador del entorno y este turbo de los objetos de
	 * carrera (1 = sin turbo): penalización tras el boost, turbo y el tope que mande (TNMovementLimits::ResolveWalkSpeed).
	 * La usa el movimiento en cada paso, con el sprint, el vadeo y el turbo de ese movimiento (FTNSavedMove_Turtle), para
	 * que el cliente y el servidor calculen lo mismo (#250, #22).
	 */
	float ComputeMaxWalkSpeed(bool bSprinting, float EnvironmentMultiplier, float RaceMultiplier = 1.f) const;

	/**
	 * Como ComputeMaxWalkSpeed, pero los topes predichos (llevar a otra, mareo: TNMovementLimits::PredictedCapBit) son los
	 * del movimiento que se simula (MovePredictedCaps), no los que tiene ahora esta máquina (#575, #574).
	 */
	float ComputeMoveMaxWalkSpeed(bool bSprinting, float EnvironmentMultiplier, float RaceMultiplier, uint8 MovePredictedCaps) const;

	/** Los topes predichos que tiene puestos esta máquina (bits de TNMovementLimits). */
	uint8 GetPredictedCapMask() const { return PredictedCapMask; }

	/**
	 * Servidor, cada movimiento validado de su dueño (una vez por movimiento, con su DeltaTime): con qué topes predichos lo
	 * simula si pide ClaimedMask (TNMovementLimits::StepPredictedCap con la ventana del último cambio de cada tope). Avanza
	 * el reloj de movimientos del dueño, que es con el que se miden las ventanas.
	 */
	uint8 ConsumeClientPredictedCaps(uint8 ClaimedMask, float MoveDeltaSeconds);

	/** Reloj de movimientos del dueño en el servidor: la suma de los DeltaTime que ha validado (s). */
	double GetServerMoveClock() const { return ServerMoveClock; }

	/**
	 * @brief Otorga stamina ilimitada durante DurationSeconds (Barrita Energética / boosts).
	 * @param DurationSeconds Duración del boost.
	 * @note Al expirar, activa PostBoostExhaustion (multiplicadores de velocidad y drenaje).
	 */
	UFUNCTION(BlueprintCallable, Category = "Stamina")
	void GrantUnlimitedStamina(float DurationSeconds);

	/** @brief Restaura la stamina al máximo efectivo e invalida la penalización de agotamiento. */
	UFUNCTION(BlueprintCallable, Category = "Stamina")
	void RestoreStaminaToFull();

	/** @brief Sobreescribe en runtime la penalización post-boost (útil para ítems con distinto penalty). */
	void SetPostBoostExhaustionSeconds(float NewValue) { PostBoostExhaustionSeconds = FMath::Max(0.f, NewValue); }

	/**
	 * @brief Pone (o cambia) el tope de velocidad de Source: zona lenta, mareo, llevar a otra, alga, caparazón...
	 *        Manda el menor de todos (TNMovementLimits::ResolveSpeedCap) y ApplyMovementSpeed lo respeta:
	 *        MaxWalkSpeed = Min(WalkSpeed|SprintSpeed, ActiveSpeedCap). Cada sistema quita solo el suyo.
	 * @param Source Quién lo pone (un nombre fijo, o el del actor si puede haber varios a la vez, como las zonas lentas).
	 * @param Cap Velocidad máxima a forzar (cm/s).
	 * @note Llamar en todas las máquinas (sin HasAuthority) — cada una aplica localmente.
	 */
	void SetSpeedCap(FName Source, float Cap);

	/** @brief Quita el tope de Source; los de los demás siguen. */
	void ClearSpeedCap(FName Source);

	/**
	 * @brief Pone (o cambia) el límite de salto de Source: JumpZVelocity = Min(base × multiplicadores, menor tope).
	 *        La base se guarda al poner el primero y vuelve al quitar el último, en cualquier orden. Llamar donde se mueve
	 *        el personaje (servidor y dueño), como antes se tocaba JumpZVelocity.
	 * @param Cap Salto máximo (cm/s); TNMovementLimits::NoCap para ninguno.
	 * @param Multiplier Sobre el salto de base (1 = ninguno).
	 */
	void SetJumpLimit(FName Source, float Cap, float Multiplier = 1.f);

	/** @brief Quita el límite de salto de Source; sin ninguno, el salto vuelve a su base. */
	void ClearJumpLimit(FName Source);

	/** @brief Pone la escala de gravedad de Source (el sirope de las zonas lentas); manda la menor. Como SetJumpLimit. */
	void SetGravityScaleOverride(FName Source, float Scale);

	/** @brief Quita la escala de gravedad de Source; sin ninguna, la gravedad vuelve a su base. */
	void ClearGravityScaleOverride(FName Source);

	/**
	 * @brief Multiplicador ambiental independiente del speed cap (ej. vadeo de agua).
	 *        ApplyMovementSpeed lo aplica ANTES del Min con ActiveSpeedCap, así que
	 *        no pisa el cap de TN_SlowZoneVolume ni el estado de sprint.
	 * @param Multiplier 1.0 = sin efecto. Llamar en todas las máquinas (sin HasAuthority)
	 *        — cada una aplica localmente, igual que SetSpeedCap.
	 */
	void SetEnvironmentSpeedMultiplier(float Multiplier);

	/** @brief Vincula el componente de inventario para calcular el peso total cargado. */
	void SetInventoryComponent(UTN_InventoryComponent* InvComp);

	UFUNCTION(BlueprintPure, Category = "Stamina")
	float GetCurrentStamina() const { return CurrentStamina; }

	UFUNCTION(BlueprintPure, Category = "Stamina")
	float GetMaxStamina() const { return MaxStamina; }

	/** Velocidad de andar (cm/s), sin esprintar ni penalizaciones. */
	UFUNCTION(BlueprintPure, Category = "Stamina|Movement")
	float GetWalkSpeed() const { return WalkSpeed; }

	/**
	 * Stamina máxima efectiva tras aplicar la penalización por peso.
	 * EffectiveMax = MaxStamina - (TotalWeight * StaminaPerWeightUnit).
	 * La stamina no puede superar este valor mientras se lleva peso.
	 */
	UFUNCTION(BlueprintPure, Category = "Stamina|Weight")
	float GetEffectiveMaxStamina() const;

	/**
	 * Stamina "bloqueada" por el peso: MaxStamina - EffectiveMaxStamina.
	 * Usar en la UI como relleno de la barra de penalización (zona oscura).
	 */
	UFUNCTION(BlueprintPure, Category = "Stamina|Weight")
	float GetWeightPenalty() const { return MaxStamina - GetEffectiveMaxStamina(); }

	UFUNCTION(BlueprintPure, Category = "Stamina")
	bool IsSprinting() const { return bIsSprinting; }

	UFUNCTION(BlueprintPure, Category = "Stamina")
	bool HasUnlimitedStamina() const { return bUnlimitedStamina; }

	/** True mientras la stamina está penalizada por haberse agotado completamente. */
	UFUNCTION(BlueprintPure, Category = "Stamina")
	bool IsExhausted() const { return bIsExhausted; }

	/** True si el efecto post-boost está activo (penalización tras expirar stamina ilimitada). */
	UFUNCTION(BlueprintPure, Category = "Stamina")
	bool IsPostBoostPenalized() const { return bPostBoostPenaltyActive; }

protected:
	/** Stamina máxima base. Configurable desde Blueprint. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stamina", meta = (ClampMin = "1.0"))
	float MaxStamina = 200.0f;

	/**
	 * Stamina máxima que se reduce por cada unidad de peso cargado.
	 * Ejemplo: StaminaPerWeightUnit=20, ítem con ItemWeight=2 → -40 stamina máx.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stamina|Weight", meta = (ClampMin = "0.0"))
	float StaminaPerWeightUnit = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float SprintDrainPerSecond = 15.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float RechargeDelaySeconds = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float RechargeBasePerSecond = 6.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float RechargeExponentGrowth = 1.1f;

	/**
	 * Penalización por agotar la stamina completamente.
	 * Bloquea la recuperación este tiempo extra (en segundos) además de RechargeDelaySeconds.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float ExhaustionPenaltySeconds = 1.0f;

	/**
	 * Segundos de penalización al expirar la stamina ilimitada (Barrita Energética).
	 * Durante este tiempo: MaxWalkSpeed *= PostBoostSpeedMultiplier y el sprint drena
	 * a PostBoostDrainMultiplier × ritmo normal. La recarga NO se bloquea.
	 * 0 = sin penalización. Configurable por BP.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float PostBoostExhaustionSeconds = 4.0f;

	/** Multiplicador de velocidad máxima mientras dura la penalización post-boost. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float PostBoostSpeedMultiplier = 0.75f;

	/** Multiplicador de drenaje de stamina al sprintar durante la penalización. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "1.0"))
	float PostBoostDrainMultiplier = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stamina|Movement", meta = (ClampMin = "0.0"))
	float WalkSpeed = 450.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stamina|Movement", meta = (ClampMin = "0.0"))
	float SprintSpeed = 800.0f;

private:
	/** @brief Server RPC: aplica stamina ilimitada del lado servidor. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerGrantUnlimitedStamina(float DurationSeconds);

	UPROPERTY(ReplicatedUsing = OnRep_CurrentStamina)
	float CurrentStamina = 100.0f;

	/**
	 * CurrentStamina para los que no son el dueño: fracción de MaxStamina en 0..255 (1 byte en vez de un float que cambiaba en
	 * cada fotograma al esprintar o recargar). Lo pone el servidor; en esas máquinas OnRep lo vuelve a pasar a CurrentStamina.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_StaminaShared)
	uint8 StaminaShared = 255;

	UPROPERTY(ReplicatedUsing = OnRep_IsSprinting)
	bool bIsSprinting = false;

	/** Lo pide cada máquina para sí (el servidor, con lo que traen los movimientos del cliente): no se replica. */
	bool bSprintRequested = false;

	UPROPERTY(ReplicatedUsing = OnRep_UnlimitedStamina)
	bool bUnlimitedStamina = false;

	float UnlimitedStaminaRemaining = 0.0f;
	float RechargeElapsed = 0.0f;
	float TimeSinceSprintStopped = 0.0f;
	float ExhaustionTimer = 0.0f;
	float PostBoostPenaltyTimer = 0.0f;

	UPROPERTY(Replicated)
	bool bIsExhausted = false;

	UPROPERTY(Replicated)
	bool bPostBoostPenaltyActive = false;

	/** Referencia al inventario del propietario — necesaria para calcular el peso. */
	TWeakObjectPtr<UTN_InventoryComponent> InventoryComponentRef;

	/** Topes de velocidad por quien los pone (SetSpeedCap). */
	TMap<FName, float> SpeedCaps;

	/**
	 * El menor de SpeedCaps (TNMovementLimits::ResolveSpeedCap): MAX_FLT = sin límite activo. ApplyMovementSpeed hace
	 * Min(baseSpeed, cap).
	 */
	float ActiveSpeedCap = TNumericLimits<float>::Max();

	/** El menor de los topes que no son predichos (los predichos los pone cada movimiento: ComputeMoveMaxWalkSpeed). */
	float UnpredictedSpeedCap = TNumericLimits<float>::Max();

	/** Topes predichos puestos ahora y su último valor. */
	uint8 PredictedCapMask = 0;
	float PredictedCapValues[TNMovementLimits::NumPredictedCaps] = { TNMovementLimits::NoCap, TNMovementLimits::NoCap };

	/** Servidor: la ventana del último cambio de cada tope predicho y el reloj de movimientos del dueño que las mide. */
	TNMovementLimits::FPredictedCapGrace PredictedCapGrace[TNMovementLimits::NumPredictedCaps];
	double ServerMoveClock = 0.0;

	/** Tras cambiar SpeedCaps: el tope que manda, el de los no predichos y la velocidad. */
	void RefreshSpeedCaps();

	/** Límites de salto y escalas de gravedad por quien los pone, y los valores de base guardados al poner el primero. */
	TMap<FName, TNMovementLimits::FJumpLimit> JumpLimits;
	TMap<FName, float> GravityScaleOverrides;
	float BaseJumpZVelocity = 0.f;
	float BaseGravityScale = 1.f;

	/** Aplica al CharacterMovement el salto y la gravedad que mandan (la base si ya no hay límites). */
	void ApplyJumpLimits();
	void ApplyGravityScaleOverrides();

	/** Multiplicador ambiental (vadeo, etc.). 1.0 = sin efecto. Ver SetEnvironmentSpeedMultiplier. */
	float EnvironmentSpeedMultiplier = 1.0f;

	/** @brief OnRep: aplica MovementSpeed/visual al cambiar el estado de sprint. */
	UFUNCTION()
	void OnRep_IsSprinting();

	/**
	 * @brief OnRep (solo el dueño: se replica solo a él): la estamina llega del servidor, que es quien la gasta; vuelve a
	 *        decidir si esprinta con ella. El dueño no simula la estamina (TickComponent solo corre en el servidor), así que
	 *        sin esto bIsSprinting se quedaba en true con la tecla pulsada hasta soltarla aunque la estamina llegara a 0 (#834).
	 */
	UFUNCTION()
	void OnRep_CurrentStamina();

	/** @brief OnRep: feedback visual cuando el boost de stamina ilimitada cambia. */
	UFUNCTION()
	void OnRep_UnlimitedStamina();

	/** @brief OnRep (los que no son el dueño): CurrentStamina aproximada a partir de StaminaShared. */
	UFUNCTION()
	void OnRep_StaminaShared();

	/** @brief Servidor: StaminaShared al día con CurrentStamina (solo cambia si cambia el byte). */
	void SyncStaminaShared();

	/** @brief Decrementa el timer del boost; al llegar a 0 inicia la penalización post-boost. */
	void TickUnlimitedTimer(float DeltaTime);

	/** @brief Calcula drenaje/recarga según estado y aplica a CurrentStamina. */
	void TickStamina(float DeltaTime);

	/** @brief Resuelve si bIsSprinting debe estar activo en función de bSprintRequested + stamina. */
	void RecomputeSprintState();

	/**
	 * @brief Actualiza CharacterMovement->MaxWalkSpeed respetando WalkSpeed/SprintSpeed/SpeedCap.
	 * @note La inclinación del mesh al sprintar se eliminó; el feedback visual de sprint
	 *       viene únicamente del aumento de amplitud de las piernas.
	 */
	void ApplyMovementSpeed() const;
};
