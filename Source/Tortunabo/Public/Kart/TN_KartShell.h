// Concha de los karts (#304): sale del kart y corre pegada al suelo; la recta rebota en las paredes y la teledirigida
// persigue al kart de delante (TNKart::SteerShell). Al primer kart que toca lo hace trompear (UTN_KartItemComponent::
// SpinOut, que respeta su escudo, la estrella y el fantasma). La mueve el servidor y se replica su movimiento.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_KartShell.generated.h"

class ATN_Buggy;
class UStaticMeshComponent;

UCLASS()
class TORTUNABO_API ATN_KartShell : public AActor
{
	GENERATED_BODY()

public:
	ATN_KartShell();

	/** Servidor: crea una concha en Where hacia Direction (con Target, teledirigida). Shooter no recibe su propia concha al salir. */
	static ATN_KartShell* LaunchShell(UWorld* World, ATN_Buggy* Shooter, const FVector& Where, const FVector& Direction,
		ATN_Buggy* Target, bool bHoming);

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	/** Servidor: un paso de la concha; false si se ha gastado. */
	bool StepServer(float DeltaSeconds);
	void Burst();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(ReplicatedUsing = OnRep_Homing)
	bool bHoming = false;

	UFUNCTION()
	void OnRep_Homing();

	TWeakObjectPtr<ATN_Buggy> Shooter;
	TWeakObjectPtr<ATN_Buggy> Target;
	FVector Direction = FVector::ForwardVector;
	float Age = 0.f;
	int32 Bounces = 0;
};
