// El decorado vivo de las arenas de Todos contra Todos (#829): ver TN_TctScenery.h. La vegetación se monta como en
// ATN_ProcMapGenerator::BuildFlora (mismas mallas, material y distancias de corte) sobre el reparto de TNTctScenery::MakePlan.

#include "World/TN_TctScenery.h"
#include "Core/TN_Log.h"
#include "Core/TN_ProjectMaterials.h"
#include "World/Beach/TN_BeachDecorField.h"
#include "World/Beach/TN_BeachLayout.h"
#include "World/ProcMap/TN_ProcFauna.h"
#include "World/ProcMap/TN_ProcMapFlora.h"
#include "World/ProcMap/TN_ProcMapMath.h"
#include "World/ProcMap/TN_ProcMapTypes.h"
#include "World/TN_TctArena.h"
#include "ProcMap/TN_ProcMapFloraMeshes.h"
#include "ProcMap/TN_ProcMapPropMeshes.h"
#include "ProcMap/TN_ProcMapRuntimeMesh.h"
#include "ProcMap/TN_TctPropMeshes.h"
#include "Game/TN_TctGameState.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Engine/CollisionProfile.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/Crc.h"

namespace TNTctSceneryActorDetail
{
	/** Hasta dónde se mide «suelo llano» (rampas más empinadas que esto no cuentan) y el tope de «sitio libre» (uu). */
	constexpr float WalkableSlopeDeg = 10.f;
	constexpr float OpenCap = 3500.f;
	/** Presupuesto de tiempo (s) de cada paso del montaje del decorado con colisión y pasos como mucho. */
	constexpr double DecorStepBudget = 0.05;
	constexpr int32 DecorMaxSteps = 10000;
	/** Animales como mucho y su densidad: una arena de 200 m no es un mapa de kilómetros. */
	constexpr int32 FaunaMaxAnimals = 150;
	constexpr float FaunaDensity = 0.9f;
	const TCHAR* FoliageMaterialPath = TEXT("/Game/ProcMap/Materials/M_ProcFoliage.M_ProcFoliage");
}

ATN_TctScenery::ATN_TctScenery()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetReplicatingMovement(false);
	SetCanBeDamaged(false);
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void ATN_TctScenery::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(DecorField)) { DecorField->Destroy(); }
	DecorField = nullptr;
	if (IsValid(Fauna)) { Fauna->Destroy(); }
	Fauna = nullptr;
	Super::EndPlay(EndPlayReason);
}

bool ATN_TctScenery::GroundAt(const FVector2D& P, float& OutZ) const
{
	return Field.IsValid() && Field->HeightAt(P, OutZ);
}

int32 ATN_TctScenery::GetNumDecorBuilt() const
{
	return IsValid(DecorField) ? DecorField->GetStats().Items : 0;
}

int32 ATN_TctScenery::GetNumAnimals() const
{
	return IsValid(Fauna) ? Fauna->GetNumAnimals() : 0;
}

bool ATN_TctScenery::MeasureField(ATN_TctArena* Arena)
{
	const FBox& Box = Arena->GetGroundBox();
	if (!Box.IsValid)
	{
		return false;
	}
	Field = MakeShared<TNTctScenery::FHeightField>();
	Field->Build(FVector2D(Box.Min.X, Box.Min.Y), FVector2D(Box.Max.X, Box.Max.Y), FieldCell,
		[Arena](double X, double Y, float& OutZ) { return Arena->TraceTerrainHeight(X, Y, OutZ); });
	Field->ComputeOpen(TNTctSceneryActorDetail::WalkableSlopeDeg, TNTctSceneryActorDetail::OpenCap);
	return Field->IsValid();
}

