#include "Rally/TN_RallyTrackDressing.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Rally/TN_RallyGate.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyTrack.h"
#include "TN_RallyMeshUtils.h"
#include "TN_RallyTrackDressingBatches.h"
#include "UObject/Package.h"
#include "../World/Beach/TN_BeachDecorKit.h"
#include "../World/ProcMap/TN_ProcMapFaunaMeshes.h"
#include "../World/ProcMap/TN_ProcMapRuntimeMesh.h"

namespace TNRallyDressingActor
{
	using namespace TNRallyDressingPlace;

	/** Trazas de suelo: por encima y por debajo de la cota de referencia, y desnivel máximo para darlo por bueno (cm). */
	constexpr double GroundUpCm = 800.0;
	constexpr double GroundDownCm = 3000.0;
	constexpr double MaxGroundStepCm = 600.0;
	constexpr double DecorGroundUpCm = 3000.0;
	constexpr double DecorGroundDownCm = 8000.0;
	/** La sonda de caída mira un poco más allá del límite en recta (cm). */
	constexpr double DropProbeExtraCm = 300.0;
	/** Tramo de palos y cuerda (cm) y tamaño de las piezas de límite. */
	constexpr double PostRopeSegmentCm = 3000.0;
	constexpr float PostRopeSize = 0.8f;
	/** Una malla de pórtico tiene el lado largo al menos este múltiplo del corto (si no, es un poste suelto). */
	constexpr double GateArchAspect = 2.0;
	constexpr float CrabCullCm = 20000.f;
	constexpr float TireCullCm = 20000.f;
	constexpr float SpectatorCullCm = 30000.f;
	constexpr int32 SpectatorVariantCount = 4;
	/** Pisos como mucho de una pila de neumáticos que baja hasta un suelo más hondo que la calzada. */
	constexpr int32 MaxTireStackLevels = 8;
	/** Holgura entre el borde de la calzada y la cara interior de la pila (cm). */
	constexpr double TireEdgeClearanceCm = 10.0;
	/** Fracción del grueso de un neumático que la pila se hunde en el suelo. */
	constexpr double TireSinkFraction = 0.15;

	/** Tortuga del público de pie (base en el origen, mirando a +X, ~1,4 m): caparazón, peto, cabeza, brazos en alto y pies. */
	void BuildSpectatorTurtle(TNProcMesh::FTNProcMeshBuffers& Buffers, int32 Variant)
	{
		static const FLinearColor Shells[SpectatorVariantCount] = { FLinearColor(0.33f, 0.58f, 0.30f), FLinearColor(0.22f, 0.55f, 0.55f),
			FLinearColor(0.55f, 0.40f, 0.22f), FLinearColor(0.72f, 0.30f, 0.25f) };
		const FLinearColor Shell = Shells[FMath::Abs(Variant) % SpectatorVariantCount];
		const FLinearColor Skin(0.62f, 0.80f, 0.42f);
		const FLinearColor Belly(0.93f, 0.84f, 0.58f);
		const FLinearColor Eye(0.08f, 0.08f, 0.08f);
		TNFauna::TNFaunaBlob(Buffers, FVector(-14.0, 0.0, 66.0), FVector(26.0, 44.0, 56.0), Shell, Shell * 0.8f, 10, 5);
		TNFauna::TNFaunaBlob(Buffers, FVector(6.0, 0.0, 60.0), FVector(22.0, 36.0, 48.0), Belly, Belly * 0.9f, 8, 4);
		TNFauna::TNFaunaBlob(Buffers, FVector(10.0, 0.0, 122.0), FVector(24.0, 21.0, 21.0), Skin, Skin * 0.9f, 8, 4);
		for (const double Side : { -1.0, 1.0 })
		{
			TNFauna::TNFaunaBlob(Buffers, FVector(30.0, Side * 9.0, 128.0), FVector(5.0, 5.0, 5.0), Eye, Eye, 6, 3);
			TNFauna::TNFaunaBlob(Buffers, FVector(4.0, Side * 44.0, 100.0), FVector(10.0, 10.0, 26.0), Skin, Skin * 0.9f, 6, 3);
			TNFauna::TNFaunaBlob(Buffers, FVector(12.0, Side * 17.0, 7.0), FVector(18.0, 10.0, 7.0), Skin, Skin * 0.85f, 6, 3);
		}
	}

	TArray<FTNRallyDecorEntry> DefaultDecorEntries()
	{
		auto Make = [](ETNBeachElement Element, float Weight, float MinSize, float MaxSize)
		{
			FTNRallyDecorEntry Entry;
			Entry.Element = Element;
			Entry.Weight = Weight;
			Entry.MinSize = MinSize;
			Entry.MaxSize = MaxSize;
			return Entry;
		};
		return { Make(ETNBeachElement::PlantedUmbrella, 3.f, 0.5f, 0.8f), Make(ETNBeachElement::BeachTowel, 3.f, 0.5f, 0.8f),
			Make(ETNBeachElement::SandCastleSmall, 2.f, 0.6f, 1.f), Make(ETNBeachElement::ToyBucket, 2.f, 0.7f, 1.1f),
			Make(ETNBeachElement::BeachChair, 1.f, 0.5f, 0.8f), Make(ETNBeachElement::BeachBall, 1.f, 0.7f, 1.1f),
			Make(ETNBeachElement::FlipFlop, 1.f, 0.8f, 1.2f) };
	}
}

