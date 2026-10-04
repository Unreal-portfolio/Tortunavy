#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_BeachShelterVolume.generated.h"

/**
 * Refugio (#689): caja dentro de un búnker donde la tormenta no ralentiza, no empuja ni cuenta su cuenta atrás y los
 * enemigos (gaviotas incluidas) no marcan ni agarran a la tortuga. Sin curación ni ningún efecto de vida.
 *
 * No se replica: lo crea cada máquina con su búnker (ATN_BeachBunker en la carrera, la formación Bunker del ProcMap), así
 * que IsSheltered responde igual en el servidor y en los clientes. Origen en el suelo del centro; HalfExtent.Z es medio alto.
 */
UCLASS()
class TORTUNABO_API ATN_BeachShelterVolume : public AActor
{
	GENERATED_BODY()

public:
	ATN_BeachShelterVolume();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void SetShelterExtent(const FVector& InHalfExtent) { HalfExtent = InHalfExtent; }
	const FVector& GetShelterExtent() const { return HalfExtent; }

	/** Point (mundo) está dentro de este refugio. */
	bool ContainsPoint(const FVector& Point) const;

	/** El actor está dentro de algún refugio de su mundo. */
	static bool IsSheltered(const AActor* Actor);

	/** Lo mismo con un punto del mundo World. */
	static bool IsPointSheltered(const UWorld* World, const FVector& Point);

	/** Crea un refugio local (sin réplica) en Transform. */
	static ATN_BeachShelterVolume* SpawnLocal(UWorld* World, const FTransform& Transform, const FVector& InHalfExtent, AActor* Owner);

private:
	UPROPERTY(EditAnywhere, Category = "Refugio")
	FVector HalfExtent = FVector(250.0, 200.0, 130.0);

	static TArray<TWeakObjectPtr<ATN_BeachShelterVolume>>& Registry();
};
