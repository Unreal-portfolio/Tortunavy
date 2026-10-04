#include "Rally/TN_RallyTrack.h"

#include "Components/ArrowComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Kart/TN_KartItemBox.h"
#include "Rally/TN_RallyGate.h"
#include "TN_RallyMeshUtils.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Paso de muestreo de la ventana de arco (cm). */
	constexpr double RallyArcStepCm = 200.0;
	/** Distancia tras la puerta a la que aparece una fila de cajas en las puertas pares (cm). */
	constexpr double RallyAmmoAfterGateCm = 1500.0;
	/** Reaparición: unos metros pasada la puerta (la separación entre carriles está en TNRallyRace). */
	constexpr double RallyRespawnAfterGateCm = 400.0;
}

double TNRallyRace::RespawnLaneLateralCm(int32 Lane, double SpacingCm)
{
	switch (FMath::Abs(Lane) % RespawnLaneCount)
	{
	case 1:
		return -SpacingCm;
	case 2:
		return SpacingCm;
	default:
		return 0.0;
	}
}

int32 TNRallyRace::PickFreeRespawnLane(const TArray<FVector>& LaneLocations, const TArray<FVector>& Occupied, double ClearRadiusCm)
{
	int32 Roomiest = INDEX_NONE;
	double RoomiestDistSq = -1.0;
	for (int32 Lane = 0; Lane < LaneLocations.Num(); ++Lane)
	{
		double NearestSq = TNumericLimits<double>::Max();
		for (const FVector& Other : Occupied)
		{
			NearestSq = FMath::Min(NearestSq, FVector::DistSquared(LaneLocations[Lane], Other));
		}
		if (NearestSq >= FMath::Square(ClearRadiusCm))
		{
			return Lane;
		}
		if (NearestSq > RoomiestDistSq)
		{
			RoomiestDistSq = NearestSq;
			Roomiest = Lane;
		}
	}
	return Roomiest;
}

float TNRallyRace::EffectiveGhostSeconds(float LockSeconds, float GhostSeconds)
{
	return FMath::Max(GhostSeconds, FMath::Max(0.f, LockSeconds) + GhostAfterLockSeconds);
}

double TNRallyRace::RestingLiftCm(double OriginAboveBottomCm, double ClearanceCm)
{
	const bool bMeasured = FMath::IsFinite(OriginAboveBottomCm) && OriginAboveBottomCm >= 0.0
		&& OriginAboveBottomCm <= MaxOriginAboveBottomCm;
	return (bMeasured ? OriginAboveBottomCm : FallbackOriginAboveBottomCm) + FMath::Max(0.0, ClearanceCm);
}

ATN_RallyCheckpoint::ATN_RallyCheckpoint()
{
	PrimaryActorTick.bCanEverTick = false;
	SetHidden(true);
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	UArrowComponent* Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Direction"));
	Arrow->SetupAttachment(Root);
	Arrow->ArrowSize = 8.f;
}

ATN_RallyTrack::ATN_RallyTrack()
{
	PrimaryActorTick.bCanEverTick = false;
	// Cada máquina construye su pista: la spline, las puertas y los bordes salen de los mismos datos del manifest.
	bReplicates = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Axis"));
	Spline->SetupAttachment(Root);
	Spline->SetUsingAbsoluteLocation(true);
	Spline->SetUsingAbsoluteRotation(true);
	Spline->SetUsingAbsoluteScale(true);
	Spline->ReparamStepsPerSegment = 50;
	Spline->ClearSplinePoints(true);

	Borders = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Borders"));
	Borders->SetupAttachment(Root);
	Borders->SetUsingAbsoluteLocation(true);
	Borders->SetUsingAbsoluteRotation(true);
	Borders->SetUsingAbsoluteScale(true);
	Borders->SetCollisionProfileName(TEXT("BlockAll"));
	Borders->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> RockFinder(TNRallyMesh::BorderRockPath);
	BorderMesh = RockFinder.Object;

	GateClass = ATN_RallyGate::StaticClass();
	ItemBoxClass = ATN_KartItemBox::StaticClass();
}

