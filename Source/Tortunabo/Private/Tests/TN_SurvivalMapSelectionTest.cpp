// Elección del mapa de cada nivel de Supervivencia sobre el catálogo (TN_SurvivalMapSelection.h, #518). Sin mundo ni
// actores. Correr desde Session Frontend (categoría "Tortunabo.Survival") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Survival; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Game/TN_SurvivalMapSelection.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Semillas de partida variadas: la de ?SurvivalSeed por defecto, pequeñas, grandes y negativas. */
	constexpr int32 MatchSeeds[] = { 1, 7, 42, 1337, 99991, 1 << 29, 1 << 30, -5 };

	/** Los mapas de los niveles 1..LastLevel de una partida, como los juega ATN_ChunkManager::BuildLevel. */
	TArray<FTNSurvivalMapPick> PlayMatch(int32 MatchSeed, int32 LastLevel, uint32 FirstMap = 0u)
	{
		TArray<FTNSurvivalMapPick> Picks;
		TArray<uint32> Played;
		for (int32 Level = 1; Level <= LastLevel; ++Level)
		{
			const FTNSurvivalMapPick Forced = Level == 1 && FirstMap != 0u
				? TNSurvivalMapSelection::PickForcedMap(FirstMap) : FTNSurvivalMapPick();
			const FTNSurvivalMapPick Pick = Forced.IsValid() ? Forced : TNSurvivalMapSelection::PickLevelMap(MatchSeed, Level, Played);
			Played = TNSurvivalMapSelection::RecordPlayed(Played, Pick);
			Picks.Add(Pick);
		}
		return Picks;
	}

	bool IsTestMap(uint32 Seed)
	{
		for (const TNSurvivalCatalog::FMapEntry& M : TNSurvivalCatalog::TestMaps)
		{
			if (M.Seed == Seed) { return true; }
		}
		return false;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Niveles 1 a 18 sin repetir; el 19 olvida y no repite el 18
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalMapSelectionLevelsTest,
	"Tortunabo.Survival.Catalogo.Eleccion.Niveles",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalMapSelectionLevelsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("La dificultad 5 tiene 14 mapas (nivel 19 = primero tras agotarlos)"),
		TNSurvivalMapSelection::MapsOfDifficulty(5).Num(), 14);

	for (const int32 MatchSeed : MatchSeeds)
	{
		const TArray<FTNSurvivalMapPick> Picks = PlayMatch(MatchSeed, 19);
		TSet<uint32> Seen;
		for (int32 Index = 0; Index < 18; ++Index)
		{
			const int32 Level = Index + 1;
			const FTNSurvivalMapPick& Pick = Picks[Index];
			const TNSurvivalCatalog::FMapEntry* Entry = TNSurvivalCatalog::FindMap(Pick.Seed);
			if (!TestNotNull(*FString::Printf(TEXT("Partida %d, nivel %d: mapa del catálogo"), MatchSeed, Level), Entry))
			{
				return false;
			}
			TestEqual(*FString::Printf(TEXT("Partida %d, nivel %d: dificultad min(N, 5)"), MatchSeed, Level),
				Pick.Difficulty, FMath::Min(Level, 5));
			TestEqual(*FString::Printf(TEXT("Partida %d, nivel %d: la dificultad es la de la entrada"), MatchSeed, Level),
				Entry->Difficulty, Pick.Difficulty);
			TestFalse(*FString::Printf(TEXT("Partida %d, nivel %d: no es el mapa de pruebas"), MatchSeed, Level), IsTestMap(Pick.Seed));
			TestFalse(*FString::Printf(TEXT("Partida %d, nivel %d: no olvida antes de agotar la dificultad"), MatchSeed, Level), Pick.bForgotPlayed);
			TestFalse(*FString::Printf(TEXT("Partida %d, nivel %d: mapa no repetido (%u)"), MatchSeed, Level, Pick.Seed), Seen.Contains(Pick.Seed));
			Seen.Add(Pick.Seed);
		}
		TestEqual(*FString::Printf(TEXT("Partida %d: 18 mapas distintos en los niveles 1-18"), MatchSeed), Seen.Num(), 18);

		const FTNSurvivalMapPick& Level19 = Picks[18];
		TestTrue(*FString::Printf(TEXT("Partida %d: el nivel 19 olvida los jugados"), MatchSeed), Level19.bForgotPlayed);
		TestEqual(*FString::Printf(TEXT("Partida %d: el nivel 19 es de dificultad 5"), MatchSeed), Level19.Difficulty, 5);
		TestNotEqual(*FString::Printf(TEXT("Partida %d: el nivel 19 no repite el 18"), MatchSeed), Level19.Seed, Picks[17].Seed);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// La misma semilla da la misma partida; semillas distintas, partidas distintas
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalMapSelectionDeterminismTest,
	"Tortunabo.Survival.Catalogo.Eleccion.Determinista",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalMapSelectionDeterminismTest::RunTest(const FString& Parameters)
{
	TSet<uint32> FirstMaps;
	for (const int32 MatchSeed : MatchSeeds)
	{
		const TArray<FTNSurvivalMapPick> A = PlayMatch(MatchSeed, 40);
		const TArray<FTNSurvivalMapPick> B = PlayMatch(MatchSeed, 40);
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("Partida %d, nivel %d: misma semilla, mismo mapa"), MatchSeed, Index + 1), A[Index].Seed, B[Index].Seed);
		}
		FirstMaps.Add(A[0].Seed);
	}
	TestTrue(TEXT("Partidas distintas no empiezan siempre por el mismo mapa"), FirstMaps.Num() > 1);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Partida larga: cada vuelta a los 14 de la dificultad 5 sin repetir ni encadenar
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalMapSelectionLongMatchTest,
	"Tortunabo.Survival.Catalogo.Eleccion.PartidaLarga",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalMapSelectionLongMatchTest::RunTest(const FString& Parameters)
{
	constexpr int32 LastLevel = 5 + 14 * 3 - 1;
	for (const int32 MatchSeed : MatchSeeds)
	{
		const TArray<FTNSurvivalMapPick> Picks = PlayMatch(MatchSeed, LastLevel);
		for (int32 Index = 1; Index < Picks.Num(); ++Index)
		{
			TestNotEqual(*FString::Printf(TEXT("Partida %d, nivel %d: no repite el mapa anterior"), MatchSeed, Index + 1),
				Picks[Index].Seed, Picks[Index - 1].Seed);
		}

		// Niveles 5-18, 19-32 y 33-46: cada vuelta juega los 14 de la dificultad 5, y solo olvida al empezarla.
		for (int32 First = 5; First + 13 <= LastLevel; First += 14)
		{
			TSet<uint32> Round;
			for (int32 Level = First; Level < First + 14; ++Level)
			{
				const FTNSurvivalMapPick& Pick = Picks[Level - 1];
				Round.Add(Pick.Seed);
				TestEqual(*FString::Printf(TEXT("Partida %d, nivel %d: olvida solo al empezar la vuelta"), MatchSeed, Level),
					Pick.bForgotPlayed, Level == First && First > 5);
			}
			TestEqual(*FString::Printf(TEXT("Partida %d, vuelta desde el nivel %d: los 14 mapas"), MatchSeed, First), Round.Num(), 14);
		}
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ?SurvivalMap=<semilla>
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSurvivalMapSelectionForcedTest,
	"Tortunabo.Survival.Catalogo.Eleccion.MapaFijado",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSurvivalMapSelectionForcedTest::RunTest(const FString& Parameters)
{
	using namespace TNSurvivalMapSelection;

	const FTNSurvivalMapPick Forced = PickForcedMap(58);
	TestTrue(TEXT("58 es del catálogo"), Forced.IsValid());
	TestEqual(TEXT("58 se juega con su dificultad (5)"), Forced.Difficulty, 5);
	TestFalse(TEXT("Una semilla fuera del catálogo no vale"), PickForcedMap(12345).IsValid());
	TestFalse(TEXT("La semilla 0 no vale"), PickForcedMap(0).IsValid());
	TestEqual(TEXT("El mapa de pruebas (6) se puede pedir"), PickForcedMap(6).Difficulty, 5);

	// Un mapa de la dificultad 5 en el nivel 1: no vuelve a salir hasta agotar los 14 (nivel 18, uno antes).
	for (const int32 MatchSeed : MatchSeeds)
	{
		const TArray<FTNSurvivalMapPick> Picks = PlayMatch(MatchSeed, 18, 58);
		TestEqual(*FString::Printf(TEXT("Partida %d: el nivel 1 juega el 58"), MatchSeed), Picks[0].Seed, 58u);
		TSet<uint32> Seen;
		for (int32 Index = 0; Index < 17; ++Index)
		{
			TestFalse(*FString::Printf(TEXT("Partida %d, nivel %d: no repite"), MatchSeed, Index + 1), Seen.Contains(Picks[Index].Seed));
			Seen.Add(Picks[Index].Seed);
		}
		TestTrue(*FString::Printf(TEXT("Partida %d: el nivel 18 ya ha agotado la dificultad 5"), MatchSeed), Picks[17].bForgotPlayed);
		TestNotEqual(*FString::Printf(TEXT("Partida %d: el nivel 18 no repite el 17"), MatchSeed), Picks[17].Seed, Picks[16].Seed);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
