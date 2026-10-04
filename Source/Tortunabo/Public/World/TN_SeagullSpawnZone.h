#pragma once

#include "CoreMinimal.h"
#include "World/TN_SpawnZoneBase.h"
#include "TN_SeagullSpawnZone.generated.h"

class ATN_EnemySeagull;

/**
 * Zona de spawn de gaviotas enemigas dinámicas (#11).
 *
 * Uso:
 *   Colocar en el mundo y asignar SeagullClass (BP_EnemySeagull).
 *   El servidor elige aleatoriamente un jugador vivo dentro del volumen
 *   cada SpawnInterval segundos y spawnea una gaviota encima de él.
 *
 * La gaviota puede salir de esta zona siguiendo al jugador — la zona solo
 * controla quién puede ser elegido como objetivo inicial. La lógica de
 * escape/retirada vive en ATN_EnemySeagull.
 *
 * Respeta MaxConcurrent: no spawnea si ya hay esa cantidad de gaviotas activas
 * originadas por esta zona. El volumen, timer, selección de jugadores y clamp
 * los aporta ATN_SpawnZoneBase.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_SeagullSpawnZone : public ATN_SpawnZoneBase
{
	GENERATED_BODY()

public:
	ATN_SeagullSpawnZone();

	/** Gaviota por defecto si el Blueprint no trae una (zona creada desde código, #516). */
	void SetSeagullClassIfMissing(TSubclassOf<ATN_EnemySeagull> InClass);

protected:
	/** Clase de gaviota a spawnear. Apuntar al BP hijo de ATN_EnemySeagull. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeagullSpawnZone")
	TSubclassOf<ATN_EnemySeagull> SeagullClass;

	virtual void TrySpawn() override;
};
