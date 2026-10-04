#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/TN_FinishLineVolume.h"
#include "TN_ProcTraversalActors.generated.h"

class UBoxComponent;
class UCapsuleComponent;
class UStaticMeshComponent;
class UNiagaraComponent;
class USoundBase;
class ACharacter;
class UTN_AmbientSynthComponent;

/**
 * Géiser: lanza a quien lo pisa hasta un punto de aterrizaje (cima de torre
 * colosal o borde del escalón). Un sentido: no hay forma de volver a bajar sin
 * caerse. Actor LOCAL en todas las máquinas: el servidor y el cliente dueño
 * aplican el mismo impulso balístico y la predicción de movimiento cuadra.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_ProcGeyser : public AActor
{
	GENERATED_BODY()

public:
	ATN_ProcGeyser();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Punto de aterrizaje en mundo (suelo). */
	void SetTarget(const FVector& InTarget) { Target = InTarget; }

	/**
	 * Géiser dentro de una torre hueca: lanza en vertical hasta el centro del hueco del forjado (InHole, en mundo, a la
	 * cota de su cara de arriba) y, ya por encima, empuja hacia Target para caer en él. La columna de agua crece hasta
	 * asomar por el hueco.
	 */
	void SetShaft(const FVector& InHole)
	{
		Hole = InHole;
		bShaft = true;
		JetHigh = FMath::Max(JetHigh, static_cast<float>(InHole.Z - GetActorLocation().Z) + 350.f);
	}

	/** Punto de aterrizaje, si es el de una torre hueca y altura extra de la parábola (los karts se lanzan solos, #293). */
	const FVector& GetTarget() const { return Target; }
	bool IsShaft() const { return bShaft; }
	float GetApexExtra() const { return ApexExtra; }

	/** Estallido del chorro al lanzar (gotas, espuma, bruma y sonido), en esta máquina. Lo usan los karts al subir. */
	void PlayLaunchBurst();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geyser")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geyser")
	TObjectPtr<UCapsuleComponent> Trigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geyser")
	TObjectPtr<UStaticMeshComponent> BaseMesh;

	/** Columna de agua: sube de golpe, se sostiene, baja y borbotea abajo, en bucle. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geyser")
	TObjectPtr<UStaticMeshComponent> ColumnMesh;

	/** Corona de espuma que va en lo alto de la columna. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geyser")
	TObjectPtr<UStaticMeshComponent> FoamCapMesh;

	/** Anillo de espuma alrededor de la boca. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geyser")
	TObjectPtr<UStaticMeshComponent> FoamBaseMesh;

	/** Altura de la columna en reposo y en el chorro (cm); en una torre hueca, la segunda llega a asomar por el hueco. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geyser", meta = (ClampMin = "50.0"))
	float JetLow = 260.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geyser", meta = (ClampMin = "100.0"))
	float JetHigh = 1050.f;

	/** Segundos de un ciclo del chorro (subida, chorro sostenido, bajada y borboteo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geyser", meta = (ClampMin = "1.0"))
	float CycleSeconds = 4.2f;

	/** VFX opcional (asignar un sistema Niagara en el BP hijo). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geyser")
	TObjectPtr<UNiagaraComponent> SprayVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geyser")
	TObjectPtr<USoundBase> LaunchSound;

	/** Altura extra de la parábola sobre el punto más alto (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geyser", meta = (ClampMin = "0.0"))
	float ApexExtra = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geyser")
	FVector Target = FVector::ZeroVector;

private:
	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void Launch(ACharacter* Character);

	TMap<TWeakObjectPtr<ACharacter>, double> LastLaunchTime;
	float PulseTime = 0.f;

	bool bShaft = false;
	FVector Hole = FVector::ZeroVector;
	/** Lanzados por el tiro que aún no han pasado el forjado (y cuándo se lanzaron). */
	TMap<TWeakObjectPtr<ACharacter>, double> InShaft;

	/** Borboteo y chorro sintetizados (TN_AmbientSynthComponent), al ritmo del chorro. */
	UPROPERTY(Transient)
	TObjectPtr<UTN_AmbientSynthComponent> WaterSound;
};

/**
 * Tobogán-cascada: la bajada es terreno con pendiente no caminable; este actor
 * añade el empuje ladera abajo y marca al personaje como inmune a la caída hasta
 * que aterriza. LOCAL en todas las máquinas (mismo motivo que el géiser).
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_ProcSlideZone : public AActor
{
	GENERATED_BODY()

public:
	ATN_ProcSlideZone();

	virtual void Tick(float DeltaTime) override;

	/** Crea los volúmenes a lo largo de la bajada (puntos en mundo, de arriba abajo). */
	/**
	 * Cajas de la bajada por los puntos del camino (mundo) y efectos: en el labio, espuma y gotas por donde el agua se
	 * asoma; en la poza (PoolCenter a la cota del agua, radio PoolRadius, Impact donde cae el agua), salpicaduras,
	 * espuma, bruma y ondas que se abren desde el impacto.
	 */
	void InitFromPoints(const TArray<FVector>& Points, float Width, const FVector& PoolCenter, float PoolRadius, const FVector& Impact);

	/** Dirección de la bajada en Point si está sobre el tobogán (karts, #293). */
	bool FindFlowAt(const FVector& Point, FVector& OutFlow) const;

	/** La poza del pie de la cascada: centro a la cota del agua y radio (cm). False si no tiene. */
	bool GetPool(FVector& OutCenter, float& OutRadius) const;

	/** Lo alto de la cascada (el labio, en el suelo) y hacia dónde baja. False si no tiene tramos. */
	bool GetTop(FVector& OutTop, FVector& OutFlow) const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Slide")
	TObjectPtr<USceneComponent> Root;

	/** Aceleración extra ladera abajo (cm/s²). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = "0.0"))
	float BoostAcceleration = 700.f;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoxComponent>> Segments;

	TArray<FVector> SegmentDirs;
	FVector PoolCenterWorld = FVector::ZeroVector;
	float PoolRadiusCm = 0.f;

	UFUNCTION()
	void OnSegmentOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};

/**
 * Volumen que mata al entrar (fondo de zanjas, lava). LOCAL en todas las
 * máquinas; solo el servidor ejecuta la muerte.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_ProcKillVolume : public AActor
{
	GENERATED_BODY()

public:
	ATN_ProcKillVolume();

	virtual void BeginPlay() override;

	void SetExtent(const FVector& Extent);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Kill")
	TObjectPtr<UBoxComponent> Box;

private:
	UFUNCTION()
	void OnBoxOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};

/** Meta del mapa procedural: la línea de meta de siempre con tamaño ajustable. */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_ProcFinishVolume : public ATN_FinishLineVolume
{
	GENERATED_BODY()

public:
	void SetExtent(const FVector& Extent);
};
