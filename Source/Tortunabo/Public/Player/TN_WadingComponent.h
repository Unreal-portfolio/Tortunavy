#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TN_WadingComponent.generated.h"

class UTN_StaminaComponent;
class UCharacterMovementComponent;
class UNiagaraSystem;
class USoundBase;

/**
 * @brief Vadeo simple (no nado). Ralentiza y amortigua el salto mientras el
 * personaje anda por zonas cubiertas de agua poco profunda.
 *
 * En BeginPlay busca un actor con el tag WaterActorTag ("TN_Water" por
 * defecto) y fija su Z como cota del agua; si no existe, el componente se
 * apaga (tick desactivado, sin coste). Profundidad = cota del agua − pies de
 * la cápsula. Por encima de MinDepth y con el personaje andando (no cayendo)
 * aplica un multiplicador de velocidad vía UTN_StaminaComponent::
 * SetEnvironmentSpeedMultiplier (no pisa el cap de TN_SlowZoneVolume ni el
 * sprint) y reduce el impulso de salto.
 *
 * La parte de movimiento corre en el servidor y en el cliente que controla
 * el pawn (igual que TN_SlowZoneVolume): cada máquina calcula su propia
 * profundidad, sin replicación nueva. Las salpicaduras corren en cualquier
 * máquina con render (todas menos el servidor dedicado), también para pawns
 * simulados, para que se vean en todos los clientes.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TORTUNABO_API UTN_WadingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_WadingComponent();

	/** @brief Busca el actor de agua por tag y fija la cota; se desactiva si no hay agua. */
	virtual void BeginPlay() override;

	/** @brief A 10 Hz: profundidad, multiplicador de velocidad/salto y salpicaduras. */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** True mientras el personaje está sobre agua a más de MinDepth y andando. */
	UFUNCTION(BlueprintPure, Category = "Wading")
	bool IsInWater() const { return bIsInWater; }

	/** Profundidad actual (cm). 0 (o negativo) si no hay agua bajo los pies. */
	UFUNCTION(BlueprintPure, Category = "Wading")
	float GetCurrentDepth() const { return CurrentDepth; }

	/**
	 * Multiplicador de velocidad con los pies a FeetZ, andando o no. Lo pide el movimiento en cada paso
	 * (UTN_TurtleMovementComponent), así el cliente y el servidor usan el de la misma posición: el del Tick de este componente
	 * (10 Hz, en cada máquina a su hora) cambiaba a destiempo y daba correcciones al andar por la orilla.
	 */
	float GetSpeedMultiplierAt(double FeetZ, bool bOnGround) const;

protected:
	/** Tag de actor que marca el plano/volumen de agua a buscar en BeginPlay. */
	UPROPERTY(EditDefaultsOnly, Category = "Wading")
	FName WaterActorTag = TEXT("TN_Water");

	/** Profundidad mínima (cm) a partir de la cual empieza a notarse el vadeo. */
	UPROPERTY(EditDefaultsOnly, Category = "Wading", meta = (ClampMin = "0.0"))
	float MinDepth = 10.f;

	/** Profundidad (cm) a la que el multiplicador de velocidad satura en WadeSpeedMultiplier. */
	UPROPERTY(EditDefaultsOnly, Category = "Wading", meta = (ClampMin = "1.0"))
	float FullDepth = 90.f;

	/** Multiplicador de velocidad en profundidad máxima (FullDepth). 1 = sin efecto. */
	UPROPERTY(EditDefaultsOnly, Category = "Wading", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float WadeSpeedMultiplier = 0.55f;

	/** Multiplicador del impulso de salto (JumpZVelocity) mientras se está en el agua. */
	UPROPERTY(EditDefaultsOnly, Category = "Wading", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float JumpInWaterMultiplier = 0.8f;

	/** Velocidad horizontal (cm/s) mínima para generar salpicaduras de paso. */
	UPROPERTY(EditDefaultsOnly, Category = "Wading", meta = (ClampMin = "0.0"))
	float SplashSpeedThreshold = 100.f;

	/** Intervalo mínimo (s) entre salpicaduras de paso. */
	UPROPERTY(EditDefaultsOnly, Category = "Wading", meta = (ClampMin = "0.05"))
	float SplashInterval = 0.35f;

	/** FX de salpicadura, lanzado en la superficie del agua bajo el personaje. */
	UPROPERTY(EditDefaultsOnly, Category = "Wading")
	TObjectPtr<UNiagaraSystem> WadeSplashFX;

	/** Sonido de salpicadura, acompaña a WadeSplashFX. */
	UPROPERTY(EditDefaultsOnly, Category = "Wading")
	TObjectPtr<USoundBase> WadeStepSound;

private:
	/** Cota Z del agua encontrada en BeginPlay. Solo válida si bHasWater. */
	float WaterZ = 0.f;
	bool bHasWater = false;

	bool bIsInWater = false;
	float CurrentDepth = 0.f;
	float TimeSinceLastSplash = 0.f;

	/** Referencia al componente de stamina del propietario — aplica el multiplicador ahí. */
	TWeakObjectPtr<UTN_StaminaComponent> StaminaComponentRef;

	/** Multiplicador de velocidad y límite de salto (UTN_StaminaComponent). Solo servidor o cliente dueño del pawn. */
	void ApplyMovementEffects(bool bWasInWater);

	/** Salpicadura de entrada y de paso. Cualquier máquina con render, también simuladas. */
	void UpdateSplashEffects(float DeltaTime, bool bWasInWater, const UCharacterMovementComponent* CMC);

	/** Lanza FX + sonido en la superficie del agua, bajo la posición actual del personaje. */
	void SpawnSplashEffect() const;
};
