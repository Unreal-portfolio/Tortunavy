// Gálibo de la calzada del Rally (#693): ninguna pieza de la barrera (neumáticos, valla o carril de colisión) dentro del ancho
// de la calzada de ningún tramo, también del que pasa por debajo en un cruce, ni bajo un techo sobre la calzada (un túnel o su
// boca). FRoadFootprint es lógica pura (Tortunabo.Rally.Dressing.RoadClear.*); las trazas de techo son del actor.

#include "Rally/TN_RallyTrackDressing.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Rally/TN_RallyTrack.h"

namespace TNRallyDressingClearance
{
	/**
	 * Sondas de techo: desde la pieza hacia la calzada, a esto más allá de su radio, y lo mismo antes y después a lo largo del
	 * borde (la boca de un túnel empieza de golpe).
	 */
	constexpr double RoofInwardCm = 150.0;
	constexpr double RoofAlongCm = 300.0;
	/** Las sondas salen de un poco por encima de la cara de arriba de la pieza. */
	constexpr double RoofProbeLiftCm = 10.0;
	/**
	 * Un techo deja aire debajo: lo que la sonda toca a menos de esto es el propio suelo (la malla del terreno no sigue al
	 * centímetro la cota del eje en una cresta o un salto), no una bóveda. La de los túneles queda a más de 4 m de la sonda.
	 */
	constexpr double RoofMinGapCm = 150.0;
}

namespace TNRallyDressing
{
	FRoadFootprint::FRoadFootprint(const FTrackData& Track, const FBarrierParams& Params, const FRoadClearance& InClearance)
		: Clearance(InClearance)
	{
		const int32 Num = Track.Samples.Num();
		const int32 Count = Track.bClosed ? Num : Num - 1;
		for (int32 Index = 0; Num >= 2 && Index < Count; ++Index)
		{
			const int32 Next = (Index + 1) % Num;
			FSegment Segment;
			Segment.A = Track.Samples[Index].Location;
			Segment.B = Track.Samples[Next].Location;
			Segment.HalfA = SampleRoadHalfCm(Track, Index, Params);
			Segment.HalfB = SampleRoadHalfCm(Track, Next, Params);
			const double Half = FMath::Max(Segment.HalfA, Segment.HalfB);
			MaxHalfCm = FMath::Max(MaxHalfCm, Half);
			const int32 SegmentIndex = Segments.Add(Segment);
			const FIntPoint Low = CellOf(FMath::Min(Segment.A.X, Segment.B.X) - Half, FMath::Min(Segment.A.Y, Segment.B.Y) - Half);
			const FIntPoint High = CellOf(FMath::Max(Segment.A.X, Segment.B.X) + Half, FMath::Max(Segment.A.Y, Segment.B.Y) + Half);
			for (int32 X = Low.X; X <= High.X; ++X)
			{
				for (int32 Y = Low.Y; Y <= High.Y; ++Y)
				{
					Cells.FindOrAdd(FIntPoint(X, Y)).Add(SegmentIndex);
				}
			}
		}
	}

	FIntPoint FRoadFootprint::CellOf(double X, double Y) const
	{
		return FIntPoint(FMath::FloorToInt32(X / CellCm), FMath::FloorToInt32(Y / CellCm));
	}

