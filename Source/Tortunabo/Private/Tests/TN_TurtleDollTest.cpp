// Muñecos tortuga del Coop (#797): reparto en el mapa procedural (TNProcMap::PlanTurtleDolls: determinista, PerLevel por
// mapa, separados, sin pisar nada ni el agua y respetando lo ya ocupado), regla de recogida (una por muñeco y jugadora) y
// figurita de código (caja y triángulos). Sin mundo ni actores: el mismo código que usan ATN_ProcMapGenerator y
// ATN_TurtleDoll. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.ProcMap.Dolls; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/ProcMap/TN_ProcMapGenerate.h"
#include "World/ProcMap/TN_ProcMapDolls.h"
#include "World/ProcMap/TN_TurtleDoll.h"
#include "World/ProcMap/TN_TurtleDollMesh.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTurtleDollTest
{
	TNProcMap::FGenParams MakeParams(uint32 Seed, int32 Grid, double Difficulty)
	{
		TNProcMap::FGenParams P;
		P.Seed = Seed;
		P.GridSize = Grid;
		P.NumCrossings = Grid >= 8 ? 4 : (Grid >= 6 ? 2 : (Grid >= 3 ? 1 : 0));
		P.NumBranches = Grid >= 6 ? 12 : 4;
		P.GapsPerKm = TNProcMap::LerpD(9.0, 17.0, Difficulty);
		P.Difficulty01 = Difficulty;
		return P;
	}

	/** Huella en planta (cm) de lo que un muñeco no puede pisar; 0 si no cuenta. */
	double FootprintOf(const TNProcMap::FFeature& F)
	{
		using TNProcMap::EFeature;
		switch (F.Type)
		{
			case EFeature::Boulder:
			case EFeature::RockSpire:   return F.Radius;
			case EFeature::PathProp:    return FMath::Max(F.Radius, F.Length * 0.5);
			case EFeature::ClimbTower:  return F.Radius * 1.42;
			case EFeature::BonusPickup:
			case EFeature::Bouncer:     return 150.0;
			case EFeature::EggNest:
			case EFeature::Geyser:      return 500.0;
			default:                    return 0.0;
		}
	}

	const TArray<TNProcMap::FPathSample>& SamplesOf(const TNProcMap::FLayout& L, int32 Branch)
	{
		return L.Branches.IsValidIndex(Branch) ? L.Branches[Branch].Samples : L.Main;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTurtleDollPlanTest,
	"Tortunabo.ProcMap.Dolls.Plan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTurtleDollPlanTest::RunTest(const FString& Parameters)
{
	using namespace TNProcMap;
	struct FCase { uint32 Seed; int32 Grid; double Diff; };
	const FCase Cases[] = { { 11u, 6, 0.5 }, { 2024u, 8, 0.2 }, { 77u, 4, 0.8 }, { 4242u, 6, 1.0 }, { 9001u, 8, 0.6 } };
	int32 Layouts = 0;
	int32 Full = 0;
	for (const FCase& Case : Cases)
	{
		const FString Ctx = FString::Printf(TEXT("semilla %u grid %d"), Case.Seed, Case.Grid);
		FLayout L;
		if (!GenerateLayout(TNTurtleDollTest::MakeParams(Case.Seed, Case.Grid, Case.Diff), L) || !L.bValid)
		{
			AddWarning(Ctx + TEXT(": sin layout, se salta."));
			continue;
		}
		++Layouts;

		TArray<FDollSpawn> Plan, Again;
		PlanTurtleDolls(L, TArray<FVector>(), Plan);
		PlanTurtleDolls(L, TArray<FVector>(), Again);
		bool bSame = Plan.Num() == Again.Num();
		for (int32 i = 0; bSame && i < Plan.Num(); ++i) { bSame &= Plan[i].Location.Equals(Again[i].Location, 0.01); }
		TestTrue(Ctx + TEXT(": el reparto es determinista"), bSame);
		TestTrue(Ctx + TEXT(": hay muñecos y no pasan del tope"), Plan.Num() > 0 && Plan.Num() <= DollDims::PerLevel);
		Full += Plan.Num() == DollDims::PerLevel ? 1 : 0;

		bool bApart = true, bDry = true, bClear = true;
		for (int32 i = 0; i < Plan.Num(); ++i)
		{
			const FVector2D P(Plan[i].Location.X, Plan[i].Location.Y);
			for (int32 j = i + 1; j < Plan.Num(); ++j)
			{
				bApart &= FVector2D::Distance(P, FVector2D(Plan[j].Location.X, Plan[j].Location.Y)) >= DollDims::Spacing - 1.0;
			}
			const TArray<FPathSample>& S = TNTurtleDollTest::SamplesOf(L, Plan[i].BranchIndex);
			bDry &= S.IsValidIndex(Plan[i].PathIndex) && !IsWetBiome(S[Plan[i].PathIndex].Biome);
			for (const FFeature& F : L.Features)
			{
				const double Foot = TNTurtleDollTest::FootprintOf(F);
				bClear &= Foot <= 0.0 || FVector2D::Distance(P, FVector2D(F.Location.X, F.Location.Y)) >= Foot;
			}
		}
		TestTrue(Ctx + TEXT(": muñecos separados"), bApart);
		TestTrue(Ctx + TEXT(": ninguno en el agua"), bDry);
		TestTrue(Ctx + TEXT(": ninguno pisa obstáculos, huevos ni géiseres"), bClear);

		// Lo ocupado manda: con un disco de 20 m sobre el primer muñeco, ninguno cae dentro.
		if (Plan.Num() > 0)
		{
			const FVector Disc(Plan[0].Location.X, Plan[0].Location.Y, 2000.0);
			TArray<FDollSpawn> Avoiding;
			PlanTurtleDolls(L, { Disc }, Avoiding);
			bool bOutside = true;
			for (const FDollSpawn& D : Avoiding)
			{
				bOutside &= FVector2D::Distance(FVector2D(D.Location.X, D.Location.Y), FVector2D(Disc.X, Disc.Y)) >= Disc.Z;
			}
			TestTrue(Ctx + TEXT(": no pisa lo que ya está ocupado"), bOutside);
		}
	}
	TestTrue(TEXT("se ha probado algún mapa"), Layouts > 0);
	TestTrue(TEXT("casi todos los mapas llevan todos sus muñecos"), Full * 2 > Layouts);

	TArray<FDollSpawn> None;
	PlanTurtleDolls(FLayout(), TArray<FVector>(), None);
	TestEqual(TEXT("sin mapa no hay muñecos"), None.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTurtleDollCollectTest,
	"Tortunabo.ProcMap.Dolls.Collect",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTurtleDollCollectTest::RunTest(const FString& Parameters)
{
	using TNTurtleDollRules::CanCollect;
	const TArray<int32> Nobody;
	const TArray<int32> Ana = { 256 };
	TestTrue(TEXT("una jugadora en juego coge el muñeco"), CanCollect(Nobody, 256, true));
	TestFalse(TEXT("la misma no lo coge dos veces"), CanCollect(Ana, 256, true));
	TestTrue(TEXT("otra jugadora coge el suyo aunque ya lo tenga la primera"), CanCollect(Ana, 257, true));
	TestFalse(TEXT("muerta o fuera de juego no lo coge"), CanCollect(Nobody, 256, false));
	TestFalse(TEXT("sin PlayerId no lo coge"), CanCollect(Nobody, INDEX_NONE, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTurtleDollMeshTest,
	"Tortunabo.ProcMap.Dolls.Mesh",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTurtleDollMeshTest::RunTest(const FString& Parameters)
{
	TNProcMesh::FTNProcMeshBuffers B;
	TNTurtleDollMesh::Build(B);
	const int32 Tris = B.Tris.Num() / 3;
	TestTrue(FString::Printf(TEXT("la figurita tiene forma (%d triángulos)"), Tris), Tris >= 200 && Tris <= 3000);
	FBox Box(ForceInit);
	for (const FVector& V : B.Verts) { Box += V; }
	const FVector Size = Box.GetSize();
	const FVector Limit = TNTurtleDollMesh::Size();
	TestTrue(FString::Printf(TEXT("cabe en su caja (%s)"), *Size.ToString()), Size.X <= Limit.X && Size.Y <= Limit.Y && Size.Z <= Limit.Z);
	TestTrue(TEXT("nada por debajo de la base de la peana"), Box.Min.Z >= -0.01);
	TestTrue(TEXT("se ve: más de 30 cm de largo y de alto"), Size.X >= 30.0 && Size.Z >= 30.0);
	TestEqual(TEXT("un color por vértice"), B.Colors.Num(), B.Verts.Num());
	return true;
}

#endif
