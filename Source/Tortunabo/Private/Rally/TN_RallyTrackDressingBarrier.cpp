// Límites del Rally construidos sobre el terreno: el carril de colisión (invisible) y la barrera visible por tramos (pilas
// de neumáticos, palos y cuerda, sacos, troncos o castillos). Cada pieza busca su suelo y no entra en la calzada de ningún
// tramo ni queda bajo un techo sobre ella (#693, TN_RallyTrackDressingClearance.cpp). Las reglas puras, en
// TN_RallyTrackDressingPlan.cpp.

#include "Rally/TN_RallyTrackDressing.h"

#include "Engine/StaticMesh.h"
#include "Rally/TN_RallyLogic.h"
#include "TN_RallyMeshUtils.h"
#include "TN_RallyTrackDressingBatches.h"
#include "../World/Beach/TN_BeachDecorKit.h"

namespace TNRallyDressingBarrier
{
	/** Tramo de palos y cuerda (cm) y tamaño de las piezas de límite. */
	constexpr double PostRopeSegmentCm = 3000.0;
	constexpr float PostRopeSize = 0.8f;
	constexpr float TireCullCm = 20000.f;
	/** Pisos como mucho de una pila de neumáticos que baja hasta un suelo más hondo que la calzada. */
	constexpr int32 MaxTireStackLevels = 8;
	/** Fracción del grueso de un neumático que la pila se hunde en el suelo. */
	constexpr double TireSinkFraction = 0.15;
	/** Alto de una pieza de valla (palos y cuerda, sacos, troncos, castillos) para el gálibo (cm). */
	constexpr double FencePieceHeightCm = 150.0;
	/** Radio de un palo de la valla de palos y cuerda (cm). */
	constexpr double PostRopeRadiusCm = 30.0;

	TNRallyDressing::FBarrierPiece MakeBarrierPiece(const FVector& Center, double RadiusCm, double BottomZ, double TopZ, bool bRail)
	{
		TNRallyDressing::FBarrierPiece Piece;
		Piece.Center = Center;
		Piece.RadiusCm = RadiusCm;
		Piece.BottomZ = BottomZ;
		Piece.TopZ = TopZ;
		Piece.bRail = bRail;
		return Piece;
	}
}

double ATN_RallyTrackDressing::TireStackGroundZ(const TNRallyDressing::FTrackData& Track, const FVector& Center, double YawDeg,
	double RadiusCm) const
{
	using namespace TNRallyDressingBarrier;
	using namespace TNRallyDressingPlace;
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
				// El carril tampoco entra en la calzada de otro tramo (#693), pero solo se quita el trozo que entra: el resto se
				// conserva para no dejar un agujero entero por el que salirse de la pista.
				PlaceRail(Cube, Points[Point], Points[Point + 1], Batches);
			}
			if (!bVisuals || BarrierStyles.Num() == 0)
			{
				continue;
			}
			// Toda la barrera visible, también sobre los taludes y las caídas (#303: antes se quitaba donde no había suelo
			// cerca y quedaban tramos sin neumáticos). Cada pieza busca su suelo. Con un solo estilo, el tramo entero con la
			// misma separación (#666); con varios, cambia por trozos sin hueco.
			const int32 RunSeed = TNRallyDressing::SubSeed(Seed, Side, RunIndex);
			const TArray<TArray<FVector>> Chunks = TNRallyDressing::BarrierSections(Edge, BarrierStyles.Num(), StyleSectionCm);
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
		AddPostRopeRun(TNRallyDressing::ResamplePolyline(Grounded, TNRallyDressingBarrier::PostRopeSegmentCm), Side, RunSeed, Batches);
		break;
	case ETNRallyBarrierStyle::Sandbags:
		AddPieceRun(Grounded, ETNBeachElement::Sandbags, ETNBeachElement::Sandbags, 0.55f, Side, RunSeed, Batches);
		break;
	case ETNRallyBarrierStyle::Logs:
		AddPieceRun(Grounded, ETNBeachElement::MossyLog, ETNBeachElement::Driftwood, 0.5f, Side, RunSeed, Batches);
		break;
	case ETNRallyBarrierStyle::Castles:
		AddPieceRun(Grounded, ETNBeachElement::SandCastleSmall, ETNBeachElement::ToyBucket, 0.6f, Side, RunSeed, Batches);
		break;
	default:
		break;
	}
}

