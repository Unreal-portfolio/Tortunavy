#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "TN_RaceItemSynth.generated.h"

namespace TNRaceItemDSP
{
	// Cola de disparos compartida con el generador de audio (Private/World/Beach/TN_RaceItemSynth.cpp).
	struct FRaceSfxQueue;
}

/**
 * Sonidos sintetizados de los objetos de carrera (el orden es el del motor DSP de TN_RaceItemSynth.cpp; si se añade uno,
 * al final y antes de Count). Todos son cortos, se pueden disparar a la vez y admiten Pitch (multiplica las frecuencias) y
 * Volume.
 */
UENUM(BlueprintType)
enum class ETNRaceSound : uint8
{
	/** Coger una caja de objetos: «¡pop!» con un arpegio brillante corto que sube. */
	BoxOpen     UMETA(DisplayName = "Abrir caja"),
	/** Coco turbo: barrido de ruido que sube y se abre («¡fiuuum!») con un golpe seco de arranque. */
	Turbo       UMETA(DisplayName = "Turbo"),
	/** Coco dorado: como el turbo, más largo y brillante, con campanillas doradas encima. */
	Golden      UMETA(DisplayName = "Turbo dorado"),
	/** Protector solar puesto: arpegio brillante y rápido que sube, con destello final. */
	StarUp      UMETA(DisplayName = "Protector puesto"),
	/** Se acaba el protector: el mismo arpegio, más lento y cayendo, con un «pof». */
	StarDown    UMETA(DisplayName = "Protector se acaba"),
	/** Silbato del sargento: silbido agudo con trino rápido, dos pitidos. */
	Whistle     UMETA(DisplayName = "Silbato"),
	/** Graznido de ave: gaviota con Pitch 1, pelícano (grave y más largo) con Pitch 0,55. */
	Squawk      UMETA(DisplayName = "Graznido"),
	/** Aleteo grande: golpe grave suave de aire (un batido). */
	Flap        UMETA(DisplayName = "Aleteo"),
	/** Pasitos rápidos de cangrejo: ráfaga de clics secos. */
	Scuttle     UMETA(DisplayName = "Pasitos"),
	/** Salida del cangrejo o del objeto lanzado: «¡boing!» de muelle. */
	Boing       UMETA(DisplayName = "Boing"),
	/** Golpe seco y hueco («¡bonk!») de un impacto. */
	Bonk        UMETA(DisplayName = "Bonk"),
	/** Pegote que cae en el suelo («¡plaf!»): golpe blando y húmedo. */
	Splat       UMETA(DisplayName = "Plaf"),
	/** Algo que cae: silbido descendente. */
	Fall        UMETA(DisplayName = "Cae"),
	/** Lanzar algo: soplido corto («¡fuff!»). */
	Throw       UMETA(DisplayName = "Lanzar"),
	/** Disco que gira: zumbido con vibrato, dura casi un segundo. */
	Whirr       UMETA(DisplayName = "Zumbido"),
	/** Atrapar el disco: «¡clap!» seco. */
	Catch       UMETA(DisplayName = "Atrapar"),
	/** Trueno: retumbo grave largo que rueda. */
	Rumble      UMETA(DisplayName = "Trueno"),
	/** Rayo: chasquido eléctrico muy agudo y corto, con crepitar. */
	Zap         UMETA(DisplayName = "Rayo"),
	/** No se puede usar ahora: dos notas graves que bajan, apagadas. */
	Nope        UMETA(DisplayName = "No se puede"),
	/** Aviso corto y agudo (una nube que se forma, una mina que se arma): pitido. */
	Beep        UMETA(DisplayName = "Pitido"),
	/** Pelícano taxi: cuando te suelta, «¡pop!» de despedida con un trino descendente alegre. */
	Land        UMETA(DisplayName = "Aterriza"),
	/** Tabla de surf: la ola que se levanta, rugido de agua que crece y rompe con espuma (#786). */
	Wave        UMETA(DisplayName = "Ola"),
	/** Cohete de feria: mecha que chisporrotea y arranque silbante que sube (#786). */
	Rocket      UMETA(DisplayName = "Cohete"),
	/** Caña de pescar: carraca del carrete muy rápida con el zumbido del sedal (#786). */
	Reel        UMETA(DisplayName = "Carrete"),
	/** Remolino: gorgoteo grave de agua que da vueltas (#786). */
	Gurgle      UMETA(DisplayName = "Gorgoteo"),
	Count       UMETA(Hidden)
};

/**
 * Sintetizador de los objetos de carrera, sin archivos de audio. Mismo patrón que UTN_BeachMineSynthComponent: el sonido
 * lo genera un ISoundGenerator en el hilo de audio (C++ puro, sin UObjects, asignaciones ni bloqueos); el hilo de juego
 * solo deja disparos en una cola circular sin bloqueos. Mono y espacializado, con la atenuación hecha en código. Solo suena
 * cuando hace falta: Play arranca el sintetizador si el oyente está a su alcance y lo para tras unos segundos de silencio.
 */
UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_RaceItemSynthComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UTN_RaceItemSynthComponent(const FObjectInitializer& ObjectInitializer);

	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Dispara un efecto. Pitch multiplica las frecuencias (1 = de serie) y Volume la ganancia (0..2). */
	void Play(ETNRaceSound Sound, float Pitch = 1.f, float Volume = 1.f);

	/**
	 * Crea, coloca en InWorldLocation y registra el componente en InOwner (enganchado a su raíz). Devuelve null en servidor
	 * dedicado, sin audio o fuera de un mundo de juego.
	 */
	static UTN_RaceItemSynthComponent* AttachTo(AActor* InOwner, const FVector& InWorldLocation, float InInnerRadius = 1500.f, float InFalloff = 12000.f);

	/** Radio con volumen pleno (cm). Solo tiene efecto antes de registrar el componente. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Audio", meta = (ClampMin = "0.0"))
	float InnerRadius = 1500.f;

	/** Distancia (cm) desde el radio interior en la que se apaga. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Audio", meta = (ClampMin = "100.0"))
	float FalloffDistance = 12000.f;

	/** Volumen general de este componente. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
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
	TSharedPtr<TNRaceItemDSP::FRaceSfxQueue, ESPMode::ThreadSafe> SfxQueue;

	/** Segundos de silencio que quedan antes de parar el sintetizador. */
	float SilenceLeft = 0.f;
};
