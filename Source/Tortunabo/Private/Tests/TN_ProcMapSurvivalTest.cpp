// Mapa de Supervivencia (#273) sobre el generador del Coop: rejilla rectangular, trazado lineal y ramas sin
// callejones. El Coop no cambia: su huella con las mismas semillas tiene que seguir siendo la de antes.
// Correr desde Session Frontend (categoría "Tortunabo.ProcMap.Survival") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.ProcMap.Survival; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/ProcMap/TN_ProcMapSurvival.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TNProcMap::FGenParams MakeCoopParams(uint32 Seed, int32 Grid)
	{
		TNProcMap::FGenParams P;
		P.Seed = Seed;
		P.GridSize = Grid;
		P.NumCrossings = Grid >= 6 ? 2 : 1;
		P.NumBranches = 3;
		P.bRiver = (Seed % 3) == 0;
		P.Difficulty01 = static_cast<double>(Seed % 5) / 4.0;
		return P;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// El Coop no cambia
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNProcMapCoopUnchangedTest,
	"Tortunabo.ProcMap.Survival.CoopSinCambios",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNProcMapCoopUnchangedTest::RunTest(const FString& Parameters)
{
	using namespace TNProcMap;
	struct FCase { uint32 Seed; int32 Grid; uint64 Expected; };
	// Huellas del generador antes de admitir rejillas rectangulares (dev a1ced1de). Son las mismas en DebugGame y en
	// Development: la de grid 6 y semilla 21 salía 0x787A8D4838D3FD94 en DebugGame y 0x06BAEAE6AA08AC97 en Development
	// porque una senda tenía un punto de más o de menos según el redondeo (#579, arreglado en AppendHermite).
	const FCase Cases[] = {
		{ 11u, 3, 0xFD5CBBA920111AD3ull },
		{ 12u, 3, 0xD8A940E8D5C2E22Dull },
		{ 21u, 6, 0x06BAEAE6AA08AC97ull },
	};
	for (const FCase& C : Cases)
	{
		FLayout L;
		const FString Ctx = FString::Printf(TEXT("grid %d semilla %u"), C.Grid, C.Seed);
		if (!TestTrue(Ctx + TEXT(": genera layout"), GenerateLayout(MakeCoopParams(C.Seed, C.Grid), L) && L.bValid)) { continue; }
		const uint64 Got = TNProcMap::LayoutFingerprint(L);
		AddInfo(FString::Printf(TEXT("%s: huella 0x%016llXull"), *Ctx, Got));
		TestEqual(Ctx + TEXT(": misma huella que antes"), Got, C.Expected);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rejilla rectangular: más largo que ancho, con todo dentro
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNProcMapRectangularGridTest,
	"Tortunabo.ProcMap.Survival.RejillaRectangular",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNProcMapRectangularGridTest::RunTest(const FString& Parameters)
{
	using namespace TNProcMap;
	for (uint32 Seed = 1; Seed <= 6; ++Seed)
	{
		FGenParams P = MakeCoopParams(Seed, 6);
		P.GridSizeX = 3;
		FLayout L;
		const FString Ctx = FString::Printf(TEXT("3x6 semilla %u"), Seed);
		if (!TestTrue(Ctx + TEXT(": genera layout"), GenerateLayout(P, L) && L.bValid)) { continue; }
		TestEqual(Ctx + TEXT(": 18 módulos"), L.Modules.Num(), 18);
		TestEqual(Ctx + TEXT(": ancho de 3 módulos"), L.WorldSizeX, 3.0 * P.ModuleSize);
		TestEqual(Ctx + TEXT(": largo de 6 módulos"), L.WorldSize, 6.0 * P.ModuleSize);
		TestEqual(Ctx + TEXT(": acaba en el borde norte"), L.Modules[L.Route.Last().Module].GridCoord.Y, 5);
		bool bInside = true;
		for (const FPathSample& S : L.Main)
		{
			bInside &= S.P.X >= 0.0 && S.P.X <= L.WorldSizeX && S.P.Y >= 0.0 && S.P.Y <= L.WorldSize;
		}
		TestTrue(Ctx + TEXT(": el camino no se sale del mapa"), bInside);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Supervivencia: lineal y sin callejones
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNProcMapSurvivalLinearTest,
	"Tortunabo.ProcMap.Survival.LinealSinCallejones",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNProcMapSurvivalLinearTest::RunTest(const FString& Parameters)
{
	using namespace TNProcMap;
	const double Tol = 150.0;
	int32 BranchesSeen = 0;
	for (int32 D = SurvivalMinDifficulty; D <= SurvivalMaxDifficulty; ++D)
	{
		for (uint32 Seed = 1; Seed <= 8; ++Seed)
		{
			FLayout L;
			const FString Ctx = FString::Printf(TEXT("dificultad %d semilla %u"), D, Seed);
			if (!TestTrue(Ctx + TEXT(": genera layout"), GenerateSurvivalLayout(Seed, D, L) != 0)) { continue; }
			TestFalse(Ctx + TEXT(": el camino no se pliega sobre sí mismo"), SurvivalPathFolds(L));
			TestTrue(Ctx + TEXT(": las ramas se unen sin bordillo"), SurvivalBranchesFlush(L));
			TestTrue(Ctx + TEXT(": ninguna zanja toca la salida ni la meta"), SurvivalEndsClear(L));
			TestTrue(Ctx + TEXT(": el camino cabe en los 110 m centrales"), SurvivalPathInside(L));

			// Sin pasadas de cruce y la ruta de módulos nunca vuelve hacia el sur.
			TestEqual(Ctx + TEXT(": sin cruces"), L.Crossings.Num(), 0);
			bool bForward = true;
			for (int32 k = 1; k < L.Route.Num(); ++k)
			{
				bForward &= L.Modules[L.Route[k].Module].GridCoord.Y >= L.Modules[L.Route[k - 1].Module].GridCoord.Y;
			}
			TestTrue(Ctx + TEXT(": el principal avanza hacia la meta"), bForward);

			// Cada rama empieza y acaba en el principal o en otra rama, y todas se alcanzan desde el principal.
			auto PointOf = [&L](int32 Branch, int32 BranchSample, int32 MainSample)
			{
				return Branch == INDEX_NONE ? L.Main[MainSample].P : L.Branches[Branch].Samples[BranchSample].P;
			};
			TArray<uint8> Linked;
			Linked.Init(0, L.Branches.Num());
			for (int32 b = 0; b < L.Branches.Num(); ++b)
			{
				const FBranch& B = L.Branches[b];
				++BranchesSeen;
				const FString BCtx = FString::Printf(TEXT("%s rama %d"), *Ctx, b);
				if (!TestTrue(BCtx + TEXT(": tiene muestras"), B.Samples.Num() >= 2)) { continue; }
				const double DStart = FVector2D::Distance(B.Samples[0].P, PointOf(B.FromBranch, B.FromSample, B.ForkSample));
				const double DEnd = FVector2D::Distance(B.Samples.Last().P, PointOf(B.ToBranch, B.ToSample, B.RejoinSample));
				TestTrue(FString::Printf(TEXT("%s: sale de un camino (a %.0f cm)"), *BCtx, DStart), DStart <= Tol);
				TestTrue(FString::Printf(TEXT("%s: acaba en un camino, no en un callejón (a %.0f cm)"), *BCtx, DEnd), DEnd <= Tol);
				Linked[b] = (B.FromBranch == INDEX_NONE || B.ToBranch == INDEX_NONE) ? 1 : 0;
			}
			for (int32 Pass = 0; Pass < L.Branches.Num(); ++Pass)
			{
				for (int32 b = 0; b < L.Branches.Num(); ++b)
				{
					const FBranch& B = L.Branches[b];
					if (!Linked[b] && ((B.FromBranch != INDEX_NONE && Linked[B.FromBranch]) || (B.ToBranch != INDEX_NONE && Linked[B.ToBranch]))) { Linked[b] = 1; }
				}
			}
			for (int32 b = 0; b < L.Branches.Num(); ++b) { TestTrue(FString::Printf(TEXT("%s rama %d: unida al principal"), *Ctx, b), Linked[b] != 0); }
		}
	}
	// En el mapa de 80 × 200 m apenas caben ramas (el trazado de ramas usa medidas de módulos de 400 m).
	AddInfo(FString::Printf(TEXT("%d ramas comprobadas"), BranchesSeen));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Perfil de Supervivencia: genera con todas las dificultades
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNProcMapSurvivalGeneratesTest,
	"Tortunabo.ProcMap.Survival.Genera",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNProcMapSurvivalGeneratesTest::RunTest(const FString& Parameters)
{
	using namespace TNProcMap;
	for (int32 D = SurvivalMinDifficulty; D <= SurvivalMaxDifficulty; ++D)
	{
		for (uint32 Seed = 1; Seed <= 10; ++Seed)
		{
			FLayout L;
			const FString Ctx = FString::Printf(TEXT("dificultad %d semilla %u"), D, Seed);
			const double T0 = FPlatformTime::Seconds();
			const bool bOk = GenerateSurvivalLayout(Seed, D, L) != 0;
			const double Secs = FPlatformTime::Seconds() - T0;
			if (!bOk)
			{
				// Por qué se descarta la primera semilla de la secuencia.
				FLayout Raw;
				const bool bRaw = GenerateLayout(MakeSurvivalParams(Seed, D), Raw) && Raw.bValid;
				FString Out;
				const double Margin = 0.5 * (Raw.WorldSizeX - SurvivalWindowWidth) + 100.0;
				for (const FPathSample& S : Raw.Main)
				{
					if ((S.Flags & PathFlags::Shore) == 0 && (S.P.X - 0.5 * S.Width < Margin || S.P.X + 0.5 * S.Width > Raw.WorldSizeX - Margin))
					{
						Out = FString::Printf(TEXT("fuera: x %.0f ancho %.0f flags %u s %.0f/%.0f"), S.P.X, S.Width, S.Flags, S.S, Raw.MainLength());
						break;
					}
				}
				AddInfo(FString::Printf(TEXT("%s: layout %d · plegado %d · %s"), *Ctx, bRaw, SurvivalPathFolds(Raw), *Out));
			}
			AddInfo(FString::Printf(TEXT("%s: %s · %.0fx%.0f m · ruta %d · camino %.0f m · ramas %d · huecos %d · %.2f s · %hs"),
				*Ctx, bOk ? TEXT("ok") : TEXT("FALLA"), L.WorldSizeX / 100.0, L.WorldSize / 100.0, L.Route.Num(),
				L.MainLength() / 100.0, L.Branches.Num(),
				static_cast<int32>(L.Features.FilterByPredicate([](const FFeature& F) { return F.Type == EFeature::Gap; }).Num()),
				Secs, L.FailReason));
			TestTrue(Ctx + TEXT(": genera layout"), bOk);
		}
	}
	return true;
}

#endif