void ATN_RallyTrack::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearTrack();
	Super::EndPlay(EndPlayReason);
}

bool ATN_RallyTrack::BuildFromVariant(FName Variant)
{
	return BuildFromManifestFile(Variant.IsNone() ? FString() : TNRally::VariantManifestPath(Variant));
}

bool ATN_RallyTrack::BuildFromManifestFile(const FString& ManifestPath)
{
	FString Text;
	if (ManifestPath.IsEmpty() || !FFileHelper::LoadFileToString(Text, *ManifestPath))
	{
		UE_LOG(LogTNRally, Warning, TEXT("[RallyTrack] Sin manifest (%s): se usan los checkpoints colocados."), *ManifestPath);
		return BuildFromPlacedCheckpoints();
	}
	TNRally::FTrackSource Source;
	FString Error;
	if (!TNRally::ParseTrackManifest(Text, Source, Error))
	{
		UE_LOG(LogTNRally, Error, TEXT("[RallyTrack] Manifest %s no válido: %s."), *ManifestPath, *Error);
		return BuildFromPlacedCheckpoints();
	}
	bool bCircuit = false;
	const TArray<TNRally::FGateDef> GateDefs = TNRally::BuildGateList(Source, bCircuit);
	const bool bOk = BuildFromGates(GateDefs, bCircuit, Source.Road, Source.RoadWidthCm, Source.RoadBankDeg, Source.RoadWidthsCm);
	if (bOk && RoadLengthCm > 0.0)
	{
		Features = TNRallyCircuit::ToFeatureArcs(Source.Elements, RoadLengthCm, GetTrackLengthCm(), bClosed);
	}
	if (bOk && (Features.Num() > 0 || BankSampleDeg.Num() > 0))
	{
		UE_LOG(LogTNRally, Log, TEXT("[RallyTrack] Manifest de circuito: %d elementos, peralte en %d puntos y ancho en %d puntos del eje."),
			Features.Num(), BankSampleDeg.Num(), WidthSampleCm.Num());
	}
	bHasWater = Source.bHasWater;
	WaterZ = Source.WaterZ;
	ManifestLaps = Source.Laps;
	return bOk;
}

bool ATN_RallyTrack::BuildFromPlacedCheckpoints()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	TArray<ATN_RallyCheckpoint*> Placed;
	for (TActorIterator<ATN_RallyCheckpoint> It(World); It; ++It)
	{
		Placed.Add(*It);
	}
	Placed.Sort([](const ATN_RallyCheckpoint& A, const ATN_RallyCheckpoint& B) { return A.Order < B.Order; });
	bool bCircuit = true;
	TArray<TNRally::FGateDef> GateDefs;
	for (const ATN_RallyCheckpoint* Checkpoint : Placed)
	{
		TNRally::FGateDef Def;
		Def.Location = Checkpoint->GetActorLocation();
		Def.YawDeg = Checkpoint->GetActorRotation().Yaw;
		GateDefs.Add(Def);
		bCircuit &= !Checkpoint->bIsFinish;
	}
	bHasWater = false;
	ManifestLaps = 0;
	return BuildFromGates(GateDefs, bCircuit);
}

