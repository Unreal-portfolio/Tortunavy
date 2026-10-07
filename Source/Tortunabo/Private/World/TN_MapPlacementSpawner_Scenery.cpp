// Lo que cada máquina monta para sí a partir del bloque "placements" (#652): el decorado (ATN_BeachDecorField, instanciado
// y con colisión) y la vegetación (mallas de flora del mapa procedural, instanciadas y sin colisión). Igual en todas las
// máquinas porque sale del mismo manifest y de semillas por id.

#include "World/TN_MapPlacementSpawner.h"

#include "Core/TN_Log.h"
#include "World/Beach/TN_BeachDecorField.h"
#include "World/Beach/TN_BeachLayout.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "World/ProcMap/TN_ProcMapFlora.h"
#include "World/ProcMap/TN_ProcMapMath.h"
#include "World/ProcMap/TN_ProcMapTypes.h"
#include "ProcMap/TN_ProcMapFloraMeshes.h"
#include "ProcMap/TN_ProcMapRuntimeMesh.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Crc.h"

namespace TNMapPlacementScenery
{
	/** Material de la flora del mapa procedural (color de vértice con viento). */
	const TCHAR* FoliageMaterialPath = TEXT("/Game/ProcMap/Materials/M_ProcFoliage.M_ProcFoliage");

	/** Presupuesto (s) de cada paso del montaje del decorado y pasos como mucho (se monta entero al cargar). */
	constexpr double DecorStepBudget = 0.05;
	constexpr int32 DecorMaxSteps = 10000;

	bool ShapeOf(const FString& Kind, TNProcMap::EFloraShape& Out)
	{
		using EShape = TNProcMap::EFloraShape;
		if (Kind == TEXT("Palm")) { Out = EShape::Palm; return true; }
		if (Kind == TEXT("Shrub")) { Out = EShape::Bush; return true; }
		if (Kind == TEXT("Grass")) { Out = EShape::Grass; return true; }
		return false;
	}

	uint32 SeedOf(const FString& Id)
	{
		return FCrc::StrCrc32(*Id);
	}
}

void ATN_MapPlacementSpawner::QueueDecor(const TNMapPlacements::FPlacement& P)
{
	PendingDecor.Add(P);
}

void ATN_MapPlacementSpawner::QueueVegetation(const TNMapPlacements::FPlacement& P)
{
	PendingVegetation.Add(P);
}

