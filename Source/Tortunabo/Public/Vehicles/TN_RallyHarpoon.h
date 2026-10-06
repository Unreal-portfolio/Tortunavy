// Arpón del Rally y de Karts (#772): se clava en el buggy alcanzado y una cuerda tira del buggy que lo ha disparado hacia
// él TNRallyTurret::HarpoonSeconds, hasta el 115 % de la velocidad punta (servidor, TNRallyTurret::HarpoonPullAccel). Es el
// contrario del ancla (ATN_RallyAnchorTether), que frena al alcanzado. Visual en cada máquina: la cuerda (cilindro
// estirado) de la torreta del que tira al gancho del alcanzado y la punta del arpón clavada.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_RallyHarpoon.generated.h"

class ATN_Buggy;
class UStaticMeshComponent;

UCLASS()
class TORTUNABO_API ATN_RallyHarpoonTether : public AActor
{
	GENERATED_BODY()

public:
	ATN_RallyHarpoonTether();

	/**
	 * Solo servidor: el arpón de Puller se clava en Target en WorldHook (sustituye al que Puller ya tuviera). Null si falta
	 * alguno o son el mismo buggy.
	 */
	static ATN_RallyHarpoonTether* Attach(ATN_Buggy* Puller, ATN_Buggy* Target, const FVector& WorldHook);

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	ATN_Buggy* GetPuller() const { return Puller; }
	ATN_Buggy* GetTarget() const { return Target; }

protected:
	virtual void BeginPlay() override;

private:
	/** Punto del gancho en mundo (sigue al alcanzado). */
	FVector GetHookLocation() const;
	/** De dónde sale la cuerda: la torreta del que tira (o su centro). */
	FVector GetRopeStart() const;
	void ApplyPull(float DeltaSeconds);
	void UpdateVisual();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> TipMesh;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> RopeMesh;

	UPROPERTY(Replicated)
	TObjectPtr<ATN_Buggy> Puller;

	UPROPERTY(Replicated)
	TObjectPtr<ATN_Buggy> Target;

	/** Gancho en el espacio local del alcanzado. */
	UPROPERTY(Replicated)
	FVector_NetQuantize10 LocalHook = FVector::ZeroVector;
};
