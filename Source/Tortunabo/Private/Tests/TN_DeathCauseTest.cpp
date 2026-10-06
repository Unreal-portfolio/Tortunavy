// Textos de las causas de eliminación (TN_DeathCause.h, #728). Sin mundo. Correr desde Session Frontend (categoría
// "Tortunabo.DeathCause") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.DeathCause; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Core/TN_DeathCause.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNDeathCauseTextTest,
	"Tortunabo.DeathCause.Text",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNDeathCauseTextTest::RunTest(const FString& Parameters)
{
	const FString Unknown = TNDeathCause::Describe(ETNDeathCause::Unknown).ToString();
	TestFalse(TEXT("Sin causa conocida: tiene texto"), Unknown.IsEmpty());

	// Cada causa, su texto propio: ninguna se queda en el «Eliminado» de sin causa ni repite el de otra.
	TSet<FString> Seen;
	for (int32 Index = static_cast<int32>(ETNDeathCause::Unknown) + 1; Index < static_cast<int32>(ETNDeathCause::Count); ++Index)
	{
		const ETNDeathCause Cause = static_cast<ETNDeathCause>(Index);
		const FString Name = UEnum::GetValueAsString(Cause);
		const FString Text = TNDeathCause::Describe(Cause).ToString();
		TestFalse(FString::Printf(TEXT("%s: tiene texto"), *Name), Text.IsEmpty());
		TestNotEqual(FString::Printf(TEXT("%s: no es el de sin causa"), *Name), Text, Unknown);
		TestFalse(FString::Printf(TEXT("%s: no repite el de otra causa"), *Name), Seen.Contains(Text));
		Seen.Add(Text);
	}

	TestTrue(TEXT("Sin quién la pide: sin causa"), TNDeathCause::FromInstigator(nullptr, nullptr) == ETNDeathCause::Unknown);
	return true;
}

#endif