bool ATN_RallyTrack::BuildFromGates(const TArray<TNRally::FGateDef>& GateDefs, bool bCircuit, const TArray<FVector>& RoadAxis,
	double RoadWidthCm, TConstArrayView<double> RoadBankDeg, TConstArrayView<double> RoadWidthsCm)
{
	ClearTrack();
	if (GateDefs.Num() < 2)
	{
		UE_LOG(LogTNRally, Error, TEXT("[RallyTrack] Hacen falta al menos 2 puertas (hay %d): sin pista."), GateDefs.Num());
		return false;
	}
	bClosed = bCircuit;
	ManifestRoadWidthCm = FMath::Max(0.0, RoadWidthCm);
	ActiveBorderOffsetCm = RoadWidthCm > 0.0 ? 0.5 * RoadWidthCm + BorderOutsideRoadCm : BorderOffsetCm;
	if (RoadAxis.Num() >= 2)
	{
		BuildSplineFromRoad(RoadAxis, GateDefs);
		const TArray<double> PointArcs = TNRallyCircuit::RoadPointArcs(RoadAxis, bClosed, GetTrackLengthCm(), RoadLengthCm);
		if (RoadBankDeg.Num() == RoadAxis.Num())
		{
			BankSampleArcs = PointArcs;
			BankSampleDeg = TArray<double>(RoadBankDeg);
		}
		if (RoadWidthsCm.Num() == RoadAxis.Num())
		{
			WidthSampleArcs = PointArcs;
			WidthSampleCm = TArray<double>(RoadWidthsCm);
		}
	}
	else
	{
		BuildSpline(GateDefs);
	}
	SpawnGates(GateDefs);
	AmmoRowArcs = TNRally::AmmoRowArcs(GateArcs, GetTrackLengthCm(), bClosed, RallyAmmoAfterGateCm, NoAmmoBeforeFinishCm);
	// Los límites del carril los pone ATN_RallyTrackDressing (PrepareTrack); aquí ya no hay rocas con colisión.
	// Las cajas se replican: solo las crea el servidor. La pista no se replica (cada máquina construye la suya), así que en un
	// cliente HasAuthority() es true y no sirve para distinguirlo.
	if (GetNetMode() != NM_Client)
	{
		SpawnAmmoRows();
	}
	bBuilt = true;
	UE_LOG(LogTNRally, Log, TEXT("[RallyTrack] Pista: %d puertas, %.0f m, %s, eje %s, %d cajas."),
		GateArcs.Num(), GetTrackLengthCm() / 100.0, bClosed ? TEXT("circuito") : TEXT("punto a punto"),
		RoadAxis.Num() >= 2 ? TEXT("de road_uu") : TEXT("por las puertas"), ItemBoxes.Num());
	return true;
}

void ATN_RallyTrack::ClearItemBoxes()
{
	for (ATN_KartItemBox* Box : ItemBoxes)
	{
		if (IsValid(Box)) { Box->Destroy(); }
	}
	ItemBoxes.Reset();
}

void ATN_RallyTrack::ClearTrack()
{
	for (ATN_RallyGate* Gate : Gates)
	{
		if (IsValid(Gate)) { Gate->Destroy(); }
	}
	ClearItemBoxes();
	Gates.Reset();
	GateArcs.Reset();
	AmmoRowArcs.Reset();
	BankSampleArcs.Reset();
	BankSampleDeg.Reset();
	WidthSampleArcs.Reset();
	WidthSampleCm.Reset();
	Features.Reset();
	RoadLengthCm = 0.0;
	if (Borders) { Borders->ClearInstances(); }
	if (Spline) { Spline->ClearSplinePoints(true); }
	bBuilt = false;
}

void ATN_RallyTrack::BuildSpline(const TArray<TNRally::FGateDef>& GateDefs)
{
	const int32 Num = GateDefs.Num();
	Spline->ClearSplinePoints(false);
	for (const TNRally::FGateDef& Def : GateDefs)
	{
		Spline->AddSplinePoint(Def.Location, ESplineCoordinateSpace::World, false);
	}
	Spline->SetClosedLoop(bClosed, false);
	Spline->UpdateSpline();

	// Tangentes con el rumbo de cada puerta (el del manifest) y módulo de la cuerda media: la spline pasa por las puertas
	// en la dirección de la calzada en vez de recortar las curvas. La pendiente se queda la automática.
	for (int32 Index = 0; Index < Num; ++Index)
	{
		const int32 Prev = Index > 0 ? Index - 1 : (bClosed ? Num - 1 : Index);
		const int32 Next = Index < Num - 1 ? Index + 1 : (bClosed ? 0 : Index);
		const double ChordPrev = FVector::Dist(GateDefs[Index].Location, GateDefs[Prev].Location);
		const double ChordNext = FVector::Dist(GateDefs[Index].Location, GateDefs[Next].Location);
		const double Magnitude = (ChordPrev > 0.0 && ChordNext > 0.0) ? 0.5 * (ChordPrev + ChordNext) : FMath::Max(ChordPrev, ChordNext);
		const FVector Auto = Spline->GetTangentAtSplinePoint(Index, ESplineCoordinateSpace::World);
		FVector Tangent = FRotator(0.0, GateDefs[Index].YawDeg, 0.0).Vector() * Magnitude;
		Tangent.Z = Auto.Z;
		Spline->SetTangentAtSplinePoint(Index, Tangent, ESplineCoordinateSpace::World, false);
	}
	Spline->UpdateSpline();

	GateArcs.SetNum(Num);
	for (int32 Index = 0; Index < Num; ++Index)
	{
		GateArcs[Index] = Spline->GetDistanceAlongSplineAtSplinePoint(Index);
	}
}

