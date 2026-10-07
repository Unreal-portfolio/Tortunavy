#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/TN_BreakablePlatform.h"
#include "TN_ProcSurvivalTraps.generated.h"

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
