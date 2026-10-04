// Ray tracing por calidad (#562): con iluminación global Bajo, Medio o Alto, r.RayTracing.Enable vale 0 (sin BLAS del
// terreno ProcMesh ni recogida de instancias en el hilo de render); en Épico y Cine, 1. Se testea la regla
// (TNRayTracingQuality) y, con el motor de verdad, que UTN_RayTracingQualitySubsystem sigue a sg.GlobalIlluminationQuality.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Render.RayTracingQuality; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
#include "Scalability.h"
#include "Settings/TN_RayTracingQualitySubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRayTracingQualityRuleTest,
	"Tortunabo.Render.RayTracingQuality.Rule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRayTracingQualityRuleTest::RunTest(const FString& Parameters)
{
	using namespace TNRayTracingQuality;
	TestFalse(TEXT("Bajo: sin ray tracing"), ShouldEnable(0));
	TestFalse(TEXT("Medio: sin ray tracing"), ShouldEnable(1));
	TestFalse(TEXT("Alto: sin ray tracing (Lumen por software)"), ShouldEnable(2));
	TestTrue(TEXT("Épico: con ray tracing"), ShouldEnable(3));
	TestTrue(TEXT("Cine: con ray tracing"), ShouldEnable(4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRayTracingQualityFollowTest,
	"Tortunabo.Render.RayTracingQuality.FollowsGlobalIllumination",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRayTracingQualityFollowTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* Quality = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.GlobalIlluminationQuality"));
	IConsoleVariable* RayTracing = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Enable"));
	if (!TestNotNull(TEXT("sg.GlobalIlluminationQuality existe"), Quality)) { return false; }
	if (!RayTracing)
	{
		AddInfo(TEXT("Plataforma sin ray tracing (r.RayTracing.Enable no existe): nada que comprobar."));
		return true;
	}
	if ((static_cast<uint32>(RayTracing->GetFlags()) & ECVF_SetByMask) > static_cast<uint32>(ECVF_SetByScalability))
	{
		AddInfo(TEXT("r.RayTracing.Enable puesto a mano con más prioridad: el subsistema no lo toca (comportamiento esperado)."));
		return true;
	}

	// Por el mismo camino que el menú de ajustes (Scalability::SetQualityLevels), y se deja como estaba.
	const Scalability::FQualityLevels Original = Scalability::GetQualityLevels();
	const auto SetGlobalIllumination = [&Original](int32 Level)
	{
		Scalability::FQualityLevels Levels = Original;
		Levels.GlobalIlluminationQuality = Level;
		Scalability::SetQualityLevels(Levels);
	};
	SetGlobalIllumination(2);
	TestEqual(TEXT("Iluminación global Alta: r.RayTracing.Enable = 0"), RayTracing->GetInt(), 0);
	SetGlobalIllumination(1);
	TestEqual(TEXT("Iluminación global Media: r.RayTracing.Enable = 0"), RayTracing->GetInt(), 0);
	SetGlobalIllumination(3);
	TestEqual(TEXT("Iluminación global Épica: r.RayTracing.Enable = 1"), RayTracing->GetInt(), 1);
	TestEqual(TEXT("sg.GlobalIlluminationQuality sigue al menú"), Quality->GetInt(), 3);
	Scalability::SetQualityLevels(Original);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