	double FRoadFootprint::IntrusionCm(const FVector& Center, double RadiusCm, double BottomZ, double TopZ) const
	{
		const double Radius = FMath::Max(0.0, RadiusCm);
		const FIntPoint Low = CellOf(Center.X - Radius, Center.Y - Radius);
		const FIntPoint High = CellOf(Center.X + Radius, Center.Y + Radius);
		const FVector2D Point(Center.X, Center.Y);
		// Un tramo que cae en varias celdas se mira varias veces: da igual, solo se queda la mayor penetración.
		double Deepest = 0.0;
		for (int32 X = Low.X; X <= High.X; ++X)
		{
			for (int32 Y = Low.Y; Y <= High.Y; ++Y)
			{
				const TArray<int32>* InCell = Cells.Find(FIntPoint(X, Y));
				if (!InCell)
				{
					continue;
				}
				for (const int32 SegmentIndex : *InCell)
				{
					const FSegment& Segment = Segments[SegmentIndex];
					const FVector2D A(Segment.A.X, Segment.A.Y);
					const FVector2D AB = FVector2D(Segment.B.X, Segment.B.Y) - A;
					const double LengthSq = AB.SizeSquared();
					const double T = LengthSq > UE_KINDA_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LengthSq, 0.0, 1.0) : 0.0;
					const double RoadZ = FMath::Lerp(Segment.A.Z, Segment.B.Z, T);
					// Solo cuenta si la pieza corta el gálibo de ese tramo: una barrera de otro tramo muy por encima (un puente) no.
					if (TopZ < RoadZ - Clearance.BelowCm || BottomZ > RoadZ + Clearance.AboveCm)
					{
						continue;
					}
					const double Half = FMath::Lerp(Segment.HalfA, Segment.HalfB, T);
					const double Distance = FVector2D::Distance(Point, A + AB * T);
					Deepest = FMath::Max(Deepest, Half + Radius - Distance);
				}
			}
		}
		return Deepest;
	}

	double FRoadFootprint::RoadZAt(const FVector& Point, double ReferenceZ, double FallbackZ) const
	{
		const TArray<int32>* InCell = Cells.Find(CellOf(Point.X, Point.Y));
		if (!InCell)
		{
			return FallbackZ;
		}
		const FVector2D Flat(Point.X, Point.Y);
		double Best = FallbackZ;
		double BestDz = TNumericLimits<double>::Max();
		for (const int32 SegmentIndex : *InCell)
		{
			const FSegment& Segment = Segments[SegmentIndex];
			const FVector2D A(Segment.A.X, Segment.A.Y);
			const FVector2D AB = FVector2D(Segment.B.X, Segment.B.Y) - A;
			const double LengthSq = AB.SizeSquared();
			const double T = LengthSq > UE_KINDA_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(Flat - A, AB) / LengthSq, 0.0, 1.0) : 0.0;
			if (FVector2D::Distance(Flat, A + AB * T) > FMath::Lerp(Segment.HalfA, Segment.HalfB, T))
			{
				continue;
			}
			const double Z = FMath::Lerp(Segment.A.Z, Segment.B.Z, T);
			if (FMath::Abs(Z - ReferenceZ) < BestDz)
			{
				BestDz = FMath::Abs(Z - ReferenceZ);
				Best = Z;
			}
		}
		return Best;
	}

	TArray<FBarrierPiece> RailProbes(const FVector& A, const FVector& B, const FRailParams& Rail)
	{
		const double Spacing = FMath::Max(1.0, 2.0 * Rail.RadiusCm);
		const int32 Intervals = FMath::Max(1, FMath::CeilToInt32(FVector::Dist2D(A, B) / Spacing));
		TArray<FBarrierPiece> Probes;
		Probes.Reserve(Intervals + 1);
		for (int32 Index = 0; Index <= Intervals; ++Index)
		{
			FBarrierPiece Probe;
			Probe.Center = FMath::Lerp(A, B, static_cast<double>(Index) / Intervals);
			Probe.RadiusCm = Rail.RadiusCm;
			Probe.BottomZ = Probe.Center.Z - Rail.SinkCm;
			Probe.TopZ = Probe.BottomZ + Rail.HeightCm;
			Probe.bRail = true;
			Probes.Add(Probe);
		}
		return Probes;
	}

	TArray<FRailSpan> ClearRailSpans(const FRoadFootprint& Road, const FVector& A, const FVector& B, const FRailParams& Rail, int32& OutDropped)
	{
		TArray<FRailSpan> Kept;
		// Pila de trozos por probar: se saca primero la mitad de A, así los trozos conservados salen en orden.
		TArray<FRailSpan, TInlineAllocator<16>> Pending;
		Pending.Add({ A, B });
		while (Pending.Num() > 0)
		{
			const FRailSpan Span = Pending.Pop(EAllowShrinking::No);
			const bool bFits = !RailProbes(Span.A, Span.B, Rail).ContainsByPredicate([&Road](const FBarrierPiece& Probe)
			{
				return Road.Intrudes(Probe.Center, Probe.RadiusCm, Probe.BottomZ, Probe.TopZ);
			});
			if (bFits)
			{
				Kept.Add(Span);
				continue;
			}
			if (FVector::Dist2D(Span.A, Span.B) < 2.0 * FMath::Max(1.0, Rail.MinSpanCm))
			{
				++OutDropped;
				continue;
			}
			const FVector Mid = 0.5 * (Span.A + Span.B);
			Pending.Add({ Mid, Span.B });
			Pending.Add({ Span.A, Mid });
		}
		return Kept;
	}
}

