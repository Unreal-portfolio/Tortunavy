// Reparto de la carrera sobre las rondas del dorado (24 semillas × 3 dificultades, TN_BeachLayoutGoldenTest.cpp): lo que
// el GameMode hace con la ronda después de repartirla. El sprint final despeja el nido de los huevos
// (ATN_BeachRaceGameMode::PlaceSprintFinalists → ClearElementsAround): el reparto de sprint lo deja vacío para que ese
// despeje no corte el castillo principal ni nada de lo que lleva encima (#346).
// Correr: Automation RunTests Tortunabo.Beach.Course

#include "Misc/AutomationTest.h"
#include "World/Beach/TN_BeachLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNBeachCourseTest
{
	constexpr int32 NumSeeds = 24;
	const ETNProcDifficulty Difficulties[] = { ETNProcDifficulty::Easy, ETNProcDifficulty::Normal, ETNProcDifficulty::Hard };

	int32 SeedAt(int32 Index)
	{
		return 1000 + Index * 7919;
	}

	const TCHAR* DifficultyName(ETNProcDifficulty Difficulty)
	{
		return Difficulty == ETNProcDifficulty::Easy ? TEXT("Fácil") : (Difficulty == ETNProcDifficulty::Hard ? TEXT("Difícil") : TEXT("Normal"));
	}

	/**
	 * Lo que quitaría ATN_BeachRaceGenerator::ClearElementsAround con el círculo del GameMode (todos los huevos del nido y
	 * SprintClearMargin): cada elemento cuya huella (disco o cápsula) toca el círculo.
	 */
	bool IsCutBySprintNest(const TNBeachLayout::FItem& Item, const FVector2D& Center, double Radius)
	{
		double T = 0.0;
		return TNProcMap::DistPointSegment(Center, Item.EndA(), Item.EndB(), T) <= Radius + Item.Radius;
	}

	/**
	 * Lo que un lanzador delante de un obstáculo (rol Launcher) puede pisar con su arco de salto porque es lo que salta:
	 * castillos con salas y enormes, piezas de las filas y plataformas.
	 */
	bool IsJumpTarget(const TNBeachLayout::FItem& Item)
	{
		using TNBeachLayout::EItemRole;
		return Item.Role == EItemRole::Dungeon || Item.Role == EItemRole::Row || Item.Role == EItemRole::Castle
			|| Item.Element == ETNBeachElement::SandDungeon || Item.Element == ETNBeachElement::SandCastleHuge
			|| Item.Element == ETNBeachElement::WobblyPlatform || Item.Element == ETNBeachElement::MovingPlatform;
	}

	/** El arco de salto Zone cruza algo del terreno fijo que se salta: una poza o la cornisa de una cresta. */
	bool ArcCrossesTerrain(const TNBeachLayout::FItem& Zone)
	{
		for (int32 k = 0; k <= 20; ++k)
		{
			const FVector2D P = FMath::Lerp(Zone.EndA(), Zone.EndB(), k / 20.0);
			if (TNBeachLayout::PoolAt(P, 1.0) != INDEX_NONE) { return true; }
		}
		for (const FVector2D& Lip : TNBeachLayout::LipSamples())
		{
			double T = 0.0;
			if (TNProcMap::DistPointSegment(Lip, Zone.EndA(), Zone.EndB(), T) <= Zone.Radius + 300.0) { return true; }
		}
		return false;
	}

	/** X mínima y máxima de la huella de un elemento. */
	void SpanX(const TNBeachLayout::FItem& Item, double& OutMin, double& OutMax)
	{
		OutMin = FMath::Min(Item.EndA().X, Item.EndB().X) - Item.Radius;
		OutMax = FMath::Max(Item.EndA().X, Item.EndB().X) + Item.Radius;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachCourseSprintTest,
	"Tortunabo.Beach.Course.Sprint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachCourseSprintTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachCourseTest;
	using TNBeachLayout::EItemRole;
	FVector2D NestCenter;
	double NestRadius = 0.0;
	TNBeachLayout::SprintNestCircle(NestCenter, NestRadius);
	const double SprintX = TNBeachLayout::SprintLineX();
	const double FarBehind = SprintX - TNBeachLayout::SprintCastleBehind;
	const double FarAhead = SprintX + TNBeachLayout::SprintCastleAhead;
	int32 Rounds = 0;
	int32 CastlesIntact = 0;
	int32 CastlesAhead = 0;
	for (const ETNProcDifficulty Difficulty : Difficulties)
	{
		for (int32 s = 0; s < NumSeeds; ++s)
		{
			const FString Ctx = FString::Printf(TEXT("%s, semilla %d, sprint"), DifficultyName(Difficulty), SeedAt(s));
			TNBeachLayout::FRoundLayout L;
			TNBeachLayout::GenerateRound(SeedAt(s), Difficulty, L, true);
			++Rounds;
			TestTrue(Ctx + TEXT(": el reparto sabe que es de sprint"), L.bSprint);
			TestTrue(Ctx + TEXT(": paso libre"), L.bPassageOk);

			int32 Cut = 0;
			int32 CastleCut = 0;
			int32 CastlePieces = 0;
			bool bCastleAway = false;
			FString CutNames;
			for (const TNBeachLayout::FItem& It : L.Items)
			{
				const bool bMainCastle = It.Role == EItemRole::DungeonWing
					|| (It.Role == EItemRole::Dungeon && It.Pos.Equals(L.DungeonPos, 1.0));
				if (bMainCastle) { ++CastlePieces; }
				if (It.Role == EItemRole::Dungeon && It.Pos.Equals(L.DungeonPos, 1.0))
				{
					double XMin = 0.0;
					double XMax = 0.0;
					SpanX(It, XMin, XMax);
					bCastleAway = XMax < FarBehind || XMin > FarAhead;
				}
				if (!IsCutBySprintNest(It, NestCenter, NestRadius)) { continue; }
				++Cut;
				CastleCut += bMainCastle ? 1 : 0;
				if (Cut <= 4) { CutNames += FString::Printf(TEXT(" %s (%.0f, %.0f) m"), *UEnum::GetValueAsString(It.Element), It.Pos.X / 100.0, It.Pos.Y / 100.0); }
			}
			TestTrue(FString::Printf(TEXT("%s: hay castillo principal (%d piezas con sus alas)"), *Ctx, CastlePieces), L.NumDungeons > 0 && CastlePieces > 1);
			TestTrue(FString::Printf(TEXT("%s: castillo principal (%.0f m) fuera de [%.0f, %.0f] m"), *Ctx, L.DungeonPos.X / 100.0, FarBehind / 100.0, FarAhead / 100.0),
				bCastleAway);
			TestEqual(FString::Printf(TEXT("%s: el despeje del nido no corta el castillo principal ni sus alas"), *Ctx), CastleCut, 0);
			TestEqual(FString::Printf(TEXT("%s: el despeje del nido no quita nada (%d:%s)"), *Ctx, Cut, *CutNames), Cut, 0);
			CastlesIntact += CastleCut == 0 && bCastleAway ? 1 : 0;
			CastlesAhead += L.DungeonPos.X > SprintX ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("Sprint: castillo principal intacto en %d de %d rondas (%d por delante del nido, en %.0f m con radio %.0f m)."),
		CastlesIntact, Rounds, CastlesAhead, NestCenter.X / 100.0, NestRadius / 100.0));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachCourseLaunchersTest,
	"Tortunabo.Beach.Course.Launchers",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachCourseLaunchersTest::RunTest(const FString& Parameters)
{
	// #349: ningún lanzador huérfano (el de delante de un obstáculo salta algo: un elemento o el terreno fijo) y el arco de
	// salto de todos, también los de delante de un obstáculo, libre de todo salvo lo que salta (los pasos de quads cruzan
	// por debajo y los pulpos nadan bajo algún arco, como en Tortunabo.Beach.Layout.Rules).
	using namespace TNBeachCourseTest;
	using TNBeachLayout::FItem;
	using TNBeachLayout::EItemRole;
	int32 TotalOrphans = 0;
	int32 TotalBlocked = 0;
	int32 TotalAimed = 0;
	for (const ETNProcDifficulty Difficulty : Difficulties)
	{
		for (int32 s = 0; s < NumSeeds; ++s)
		{
			const FString Ctx = FString::Printf(TEXT("%s, semilla %d"), DifficultyName(Difficulty), SeedAt(s));
			TNBeachLayout::FRoundLayout L;
			TNBeachLayout::GenerateRound(SeedAt(s), Difficulty, L);
			int32 Orphans = 0;
			int32 Blocked = 0;
			FString Where;
			for (int32 i = 0; i < L.Items.Num(); ++i)
			{
				const FItem& It = L.Items[i];
				if (!TNBeachLayout::RuleOf(It.Element).bLauncher) { continue; }
				const bool bAimed = It.Role == EItemRole::Launcher;
				TotalAimed += bAimed ? 1 : 0;
				const FItem Zone = TNBeachLayout::FBuilder::JumpArcZone(It);
				bool bJumpsSomething = false;
				bool bArcFree = true;
				for (int32 j = 0; j < L.Items.Num(); ++j)
				{
					const FItem& Other = L.Items[j];
					if (j == i || Other.bOverlay || Other.Element == ETNBeachElement::QuadLane || Other.Element == ETNBeachElement::PoolOctopus) { continue; }
					if (TNBeachLayout::Clearance(Zone, Other) >= -1.0) { continue; }
					if (bAimed && IsJumpTarget(Other))
					{
						bJumpsSomething = true;
						continue;
					}
					bArcFree = false;
				}
				if (bAimed && !bJumpsSomething && !ArcCrossesTerrain(Zone))
				{
					++Orphans;
					Where += FString::Printf(TEXT(" huérfano %s (%.0f, %.0f) m;"), *UEnum::GetValueAsString(It.Element), It.Pos.X / 100.0, It.Pos.Y / 100.0);
				}
				if (!bArcFree)
				{
					++Blocked;
					Where += FString::Printf(TEXT(" arco pisado %s%s (%.0f, %.0f) m;"), *UEnum::GetValueAsString(It.Element), bAimed ? TEXT(" ante obstáculo") : TEXT(""),
						It.Pos.X / 100.0, It.Pos.Y / 100.0);
				}
			}
			TestEqual(FString::Printf(TEXT("%s: lanzadores huérfanos (%s)"), *Ctx, *Where.Left(300)), Orphans, 0);
			TestEqual(FString::Printf(TEXT("%s: arcos de salto pisados (%s)"), *Ctx, *Where.Left(300)), Blocked, 0);
			TotalOrphans += Orphans;
			TotalBlocked += Blocked;
		}
	}
	AddInfo(FString::Printf(TEXT("Lanzadores en 72 rondas: %d delante de un obstáculo; %d huérfanos y %d arcos pisados."), TotalAimed, TotalOrphans, TotalBlocked));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
