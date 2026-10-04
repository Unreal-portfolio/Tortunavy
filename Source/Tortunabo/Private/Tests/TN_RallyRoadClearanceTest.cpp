// Gálibo de la calzada del Rally (#693): ninguna pieza de la barrera (neumáticos, valla o carril de colisión) dentro del ancho de
// la calzada de ningún tramo (road_uu con su ancho por tramo), tampoco la de un paso superior que cae sobre la calzada de debajo,
// ni bajo un techo sobre la calzada (túnel o su boca).
//  - Footprint: TNRallyDressing::FRoadFootprint con trazados montados a mano (ancho por tramos y un cruce a dos alturas, el
//    caso del túnel del trébol), con sus casos negativos.
//  - Plan: la barrera planificada de todos los circuitos del Rally (TNLobbyMission::RallyMapOptions) no entra en la calzada; caso
//    negativo: pegada al borde sin margen, sí entra.
//  - Built: con el terreno de cada circuito y el decorado construido como en la carrera, ninguna pieza colocada entra en la
//    calzada ni tiene un techo encima.
// Plan y Built solo en el editor: Scripts/ no se empaqueta. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Dressing.RoadClear; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Lobby/TN_LobbyMission.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyTrack.h"
#include "Rally/TN_RallyTrackDressing.h"
#include "World/TN_MapVariantLoader.h"

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
		if (!TestNotNull(TEXT("Decorado"), Dressing)
			|| !TestTrue(*FString::Printf(TEXT("El decorado de %s se construye"), *Name), Dressing->BuildFromTrack(Track, static_cast<int32>(FCrc::StrCrc32(*Name)))))
		{
			continue;
		}
		const FTrackData Data = SampleTrack(*Track, SampleStepCm);
		const FRoadFootprint Road(Data, ActorParams());
		FCollisionQueryParams Query(SCENE_QUERY_STAT(RallyRoadClearTest), true);
		Query.AddIgnoredActor(Dressing);
		Query.AddIgnoredActor(Track);
		int32 OnRoad = 0;
		int32 UnderRoof = 0;
		int32 Visible = 0;
		double Worst = 0.0;
		for (const FBarrierPiece& Piece : Dressing->GetBarrierPieces())
		{
			const double Depth = Road.IntrusionCm(Piece.Center, Piece.RadiusCm, Piece.BottomZ, Piece.TopZ);
			Worst = FMath::Max(Worst, Depth);
			OnRoad += Depth > Road.GetClearance().ToleranceCm ? 1 : 0;
			if (Piece.bRail)
			{
				continue;
			}
			++Visible;
			FHitResult Hit;
			const FVector Top(Piece.Center.X, Piece.Center.Y, Piece.TopZ + 10.0);
			UnderRoof += World->LineTraceSingleByObjectType(Hit, Top, Top + FVector(0.0, 0.0, RoofProbeCm), FCollisionObjectQueryParams(ECC_WorldStatic),
				Query) && Hit.ImpactPoint.Z - Top.Z >= RoofMinGapCm ? 1 : 0;
		}
		AddInfo(FString::Printf(TEXT("%s: %d piezas de barrera visibles; quitadas %d por la calzada y %d por un techo; la que más entra, %.0f cm."),
			*Name, Visible, Dressing->GetBlockedBarrierCount() - Dressing->GetRoofBlockedBarrierCount(), Dressing->GetRoofBlockedBarrierCount(), Worst));
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
		TestTrue(*FString::Printf(TEXT("%s: hay barrera"), *Name), Visible > 0);
		TestEqual(*FString::Printf(TEXT("%s: piezas de la barrera dentro de la calzada"), *Name), OnRoad, 0);
		TestEqual(*FString::Printf(TEXT("%s: piezas de la barrera bajo un techo"), *Name), UnderRoof, 0);
		Dressing->ClearDressing();
		Track->ClearTrack();
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