void ATN_RallyTrackDressing::CollectRoofIgnoredActors()
{
	RoofIgnoredActors.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->IsA<ATN_RallyTrack>() || (It->GetOwner() && It->GetOwner()->IsA<ATN_RallyTrack>()))
		{
			RoofIgnoredActors.Add(*It);
		}
	}
}

void ATN_RallyTrackDressing::PlaceRail(UStaticMesh* Cube, const FVector& A, const FVector& B, FTNRallyDressingBatches& Batches)
{
	TNRallyDressing::FRailParams Rail;
	Rail.RadiusCm = 0.5 * RailThicknessCm;
	Rail.SinkCm = RailSinkCm;
	Rail.HeightCm = RailHeightCm;
	Rail.MinSpanCm = RailThicknessCm;
	// El carril no se ve y va fuera de la calzada: no se mira el techo (dentro de la pared de un túnel sigue cerrando el paso).
	TArray<TNRallyDressing::FRailSpan> Spans;
	if (RoadFootprint.IsValid())
	{
		Spans = TNRallyDressing::ClearRailSpans(*RoadFootprint, A, B, Rail, BlockedRailCount);
	}
	else
	{
		Spans.Add({ A, B });
	}
	for (const TNRallyDressing::FRailSpan& Span : Spans)
	{
		BarrierPieces.Append(TNRallyDressing::RailProbes(Span.A, Span.B, Rail));
		AddRailSegment(Cube, Span.A, Span.B, Batches);
	}
}

bool ATN_RallyTrackDressing::TryPlaceBarrierPieces(TConstArrayView<TNRallyDressing::FBarrierPiece> Pieces, double YawDeg, int32 Side)
{
	for (const TNRallyDressing::FBarrierPiece& Piece : Pieces)
	{
		if (RoadFootprint.IsValid() && RoadFootprint->Intrudes(Piece.Center, Piece.RadiusCm, Piece.BottomZ, Piece.TopZ))
		{
			++BlockedOnRoadCount;
			return false;
		}
		// El carril no se ve y va fuera de la calzada: dentro de la pared de un túnel no estorba y sigue cerrando el paso.
		if (!Piece.bRail && IsUnderRoof(Piece.Center, Piece.RadiusCm, Piece.TopZ, YawDeg, Side))
		{
			++BlockedUnderRoofCount;
			return false;
		}
	}
	BarrierPieces.Append(Pieces.GetData(), Pieces.Num());
	return true;
}

bool ATN_RallyTrackDressing::IsUnderRoof(const FVector& Center, double RadiusCm, double TopZ, double YawDeg, int32 Side) const
{
	using namespace TNRallyDressingClearance;
	const UWorld* World = GetWorld();
	if (!World || RoofProbeCm <= 0.f)
	{
		return false;
	}
	// Desde la pieza hacia la calzada: la pila puede quedar metida en la pared del túnel, pero entre ella y la calzada hay aire
	// hasta la bóveda. También un poco antes y después, para la boca.
	const FVector Along = FRotator(0.0, YawDeg, 0.0).Vector();
	const FVector Inward = FVector(-Along.Y, Along.X, 0.0) * -TNRallyDressing::SideSign(Side);
	const FVector Near = FVector(Center.X, Center.Y, TopZ) + Inward * (RadiusCm + RoofInwardCm);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RallyDressingRoof), true, this);
	for (const TWeakObjectPtr<const AActor>& Ignored : RoofIgnoredActors)
	{
		if (const AActor* Actor = Ignored.Get())
		{
			Params.AddIgnoredActor(Actor);
		}
	}
	// Y sobre la propia pieza: bajo el labio de la boca, o metida en la pared del túnel.
	const FVector Own(Center.X, Center.Y, TopZ);
	for (FVector Probe : { Own, Near, Near + Along * RoofAlongCm, Near - Along * RoofAlongCm })
	{
		// Desde encima de la calzada (en un salto, su rampa sube por encima de la pila) para no empezar dentro del suelo.
		const double RoadZ = RoadFootprint.IsValid() ? RoadFootprint->RoadZAt(Probe, TopZ, TopZ) : TopZ;
		Probe.Z = FMath::Max(TopZ, RoadZ) + RoofProbeLiftCm;
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, Probe, Probe + FVector(0.0, 0.0, RoofProbeCm), FCollisionObjectQueryParams(ECC_WorldStatic),
			Params) && Hit.ImpactPoint.Z - Probe.Z >= RoofMinGapCm)
		{
			return true;
		}
	}
	return false;
}
