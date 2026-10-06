#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/TN_TctSceneryPlan.h"
#include "TN_TctScenery.generated.h"

class ATN_BeachDecorField;
class ATN_ProcFauna;
class ATN_TctArena;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;

/**
 * El decorado vivo de una arena de Todos contra Todos (#829), local en cada máquina (no se replica nada del decorado: lo que se
 * replica es la semilla y los sitios libres, ver ATN_TctArena::ServerSetScenery). Lo crea y lo destruye ATN_TctArena.
 *
 * Mide el suelo de la arena (trazas cada metro contra su malla), lo reparte con TNTctScenery::MakePlan y lo monta con lo del
 * mapa generado de ProcMap y Coop:
 *  - Vegetación, rocas pequeñas y objetos sueltos: una malla por especie y variante (TNFloraBuild, TNPropBuild: las mismas que el
 *    generador) instanciada con HISM, sin colisión, con las distancias de corte del generador.
 *  - Decorado con colisión (rocas, troncos, castillos de arena, sacos...): ATN_BeachDecorField::BeginBuildPlaced, igual que el
 *    decorado de los mapas de terreno fijo. Es lo único que tiene colisión y sale igual en el servidor y en todos los clientes.
 *  - Fauna: ATN_ProcFauna::InitCustom, con animales que huyen de las tortugas y no tienen colisión.
 * En un servidor dedicado solo se monta el decorado con colisión.
 */
UCLASS(NotPlaceable, Transient)
class TORTUNABO_API ATN_TctScenery : public AActor
{
	GENERATED_BODY()

public:
	ATN_TctScenery();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Mide el suelo de Arena, reparte con Seed sin llenar KeepOuts y lo monta. false si la arena no tiene suelo. */
	bool Build(ATN_TctArena* Arena, uint32 Seed, const TArray<TNTctScenery::FKeepOut>& KeepOuts);

	/** Lo repartido (huella de la colisión, piezas, plantas). */
	const TNTctScenery::FPlan& GetPlan() const { return Plan; }

	/** Plantas instanciadas, piezas con colisión montadas y animales en esta máquina. */
	int32 GetNumFlora() const { return NumFlora; }
	int32 GetNumDecorBuilt() const;
	int32 GetNumAnimals() const;

	/** Cota del suelo de la arena en (X, Y) según lo medido, si lo hay. */
	bool GroundAt(const FVector2D& P, float& OutZ) const;

	/** Lado de la casilla con que se mide el suelo (uu). */
	static constexpr double FieldCell = 100.0;

private:
	/** Mide el suelo de la arena. */
	bool MeasureField(ATN_TctArena* Arena);
	void BuildFlora(float WaterBaseZ);
	void BuildDecor();
	void BuildFauna(ATN_TctArena* Arena, uint32 Seed, const TArray<TNTctScenery::FKeepOut>& KeepOuts, float WaterBaseZ);

	TSharedPtr<TNTctScenery::FHeightField> Field;
	TNTctScenery::FPlan Plan;
	int32 NumFlora = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> Meshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Instances;

	UPROPERTY(Transient)
	TObjectPtr<ATN_BeachDecorField> DecorField;

	UPROPERTY(Transient)
	TObjectPtr<ATN_ProcFauna> Fauna;
};