bool ATN_TctScenery::Build(ATN_TctArena* Arena, uint32 Seed, const TArray<TNTctScenery::FKeepOut>& KeepOuts)
{
	UWorld* World = GetWorld();
	if (!World || !Arena || !MeasureField(Arena))
	{
		return false;
	}
	const double Start = FPlatformTime::Seconds();
	const float WaterBaseZ = Arena->GetBaseWaterZ();
	Plan = TNTctScenery::MakePlan(*Field, WaterBaseZ, KeepOuts, TNTctScenery::PrimaryBiome(Arena->GetArenaVariant()), Seed);
	const double Planned = FPlatformTime::Seconds();

	BuildDecor();
	BuildStructures();
	const bool bVisuals = World->GetNetMode() != NM_DedicatedServer;
	if (bVisuals)
	{
		BuildFlora(WaterBaseZ);
		BuildFauna(Arena, Seed, KeepOuts, WaterBaseZ);
	}
	// La huella de lo que tiene colisión es la misma en el servidor y en cada cliente: el log de las dos máquinas se compara.
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] Decorado de «%s» (semilla %08X): %d piezas y %d estructuras con colisión (huella %08X), %d plantas, %d animales; reparto %.0f ms, montaje %.0f ms."),
		*Arena->GetArenaVariant().ToString(), Seed, Plan.Decor.Num(), Plan.Structures.Num(), Plan.Fingerprint, NumFlora, GetNumAnimals(),
		(Planned - Start) * 1000.0, (FPlatformTime::Seconds() - Planned) * 1000.0);
	return true;
}

void ATN_TctScenery::BuildFlora(float WaterBaseZ)
{
	using namespace TNProcMap;
	using namespace TNFloraMesh;
	using namespace TNProcMesh;
	if (Plan.Flora.Num() == 0)
	{
		return;
	}
	TArray<FFloraSpecies> Tables[NumBiomes];
	for (int32 B = 0; B < NumBiomes; ++B) { TNTctScenery::SpeciesFor(BiomeFromIndex(B), Tables[B]); }

	// Transformadas por malla (bioma, especie, variante), como el generador del mapa.
	TMap<int32, TArray<FTransform>> ByMesh;
	for (const FFloraInstance& I : Plan.Flora)
	{
		const FFloraSpecies& Sp = Tables[I.Biome][I.Species];
		FTNFloraLook Look = TNFloraLookOf(Sp.Shape);
		if (Sp.Shape == EFloraShape::Prop) { Look.bStretch = false; }
		const double Stretch = Look.bStretch ? 0.88 + 0.24 * (0.5 + 0.5 * TNProcHashNoise(FMath::RoundToInt32(I.Location.X), FMath::RoundToInt32(I.Location.Y), 0x5EEDu)) : 1.0;
		const FQuat Yaw(FVector::UpVector, FMath::DegreesToRadians(I.Yaw));
		const FQuat Lean(FVector(-I.LeanDir.Y, I.LeanDir.X, 0.0), FMath::DegreesToRadians(I.LeanDeg));
		const int32 Key = (I.Biome * 64 + I.Species) * FloraVariants + I.Variant;
		ByMesh.FindOrAdd(Key).Add(FTransform(Lean * Yaw, I.Location, FVector(I.Scale, I.Scale, I.Scale * Stretch)));
	}

	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TNTctSceneryActorDetail::FoliageMaterialPath);
	if (!Material) { Material = TNMaterials::VertexColor(); }
	for (TPair<int32, TArray<FTransform>>& Entry : ByMesh)
	{
		const int32 Variant = Entry.Key % FloraVariants;
		const int32 Species = (Entry.Key / FloraVariants) % 64;
		const int32 BiomeIdx = Entry.Key / FloraVariants / 64;
		const FFloraSpecies& Sp = Tables[BiomeIdx][Species];
		const ETNProcBiome Biome = BiomeFromIndex(BiomeIdx);
		FLinearColor Ground, PathC, RockC, Bed;
		TN_DefaultBiomeColors(Biome, Ground, PathC, RockC, Bed);
		FTNProcMeshBuffers Buffers;
		const uint32 MeshSeed = HashCell(0x829F10Au, (BiomeIdx * 64 + static_cast<int32>(Sp.Shape)) * 64 + static_cast<int32>(Sp.Prop), Variant);
		if (Sp.Shape == EFloraShape::Prop)
		{
			TNPropMesh::TNPropBuild(Buffers, Sp.Prop, Variant, MeshSeed, TNPropMesh::TNPropCrystalColor(Biome), Biome == ETNProcBiome::Volcanic);
			// La paleta de los objetos es la de las mallas procedurales: se decodifica una vez más (como en el generador).
			for (FLinearColor& Col : Buffers.Colors)
			{
				Col = FLinearColor(TNProcRuntimeMesh::SRGBToLinear(Col.R), TNProcRuntimeMesh::SRGBToLinear(Col.G), TNProcRuntimeMesh::SRGBToLinear(Col.B), Col.A);
			}
		}
		else
		{
			TNFloraBuild(Buffers, Sp.Shape, TNFloraPaletteFor(Biome, Ground, RockC), Variant, MeshSeed);
		}
		const FTNFloraWind Wind = TNFloraWindOf(Sp.Shape);
		UStaticMesh* Mesh = TNProcRuntimeMesh::MakeStaticMesh(this, Buffers, Material, false, Wind.Stiffness, Wind.Exponent);
		if (!Mesh) { continue; }
		Meshes.Add(Mesh);

		FTNFloraLook Look = TNFloraLookOf(Sp.Shape);
		if (Sp.Shape == EFloraShape::Prop)
		{
			const TNPropMesh::FTNPropLook Prop = TNPropMesh::TNPropLookOf(Sp.Prop);
			Look.Cull = Prop.Cull;
			Look.bShadow = Prop.bShadow;
		}
		UHierarchicalInstancedStaticMeshComponent* Comp = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
		Comp->SetupAttachment(RootComponent);
		Comp->SetStaticMesh(Mesh);
		// La vegetación se atraviesa (como en el mapa generado): lo que cierra el paso es el decorado con colisión.
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCastShadow(Look.bShadow);
		Comp->bAffectDistanceFieldLighting = false;
		if (Wind.Stiffness > 0.f) { Comp->WorldPositionOffsetDisableDistance = Wind.DisableDistance; }
		else { Comp->bEvaluateWorldPositionOffset = false; }
		if (Look.Cull > 0.f) { Comp->SetCullDistances(FMath::RoundToInt(Look.Cull * 0.8f), FMath::RoundToInt(Look.Cull)); }
		Comp->RegisterComponent();
		Comp->AddInstances(Entry.Value, false, false);
		Instances.Add(Comp);
		NumFlora += Entry.Value.Num();
	}
}

