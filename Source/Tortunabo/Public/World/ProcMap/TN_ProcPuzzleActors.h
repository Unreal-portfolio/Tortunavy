#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/TN_DirectInteractableBase.h"
#include "TN_ProcPuzzleActors.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class USoundBase;

/**
 * Muro de lanzamiento (2vs2): un bloque de roca demasiado alto para saltar
 * (4,8 m; la tortuga sube 1,5 m). Un compañero lanza al otro arriba; el de arriba
 * pulsa el interruptor y baja la rampa para que suba el que lanzó. La rampa se
 * vuelve a levantar sola, así que la otra pareja tiene que hacer su propio puzle.
 *
 * Espacio local: +X = sentido de avance del camino. El bloque ocupa X ∈ [-L/2, L/2].
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_ProcThrowWall : public AActor
{
	GENERATED_BODY()

public:
	ATN_ProcThrowWall();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Servidor: dimensiones antes de replicar. */
	void Setup(float InWidth, float InHeight, float InLength);

	/** Servidor: baja la rampa durante Duration segundos. */
	void LowerRamp(float Duration);

	/** Punto (mundo) donde colocar el interruptor: arriba, junto al borde trasero. */
	FVector GetSwitchLocation() const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ThrowWall")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ThrowWall")
	TObjectPtr<UStaticMeshComponent> Block;

	/** Bisagra de la rampa en el borde superior delantero. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ThrowWall")
	TObjectPtr<USceneComponent> RampHinge;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ThrowWall")
	TObjectPtr<UStaticMeshComponent> Ramp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThrowWall")
	TObjectPtr<USoundBase> RampSound;

	/** Longitud horizontal que cubre la rampa bajada (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ThrowWall", meta = (ClampMin = "100.0"))
	float RampRun = 750.f;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Dimensions)
	FVector Dimensions = FVector(1200.f, 1000.f, 480.f);

	UPROPERTY(ReplicatedUsing = OnRep_RampDown)
	bool bRampDown = false;

	UFUNCTION()
	void OnRep_Dimensions();

	UFUNCTION()
	void OnRep_RampDown();

	void RaiseRamp();
	void ApplyDimensions();

	float RampAlpha = 0.f;
	FTimerHandle RampTimer;
};

/**
 * Compuerta de sabotaje: normalmente enterrada; cuando la pareja del otro carril
 * pulsa su interruptor, sale del suelo y corta el paso unos segundos.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_ProcSabotageGate : public AActor
{
	GENERATED_BODY()

public:
	ATN_ProcSabotageGate();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void Setup(float InWidth, float InHeight);

	/** Servidor: levanta la compuerta durante Duration segundos. */
	void Raise(float Duration);

	/** Servidor: levantada (corta el paso) o enterrada sin plazo. El atajo de Supervivencia la abre con sus placas (#517). */
	void SetBlocking(bool bBlock);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gate")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gate")
	TObjectPtr<UStaticMeshComponent> Gate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate")
	TObjectPtr<USoundBase> RaiseSound;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Size)
	FVector2D Size = FVector2D(1000.f, 350.f);

	UPROPERTY(ReplicatedUsing = OnRep_Raised)
	bool bRaised = false;

	UFUNCTION()
	void OnRep_Size();

	UFUNCTION()
	void OnRep_Raised();

	void Lower();

	float RaiseAlpha = 0.f;
	FTimerHandle GateTimer;
};

/**
 * Interruptor del mapa procedural. Según su objetivo: baja la rampa de un muro de
 * lanzamiento o levanta la compuerta de sabotaje del otro carril.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_ProcSwitch : public ATN_DirectInteractableBase
{
	GENERATED_BODY()

public:
	ATN_ProcSwitch();

	/** Servidor: a quién afecta y cuánto dura el efecto. */
	void SetTarget(AActor* InTarget, float InEffectSeconds);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void OnInteracted_Implementation(APawn* Interactor) override;

	/** Color según el objetivo (0 sin objetivo, 1 muro de lanzamiento: verde, 2 compuerta de sabotaje: rojo), replicado. */
	UPROPERTY(ReplicatedUsing = OnRep_TintKind)
	uint8 TintKind = 0;

	UFUNCTION()
	void OnRep_TintKind();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Switch", meta = (ClampMin = "0.5"))
	float EffectSeconds = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Switch")
	TObjectPtr<USoundBase> PressSound;

private:
	TWeakObjectPtr<AActor> Target;
};
