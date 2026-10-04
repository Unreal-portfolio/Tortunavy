#include "Kart/TN_KartTrack.h"

#include "Algo/BinarySearch.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Rally/TN_RallyGate.h"
#include "Kart/TN_KartItemBox.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "World/ProcMap/TN_ProcMapLayout.h"

namespace TNKart
{
	namespace
	{
		/** Lo que se hunde cada puerta bajo el suelo (cm). */
		constexpr double GateSinkCm = 200.0;
		/** La meta, como poco esto por encima del mar (cm): en la arena seca, no en la orilla. */
		constexpr double FinishAboveSeaCm = 40.0;
		/** Altura de las cajas sobre el suelo (cm). */
		constexpr double ItemBoxLiftCm = 90.0;
	}

	FRoutePlanParams MakePlanParams(double SeaLevelZ)
	{
		FRoutePlanParams Params;
		Params.MinFinishZ = SeaLevelZ + FinishAboveSeaCm;
		// Un punto del eje por muestra del camino (4 m): la spline sigue el camino de verdad en las cuevas y las curvas
		// cerradas, sin recortarlas contra las paredes.
		Params.RoadStepCm = 400.0;
		return Params;
	}

	TArray<FRouteSample> RouteSamplesFrom(const TArray<FTNProcPathPoint>& Points)
	{
		using namespace TNProcMap;
		// Donde no cabe o no se vería el arco de una puerta: cuevas, tableros, torres, toboganes, géiseres, isletas,
		// pasarelas, huecos y puentes del río.
		constexpr uint32 NoGate = PathFlags::Tunnel | PathFlags::Elevated | PathFlags::Colossal | PathFlags::TowerTop | PathFlags::UnderTower
			| PathFlags::Slide | PathFlags::GeyserBase | PathFlags::Islet | PathFlags::Boardwalk | PathFlags::Gap | PathFlags::RiverCross;
		// Agua, cascadas, géiseres y huecos: tampoco cerca (el kart sale del agua por debajo del arco o llega volando).
		constexpr uint32 Away = PathFlags::Islet | PathFlags::Slide | PathFlags::GeyserBase | PathFlags::CliffUp | PathFlags::Gap
			| PathFlags::RiverCross;
		TArray<FRouteSample> Samples;
		Samples.Reserve(Points.Num());
		TArray<double> Arc;
		Arc.Reserve(Points.Num());
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const FTNProcPathPoint& Point = Points[Index];
			FRouteSample& Sample = Samples.AddDefaulted_GetRef();
			Sample.Location = Point.Location;
			Sample.WidthCm = Point.Width;
			Sample.bNoGate = (Point.Flags & NoGate) != 0;
			Sample.bShore = (Point.Flags & PathFlags::Shore) != 0;
			Arc.Add(Index > 0 ? Arc.Last() + FVector::Dist(Points[Index - 1].Location, Point.Location) : 0.0);
		}
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			if ((Points[Index].Flags & Away) == 0)
			{
				continue;
			}
			for (int32 Other = Index - 1; Other >= 0 && Arc[Index] - Arc[Other] <= GateAwayFromHazardCm; --Other)
			{
				Samples[Other].bNoGate = true;
			}
			for (int32 Other = Index + 1; Other < Points.Num() && Arc[Other] - Arc[Index] <= GateAwayFromHazardCm; ++Other)
			{
				Samples[Other].bNoGate = true;
			}
		}
		return Samples;
	}

	void BlockGatesNearObstacles(TArray<FRouteSample>& Samples, const TArray<FLineObstacle>& Obstacles, double ClearCm)
	{
		for (FRouteSample& Sample : Samples)
		{
			const FVector2D Point(Sample.Location);
			for (const FLineObstacle& Obstacle : Obstacles)
			{
				if (!Obstacle.bKeepCentered && FVector2D::Distance(Point, Obstacle.Center) <= Obstacle.RadiusCm + ClearCm)
				{
					Sample.bNoGate = true;
					break;
				}
			}
		}
	}
}

ATN_KartTrack::ATN_KartTrack()
{
	// Un punto de la spline por muestra del camino (4 m): no recorta las curvas cerradas ni las cuevas.
	RoadSampleStepCm = 400.f;
	// Sin cajas de munición del Rally: las de objetos las pone esta pista.
	AmmoBoxesPerRow = 0;
	ItemBoxClass = ATN_KartItemBox::StaticClass();
}

void ATN_KartTrack::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearItemBoxes();
	Super::EndPlay(EndPlayReason);
}

void ATN_KartTrack::ClearItemBoxes()
{
	for (ATN_KartItemBox* Box : ItemBoxes)
	{
		if (IsValid(Box))
		{
			Box->Destroy();
		}
	}
	ItemBoxes.Reset();
}