ATN_RallyTrackDressing::ATN_RallyTrackDressing()
{
	PrimaryActorTick.bCanEverTick = false;
	// Cada máquina construye su decorado con el mismo eje y la misma semilla (como ATN_RallyTrack): no se replica.
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// Solo neumáticos apilados (#303, director, 03-10): ni vallas ni paredes mezcladas.
	BarrierStyles = { ETNRallyBarrierStyle::Tires };
	DecorEntries = TNRallyDressingActor::DefaultDecorEntries();
	FarDecorEntries = TNRallyDressingFar::DefaultEntries();
	TireMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Generated/Meshes/Buggy/SM_BuggyTire.SM_BuggyTire")));
	CrabPropMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Blueprints/Characters/Meshes/MiniCangrejo.MiniCangrejo")));
	SpectatorMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Cosmetics/Materials/M_CosmeticVertexColor.M_CosmeticVertexColor")));
	StartGateMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Art/IA/rally/portico_meta_salida/SM_TN_PorticoSalida.SM_TN_PorticoSalida")));
	FinishGateMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Art/IA/rally/portico_meta_salida/SM_TN_PorticoMeta.SM_TN_PorticoMeta")));
	CheckpointMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Art/IA/rally/checkpoint/SM_TN_Checkpoint.SM_TN_Checkpoint")));
}

void ATN_RallyTrackDressing::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearDressing();
	Super::EndPlay(EndPlayReason);
}

ATN_RallyTrackDressing* ATN_RallyTrackDressing::BuildForTrack(ATN_RallyTrack* Track, int32 Seed)
{
	UWorld* World = IsValid(Track) ? Track->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	// El del nivel si lo hay (una subclase de Blueprint con su densidad); si no, uno nuevo con los valores por defecto.
	ATN_RallyTrackDressing* Dressing = nullptr;
	for (TActorIterator<ATN_RallyTrackDressing> It(World); It; ++It)
	{
		Dressing = *It;
		break;
	}
	if (!Dressing)
	{
		FActorSpawnParameters Params;
		Params.Owner = Track;
		Params.ObjectFlags |= RF_Transient;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Dressing = World->SpawnActor<ATN_RallyTrackDressing>(ATN_RallyTrackDressing::StaticClass(), FTransform::Identity, Params);
	}
	if (Dressing)
	{
		Dressing->BuildFromTrack(Track, Seed);
	}
	return Dressing;
}

bool ATN_RallyTrackDressing::BuildFromTrack(ATN_RallyTrack* Track, int32 Seed)
{
	if (!IsValid(Track) || !Track->IsBuilt())
	{
		UE_LOG(LogTNRally, Warning, TEXT("[RallyDressing] La pista no está construida: sin decorado."));
		ClearDressing();
		return false;
	}
	const bool bBuilt = Build(TNRallyDressing::SampleTrack(*Track, SampleStepCm), Seed);
	if (bBuilt && bReplaceTrackGateArches)
	{
		HideTrackGateArches(*Track);
	}
	return bBuilt;
}

bool ATN_RallyTrackDressing::Build(const TNRallyDressing::FTrackData& InTrack, int32 Seed)
{
	using namespace TNRallyDressing;
	ClearDressing();
	const FTrackData Track = WithOverrides(InTrack);
	if (Track.Samples.Num() < 3 || !GetWorld())
	{
		UE_LOG(LogTNRally, Warning, TEXT("[RallyDressing] Sin trazado (%d muestras): sin decorado."), Track.Samples.Num());
		return false;
	}
	// Sin pantalla (servidor dedicado) basta con lo que colisiona: el carril y el decorado de playa.
	bVisuals = GetNetMode() != NM_DedicatedServer;
	const FBarrierParams Params = MakeBarrierParams();
	const double Base = BaseOffsetCm(RoadHalfWidthCm(Track, Params), Params);
	const FBarrierPlan Plan = PlanBarriers(Track, ProbeDrops(Track, Base), Params);
	FTNRallyDressingBatches Batches;
	AddRails(Track, Plan, Seed, Batches);
	// Primero lo lejano y grande; el decorado cercano rellena alrededor sin pisarlo.
	const TArray<FSpot> FarSpots = AddFarDecor(Track, Plan, Seed, Batches);
	AddDecor(Track, Plan, Seed, FarSpots, Batches);
	DressedGates.Init(false, Track.Gates.Num());
	if (bVisuals)
	{
		AddSpectators(Track, Plan, Seed, Batches);
		AddGateMeshes(Track, Batches);
	}
	CreateComponents(Batches);
	UE_LOG(LogTNRally, Log, TEXT("[RallyDressing] Semilla %d: %d tramos de carril, %d piezas de límite, %d de decorado, %d lejanas, %d de público y %d pórticos."),
		Seed, RailSegmentCount, BarrierPieceCount, DecorCount, FarDecorCount, SpectatorCount, GateMeshCount);
	// Comprobación de #303 con el trazado de verdad: ningún hueco de la barrera más ancho que una tortuga.
	for (int32 Side = LeftSide; Side <= RightSide; ++Side)
	{
		double Widest = 0.0;
		for (const double Gap : BarrierGapsCm(Track, Plan, Side))
		{
			Widest = FMath::Max(Widest, Gap);
		}
		UE_LOG(LogTNRally, Log, TEXT("[RallyDressing] Barrera %s: hueco mayor %.0f cm (%s)."), Side == LeftSide ? TEXT("izquierda") : TEXT("derecha"),
			Widest, Widest <= TurtleWidthCm ? TEXT("cerrada") : TEXT("HAY HUECOS"));
	}
	return true;
}

