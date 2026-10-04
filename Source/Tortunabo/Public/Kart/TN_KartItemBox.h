// Caja «?» de los karts (#304) y del Rally (#629), al estilo de las de la carrera (TN_RaceItem*): el cubo de juguete con la
// «?» dibujado en código (TNRaceItemArt), grande e inclinado, que flota, gira y brilla con la marca de lo que se coge
// (UTN_PickupGlowComponent: anillo, columna de luz que se ve de lejos, chispitas y luz). El vehículo que pasa (lo comprueba
// el servidor en ATN_RallyGameMode::CheckItemBoxes) la abre y la caja da lo que ese vehículo usa: en Karts, un objeto según
// su puesto (UTN_KartItemComponent::TryGiveFromBox); en el Rally, munición especial de la torreta según su puesto
// (TNRally::AmmoWeightsForPlace). Estalla en estrellas en cada máquina y a los RespawnSeconds vuelve a aparecer creciendo.
// Solo se replica bAvailable (los efectos salen de su OnRep, sin RPC).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_KartItemBox.generated.h"

class APawn;
class USceneComponent;
class UStaticMeshComponent;
class UTN_PickupGlowComponent;

UCLASS(Blueprintable)
class TORTUNABO_API ATN_KartItemBox : public AActor
{
	GENERATED_BODY()

public:
	ATN_KartItemBox();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Servidor: si está disponible, da al vehículo (puesto Place de NumTeams) un objeto de Karts o, si no los usa
	 * (ATN_KartBuggy::UsesDriverItems), munición especial de la torreta; luego se oculta un rato.
	 */
	bool TryCollect(APawn* Vehicle, int32 Place, int32 NumTeams);

	UFUNCTION(BlueprintPure, Category = "Karts")
	bool IsAvailable() const { return bAvailable; }

	/** Radio de recogida (cm): el GameMode mira el paso de cada kart por él. */
	UPROPERTY(EditAnywhere, Category = "Karts")
	float PickupRadiusCm = 260.f;

	/** Segundos hasta que vuelve a aparecer. */
	UPROPERTY(EditAnywhere, Category = "Karts", meta = (ClampMin = "0.1"))
	float RespawnSeconds = 3.f;

	/** Lado del cubo (cm) y altura de su centro sobre el sitio de la caja (cm). */
	UPROPERTY(EditAnywhere, Category = "Karts|Aspecto", meta = (ClampMin = "20"))
	float BoxSizeCm = 130.f;

	UPROPERTY(EditAnywhere, Category = "Karts|Aspecto")
	float BoxCenterZCm = 40.f;

	/** Inclinación del cubo (grados) para que gire «de pico», como las cajas de las carreras de karts. */
	UPROPERTY(EditAnywhere, Category = "Karts|Aspecto")
	FRotator BoxTilt = FRotator(25.f, 0.f, 25.f);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Karts")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Karts")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Karts")
	TObjectPtr<UTN_PickupGlowComponent> Glow;

	UPROPERTY(ReplicatedUsing = OnRep_Available)
	bool bAvailable = true;

	UFUNCTION()
	void OnRep_Available();

private:
	void Reactivate();
	/** Pone la malla de la caja (la «?» de la carrera o, sin ella, la concha) a su tamaño. */
	void ApplyLook();
	/** Estallido al abrirse o destello al volver (solo en máquinas con pantalla). */
	void PlayBurst(bool bOpened);

	FTimerHandle ReactivateHandle;
	/** Escala de la malla al tamaño pedido; la animación de aparecer y de abrirse la multiplica. */
	FVector BaseScale = FVector::OneVector;
	/** Segundos desde el último cambio de bAvailable (anima el estallido o la aparición). */
	float SinceChange = 10.f;
	bool bLookApplied = false;
};
