#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachElement.h"
#include "TN_BeachFortress.generated.h"

class UProceduralMeshComponent;
class UStaticMeshComponent;

/**
 * Fortaleza de arena de la playa (FortressMedium, FortressLarge y FortressColossal; Docs/Modo_Carrera.md, «Fortalezas de
 * arena»): un castillo inmenso que se puede subir entero, andando y saltando, con premio en la cima. X local = sentido de
 * la carrera (el reparto la gira con su +X hacia el mar); origen en la arena, en el centro.
 *
 * - Muralla cuadrada con adarve, almenas y cuatro torres (con un cubo de juguete boca abajo y la bandera de Tortunavy),
 *   puertas a -X y a +X y patio dentro. En medio, terrazas macizas cada vez más altas: una en la mediana, dos en la
 *   grande y tres en la colosal; la última es la cima.
 * - Subidas: rampa y escalera por fuera hasta el adarve, escalera y rampa por dentro, rampas de terraza en terraza (en
 *   espiral) y atajos arriesgados: torrecillas de cubo que se saltan y una pala tendida hasta una cornisa de 70 cm.
 * - Premio en la cima: siempre una catapulta potenciada (TNBeach::FlagBoosted) en su borde +X que lanza mucho más lejos
 *   hacia el mar, un cofre (TreasureChest con TNBeach::FlagSummitPrize: da lo mejor de la carrera para cualquier puesto,
 *   ETNRaceLootSource::Summit) y conchas de puntos de 50 y 100 (más alguna de 50 al final de los atajos). Los crea el
 *   servidor al construirla y los destruye con ella.
 *
 * Variantes por Spec.Seed: la espiral hacia un lado u otro (espejo en Y), el tinte de la arena y los colores y adornos. Spec.SizeScale se recorta a 0,85-1,2 (por debajo no caben los pasillos de tortuga). Todo es
 * arena de molde con colisión convexa por piezas (también para la cámara); las almenas, banderas y conchas no chocan.
 */
UCLASS()
class TORTUNABO_API ATN_BeachFortress : public ATN_BeachElement
{
	GENERATED_BODY()

public:
	ATN_BeachFortress();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Huella real: la del contrato con el SizeScale recortado a 0,85-1,2. */
	virtual float GetFootprintRadius() const override;

	/** Centro del suelo de la cima en el mundo (en cualquier máquina, tras construirla). */
	FVector GetSummitLocation() const;

	/** Altura del suelo de la cima sobre la arena (cm). */
	float GetSummitHeight() const { return static_cast<float>(SummitLocal.Z); }

	/** Semilado de la cima (cm). */
	float GetSummitHalfSize() const { return SummitHalf; }

	/** Lado de la cima (+1 = +Y local, -1 = -Y) donde está el cofre. */
	float GetChestSide() const { return ChestLocal.Y >= 0.0 ? 1.f : -1.f; }

	/** Hacia dónde lanza el lanzador de la cima (el +X de la fortaleza, en planta). */
	FVector GetLaunchDirection() const;

	/** Poner en la cima el lanzador potenciado, el cofre y las conchas (servidor). Apagado, la fortaleza sale vacía. */
	UPROPERTY(EditAnywhere, Category = "Fortaleza")
	bool bSpawnPrizes = true;

protected:
	virtual void BeginPlay() override;
	virtual void ApplySpec() override;

	/** Arena: murallas, torres, terrazas, rampas, escaleras, torrecillas y cornisa. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Fortaleza")
	TObjectPtr<UStaticMeshComponent> CastleMesh;

	/** Almenas, banderas, estandartes, conchas, ventanas, cubos y pala (sin colisión: lo que se pisa va en los cascos). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Fortaleza")
	TObjectPtr<UStaticMeshComponent> DecorMesh;

	/** Cascos convexos de todo lo que se pisa o para (también la cámara). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Fortaleza")
	TObjectPtr<UProceduralMeshComponent> CastleCollision;

private:
	/** Servidor: lanzador potenciado, cofre y conchas de la cima. */
	void SpawnPrizes();

	struct FPrizeShell
	{
		FVector Local = FVector::ZeroVector;
		int32 Value = 50;
	};

	/** Lo de la cima, en el espacio del actor (con el espejo ya aplicado), calculado en ApplySpec. */
	FVector SummitLocal = FVector::ZeroVector;
	float SummitHalf = 0.f;
	FVector LauncherLocal = FVector::ZeroVector;
	FVector ChestLocal = FVector::ZeroVector;
	double ChestYaw = 0.0;
	bool bLauncherIsCatapult = true;
	float LauncherSize = 1.f;
	/** Semilla del lanzador (de ella sale el lado de su cartel: el cofre va al otro). */
	int32 LauncherSeed = 0;
	TArray<FPrizeShell> PrizeShells;

	bool bPrizesSpawned = false;
	TArray<TWeakObjectPtr<AActor>> SpawnedPrizes;
};