void ATN_TctScenery::BuildDecor()
{
	UWorld* World = GetWorld();
	if (!World || Plan.Decor.Num() == 0)
	{
		return;
	}
	TArray<TNBeachLayout::FItem> Items;
	TArray<FTransform> Placements;
	for (const TNTctScenery::FDecorPick& Pick : Plan.Decor)
	{
		TNBeachLayout::FItem Item;
		Item.Element = Pick.Element;
		Item.Pos = FVector2D(Pick.Location.X, Pick.Location.Y);
		Item.Yaw = Pick.YawDeg;
		Item.Radius = TNBeach::FootprintRadius(Pick.Element) * Pick.Scale;
		Item.Core = Item.Radius;
		Item.HalfLength = 0.0;
		Item.Spec.Element = Pick.Element;
		// La semilla de cada pieza sale de su sitio: la misma en todas las máquinas.
		Item.Spec.Seed = static_cast<int32>(FCrc::MemCrc32(&Pick.Location, sizeof(FVector)));
		Item.Spec.SizeScale = Pick.Scale;
		Items.Add(Item);
		Placements.Add(FTransform(FRotator(0.0, Pick.YawDeg, 0.0), Pick.Location));
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient | RF_DuplicateTransient;
	DecorField = World->SpawnActor<ATN_BeachDecorField>(ATN_BeachDecorField::StaticClass(), FTransform::Identity, Params);
	if (!DecorField)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[TcT] No se ha podido crear el decorado con colisión."));
		return;
	}
	DecorField->BeginBuildPlaced(Items, Placements, 1);
	int32 Steps = 0;
	while (!DecorField->StepBuild(TNTctSceneryActorDetail::DecorStepBudget) && ++Steps < TNTctSceneryActorDetail::DecorMaxSteps) {}
}

