// Decorado lejano del trazado del Rally (#303): piezas grandes que se leen a distancia (castillos de arena enormes, grupos de
// rocas, restos de vela, sombrillas gigantes, palmeras, pedruscos, conchas y pinzas de cangrejo) a 15-60 m del borde del
// corredor. El reparto es TNRallyDressing::PlanFarDecor (puro, Tortunabo.Rally.Dressing.Far*); aquí, el suelo por trazas y
// las instancias por lotes. Mallas existentes: recetas de la playa de la Carrera, palmeras del mapa procedural y mallas de
// /Game/Blueprints/Characters/Meshes.
#include "Rally/TN_RallyTrackDressing.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "Rally/TN_RallyLogic.h"
#include "TN_RallyTrackDressingBatches.h"
#include "UObject/Package.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "World/ProcMap/TN_ProcMapFlora.h"
#include "World/ProcMap/TN_ProcMapTypes.h"
#include "../World/Beach/TN_BeachDecorKit.h"
#include "../World/ProcMap/TN_ProcMapFloraMeshes.h"
#include "../World/ProcMap/TN_ProcMapRuntimeMesh.h"

namespace TNRallyDressingFar
{
	/** Trazas de suelo de las sondas: por encima y por debajo de la cota del eje (cm). */
	constexpr double GroundUpCm = 3000.0;
	constexpr double GroundDownCm = 8000.0;
	/** Radio de la copa de la palmera del mapa procedural a escala 1 (frondas de 3,4-4,4 m y algo de inclinación). */
	constexpr double PalmCrownRadiusCm = 440.0;
	/** Lo que se hunden en la arena las piezas, en fracción de su alto (no se ve el canto de la base en las laderas). */
	constexpr double SinkFraction = 0.06;
	const TCHAR* FoliageMaterialPath = TEXT("/Game/ProcMap/Materials/M_ProcFoliage.M_ProcFoliage");
	const TCHAR* VertexColorFallbackPath = TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial");

	FTNRallyFarDecorEntry Beach(ETNBeachElement Element, float Weight, float MinRadiusCm, float MaxRadiusCm)
	{
		FTNRallyFarDecorEntry Entry;
		Entry.Source = ETNRallyFarDecorSource::BeachElement;
		Entry.Element = Element;
		Entry.Weight = Weight;
		Entry.MinRadiusCm = MinRadiusCm;
		Entry.MaxRadiusCm = MaxRadiusCm;
		return Entry;
	}

	FTNRallyFarDecorEntry Mesh(const TCHAR* Path, float Weight, float MinRadiusCm, float MaxRadiusCm)
	{
		FTNRallyFarDecorEntry Entry = Beach(ETNBeachElement::Rock, Weight, MinRadiusCm, MaxRadiusCm);
		Entry.Source = ETNRallyFarDecorSource::StaticMesh;
		Entry.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(Path));
		return Entry;
	}

	TArray<FTNRallyFarDecorEntry> DefaultEntries()
	{
		FTNRallyFarDecorEntry Palm = Beach(ETNBeachElement::Rock, 3.f, 900.f, 1400.f);
		Palm.Source = ETNRallyFarDecorSource::Palm;
		// Huellas de 9 a 36 m de radio: castillos de 25-40 m, palmeras de 18-30 m de alto, pedruscos de 10-20 m.
		return { Beach(ETNBeachElement::SandCastleHuge, 2.f, 2000.f, 3600.f), Beach(ETNBeachElement::RockCluster, 2.f, 1400.f, 2400.f),
			Beach(ETNBeachElement::ShipSailWreck, 0.6f, 2500.f, 3500.f), Beach(ETNBeachElement::PlantedUmbrella, 1.2f, 1800.f, 2500.f), Palm,
			Mesh(TEXT("/Game/Blueprints/Characters/Meshes/Piedra1.Piedra1"), 1.5f, 500.f, 1000.f),
			Mesh(TEXT("/Game/Blueprints/Characters/Meshes/ConchaAbierta.ConchaAbierta"), 1.f, 500.f, 800.f),
			Mesh(TEXT("/Game/Blueprints/Characters/Meshes/PinzasCangrejoAbierto.PinzasCangrejoAbierto"), 0.6f, 450.f, 700.f) };
	}

	bool HasSimpleCollision(const UStaticMesh* Mesh)
	{
		const UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
		return Body && Body->AggGeom.GetElementCount() > 0;
	}
}

TNRallyDressing::FFarDecorParams ATN_RallyTrackDressing::MakeFarDecorParams() const
{
	TNRallyDressing::FFarDecorParams Params;
	Params.PerKm = FarDecorPerKm;
	Params.MinFromEdgeCm = FarDecorMinFromEdgeCm;
	Params.BandCm = FarDecorBandCm;
	Params.MaxPieces = FarDecorMaxPieces;
	Params.MaxCullCm = FarDecorMaxCullCm;
	Params.MinCullCm = FMath::Min(Params.MinCullCm, Params.MaxCullCm);
	return Params;
}