double ATN_KartTrack::GetRoadHalfWidthAtArc(double Arc) const
{
	if (RoadArcs.Num() < 2 || RoadHalfWidths.Num() != RoadArcs.Num())
	{
		return 700.0;
	}
	// La spline va por la línea del piloto: su largo difiere un poco del eje; se lleva el arco a la escala del eje.
	const double SplineLength = FMath::Max(1.0, static_cast<double>(GetTrackLengthCm()));
	const double PlanArc = FMath::Clamp(Arc * RoadArcs.Last() / SplineLength, 0.0, RoadArcs.Last());
	const int32 Upper = FMath::Clamp(Algo::UpperBound(RoadArcs, PlanArc), 1, RoadArcs.Num() - 1);
	const double Span = FMath::Max(1.0, RoadArcs[Upper] - RoadArcs[Upper - 1]);
	return FMath::Lerp(RoadHalfWidths[Upper - 1], RoadHalfWidths[Upper], FMath::Clamp((PlanArc - RoadArcs[Upper - 1]) / Span, 0.0, 1.0));
}

bool ATN_KartTrack::BuildFromMap(ATN_ProcMapGenerator& Generator)
{
	ClearItemBoxes();
	MapGeneration = 0;
	if (!Generator.IsMapReady())
	{
		return false;
	}
	TArray<FTNProcPathPoint> Points;
	Generator.GetMainPathWorld(Points);
	const double SeaZ = Generator.GetSeaLevelWorldZ();

	TArray<FVector4> Raw;
	Generator.GetMainPathObstaclesWorld(Raw);
	TArray<TNKart::FLineObstacle> Obstacles;
	Obstacles.Reserve(Raw.Num());
	for (const FVector4& Obstacle : Raw)
	{
		Obstacles.Add({ FVector2D(Obstacle.X, Obstacle.Y), FMath::Abs(Obstacle.W), Obstacle.W < 0.0 });
	}

	TArray<TNKart::FRouteSample> Samples = TNKart::RouteSamplesFrom(Points);
	TNKart::BlockGatesNearObstacles(Samples, Obstacles, GateObstacleClearCm);
	const TNKart::FRoutePlan Plan = TNKart::PlanRouteFromPath(Samples, TNKart::MakePlanParams(SeaZ));
	if (!Plan.bValid)
	{
		UE_LOG(LogTNRally, Error, TEXT("[KartTrack] El camino del mapa (%d muestras) no da para una pista."), Points.Num());
		return false;
	}

	// Línea del piloto IA: alrededor de los obstáculos y centrada en las puertas (la parrilla va sobre ella).
	TArray<TNKart::FLineObstacle> LineObstacles = Obstacles;
	for (const TNRally::FGateDef& Gate : Plan.Gates)
	{
		LineObstacles.Add({ FVector2D(Gate.Location), static_cast<double>(GateCenteredRadiusCm), true });
	}
	TArray<double> HalfWidths;
	HalfWidths.Reserve(Plan.RoadWidthCm.Num());
	for (const double Width : Plan.RoadWidthCm)
	{
		HalfWidths.Add(0.5 * Width);
	}
	const TArray<double> Offsets = TNKart::PlanRacingLineOffsets(Plan.Road, HalfWidths, LineObstacles, LineClearanceCm, LineRampCm);
	const TArray<FVector> Line = TNKart::OffsetRoad(Plan.Road, Offsets);

	// Cada puerta, algo hundida en el suelo: su volumen (10 m de alto desde la base) recoge al kart aunque el suelo real quede
	// un poco por debajo de la cota del camino (bocas de cueva, orillas) o salga de un salto.
	TArray<TNRally::FGateDef> GateDefs = Plan.Gates;
	for (TNRally::FGateDef& Gate : GateDefs)
	{
		Gate.Location.Z = FMath::Min(Gate.Location.Z, static_cast<double>(Generator.GetTerrainHeightAt(Gate.Location))) - TNKart::GateSinkCm;
	}
	if (!BuildFromGates(GateDefs, false, Line, 0.0))
	{
		return false;
	}
	RoadArcs = Plan.RoadArcCm;
	RoadHalfWidths = HalfWidths;
	if (UE_LOG_ACTIVE(LogTNRally, Verbose))
	{
		for (const TNKart::FLineObstacle& Obstacle : LineObstacles)
		{
			const FVector Center(Obstacle.Center, 0.0);
			const double Arc = FindArcGlobal(Center);
			UE_LOG(LogTNRally, Verbose, TEXT("[KartTrack] Obstáculo%s en (%.0f, %.0f), arco %.0f m, radio %.1f m; la línea pasa a %.1f m."),
				Obstacle.bKeepCentered ? TEXT(" centrado") : TEXT(""), Obstacle.Center.X, Obstacle.Center.Y, Arc / 100.0,
				Obstacle.RadiusCm / 100.0, FVector::Dist2D(Center, GetLocationAtArc(Arc)) / 100.0);
		}
	}
	BuildGateWings(Plan, Generator);
	MapGeneration = Generator.GetBuiltGeneration();

	// Las cajas se replican: solo las crea el servidor (la pista no se replica, así que HasAuthority no sirve aquí).
	if (GetNetMode() != NM_Client)
	{
		SpawnItemRows(Plan, Plan.GateArcCm.Num() > 0 ? Plan.GateArcCm[0] : 0.0, Generator);
	}

	// Validación del camino para el kart (en el log): las rampas más empinadas y el paso más estrecho.
	double MaxSlope = 0.0;
	double MaxSlopeArc = 0.0;
	double MinWidth = TNumericLimits<double>::Max();
	double MinWidthArc = 0.0;
	for (int32 Index = 1; Index < Plan.Road.Num(); ++Index)
	{
		const double Run = FMath::Max(1.0, FVector::Dist2D(Plan.Road[Index], Plan.Road[Index - 1]));
		const double Slope = FMath::Abs(Plan.Road[Index].Z - Plan.Road[Index - 1].Z) / Run;
		if (Slope > MaxSlope) { MaxSlope = Slope; MaxSlopeArc = Plan.RoadArcCm[Index]; }
		if (Plan.RoadWidthCm[Index] < MinWidth) { MinWidth = Plan.RoadWidthCm[Index]; MinWidthArc = Plan.RoadArcCm[Index]; }
	}
	// Lo que hay por el camino para el kart (#293): géiseres, cascadas y metros de agua.
	int32 GeyserCount = 0;
	int32 SlideCount = 0;
	int32 WaterSamples = 0;
	uint32 PreviousFlags = 0;
	double PathArc = 0.0;
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		const FTNProcPathPoint& Point = Points[Index];
		PathArc += Index > 0 ? FVector::Dist(Points[Index - 1].Location, Point.Location) : 0.0;
		constexpr uint32 Watched = TNProcMap::PathFlags::GeyserBase | TNProcMap::PathFlags::Slide | TNProcMap::PathFlags::CliffUp
			| TNProcMap::PathFlags::Islet | TNProcMap::PathFlags::Tunnel | TNProcMap::PathFlags::Portal;
		if ((Point.Flags & Watched) != (PreviousFlags & Watched) || (Index > 0 && FMath::Abs(Point.Location.Z - Points[Index - 1].Location.Z) > 150.0))
		{
			UE_LOG(LogTNRally, Verbose, TEXT("[KartTrack] Camino a %.0f m: cota %.0f, ancho %.1f m, banderas %u."), PathArc / 100.0,
				Point.Location.Z, Point.Width / 100.0, Point.Flags);
		}
		GeyserCount += (Point.Flags & TNProcMap::PathFlags::GeyserBase) != 0 && (PreviousFlags & TNProcMap::PathFlags::GeyserBase) == 0 ? 1 : 0;
		SlideCount += (Point.Flags & TNProcMap::PathFlags::Slide) != 0 && (PreviousFlags & TNProcMap::PathFlags::Slide) == 0 ? 1 : 0;
		WaterSamples += (Point.Flags & TNProcMap::PathFlags::Islet) != 0 ? 1 : 0;
		PreviousFlags = Point.Flags;
	}
	UE_LOG(LogTNRally, Log, TEXT("[KartTrack] Mapa %d: %.2f km, %d puertas, %d cajas, %d obstáculos rodeados, %d géiseres, %d cascadas, %d muestras de agua; pendiente máxima %.0f %% en %.0f m, paso más estrecho %.1f m en %.0f m."),
		MapGeneration, GetTrackLengthCm() / 100000.0, GetGateCount(), ItemBoxes.Num(), Obstacles.Num(), GeyserCount, SlideCount, WaterSamples,
		100.0 * MaxSlope, MaxSlopeArc / 100.0, MinWidth / 100.0, MinWidthArc / 100.0);
	return true;
}

