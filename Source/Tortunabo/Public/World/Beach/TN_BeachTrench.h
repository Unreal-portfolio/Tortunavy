#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachElement.h"
#include "TN_BeachTrench.generated.h"

class ACharacter;
class UProceduralMeshComponent;
class UStaticMeshComponent;

/**
 * Agujero de trinchera (ETNBeachElement::Trench, #690, Excel_DayT): fosa de ~1,3 m rodeada de caballones de arena que se
 * suben andando por fuera; tres lados caen en vertical y el cuarto (+X) es una rampa de salida. No cava el terreno (vale
 * igual sobre la arena de la carrera y sobre el mapa de Supervivencia): el fondo es el suelo y los caballones lo rodean.
 *
 * Caer dentro no hace nada (ni daño ni derribo); dentro, el salto no llega al borde (límite de salto local, como la zona
 * lenta: lo aplica cada máquina a la tortuga que simula), así que se sale por la rampa o lanzada por otra tortuga.
 */
UCLASS()
class TORTUNABO_API ATN_BeachTrench : public ATN_BeachElement
{
	GENERATED_BODY()

public:
	ATN_BeachTrench();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual float GetTickWakeDistance() const override { return 5000.f; }

	/** Medidas (cm, ejes del actor): semilados del hoyo, alto del borde y largo de la rampa de salida. */
	FVector2D GetPitHalf() const { return PitHalf; }
	float GetRimHeight() const { return RimHeight; }
	float GetRampLength() const { return RampLength; }

protected:
	virtual void ApplySpec() override;

	UPROPERTY(VisibleAnywhere, Category = "Trinchera")
	TObjectPtr<UStaticMeshComponent> TrenchMesh;

	UPROPERTY(VisibleAnywhere, Category = "Trinchera")
	TObjectPtr<UProceduralMeshComponent> TrenchCollision;

private:
	void ClearJumpLimit(ACharacter* Turtle) const;
	FName LimitSource() const { return FName(TEXT("Trench"), static_cast<int32>(GetUniqueID())); }

	FVector2D PitHalf = FVector2D(380.0, 240.0);
	float RimHeight = 130.f;
	float RampLength = 210.f;
	float BermTop = 40.f;
	float BermFoot = 230.f;
	TSet<TWeakObjectPtr<ACharacter>> Limited;
};
