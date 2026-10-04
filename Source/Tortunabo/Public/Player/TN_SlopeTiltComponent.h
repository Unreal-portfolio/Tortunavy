#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/TN_SlopeTiltDecisions.h"
#include "TN_SlopeTiltComponent.generated.h"

class ACharacter;
class USceneComponent;

/**
 * Inclina la malla de la tortuga con la pendiente (#586) solo de tripa en el suelo: al deslizarse en el panzazo o caer de
 * tripa en una cuesta, el modelo cabecea y alabea hasta quedar paralelo al suelo (con tope). Andando y corriendo va recta,
 * como una persona (decisión del director, 04-10); en llano, en el aire, en la bola, llevada, derribada o en ragdoll
 * vuelve a su orientación normal. Solo visual: la cápsula y el movimiento no cambian y no se
 * replica nada; cada máquina lo calcula con el suelo que ve (el del movimiento en el dueño y el servidor; en los
 * proxies simulados, una traza corta hacia abajo como mucho por fotograma).
 *
 * La inclinación se aplica como giro adicional, en los ejes de la cápsula, sobre el giro relativo de la malla que haya
 * dejado cualquier otro sistema (suavizado de red del CMC, panzazo, derribo, emotes): si otro sistema ha escrito el
 * giro desde el último fotograma, ese valor pasa a ser la base. Hace Tick después del actor y del movimiento.
 */
UCLASS(ClassGroup = (Tortunabo), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_SlopeTiltComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_SlopeTiltComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Inclinación total máxima (grados) respecto a la vertical. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slope Tilt", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float MaxTiltDeg = 35.f;

	/** Por debajo de esta pendiente (grados) se considera llano y no se inclina (evita temblar en suelo irregular). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slope Tilt", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float MinTiltDeg = 2.f;

	/** Velocidad de la interpolación exponencial hacia la inclinación del suelo (1/s; más alto, más brusco). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slope Tilt", meta = (ClampMin = "0.5", ClampMax = "40.0"))
	float TiltInterpSpeed = TNSlopeTilt::DefaultInterpSpeed;

	/**
	 * Giro máximo de la inclinación (grados/s): evita el tirón del primer fotograma al aterrizar en una cuesta (p. ej. del
	 * panzazo), que con solo la interpolación exponencial depende de la pendiente y de los fps.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slope Tilt", meta = (ClampMin = "10.0", ClampMax = "720.0"))
	float MaxTiltRateDegPerSec = TNSlopeTilt::DefaultMaxRateDegPerSec;

	/** Proxies simulados: distancia (cm) bajo la base de la cápsula que mira la traza del suelo. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slope Tilt", meta = (ClampMin = "5.0"))
	float ProxyTraceDistance = 40.f;

	/** Inclinación aplicada ahora (Pitch y Roll en grados, ejes de la cápsula). */
	UFUNCTION(BlueprintPure, Category = "Slope Tilt")
	FRotator GetCurrentTilt() const { return Driver.CurrentTilt; }

	/** Inclinación hacia la que va (la del suelo, o cero si no toca inclinarse). */
	UFUNCTION(BlueprintPure, Category = "Slope Tilt")
	FRotator GetTargetTilt() const { return Driver.TargetTilt; }

private:
	/** Normal del suelo que ve esta máquina; false si no hay suelo andable debajo. */
	bool ReadFloorNormal(const ACharacter& Character, FVector& OutNormal);

	/** Traza corta hacia abajo (proxies simulados), cacheada mientras la tortuga no se mueve. */
	bool TraceFloorNormal(const ACharacter& Character, FVector& OutNormal);

	/** Estado de la tortuga que decide si se inclina (suelo, bola, llevada, derribada, muerta, en el huevo). */
	TNSlopeTilt::FTiltGate ReadGate(const ACharacter& Character) const;

	/** Quita la inclinación al momento: devuelve la malla a su base si nadie la ha cambiado desde la última vez. */
	void DropTilt(USceneComponent& Visual);

	/** Inclinación aplicada, su base y la que se retoma tras una foto (lógica pura, probada en Tortunabo.Player.SlopeTilt.*). */
	TNSlopeTilt::FTiltDriver Driver;

	/** Caché de la traza de los proxies simulados. */
	FVector LastTraceLocation = FVector(UE_BIG_NUMBER);
	FVector CachedTraceNormal = FVector::UpVector;
	bool bCachedTraceHit = false;
	float TraceAge = 0.f;
};
