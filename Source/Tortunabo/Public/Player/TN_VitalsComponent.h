#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/TN_DeathCause.h"
#include "Player/TN_VitalsRules.h"
#include "TN_VitalsComponent.generated.h"

class ATortugaCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTNOnVitalValueChanged, float, NewValue, float, OldValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTNOnPoisonChanged, bool, bPoisoned, float, DamagePerSecond);

/** Veneno replicado: daño por segundo (0 = sin veneno) y hora del servidor a la que se acaba. */
USTRUCT()
struct FTNPoisonNet
{
	GENERATED_BODY()

	UPROPERTY()
	float DamagePerSecond = 0.f;

	UPROPERTY()
	float EndsAtServerTime = 0.f;
};

/**
 * @brief Vitales de la tortuga (#855, plan maestro del modo único §1, decisión 9): vida, veneno e hidratación, además de la
 *        estamina (UTN_StaminaComponent). Las cuentas están en Player/TN_VitalsRules.h.
 *
 * Autoridad: solo el servidor cambia los vitales (las llamadas en un cliente no hacen nada y devuelven 0). Avanza el veneno y
 * la hidratación cuatro veces por segundo. Con la vida a cero pide la muerte por el flujo de siempre
 * (ATortugaCharacter::RequestKillBy → ATN_RunGameMode::MarkPlayerDeadBy): el tótem la cancela (y llena los vitales) y al
 * revivir se llenan otra vez (ATN_RunGameMode).
 *
 * Red: vida, hidratación y veneno se replican a todos (los compañeros también podrán leer el estado). Los delegados
 * On*Changed saltan en el servidor al cambiar y en cada cliente al llegar el valor, para el HUD, los efectos o la lectura del
 * estado sin barras. Este componente no pinta nada.
 *
 * Depuración: TN.Vitals.Damage, TN.Vitals.Poison, TN.Vitals.Heal, TN.Vitals.Hydrate y TN.Vitals.Dump (Docs/Comandos_Prueba.md).
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TORTUNABO_API UTN_VitalsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_VitalsComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** El componente de vitales del actor, o nulo. */
	static UTN_VitalsComponent* FindOn(const AActor* Actor);

	/**
	 * @brief Servidor: quita Amount de vida. A cero, pide la muerte con Cause (sin causa, la de quién la causa:
	 *        TNDeathCause::FromInstigator).
	 * @return La vida quitada de verdad (0 en un cliente, con la tortuga muerta o con la inmunidad de recién revivida).
	 */
	UFUNCTION(BlueprintCallable, Category = "Vitals")
	float ApplyDamage(float Amount, AActor* DamageCauser = nullptr, ETNDeathCause Cause = ETNDeathCause::Unknown);

	/** @brief Servidor: suma vida hasta el máximo. @return La vida sumada de verdad. */
	UFUNCTION(BlueprintCallable, Category = "Vitals")
	float Heal(float Amount);

	/**
	 * @brief Servidor: envenena DamagePerSecond durante Seconds. Con veneno ya puesto, manda el daño por segundo mayor y la
	 *        duración más larga. Si mata, la causa es el veneno.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vitals")
	void ApplyPoison(float DamagePerSecond, float Seconds);

	/** @brief Servidor: quita el veneno. */
	UFUNCTION(BlueprintCallable, Category = "Vitals")
	void CurePoison();

	/** @brief Servidor: suma hidratación hasta el máximo. @return La hidratación sumada de verdad. */
	UFUNCTION(BlueprintCallable, Category = "Vitals")
	float Hydrate(float Amount);

	/** @brief Servidor: vida e hidratación al máximo y sin veneno (al revivir o al salvarla el tótem). */
	UFUNCTION(BlueprintCallable, Category = "Vitals")
	void RestoreAll();

	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetMaxHealth() const { return MaxHealth; }

	/** Vida entre 0 y 1. */
	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetHealthFraction() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }

	UFUNCTION(BlueprintPure, Category = "Vitals")
	bool IsPoisoned() const { return Poison.DamagePerSecond > 0.f; }

	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetPoisonDamagePerSecond() const { return Poison.DamagePerSecond; }

	/** Segundos de veneno que quedan (en los clientes, con la hora del servidor). */
	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetPoisonSecondsLeft() const;

	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetHydration() const { return Hydration; }

	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetMaxHydration() const { return MaxHydration; }

	/** Hidratación entre 0 y 1. */
	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetHydrationFraction() const { return MaxHydration > 0.f ? Hydration / MaxHydration : 0.f; }

	/** Sin agua: la vida se desgasta. */
	UFUNCTION(BlueprintPure, Category = "Vitals")
	bool IsDehydrated() const { return Hydration <= 0.f; }

	/** Vida a cero: ya ha pedido la muerte (hasta que se llenen los vitales). */
	UFUNCTION(BlueprintPure, Category = "Vitals")
	bool IsDepleted() const { return Health <= 0.f; }

	/** Causa con la que pidió la muerte la última vez que la vida llegó a cero (Unknown si nunca). */
	ETNDeathCause GetDepletionCause() const { return DepletionCause; }

	/** Avanza los vitales DeltaSeconds como el tick del servidor (para las pruebas y la depuración). Solo en el servidor. */
	void AdvanceVitals(float DeltaSeconds);

	/** Vida: (nueva, anterior). En el servidor y en cada cliente. */
	UPROPERTY(BlueprintAssignable, Category = "Vitals")
	FTNOnVitalValueChanged OnHealthChanged;

	/** Veneno: (envenenada, daño por segundo). En el servidor y en cada cliente. */
	UPROPERTY(BlueprintAssignable, Category = "Vitals")
	FTNOnPoisonChanged OnPoisonChanged;

	/** Hidratación: (nueva, anterior). En el servidor y en cada cliente. */
	UPROPERTY(BlueprintAssignable, Category = "Vitals")
	FTNOnVitalValueChanged OnHydrationChanged;

