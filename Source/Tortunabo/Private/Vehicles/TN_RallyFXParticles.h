// Partículas de los efectos del Rally (#301), con el sistema ligero de partículas del proyecto (TNAmbientFX: instancias de
// mallas de caras planas movidas en el Tick del dueño, las mismas del polvo de la tortuga y los géiseres). Sin assets
// nuevos: las mallas se construyen en ejecución con los materiales M_ProcFXSoft, M_ProcFXCloud y M_ProcFoliage.
// Todo es cosmético y local: nada de esto existe en un servidor dedicado.
#pragma once

#include "CoreMinimal.h"
#include "Vehicles/TN_RallyProjectile.h"
#include "World/ProcMap/TN_ProcMapAmbientFX.h"

namespace TNRallyParticles
{
	/** Emisores de un dueño: se crean al usarse y se mueven mientras quedan partículas vivas. */
	struct FEmitterSet
	{
		TArray<TNAmbientFX::FEmitter> Emitters;
	};

	/** Una capa de partículas de una ráfaga: su descripción y cuántas nacen de golpe. */
	struct FBurstLayer
	{
		TNAmbientFX::FEmitterDesc Desc;
		int32 Count = 0;
	};

	/** Capas de partículas de cada ráfaga (coco, explosión, tinta, burbuja, escudo, arena, fogonazo y chispas). */
	TArray<FBurstLayer> LayersFor(ETNRallyBurstKind Kind, float RadiusCm);

	/** La ráfaga conserva la esfera que crece (escudo y fogonazo); el resto solo lleva partículas. */
	bool KeepsSphere(ETNRallyBurstKind Kind);

	/** Crea en Owner un emisor con Desc (sin nacimientos continuos). Null en servidor dedicado. */
	TNAmbientFX::FEmitter* AddEmitter(AActor* Owner, FEmitterSet& Set, const TNAmbientFX::FEmitterDesc& Desc, const FVector& Origin);

	/** Lanza la ráfaga Kind en Where con Owner como dueño. Devuelve la vida máxima de sus partículas (s). */
	float SpawnBurst(AActor* Owner, FEmitterSet& Set, ETNRallyBurstKind Kind, const FVector& Where, float RadiusCm);

	/** Mueve todos los emisores (View: la cámara local). True mientras quede alguna partícula viva. */
	bool Tick(FEmitterSet& Set, float Dt, const FVector& View);

	/** Estallido de Count partículas en Origin hacia Direction (sin tocar el origen del goteo continuo). */
	void BurstAt(TNAmbientFX::FEmitter& Emitter, const FVector& Origin, const FVector& Direction, int32 Count);

	/** Posición de la cámara local (o Fallback si no hay). */
	FVector LocalView(const UWorld* World, const FVector& Fallback);
}
