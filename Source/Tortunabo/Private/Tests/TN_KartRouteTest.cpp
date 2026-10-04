// Karts sobre el mapa generado del cooperativo (#291, #293): el camino conducible del generador (TNProcMap::FGenParams::bDrivable)
// y la pista que sale de él (TNKart::PlanRouteFromPath: puertas, salida, meta, cajas y línea del piloto IA). Lógica pura.
// Correr desde Session Frontend (categorías "Tortunabo.Kart.Route" y "Tortunabo.ProcMap.Drivable") o headless con
// UnrealEditor-Win64-DebugGame-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Kart; Quit".

#include "Misc/AutomationTest.h"
#include "Lobby/TN_LobbyMission.h"
#include "Kart/TN_KartRoutePlan.h"
#include "Kart/TN_KartTrack.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "World/ProcMap/TN_ProcMapTerrain.h"
#include "World/ProcMap/TN_ProcMapGenerate.h"
#include "World/ProcMap/TN_ProcMapTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNKartRouteTestHelpers
{
	/**
	 * Camino recto en X de Length cm con una muestra cada Step cm y ancho Width: la playa empieza en ShoreFrom (cm) y baja
	 * hasta el mar al final; las muestras de [TunnelFrom, TunnelTo] van marcadas sin puerta.
	 */
	static TArray<TNKart::FRouteSample> StraightPath(double Length, double Step, double Width, double ShoreFrom,
		double TunnelFrom = -1.0, double TunnelTo = -1.0)
	{
		TArray<TNKart::FRouteSample> Samples;
		for (double X = 0.0; X <= Length + 1.0; X += Step)
		{
			TNKart::FRouteSample& Sample = Samples.AddDefaulted_GetRef();
			// En la playa el suelo baja de 300 cm a -100 cm en los últimos metros (el mar a 0).
			const double Z = X < ShoreFrom ? 300.0 : FMath::Lerp(300.0, -100.0, (X - ShoreFrom) / FMath::Max(1.0, Length - ShoreFrom));
			Sample.Location = FVector(X, 0.0, Z);
			Sample.WidthCm = Width;
			Sample.bShore = X >= ShoreFrom;
			Sample.bNoGate = X >= TunnelFrom && X <= TunnelTo;
		}
		return Samples;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartRouteGatesTest, "Tortunabo.Kart.Route.GatesFromPath",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartRouteGatesTest::RunTest(const FString& Parameters)
{
	using namespace TNKart;
	// 3 km recto, cueva de 1000 a 1150 m, playa desde 2900 m.
	const TArray<FRouteSample> Samples = TNKartRouteTestHelpers::StraightPath(300000.0, 400.0, 1600.0, 290000.0, 100000.0, 115000.0);
	FRoutePlanParams Params;
	Params.MinFinishZ = 40.0;
	const FRoutePlan Plan = PlanRouteFromPath(Samples, Params);
	TestTrue(TEXT("El plan vale"), Plan.bValid);
	TestEqual(TEXT("Una puerta por arco"), Plan.Gates.Num(), Plan.GateArcCm.Num());
	if (Plan.GateArcCm.Num() < 3)
	{
		AddError(TEXT("Hacen falta al menos salida, una puerta y meta"));
		return false;
	}
	TestEqual(TEXT("La salida, a 42 m del principio"), Plan.GateArcCm[0], Params.StartGateArcCm, 1.0);
	TestEqual(TEXT("La meta, 25 m dentro de la playa"), Plan.FinishArcCm, 290000.0 + Params.FinishIntoShoreCm, 1.0);
	TestEqual(TEXT("La última puerta es la meta"), Plan.GateArcCm.Last(), Plan.FinishArcCm, 1.0);
	for (int32 Index = 1; Index < Plan.GateArcCm.Num(); ++Index)
	{
		const double Gap = Plan.GateArcCm[Index] - Plan.GateArcCm[Index - 1];
		TestTrue(FString::Printf(TEXT("Puertas %d-%d a no menos de %.0f m"), Index - 1, Index, Params.MinGateGapCm / 100.0), Gap >= Params.MinGateGapCm - 1.0);
		TestTrue(FString::Printf(TEXT("Puertas %d-%d a no más de una separación y la cueva"), Index - 1, Index),
			Gap <= Params.GateSpacingCm + Params.MaxGateShiftCm + 1.0);
		const double Arc = Plan.GateArcCm[Index];
		TestFalse(FString::Printf(TEXT("La puerta %d no cae en la cueva (%.0f m)"), Index, Arc / 100.0), Arc >= 100000.0 - 400.0 && Arc <= 115000.0 + 400.0);
	}
	for (const TNRally::FGateDef& Gate : Plan.Gates)
	{
		TestEqual(TEXT("Puerta mirando hacia delante"), Gate.YawDeg, 0.0, 0.5);
	}
	TestTrue(TEXT("El eje sigue pasada la meta (escapatoria)"), Plan.LengthCm > Plan.FinishArcCm);
	TestEqual(TEXT("Un ancho por punto del eje"), Plan.RoadWidthCm.Num(), Plan.Road.Num());

	// Camino muy corto: no da para salida y meta.
	const TArray<FRouteSample> Short = TNKartRouteTestHelpers::StraightPath(8000.0, 400.0, 1600.0, 6000.0);
	TestFalse(TEXT("Un camino de 80 m no da para una pista"), PlanRouteFromPath(Short, Params).bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartRouteFinishTest, "Tortunabo.Kart.Route.FinishOnDryBeach",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartRouteFinishTest::RunTest(const FString& Parameters)
{
	using namespace TNKart;
	// Playa corta: de 300 cm a -100 cm en 40 m; con la meta a 25 m caería en el agua, así que se queda antes de la orilla.
	const TArray<FRouteSample> Samples = TNKartRouteTestHelpers::StraightPath(100000.0, 200.0, 1600.0, 96000.0);
	TArray<FVector> Points;
	for (const FRouteSample& Sample : Samples)
	{
		Points.Add(Sample.Location);
	}
	const TArray<double> Arc = CumulativeArc(Points);
	FRoutePlanParams Params;
	Params.MinFinishZ = 40.0;
	const double Finish = FinishArcForPath(Samples, Arc, Params);
	TestTrue(TEXT("La meta va en la playa"), Finish >= 96000.0);
	// Cota del suelo en la meta (recta y bajada lineal): por encima de la mínima.
	const double ZAtFinish = FMath::Lerp(300.0, -100.0, (Finish - 96000.0) / 4000.0);
	TestTrue(FString::Printf(TEXT("La meta queda en seco (%.0f cm sobre el mar)"), ZAtFinish), ZAtFinish >= Params.MinFinishZ - 20.0);

	// Sin playa: la meta, al final del camino menos la escapatoria.
	TArray<FRouteSample> NoShore = Samples;
	for (FRouteSample& Sample : NoShore)
	{
		Sample.bShore = false;
	}
	TestEqual(TEXT("Sin playa, al final menos la escapatoria"), FinishArcForPath(NoShore, Arc, Params), Arc.Last() - Params.RunOffCm, 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartRouteGateShiftTest, "Tortunabo.Kart.Route.GateShift",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartRouteGateShiftTest::RunTest(const FString& Parameters)
{
	using namespace TNKart;
	FRoutePlanParams Params;
	// Prohibido de 240 a 260 m: la puerta de 250 m (42 + 250 = 292 m, libre) no se mueve; con prohibido de 280 a 300 m se adelanta.
	const TArray<double> Free = PlanGateArcs(4200.0, 100000.0, Params, [](double) { return false; });
	TestEqual(TEXT("Sin estorbos: salida, cada 250 m y meta"), Free.Num(), 5);
	TestEqual(TEXT("Segunda puerta a 250 m de la salida"), Free[1], 4200.0 + 25000.0, 1.0);
	const TArray<double> Shifted = PlanGateArcs(4200.0, 100000.0, Params, [](double S) { return S >= 28000.0 && S <= 30000.0; });
	TestTrue(TEXT("La puerta que cae en la cueva se adelanta hasta salir de ella"), Shifted.Num() >= 2 && Shifted[1] > 30000.0 && Shifted[1] <= 30000.0 + 400.0);
	// Prohibido un tramo más largo que lo que se puede adelantar: esa puerta no se pone.
	const TArray<double> Dropped = PlanGateArcs(4200.0, 100000.0, Params, [](double S) { return S >= 28000.0 && S <= 45000.0; });
	TestTrue(TEXT("Sin sitio en 80 m, la puerta se quita"), Dropped.Num() >= 2 && (Dropped[1] < 28000.0 || Dropped[1] > 45000.0));
	TestEqual(TEXT("Siempre acaba en la meta"), Dropped.Last(), 100000.0, 1.0);
	TestEqual(TEXT("Meta antes de la salida: ninguna puerta"), PlanGateArcs(5000.0, 4000.0, Params, [](double) { return false; }).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartRouteRacingLineTest, "Tortunabo.Kart.Route.RacingLine",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartRouteRacingLineTest::RunTest(const FString& Parameters)
{
	using namespace TNKart;
	TArray<FVector> Road;
	TArray<double> Half;
	for (int32 Index = 0; Index <= 60; ++Index)
	{
		Road.Add(FVector(Index * 1000.0, 0.0, 0.0));
		Half.Add(1500.0);
	}
	TestTrue(TEXT("Sin obstáculos, la línea va por el eje"), !PlanRacingLineOffsets(Road, Half, {}, 450.0, 4500.0).ContainsByPredicate(
		[](double Offset) { return !FMath::IsNearlyZero(Offset); }));

	// Pieza de 4 m de radio a 1 m a la izquierda del eje a 300 m: la línea pasa por la derecha, a 4 m + 4,5 m de su centro.
	const FLineObstacle Plaza{ FVector2D(30000.0, -100.0), 400.0 };
	const TArray<double> Offsets = PlanRacingLineOffsets(Road, Half, { Plaza }, 450.0, 4500.0);
	TestTrue(TEXT("Rodea por la derecha (más sitio)"), Offsets[30] > 0.0);
	TestTrue(TEXT("Pasa a la holgura pedida del obstáculo"), Offsets[30] - (-100.0) >= 400.0 + 450.0 - 1.0);
	TestTrue(TEXT("Rodea toda la pieza, no solo su centro"), Offsets[29] > 0.0 && Offsets[31] > 0.0);
	TestTrue(TEXT("Lejos del obstáculo, por el eje"), FMath::IsNearlyZero(Offsets[0]) && FMath::IsNearlyZero(Offsets[60]));
	for (int32 Index = 0; Index < Offsets.Num(); ++Index)
	{
		TestTrue(TEXT("Nunca fuera de la calzada"), FMath::Abs(Offsets[Index]) <= Half[Index] - 250.0 + 1.0);
	}

	// Dos obstáculos a 17 m uno del otro (lo que pasaba en la salida de un mapa normal): la línea los rodea a los dos.
	TArray<double> WideHalf;
	WideHalf.Init(1750.0, Road.Num());
	const FLineObstacle A{ FVector2D(30000.0, 550.0), 300.0 };
	const FLineObstacle B{ FVector2D(31700.0, 200.0), 170.0 };
	const TArray<double> Weave = PlanRacingLineOffsets(Road, WideHalf, { A, B }, 450.0, 4500.0);
	for (int32 Index = 0; Index < Road.Num(); ++Index)
	{
		for (const FLineObstacle& Obstacle : { A, B })
		{
			const double Along = FMath::Abs(Road[Index].X - Obstacle.Center.X);
			if (Along <= Obstacle.RadiusCm + 450.0)
			{
				TestTrue(FString::Printf(TEXT("A la altura de un obstáculo (punto %d), la línea pasa a su holgura"), Index),
					FMath::Abs(Weave[Index] - Obstacle.Center.Y) >= Obstacle.RadiusCm + 450.0 - 1.0);
			}
		}
	}

	// Un obstáculo fuera de la calzada no cambia nada.
	const FLineObstacle Far{ FVector2D(30000.0, 6000.0), 400.0 };
	TestTrue(TEXT("Obstáculo lejos del camino: línea por el eje"), FMath::IsNearlyZero(PlanRacingLineOffsets(Road, Half, { Far }, 450.0, 4500.0)[30]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartRouteHazardGatesTest, "Tortunabo.Kart.Route.NoGateNearWater",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartRouteHazardGatesTest::RunTest(const FString& Parameters)
{
	// Camino recto con un canal de agua de 300 a 320 m: sin puertas en el canal ni a menos de 25 m de él.
	TArray<FTNProcPathPoint> Points;
	for (int32 Index = 0; Index <= 200; ++Index)
	{
		FTNProcPathPoint& Point = Points.AddDefaulted_GetRef();
		Point.Location = FVector(Index * 400.0, 0.0, 100.0);
		Point.Width = 1600.f;
		Point.Flags = (Index >= 75 && Index <= 80) ? TNProcMap::PathFlags::Islet : 0u;
	}
	const TArray<TNKart::FRouteSample> Samples = TNKart::RouteSamplesFrom(Points);
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		const double X = Index * 400.0;
		const bool bNear = X >= 30000.0 - TNKart::GateAwayFromHazardCm && X <= 32000.0 + TNKart::GateAwayFromHazardCm;
		TestEqual(*FString::Printf(TEXT("Muestra a %.0f m: puerta %s"), X / 100.0, bNear ? TEXT("prohibida") : TEXT("permitida")),
			Samples[Index].bNoGate, bNear);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartRouteGeneratedMapsTest, "Tortunabo.Kart.Route.GeneratedMaps",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartRouteGeneratedMapsTest::RunTest(const FString& Parameters)
{
	using namespace TNProcMap;
	// El mapa de los karts de tres semillas (fácil y normal): puertas, salida y meta sobre el camino, la meta en la playa y
	// ninguna puerta a menos de 25 m del agua, las cascadas, los géiseres o los huecos (lo mismo que ATN_KartTrack, sin mundo).
	constexpr uint32 Hazard = PathFlags::Islet | PathFlags::Slide | PathFlags::GeyserBase | PathFlags::CliffUp | PathFlags::Gap
		| PathFlags::RiverCross;
	const TPair<uint32, ETNProcDifficulty> Maps[] = { { 11u, ETNProcDifficulty::Easy }, { 4242u, ETNProcDifficulty::Easy },
		{ 777u, ETNProcDifficulty::Normal } };
	for (const TPair<uint32, ETNProcDifficulty>& Map : Maps)
	{
		FGenParams Params = TN_MakeDefaultProcProfile(ETNProcGameMode::Coop, Map.Value).ToGenParams(Map.Key);
		Params.bDrivable = true;
		FLayout Layout;
		if (!GenerateLayout(Params, Layout))
		{
			AddError(FString::Printf(TEXT("Sin mapa de karts con la semilla %u"), Map.Key));
			continue;
		}
		TArray<FTNProcPathPoint> Points;
		TArray<double> PathArc;
		for (const FPathSample& Sample : Layout.Main)
		{
			FTNProcPathPoint& Point = Points.AddDefaulted_GetRef();
			Point.Location = FVector(Sample.P.X, Sample.P.Y, Sample.Z);
			Point.Direction = FVector(Sample.Dir.X, Sample.Dir.Y, 0.0);
			Point.Width = static_cast<float>(Sample.Width);
			Point.Flags = Sample.Flags;
			PathArc.Add(PathArc.Num() > 0 ? PathArc.Last() + FVector::Dist(Points[Points.Num() - 2].Location, Point.Location) : 0.0);
		}
		const TNKart::FRoutePlan Plan = TNKart::PlanRouteFromPath(TNKart::RouteSamplesFrom(Points), TNKart::MakePlanParams(SeaLevel));
		const FString Where = FString::Printf(TEXT("semilla %u"), Map.Key);
		if (!TestTrue(*FString::Printf(TEXT("Hay pista (%s)"), *Where), Plan.bValid && Plan.Gates.Num() >= 3))
		{
			continue;
		}
		for (int32 Gate = 0; Gate < Plan.Gates.Num(); ++Gate)
		{
			// La muestra más cercana del camino.
			int32 Nearest = 0;
			double Best = TNumericLimits<double>::Max();
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				const double D = FVector::Dist2D(Points[Index].Location, Plan.Gates[Gate].Location);
				if (D < Best) { Best = D; Nearest = Index; }
			}
			TestTrue(*FString::Printf(TEXT("Puerta %d sobre el camino (%s): a %.1f m del centro"), Gate, *Where, Best / 100.0),
				Best <= 0.5 * Points[Nearest].Width + 100.0);
			if (Gate > 0)
			{
				TestTrue(*FString::Printf(TEXT("Puertas en orden (%s)"), *Where), Plan.GateArcCm[Gate] > Plan.GateArcCm[Gate - 1]);
			}
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				if ((Points[Index].Flags & Hazard) != 0 && FMath::Abs(PathArc[Index] - PathArc[Nearest]) < TNKart::GateAwayFromHazardCm - 450.0)
				{
					AddError(FString::Printf(TEXT("Puerta %d a %.0f m de agua, cascada o géiser (%s)"), Gate,
						FMath::Abs(PathArc[Index] - PathArc[Nearest]) / 100.0, *Where));
					break;
				}
			}
		}
		// La meta, en la playa final.
		int32 FinishSample = 0;
		double FinishBest = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const double D = FVector::Dist2D(Points[Index].Location, Plan.Gates.Last().Location);
			if (D < FinishBest) { FinishBest = D; FinishSample = Index; }
		}
		TestTrue(*FString::Printf(TEXT("La meta va en la playa (%s)"), *Where), (Points[FinishSample].Flags & PathFlags::Shore) != 0);
		AddInfo(FString::Printf(TEXT("Karts, %s: %.2f km, %d puertas."), *Where, Plan.LengthCm / 100000.0, Plan.Gates.Num()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNProcMapDrivableTest, "Tortunabo.ProcMap.Drivable",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNProcMapDrivableTest::RunTest(const FString& Parameters)
{
	using namespace TNProcMap;
	// Géiseres, cascadas y canales de agua sí (#293: el kart sube, baja y flota); pasarelas, huecos y estructuras, no.
	constexpr uint32 NotDrivable = PathFlags::Boardwalk | PathFlags::Gap | PathFlags::Elevated | PathFlags::Colossal | PathFlags::TowerTop
		| PathFlags::UnderTower;
	int32 Geysers = 0;
	int32 Slides = 0;
	int32 WaterSamples = 0;
	double PreviousLength = 0.0;
	for (int32 D = 0; D < 3; ++D)
	{
		const ETNProcDifficulty Difficulty = static_cast<ETNProcDifficulty>(D);
		double LengthSum = 0.0;
		for (const uint32 Seed : { 11u, 777u, 2027u })
		{
			// El perfil del cooperativo, como en los karts (ATN_ProcMapGenerator::BuildLayout).
			FGenParams Params = TN_MakeDefaultProcProfile(ETNProcGameMode::Coop, Difficulty).ToGenParams(Seed);
			Params.bDrivable = true;
			FLayout Layout;
			if (!GenerateLayout(Params, Layout))
			{
				AddError(FString::Printf(TEXT("Sin layout conducible (semilla %u, dificultad %d): %hs"), Seed, D, Layout.FailReason));
				continue;
			}
			const FString Where = FString::Printf(TEXT("semilla %u, dificultad %d"), Seed, D);
			TestEqual(*FString::Printf(TEXT("Sin cruces colosales (%s)"), *Where), Layout.Crossings.Num(), 0);
			TestEqual(*FString::Printf(TEXT("Sin ramas (%s)"), *Where), Layout.Branches.Num(), 0);
			int32 Blocked = 0;
			double MinWidth = TNumericLimits<double>::Max();
			double MaxSlope = 0.0;
			// Los cortes del géiser (pared) y de la cascada no cuentan para la pendiente: se suben y se bajan de otra forma.
			constexpr uint32 Cut = PathFlags::Slide | PathFlags::CliffUp | PathFlags::GeyserBase;
			for (int32 Index = 0; Index < Layout.Main.Num(); ++Index)
			{
				const FPathSample& Sample = Layout.Main[Index];
				Blocked += (Sample.Flags & NotDrivable) != 0 ? 1 : 0;
				WaterSamples += (Sample.Flags & PathFlags::Islet) != 0 ? 1 : 0;
				if ((Sample.Flags & PathFlags::Shore) == 0)
				{
					MinWidth = FMath::Min(MinWidth, Sample.Width);
				}
				if (Index > 0 && (Sample.Flags & PathFlags::Shore) == 0 && ((Sample.Flags | Layout.Main[Index - 1].Flags) & Cut) == 0)
				{
					const double Run = FMath::Max(1.0, Sample.S - Layout.Main[Index - 1].S);
					// Un corte de bajada corto entre módulos (más bajo que la cascada) es un saltito: el kart cae y sigue.
					const bool bLedge = (Layout.Main[Index - 1].Flags & PathFlags::Portal) != 0 && Sample.Z < Layout.Main[Index - 1].Z;
					if (!bLedge)
					{
						MaxSlope = FMath::Max(MaxSlope, FMath::Abs(Sample.Z - Layout.Main[Index - 1].Z) / Run);
					}
				}
			}
			TestEqual(*FString::Printf(TEXT("Ni pasarelas, ni huecos, ni estructuras (%s)"), *Where), Blocked, 0);
			TestTrue(*FString::Printf(TEXT("Sin pasos de menos de 6,5 m, tampoco en las cuevas (%s): %.0f cm"), *Where, MinWidth), MinWidth >= 650.0);
			TestTrue(*FString::Printf(TEXT("Pendiente conducible (%s): %.2f"), *Where, MaxSlope), MaxSlope <= Layout.Params.MaxPathSlope * 1.6);
			int32 Forbidden = 0;
			for (const FFeature& Feature : Layout.Features)
			{
				Geysers += Feature.Type == EFeature::Geyser ? 1 : 0;
				Slides += Feature.Type == EFeature::SlideZone ? 1 : 0;
				const bool bForbidden = Feature.Type == EFeature::Gap
					|| Feature.Type == EFeature::EggNest || Feature.Type == EFeature::Log || Feature.Type == EFeature::PathProp
					|| Feature.Type == EFeature::Boulder || Feature.Type == EFeature::ClimbTower || Feature.Type == EFeature::BonusPickup
					|| Feature.Type == EFeature::Islet || Feature.Type == EFeature::Boardwalk || Feature.Type == EFeature::Tower;
				Forbidden += bForbidden ? 1 : 0;
			}
			TestEqual(*FString::Printf(TEXT("Nada de lo de las tortugas a pie en el camino (%s)"), *Where), Forbidden, 0);
			LengthSum += Layout.MainLength();
		}
		TestTrue(FString::Printf(TEXT("La dificultad %d alarga el camino como en el cooperativo"), D), LengthSum > PreviousLength);
		PreviousLength = LengthSum;
	}
	AddInfo(FString::Printf(TEXT("Mapas de karts: %d géiseres, %d cascadas y %d muestras de agua en el camino."), Geysers, Slides, WaterSamples));
	TestTrue(TEXT("Con todas las semillas y dificultades, el camino de los karts sube en géiser y baja por cascadas"), Geysers > 0 && Slides > 0);

	// Sin bDrivable la generación es la de siempre (mismos parámetros, mismo layout).
	const FGenParams Coop = TN_MakeDefaultProcProfile(ETNProcGameMode::Coop, ETNProcDifficulty::Easy).ToGenParams(11u);
	FLayout A;
	FLayout B;
	if (GenerateLayout(Coop, A) && GenerateLayout(Coop, B))
	{
		TestEqual(TEXT("El cooperativo no cambia: mismo camino"), A.Main.Num(), B.Main.Num());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartLobbyModeTest, "Tortunabo.Kart.Route.LobbyMode",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartLobbyModeTest::RunTest(const FString& Parameters)
{
	bool bInMenu = false;
	for (const ETNProcGameMode Mode : TNLobbyMission::MenuModes)
	{
		bInMenu |= Mode == ETNProcGameMode::Karts;
	}
	TestTrue(TEXT("Los karts se eligen en el menú, la sala y el general"), bInMenu);
	TestEqual(TEXT("Una sala de karts se queda en karts"), TNLobbyMission::NormalizeMenuMode(ETNProcGameMode::Karts), ETNProcGameMode::Karts);
	TestFalse(TEXT("Los karts tienen nombre propio"), TNLobbyMission::ModeName(ETNProcGameMode::Karts).IsEmpty());
	TestTrue(TEXT("Los karts van después de los modos que ya había (el número se guarda en las salas)"),
		static_cast<int32>(ETNProcGameMode::Karts) > static_cast<int32>(ETNProcGameMode::Survival));
	return true;
}

#endif
