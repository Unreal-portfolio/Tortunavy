// Ancla del Rally: se engancha al buggy alcanzado y lo frena TNRallyTurret::AnchorSeconds con una fuerza opuesta a su
// velocidad (servidor). Visual simple en cada máquina: un bloque que se arrastra por el suelo y una cuerda (cilindro
// estirado) hasta el gancho del buggy.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_RallyAnchor.generated.h"

class ATN_Buggy;
class UStaticMeshComponent;

UCLASS()
class TORTUNABO_API ATN_RallyAnchorTether : public AActor
{
	GENERATED_BODY()

public:
	ATN_RallyAnchorTether();

	/** Solo servidor: engancha un ancla a Target en WorldHook (sustituye a la que ya tuviera). */
	static ATN_RallyAnchorTether* Attach(ATN_Buggy* Target, const FVector& WorldHook);

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	/** Punto del gancho en mundo (sigue al buggy). */
	FVector GetHookLocation() const;
	void ApplyDrag();
	void UpdateVisual();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> AnchorMesh;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> RopeMesh;

	UPROPERTY(Replicated)
	TObjectPtr<ATN_Buggy> Target;

	/** Gancho en el espacio local del buggy. */
	UPROPERTY(Replicated)
	FVector_NetQuantize10 LocalHook = FVector::ZeroVector;

	/** Posición del ancla arrastrada (cada máquina la calcula a partir del buggy). */
	FVector AnchorLocation = FVector::ZeroVector;
	bool bAnchorPlaced = false;
};