void ATN_RallyTrack::BuildSplineFromRoad(const TArray<FVector>& Road, const TArray<TNRally::FGateDef>& GateDefs)
{
	const TArray<FVector> Points = TNRally::DownsampleRoad(Road, RoadSampleStepCm, bClosed);
	Spline->ClearSplinePoints(false);
	for (const FVector& Point : Points)
	{
		Spline->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
	}
	Spline->SetClosedLoop(bClosed, false);
	Spline->UpdateSpline();

	// Cada puerta se proyecta en la calzada en orden, buscando solo por delante de la anterior (lazos y niveles).
	const double Length = GetTrackLengthCm();
	const USplineComponent* Axis = Spline;
	auto PositionAt = [Axis](double Arc) { return Axis->GetLocationAtDistanceAlongSpline(Arc, ESplineCoordinateSpace::World); };
	GateArcs.SetNum(GateDefs.Num());
	double Previous = 0.0;
	for (int32 Index = 0; Index < GateDefs.Num(); ++Index)
	{
		const bool bFirst = Index == 0;
		const double Chord = bFirst ? Length : FVector::Dist(GateDefs[Index].Location, GateDefs[Index - 1].Location);
		const double Ahead = bFirst ? Length : FMath::Min<double>(Length, 3.0 * Chord + 10000.0);
		GateArcs[Index] = TNRally::FindArcInWindow(PositionAt, Length, bClosed, GateDefs[Index].Location, Previous,
			bFirst ? 0.0 : 1000.0, Ahead, RallyArcStepCm);
		Previous = GateArcs[Index];
	}
}

void ATN_RallyTrack::SpawnGates(const TArray<TNRally::FGateDef>& GateDefs)
{
	UWorld* World = GetWorld();
	UClass* Class = GateClass ? GateClass.Get() : ATN_RallyGate::StaticClass();
	for (int32 Index = 0; Index < GateDefs.Num(); ++Index)
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.ObjectFlags |= RF_Transient;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// Con peralte, la puerta se inclina con la calzada: si no, quien va por el lado bajo de la curva pasa por debajo de su
		// volumen y la puerta no cuenta (#622).
		const FRotator Rotation = TNRallyCircuit::GateRotation(GetDirectionAtArc(GateArcs[Index]).Rotation().Yaw,
			GetBankDegAtArc(GateArcs[Index]));
		ATN_RallyGate* Gate = World->SpawnActor<ATN_RallyGate>(Class, GateDefs[Index].Location, Rotation, Params);
		if (!Gate)
		{
			continue;
		}
		const bool bFinish = bClosed ? Index == 0 : Index == GateDefs.Num() - 1;
		Gate->Configure(Index, bFinish || Index == 0);
		Gates.Add(Gate);
	}
}

void ATN_RallyTrack::SpawnAmmoRows()
{
	for (const double Arc : AmmoRowArcs)
	{
		SpawnAmmoRow(Arc);
	}
}