void ATN_RallyTrackDressing::ClearDressing()
{
	for (UInstancedStaticMeshComponent* Comp : MeshComponents)
	{
		if (IsValid(Comp))
		{
			Comp->DestroyComponent();
		}
	}
	MeshComponents.Reset();
	DressedGates.Reset();
	TireStackBases[0].Reset();
	TireStackBases[1].Reset();
	RailSegmentCount = 0;
	BarrierPieceCount = 0;
	DecorCount = 0;
	FarDecorCount = 0;
	SpectatorCount = 0;
	GateMeshCount = 0;
}

TNRallyDressing::FBarrierParams ATN_RallyTrackDressing::MakeBarrierParams() const
{
	// Las reglas de los límites viven en FBarrierParams (probadas en Tortunabo.Rally.Dressing.*). La pila de neumáticos va con
	// su cara interior en el borde de la calzada (#303).
	TNRallyDressing::FBarrierParams Params;
	Params.RoadEdgeMarginCm = 0.5 * TireDiameterCm + TNRallyDressingActor::TireEdgeClearanceCm;
	return Params;
}

TNRallyDressing::FDecorParams ATN_RallyTrackDressing::MakeDecorParams() const
{
	TNRallyDressing::FDecorParams Params;
	Params.BeachPerKm = DecorPerKm;
	Params.CrabsPerKm = CrabPropsPerKm;
	Params.BandCm = DecorBandCm;
	Params.SpectatorsPerGroup = SpectatorsPerGroup;
	Params.SpectatorsAtFinish = SpectatorsAtFinish;
	Params.SpectatorGroupSpacingCm = SpectatorGroupSpacingCm;
	Params.SpectatorVariants = TNRallyDressingActor::SpectatorVariantCount;
	return Params;
}

TNRallyDressing::FTrackData ATN_RallyTrackDressing::WithOverrides(const TNRallyDressing::FTrackData& Track) const
{
	TNRallyDressing::FTrackData Result = Track;
	if (RoadWidthOverrideCm > 0.f)
	{
		Result.RoadWidthCm = RoadWidthOverrideCm;
	}
	Result.Gaps.Append(ShortcutGaps);
	return Result;
}

bool ATN_RallyTrackDressing::TraceGround(const FVector& Location, double UpCm, double DownCm, FVector& OutGround) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RallyDressingGround), true, this);
	FHitResult Hit;
	if (!World->LineTraceSingleByObjectType(Hit, Location + FVector(0.0, 0.0, UpCm), Location - FVector(0.0, 0.0, DownCm),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		return false;
	}
	OutGround = Hit.ImpactPoint;
	return true;
}

bool ATN_RallyTrackDressing::FindGroundNear(const TNRallyDressing::FTrackData& Track, const FVector& Location, double ReferenceZ,
	FVector& OutGround) const
{
	using namespace TNRallyDressingActor;
	FVector Ground;
	if (!TraceGround(FVector(Location.X, Location.Y, ReferenceZ), GroundUpCm, GroundDownCm, Ground)
		|| IsWater(Track, Ground.Z) || FMath::Abs(Ground.Z - ReferenceZ) > MaxGroundStepCm)
	{
		return false;
	}
	OutGround = Ground;
	return true;
}

bool ATN_RallyTrackDressing::IsWater(const TNRallyDressing::FTrackData& Track, double Z) const
{
	return Track.bHasWater && Z <= Track.WaterZ + 10.0;
}

TArray<uint8> ATN_RallyTrackDressing::ProbeDrops(const TNRallyDressing::FTrackData& Track, double BaseOffsetCm) const
{
	using namespace TNRallyDressing;
	TArray<uint8> Mask;
	Mask.Init(0, Track.Samples.Num());
	for (int32 Index = 0; Index < Track.Samples.Num(); ++Index)
	{
		const FAxisSample& Sample = Track.Samples[Index];
		for (int32 Side = LeftSide; Side <= RightSide; ++Side)
		{
			FVector Ground;
			const FVector Probe = LateralPoint(Sample, Side, BaseOffsetCm + TNRallyDressingActor::DropProbeExtraCm);
			const bool bHit = TraceGround(Probe, TNRallyDressingActor::GroundUpCm, TNRallyDressingActor::GroundDownCm, Ground);
			// Caída: sin suelo, suelo muy por debajo de la calzada o agua.
			if (!bHit || Sample.Location.Z - Ground.Z > DropThresholdCm || IsWater(Track, Ground.Z))
			{
				Mask[Index] |= static_cast<uint8>(1u << Side);
			}
		}
	}
	return Mask;
}

