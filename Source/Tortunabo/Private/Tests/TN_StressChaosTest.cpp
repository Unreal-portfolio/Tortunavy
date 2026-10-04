// Escenario de estrés «caos» (Testing/TN_StressChaosPlan.h): fases que suman carga y elección de tareas. Correr con:
//   UnrealEditor-Win64-DebugGame-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Stress.Chaos; Quit" -nullrhi -unattended -nosplash

#include "Misc/AutomationTest.h"
#include "Testing/TN_StressChaosPlan.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNStressChaosTimelineTest, "Tortunabo.Stress.Chaos.Timeline",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNStressChaosTimelineTest::RunTest(const FString& Parameters)
{
	using namespace TNChaos;
	FConfig Config;
	Config.PhaseSeconds = 20.f;
	const TArray<FPhase> Phases = BuildTimeline(Config);
	TestEqual(TEXT("Siete fases"), Phases.Num(), static_cast<int32>(EStep::Count));
	TestEqual(TEXT("Dura 7 x 20 s"), Phases.Last().End, 140.f);
	TestEqual(TEXT("La referencia solo anda"), static_cast<int32>(Phases[0].Tasks), static_cast<int32>(TaskBit(ETask::Wander)));
	for (int32 Index = 1; Index < Phases.Num(); ++Index)
	{
		TestEqual(TEXT("Fases seguidas, sin huecos"), Phases[Index].Start, Phases[Index - 1].End);
		TestTrue(TEXT("Cada fase permite lo de la anterior"), (Phases[Index].Tasks & Phases[Index - 1].Tasks) == Phases[Index - 1].Tasks);
	}
	TestEqual(TEXT("Las catapultas se ponen en su fase"), Phases[static_cast<int32>(EStep::Catapults)].Catapults, Config.Catapults);
	int32 EnemiesBefore = 0;
	for (int32 Index = 0; Index < static_cast<int32>(EStep::Enemies); ++Index)
	{
		EnemiesBefore += Phases[Index].Crabs + Phases[Index].Gulls + Phases[Index].Tanks;
	}
	TestEqual(TEXT("Sin enemigos antes de su fase"), EnemiesBefore, 0);
	const FPhase& Enemies = Phases[static_cast<int32>(EStep::Enemies)];
	const FPhase& Peak = Phases[static_cast<int32>(EStep::Peak)];
	TestEqual(TEXT("12 cangrejos por tanda"), Enemies.Crabs, 12);
	TestTrue(TEXT("El pico vuelve a crear enemigos (el doble en total)"), Peak.Crabs == Enemies.Crabs && Peak.Tanks == Enemies.Tanks);
	TestTrue(TEXT("El pico pone más catapultas"), Peak.Catapults > 0);
	TestTrue(TEXT("El pico lanza objetos más seguido"), Peak.ItemEverySeconds > 0.f && Peak.ItemEverySeconds < Enemies.ItemEverySeconds);
	TestEqual(TEXT("Antes de la fase de objetos no se lanza nada"), Phases[static_cast<int32>(EStep::Ball)].ItemEverySeconds, 0.f);

	FConfig NoEnemies;
	NoEnemies.EnemyScale = 0.f;
	TestEqual(TEXT("Escala 0: sin enemigos"), BuildTimeline(NoEnemies)[static_cast<int32>(EStep::Peak)].Crabs, 0);
	FConfig Double;
	Double.EnemyScale = 2.f;
	TestEqual(TEXT("Escala 2: el doble"), BuildTimeline(Double)[static_cast<int32>(EStep::Enemies)].Tanks, 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNStressChaosPickTaskTest, "Tortunabo.Stress.Chaos.PickTask",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNStressChaosPickTaskTest::RunTest(const FString& Parameters)
{
	using namespace TNChaos;
	FRandomStream Stream(585);
	const uint8 Ball = TaskBit(ETask::Wander) | TaskBit(ETask::Ball);
	int32 Counts[static_cast<int32>(ETask::Count)] = {};
	for (int32 Index = 0; Index < 2000; ++Index)
	{
		++Counts[static_cast<int32>(PickTask(Stream, Ball, true))];
	}
	TestEqual(TEXT("Nunca una tarea no permitida (catapulta)"), Counts[static_cast<int32>(ETask::Catapult)], 0);
	TestTrue(TEXT("Salen las permitidas"), Counts[static_cast<int32>(ETask::Ball)] > 0 && Counts[static_cast<int32>(ETask::Wander)] > 0);

	const uint8 All = TaskBit(ETask::Wander) | TaskBit(ETask::Carry) | TaskBit(ETask::Catapult);
	bool bCarryWithoutPartner = false;
	for (int32 Index = 0; Index < 2000; ++Index)
	{
		bCarryWithoutPartner |= PickTask(Stream, All, false) == ETask::Carry;
	}
	TestFalse(TEXT("Sin pareja no se coge a nadie"), bCarryWithoutPartner);
	int32 Focused[static_cast<int32>(ETask::Count)] = {};
	for (int32 Index = 0; Index < 2000; ++Index)
	{
		++Focused[static_cast<int32>(PickTask(Stream, Ball, true, FocusOf(EStep::Ball)))];
	}
	TestTrue(TEXT("La tarea que estrena la fase sale mucho más"), Focused[static_cast<int32>(ETask::Ball)] > 4 * Focused[static_cast<int32>(ETask::Wander)]);
	TestTrue(TEXT("Máscara vacía: andar"), PickTask(Stream, 0, true) == ETask::Wander);

	FRandomStream A(7);
	FRandomStream B(7);
	bool bSame = true;
	for (int32 Index = 0; Index < 200; ++Index)
	{
		bSame &= PickTask(A, All, true) == PickTask(B, All, true);
	}
	TestTrue(TEXT("Misma semilla: mismas tareas"), bSame);
	TestTrue(TEXT("caos y chaos valen"), IsChaosName(TEXT("CAOS")) && IsChaosName(TEXT("chaos")) && !IsChaosName(TEXT("heavy")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNStressChaosShellGateTest, "Tortunabo.Stress.Chaos.ShellGate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNStressChaosShellGateTest::RunTest(const FString& Parameters)
{
	using namespace TNChaos;
	// Un cliente que no ve todavía el cambio de IsInShell pide pulsar en cada fotograma de 3 s a 60 fps.
	FShellGate Gate;
	int32 Presses = 0;
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		Presses += Gate.TryPress() ? 1 : 0;
		Gate.Tick(1.f / 60.f);
	}
	TestEqual(TEXT("Una pulsación por segundo, no una por fotograma"), Presses, 3);

	FShellGate Fresh;
	TestTrue(TEXT("La primera pulsación sale al momento"), Fresh.TryPress());
	TestFalse(TEXT("La segunda espera"), Fresh.TryPress());
	return true;
}

#endif
