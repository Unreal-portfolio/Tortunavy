// Catálogo de mapas de Supervivencia (#515): 9 mapas por dificultad de la 1 a la 4 y 14 en la 5, sin semillas
// repetidas, con trampas válidas, y cada semilla genera el mismo layout que cuando se eligió (huella).
// Correr desde Session Frontend (categoría "Tortunabo.Survival.Catalogo") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Survival.Catalogo; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/ProcMap/TN_ProcMapSurvival.h"
#include "World/ProcMap/TN_SurvivalCatalog.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
