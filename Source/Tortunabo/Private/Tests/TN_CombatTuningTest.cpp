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
		{ TEXT("QuadLaneKnockSeconds (TN_BeachQuadLane)"), &UTN_CombatTuning::QuadLaneKnockSeconds, 3.f },
		{ TEXT("GullZonePoopKnockSeconds (TN_BeachGullZone)"), &UTN_CombatTuning::GullZonePoopKnockSeconds, 2.4f },
		{ TEXT("GullZonePoopIgnoreSeconds (TN_BeachGullZone)"), &UTN_CombatTuning::GullZonePoopIgnoreSeconds, 6.f },
		{ TEXT("GullZoneAfterDropStunSeconds (TN_BeachGullZone)"), &UTN_CombatTuning::GullZoneAfterDropStunSeconds, 2.f },
		{ TEXT("GullZoneGrabIgnoreSeconds (TN_BeachGullZone)"), &UTN_CombatTuning::GullZoneGrabIgnoreSeconds, 12.f },
		{ TEXT("GullZoneGravity (TN_BeachGullZone)"), &UTN_CombatTuning::GullZoneGravity, 980.f },
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
