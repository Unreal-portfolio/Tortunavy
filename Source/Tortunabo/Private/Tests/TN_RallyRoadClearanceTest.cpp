// Gálibo de la calzada del Rally (#693): ninguna pieza de la barrera (neumáticos, valla o carril de colisión) dentro del ancho de
// la calzada de ningún tramo (road_uu con su ancho por tramo), tampoco la de un paso superior que cae sobre la calzada de debajo,
// ni bajo un techo sobre la calzada (túnel o su boca).
//  - Footprint: TNRallyDressing::FRoadFootprint con trazados montados a mano (ancho por tramos y un cruce a dos alturas, el
//    caso del túnel del trébol), con sus casos negativos.
//  - Plan: la barrera planificada de todos los circuitos del Rally (TNLobbyMission::RallyMapOptions) no entra en la calzada; caso
//    negativo: pegada al borde sin margen, sí entra.
//  - Rail: el carril de un paso superior que entra en la calzada de debajo se parte y solo pierde lo que entra; caso negativo:
//    el tramo entero entra. El gálibo del carril es el buggy con la artillera más 0,5 m (decisión del 04-10): un paso
//    superior a 5 m conserva el carril entero aunque caiga dentro del gálibo de túnel de 6 m de las piezas que se ven.
//  - Built: con el terreno de cada circuito y el decorado construido como en la carrera, ninguna pieza colocada entra en la
//    calzada ni tiene un techo encima, y no se ha quitado ningún trozo del carril de colisión. Caso negativo (#698): el mismo
//    decorado sin el filtro (SetRoadClearanceEnabled(false)) sí deja piezas en la calzada o bajo un techo en algún circuito.
//  - Roof: lo que puede ser techo (CanBeRoof): una estructura sí; el follaje y el decorado, no.
// Plan y Built solo en el editor: Scripts/ no se empaqueta. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Dressing.RoadClear; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Lobby/TN_LobbyMission.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyTrack.h"
#include "Rally/TN_RallyTrackDressing.h"
#include "Vehicles/TN_Buggy.h"
#include "World/TN_MapVariantLoader.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "FoliageInstancedStaticMeshComponent.h"
#include "InstancedFoliageActor.h"
#include "World/Beach/TN_BeachDecorField.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyRoadClearanceTest
{
	using namespace TNRallyDressing;

	/** ATN_RallyTrackDressing por defecto: paso de las muestras, diámetro de un neumático y separación de las pilas. */
	constexpr double SampleStepCm = 400.0;
	constexpr double TireDiameterCm = 120.0;
	constexpr double TireSpacingCm = TireDiameterCm * 1.02;
	/** Alto de una pila de tres neumáticos (cm, holgado). */
	constexpr double StackHeightCm = 150.0;
	/** ATN_RallyTrackDressing::RoofProbeCm por defecto, y aire mínimo bajo un techo (lo que queda más cerca es el suelo). */
	constexpr double RoofProbeCm = 1000.0;
	constexpr double RoofMinGapCm = 150.0;

	/** Los parámetros con los que el actor planifica la barrera (MakeBarrierParams): la pila con su cara interior en el borde. */
	FBarrierParams ActorParams()
	{
		FBarrierParams Params;
		Params.RoadEdgeMarginCm = 0.5 * TireDiameterCm + 10.0;
		return Params;
	}

	/** Añade muestras de From a To (sin To) cada StepCm, con el ancho de calzada WidthCm (0 = el del trazado). */
	void AddLine(FTrackData& Track, const FVector& From, const FVector& To, double WidthCm)
	{
		const double Length = FVector::Dist2D(From, To);
		const int32 Steps = FMath::Max(1, FMath::RoundToInt32(Length / Track.StepCm));
		const FVector Direction = (To - From).GetSafeNormal2D();
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			FAxisSample& Sample = Track.Samples.AddDefaulted_GetRef();
			Sample.Location = FMath::Lerp(From, To, static_cast<double>(Step) / Steps);
			Sample.Direction = Direction;
			Sample.Arc = Track.LengthCm;
			Sample.RoadWidthCm = WidthCm;
			Track.LengthCm += Length / Steps;
		}
	}

	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		explicit FScopedTestWorld(const TCHAR* Name)
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	/** Pilas planificadas (sin terreno: a la cota del borde) que entran en la calzada, y la mayor penetración. */
	int32 PlannedIntrusions(const FTrackData& Data, const FBarrierParams& Params, double& OutWorstCm)
	{
		const FBarrierPlan Plan = PlanBarriers(Data, TArray<uint8>(), Params);
		const FRoadFootprint Road(Data, Params);
		int32 Count = 0;
		OutWorstCm = 0.0;
		for (int32 Side = LeftSide; Side <= RightSide; ++Side)
		{
			for (const TArray<int32>& Run : Plan.Sides[Side].Runs)
			{
				for (const FPolySpot& Spot : ResamplePolyline(RunEdge(Data, Plan.Sides[Side], Run, Side), TireSpacingCm))
				{
					const double Depth = Road.IntrusionCm(Spot.Location, 0.5 * TireDiameterCm, Spot.Location.Z - 20.0, Spot.Location.Z + StackHeightCm);
					OutWorstCm = FMath::Max(OutWorstCm, Depth);
					Count += Depth > Road.GetClearance().ToleranceCm ? 1 : 0;
				}
			}
		}
		return Count;
	}

	/** Lo que incumplen las piezas colocadas de un decorado construido. */
	struct FPieceCheck
	{
		int32 OnRoad = 0;
		int32 UnderRoof = 0;
		int32 Visible = 0;
		double WorstCm = 0.0;
	};

	/** Mide las piezas de la barrera de Dressing contra la calzada (Road) y contra los techos, como el actor (CanBeRoof). */
	FPieceCheck CheckPieces(const UWorld& World, const ATN_RallyTrackDressing& Dressing, const AActor* Track, const FRoadFootprint& Road)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(RallyRoadClearTest), true);
		Query.AddIgnoredActor(&Dressing);
		if (Track)
		{
			Query.AddIgnoredActor(Track);
		}
		FPieceCheck Check;
		for (const FBarrierPiece& Piece : Dressing.GetBarrierPieces())
		{
			const double Depth = Road.IntrusionCm(Piece.Center, Piece.RadiusCm, Piece.BottomZ, Piece.TopZ);
			Check.WorstCm = FMath::Max(Check.WorstCm, Depth);
			Check.OnRoad += Depth > Road.GetClearance().ToleranceCm ? 1 : 0;
			if (Piece.bRail)
			{
				continue;
			}
			++Check.Visible;
			const FVector Top(Piece.Center.X, Piece.Center.Y, Piece.TopZ + 10.0);
			Check.UnderRoof += ATN_RallyTrackDressing::HasRoofAbove(World, Top, RoofProbeCm, RoofMinGapCm, Query) ? 1 : 0;
		}
		return Check;
	}

	/** Losa de Cube (escalada a Scale) centrada en Center, con colisión de mundo estático; de follaje si bFoliage. */
	AActor* SpawnSlab(UWorld& World, UStaticMesh& Cube, bool bFoliage, const FVector& Center, const FVector& Scale)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World.SpawnActor<AActor>(AActor::StaticClass(), FTransform(Center), Params);
		if (!Actor)
		{
			return nullptr;
		}
		UInstancedStaticMeshComponent* Comp = bFoliage ? NewObject<UFoliageInstancedStaticMeshComponent>(Actor)
			: NewObject<UInstancedStaticMeshComponent>(Actor);
		Comp->SetMobility(EComponentMobility::Static);
		Comp->SetStaticMesh(&Cube);
		Comp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Comp->SetWorldTransform(FTransform(Center));
		Actor->SetRootComponent(Comp);
		Comp->RegisterComponent();
		Comp->AddInstance(FTransform(FQuat::Identity, FVector::ZeroVector, Scale), false);
		return Actor;
	}

	/** Trébol: el trazado pasa por debajo (Y = 0, cota 0) y vuelve por encima (X = 2000, cota 800) cruzándose; 14 m de ancho. */
	FTrackData CloverTrack()
	{
		FTrackData Clover;
		Clover.StepCm = SampleStepCm;
		Clover.RoadWidthCm = 1400.0;
		AddLine(Clover, FVector(0.0, 0.0, 0.0), FVector(4000.0, 0.0, 0.0), 0.0);
		AddLine(Clover, FVector(4000.0, 0.0, 0.0), FVector(4000.0, 4000.0, 400.0), 0.0);
		AddLine(Clover, FVector(4000.0, 4000.0, 400.0), FVector(2000.0, 4000.0, 800.0), 0.0);
		AddLine(Clover, FVector(2000.0, 4000.0, 800.0), FVector(2000.0, -4000.0, 800.0), 0.0);
		return Clover;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyRoadClearFootprintTest, "Tortunabo.Rally.Dressing.RoadClear.Footprint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyRoadClearFootprintTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyRoadClearanceTest;
	const FBarrierParams Params = ActorParams();
	const double Radius = 0.5 * TireDiameterCm;

	// Recta por +X: los primeros 20 m de 10 m de ancho y los siguientes de 20 m.
	FTrackData Straight;
	Straight.StepCm = SampleStepCm;
	AddLine(Straight, FVector(0.0, 0.0, 0.0), FVector(2000.0, 0.0, 0.0), 1000.0);
	AddLine(Straight, FVector(2000.0, 0.0, 0.0), FVector(4400.0, 0.0, 0.0), 2000.0);
	const FRoadFootprint Road(Straight, Params);
	TestTrue(TEXT("Una pila en medio de la calzada la invade"), Road.Intrudes(FVector(800.0, 0.0, 0.0), Radius, 0.0, StackHeightCm));
	const double Edge = 500.0 + Params.RoadEdgeMarginCm;
	TestEqual(TEXT("Pegada al borde del tramo estrecho no entra"), Road.IntrusionCm(FVector(800.0, Edge, 0.0), Radius, 0.0, StackHeightCm), 0.0);
	TestFalse(TEXT("Pegada al borde del tramo estrecho se puede poner"), Road.Intrudes(FVector(800.0, -Edge, 0.0), Radius, 0.0, StackHeightCm));
	// Caso negativo: a la misma distancia del eje, en el tramo ancho, está dentro de la calzada.
	TestTrue(TEXT("El borde del tramo estrecho cae dentro del tramo ancho"), Road.Intrudes(FVector(3600.0, Edge, 0.0), Radius, 0.0, StackHeightCm));
	TestFalse(TEXT("Por encima del gálibo (un puente sobre la calzada) no cuenta"),
		Road.Intrudes(FVector(800.0, 0.0, 0.0), Radius, 700.0, 700.0 + StackHeightCm));
	TestFalse(TEXT("Por debajo de la calzada no cuenta"), Road.Intrudes(FVector(800.0, 0.0, 0.0), Radius, -400.0, -200.0));

	// Trébol: el trazado pasa por debajo (Y = 0, cota 0) y vuelve por encima (X = 2000, cota 800) cruzándose.
	FTrackData Clover;
	Clover.StepCm = SampleStepCm;
	Clover.RoadWidthCm = 1400.0;
	AddLine(Clover, FVector(0.0, 0.0, 0.0), FVector(4000.0, 0.0, 0.0), 0.0);
	AddLine(Clover, FVector(4000.0, 0.0, 0.0), FVector(4000.0, 4000.0, 400.0), 0.0);
	AddLine(Clover, FVector(4000.0, 4000.0, 400.0), FVector(2000.0, 4000.0, 800.0), 0.0);
	AddLine(Clover, FVector(2000.0, 4000.0, 800.0), FVector(2000.0, -4000.0, 800.0), 0.0);
	const FRoadFootprint Crossing(Clover, Params);
	const FVector UpperEdge(2000.0 + 700.0 + Params.RoadEdgeMarginCm, 0.0, 0.0);
	// Caso negativo (#693): la pila del paso superior que cae a la calzada de debajo tapa la boca del túnel.
	TestTrue(TEXT("La pila del paso superior apoyada en la calzada de debajo la invade"), Crossing.Intrudes(UpperEdge, Radius, 0.0, StackHeightCm));
	TestFalse(TEXT("La pila del paso superior apoyada en su tablero no invade nada"),
		Crossing.Intrudes(UpperEdge + FVector(0.0, 0.0, 800.0), Radius, 800.0, 800.0 + StackHeightCm));
	TestFalse(TEXT("El carril del paso superior queda por encima del gálibo de debajo"),
		Crossing.Intrudes(UpperEdge + FVector(0.0, 0.0, 800.0), Radius, 760.0, 1060.0));
	// La cota de la calzada en el cruce: la del tramo más cercano a la referencia; fuera de toda calzada, la de reserva.
	TestEqual(TEXT("En el cruce, desde abajo, la calzada de debajo"), Crossing.RoadZAt(FVector(2000.0, 0.0, 0.0), 100.0, -1.0), 0.0, 1.0);
	TestEqual(TEXT("En el cruce, desde arriba, el paso superior"), Crossing.RoadZAt(FVector(2000.0, 0.0, 0.0), 900.0, -1.0), 800.0, 1.0);
	TestEqual(TEXT("Fuera de la calzada, la cota de reserva"), Crossing.RoadZAt(FVector(1000.0, 3000.0, 0.0), 0.0, -1.0), -1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyRoadClearRailTest, "Tortunabo.Rally.Dressing.RoadClear.Rail",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyRoadClearRailTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyRoadClearanceTest;
	// El gálibo del carril sale del buggy de verdad: la cabeza de la artillera sentada y la boca de la torreta caben debajo.
	TestTrue(TEXT("El gálibo del carril cubre la cabeza de la artillera"), BuggyWithGunnerHeightCm >= ATN_Buggy::GunnerSeatLocal.Z + 72.0);
	TestTrue(TEXT("El gálibo del carril cubre la boca de la torreta"), BuggyWithGunnerHeightCm >= ATN_Buggy::MuzzleLocal.Z);
	const FRoadClearance Defaults;
	TestEqual(TEXT("Gálibo del carril: buggy con la artillera más 0,5 m"), Defaults.RailAboveCm, BuggyWithGunnerHeightCm + 50.0);
	TestTrue(TEXT("El gálibo del carril es más bajo que el de túnel"), Defaults.RailAboveCm < Defaults.AboveCm);

	// Calzada de debajo por +X, de 10 m de ancho, y un paso superior por +Y a 2,5 m: el fondo de su carril (Z - 40) cae dentro
	// del gálibo del carril (2,5 m) de la calzada de debajo, solo donde la cruza.
	FTrackData Lower;
	Lower.StepCm = SampleStepCm;
	AddLine(Lower, FVector(-4000.0, 0.0, 0.0), FVector(4400.0, 0.0, 0.0), 1000.0);
	const FRoadFootprint Road(Lower, ActorParams());
	const FRailParams Rail;
	const FVector A(0.0, -4000.0, 250.0);
	const FVector B(0.0, 4000.0, 250.0);
	// Caso negativo: el tramo entero entra en la calzada de debajo (antes se quitaba entero y quedaba un agujero de 80 m).
	TestTrue(TEXT("El tramo entero del carril entra en la calzada de debajo"), RailProbes(A, B, Rail).ContainsByPredicate([&Road](const FBarrierPiece& Probe)
	{
		return Road.RailIntrudes(Probe.Center, Probe.RadiusCm, Probe.BottomZ, Probe.TopZ);
	}));
	int32 Dropped = 0;
	const TArray<FRailSpan> Spans = ClearRailSpans(Road, A, B, Rail, Dropped);
	if (!TestTrue(TEXT("Se conserva el carril fuera de la calzada"), Spans.Num() >= 2))
	{
		return false;
	}
	TestTrue(TEXT("Se quita lo que entra en la calzada"), Dropped > 0);
	TestTrue(TEXT("El primer trozo empieza en A"), Spans[0].A.Equals(A));
	TestTrue(TEXT("El último trozo acaba en B"), Spans.Last().B.Equals(B));
	double KeptCm = 0.0;
	int32 Intruding = 0;
	for (int32 Index = 0; Index < Spans.Num(); ++Index)
	{
		KeptCm += FVector::Dist2D(Spans[Index].A, Spans[Index].B);
		TestTrue(TEXT("Los trozos salen en orden de A a B"), Index == 0 || Spans[Index].A.Y >= Spans[Index - 1].B.Y - 1.0);
		for (const FBarrierPiece& Probe : RailProbes(Spans[Index].A, Spans[Index].B, Rail))
		{
			Intruding += Road.RailIntrudes(Probe.Center, Probe.RadiusCm, Probe.BottomZ, Probe.TopZ) ? 1 : 0;
		}
	}
	TestEqual(TEXT("Ningún trozo conservado entra en la calzada"), Intruding, 0);
	// Lo que entra: el ancho de la calzada de debajo más el radio del carril (11,1 m); se pierde como mucho un trozo mínimo por lado.
	const double MaxLostCm = 1000.0 + 2.0 * Rail.RadiusCm + 4.0 * Rail.MinSpanCm;
	AddInfo(FString::Printf(TEXT("Carril conservado: %.0f de 8000 cm en %d trozos; %d trozos quitados."), KeptCm, Spans.Num(), Dropped));
	TestTrue(TEXT("Solo se pierde el carril sobre la calzada de debajo"), KeptCm >= 8000.0 - MaxLostCm && KeptCm < 8000.0);

	// Paso superior a 5 m (#693, decisión del 04-10): el fondo del carril (4,6 m) entra en el gálibo de túnel de 6 m, que es el
	// que se usaba antes y quitaba el trozo (caso negativo), pero queda por encima del buggy con la artillera: se conserva entero.
	const FVector MidA(A.X, A.Y, 500.0);
	const FVector MidB(B.X, B.Y, 500.0);
	TestTrue(TEXT("A 5 m, el carril entra en el gálibo de túnel de 6 m"), RailProbes(MidA, MidB, Rail).ContainsByPredicate([&Road](const FBarrierPiece& Probe)
	{
		return Road.Intrudes(Probe.Center, Probe.RadiusCm, Probe.BottomZ, Probe.TopZ);
	}));
	int32 MidDropped = 0;
	const TArray<FRailSpan> Mid = ClearRailSpans(Road, MidA, MidB, Rail, MidDropped);
	TestEqual(TEXT("A 5 m, el tramo entero (no estorba al buggy de abajo)"), Mid.Num(), 1);
	TestEqual(TEXT("A 5 m, nada quitado"), MidDropped, 0);

	// En el borde: el fondo del carril justo por encima del gálibo del carril se conserva; justo por debajo, se parte.
	const double EdgeZ = Defaults.RailAboveCm + Rail.SinkCm;
	int32 AboveDropped = 0;
	const TArray<FRailSpan> JustAbove = ClearRailSpans(Road, FVector(A.X, A.Y, EdgeZ + 1.0), FVector(B.X, B.Y, EdgeZ + 1.0), Rail, AboveDropped);
	TestEqual(TEXT("Fondo del carril 1 cm por encima del gálibo: entero"), JustAbove.Num(), 1);
	TestEqual(TEXT("Fondo del carril 1 cm por encima del gálibo: nada quitado"), AboveDropped, 0);
	int32 BelowDropped = 0;
	ClearRailSpans(Road, FVector(A.X, A.Y, EdgeZ - 1.0), FVector(B.X, B.Y, EdgeZ - 1.0), Rail, BelowDropped);
	TestTrue(TEXT("Fondo del carril 1 cm por debajo del gálibo: se quita lo que entra"), BelowDropped > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyRoadClearPlanTest, "Tortunabo.Rally.Dressing.RoadClear.Plan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyRoadClearPlanTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyRoadClearanceTest;
	const TArray<FName>& Circuits = TNLobbyMission::RallyMapOptions();
	if (!TestTrue(TEXT("Hay circuitos del Rally"), Circuits.Num() > 0))
	{
		return false;
	}
	FScopedTestWorld Scoped(TEXT("TNRallyRoadClearPlanTestWorld"));
	if (!TestNotNull(TEXT("Mundo de prueba"), Scoped.World))
	{
		return false;
	}
	int32 NegativeHits = 0;
	for (const FName Circuit : Circuits)
	{
		ATN_RallyTrack* Track = Scoped.World->SpawnActor<ATN_RallyTrack>();
		if (!TestNotNull(TEXT("Pista"), Track) || !TestTrue(*FString::Printf(TEXT("La pista de %s se construye"), *Circuit.ToString()), Track->BuildFromVariant(Circuit)))
		{
			continue;
		}
		const FTrackData Data = SampleTrack(*Track, SampleStepCm);
		Track->ClearTrack();
		Track->Destroy();
		double Worst = 0.0;
		const int32 Intrusions = PlannedIntrusions(Data, ActorParams(), Worst);
		AddInfo(FString::Printf(TEXT("%s: %.1f km, %d pilas planificadas en la calzada (la que más, %.0f cm)."), *Circuit.ToString(),
			Data.LengthCm / 100000.0, Intrusions, Worst));
		TestEqual(*FString::Printf(TEXT("%s: pilas de la barrera dentro de la calzada"), *Circuit.ToString()), Intrusions, 0);
		// Caso negativo: el eje de la pila en el borde mismo de la calzada (sin margen) la invade en todo el trazado.
		FBarrierParams Flush = ActorParams();
		Flush.RoadEdgeMarginCm = 0.0;
		double FlushWorst = 0.0;
		NegativeHits += PlannedIntrusions(Data, Flush, FlushWorst) > 0 ? 1 : 0;
	}
	TestEqual(TEXT("Sin margen, la barrera de todos los circuitos entra en la calzada (caso negativo)"), NegativeHits, Circuits.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyRoadClearRoofTest, "Tortunabo.Rally.Dressing.RoadClear.Roof",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyRoadClearRoofTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyRoadClearanceTest;
	FScopedTestWorld Scoped(TEXT("TNRallyRoadClearRoofTestWorld"));
	UWorld* World = Scoped.World;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Mundo de prueba"), World) || !TestNotNull(TEXT("Cubo del motor"), Cube))
	{
		return false;
	}
	// Losas de 2 m x 2 m x 50 cm.
	const FVector Slab(2.0, 2.0, 0.5);
	TestTrue(TEXT("Una estructura puede ser techo"), ATN_RallyTrackDressing::CanBeRoof(SpawnSlab(*World, *Cube, false, FVector(0.0, 0.0, 600.0), Slab), nullptr));
	TestFalse(TEXT("El decorado de playa no es techo"), ATN_RallyTrackDressing::CanBeRoof(World->SpawnActor<ATN_BeachDecorField>(), nullptr));
	TestFalse(TEXT("El decorado del Rally no es techo"), ATN_RallyTrackDressing::CanBeRoof(World->SpawnActor<ATN_RallyTrackDressing>(), nullptr));
	TestFalse(TEXT("El actor del follaje no es techo"), ATN_RallyTrackDressing::CanBeRoof(World->SpawnActor<AInstancedFoliageActor>(), nullptr));
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(RallyRoadClearRoofTest), true);
	// En X = 0, una estructura a 6 m: techo. En X = 1000, solo follaje a 4 m: no. En X = 2000, follaje a 3 m y estructura a 7 m:
	// el follaje no tapa el techo de encima.
	const AActor* Foliage = SpawnSlab(*World, *Cube, true, FVector(1000.0, 0.0, 400.0), Slab);
	TestFalse(TEXT("Un componente de follaje no es techo"),
		ATN_RallyTrackDressing::CanBeRoof(Foliage, Foliage ? Cast<UPrimitiveComponent>(Foliage->GetRootComponent()) : nullptr));
	SpawnSlab(*World, *Cube, true, FVector(2000.0, 0.0, 300.0), Slab);
	SpawnSlab(*World, *Cube, false, FVector(2000.0, 0.0, 700.0), Slab);
	TestTrue(TEXT("Estructura encima: techo"), ATN_RallyTrackDressing::HasRoofAbove(*World, FVector(0.0, 0.0, 0.0), RoofProbeCm, RoofMinGapCm, Query));
	TestFalse(TEXT("Solo follaje encima: sin techo"), ATN_RallyTrackDressing::HasRoofAbove(*World, FVector(1000.0, 0.0, 0.0), RoofProbeCm, RoofMinGapCm, Query));
	TestTrue(TEXT("Follaje y una estructura más arriba: techo"), ATN_RallyTrackDressing::HasRoofAbove(*World, FVector(2000.0, 0.0, 0.0), RoofProbeCm, RoofMinGapCm, Query));
	TestFalse(TEXT("Nada encima: sin techo"), ATN_RallyTrackDressing::HasRoofAbove(*World, FVector(5000.0, 0.0, 0.0), RoofProbeCm, RoofMinGapCm, Query));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyRoadClearBuiltTest, "Tortunabo.Rally.Dressing.RoadClear.Built",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyRoadClearBuiltTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyRoadClearanceTest;
	const TArray<FName>& Circuits = TNLobbyMission::RallyMapOptions();
	if (!TestTrue(TEXT("Hay circuitos del Rally"), Circuits.Num() > 0))
	{
		return false;
	}
	// Caso negativo (#698): los circuitos de hoy no se cruzan ni tienen túneles, así que su decorado sin filtro ya cumple y no
	// probaría nada. El trébol (paso superior sin tablero sobre la calzada de debajo) y una losa sobre el borde sí lo
	// incumplen sin el filtro de #693: con el filtro, nada. Si el filtro deja de actuar, este caso falla.
	{
		FScopedTestWorld Scoped(TEXT("TNRallyRoadClearCloverTestWorld"));
		UWorld* World = Scoped.World;
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!TestNotNull(TEXT("Mundo del trébol"), World) || !TestNotNull(TEXT("Cubo del motor"), Cube))
		{
			return false;
		}
		// Suelo de 200 m a cota 0 y una losa a 5 m sobre el borde de la calzada de debajo (X 700-1300).
		SpawnSlab(*World, *Cube, false, FVector(0.0, 0.0, -50.0), FVector(200.0, 200.0, 1.0));
		SpawnSlab(*World, *Cube, false, FVector(1000.0, 0.0, 500.0), FVector(6.0, 30.0, 0.5));
		ATN_RallyTrackDressing* Dressing = World->SpawnActor<ATN_RallyTrackDressing>();
		if (!TestNotNull(TEXT("Decorado del trébol"), Dressing))
		{
			return false;
		}
		const FTrackData Clover = CloverTrack();
		const FRoadFootprint Road(Clover, ActorParams());
		Dressing->SetRoadClearanceEnabled(false);
		TestTrue(TEXT("El trébol sin filtro se construye"), Dressing->Build(Clover, 7));
		const FPieceCheck Unfiltered = CheckPieces(*World, *Dressing, nullptr, Road);
		Dressing->SetRoadClearanceEnabled(true);
		TestTrue(TEXT("El trébol se construye"), Dressing->Build(Clover, 7));
		const FPieceCheck Filtered = CheckPieces(*World, *Dressing, nullptr, Road);
		AddInfo(FString::Printf(TEXT("Trébol: sin filtro %d en la calzada y %d bajo un techo; con filtro %d y %d (quitadas %d y %d)."),
			Unfiltered.OnRoad, Unfiltered.UnderRoof, Filtered.OnRoad, Filtered.UnderRoof,
			Dressing->GetBlockedBarrierCount() - Dressing->GetRoofBlockedBarrierCount(), Dressing->GetRoofBlockedBarrierCount()));
		TestTrue(TEXT("Trébol sin filtro: alguna pila cae en la calzada de debajo"), Unfiltered.OnRoad > 0);
		TestTrue(TEXT("Trébol sin filtro: alguna pila queda bajo la losa"), Unfiltered.UnderRoof > 0);
		TestEqual(TEXT("Trébol: piezas dentro de la calzada"), Filtered.OnRoad, 0);
		TestEqual(TEXT("Trébol: piezas bajo un techo"), Filtered.UnderRoof, 0);
		TestTrue(TEXT("Trébol: queda barrera"), Filtered.Visible > 0);
		Dressing->ClearDressing();
	}
	for (const FName Circuit : Circuits)
	{
		const FString Name = Circuit.ToString();
		FScopedTestWorld Scoped(TEXT("TNRallyRoadClearBuiltTestWorld"));
		UWorld* World = Scoped.World;
		if (!TestNotNull(TEXT("Mundo de prueba"), World))
		{
			return false;
		}
		// El terreno de la variante, como ATN_RallyGameState::PrepareTrack: la variante puesta antes de OnConstruction.
		ATN_MapVariantLoader* Loader = World->SpawnActorDeferred<ATN_MapVariantLoader>(ATN_MapVariantLoader::StaticClass(), FTransform::Identity,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!TestNotNull(TEXT("Cargador del terreno"), Loader))
		{
			continue;
		}
		Loader->Variant = Circuit;
		Loader->FinishSpawning(FTransform::Identity);
		ATN_RallyTrack* Track = World->SpawnActor<ATN_RallyTrack>();
		if (!TestNotNull(TEXT("Pista"), Track) || !TestTrue(*FString::Printf(TEXT("La pista de %s se construye"), *Name), Track->BuildFromVariant(Circuit)))
		{
			continue;
		}
		ATN_RallyTrackDressing* Dressing = World->SpawnActor<ATN_RallyTrackDressing>();
		if (!TestNotNull(TEXT("Decorado"), Dressing))
		{
			continue;
		}
		const int32 Seed = static_cast<int32>(FCrc::StrCrc32(*Name));
		const FTrackData Data = SampleTrack(*Track, SampleStepCm);
		const FRoadFootprint Road(Data, ActorParams());
		// Informativo: lo que haría el decorado de este circuito sin el filtro de #693 (el caso negativo es el trébol).
		Dressing->SetRoadClearanceEnabled(false);
		if (!TestTrue(*FString::Printf(TEXT("El decorado sin filtro de %s se construye"), *Name), Dressing->BuildFromTrack(Track, Seed)))
		{
			continue;
		}
		const FPieceCheck Unfiltered = CheckPieces(*World, *Dressing, Track, Road);
		AddInfo(FString::Printf(TEXT("%s sin filtro: %d piezas visibles, %d en la calzada y %d bajo un techo."), *Name, Unfiltered.Visible,
			Unfiltered.OnRoad, Unfiltered.UnderRoof));
		Dressing->SetRoadClearanceEnabled(true);
		if (!TestTrue(*FString::Printf(TEXT("El decorado de %s se construye"), *Name), Dressing->BuildFromTrack(Track, Seed)))
		{
			continue;
		}
		const FPieceCheck Built = CheckPieces(*World, *Dressing, Track, Road);
		AddInfo(FString::Printf(TEXT("%s: %d piezas de barrera visibles; quitadas %d por la calzada y %d por un techo; la que más entra, %.0f cm."),
			*Name, Built.Visible, Dressing->GetBlockedBarrierCount() - Dressing->GetRoofBlockedBarrierCount(), Dressing->GetRoofBlockedBarrierCount(),
			Built.WorstCm));
		// Informativo: los huecos que dejan las pilas quitadas (en un túnel, toda su longitud; el límite es la pared).
		for (int32 Side = LeftSide; Side <= RightSide; ++Side)
		{
			const TArray<FVector>& Bases = Dressing->GetTireStackBases(Side);
			int32 Holes = 0;
			double Widest = 0.0;
			for (int32 Index = 1; Index < Bases.Num(); ++Index)
			{
				const double Gap = FVector::Dist2D(Bases[Index - 1], Bases[Index]) - TireDiameterCm;
				Widest = FMath::Max(Widest, Gap);
				Holes += Gap > TurtleWidthCm ? 1 : 0;
			}
			AddInfo(FString::Printf(TEXT("%s, lado %d: %d huecos entre pilas más anchos que una tortuga, el mayor de %.0f cm."), *Name, Side, Holes, Widest));
		}
		TestTrue(*FString::Printf(TEXT("%s: hay barrera"), *Name), Built.Visible > 0);
		TestEqual(*FString::Printf(TEXT("%s: piezas de la barrera dentro de la calzada"), *Name), Built.OnRoad, 0);
		TestEqual(*FString::Printf(TEXT("%s: piezas de la barrera bajo un techo"), *Name), Built.UnderRoof, 0);
		// El carril es el único límite que choca: cada trozo quitado es un agujero por el que el buggy se sale de la pista.
		TestEqual(*FString::Printf(TEXT("%s: trozos del carril de colisión quitados"), *Name), Dressing->GetBlockedRailCount(), 0);
		Dressing->ClearDressing();
		Track->ClearTrack();
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