TArray<TNRallyDressing::FSpot> ATN_RallyTrackDressing::AddFarDecor(const TNRallyDressing::FTrackData& Track,
	const TNRallyDressing::FBarrierPlan& Plan, int32 Seed, FTNRallyDressingBatches& Batches)
{
	using namespace TNRallyDressing;
	const FFarDecorParams Params = MakeFarDecorParams();
	// Suelo firme: la traza da en el terreno (o en lo estático del nivel) y no es agua.
	auto Ground = [this, &Track](const FVector& Probe, double& OutZ)
	{
		FVector Hit;
		if (!TraceGround(Probe, TNRallyDressingFar::GroundUpCm, TNRallyDressingFar::GroundDownCm, Hit) || IsWater(Track, Hit.Z))
		{
			return false;
		}
		OutZ = Hit.Z;
		return true;
	};
	const TArray<FSpot> Spots = PlanFarDecor(Track, Plan, FarDecorEntries, Params, Seed, Ground);
	for (const FSpot& Spot : Spots)
	{
		const float Cull = static_cast<float>(FarCullDistanceCm(Spot.RadiusCm, Params));
		if (FarDecorEntries.IsValidIndex(Spot.Entry) && AddFarPiece(FarDecorEntries[Spot.Entry], Spot, Cull, Batches))
		{
			++FarDecorCount;
		}
	}
	return Spots;
}

bool ATN_RallyTrackDressing::AddFarPiece(const FTNRallyFarDecorEntry& Entry, const TNRallyDressing::FSpot& Spot, float CullCm,
	FTNRallyDressingBatches& Batches)
{
	using ECollision = FTNRallyDressingBatches::ECollision;
	using namespace TNRallyDressingPlace;
	const FQuat Rotation = FRotator(0.0, Spot.YawDeg, 0.0).Quaternion();
	switch (Entry.Source)
	{
	case ETNRallyFarDecorSource::BeachElement:
	{
		// La receta crece con el radio pedido (dentro de lo que admite el kit) y lleva su colisión simple, nunca contra la cámara.
		const float Size = static_cast<float>(Spot.RadiusCm / FMath::Max(1.0, TNBeach::FootprintRadius(Entry.Element)));
		if (!bVisuals && !bFarDecorCollision)
		{
			return false;
		}
		return AddBeachPiece(Entry.Element, Spot.Seed, Size, FTransform(Rotation, Spot.Location), true, bFarDecorCollision, Batches, CullCm, false);
	}
	case ETNRallyFarDecorSource::Palm:
	{
		UStaticMesh* Mesh = bVisuals ? GetFarPalmMesh(Spot.Seed % TNProcMap::FloraVariants) : nullptr;
		if (!Mesh)
		{
			return false;
		}
		const double Scale = Spot.RadiusCm / TNRallyDressingFar::PalmCrownRadiusCm;
		const FVector Base = Spot.Location - FVector(0.0, 0.0, 20.0 * Scale);
		Batches.Get(Mesh, ECollision::None, true, CullCm).Add(FTransform(Rotation, Base, FVector(Scale)));
		return true;
	}
	case ETNRallyFarDecorSource::StaticMesh:
	{
		UStaticMesh* Mesh = Entry.Mesh.LoadSynchronous();
		if (!Mesh)
		{
			UE_LOG(LogTNRally, Warning, TEXT("[RallyDressing] No carga %s: pieza lejana sin malla."), *Entry.Mesh.ToString());
			return false;
		}
		const ECollision Collision = bFarDecorCollision && TNRallyDressingFar::HasSimpleCollision(Mesh) ? ECollision::Block : ECollision::None;
		if (!bVisuals && Collision == ECollision::None)
		{
			return false;
		}
		const double Scale = UniformScaleFor(Mesh, Rotation, 2.0 * Spot.RadiusCm);
		const double Sink = TNRallyDressingFar::SinkFraction * PlacedBox(Mesh, Rotation, Scale).GetSize().Z;
		Batches.Get(Mesh, Collision, true, CullCm).Add(FitUniformOnGround(Mesh, Spot.Location - FVector(0.0, 0.0, Sink), Rotation, Scale));
		return true;
	}
	}
	return false;
}

UStaticMesh* ATN_RallyTrackDressing::GetFarPalmMesh(int32 Variant)
{
	const int32 Count = TNProcMap::FloraVariants;
	if (Variant < 0 || Variant >= Count)
	{
		return nullptr;
	}
	if (FarPalmMeshes.Num() != Count)
	{
		FarPalmMeshes.SetNum(Count);
	}
	if (!FarPalmMeshes[Variant])
	{
		// Las palmeras del mapa procedural con la paleta de la playa, quietas (sin peso de viento): a esta distancia no se nota.
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TNRallyDressingFar::FoliageMaterialPath);
		Material = Material ? Material : SpectatorMaterial.LoadSynchronous();
		Material = Material ? Material : LoadObject<UMaterialInterface>(nullptr, TNRallyDressingFar::VertexColorFallbackPath);
		FLinearColor GroundColor;
		FLinearColor PathColor;
		FLinearColor RockColor;
		FLinearColor BedColor;
		TN_DefaultBiomeColors(ETNProcBiome::Beach, GroundColor, PathColor, RockColor, BedColor);
		TNProcMesh::FTNProcMeshBuffers Buffers;
		TNFloraMesh::TNFloraBuild(Buffers, TNProcMap::EFloraShape::Palm, TNFloraMesh::TNFloraPaletteFor(ETNProcBiome::Beach, GroundColor, RockColor),
			Variant, 0x9A1Au + static_cast<uint32>(Variant) * 7919u);
		FarPalmMeshes[Variant] = TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), Buffers, Material);
	}
	return FarPalmMeshes[Variant];
}
