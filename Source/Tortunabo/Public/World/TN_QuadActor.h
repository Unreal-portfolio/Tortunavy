#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_QuadActor.generated.h"

class UStaticMeshComponent;
class UCapsuleComponent;
class ATortugaCharacter;

/**
 * @deprecated Usar ATN_BeachQuadLane (ETNBeachElement::QuadLane), el paso de quads de la playa (#353).
 *
 * Se queda compilando en Source/Tortunabo y no en Deprecado/ porque BP_QuadActor hereda de ella, y BP_QuadSpawner,
 * BP_Chunk_Easy_05, BP_Chunk_Hard_02, BP_Chunk_Medium_Personaliced y LVL_TestMap la usan: sin la clase esos assets no
 * cargan. Se mueve cuando se retiren los chunks (Docs/Limpieza-2026-09-29.md, 2.6). No crear instancias nuevas.
 *
 * Quad gigante que cruza el mapa matando con sus ruedas.
 *
 * QuadMesh: puramente visual, sin colisión con pawns.
 * WheelLeft / WheelRight: UCapsuleComponent orientadas horizontalmente.
 *   Cualquier TortugaCharacter que las toque muere (RequestKill).
 *
 * Movimiento: línea recta de SpawnLocation a EndLocation a velocidad constante.
 * Al llegar al destino se autodestruye.
 * Spawneado periódicamente por ATN_QuadSpawner.
 *
 * Red: bReplicateMovement=true, bAlwaysRelevant=true.
 *   El servidor mueve el actor; los clientes reciben posición replicada.
 *   El kill se aplica solo en servidor (HasAuthority guard en overlap).
 */
// NotPlaceable impide colocar instancias nuevas; las ya guardadas en mapas y chunks siguen cargando.
UCLASS(Blueprintable, NotPlaceable, meta = (DisplayName = "Quad Actor (DEPRECATED)"))
class TORTUNABO_API ATN_QuadActor : public AActor
{
	GENERATED_BODY()

public:
	ATN_QuadActor();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/**
	 * Inicializa el viaje del quad. Llamado por ATN_QuadSpawner tras spawnear.
	 * @param InEndLocation  Posición world donde el quad se autodestruye.
	 * @param InSpeed        Velocidad de viaje (cm/s).
	 */
	void InitializeTravel(const FVector& InEndLocation, float InSpeed);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Quad")
	TObjectPtr<UStaticMeshComponent> QuadMesh;

	/** Rueda izquierda — kill collider horizontal. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Quad")
	TObjectPtr<UCapsuleComponent> WheelLeft;

	/** Rueda derecha — kill collider horizontal. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Quad")
	TObjectPtr<UCapsuleComponent> WheelRight;

	/**
	 * Mesh visual de la rueda izquierda (cilindro por defecto).
	 * Hijo de WheelLeft: hereda posición, rotación y escala de la cápsula.
	 * Escala se ajusta automáticamente en BeginPlay para coincidir con
	 * WheelCapsuleRadius y WheelCapsuleHalfHeight.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Quad")
	TObjectPtr<UStaticMeshComponent> WheelLeftMesh;

	/** Mesh visual de la rueda derecha. Igual que WheelLeftMesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Quad")
	TObjectPtr<UStaticMeshComponent> WheelRightMesh;

	/** Velocidad por defecto (cm/s). Sobreescrita por InitializeTravel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quad",
		meta = (ClampMin = "100.0"))
	float TravelSpeed = 800.f;

	/**
	 * Offset lateral (Y local) de cada rueda desde el centro del quad (cm).
	 * Ajustar para que coincida con el mesh en el Blueprint hijo.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quad")
	float WheelLateralOffset = 280.f;

	/** Radio de la cápsula de rueda (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quad",
		meta = (ClampMin = "20.0"))
	float WheelCapsuleRadius = 90.f;

	/**
	 * Mitad del ancho de la rueda (cm). Con la nueva orientación (eje = axle Y),
	 * este valor controla el grosor del cilindro, no el largo de la huella.
	 * Default 55 = rueda de 110 cm de ancho — ajustar al mesh en el BP hijo.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quad",
		meta = (ClampMin = "20.0"))
	float WheelCapsuleHalfHeight = 55.f;

	/**
	 * Colores del quad de la playa (TNBeachMeshes::QuadPalette, 0-4) que se monta si QuadMesh no lleva una malla del
	 * proyecto. Se escala para que sus ruedas caigan sobre las de matar (WheelLateralOffset) y se apoya en el suelo de
	 * las cápsulas. Con una malla propia no se monta nada.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quad|Art", meta = (ClampMin = "0", ClampMax = "4"))
	int32 CodeArtPalette = 0;

private:
	FVector EndLocation;
	bool bTraveling = false;

	// ── Arte de código (#50; local en cada máquina) ───────────────────────────

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> CodeArtRoot;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> CodeArtWheels;

	TArray<FVector> CodeArtWheelPivots;
	FVector CodeArtLastLocation = FVector::ZeroVector;
	float CodeArtWheelRadius = 0.f;
	float CodeArtWheelAngle = 0.f;

	/** Monta el quad de la playa (cuerpo con piloto y cuatro ruedas) y oculta los marcadores del motor. */
	void BuildCodeArt();

	/** Las ruedas giran con lo que avanza el quad (también en los clientes, con el movimiento replicado). */
	void AnimateCodeArt();

	UFUNCTION()
	void OnWheelOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);
};

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @deprecated Va con ATN_QuadActor (ver arriba): lo sustituye ATN_BeachQuadLane, que programa sus propias pasadas (#353).
 *
 * Spawner que crea ATN_QuadActor periódicamente.
 *
 * Coloca este actor en el nivel donde el quad debe aparecer.
 * Ajusta EndLocation (en world space) al otro extremo de la pista.
 * Solo activo en servidor.
 */
UCLASS(Blueprintable, NotPlaceable, meta = (DisplayName = "Quad Spawner (DEPRECATED)"))
class TORTUNABO_API ATN_QuadSpawner : public AActor
{
	GENERATED_BODY()

public:
	ATN_QuadSpawner();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	/** Clase del quad a spawnear. Asignar BP_QuadActor en el Editor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner")
	TSubclassOf<ATN_QuadActor> QuadClass;

	/**
	 * Offset LOCAL desde el spawner (cm) hasta donde el quad se autodestruye.
	 * Ejemplo: (5000, 0, 0) = el quad recorre 5000 cm en X local desde el spawner.
	 * Usa el transform del spawner: si rotas el actor, el offset rota con él.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner")
	FVector EndOffsetLocal = FVector(5000.f, 0.f, 0.f);

	/** Velocidad del quad (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner",
		meta = (ClampMin = "100.0"))
	float QuadSpeed = 800.f;

	/** Tiempo entre spawns (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner",
		meta = (ClampMin = "1.0"))
	float SpawnInterval = 20.f;

	/** Delay antes del primer spawn (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner",
		meta = (ClampMin = "0.0"))
	float InitialDelay = 3.f;

private:
	FTimerHandle SpawnTimerHandle;

	void SpawnQuad();
};