void ATN_MapPlacementSpawner::BuildDecor()
{
	using namespace TNMapPlacementScenery;
	UWorld* World = GetWorld();
	if (!World || PendingDecor.Num() == 0)
	{
		return;
	}
	TArray<TNBeachLayout::FItem> Items;
	TArray<FTransform> Placements;
	Items.Reserve(PendingDecor.Num());
	Placements.Reserve(PendingDecor.Num());
	for (const TNMapPlacements::FPlacement& P : PendingDecor)
	{
		TNBeachLayout::FItem Item;
		Item.Element = P.Element;
		Item.Pos = FVector2D(P.Location.X, P.Location.Y);
		Item.Yaw = P.YawDeg;
		Item.Radius = TNBeach::FootprintRadius(P.Element) * P.SizeScale;
		Item.Core = Item.Radius;
		Item.HalfLength = 0.5 * P.ExtentCm;
		Item.Spec.Element = P.Element;
		Item.Spec.Seed = static_cast<int32>(SeedOf(P.Id));
		Item.Spec.SizeScale = P.SizeScale;
		Item.Spec.Extent = static_cast<float>(P.ExtentCm);
		Items.Add(Item);
		Placements.Add(FTransform(FRotator(0.0, P.YawDeg, 0.0), Grounded(P.Location)));
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient | RF_DuplicateTransient;
	DecorField = World->SpawnActor<ATN_BeachDecorField>(ATN_BeachDecorField::StaticClass(), FTransform::Identity, Params);
	if (!DecorField)
	{
		Stats.Failed += PendingDecor.Num();
		UE_LOG(LogTortunabo, Error, TEXT("[MapPlacements] No se ha podido crear el decorado local."));
		return;
	}
	DecorField->BeginBuildPlaced(Items, Placements, 1);
	int32 Steps = 0;
	while (!DecorField->StepBuild(DecorStepBudget) && ++Steps < DecorMaxSteps) {}
	Stats.DecorItems = DecorField->GetStats().Items;
	PendingDecor.Reset();
}

void ATN_MapPlacementSpawner::BuildVegetation()
{
	using namespace TNMapPlacementScenery;
	using EShape = TNProcMap::EFloraShape;
	UWorld* World = GetWorld();
	// Sin pantalla no hace falta: la vegetación no tiene colisión.
	if (!World || World->GetNetMode() == NM_DedicatedServer || PendingVegetation.Num() == 0)
	{
		PendingVegetation.Reset();
		return;
	}
	// Una malla por forma y variante, con la paleta de la playa del mapa procedural.
	TMap<int32, TArray<FTransform>> ByMesh;
	for (const TNMapPlacements::FPlacement& P : PendingVegetation)
	{
		EShape Shape;
		if (!ShapeOf(P.Kind, Shape))
		{
			continue;
		}
		const int32 Variant = static_cast<int32>(SeedOf(P.Id) % static_cast<uint32>(TNProcMap::FloraVariants));
		const int32 Key = static_cast<int32>(Shape) * TNProcMap::FloraVariants + Variant;
		ByMesh.FindOrAdd(Key).Add(FTransform(FRotator(0.0, P.YawDeg, 0.0), Grounded(P.Location), FVector(P.SizeScale)));
	}
	PendingVegetation.Reset();

	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, FoliageMaterialPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
	FLinearColor GroundC, PathC, RockC, BedC;
	TN_DefaultBiomeColors(ETNProcBiome::Beach, GroundC, PathC, RockC, BedC);
	const TNFloraMesh::FTNFloraPalette Palette = TNFloraMesh::TNFloraPaletteFor(ETNProcBiome::Beach, GroundC, RockC);
	for (TPair<int32, TArray<FTransform>>& Entry : ByMesh)
	{
		const EShape Shape = static_cast<EShape>(Entry.Key / TNProcMap::FloraVariants);
		const int32 Variant = Entry.Key % TNProcMap::FloraVariants;
		const TNFloraMesh::FTNFloraWind Wind = TNFloraMesh::TNFloraWindOf(Shape);
		TNProcMesh::FTNProcMeshBuffers Buffers;
		TNFloraMesh::TNFloraBuild(Buffers, Shape, Palette, Variant, TNProcMap::HashCell(0x652u, Entry.Key, Variant));
		UStaticMesh* Mesh = TNProcRuntimeMesh::MakeStaticMesh(this, Buffers, Material, false, Wind.Stiffness, Wind.Exponent);
		if (!Mesh)
		{
			Stats.Failed += Entry.Value.Num();
			continue;
		}
		VegetationMeshes.Add(Mesh);
		UHierarchicalInstancedStaticMeshComponent* Comp = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None,
			RF_Transient | RF_DuplicateTransient);
		Comp->SetStaticMesh(Mesh);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetGenerateOverlapEvents(false);
		Comp->bAffectDistanceFieldLighting = false;
		if (Wind.Stiffness > 0.f)
		{
			Comp->WorldPositionOffsetDisableDistance = Wind.DisableDistance > 0 ? Wind.DisableDistance : 20000;
		}
		else
		{
			Comp->bEvaluateWorldPositionOffset = false;
		}
		Comp->SetupAttachment(SceneRoot);
		Comp->RegisterComponent();
		Comp->AddInstances(Entry.Value, false, true);
		VegetationComps.Add(Comp);
		Stats.VegetationInstances += Entry.Value.Num();
	}
}
