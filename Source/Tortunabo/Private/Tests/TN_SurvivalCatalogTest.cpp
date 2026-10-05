// Catálogo de mapas de Supervivencia (#515): 9 mapas por dificultad de la 1 a la 4 y 14 en la 5, sin semillas
// repetidas, con trampas válidas, y cada semilla genera el mismo layout que cuando se eligió (huella).
// Colocación (#516): ninguna trampa se pierde ni cae en un hueco, la salida, la meta o una unión; ninguna zona lenta
// antes de un hueco (ni algas, ni conchas que atrapan); los obstáculos (también minas, conchas y alambres) dejan 3 m de
// paso libre; el tanque y el cangrejo ermitaño van en un tramo recto.
// El mapa de pruebas (TestMaps) pasa por las mismas huellas y colocación, y tiene una trampa de cada tipo menos la puerta de
// conchas (#731), que va en una rama y la única que tiene es la de las placas: se prueba en El Desvío (semilla 11).
// Correr desde Session Frontend (categoría "Tortunabo.Survival.Catalogo") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Survival.Catalogo; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/ProcMap/TN_ProcMapSurvival.h"
#include "World/ProcMap/TN_SurvivalCatalog.h"
#include "World/ProcMap/TN_SurvivalTrapPlacement.h"
#include "World/ProcMap/TN_SurvivalSearchPlacement.h"
#include "Game/TN_SurvivalRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Los 50 mapas del catálogo y el mapa de pruebas. */
	TArray<TNSurvivalCatalog::FMapEntry> CatalogAndTestMaps()
	{
		TArray<TNSurvivalCatalog::FMapEntry> All(TNSurvivalCatalog::Maps, UE_ARRAY_COUNT(TNSurvivalCatalog::Maps));
		All.Append(TNSurvivalCatalog::TestMaps, UE_ARRAY_COUNT(TNSurvivalCatalog::TestMaps));
		return All;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Reparto: 9 + 9 + 9 + 9 + 14, sin semillas repetidas y con trampas válidas
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalCatalogShareTest,
	"Tortunabo.Survival.Catalogo.Reparto",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalCatalogShareTest::RunTest(const FString& Parameters)
{
	using namespace TNSurvivalCatalog;
	const int32 Expected[] = { 9, 9, 9, 9, 14 };
	int32 PerDifficulty[5] = {};
	TSet<uint32> Seeds;
	for (const FMapEntry& M : Maps)
	{
		const FString Ctx = FString::Printf(TEXT("semilla %u (%s)"), M.Seed, M.Name);
		if (!TestTrue(Ctx + TEXT(": dificultad 1-5"), M.Difficulty >= TNProcMap::SurvivalMinDifficulty
			&& M.Difficulty <= TNProcMap::SurvivalMaxDifficulty)) { continue; }
		++PerDifficulty[M.Difficulty - 1];
		bool bRepeated = false;
		Seeds.Add(M.Seed, &bRepeated);
		TestFalse(Ctx + TEXT(": semilla sin repetir"), bRepeated);
		TestTrue(Ctx + TEXT(": tiene trampas"), TrapsOf(M.Seed).Num() > 0);
	}
	for (int32 D = 0; D < 5; ++D)
	{
		TestEqual(FString::Printf(TEXT("mapas de dificultad %d"), D + 1), PerDifficulty[D], Expected[D]);
	}
	// El mapa de pruebas: fuera del catálogo y con una trampa de cada tipo.
	for (const FMapEntry& M : TestMaps)
	{
		const FString Ctx = FString::Printf(TEXT("mapa de pruebas %u (%s)"), M.Seed, M.Name);
		TestFalse(Ctx + TEXT(": su semilla no es del catálogo"), Seeds.Contains(M.Seed));
		TSet<ETrap> Kinds;
		for (const FTrapSpot& T : TrapsOf(M.Seed)) { Kinds.Add(T.Trap); }
		TestFalse(Ctx + TEXT(": sin puerta de conchas (su rama es la de las placas)"), Kinds.Contains(ETrap::ShellGate));
		TestEqual(Ctx + TEXT(": tiene todas las demás trampas"), Kinds.Num(), static_cast<int32>(LastTrap));
	}
	for (const FTrapSpot& T : Traps)
	{
		const FString Ctx = FString::Printf(TEXT("trampa %d de la semilla %u"), static_cast<int32>(T.Trap), T.Seed);
		TestNotNull(Ctx + TEXT(": la semilla está en el catálogo"), FindMap(T.Seed));
		TestTrue(Ctx + TEXT(": tramo dentro del recorrido"), T.FromPct <= T.ToPct && T.ToPct <= 100);
		TestTrue(Ctx + TEXT(": al menos una"), T.Count >= 1);
		TestTrue(Ctx + TEXT(": sombrillas solo con gaviotas"), T.Umbrellas == 0 || T.Trap == ETrap::Seagull);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Huellas: el generador sigue dando los mismos 50 mapas
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalCatalogFingerprintTest,
	"Tortunabo.Survival.Catalogo.Huellas",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalCatalogFingerprintTest::RunTest(const FString& Parameters)
{
	using namespace TNSurvivalCatalog;
	for (const FMapEntry& M : CatalogAndTestMaps())
	{
		const FString Ctx = FString::Printf(TEXT("semilla %u dificultad %d (%s)"), M.Seed, M.Difficulty, M.Name);
		TNProcMap::FLayout L;
		if (!TestTrue(Ctx + TEXT(": genera mapa"), TNProcMap::GenerateSurvivalLayout(M.Seed, M.Difficulty, L) != 0)) { continue; }
		const uint64 Got = TNProcMap::LayoutFingerprint(L);
		if (Got != M.Fingerprint)
		{
			// La línea lista para pegar en el catálogo si el mapa nuevo sigue valiendo.
			AddError(FString::Printf(TEXT("%s: el layout ha cambiado; huella nueva 0x%016llXull"), *Ctx, Got));
		}
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Colocación de las trampas de #516 sobre los 50 mapas
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalCatalogPlacementTest,
	"Tortunabo.Survival.Catalogo.Colocacion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalCatalogPlacementTest::RunTest(const FString& Parameters)
{
	using namespace TNSurvivalCatalog;
	for (const FMapEntry& M : CatalogAndTestMaps())
	{
		const FString Ctx = FString::Printf(TEXT("semilla %u (%s)"), M.Seed, M.Name);
		TNProcMap::FLayout L;
		if (!TestTrue(Ctx + TEXT(": genera mapa"), TNProcMap::GenerateSurvivalLayout(M.Seed, M.Difficulty, L) != 0)) { continue; }
		const TArray<FTrapPlacement> Plan = PlaceLooseTraps(L, M.Seed);

		// Ninguna se pierde: una por cada una del punto o del tramo; un cangrejo gigante cada 20 m de tramo; las zonas de
		// gaviotas (una cada 30 m) y sus sombrillas.
		TMap<ETrap, int32> ExpectedByTrap;
		for (const FTrapSpot& T : TrapsOf(M.Seed))
		{
			const double From = L.Main.Last().S * T.FromPct / 100.0;
			const double To = L.Main.Last().S * T.ToPct / 100.0;
			switch (T.Trap)
			{
				case ETrap::Crab: ExpectedByTrap.FindOrAdd(T.Trap) += GiantCrabCount(From, To, T.Count); break;
				case ETrap::Seagull: ExpectedByTrap.FindOrAdd(T.Trap) += GullZoneCount(From, To) + (T.Umbrellas > 0 ? T.Umbrellas : DefaultUmbrellas); break;
				default: if (IsLooseTrap(T.Trap)) { ExpectedByTrap.FindOrAdd(T.Trap) += T.Count; } break;
			}
		}
		TMap<ETrap, int32> PlacedByTrap;
		for (const FTrapPlacement& P : Plan) { PlacedByTrap.FindOrAdd(P.Trap)++; }
		for (const TPair<ETrap, int32>& Pair : ExpectedByTrap)
		{
			TestEqual(Ctx + FString::Printf(TEXT(": todas las %s colocadas"), TrapName(Pair.Key)), PlacedByTrap.FindRef(Pair.Key), Pair.Value);
		}

		for (const FTrapPlacement& P : Plan)
		{
			const FString What = FString::Printf(TEXT("%s: trampa %d al %.0f %%"), *Ctx, static_cast<int32>(P.Trap), 100.0 * P.Along / L.Main.Last().S);
			if (!TestTrue(What + TEXT(": en el camino"), L.Main.IsValidIndex(P.Sample))) { continue; }
			// Las zonas de gaviotas cubren un tramo: su muestra es solo la del medio.
			if (!(P.Trap == ETrap::Seagull && !P.bUmbrella))
			{
				TestEqual(What + TEXT(": fuera de huecos, salida, meta y uniones"), L.Main[P.Sample].Flags & BlockedFlags, 0u);
			}
			if (P.Trap == ETrap::SlowZone)
			{
				TestTrue(What + TEXT(": sin hueco en los 30 m siguientes"), Placement::GapEndBetween(L.Main,
					P.Along - SlowZoneHalfLength, P.Along + SlowZoneHalfLength + SlowZoneGapClearance) < 0.0);
			}
			if (P.Trap == ETrap::Quicksand)
			{
				TestTrue(What + TEXT(": arenas movedizas sin hueco en los 30 m siguientes"), Placement::GapEndBetween(L.Main,
					P.Along - QuicksandMaxRadius, P.Along + QuicksandMaxRadius + SlowZoneGapClearance) < 0.0);
			}

			if (P.Trap == ETrap::Seaweed || P.Trap == ETrap::ClamTrap)
			{
				const double R = P.Trap == ETrap::Seaweed ? 500.0 : ClamTrapRadius;
				TestTrue(What + TEXT(": algas o concha sin hueco en los 30 m siguientes"), Placement::GapEndBetween(L.Main,
					P.Along - R, P.Along + R + SlowZoneGapClearance) < 0.0);
			}
			if (P.Trap == ETrap::ToyTank || P.Trap == ETrap::HermitCrab)
			{
				// Su tramo recto: dentro del camino y sin huecos.
				TestTrue(What + FString::Printf(TEXT(": tramo recto de %.0f cm"), 2.0 * P.Extent.X),
					P.Extent.X > 0.0 && Placement::StraightHalfLength(L.Main, P.Sample, P.Extent.X, 150.0) >= P.Extent.X);
			}
			if (P.Trap == ETrap::BarbedWire)
			{
				TestTrue(What + TEXT(": alambre de largo útil"), 2.0 * P.Extent.X >= MinWireLength);
			}

			// Paso libre: los obstáculos a menos de 1,5 m a lo largo del camino ocupan franjas de la sección.
			const double R = ObstacleRadius(P);
			if (R <= 0.0) { continue; }
			const double Half = L.Main[P.Sample].Width * 0.5;
			TArray<FVector2D> Taken;
			for (const FTrapPlacement& Q : Plan)
			{
				const double RQ = ObstacleRadius(Q);
				if (RQ > 0.0 && FMath::Abs(Q.Along - P.Along) <= 150.0)
				{
					Taken.Add(FVector2D(FMath::Max(-Half, Q.Lateral - RQ), FMath::Min(Half, Q.Lateral + RQ)));
				}
			}
			Taken.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });
			double Free = 0.0, Edge = -Half;
			for (const FVector2D& T : Taken) { Free = FMath::Max(Free, T.X - Edge); Edge = FMath::Max(Edge, T.Y); }
			Free = FMath::Max(Free, Half - Edge);
			TestTrue(What + FString::Printf(TEXT(": paso libre %.0f cm >= %.0f"), Free, MinFreePassage), Free >= MinFreePassage);
		}

		// #517: cada puente encuentra su viga, cada placa su rama y cada quad un punto libre del camino.
		const FTerrainTrapPlan Terrain = PlaceTerrainTraps(L, M.Seed);
		int32 Quads = 0, Bridges = 0, Shortcuts = 0;
		for (const FTrapSpot& T : TrapsOf(M.Seed))
		{
			Quads += T.Trap == ETrap::Quad ? T.Count : 0;
			Bridges += T.Trap == ETrap::BreakableBridge ? 1 : 0;
			Shortcuts += (T.Trap == ETrap::PressurePlate || T.Trap == ETrap::ShellGate) ? 1 : 0;
		}
		TestEqual(Ctx + TEXT(": cruces de quads"), Terrain.Quads.Num(), Quads);
		TestEqual(Ctx + TEXT(": puentes que se rompen sobre una viga"), Terrain.Bridges.Num(), Bridges);
		TestEqual(Ctx + TEXT(": atajos con placas o puerta de conchas en una rama"), Terrain.Shortcuts.Num(), Shortcuts);
		for (const FPlateShortcut& Sc : Terrain.Shortcuts)
		{
			TestTrue(Ctx + TEXT(": la puerta de conchas no lleva placas"), !Sc.bShellGate || Sc.Plates.Num() == 0);
		}
		for (const FQuadCrossing& Q : Terrain.Quads)
		{
			TestEqual(Ctx + TEXT(": quad fuera de huecos, salida, meta y uniones"), L.Main[Q.Sample].Flags & BlockedFlags, 0u);
		}
		for (const FBreakableBridge& B : Terrain.Bridges)
		{
			TestTrue(Ctx + TEXT(": el puente sustituye a una viga"), L.Features.IsValidIndex(B.Feature)
				&& TNProcMap::GapStyleOf(L.Features[B.Feature]) == TNProcMap::EGapStyle::Beam);
		}
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Densidad de trampas según la dificultad elegida (#730)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalCatalogDensityTest,
	"Tortunabo.Survival.Catalogo.Densidad",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalCatalogDensityTest::RunTest(const FString& Parameters)
{
	using namespace TNSurvivalCatalog;
	TestEqual(TEXT("Fácil: 6,5 cada 100 m"), TNSurvivalLogic::TrapsPer100mTenths(ETNProcDifficulty::Easy), 65);
	TestEqual(TEXT("Normal: 10 cada 100 m"), TNSurvivalLogic::TrapsPer100mTenths(ETNProcDifficulty::Normal), 100);
	TestEqual(TEXT("Difícil: 15 cada 100 m"), TNSurvivalLogic::TrapsPer100mTenths(ETNProcDifficulty::Hard), 150);

	// Las copias de un punto no se amontonan: con el triple, cada punto lleva dos copias en sitios distintos.
	{
		TArray<FTrapSpot> Two;
		Two.Add({ 1u, ETrap::Jellyfish, 10, 10 });
		Two.Add({ 1u, ETrap::Jellyfish, 40, 40 });
		const TArray<FTrapSpot> Tripled = ScaleTrapSpots(Two, 300);
		TestEqual(TEXT("Al 300 %: el triple de puntos"), Tripled.Num(), 6);
		TSet<uint8> Where;
		for (const FTrapSpot& T : Tripled) { Where.Add(T.FromPct); }
		TestEqual(TEXT("Al 300 %: cada uno en su sitio"), Where.Num(), 6);
	}

	for (const ETNProcDifficulty Difficulty : { ETNProcDifficulty::Easy, ETNProcDifficulty::Normal, ETNProcDifficulty::Hard })
	{
		const double Target = TNSurvivalLogic::TrapsPer100mTenths(Difficulty) / 10.0;
		double SumDensity = 0.0;
		double MinDensity = TNumericLimits<double>::Max();
		double MaxDensity = 0.0;
		int32 MapCount = 0;
		int32 Short = 0;
		for (const FMapEntry& M : TNSurvivalCatalog::Maps)
		{
			const FString Ctx = FString::Printf(TEXT("%s, semilla %u (%s)"), *UEnum::GetValueAsString(Difficulty), M.Seed, M.Name);
			TNProcMap::FLayout L;
			if (!TestTrue(Ctx + TEXT(": genera mapa"), TNProcMap::GenerateSurvivalLayout(M.Seed, M.Difficulty, L) != 0)) { continue; }
			const int32 Pct = DensityPctForTarget(L, M.Seed, Target);
			TestTrue(Ctx + TEXT(": % entre 100 y el tope"), Pct >= 100 && Pct <= MaxDensityPct);
			TestEqual(Ctx + TEXT(": determinista"), DensityPctForTarget(L, M.Seed, Target), Pct);
			const int32 TrapTotal = CountTraps(PlaceLooseTraps(L, M.Seed, Pct), PlaceTerrainTraps(L, M.Seed, Pct));
			const double Density = TrapTotal * 10000.0 / L.Main.Last().S;
			SumDensity += Density;
			MinDensity = FMath::Min(MinDensity, Density);
			MaxDensity = FMath::Max(MaxDensity, Density);
			Short += Density < Target * 0.9 ? 1 : 0;
			++MapCount;
		}
		const double Average = MapCount > 0 ? SumDensity / MapCount : 0.0;
		AddInfo(FString::Printf(TEXT("%s: %.2f trampas cada 100 m de media (objetivo %.1f; de %.2f a %.2f; %d de %d mapas por debajo del 90 %%)."),
			*UEnum::GetValueAsString(Difficulty), Average, Target, MinDensity, MaxDensity, Short, MapCount));
		TestTrue(FString::Printf(TEXT("%s: la media llega al objetivo"), *UEnum::GetValueAsString(Difficulty)), Average >= Target * 0.95);
		TestTrue(FString::Printf(TEXT("%s: la media no se pasa mucho"), *UEnum::GetValueAsString(Difficulty)), Average <= Target * 1.25);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rebuscables añadidos: 1 cada 2 trampas (#724)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalCatalogSearchPropsTest,
	"Tortunabo.Survival.Catalogo.Rebuscables",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalCatalogSearchPropsTest::RunTest(const FString& Parameters)
{
	using namespace TNSurvivalCatalog;
	TestEqual(TEXT("Fácil: 3 cada 100 m"), TNSurvivalLogic::SearchSpotsPer100mTenths(ETNProcDifficulty::Easy), 30);
	TestEqual(TEXT("Normal: 5 cada 100 m"), TNSurvivalLogic::SearchSpotsPer100mTenths(ETNProcDifficulty::Normal), 50);
	TestEqual(TEXT("Difícil: 7,5 cada 100 m"), TNSurvivalLogic::SearchSpotsPer100mTenths(ETNProcDifficulty::Hard), 75);

	for (const ETNProcDifficulty Difficulty : { ETNProcDifficulty::Easy, ETNProcDifficulty::Normal, ETNProcDifficulty::Hard })
	{
		const FString DiffName = UEnum::GetValueAsString(Difficulty);
		const double Target = TNSurvivalLogic::SearchSpotsPer100mTenths(Difficulty) / 10.0;
		const double TrapTarget = TNSurvivalLogic::TrapsPer100mTenths(Difficulty) / 10.0;
		double SumDensity = 0.0;
		double MinDensity = TNumericLimits<double>::Max();
		int32 MapCount = 0;
		for (const FMapEntry& M : TNSurvivalCatalog::Maps)
		{
			const FString Ctx = FString::Printf(TEXT("%s, semilla %u (%s)"), *DiffName, M.Seed, M.Name);
			TNProcMap::FLayout L;
			if (!TestTrue(Ctx + TEXT(": genera mapa"), TNProcMap::GenerateSurvivalLayout(M.Seed, M.Difficulty, L) != 0)) { continue; }
			const int32 Pct = DensityPctForTarget(L, M.Seed, TrapTarget);
			const TArray<FTrapPlacement> Loose = PlaceLooseTraps(L, M.Seed, Pct);
			const FTerrainTrapPlan Terrain = PlaceTerrainTraps(L, M.Seed, Pct);
			// Suelo de prueba: el del camino más cercano (como un mapa sin cortados junto al camino).
			auto Ground = [&L](const FVector2D& C)
			{
				double Best = TNumericLimits<double>::Max();
				double Z = 0.0;
				for (const TNProcMap::FPathSample& Sm : L.Main)
				{
					const double D = FVector2D::DistSquared(C, Sm.P);
					if (D < Best) { Best = D; Z = Sm.Z; }
				}
				return Z;
			};
			const TArray<TNProcMap::FFeature> Props = PlaceSearchProps(L, Loose, Terrain, Target, M.Seed, Ground);
			TestEqual(Ctx + TEXT(": determinista"), PlaceSearchProps(L, Loose, Terrain, Target, M.Seed, Ground).Num(), Props.Num());

			TArray<FVector2D> All;
			int32 Existing = 0;
			for (const TNProcMap::FFeature& F : L.Features)
			{
				if (IsExistingSearchable(F)) { All.Add(FVector2D(F.Location.X, F.Location.Y)); ++Existing; }
			}
			for (const TNProcMap::FFeature& F : Props)
			{
				const FVector2D C(F.Location.X, F.Location.Y);
				TestTrue(Ctx + TEXT(": objeto del camino que se puede rebuscar"), F.Type == TNProcMap::EFeature::PathProp
					&& IsSearchPropKind(static_cast<TNProcMap::EPathProp>(F.Aux)));
				bool bCrowded = false;
				for (const FVector2D& O : All) { bCrowded |= FVector2D::Distance(C, O) < SearchPropSpacing - 1.0; }
				TestFalse(Ctx + TEXT(": a 9 m de otro rebuscable"), bCrowded);
				const double S = L.Main[FMath::Clamp(F.PathIndex, 0, L.Main.Num() - 1)].S;
				bool bNearTrap = false;
				for (const FTrapPlacement& P : Loose) { bNearTrap |= HoldsPlace(P) && FMath::Abs(P.Along - S) < SearchPropTrapClearance; }
				TestFalse(Ctx + TEXT(": lejos de las trampas"), bNearTrap);
				TestTrue(Ctx + TEXT(": fuera de huecos, salida y meta"), Placement::IsFree(L.Main, F.PathIndex));
				All.Add(C);
			}
			const double Density = (Existing + Props.Num()) * 10000.0 / L.Main.Last().S;
			SumDensity += Density;
			MinDensity = FMath::Min(MinDensity, Density);
			++MapCount;
		}
		const double Average = MapCount > 0 ? SumDensity / MapCount : 0.0;
		AddInfo(FString::Printf(TEXT("%s: %.2f rebuscables cada 100 m de media (objetivo %.1f; el que menos, %.2f)."), *DiffName, Average, Target, MinDensity));
		// En difícil, con 15 trampas cada 100 m (#730) los rebuscables no caben todos (lejos de las trampas y a 9 m entre sí):
		// se acepta lo que quepa (decidido el 05-10, #731). Hoy salen ~6,3 de 7,5.
		const double MinShare = Difficulty == ETNProcDifficulty::Hard ? 0.8 : 0.85;
		TestTrue(DiffName + TEXT(": la media se acerca al objetivo"), Average >= Target * MinShare);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