void ATN_KartTrack::SpawnItemRows(const TNKart::FRoutePlan& Plan, double LineStartArc, const ATN_ProcMapGenerator& Generator)
{
	UWorld* World = GetWorld();
	UClass* Class = ItemBoxClass ? ItemBoxClass.Get() : ATN_KartItemBox::StaticClass();
	if (!World || Plan.Road.Num() < 2)
	{
		return;
	}
	const TArray<double> Rows = TNKart::PlanItemRowArcs(LineStartArc, Plan.FinishArcCm - ItemRowMinFromGateCm, Plan.GateArcCm,
		ItemRowSpacingCm, ItemRowMinFromGateCm);
	for (const double RowArc : Rows)
	{
		const int32 Upper = FMath::Clamp(Algo::UpperBound(Plan.RoadArcCm, RowArc), 1, Plan.Road.Num() - 1);
		const double Span = FMath::Max(1.0, Plan.RoadArcCm[Upper] - Plan.RoadArcCm[Upper - 1]);
		const double T = FMath::Clamp((RowArc - Plan.RoadArcCm[Upper - 1]) / Span, 0.0, 1.0);
		const FVector Center = FMath::Lerp(Plan.Road[Upper - 1], Plan.Road[Upper], T);
		const double Width = FMath::Lerp(Plan.RoadWidthCm[Upper - 1], Plan.RoadWidthCm[Upper], T);
		const FVector Direction = (Plan.Road[Upper] - Plan.Road[Upper - 1]).GetSafeNormal2D();
		const FVector Right(-Direction.Y, Direction.X, 0.0);
		const int32 Boxes = TNKart::ItemBoxesForWidth(Width);
		const double Spacing = TNKart::ItemLateralSpacingCm(Width, Boxes);
		for (int32 Slot = 0; Slot < Boxes; ++Slot)
		{
			const FVector OnRow = Center + Right * ((Slot - 0.5 * (Boxes - 1)) * Spacing);
			FActorSpawnParameters Params;
			Params.Owner = this;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			// Suelo sin trazas (la colisión del terreno puede estar aún cocinándose); sobre el agua, a flor de agua.
			const double GroundZ = FMath::Max(static_cast<double>(Generator.GetTerrainHeightAt(OnRow)), static_cast<double>(Generator.GetSeaLevelWorldZ()));
			const FVector Ground(OnRow.X, OnRow.Y, GroundZ);
			if (ATN_KartItemBox* Box = World->SpawnActor<ATN_KartItemBox>(Class, Ground + FVector(0.0, 0.0, TNKart::ItemBoxLiftCm),
				FRotator(0.0, Direction.Rotation().Yaw, 0.0), Params))
			{
				ItemBoxes.Add(Box);
			}
		}
	}
}

