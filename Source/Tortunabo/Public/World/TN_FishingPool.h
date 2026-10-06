#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcSearchSpot.h"
#include "TN_FishingPool.generated.h"

class UStaticMeshComponent;
class UWorld;

/**
 * Charco de pesca (GDD oficial, hoja de enemigos: «Charco de Pesca», aliado, punto de interacción estático e infinito, «se
 * puede pescar para conseguir objetos»). Es un rebuscable (ATN_ProcSearchSpot) repetible con las reglas del charco:
 *  - Mantener E TNCoopItemTuning::FishSeconds pesca y siempre sale un objeto, de la tabla del coop (TNCoopItems::RollLoot).
 *    Lo decide el servidor.
 *  - Regla fija de reutilización: cadencia por charco. Tras cada captura, TNCoopItemTuning::FishCooldownSeconds de respiro
 *    para todo el grupo (sin aviso ni anillo mientras dura) y como mucho FishMaxLootLying objetos sin recoger a la vez.
 *  - Anillo de interacción, aro de progreso, sonido y «¡puf!» del rebuscable; la malla (agua, orilla, piedras y una caña
 *    clavada) se construye en código en cada máquina con pantalla (TNCoopItemArt::GetPoolMesh), sin assets.
 *
 * Se coloca con la entrada «FishingPool» del bloque de colocaciones del mapa (categoría loot) o, para probar, con
 * TN.Coop.FishingPool. El arpón (objeto del coop) también pesca en él a distancia, con el mismo respiro.
 */
UCLASS()
class TORTUNABO_API ATN_FishingPool : public ATN_ProcSearchSpot
{
	GENERATED_BODY()

public:
	ATN_FishingPool();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool CanInteract(APawn* Interactor) const override;

	/** Servidor: crea un charco en Location (en el suelo) mirando a YawDeg. Null si no se ha podido. */
	static ATN_FishingPool* ServerSpawn(UWorld* World, const FVector& Location, float YawDeg);

	/** Se puede pescar ahora: fuera del respiro de la última captura (a mano o con el arpón) y sin nadie pescando. */
	bool CanFishNow() const;

	/**
	 * Servidor: el arpón de Fisher pesca aquí a distancia. Si se puede pescar ahora, sortea un objeto de la tabla del modo y,
	 * si le cabe, lo mete en la mochila de Fisher; empieza el respiro como una captura a mano. false si no ha dado nada.
	 */
	bool ServerHarpoonCatch(APawn* Fisher);

protected:
	virtual float GetLuck() const override;
	virtual bool PickLoot(FTN_InventoryItem& OutItem, const APawn* Searcher) const override;

	/** Agua, orilla, piedras y caña (malla construida en código; sin colisión). */
	UPROPERTY(VisibleAnywhere, Category = "Fishing")
	TObjectPtr<UStaticMeshComponent> PoolMesh;

private:
	/** Hora del servidor de la última captura con el arpón (el respiro también cuenta desde ahí); replicada para el aviso. */
	UPROPERTY(Replicated)
	float LastHarpoonCatch = -1000.f;
};
