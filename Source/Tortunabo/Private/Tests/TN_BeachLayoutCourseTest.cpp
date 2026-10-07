// Reparto de la carrera sobre las rondas del dorado (24 semillas × 3 dificultades, TN_BeachLayoutGoldenTest.cpp): lo que
// el GameMode hace con la ronda después de repartirla. El sprint final despeja el nido de los huevos
// (lo que hacía el GameMode de la antigua carrera al poner a las finalistas): el reparto de sprint lo deja vacío para que
// ese despeje no corte nada (#346).
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
	 * Lo que quitaría el despeje del nido con el círculo de SprintNestCircle (todos los huevos del nido y su margen): cada
	 * elemento cuya huella (disco o cápsula) toca el círculo.
	 */
	bool IsCutBySprintNest(const TNBeachLayout::FItem& Item, const FVector2D& Center, double Radius)
	{
		double T = 0.0;
		return TNProcMap::DistPointSegment(Center, Item.EndA(), Item.EndB(), T) <= Radius + Item.Radius;
	}

	/**
	 * Lo que un lanzador delante de un obstáculo (rol Launcher) puede pisar con su arco de salto porque es lo que salta:
	 * castillos enormes, piezas de las filas y plataformas.
	 */
	bool IsJumpTarget(const TNBeachLayout::FItem& Item)
	{
		using TNBeachLayout::EItemRole;
		return Item.Role == EItemRole::Row || Item.Role == EItemRole::Castle || Item.Element == ETNBeachElement::SandCastleHuge
			|| Item.Element == ETNBeachElement::WobblyPlatform;
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

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachCourseSprintTest,
	"Tortunabo.Beach.Course.Sprint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachCourseSprintTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachCourseTest;
	FVector2D NestCenter;
	double NestRadius = 0.0;
	TNBeachLayout::SprintNestCircle(NestCenter, NestRadius);
	int32 Rounds = 0;
	int32 RoundsClean = 0;
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
			FString CutNames;
			for (const TNBeachLayout::FItem& It : L.Items)
			{
				if (!IsCutBySprintNest(It, NestCenter, NestRadius)) { continue; }
				++Cut;
				if (Cut <= 4) { CutNames += FString::Printf(TEXT(" %s (%.0f, %.0f) m"), *UEnum::GetValueAsString(It.Element), It.Pos.X / 100.0, It.Pos.Y / 100.0); }
			}
			TestEqual(FString::Printf(TEXT("%s: el despeje del nido no quita nada (%d:%s)"), *Ctx, Cut, *CutNames), Cut, 0);
			RoundsClean += Cut == 0 ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("Sprint: nido intacto en %d de %d rondas (en %.0f m con radio %.0f m)."),
		RoundsClean, Rounds, NestCenter.X / 100.0, NestRadius / 100.0));
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
	// por debajo, como en Tortunabo.Beach.Layout.Rules).
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
					if (j == i || Other.bOverlay || Other.Element == ETNBeachElement::QuadLane) { continue; }
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
