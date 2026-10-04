#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/TN_BreakablePlatform.h"
#include "TN_ProcSurvivalTraps.generated.h"

class UDecalComponent;
class UMaterialInstanceDynamic;
class ATN_QuadActor;
class ATN_PressurePlate;
class ATN_ProcSabotageGate;

/**
 * Trampas de Supervivencia que dependen de la forma del terreno (#517). Las coloca el generador sobre los mapas del
 * catálogo (TNSurvivalCatalog::PlaceTerrainTraps); todas mueren con el mapa al pasar de nivel.
 */

/**
 * Puente que se rompe: un tablón de madera de labio a labio en lugar de la viga de un hueco que se cruza andando.
 * Es la plataforma del Clásico (ATN_BreakablePlatform) con su malla y su tamaño puestos desde código: el Blueprint de
 * la plataforma está en _Deprecado. Cruzándolo corriendo aguanta; andando se rompe y se cae a la zona de muerte del
 * hueco. Reaparece a los pocos segundos para los que vienen detrás.
 */
UCLASS()
class TORTUNABO_API ATN_ProcBreakableBridge : public ATN_BreakablePlatform
{
	GENERATED_BODY()

public:
	ATN_ProcBreakableBridge();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Servidor, antes de replicar: largo de labio a labio, ancho y grueso (cm). */
	void SetBoardSize(const FVector& InSize);

	/** Grueso del tablón (cm). Su cara de arriba queda a ras del camino. */
	static constexpr float BoardThickness = 20.f;
	/** Ancho del tablón (cm): más que la viga (64), menos que el camino. */
	static constexpr float BoardWidth = 140.f;

private:
	UPROPERTY(ReplicatedUsing = OnRep_BoardSize)
	FVector BoardSize = FVector(400.f, BoardWidth, BoardThickness);

	UFUNCTION()
	void OnRep_BoardSize();
};

/**
 * Cruce de quads: franjas amarillas y negras de lado a lado del camino y, cada Interval segundos, un quad del Clásico
 * (BP_QuadActor) que lo cruza y mata con sus ruedas. Aviso: en los WarnSeconds antes de cada paso las franjas
 * parpadean en rojo. Todas las máquinas calculan el parpadeo con el reloj del servidor; el quad lo crea el servidor.
 */
UCLASS()
class TORTUNABO_API ATN_ProcQuadCrossing : public AActor
{
	GENERATED_BODY()

public:
	ATN_ProcQuadCrossing();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Servidor, antes de replicar. HalfSpan: del centro a cada extremo del recorrido del quad; PathHalfWidth: medio
	 * ancho del camino (lo que pintan las franjas); FirstDelay: segundos hasta el primer paso.
	 */
	void Setup(float InHalfSpan, float InPathHalfWidth, float FirstDelay, TSubclassOf<ATN_QuadActor> InQuadClass);

	/** Segundos entre pasos, aviso previo y velocidad del quad (cm/s; la del Clásico). */
	static constexpr float Interval = 10.f;
	static constexpr float WarnSeconds = 2.5f;
	static constexpr float QuadSpeed = 800.f;
	/** Media anchura de las franjas a lo largo del camino (cm): la del quad, de rueda a rueda. */
	static constexpr float StripeHalfDepth = 350.f;

private:
	UPROPERTY(VisibleAnywhere, Category = "QuadCrossing")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "QuadCrossing")
	TObjectPtr<UDecalComponent> Stripes;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StripesMaterial;

	UPROPERTY(ReplicatedUsing = OnRep_Shape)
	FVector2D Shape = FVector2D(2000.f, 500.f);

	/** Instante (reloj del servidor) del primer paso. */
	UPROPERTY(Replicated)
	float FirstPassServerTime = 0.f;

	/** UPROPERTY: es la clase del Blueprint cargada con LoadClass y, sin referencia, el recolector la libera (#517). */
	UPROPERTY(Transient)
	TSubclassOf<ATN_QuadActor> QuadClass;
	FTimerHandle PassTimer;
	/** El siguiente quad sale por el otro lado. */
	bool bFromLeft = true;

	UFUNCTION()
	void OnRep_Shape();

	void SpawnQuad();
	float ServerTime() const;
};

/**
 * Cerrojo de un atajo: la compuerta (ATN_ProcSabotageGate) corta la rama hasta que todas sus placas
 * (ATN_PressurePlate en modo Latched) se han pisado alguna vez; basta un jugador. Solo existe en el servidor.
 */
UCLASS()
class TORTUNABO_API ATN_ProcShortcutLock : public AActor
{
	GENERATED_BODY()

public:
	ATN_ProcShortcutLock();

	void Setup(const TArray<ATN_PressurePlate*>& InPlates, ATN_ProcSabotageGate* InGate);

private:
	TArray<TWeakObjectPtr<ATN_PressurePlate>> Plates;
	TWeakObjectPtr<ATN_ProcSabotageGate> Gate;
	bool bOpened = false;

	void OnPlateChanged(ATN_PressurePlate* Plate, bool bOccupied);
};
