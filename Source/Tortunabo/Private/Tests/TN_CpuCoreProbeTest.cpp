// Sonda de núcleo de CPU de TN.Stress: en una CPU híbrida sabe en qué clase de núcleo corre el hilo de juego.
// Solo lectura: no pide QoS ni cambia los núcleos del proceso (eso lo hace TN.Stress al empezar).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Testing.CpuCoreProbe; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Testing/TN_CpuCoreProbe.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCpuCoreProbeTest,
	"Tortunabo.Testing.CpuCoreProbe",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCpuCoreProbeTest::RunTest(const FString& Parameters)
{
	const int32 Class = TNCpuCore::CurrentEfficiencyClass();
#if PLATFORM_WINDOWS
	TestTrue(TEXT("En Windows el núcleo actual tiene clase de eficiencia"), Class != INDEX_NONE);
#endif
	if (!TNCpuCore::IsHybrid())
	{
		TestFalse(TEXT("Sin CPU híbrida nunca se está en un núcleo de eficiencia"), TNCpuCore::IsOnEfficiencyCore());
		return true;
	}
	// En una híbrida, estar en un núcleo E equivale a tener la clase más baja; la clase se lee en el momento.
	TestTrue(TEXT("Clase válida en CPU híbrida"), Class >= 0);
	return true;
}

#endif
