#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Components/SynthComponent.h"
#include "TN_DizzyBirdsComponent.generated.h"

class UStaticMeshComponent;

namespace TNDizzyAudio
{
	// Parámetros atómicos compartidos con el generador de audio (definidos en TN_DizzyBirdsComponent.cpp).
	struct FDizzySharedParams;
}

/**
 * Sonido de dibujos animados del mareo, sintetizado en tiempo real (sin archivos de audio): pajaritos que pían a
 * ratos (trinos cortos que suben o bajan, a veces dobles) y unas «cuerdas mareadas» de fondo (dos violines a una
 * tercera con un glissando que sube y baja en círculo y vibrato). Mono y espacializado: suena en la cabeza de la
 * tortuga noqueada. El generador corre en el hilo de audio; el componente solo escribe si suena o no.
 */
UCLASS(ClassGroup = (Audio))
class TORTUNABO_API UTN_DizzySynthComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UTN_DizzySynthComponent(const FObjectInitializer& ObjectInitializer);

	/** Entra (con fundido) o se apaga (con cola) el sonido del mareo. */
	void SetDizzySound(bool bInActive);

	/** Volumen general (0-1,5). */
	void SetDizzyVolume(float InVolume);

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

private:
	TSharedPtr<TNDizzyAudio::FDizzySharedParams, ESPMode::ThreadSafe> SharedParams;
};

/**
 * Pajaritos del mareo: tres pájaros low-poly y unas estrellitas que dan vueltas sobre la cabeza de la tortuga mientras
 * está noqueada, con su sonido (UTN_DizzySynthComponent). Todo es local y cosmético: el personaje lo enciende y lo
 * apaga en todas las máquinas con el estado de derribo replicado (SetDizzy).
 *
 * Sigue el hueso de la cabeza de la malla cada fotograma (también con el ragdoll) con posición, giro y escala
 * absolutos, así que no hereda la escala 2,5 de la malla. Las mallas se construyen una vez en ejecución y se comparten.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_DizzyBirdsComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UTN_DizzyBirdsComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnUnregister() override;

	/** Enciende o apaga los pajaritos (entran y salen con un pequeño fundido de escala). */
	UFUNCTION(BlueprintCallable, Category = "Dizzy")
	void SetDizzy(bool bInDizzy);

	UFUNCTION(BlueprintPure, Category = "Dizzy")
	bool IsDizzy() const { return bDizzy; }

	/** Hueso sobre el que dan vueltas (se prueba también HeadTop_End). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dizzy")
	FName HeadBone = TEXT("Head");

	/** Radio del corro de pájaros (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dizzy", meta = (ClampMin = "5.0"))
	float OrbitRadius = 36.f;

	/** Altura del corro sobre la cabeza (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dizzy")
	float OrbitHeight = 44.f;

	/** Vueltas por segundo de los pájaros (las estrellas van al revés y más rápido). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dizzy", meta = (ClampMin = "0.05"))
	float TurnsPerSecond = 0.75f;

	/** Volumen del sonido del mareo (0-1,5). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dizzy", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float SoundVolume = 0.75f;

	/** Segundos que sigue en marcha el sintetizador al acabar el mareo (su cola); luego se para y suelta la voz (#737). */
	static constexpr float SoundTailSeconds = 1.5f;

	/** El sintetizador del mareo está en marcha (ocupa una voz del mezclador). */
	bool IsSoundPlaying() const;

private:
	void EnsureVisuals();
	/** Crea el sintetizador la primera vez y lo arranca si estaba parado. */
	void EnsureSound();
	FVector HeadTop() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Birds;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Stars;

	UPROPERTY(Transient)
	TObjectPtr<UTN_DizzySynthComponent> Synth;

	bool bDizzy = false;
	/** 0 = oculto, 1 = del todo (escala de entrada y salida). */
	float Presence = 0.f;
	float Time = 0.f;
	/** Cola que le queda al sonido tras acabar el mareo (s). */
	float SoundTailLeft = 0.f;
};
