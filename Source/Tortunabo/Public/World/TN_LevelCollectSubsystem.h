#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "TN_LevelCollectSubsystem.generated.h"

class APawn;

/**
 * Objetos del nivel para el término «recogido / total» de los puntos de final de partida (#873). Solo en el servidor.
 *
 *  - Register: el objeto está en el nivel (ATN_PickupInteractableBase en su BeginPlay; las chapas de #858 pueden
 *    apuntarse igual). Cuenta en el total aunque luego se destruya.
 *  - Forget: no es del nivel (lo ha soltado o lanzado una jugadora): ni cuenta en el total ni al recogerlo.
 *  - NotifyCollected: una jugadora lo recoge. La primera recogida de un objeto apuntado suma uno a su
 *    ATN_CoopPlayerState::LevelItemsCollected; las siguientes (tras soltarlo) no.
 */
UCLASS()
class TORTUNABO_API UTN_LevelCollectSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static void Register(const AActor* Item);
	static void Forget(const AActor* Item);
	static void NotifyCollected(const AActor* Item, const APawn* Collector);

	/** Objetos del nivel apuntados en esta partida. */
	int32 GetTotal() const { return Items.Num(); }

	/** Objetos del nivel ya recogidos por alguien. */
	int32 GetCollectedCount() const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** El subsistema del mundo de Item si este es el servidor; nullptr si no. */
	static UTN_LevelCollectSubsystem* ForServer(const AActor* Item);

	/** Objeto apuntado → si ya lo ha recogido alguien. */
	TMap<FObjectKey, bool> Items;
};