TArray<FVector> ATN_RallyTrackDressing::RunPoints(const TNRallyDressing::FTrackData& Track, const TNRallyDressing::FBarrierSide& Barrier,
	const TArray<int32>& Run, int32 Side, TArray<FVector>& OutEdge) const
{
	TArray<FVector> Points;
	OutEdge.Reset();
	for (const int32 Index : Run)
	{
		const TNRallyDressing::FAxisSample& Sample = Track.Samples[Index];
		const FVector Edge = TNRallyDressing::LateralPoint(Sample, Side, Barrier.OffsetCm[Index]);
		FVector Ground;
		const bool bGrounded = FindGroundNear(Track, Edge, Sample.Location.Z, Ground);
		// Con el suelo más bajo que la calzada (talud o caída), el carril sigue a la cota de la calzada: tapa igual.
		Points.Add(bGrounded && Ground.Z > Edge.Z ? Ground : Edge);
		OutEdge.Add(Edge);
	}
	if (Track.bClosed && Run.Num() == Track.Samples.Num() && Points.Num() > 0)
	{
		// Copias antes de añadir: Add de un elemento del propio array salta el assert de TArray (CheckAddress).
		const FVector FirstPoint = Points[0];
		const FVector FirstEdge = OutEdge[0];
		Points.Add(FirstPoint);
		OutEdge.Add(FirstEdge);
	}
	return Points;
}

TArray<FVector> ATN_RallyTrackDressing::ProjectToGround(const TNRallyDressing::FTrackData& Track, const TArray<FVector>& Points) const
{
	TArray<FVector> Out;
	Out.Reserve(Points.Num());
	for (const FVector& Point : Points)
	{
		FVector Ground;
		Out.Add(FindGroundNear(Track, Point, Point.Z, Ground) ? Ground : Point);
	}
	return Out;
}

double ATN_RallyTrackDressing::TireStackGroundZ(const TNRallyDressing::FTrackData& Track, const FVector& Center, double YawDeg,
	double RadiusCm) const
{
	using namespace TNRallyDressingActor;
	const FVector Along = FRotator(0.0, YawDeg, 0.0).Vector();
	const FVector Across(-Along.Y, Along.X, 0.0);
	const FVector Probes[] = { Center, Center + Along * RadiusCm, Center - Along * RadiusCm, Center + Across * RadiusCm,
		Center - Across * RadiusCm };
	double Lowest = TNumericLimits<double>::Max();
	for (const FVector& Probe : Probes)
	{
		FVector Ground;
		if (FindGroundNear(Track, Probe, Center.Z, Ground))
		{
			Lowest = FMath::Min(Lowest, Ground.Z);
		}
	}
	if (Lowest < TNumericLimits<double>::Max())
	{
		return Lowest;
	}
	// Sin suelo cerca (caída o agua): el suelo de más abajo o la superficie del agua; sin nada debajo, la calzada.
	FVector Deep;
	if (TraceGround(Center, GroundUpCm, GroundDownCm, Deep))
	{
		return Track.bHasWater ? FMath::Max(Deep.Z, Track.WaterZ) : Deep.Z;
	}
	return Track.bHasWater ? FMath::Min(Center.Z, Track.WaterZ) : Center.Z;
}

void ATN_RallyTrackDressing::AddRails(const TNRallyDressing::FTrackData& Track, const TNRallyDressing::FBarrierPlan& Plan, int32 Seed,
	FTNRallyDressingBatches& Batches)
{
	using namespace TNRallyDressing;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TNRallyMesh::BeamPath);
	if (!Cube)
	{
		UE_LOG(LogTNRally, Error, TEXT("[RallyDressing] No carga %s: sin carril de colisión."), TNRallyMesh::BeamPath);
		return;
	}
	for (int32 Side = LeftSide; Side <= RightSide; ++Side)
	{
		const FBarrierSide& Barrier = Plan.Sides[Side];
		for (int32 RunIndex = 0; RunIndex < Barrier.Runs.Num(); ++RunIndex)
		{
			TArray<FVector> Edge;
			const TArray<FVector> Points = RunPoints(Track, Barrier, Barrier.Runs[RunIndex], Side, Edge);
			for (int32 Point = 0; Point + 1 < Points.Num(); ++Point)
			{
				AddRailSegment(Cube, Points[Point], Points[Point + 1], Batches);
			}
			if (!bVisuals || BarrierStyles.Num() == 0)
			{
				continue;
			}
			// Toda la barrera visible, también sobre los taludes y las caídas (#303: antes se quitaba donde no había suelo
			// cerca y quedaban tramos sin neumáticos). Cada pieza busca su suelo; el estilo cambia por trozos sin hueco.
			const int32 RunSeed = TNRallyDressing::SubSeed(Seed, Side, RunIndex);
			const TArray<TArray<FVector>> Chunks = TNRallyDressing::ChunkPolyline(Edge, StyleSectionCm);
			for (int32 Chunk = 0; Chunk < Chunks.Num(); ++Chunk)
			{
				const int32 ChunkSeed = TNRallyDressing::SubSeed(RunSeed, 0, 3 + 8 * Chunk);
				AddBarrierRun(Track, Chunks[Chunk], BarrierStyles[ChunkSeed % BarrierStyles.Num()], Side, ChunkSeed, Batches);
			}
		}
	}
}

