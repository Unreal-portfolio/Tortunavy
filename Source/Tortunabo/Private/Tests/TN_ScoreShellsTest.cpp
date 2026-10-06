// Conchas de puntos: tamaños y valores, reparto de una recogida en iconos del HUD y escala del «pom» (TN_ScoreShells.h).
// Sin mundo ni actores: se testea el mismo código que usan ATN_ScorePickup y UTN_RunHUDWidget. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.ScoreShells; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/TN_ScoreShells.h"

#if WITH_DEV_AUTOMATION_TESTS

// ─────────────────────────────────────────────────────────────────────────────
// Tamaños, valores, iconos del HUD y «pom»
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNScoreShellsSplitTest,
	"Tortunabo.ScoreShells.Split",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNScoreShellsSplitTest::RunTest(const FString& Parameters)
{
	using namespace TNScoreShells;

	TestEqual(TEXT("pequeña de 1"), ValueOf(ETier::Small), 1);
	TestEqual(TEXT("normal de 25 (la de siempre)"), ValueOf(ETier::Normal), 25);
	TestEqual(TEXT("grande de 50"), ValueOf(ETier::Big), 50);
	TestEqual(TEXT("reina de 100"), ValueOf(ETier::Grand), 100);
	for (int32 t = 0; t < NumTiers; ++t)
	{
		const ETier Tier = TierFromIndex(t);
		TestTrue(FString::Printf(TEXT("el valor del tamaño %d da ese tamaño"), t), TierForValue(ValueOf(Tier)) == Tier);
		if (t > 0)
		{
			const ETier Smaller = TierFromIndex(t - 1);
			TestTrue(FString::Printf(TEXT("tamaño %d: más grande y con más radio de recogida que el anterior"), t),
				MeshScale(Tier) > MeshScale(Smaller) && CollectRadius(Tier) > CollectRadius(Smaller));
		}
	}
	TestTrue(TEXT("un Blueprint de 10 puntos es normal"), TierForValue(10) == ETier::Normal);
	TestTrue(TEXT("uno de 70, grande"), TierForValue(70) == ETier::Big);
	TestTrue(TEXT("uno de 300, reina"), TierForValue(300) == ETier::Grand);
	TestTrue(TEXT("índices de red fuera de rango, acotados"), TierFromIndex(-3) == ETier::Small && TierFromIndex(9) == ETier::Grand);

	TestEqual(TEXT("1 punto → 1 icono"), IconCountFor(1), 1);
	TestEqual(TEXT("25 puntos → 10 iconos"), IconCountFor(25), 10);
	TestEqual(TEXT("50 puntos → 14 iconos"), IconCountFor(50), 14);
	TestEqual(TEXT("100 puntos → 15 iconos (el tope)"), IconCountFor(100), MaxIcons);
	TestEqual(TEXT("0 puntos → ningún icono"), IconCountFor(0), 0);

	bool bSums = true, bBounds = true, bCrescendo = true;
	for (int32 Value = 1; Value <= 400; ++Value)
	{
		TArray<int32> Parts;
		SplitIntoIcons(Value, MaxIcons, Parts);
		int32 Sum = 0;
		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			Sum += Parts[i];
			bBounds &= Parts[i] >= 1;
			if (i > 0) { bCrescendo &= Parts[i] >= Parts[i - 1]; }
		}
		bSums &= Sum == Value;
		bBounds &= Parts.Num() == IconCountFor(Value) && Parts.Num() >= 1 && Parts.Num() <= MaxIcons && Parts.Num() <= Value;
	}
	TestTrue(TEXT("los iconos suman exactamente el valor"), bSums);
	TestTrue(TEXT("cada icono vale al menos 1 y no hay más de 15"), bBounds);
	TestTrue(TEXT("el resto va a los últimos (el contador acelera al final)"), bCrescendo);
	{
		TArray<int32> Parts;
		SplitIntoIcons(0, MaxIcons, Parts);
		TestEqual(TEXT("nada que repartir → sin iconos"), Parts.Num(), 0);
		SplitIntoIcons(-5, MaxIcons, Parts);
		TestEqual(TEXT("valor negativo → sin iconos"), Parts.Num(), 0);
	}

	TestEqual(TEXT("el primer «pom» en la nota base"), PomSemitones(0), 0);
	bool bRises = true;
	for (int32 Step = 1; Step <= PomTopStep + 5; ++Step) { bRises &= PomSemitones(Step) >= PomSemitones(Step - 1); }
	TestTrue(TEXT("el «pom» nunca baja dentro de una tanda"), bRises);
	TestEqual(TEXT("sube dos octavas y ahí se queda"), PomSemitones(PomTopStep + 7), 24);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