void ATN_KartTrack::BuildGateWings(const TNKart::FRoutePlan& Plan, const ATN_ProcMapGenerator& Generator)
{
	if (!Borders || !BorderMesh)
	{
		return;
	}
	Borders->SetStaticMesh(BorderMesh);
	const FBoxSphereBounds Bounds = BorderMesh->GetBounds();
	const FVector Extent = Bounds.BoxExtent.ComponentMax(FVector(1.0));
	const FVector Scale(BorderSizeCm.X / (2.0 * Extent.X), BorderSizeCm.Y / (2.0 * Extent.Y), BorderSizeCm.Z / (2.0 * Extent.Z));
	const double GateHalf = 0.5 * ATN_RallyGate::WidthCm;
	const double Step = FMath::Max(60.0, static_cast<double>(GateWingSpacingCm));
	TArray<FTransform> Rocks;
	for (int32 Index = 0; Index < Plan.Gates.Num() && Index < Plan.GateArcCm.Num(); ++Index)
	{
		const FVector Center = Plan.Gates[Index].Location;
		const FVector Direction = FRotator(0.0, Plan.Gates[Index].YawDeg, 0.0).Vector();
		const FVector Right(-Direction.Y, Direction.X, 0.0);
		// Ancho del camino en la puerta (en el eje del plan).
		const int32 Upper = FMath::Clamp(Algo::UpperBound(Plan.RoadArcCm, Plan.GateArcCm[Index]), 1, Plan.Road.Num() - 1);
		const double HalfRoad = 0.5 * FMath::Max(Plan.RoadWidthCm[Upper - 1], Plan.RoadWidthCm[Upper]);
		const double Wing = FMath::Clamp(HalfRoad + 400.0 - GateHalf, static_cast<double>(GateWingMinCm), static_cast<double>(GateWingMaxCm));
		for (const double Side : { -1.0, 1.0 })
		{
			for (double Lateral = GateHalf + 0.5 * BorderSizeCm.Y; Lateral <= GateHalf + Wing; Lateral += Step)
			{
				const FVector At = Center + Right * (Side * Lateral);
				// Suelo sin trazas (la colisión del terreno puede estar aún cocinándose); algo hundida.
				const FVector Ground(At.X, At.Y, Generator.GetTerrainHeightAt(At) + 0.35 * BorderSizeCm.Z);
				const FRotator Turn(0.0, Plan.Gates[Index].YawDeg + 37.0 * Lateral, 0.0);
				Rocks.Add(FTransform(Turn, Ground - Turn.RotateVector(Bounds.Origin * Scale), Scale));
			}
		}
	}
	Borders->AddInstances(Rocks, false, true);
}
