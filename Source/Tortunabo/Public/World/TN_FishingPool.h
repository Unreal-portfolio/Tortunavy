#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcSearchSpot.h"
#include "TN_FishingPool.generated.h"

class UStaticMeshComponent;
class UWorld;

/**
 * Charco de pesca (GDD oficial, hoja de enemigos: «Charco de Pesca», aliado, punto de interacción estático e infinito, «se
 * puede pescar para conseguir objetos»). Es un rebuscable (ATN_ProcSearchSpot) repetible con las reglas del charco:
 *  - Mantener E TNCoopItemTuning::FishSeconds pesca y siempre sale un objeto, de la tabla del modo (TNCoopItems::RollModeLoot:
 *    la del coop; en la carrera de la playa, la de la carrera). Lo decide el servidor.
 *  - Regla fija de reutilización: cadencia por charco. Tras cada captura, TNCoopItemTuning::FishCooldownSeconds de respiro
 *    para todo el grupo (sin aviso ni anillo mientras dura) y como mucho FishMaxLootLying objetos sin recoger a la vez.
 *  - Anillo de interacción, aro de progreso, sonido y «¡puf!» del rebuscable; la malla (agua, orilla, piedras y una caña
 *    clavada) se construye en código en cada máquina con pantalla (TNCoopItemArt::GetPoolMesh), sin assets.
 *
 * Se coloca con la entrada «FishingPool» del bloque de colocaciones del mapa (categoría loot) o, para probar, con
 * TN.Coop.FishingPool.
 */
UCLASS()
class TORTUNABO_API ATN_FishingPool : public ATN_ProcSearchSpot
{
	GENERATED_BODY()

public:
	ATN_FishingPool();

	virtual void BeginPlay() override;

	/** Servidor: crea un charco en Location (en el suelo) mirando a YawDeg. Null si no se ha podido. */
	static ATN_FishingPool* ServerSpawn(UWorld* World, const FVector& Location, float YawDeg);

protected:
	virtual float GetLuck() const override;
	virtual bool PickLoot(FTN_InventoryItem& OutItem, const APawn* Searcher) const override;

	/** Agua, orilla, piedras y caña (malla construida en código; sin colisión). */
	UPROPERTY(VisibleAnywhere, Category = "Fishing")
	TObjectPtr<UStaticMeshComponent> PoolMesh;
};
