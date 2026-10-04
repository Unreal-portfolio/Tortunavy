// El equilibrado del combate salió de los constexpr locales de cada actor a UTN_CombatTuning (#81) sin cambiar ningún
// valor. Este test fija los valores efectivos (CDO más lo que diga Config/DefaultGame.ini) a los que había antes:
// si alguien los cambia, que sea a propósito y actualizando aquí la cifra.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Tuning; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Settings/TN_CombatTuning.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNCombatTuningTest
{
	/** Valor que tenía cada constexpr antes de #81 (fichero de origen en el comentario). */
	struct FExpected
	{
		const TCHAR* Name;
		float UTN_CombatTuning::* Member;
		float Value;
	};

	const FExpected Expected[] = {
		{ TEXT("FrisbeeKnockSeconds (TN_RaceFrisbee)"), &UTN_CombatTuning::FrisbeeKnockSeconds, 1.9f },
		{ TEXT("FrisbeeEnemyStunSeconds (TN_RaceFrisbee)"), &UTN_CombatTuning::FrisbeeEnemyStunSeconds, 4.f },
		{ TEXT("GullStrikeKnockSeconds (TN_RaceGullStrike)"), &UTN_CombatTuning::GullStrikeKnockSeconds, 2.6f },
		{ TEXT("GullStrikeEnemyStunSeconds (TN_RaceGullStrike)"), &UTN_CombatTuning::GullStrikeEnemyStunSeconds, 4.f },
		{ TEXT("HomingCrabKnockSeconds (TN_RaceHomingCrab)"), &UTN_CombatTuning::HomingCrabKnockSeconds, 2.2f },
		{ TEXT("HomingCrabEnemyStunSeconds (TN_RaceHomingCrab)"), &UTN_CombatTuning::HomingCrabEnemyStunSeconds, 4.f },
		{ TEXT("MineStunSeconds (TN_RaceMine)"), &UTN_CombatTuning::MineStunSeconds, 3.f },
		{ TEXT("MineEnemyStunSeconds (TN_RaceMine)"), &UTN_CombatTuning::MineEnemyStunSeconds, 5.f },
		{ TEXT("StarKnockSeconds (TN_RaceItems.h)"), &UTN_CombatTuning::StarKnockSeconds, 2.f },
		{ TEXT("StarEnemyStunSeconds (TN_RaceItems.h)"), &UTN_CombatTuning::StarEnemyStunSeconds, 4.f },
		{ TEXT("HermitCrabKnockSeconds (TN_BeachHermitCrab)"), &UTN_CombatTuning::HermitCrabKnockSeconds, 2.5f },
		{ TEXT("HermitCrabIgnoreSeconds (TN_BeachHermitCrab)"), &UTN_CombatTuning::HermitCrabIgnoreSeconds, 3.f },
		{ TEXT("HermitCrabGravity (TN_BeachHermitCrab)"), &UTN_CombatTuning::HermitCrabGravity, 1250.f },
		{ TEXT("SeaUrchinKnockSeconds (TN_BeachSeaUrchin)"), &UTN_CombatTuning::SeaUrchinKnockSeconds, 2.4f },
		{ TEXT("SeaUrchinIgnoreSeconds (TN_BeachSeaUrchin)"), &UTN_CombatTuning::SeaUrchinIgnoreSeconds, 4.5f },
		{ TEXT("GiantCrabStunSeconds (TN_BeachGiantCrab)"), &UTN_CombatTuning::GiantCrabStunSeconds, 3.5f },
		{ TEXT("GiantCrabChargeKnockSeconds (TN_BeachGiantCrab)"), &UTN_CombatTuning::GiantCrabChargeKnockSeconds, 2.4f },
		{ TEXT("GiantCrabCrashStunSeconds (TN_BeachGiantCrab)"), &UTN_CombatTuning::GiantCrabCrashStunSeconds, 1.6f },
		{ TEXT("GiantCrabIgnoreSeconds (TN_BeachGiantCrab)"), &UTN_CombatTuning::GiantCrabIgnoreSeconds, 6.f },
		{ TEXT("PoolOctopusStunExtraSeconds (TN_BeachPoolOctopus)"), &UTN_CombatTuning::PoolOctopusStunExtraSeconds, 1.2f },
		{ TEXT("PoolOctopusIgnoreSeconds (TN_BeachPoolOctopus)"), &UTN_CombatTuning::PoolOctopusIgnoreSeconds, 5.f },
		{ TEXT("PoolOctopusThrowGravity (TN_BeachPoolOctopus)"), &UTN_CombatTuning::PoolOctopusThrowGravity, 980.f },
		{ TEXT("SandFleasDizzySeconds (TN_BeachSandFleas)"), &UTN_CombatTuning::SandFleasDizzySeconds, 1.f },
		{ TEXT("SandFleasIgnoreSeconds (TN_BeachSandFleas)"), &UTN_CombatTuning::SandFleasIgnoreSeconds, 6.f },
		{ TEXT("QuadLaneKnockSeconds (TN_BeachQuadLane)"), &UTN_CombatTuning::QuadLaneKnockSeconds, 3.f },
		{ TEXT("GullZonePoopKnockSeconds (TN_BeachGullZone)"), &UTN_CombatTuning::GullZonePoopKnockSeconds, 2.4f },
		{ TEXT("GullZonePoopIgnoreSeconds (TN_BeachGullZone)"), &UTN_CombatTuning::GullZonePoopIgnoreSeconds, 6.f },
		{ TEXT("GullZoneAfterDropStunSeconds (TN_BeachGullZone)"), &UTN_CombatTuning::GullZoneAfterDropStunSeconds, 2.f },
		{ TEXT("GullZoneGrabIgnoreSeconds (TN_BeachGullZone)"), &UTN_CombatTuning::GullZoneGrabIgnoreSeconds, 12.f },
		{ TEXT("GullZoneGravity (TN_BeachGullZone)"), &UTN_CombatTuning::GullZoneGravity, 980.f },
		{ TEXT("ToyTankHitStunSeconds (TN_BeachToyTank)"), &UTN_CombatTuning::ToyTankHitStunSeconds, 0.8f },
		{ TEXT("ToyTankFoamGravity (TN_BeachToyTank)"), &UTN_CombatTuning::ToyTankFoamGravity, 700.f },
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCombatTuningDefaultsTest,
	"Tortunabo.Tuning.CombatDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCombatTuningDefaultsTest::RunTest(const FString& Parameters)
{
	const UTN_CombatTuning& Tuning = UTN_CombatTuning::Get();
	for (const TNCombatTuningTest::FExpected& Entry : TNCombatTuningTest::Expected)
	{
		TestEqual(Entry.Name, Tuning.*Entry.Member, Entry.Value);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCombatTuningCoverageTest,
	"Tortunabo.Tuning.CombatCoverage",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCombatTuningCoverageTest::RunTest(const FString& Parameters)
{
	// Cada propiedad float de los ajustes tiene su valor esperado arriba: una nueva sin fijar hace fallar el test.
	int32 FloatProperties = 0;
	for (TFieldIterator<FFloatProperty> It(UTN_CombatTuning::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		++FloatProperties;
	}
	TestEqual(TEXT("Propiedades con valor esperado"), FloatProperties, static_cast<int32>(UE_ARRAY_COUNT(TNCombatTuningTest::Expected)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
