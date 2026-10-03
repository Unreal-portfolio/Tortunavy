// Catálogo de mapas de Supervivencia (#515): 9 mapas por dificultad de la 1 a la 4 y 14 en la 5, sin semillas
// repetidas, con trampas válidas, y cada semilla genera el mismo layout que cuando se eligió (huella).
// Colocación (#516): ninguna trampa se pierde ni cae en un hueco, la salida, la meta o una unión; ninguna zona lenta
// antes de un hueco; los obstáculos dejan 3 m de paso libre.
// Correr desde Session Frontend (categoría "Tortunabo.Survival.Catalogo") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Survival.Catalogo; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/ProcMap/TN_ProcMapSurvival.h"
#include "World/ProcMap/TN_SurvivalCatalog.h"
#include "World/ProcMap/TN_SurvivalTrapPlacement.h"

#if WITH_DEV_AUTOMATION_TESTS

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
	for (const FMapEntry& M : Maps)
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
	for (const FMapEntry& M : Maps)
	{
		const FString Ctx = FString::Printf(TEXT("semilla %u (%s)"), M.Seed, M.Name);
		TNProcMap::FLayout L;
		if (!TestTrue(Ctx + TEXT(": genera mapa"), TNProcMap::GenerateSurvivalLayout(M.Seed, M.Difficulty, L) != 0)) { continue; }
		const TArray<FTrapPlacement> Plan = PlaceLooseTraps(L, M.Seed);

		// Ninguna se pierde: una por cáscara, medusa y zona lenta; una zona por grupo de cangrejos; la zona de
		// gaviotas y sus sombrillas.
		int32 Expected = 0;
		for (const FTrapSpot& T : TrapsOf(M.Seed))
		{
			switch (T.Trap)
			{
				case ETrap::BananaPeel: case ETrap::Jellyfish: case ETrap::SlowZone: Expected += T.Count; break;
				case ETrap::Crab: Expected += 1; break;
				case ETrap::Seagull: Expected += 1 + (T.Umbrellas > 0 ? T.Umbrellas : DefaultUmbrellas); break;
				default: break;
			}
		}
		TestEqual(Ctx + TEXT(": todas las trampas colocadas"), Plan.Num(), Expected);

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
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