void ATN_RallyTrackDressing::AddRailSegment(UStaticMesh* Cube, const FVector& A, const FVector& B, FTNRallyDressingBatches& Batches)
{
	const FVector Delta = B - A;
	const double Flat = Delta.Size2D();
	if (Flat < 1.0)
	{
		return;
	}
	// Una caja por tramo, alineada con él y un grueso más larga: las juntas de los quiebros quedan solapadas, sin rendijas.
	const FRotator Rotation(FMath::RadiansToDegrees(FMath::Atan2(Delta.Z, Flat)), FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)), 0.0);
	const FVector Center = 0.5 * (A + B) + FVector(0.0, 0.0, 0.5 * RailHeightCm - RailSinkCm);
	const FVector Size(Delta.Size() + RailThicknessCm, RailThicknessCm, RailHeightCm);
	Batches.Get(Cube, FTNRallyDressingBatches::ECollision::Rail, false, 0.f, !bShowRails).Add(TNRallyMesh::FitToBox(Cube, Center, Size, Rotation));
	++RailSegmentCount;
}

void ATN_RallyTrackDressing::AddBarrierRun(const TNRallyDressing::FTrackData& Track, const TArray<FVector>& Points, ETNRallyBarrierStyle Style,
	int32 Side, int32 RunSeed, FTNRallyDressingBatches& Batches)
{
	if (Style == ETNRallyBarrierStyle::Tires)
	{
		AddTireRun(Track, Points, Side, Batches);
		return;
	}
	const TArray<FVector> Grounded = ProjectToGround(Track, Points);
	switch (Style)
	{
	case ETNRallyBarrierStyle::PostRope:
		AddPostRopeRun(TNRallyDressing::ResamplePolyline(Grounded, TNRallyDressingActor::PostRopeSegmentCm), RunSeed, Batches);
		break;
	case ETNRallyBarrierStyle::Sandbags:
		AddPieceRun(Grounded, ETNBeachElement::Sandbags, ETNBeachElement::Sandbags, 0.55f, RunSeed, Batches);
		break;
	case ETNRallyBarrierStyle::Logs:
		AddPieceRun(Grounded, ETNBeachElement::MossyLog, ETNBeachElement::Driftwood, 0.5f, RunSeed, Batches);
		break;
	case ETNRallyBarrierStyle::Castles:
		AddPieceRun(Grounded, ETNBeachElement::SandCastleSmall, ETNBeachElement::ToyBucket, 0.6f, RunSeed, Batches);
		break;
	default:
		break;
	}
}

void ATN_RallyTrackDressing::AddPostRopeRun(const TArray<TNRallyDressing::FPolySpot>& Spots, int32 RunSeed, FTNRallyDressingBatches& Batches)
{
	using namespace TNRallyDressingActor;
	// El caminito de palos de la playa tiene dos filas: se queda la del lado -Y, centrada en la línea del límite.
	const double RowOffset = TNBeachProp::PostPathKit::HalfWidth * PostRopeSize;
	const float Cull = TNBeachDecorKit::CullDistanceFor(ETNBeachElement::WoodenPostPath, PostRopeSize);
	for (int32 Slot = 0; Slot < Spots.Num(); ++Slot)
	{
		const FTransform ItemXf(FRotator(0.0, Spots[Slot].YawDeg, 0.0), Spots[Slot].Location);
		TMap<int32, TArray<FTransform>> ByPiece;
		TNBeachDecorKit::TilePlacements(ETNBeachElement::WoodenPostPath, TNRallyDressing::SubSeed(RunSeed, Slot, 2), PostRopeSize,
			static_cast<float>(Spots[Slot].SeparationCm), ByPiece);
		for (const TPair<int32, TArray<FTransform>>& Entry : ByPiece)
		{
			const TNBeachDecorKit::FRecipe Recipe = TNBeachDecorKit::Piece(ETNBeachElement::WoodenPostPath, Entry.Key);
			if (!Recipe.Body)
			{
				continue;
			}
			TArray<FTransform>& Out = Batches.Get(Recipe.Body, FTNRallyDressingBatches::ECollision::None, Recipe.Info.bCastShadow, Cull);
			for (const FTransform& Piece : Entry.Value)
			{
				if (Piece.GetTranslation().Y < 0.0)
				{
					FTransform Shifted = Piece;
					Shifted.AddToTranslation(FVector(0.0, RowOffset, 0.0));
					Out.Add(Shifted * ItemXf);
				}
			}
		}
		++BarrierPieceCount;
	}
}

void ATN_RallyTrackDressing::AddPieceRun(const TArray<FVector>& Points, ETNBeachElement First, ETNBeachElement Second, float Size, int32 RunSeed,
	FTNRallyDressingBatches& Batches)
{
	// Piezas casi tocándose, alineadas con el límite (su eje X local es su largo) y alternando las dos recetas.
	const double Spacing = 1.7 * TNBeach::FootprintRadius(First) * Size;
	const TArray<TNRallyDressing::FPolySpot> Spots = TNRallyDressing::ResamplePolyline(Points, Spacing);
	for (int32 Slot = 0; Slot < Spots.Num(); ++Slot)
	{
		const ETNBeachElement Element = Slot % 2 == 0 ? First : Second;
		const FTransform ItemXf(FRotator(0.0, Spots[Slot].YawDeg, 0.0), Spots[Slot].Location);
		if (AddBeachPiece(Element, TNRallyDressing::SubSeed(RunSeed, Slot, 1), Size, ItemXf, false, false, Batches))
		{
			++BarrierPieceCount;
		}
	}
}