protected:
	/** Vida máxima (el Excel da los daños sobre 100). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vitals", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.f;

	/** Hidratación máxima. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vitals|Hydration", meta = (ClampMin = "1.0"))
	float MaxHydration = 100.f;

	/** Lo que baja la hidratación por segundo (supuesto de #855: de llena a vacía en 250 s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vitals|Hydration", meta = (ClampMin = "0.0"))
	float HydrationDrainPerSecond = 0.4f;

	/** Vida por segundo que quita la hidratación a cero (supuesto de #855). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vitals|Hydration", meta = (ClampMin = "0.0"))
	float DehydratedHealthDrainPerSecond = 1.f;

	/** Cada cuánto avanza el servidor el veneno y la hidratación (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vitals", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float ServerStepSeconds = 0.25f;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Health)
	float Health = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_Hydration)
	float Hydration = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_Poison)
	FTNPoisonNet Poison;

	/** Servidor: segundos de veneno que quedan (los clientes los sacan de Poison.EndsAtServerTime). */
	float PoisonSecondsLeft = 0.f;

	ETNDeathCause DepletionCause = ETNDeathCause::Unknown;

	UFUNCTION()
	void OnRep_Health(float OldHealth);

	UFUNCTION()
	void OnRep_Hydration(float OldHydration);

	UFUNCTION()
	void OnRep_Poison(const FTNPoisonNet& OldPoison);

	bool IsServer() const;

	/** Servidor: la tortuga puede recibir daño (viva y sin la inmunidad de recién revivida). */
	bool CanBeHurt() const;

	TNVitals::FParams MakeParams() const;
	TNVitals::FState MakeState() const;

	/** Servidor: aplica un estado nuevo, avisa a quien escucha y, si la vida acaba de llegar a cero, pide la muerte. */
	void CommitState(const TNVitals::FState& NewState, ETNDeathCause CauseIfDepleted);

	float ServerTimeSeconds() const;
};