void ATN_TctScenery::BuildStructures()
{
	using namespace TNTctMesh;
	if (Plan.Structures.Num() == 0)
	{
		return;
	}
	// Una malla por tipo y variante (con una caja de colisión), todas las instancias de cada una en un HISM.
	TMap<int32, TArray<FTransform>> ByMesh;
	for (const TNTctScenery::FStructurePick& Pick : Plan.Structures)
	{
		const int32 Key = static_cast<int32>(Pick.Kind) * 4 + Pick.Variant;
		ByMesh.FindOrAdd(Key).Add(FTransform(FRotator(0.0, Pick.YawDeg, 0.0), Pick.Location, FVector(Pick.Scale)));
	}
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TNTctSceneryActorDetail::FoliageMaterialPath);
	if (!Material) { Material = TNMaterials::VertexColor(); }
	for (TPair<int32, TArray<FTransform>>& Entry : ByMesh)
	{
		const TNTctScenery::EStructureKind Kind = static_cast<TNTctScenery::EStructureKind>(Entry.Key / 4);
		const int32 Variant = Entry.Key % 4;
		TNProcMesh::FTNProcMeshBuffers Buffers;
		const uint32 MeshSeed = TNProcMap::HashCell(0x920A5Au, Entry.Key, Plan.Structures.Num());
		if (Kind == TNTctScenery::EStructureKind::Hut) { BuildHut(Buffers, Variant, MeshSeed); }
		else { BuildWreck(Buffers, Variant, MeshSeed); }
		// La paleta de los props es la de las mallas procedurales: se decodifica una vez más, como en el generador.
		for (FLinearColor& Col : Buffers.Colors)
		{
			Col = FLinearColor(TNProcRuntimeMesh::SRGBToLinear(Col.R), TNProcRuntimeMesh::SRGBToLinear(Col.G), TNProcRuntimeMesh::SRGBToLinear(Col.B), Col.A);
		}
		UStaticMesh* Mesh = TNProcRuntimeMesh::MakeStaticMesh(this, Buffers, Material, true);
		if (!Mesh) { continue; }
		Meshes.Add(Mesh);
		UHierarchicalInstancedStaticMeshComponent* Comp = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
		Comp->SetupAttachment(RootComponent);
		Comp->SetStaticMesh(Mesh);
		// Con colisión: lo grande se rodea. Igual en el servidor y en cada cliente.
		Comp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetGenerateOverlapEvents(false);
		Comp->bAffectDistanceFieldLighting = false;
		Comp->bEvaluateWorldPositionOffset = false;
		Comp->SetCullDistances(9000, 14000);
		Comp->RegisterComponent();
		Comp->AddInstances(Entry.Value, false, false);
		Instances.Add(Comp);
		NumStructures += Entry.Value.Num();
	}
}

void ATN_TctScenery::BuildFauna(ATN_TctArena* Arena, uint32 Seed, const TArray<TNTctScenery::FKeepOut>& KeepOuts, float WaterBaseZ)
{
	UWorld* World = GetWorld();
	if (!World || Plan.Fauna.Num() == 0)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient | RF_DuplicateTransient;
	Fauna = World->SpawnActor<ATN_ProcFauna>(ATN_ProcFauna::StaticClass(), FTransform::Identity, Params);
	if (!Fauna)
	{
		return;
	}
	Fauna->MaxAnimals = TNTctSceneryActorDetail::FaunaMaxAnimals;
	Fauna->Density = TNTctSceneryActorDetail::FaunaDensity;

	ATN_ProcFauna::FCustomTerrain Terrain;
	const TSharedPtr<TNTctScenery::FHeightField> Ground = Field;
	Terrain.HeightAt = [Ground](const FVector& P)
	{
		float Z = 0.f;
		return Ground->HeightAt(FVector2D(P.X, P.Y), Z) ? Z : -1.0e5f;
	};
	Terrain.Blocked = [KeepOuts](const FVector2D& P) { return TNTctScenery::KeepOutDistance(KeepOuts, P) < 0.0; };
	// El agua que sube (y que es veneno): los animales de tierra se esconden cuando les llega.
	const TWeakObjectPtr<UWorld> WeakWorld = World;
	Terrain.WaterZNow = [WeakWorld, WaterBaseZ]()
	{
		const ATN_TctGameState* State = WeakWorld.IsValid() ? WeakWorld->GetGameState<ATN_TctGameState>() : nullptr;
		return State ? FMath::Max(State->GetWaterZ(), WaterBaseZ) + 2.f : WaterBaseZ + 2.f;
	};
	Terrain.WaterZ = WaterBaseZ + 2.f;
	for (const TNTctScenery::FFaunaAnchor& Anchor : Plan.Fauna)
	{
		ATN_ProcFauna::FCustomAnchor& Custom = Terrain.Anchors.AddDefaulted_GetRef();
		Custom.P = Anchor.P;
		Custom.S = Anchor.S;
		Custom.Module = Anchor.Module;
		Custom.Biome = Anchor.Biome;
	}
	Fauna->InitCustom(Terrain, Seed);
}
