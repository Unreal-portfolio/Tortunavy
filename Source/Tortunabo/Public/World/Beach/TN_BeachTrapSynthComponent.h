#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "TN_BeachTrapSynthComponent.generated.h"

namespace TNBeachTrapDSP
{
	// Cola de disparos compartida con el generador de audio (Private/World/Beach/TN_BeachTrapSynthComponent.cpp).
	struct FTrapSfxQueue;
}

/** Efectos sintetizados de las trampas de la playa (el orden es el del motor DSP). */
UENUM(BlueprintType)
enum class ETNBeachTrapSound : uint8
{
	Squelch  UMETA(DisplayName = "Chof de algas"),
	Creak    UMETA(DisplayName = "Crujido de madera"),
	Crack    UMETA(DisplayName = "Tabla que se parte"),
	Thud     UMETA(DisplayName = "Golpe sordo en la arena"),
};

/**
 * Efectos de sonido sintetizados en tiempo real (sin archivos de audio) para las trampas de la playa: el chof de las
 * algas, el crujido y el chasquido de la tabla sobre el hoyo y el golpe sordo en la arena.
 *
 * Mismo patrón que UTN_PlaygroundSynthComponent: el sonido lo genera un ISoundGenerator en el hilo de render de audio, sin
 * UObjects, asignaciones ni bloqueos; el hilo de juego solo deja disparos en una cola circular sin bloqueos. Mono y
 * espacializado, con la atenuación hecha en código. Solo suena cuando hace falta: TriggerSound arranca el sintetizador si
 * el oyente está a su alcance y lo para tras unos segundos de silencio.
 */
UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_BeachTrapSynthComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UTN_BeachTrapSynthComponent(const FObjectInitializer& ObjectInitializer);

	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Dispara un efecto. Pitch multiplica las frecuencias (1 = tono de serie) y Volume la ganancia (0..2). No hace nada si
	 * el oyente más cercano está fuera de alcance.
	 */
	UFUNCTION(BlueprintCallable, Category = "Beach|Audio")
	void TriggerSound(ETNBeachTrapSound Sound, float Pitch = 1.f, float Volume = 1.f);

	/** Lleva la fuente a WorldAt y dispara el efecto (todas las voces de este componente suenan desde el último sitio). */
	void TriggerSoundAt(ETNBeachTrapSound Sound, const FVector& WorldAt, float Pitch = 1.f, float Volume = 1.f);

	/**
	 * Crea, coloca en InWorldLocation y registra el componente en InOwner (enganchado a su raíz). Devuelve null en servidor
	 * dedicado, sin audio o fuera de un mundo de juego.
	 */
	static UTN_BeachTrapSynthComponent* AttachTo(AActor* InOwner, const FVector& InWorldLocation, float InInnerRadius = 500.f,
		float InFalloff = 3000.f);

	/** Radio con volumen pleno (cm). Solo tiene efecto antes de registrar el componente. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beach|Audio", meta = (ClampMin = "0.0"))
	float InnerRadius = 500.f;

	/** Distancia (cm) desde el radio interior en la que se apaga. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beach|Audio", meta = (ClampMin = "100.0"))
	float FalloffDistance = 3000.f;

	/** Volumen general de este componente. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beach|Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Loudness = 1.f;

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

private:
	/** Mono, espacializado y con la atenuación en código (antes de arrancar). */
	void ConfigureSpatial();

	/** true si el oyente más cercano puede oír algo de este componente. */
	bool IsListenerNear() const;

	/** Cola de disparos compartida con el generador del hilo de audio (los dos la mantienen viva). */
	TSharedPtr<TNBeachTrapDSP::FTrapSfxQueue, ESPMode::ThreadSafe> SfxQueue;

	/** Segundos de silencio que quedan antes de parar el sintetizador. */
	float SilenceLeft = 0.f;
};
