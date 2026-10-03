// Sonido continuo del buggy del Rally: motor en tres capas (ralentí, medio y alto) mezcladas por RPM y derrape en bucle
// según la deriva. Solo local y cosmético: cada máquina con audio lo calcula con el estado que ve (no se replica nada) y
// en un servidor dedicado no se crea. La mezcla es lógica pura en TNBuggyAudio, probada en Tortunabo.Rally.Audio.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TN_BuggyEngineAudioComponent.generated.h"

class ATN_Buggy;
class UAudioComponent;
class USoundBase;

namespace TNBuggyAudio
{
	/** Capas del motor: 0 = ralentí, 1 = medio, 2 = alto. */
	inline constexpr int32 EngineLayerCount = 3;

	struct FEngineLayerMix
	{
		float Volume[EngineLayerCount] = {};
		float Pitch[EngineLayerCount] = {};
	};

	/**
	 * Mezcla de potencia constante (la suma de los cuadrados de los volúmenes es 1): ralentí puro con Rpm01 = 0, medio
	 * puro con 0,5 y alto puro con 1. Cada capa sube de tono al pasar por encima de su centro y baja por debajo.
	 */
	TORTUNABO_API FEngineLayerMix MixEngineLayers(float Rpm01);

	/** RPM normalizada a partir de la velocidad cuando el motor de Chaos no informa (proxies sin simulación). */
	TORTUNABO_API float EstimateRpm01(float SpeedCms, float TopSpeedCms);

	/**
	 * Volumen del derrape (0..1): crece con la deriva entre MinSlipDeg y FullSlipDeg y con la velocidad hasta
	 * FullSpeedCms; nada en el aire ni casi parado.
	 */
	TORTUNABO_API float SkidVolume(float SlipDeg, float SpeedCms, bool bGrounded, float MinSlipDeg, float FullSlipDeg, float FullSpeedCms);
}

UCLASS(ClassGroup = (Rally), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_BuggyEngineAudioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_BuggyEngineAudioComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Bucles del motor: ralentí, medio y alto. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Sonido")
	TObjectPtr<USoundBase> EngineIdleSound;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Sonido")
	TObjectPtr<USoundBase> EngineMidSound;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Sonido")
	TObjectPtr<USoundBase> EngineHighSound;

	/** Bucle del derrape (arena despedida por las ruedas). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Sonido")
	TObjectPtr<USoundBase> SkidSound;

	/** Volumen máximo del motor y del derrape. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Sonido", meta = (ClampMin = "0"))
	float EngineVolume = 0.8f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Sonido", meta = (ClampMin = "0"))
	float SkidMaxVolume = 0.7f;

	/** Deriva (grados) a la que empieza a sonar el derrape y a la que suena entero. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Sonido", meta = (ClampMin = "0"))
	float SkidMinSlipDeg = 12.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Sonido", meta = (ClampMin = "1"))
	float SkidFullSlipDeg = 35.f;

	/** Velocidad (cm/s) a la que el derrape suena entero. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Sonido", meta = (ClampMin = "1"))
	float SkidFullSpeedCms = 1200.f;

	/** Suavizado de la RPM y del derrape (más alto, más rápido). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Sonido", meta = (ClampMin = "0.1"))
	float SmoothingSpeed = 8.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	ATN_Buggy* GetBuggy() const;
	UAudioComponent* CreateLoop(USoundBase* Sound, const TCHAR* Name);
	/** RPM del motor de Chaos en [0, 1]; si no informa, estimada por la velocidad. */
	float ReadRpm01() const;
	bool IsAnyWheelInContact() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> EngineLayers;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> SkidLoop;

	float SmoothedRpm01 = 0.f;
	float SmoothedSkid = 0.f;
};
