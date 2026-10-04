// Concha (#304, #629): munición especial de la torreta del buggy (ETNRallyAmmo::Concha y ::ConchaGuiada, de las cajas «?»).
// Sale de la boca de la torreta hacia donde apunta y corre pegada al suelo; la recta rebota en las paredes y la
// teledirigida persigue al buggy de justo delante (TNKart::SteerShell). Al primer buggy que toca lo hace trompear
// (SpinOut, que respeta su escudo y el fantasma). La mueve el servidor y se replica su movimiento.
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

	/**
	 * Servidor: blanco de la teledirigida de Shooter disparada hacia Direction: el buggy de justo delante en la carrera
	 * (TNKart::HomingTargetPlace) si dispara hacia delante; nullptr si va primero, dispara hacia atrás o no hay carrera.
	 */
	static ATN_Buggy* FindHomingTarget(const UWorld* World, const ATN_Buggy* Shooter, const FVector& Direction);

	/** Servidor: lo que hace una concha al buggy que toca: frenazo y trompo (respeta su escudo y el fantasma). */
	static bool SpinOut(ATN_Buggy& Victim, const FVector& HitDir);

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
