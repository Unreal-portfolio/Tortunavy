#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/TN_TctShoreMarks.h"
#include "TN_TctShoreWarning.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;

/**
 * El aviso del nivel del agua de Todos contra Todos (#920), dentro del mundo y sin planos ni parpadeos: con tiempo de sobra
 * (TNTctPoisonDefaults::ShoreWarnLead) antes de cada subida empieza a asomar, en la orilla que cubrirá el agua, una línea de
 * espuma y algas verdes y, de trecho en trecho, postes con marcas cuya lámpara se enciende y late más deprisa al acercarse la
 * subida. Va creciendo (más marcas, más grandes) hasta que el agua llega, y durante la subida se ve entera. La cota es siempre
 * la del siguiente escalón del plan (TNTctRules::ShoreWarnTargetZ, la misma de la cuenta atrás del HUD).
 *
 * Solo visual, local en cada máquina con pantalla; lo crea ATN_TctScenery con el suelo medido. El reparto (PlanShoreMarks) sale
 * de la semilla y del suelo, igual en todas las máquinas. HISM con distancias de corte: unas decenas de instancias por escalón.
 */
UCLASS(NotPlaceable, Transient)
class TORTUNABO_API ATN_TctShoreWarning : public AActor
{
	GENERATED_BODY()

public:
	ATN_TctShoreWarning();

	virtual void Tick(float DeltaSeconds) override;

	/** Prepara el aviso sobre el suelo medido Field, sin llenar KeepOuts. false si no hay pantalla. */
	bool Init(TSharedPtr<TNTctScenery::FHeightField> InField, const TArray<TNTctScenery::FKeepOut>& InKeepOuts, uint32 InSeed);

	/** Lo que el aviso enseña ahora: la cota que marca (si hay), cuánto se deja ver (0-1) y cuántas marcas hay puestas. */
	float GetTargetZ() const { return MarksZ; }
	float GetProgress() const { return Progress; }
	int32 GetNumShown() const { return NumShown; }

	/** Fuerza el estado del aviso (pruebas): cota, avance 0-1 y segundos que faltan para la subida. */
	void DebugSet(float TargetZ, float InProgress, float SecondsLeft);

private:
	/** Marcas puestas con la fracción Fraction (0-1) de las planeadas, a un tamaño que crece con ella. */
	void ShowMarks(float Fraction);
	void ClearMarks();

	/** Qué enseñar ahora: el estado real de la ronda o el forzado por DebugSet. */
	void Evaluate(float& OutTargetZ, float& OutProgress, float& OutSecondsLeft) const;

	UHierarchicalInstancedStaticMeshComponent* MakeInstances(UStaticMesh* Mesh, UMaterialInterface* Material, float CullEnd, bool bShadow);

	TSharedPtr<TNTctScenery::FHeightField> Field;
	TArray<TNTctScenery::FKeepOut> KeepOuts;
	uint32 Seed = 0;
	TArray<TNTctScenery::FShoreMark> Marks;
	float MarksZ = -1.0e9f;
	float Progress = 0.f;
	int32 NumShown = 0;
	int32 Stage = -1;
	bool bDebug = false;
	float DebugTargetZ = 0.f;
	float DebugProgress = 0.f;
	float DebugSecondsLeft = 0.f;

	/** Espuma (2 variantes), algas (2) y postes: una malla con su HISM cada una; las lámparas, otro. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> Meshes;

	UPROPERTY(Transient)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Foam[2];

	UPROPERTY(Transient)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Algae[2];

	UPROPERTY(Transient)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Posts;

	UPROPERTY(Transient)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Lamps;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LampMaterial;
};
