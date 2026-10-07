#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "TN_BeachEnemySynth.generated.h"

namespace TNBeachSynthDSP
{
	// Cola de disparos y parámetros continuos compartidos con el generador de audio (Private/World/Beach/TN_BeachEnemySynth.cpp).
	struct FShared;
}

/** Efectos sintetizados de los enemigos y la tormenta de la playa (el orden es el del motor DSP). */
UENUM(BlueprintType)
enum class ETNBeachSfx : uint8
{
	Clack    UMETA(DisplayName = "Chasquido de pinza"),
	Slam     UMETA(DisplayName = "Mazazo en la arena"),
	Squawk   UMETA(DisplayName = "Graznido"),
	Splat    UMETA(DisplayName = "Cagada que cae"),
	Swoop    UMETA(DisplayName = "Picado"),
	Crunch   UMETA(DisplayName = "Palmeras que crujen"),
	Stomp    UMETA(DisplayName = "Pisotón gigante"),
};

/**
 * Sonidos de los enemigos de la playa sintetizados en tiempo real, sin archivos de audio (mismo patrón que
 * UTN_PlaygroundSynthComponent): golpes cortos (chasquidos de pinza, mazazo con arena, graznidos, cagada, picado,
 * palmeras que crujen y pisotones) y dos sonidos continuos con su nivel: el motor de un
 * quad gigante (SetEngine) y el viento de la tormenta (SetWind).
 *
 * El sonido lo genera un ISoundGenerator en el hilo de render de audio sin UObjects, asignaciones ni bloqueos; el hilo de
 * juego deja disparos en una cola circular sin bloqueos y escribe los niveles continuos en atómicos. Mono y espacializado
 * con la atenuación hecha en código. Solo suena cuando hace falta: arranca si el oyente está a su alcance y se para tras
 * unos segundos de silencio.
 */
UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_BeachEnemySynthComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UTN_BeachEnemySynthComponent(const FObjectInitializer& ObjectInitializer);

	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Crea el componente en InOwner enganchado a InParent (o a su raíz) y lo registra. Null en servidor dedicado, sin
	 * audio o fuera de un mundo de juego.
	 */
	static UTN_BeachEnemySynthComponent* AttachTo(AActor* InOwner, USceneComponent* InParent, float InInnerRadius, float InFalloff);

	/** Dispara un efecto. Pitch multiplica las frecuencias (1 = de serie) y Volume la ganancia (0..2). */
	void Play(ETNBeachSfx Sound, float Pitch = 1.f, float Volume = 1.f);

	/** Motor del quad: Level 0..1 (0 = callado) y revoluciones 0..1. Se suaviza en el hilo de audio. */
	void SetEngine(float Level, float Rpm);

	/** Viento de la tormenta: 0..1 (0 = callado). */
	void SetWind(float Level);

	/** Radio con volumen pleno (cm). Solo tiene efecto antes de registrar el componente. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beach|Audio", meta = (ClampMin = "0.0"))
	float InnerRadius = 1500.f;

	/** Distancia (cm) desde el radio interior en la que se apaga. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beach|Audio", meta = (ClampMin = "100.0"))
	float FalloffDistance = 12000.f;

	/** Volumen general de este componente. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beach|Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Loudness = 1.f;

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

private:
	void ConfigureSpatial();
	bool IsListenerNear() const;
	void EnsurePlaying();

	TSharedPtr<TNBeachSynthDSP::FShared, ESPMode::ThreadSafe> Shared;

	/** Segundos de silencio que quedan antes de parar el sintetizador. */
	float SilenceLeft = 0.f;
	float EngineLevel = 0.f;
	float WindLevel = 0.f;
};
