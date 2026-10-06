#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachElement.h"
#include "TN_BeachSandDungeon.generated.h"

class UProceduralMeshComponent;
class UStaticMeshComponent;

/**
 * Castillo de arena enorme que se recorre por dentro, de -X a +X (X local = sentido de la carrera). Con SizeScale = 1
 * ocupa 57,6 x 44 m (huella de 4000·SizeScale; el recorrido por dentro es más corto que rodearlo) con murallas de 8,2 m,
 * cuatro torres con banderas y un zócalo de 50 cm (el suelo de dentro). El recorrido:
 *
 * 1. Entrada por el arco de la muralla -X (con rampita desde la arena) a la sala de las columnas (planta baja).
 * 2. Puerta de conchas (ATN_BeachShellGate desnuda, con su interruptor en la sala) al pasillo de las algas: 3,6 m de
 *    ancho, techado con lucernarios, con unas algas que enredan (ATN_BeachSeaweed) y, con bSpawnEnemiesInside, un erizo
 *    pequeño (y un cangrejo pequeño en la sala de las columnas).
 * 3. Escalera de arena de 8 peldaños de 40 cm al piso de arriba (+3,2 m): la sala de las ventanas, con muretes de 70 cm
 *    que hay que saltar o rodear y ventanas a la playa y al mar (se puede saltar por ellas: ~4 m de caída).
 * 4. Salida por la puerta alta de la muralla +X a una rampa de arena que baja a la playa.
 *
 * Premio arriba (#741): en la terraza del piso de arriba, una catapulta potenciada (TNBeach::FlagBoosted) que lanza hacia
 * el mar por encima de la muralla +X y un cofre de cima (TNBeach::FlagSummitPrize: lo mejor de la carrera para cualquier
 * puesto), como los de las fortalezas.
 *
 * Todo son bloques de arena de molde (con marcas de cubo) con colisión convexa que también para la cámara. Las piezas
 * de dentro (puerta, algas, enemigos) las crea el servidor con ATN_BeachElement::SpawnElement al empezar y las destruye
 * con el castillo. El terreno no se aplana: conviene colocarlo en una zona llana (el zócalo tapa ±50 cm).
 */
UCLASS()
class TORTUNABO_API ATN_BeachSandDungeon : public ATN_BeachElement
{
	GENERATED_BODY()

public:
	ATN_BeachSandDungeon();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Crear también un erizo en el pasillo y un cangrejo en la sala de las columnas (si existen sus clases). Apagado: los
	 * enemigos de ahora no bajan de 0,75-0,8 de tamaño, se alejan 16-38 m de su sitio sin chocar con paredes y buscan el
	 * suelo desde arriba (se subirían al techo del pasillo). Encenderlo cuando sepan moverse dentro.
	 */
	UPROPERTY(EditAnywhere, Category = "Castillo")
	bool bSpawnEnemiesInside = false;

protected:
	virtual void BeginPlay() override;
	virtual void ApplySpec() override;

	UPROPERTY(VisibleAnywhere, Category = "Castillo")
	TObjectPtr<UStaticMeshComponent> CastleMesh;

	/** Torres, almenas, banderas y adornos (malla aparte: sin colisión). */
	UPROPERTY(VisibleAnywhere, Category = "Castillo")
	TObjectPtr<UStaticMeshComponent> DecorMesh;

	UPROPERTY(VisibleAnywhere, Category = "Castillo")
	TObjectPtr<UProceduralMeshComponent> CastleCollision;

private:
	/** Servidor: puerta de conchas, algas y enemigos de dentro, y la catapulta y el cofre de la terraza. */
	void SpawnChildren();

	/** Dónde van las piezas de dentro (espacio del actor), calculado en ApplySpec. */
	FVector GateAt = FVector::ZeroVector;
	FVector SeaweedAt = FVector::ZeroVector;
	FVector UrchinAt = FVector::ZeroVector;
	FVector CrabAt = FVector::ZeroVector;
	/** Catapulta potenciada y cofre de cima en la terraza de arriba (espacio del actor; el cofre mira a +X como la catapulta). */
	FVector CatapultAt = FVector::ZeroVector;
	FVector ChestAt = FVector::ZeroVector;
	double GateWidth = 320.0;
	double CorridorWidth = 360.0;
	double CorridorLength = 1500.0;
	double RoomAShort = 2000.0;

	bool bChildrenSpawned = false;
	TArray<TWeakObjectPtr<ATN_BeachElement>> SpawnedPieces;
};
