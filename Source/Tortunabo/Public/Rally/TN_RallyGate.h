// Puerta del Rally: volumen de 24 × 10 × 4 m y arco visible. La construye ATN_RallyTrack; el cruce lo decide el servidor
// geométricamente (TNRally::SegmentCrossesGate) en ATN_RallyGameMode, no por solapamiento.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_RallyGate.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UStaticMesh;

namespace TNRallyGateFit
{
	/**
	 * Cuánto hay que alargar la pata (a lo largo de su eje, que sube UpZ por cada cm) para que su pie, a FootZ, llegue al
	 * suelo a GroundZ (#665): nada si el suelo está a su altura o por encima, nunca más de MaxCm.
	 */
	inline double FootDropAlongPost(double FootZ, double GroundZ, double UpZ, double MaxCm)
	{
		const double Gap = FootZ - GroundZ;
		return Gap <= 0.0 ? 0.0 : FMath::Min(Gap / FMath::Max(0.2, UpZ), MaxCm);
	}
}

UCLASS(Blueprintable)
class TORTUNABO_API ATN_RallyGate : public AActor
{
	GENERATED_BODY()

public:
	ATN_RallyGate();

	/** Índice en la pista y si es la meta (arco más alto y ancho). */
	void Configure(int32 InGateIndex, bool bInFinish);

	UFUNCTION(BlueprintPure, Category = "Rally")
	int32 GetGateIndex() const { return GateIndex; }

	UFUNCTION(BlueprintPure, Category = "Rally")
	bool IsFinish() const { return bFinish; }

	/** Centro del volumen con X en el sentido de la carrera. */
	FTransform GetCrossingTransform() const;
	FVector GetHalfExtent() const;

	/** Ancho, alto (sobre la calzada) y fondo del volumen (cm): 24 × 10 × 4 m (Docs/superpowers/specs/modos/03-Rally.md §5.2). */
	static constexpr double WidthCm = 2400.0;
	static constexpr double HeightCm = 1000.0;
	static constexpr double DepthCm = 400.0;
	/**
	 * El volumen baja también esto por debajo de la cota del eje (cm): un buggy con la suspensión hundida en una vaguada o por
	 * el lado bajo de un peralte pasaba por debajo y la puerta no contaba (#622, R01).
	 */
	static constexpr double BelowRoadCm = 300.0;

	/** Centro del volumen respecto al pie de la puerta (en sus ejes) y semiextensiones: de -BelowRoadCm a HeightCm. */
	static FVector CrossingCenterOffset() { return FVector(0.0, 0.0, (HeightCm - BelowRoadCm) * 0.5); }
	static FVector CrossingHalfExtent() { return FVector(DepthCm, WidthCm, HeightCm + BelowRoadCm) * 0.5; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Rally")
	TObjectPtr<UBoxComponent> Volume;

	UPROPERTY(VisibleAnywhere, Category = "Rally")
	TObjectPtr<UStaticMeshComponent> LeftPost;

	UPROPERTY(VisibleAnywhere, Category = "Rally")
	TObjectPtr<UStaticMeshComponent> RightPost;

	UPROPERTY(VisibleAnywhere, Category = "Rally")
	TObjectPtr<UStaticMeshComponent> Beam;

	UPROPERTY(EditAnywhere, Category = "Rally")
	TObjectPtr<UStaticMesh> PostMesh;

	UPROPERTY(EditAnywhere, Category = "Rally")
	TObjectPtr<UStaticMesh> BeamMesh;

private:
	void LayoutArch();
	/** Distancia, a lo largo de la pata, del pie (en ejes de la puerta) al suelo que tiene debajo; 0 si apoya o no hay suelo. */
	double MeasureFootDrop(const FVector& LocalFoot) const;

	/** La traza del pie empieza este tanto por encima y baja hasta este tanto por debajo (cm). */
	static constexpr double FootProbeUpCm = 300.0;
	static constexpr double FootProbeDownCm = 1500.0;

	int32 GateIndex = 0;
	bool bFinish = false;
};