void ATN_RallyTrackDressing::AddTireRun(const TNRallyDressing::FTrackData& Track, const TArray<FVector>& Points, int32 Side,
	FTNRallyDressingBatches& Batches)
{
	using namespace TNRallyDressingActor;
	UStaticMesh* Mesh = TireMesh.LoadSynchronous();
	Mesh = Mesh ? Mesh : LoadObject<UStaticMesh>(nullptr, TNRallyMesh::PostPath);
	if (!Mesh)
	{
		return;
	}
	// Neumático tumbado: su eje (el lado más corto de la malla) en Z; cada piso, girado para que no se vean iguales.
	const FVector Extent = Mesh->GetBounds().BoxExtent;
	FQuat Lying = FQuat::Identity;
	if (Extent.X < Extent.Y && Extent.X < Extent.Z)
	{
		Lying = FRotator(90.0, 0.0, 0.0).Quaternion();
	}
	else if (Extent.Y < Extent.X && Extent.Y < Extent.Z)
	{
		Lying = FRotator(0.0, 0.0, 90.0).Quaternion();
	}
	const double Scale = UniformScaleFor(Mesh, Lying, TireDiameterCm);
	const double Thickness = FMath::Max(1.0, PlacedBox(Mesh, Lying, Scale).GetSize().Z);
	const int32 MinLevels = FMath::Max(1, TireStackCount);
	TArray<FVector>& Bases = TireStackBases[Side == TNRallyDressing::LeftSide ? 0 : 1];
	TArray<FTransform>& Out = Batches.Get(Mesh, FTNRallyDressingBatches::ECollision::None, true, TireCullCm);
	for (const TNRallyDressing::FPolySpot& Spot : TNRallyDressing::ResamplePolyline(Points, TireDiameterCm * 1.02))
	{
		// Apoyada en el suelo de verdad bajo la pila (#303: antes iba a la cota interpolada entre muestras y flotaba).
		const double GroundZ = TireStackGroundZ(Track, Spot.Location, Spot.YawDeg, 0.5 * TireDiameterCm);
		// Con el suelo por debajo de la calzada, los pisos que falten para asomar sobre ella lo mismo que en llano.
		const double BelowRoad = FMath::Max(0.0, Spot.Location.Z - GroundZ - TireSinkFraction * Thickness);
		const int32 Levels = FMath::Clamp(MinLevels + FMath::CeilToInt32(BelowRoad / Thickness), MinLevels, MaxTireStackLevels);
		const FVector Ground(Spot.Location.X, Spot.Location.Y, GroundZ);
		for (int32 Level = 0; Level < Levels; ++Level)
		{
			const FQuat Rotation = FRotator(0.0, Spot.YawDeg + 37.0 * Level, 0.0).Quaternion() * Lying;
			const FVector Base = Ground + FVector(0.0, 0.0, (Level - TireSinkFraction) * Thickness);
			Out.Add(FitUniformOnGround(Mesh, Base, Rotation, Scale));
		}
		Bases.Add(Ground - FVector(0.0, 0.0, TireSinkFraction * Thickness));
		++BarrierPieceCount;
	}
}

bool ATN_RallyTrackDressing::AddBeachPiece(ETNBeachElement Element, int32 Seed, float Size, const FTransform& ItemXf, bool bFreePlacement,
	bool bCollision, FTNRallyDressingBatches& Batches, float CullCm, bool bAllowCameraBlock)
{
	using ECollision = FTNRallyDressingBatches::ECollision;
	const TNBeachDecorKit::FRecipe Recipe = TNBeachDecorKit::Single(Element, TNBeachDecorKit::VariantOf(Element, Seed));
	if (!Recipe.Body)
	{
		return false;
	}
	const float Clamped = TNBeachDecorKit::ClampSize(Size);
	const FTransform Local = bFreePlacement ? TNBeachDecorKit::BodyPlacement(Recipe.Info, Seed, Clamped)
		: FTransform(FQuat::Identity, FVector(0.0, 0.0, -Recipe.Info.SinkMin * Clamped), FVector(Clamped));
	const FTransform BodyXf = Local * ItemXf;
	const float Cull = CullCm >= 0.f ? CullCm : TNBeachDecorKit::CullDistanceFor(Element, Clamped);
	const ECollision Collision = !(bCollision && Recipe.bCollision) ? ECollision::None
		: (Recipe.Info.bBlocksCamera && bAllowCameraBlock ? ECollision::BlockCamera : ECollision::Block);
	Batches.Get(Recipe.Body, Collision, Recipe.Info.bCastShadow, Cull).Add(BodyXf);
	if (Recipe.Moving && bVisuals)
	{
		// La parte que se mueve (tela, banderitas), quieta en la pose del instante 0, como el decorado lejano de la playa.
		TNBeachDecorKit::FAnimState State;
		const FTransform Pose = Recipe.Info.Anim == TNBeachProp::EAnim::None ? FTransform(Recipe.Info.AnimPivot)
			: TNBeachDecorKit::AnimPose(Recipe.Info, 0.f, TNBeachDecorKit::AnimPhaseOf(Seed), Seed, State, []() { return false; });
		Batches.Get(Recipe.Moving, ECollision::None, Recipe.Info.bCastShadow, Cull).Add(Pose * BodyXf);
	}
	return true;
}