void ATN_RallyTrack::SpawnAmmoRow(double Arc)
{
	UWorld* World = GetWorld();
	UClass* Class = ItemBoxClass ? ItemBoxClass.Get() : ATN_KartItemBox::StaticClass();
	// En un tramo estrecho la fila se aprieta para que ninguna caja quede fuera de la calzada (#622).
	const double Spacing = TNRallyCircuit::RowLateralSpacingCm(AmmoLateralSpacingCm, GetRoadWidthAtArcCm(Arc), AmmoBoxesPerRow);
	for (int32 Slot = 0; Slot < AmmoBoxesPerRow; ++Slot)
	{
		const double Lateral = (Slot - 0.5 * (AmmoBoxesPerRow - 1)) * Spacing;
		const FVector OnAxis = OffsetAtArc(Arc, Lateral);
		FVector Ground = OnAxis;
		TraceGround(OnAxis, 400.0, 1500.0, Ground);
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATN_KartItemBox* Box = World->SpawnActor<ATN_KartItemBox>(Class, Ground + FVector(0.0, 0.0, ItemBoxLiftCm),
			FRotator(0.0, GetDirectionAtArc(Arc).Rotation().Yaw, 0.0), Params);
		if (Box)
		{
			ItemBoxes.Add(Box);
		}
	}
}

bool ATN_RallyTrack::TraceGround(const FVector& Location, double UpCm, double DownCm, FVector& OutGround) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RallyTrackGround), true, this);
	for (const ATN_RallyGate* Gate : Gates)
	{
		Params.AddIgnoredActor(Gate);
	}
	FHitResult Hit;
	if (!World->LineTraceSingleByObjectType(Hit, Location + FVector(0.0, 0.0, UpCm), Location - FVector(0.0, 0.0, DownCm),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		return false;
	}
	OutGround = Hit.ImpactPoint;
	return true;
}

FVector ATN_RallyTrack::OffsetAtArc(double Arc, double LateralCm) const
{
	const FVector Direction = GetDirectionAtArc(Arc);
	const FVector Right = FVector(-Direction.Y, Direction.X, 0.0).GetSafeNormal();
	return GetLocationAtArc(Arc) + Right * LateralCm;
}

float ATN_RallyTrack::GetTrackLengthCm() const
{
	return Spline && Spline->GetNumberOfSplinePoints() > 1 ? Spline->GetSplineLength() : 0.f;
}

int32 ATN_RallyTrack::GetBorderInstanceCount() const
{
	return Borders ? Borders->GetInstanceCount() : 0;
}

double ATN_RallyTrack::GetGateArc(int32 GateIndex) const
{
	return GateArcs.IsValidIndex(GateIndex) ? GateArcs[GateIndex] : 0.0;
}

double ATN_RallyTrack::GetArcBetweenGates(int32 FromGate, int32 ToGate) const
{
	return TNRally::ForwardArc(GetGateArc(FromGate), GetGateArc(ToGate), GetTrackLengthCm(), bClosed);
}

FTransform ATN_RallyTrack::GetGateCrossingTransform(int32 GateIndex) const
{
	if (Gates.IsValidIndex(GateIndex) && IsValid(Gates[GateIndex]))
	{
		return Gates[GateIndex]->GetCrossingTransform();
	}
	const double Arc = GetGateArc(GateIndex);
	const FQuat Rotation = TNRallyCircuit::GateRotation(GetDirectionAtArc(Arc).Rotation().Yaw, GetBankDegAtArc(Arc)).Quaternion();
	return FTransform(Rotation, GetLocationAtArc(Arc) + Rotation.RotateVector(ATN_RallyGate::CrossingCenterOffset()));
}

double ATN_RallyTrack::GetBankDegAtArc(double Arc) const
{
	return TNRallyCircuit::BankAtArc(BankSampleArcs, BankSampleDeg, Arc, GetTrackLengthCm(), bClosed);
}

double ATN_RallyTrack::GetRoadWidthAtArcCm(double Arc) const
{
	return TNRallyCircuit::RoadWidthAtArc(WidthSampleArcs, WidthSampleCm, Arc, GetTrackLengthCm(), bClosed, ManifestRoadWidthCm);
}

FVector ATN_RallyTrack::GetGateHalfExtent() const
{
	return ATN_RallyGate::CrossingHalfExtent();
}