void ATN_RallyTrackDressing::AddPostRopeRun(const TArray<TNRallyDressing::FPolySpot>& Spots, int32 Side, int32 RunSeed,
	FTNRallyDressingBatches& Batches)
{
	using namespace TNRallyDressingBarrier;
	using namespace TNRallyDressingPlace;
	// El caminito de palos de la playa tiene dos filas: se queda la del lado -Y, centrada en la línea del límite.
	const double RowOffset = TNBeachProp::PostPathKit::HalfWidth * PostRopeSize;
	const float Cull = TNBeachDecorKit::CullDistanceFor(ETNBeachElement::WoodenPostPath, PostRopeSize);
	for (int32 Slot = 0; Slot < Spots.Num(); ++Slot)
	{
		// Un tramo de valla es largo: cuentan sus dos extremos y el centro (#693).
		const FVector& At = Spots[Slot].Location;
		const FVector HalfAlong = FRotator(0.0, Spots[Slot].YawDeg, 0.0).Vector() * (0.5 * Spots[Slot].SeparationCm);
		TArray<TNRallyDressing::FBarrierPiece, TInlineAllocator<3>> Probes;
		for (const FVector& Probe : { At - HalfAlong, At, At + HalfAlong })
		{
			Probes.Add(MakeBarrierPiece(Probe, PostRopeRadiusCm, Probe.Z, Probe.Z + FencePieceHeightCm, false));
		}
		if (!PassesRoadClearance(Probes, Spots[Slot].YawDeg, Side))
		{
			continue;
		}
		const FTransform ItemXf(FRotator(0.0, Spots[Slot].YawDeg, 0.0), At);
		int32 Placed = 0;
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
					++Placed;
				}
			}
		}
		// Solo cuenta (y ocupa sitio en GetBarrierPieces) el tramo que ha puesto alguna pieza de verdad.
		if (Placed > 0)
		{
			BarrierPieces.Append(Probes.GetData(), Probes.Num());
			++BarrierPieceCount;
		}
	}
}

void ATN_RallyTrackDressing::AddPieceRun(const TArray<FVector>& Points, ETNBeachElement First, ETNBeachElement Second, float Size, int32 Side,
	int32 RunSeed, FTNRallyDressingBatches& Batches)
{
	using namespace TNRallyDressingBarrier;
	using namespace TNRallyDressingPlace;
	// Piezas casi tocándose, alineadas con el límite (su eje X local es su largo) y alternando las dos recetas.
	const double Spacing = 1.7 * TNBeach::FootprintRadius(First) * Size;
	const TArray<TNRallyDressing::FPolySpot> Spots = TNRallyDressing::ResamplePolyline(Points, Spacing);
	for (int32 Slot = 0; Slot < Spots.Num(); ++Slot)
	{
		const ETNBeachElement Element = Slot % 2 == 0 ? First : Second;
		const FVector& At = Spots[Slot].Location;
		// Hacia la calzada ocupa media huella: la pieza es larga a lo largo del límite y estrecha de lado (#693).
		const TNRallyDressing::FBarrierPiece Probe = MakeBarrierPiece(At, 0.5 * TNBeach::FootprintRadius(Element) * Size, At.Z,
			At.Z + FencePieceHeightCm, false);
		if (!PassesRoadClearance(MakeArrayView(&Probe, 1), Spots[Slot].YawDeg, Side))
		{
			continue;
		}
		const FTransform ItemXf(FRotator(0.0, Spots[Slot].YawDeg, 0.0), At);
		// Sin receta no hay pieza: ni cuenta ni se apunta en GetBarrierPieces.
		if (AddBeachPiece(Element, TNRallyDressing::SubSeed(RunSeed, Slot, 1), Size, ItemXf, false, false, Batches))
		{
			BarrierPieces.Add(Probe);
			++BarrierPieceCount;
		}
	}
}

void ATN_RallyTrackDressing::AddTireRun(const TNRallyDressing::FTrackData& Track, const TArray<FVector>& Points, int32 Side,
	FTNRallyDressingBatches& Batches)
{
	using namespace TNRallyDressingBarrier;
	using namespace TNRallyDressingPlace;
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
		// Ni dentro de la calzada de ningún tramo (la pila de un paso superior que cae a la calzada de debajo) ni bajo un
		// techo sobre ella (un túnel o su boca), #693.
		const double BottomZ = GroundZ - TireSinkFraction * Thickness;
		const TNRallyDressing::FBarrierPiece Stack = MakeBarrierPiece(Ground, 0.5 * TireDiameterCm, BottomZ, BottomZ + Levels * Thickness, false);
		if (!PassesRoadClearance(MakeArrayView(&Stack, 1), Spot.YawDeg, Side))
		{
			continue;
		}
		BarrierPieces.Add(Stack);
		for (int32 Level = 0; Level < Levels; ++Level)
		{
			const FQuat Rotation = FRotator(0.0, Spot.YawDeg + 37.0 * Level, 0.0).Quaternion() * Lying;
			const FVector Base = Ground + FVector(0.0, 0.0, (Level - TireSinkFraction) * Thickness);
			Out.Add(FitUniformOnGround(Mesh, Base, Rotation, Scale));
		}
		Bases.Add(FVector(Ground.X, Ground.Y, BottomZ));
		++BarrierPieceCount;
	}
}