void ATN_RallyTrackDressing::AddDecor(const TNRallyDressing::FTrackData& Track, const TNRallyDressing::FBarrierPlan& Plan, int32 Seed,
	const TArray<TNRallyDressing::FSpot>& Reserved, FTNRallyDressingBatches& Batches)
{
	using namespace TNRallyDressing;
	UStaticMesh* Crab = bVisuals ? CrabPropMesh.LoadSynchronous() : nullptr;
	for (const FSpot& Spot : PlanDecor(Track, Plan, DecorEntries, MakeDecorParams(), Seed, Reserved))
	{
		FVector Ground;
		if (!TraceGround(Spot.Location, TNRallyDressingActor::DecorGroundUpCm, TNRallyDressingActor::DecorGroundDownCm, Ground) || IsWater(Track, Ground.Z))
		{
			continue;
		}
		if (Spot.Kind == ESpotKind::Beach && DecorEntries.IsValidIndex(Spot.Entry))
		{
			const FTransform ItemXf(FRotator(0.0, Spot.YawDeg, 0.0), Ground);
			DecorCount += AddBeachPiece(DecorEntries[Spot.Entry].Element, Spot.Seed, Spot.Size, ItemXf, true, true, Batches) ? 1 : 0;
		}
		else if (Spot.Kind == ESpotKind::Crab && Crab)
		{
			const FQuat Rotation = FRotator(0.0, Spot.YawDeg, 0.0).Quaternion();
			const double Scale = TNRallyDressingActor::UniformScaleFor(Crab, Rotation, CrabPropSizeCm * Spot.Size);
			Batches.Get(Crab, FTNRallyDressingBatches::ECollision::None, true, TNRallyDressingActor::CrabCullCm).Add(TNRallyDressingActor::FitUniformOnGround(Crab, Ground, Rotation, Scale));
			++DecorCount;
		}
	}
}

void ATN_RallyTrackDressing::AddSpectators(const TNRallyDressing::FTrackData& Track, const TNRallyDressing::FBarrierPlan& Plan, int32 Seed,
	FTNRallyDressingBatches& Batches)
{
	using namespace TNRallyDressing;
	for (const FSpot& Spot : PlanSpectators(Track, Plan, MakeDecorParams(), Seed))
	{
		UStaticMesh* Mesh = GetSpectatorMesh(Spot.Entry);
		FVector Ground;
		if (!Mesh || !TraceGround(Spot.Location, TNRallyDressingActor::DecorGroundUpCm, TNRallyDressingActor::DecorGroundDownCm, Ground) || IsWater(Track, Ground.Z))
		{
			continue;
		}
		const FTransform Xf(FRotator(0.0, Spot.YawDeg, 0.0), Ground, FVector(Spot.Size));
		Batches.Get(Mesh, FTNRallyDressingBatches::ECollision::None, true, TNRallyDressingActor::SpectatorCullCm).Add(Xf);
		++SpectatorCount;
	}
}

UStaticMesh* ATN_RallyTrackDressing::GetSpectatorMesh(int32 Variant)
{
	const int32 Count = TNRallyDressingActor::SpectatorVariantCount;
	if (Variant < 0 || Variant >= Count)
	{
		return nullptr;
	}
	if (SpectatorMeshes.Num() != Count)
	{
		SpectatorMeshes.SetNum(Count);
	}
	if (!SpectatorMeshes[Variant])
	{
		UMaterialInterface* Material = SpectatorMaterial.LoadSynchronous();
		Material = Material ? Material : LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
		TNProcMesh::FTNProcMeshBuffers Buffers;
		TNRallyDressingActor::BuildSpectatorTurtle(Buffers, Variant);
		SpectatorMeshes[Variant] = TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), Buffers, Material);
	}
	return SpectatorMeshes[Variant];
}

void ATN_RallyTrackDressing::AddGateMeshes(const TNRallyDressing::FTrackData& Track, FTNRallyDressingBatches& Batches)
{
	if (!bPlaceGateMeshes)
	{
		return;
	}
	UStaticMesh* Start = StartGateMesh.LoadSynchronous();
	UStaticMesh* Finish = FinishGateMesh.LoadSynchronous();
	UStaticMesh* Checkpoint = CheckpointMesh.LoadSynchronous();
	for (int32 Gate = 0; Gate < Track.Gates.Num(); ++Gate)
	{
		// En circuito la puerta 0 es salida y meta a la vez: lleva el pórtico de meta.
		UStaticMesh* Mesh = Gate == Track.FinishGate ? Finish : (Gate == 0 ? Start : Checkpoint);
		DressedGates[Gate] = Mesh && PlaceGateMesh(Mesh, Track, Track.Gates[Gate], Batches);
		GateMeshCount += DressedGates[Gate] ? 1 : 0;
	}
}