FTransform ATN_RallyTrack::GetGridSlotTransform(int32 Slot, double LiftCm) const
{
	const FVector2D Offset = TNRally::GridSlotOffset(Slot);
	const double Arc = GetGateArc(0) - Offset.X;
	FVector Location;
	FVector Direction;
	if (!bClosed && Arc < 0.0)
	{
		Direction = GetDirectionAtArc(0.0);
		Location = GetLocationAtArc(0.0) + Direction * Arc;
	}
	else
	{
		Direction = GetDirectionAtArc(Arc);
		Location = GetLocationAtArc(Arc);
	}
	const FVector Right = FVector(-Direction.Y, Direction.X, 0.0).GetSafeNormal();
	const FVector OnAxis = Location + Right * Offset.Y;
	FVector Ground = OnAxis;
	TraceGround(OnAxis, 400.0, 1500.0, Ground);
	return FTransform(FRotator(0.0, Direction.Rotation().Yaw, 0.0), Ground + FVector(0.0, 0.0, LiftCm));
}

FTransform ATN_RallyTrack::GetRespawnTransform(int32 GateIndex, int32 Lane, double LiftCm) const
{
	const double Arc = TNRally::WrapArc(GetGateArc(GateIndex) + RallyRespawnAfterGateCm, GetTrackLengthCm(), bClosed);
	const FVector Direction = GetDirectionAtArc(Arc);
	const FVector Right = FVector(-Direction.Y, Direction.X, 0.0).GetSafeNormal();
	const FVector OnAxis = GetLocationAtArc(Arc) + Right * TNRallyRace::RespawnLaneLateralCm(Lane);
	FVector Ground = OnAxis;
	TraceGround(OnAxis, 400.0, 1500.0, Ground);
	return FTransform(FRotator(0.0, Direction.Rotation().Yaw, 0.0), Ground + FVector(0.0, 0.0, LiftCm));
}

FTransform ATN_RallyTrack::FindFreeRespawnTransform(int32 GateIndex, const TArray<FVector>& Occupied, double LiftCm, int32* OutLane) const
{
	TArray<FTransform> Lanes;
	TArray<FVector> LaneLocations;
	for (int32 Lane = 0; Lane < TNRallyRace::RespawnLaneCount; ++Lane)
	{
		const FTransform& Candidate = Lanes.Add_GetRef(GetRespawnTransform(GateIndex, Lane, LiftCm));
		LaneLocations.Add(Candidate.GetLocation());
	}
	const int32 Picked = FMath::Max(0, TNRallyRace::PickFreeRespawnLane(LaneLocations, Occupied));
	if (OutLane)
	{
		*OutLane = Picked;
	}
	return Lanes[Picked];
}

double ATN_RallyTrack::FindArcNear(const FVector& Location, double PrevArc) const
{
	const USplineComponent* Axis = Spline;
	return TNRally::FindArcInWindow(
		[Axis](double Arc) { return Axis->GetLocationAtDistanceAlongSpline(Arc, ESplineCoordinateSpace::World); },
		GetTrackLengthCm(), bClosed, Location, PrevArc, TNRally::ArcWindowBehindCm, TNRally::ArcWindowAheadCm, RallyArcStepCm);
}

double ATN_RallyTrack::FindArcGlobal(const FVector& Location) const
{
	if (GetTrackLengthCm() <= 0.f)
	{
		return 0.0;
	}
	const float Key = Spline->FindInputKeyClosestToWorldLocation(Location);
	return Spline->GetDistanceAlongSplineAtSplineInputKey(Key);
}

FVector ATN_RallyTrack::GetLocationAtArc(double Arc) const
{
	return Spline->GetLocationAtDistanceAlongSpline(TNRally::WrapArc(Arc, GetTrackLengthCm(), bClosed), ESplineCoordinateSpace::World);
}

FVector ATN_RallyTrack::GetDirectionAtArc(double Arc) const
{
	return Spline->GetDirectionAtDistanceAlongSpline(TNRally::WrapArc(Arc, GetTrackLengthCm(), bClosed), ESplineCoordinateSpace::World);
}