bool ATN_RallyTrackDressing::PlaceGateMesh(UStaticMesh* Mesh, const TNRallyDressing::FTrackData& Track, const FTransform& Gate,
	FTNRallyDressingBatches& Batches)
{
	using namespace TNRallyDressingActor;
	const FVector Extent = Mesh->GetBounds().BoxExtent;
	const double Long = FMath::Max(Extent.X, Extent.Y);
	const double Short = FMath::Max(1.0, FMath::Min(Extent.X, Extent.Y));
	const double Yaw = Gate.Rotator().Yaw;
	const double HalfSpan = Track.GateHalfWidthCm + GateMarginCm;
	TArray<FTransform>& Out = Batches.Get(Mesh, FTNRallyDressingBatches::ECollision::None, true, 0.f);
	FVector Ground;
	if (Long / Short >= GateArchAspect)
	{
		// Pórtico: el lado largo de la malla cruza la calzada y abarca la puerta con sus postes fuera del volumen.
		const FQuat Rotation = FRotator(0.0, Yaw + (Extent.X > Extent.Y ? 90.0 : 0.0), 0.0).Quaternion();
		const FVector At = FindGroundNear(Track, Gate.GetLocation(), Gate.GetLocation().Z, Ground) ? Ground : Gate.GetLocation();
		Out.Add(FitUniformOnGround(Mesh, At, Rotation, UniformScaleFor(Mesh, Rotation, 2.0 * HalfSpan)));
		return true;
	}
	// Poste suelto (bandera, baliza): uno a cada lado de la puerta, de GatePostHeightCm.
	const FQuat Rotation = FRotator(0.0, Yaw, 0.0).Quaternion();
	const double Scale = GatePostHeightCm / FMath::Max(1.0, 2.0 * Extent.Z);
	const FVector Right = Gate.GetRotation().GetRightVector();
	for (const double Sign : { -1.0, 1.0 })
	{
		const FVector Post = Gate.GetLocation() + Right * (Sign * HalfSpan);
		Out.Add(FitUniformOnGround(Mesh, FindGroundNear(Track, Post, Post.Z, Ground) ? Ground : Post, Rotation, Scale));
	}
	return true;
}

void ATN_RallyTrackDressing::HideTrackGateArches(const ATN_RallyTrack& Track) const
{
	UWorld* World = Track.GetWorld();
	if (!World)
	{
		return;
	}
	// El arco de BasicShapes de ATN_RallyGate es provisional (sin colisión): donde hay pórtico, solo se esconde.
	for (TActorIterator<ATN_RallyGate> It(World); It; ++It)
	{
		const ATN_RallyGate* Gate = *It;
		const int32 Index = Gate->GetGateIndex();
		if (Gate->GetOwner() != &Track || !DressedGates.IsValidIndex(Index) || !DressedGates[Index])
		{
			continue;
		}
		TInlineComponentArray<UStaticMeshComponent*> Pieces(Gate);
		for (UStaticMeshComponent* Piece : Pieces)
		{
			Piece->SetVisibility(false);
		}
	}
}

void ATN_RallyTrackDressing::CreateComponents(const FTNRallyDressingBatches& Batches)
{
	for (const FTNRallyDressingBatches::FBatch& Batch : Batches.Batches)
	{
		if (!Batch.Mesh || Batch.Transforms.Num() == 0)
		{
			continue;
		}
		UHierarchicalInstancedStaticMeshComponent* Comp = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
		Comp->SetupAttachment(GetRootComponent());
		Comp->SetStaticMesh(Batch.Mesh);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCastShadow(Batch.bShadow);
		Comp->SetHiddenInGame(Batch.bHidden);
		if (Batch.CullCm > 0.f)
		{
			Comp->SetCullDistances(0, FMath::RoundToInt32(Batch.CullCm));
		}
		// La colisión (y el material físico del carril) antes de registrar: los cuerpos de las instancias la copian.
		ApplyCollision(Comp, static_cast<uint8>(Batch.Collision));
		Comp->RegisterComponent();
		Comp->AddInstances(Batch.Transforms, false, true);
		MeshComponents.Add(Comp);
	}
}

void ATN_RallyTrackDressing::ApplyCollision(UInstancedStaticMeshComponent* Comp, uint8 Collision)
{
	using ECollision = FTNRallyDressingBatches::ECollision;
	switch (static_cast<ECollision>(Collision))
	{
	case ECollision::Rail:
		Comp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Comp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Comp->SetPhysMaterialOverride(GetRailMaterial());
		break;
	case ECollision::Block:
		TNBeachDecorKit::SetupCollision(Comp, true, false);
		break;
	case ECollision::BlockCamera:
		TNBeachDecorKit::SetupCollision(Comp, true, true);
		break;
	default:
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		break;
	}
}

UPhysicalMaterial* ATN_RallyTrackDressing::GetRailMaterial()
{
	if (RailPhysicalMaterial)
	{
		return RailPhysicalMaterial;
	}
	if (!RuntimeRailMaterial)
	{
		// Combinación Min: manda el rozamiento bajo del carril aunque el chasis tenga uno alto. Resbala y apenas rebota.
		RuntimeRailMaterial = NewObject<UPhysicalMaterial>(this, NAME_None, RF_Transient);
		RuntimeRailMaterial->Friction = RailFriction;
		RuntimeRailMaterial->bOverrideFrictionCombineMode = true;
		RuntimeRailMaterial->FrictionCombineMode = EFrictionCombineMode::Min;
		RuntimeRailMaterial->Restitution = RailRestitution;
		RuntimeRailMaterial->bOverrideRestitutionCombineMode = true;
		RuntimeRailMaterial->RestitutionCombineMode = EFrictionCombineMode::Min;
	}
	return RuntimeRailMaterial;
}
