// Monkey test y estrés (Source/Tortunabo/Private/Testing): plan reproducible por semilla, detector de atasco, clasificación del
// registro, escenarios de estrés y, de punta a punta, 30 s de monkey en headless sobre el mapa de la partida. Correr con:
//   UnrealEditor-Win64-DebugGame-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Monkey; Quit" -nullrhi -unattended -nosplash
// El test de punta a punta lanza un proceso hijo (-game -nullrhi, LVL_Demo01, -TNMonkey=30:11) y lee su informe JSON:
// falla si hay asserts o ensures, caídas sin rescatar, el proceso cae o no hay informe. Docs/Estres-Monkey-2026-09-29.md.

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Testing/TN_MonkeyPlan.h"
#include "Testing/TN_StressScenarios.h"
#include "Testing/TN_TestLogSink.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNMonkeyTest
{
	TArray<TNMonkey::FStep> Sequence(int32 Seed, int32 Player, int32 Count)
	{
		TNMonkey::FPlanner Planner(Seed, Player);
		TArray<TNMonkey::FStep> Out;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Out.Add(Planner.Next());
		}
		return Out;
	}

	bool SameSteps(const TArray<TNMonkey::FStep>& A, const TArray<TNMonkey::FStep>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (A[Index].Action != B[Index].Action || A[Index].Seconds != B[Index].Seconds || A[Index].Move != B[Index].Move || A[Index].Arg != B[Index].Arg)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMonkeyPlanReproducibleTest, "Tortunabo.Monkey.Plan.Reproducible",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMonkeyPlanReproducibleTest::RunTest(const FString& Parameters)
{
	using namespace TNMonkeyTest;
	TestTrue(TEXT("Misma semilla y mismo jugador: misma secuencia"), SameSteps(Sequence(5, 0, 300), Sequence(5, 0, 300)));
	TestFalse(TEXT("Otra semilla: otra secuencia"), SameSteps(Sequence(5, 0, 300), Sequence(6, 0, 300)));
	TestFalse(TEXT("Otro jugador con la misma semilla: otra secuencia"), SameSteps(Sequence(5, 0, 300), Sequence(5, 1, 300)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMonkeyPlanCoverageTest, "Tortunabo.Monkey.Plan.Coverage",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMonkeyPlanCoverageTest::RunTest(const FString& Parameters)
{
	using TNMonkey::EAction;
	int32 Counts[static_cast<int32>(EAction::Count)] = {};
	int32 WalkForward = 0;
	int32 WalkTotal = 0;
	for (const TNMonkey::FStep& Step : TNMonkeyTest::Sequence(3, 0, 4000))
	{
		++Counts[static_cast<int32>(Step.Action)];
		TestTrue(TEXT("Cada tramo dura algo"), Step.Seconds > 0.f);
		if (Step.Action == EAction::Emote)
		{
			TestTrue(TEXT("El emote está entre 0 y 9"), Step.Arg >= 0 && Step.Arg <= 9);
		}
		if (Step.Action == EAction::Walk || Step.Action == EAction::Sprint)
		{
			++WalkTotal;
			WalkForward += Step.Move.Y > 0.f ? 1 : 0;
			TestTrue(TEXT("El empuje del stick cabe en 1"), Step.Move.Size() <= 1.5f);
		}
	}
	for (int32 Index = 0; Index < static_cast<int32>(EAction::Count); ++Index)
	{
		TestTrue(FString::Printf(TEXT("Sale la acción %s"), TNMonkey::ActionName(static_cast<EAction>(Index))), Counts[Index] > 0);
	}
	TestTrue(TEXT("Andar y correr van casi siempre hacia delante"), WalkTotal > 0 && WalkForward * 100 / WalkTotal >= 75);
	TestTrue(TEXT("Andar y correr son lo más frecuente"), Counts[static_cast<int32>(EAction::Walk)] > Counts[static_cast<int32>(EAction::Emote)]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMonkeyStuckTest, "Tortunabo.Monkey.StuckDetector",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMonkeyStuckTest::RunTest(const FString& Parameters)
{
	{
		TNMonkey::FStuckDetector Detector;
		bool bFired = false;
		for (int32 Tick = 0; Tick < 80; ++Tick)
		{
			bFired |= Detector.Update(0.1f, FVector(10.0, 0.0, 0.0), true);
		}
		TestTrue(TEXT("Empujando 8 s sin moverse es un atasco"), bFired);
	}
	{
		TNMonkey::FStuckDetector Detector;
		bool bFired = false;
		for (int32 Tick = 0; Tick < 200; ++Tick)
		{
			bFired |= Detector.Update(0.1f, FVector(Tick * 20.0, 0.0, 0.0), true);
		}
		TestFalse(TEXT("Avanzando 2 m por segundo no lo es"), bFired);
	}
	{
		TNMonkey::FStuckDetector Detector;
		bool bFired = false;
		for (int32 Tick = 0; Tick < 200; ++Tick)
		{
			bFired |= Detector.Update(0.1f, FVector::ZeroVector, false);
		}
		TestFalse(TEXT("Parada por su gusto (o aturdida) no cuenta"), bFired);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMonkeyFrameStatsTest, "Tortunabo.Monkey.FrameStats",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMonkeyFrameStatsTest::RunTest(const FString& Parameters)
{
	TArray<float> Samples;
	for (int32 Index = 1; Index <= 100; ++Index)
	{
		Samples.Add(static_cast<float>(Index));
	}
	const TNMonkey::FFrameSummary Summary = TNMonkey::Summarize(Samples);
	TestEqual(TEXT("Cuenta"), Summary.Frames, 100);
	TestEqual(TEXT("Media"), Summary.Average, 50.5f, 0.01f);
	TestEqual(TEXT("Mediana"), Summary.P50, 50.5f, 0.01f);
	TestEqual(TEXT("p95"), Summary.P95, 95.05f, 0.01f);
	TestEqual(TEXT("Máximo"), Summary.Max, 100.f, 0.01f);
	TestEqual(TEXT("Sin muestras"), TNMonkey::Summarize(TArray<float>()).Frames, 0);

	// Un pico de 20 ms cada 10 fotogramas de 5 ms: periodo de 65 ms (9 x 5 + 20) y 20 picos.
	TArray<float> Frames;
	TArray<float> Stamps;
	float Clock = 0.f;
	for (int32 Index = 0; Index < 200; ++Index)
	{
		const float Ms = Index % 10 == 9 ? 20.f : 5.f;
		Clock += Ms;
		Frames.Add(Ms);
		Stamps.Add(Clock);
	}
	int32 Spikes = 0;
	float Period = 0.f;
	TNMonkey::FindSpikePeriod(Frames, Stamps, 1.8f, Spikes, Period);
	TestEqual(TEXT("Picos encontrados"), Spikes, 20);
	TestEqual(TEXT("Periodo de los picos"), Period, 65.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMonkeyLogClassifyTest, "Tortunabo.Monkey.LogSink.Classify",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMonkeyLogClassifyTest::RunTest(const FString& Parameters)
{
	using EKind = FTNTestLogSink::EKind;
	const FName Tortunabo(TEXT("LogTortunabo"));
	TestTrue(TEXT("Ensure"), FTNTestLogSink::Classify(TEXT("Ensure condition failed: X [File:Y] [Line: 3]"), FName(TEXT("LogOutputDevice")), ELogVerbosity::Error) == EKind::Ensure);
	TestTrue(TEXT("Caja bajo el terreno"),
		FTNTestLogSink::Classify(TEXT("[Caparazón] TN.Shell.Debug ATN_ShellBody_0 de X en un cliente: caja bajo el terreno · caja en"), Tortunabo, ELogVerbosity::Warning) == EKind::ShellSunk);
	TestTrue(TEXT("Torbellino"),
		FTNTestLogSink::Classify(TEXT("[Caparazón] TN.Shell.Debug A de B en el servidor: torbellino (giro sostenido cerca del tope) · caja"), Tortunabo, ELogVerbosity::Warning) == EKind::ShellSpin);
	TestTrue(TEXT("Salto de velocidad"),
		FTNTestLogSink::Classify(TEXT("[Caparazón] TN.Shell.Debug A de B: salto de velocidad sin lanzamiento · caja"), Tortunabo, ELogVerbosity::Warning) == EKind::ShellVelocityJump);
	TestTrue(TEXT("Corrección de red"),
		FTNTestLogSink::Classify(TEXT("*** Client: Error for BP_Tortuga at Time=1.2 is 40.0"), FName(TEXT("LogNetPlayerMovement")), ELogVerbosity::Warning) == EKind::NetCorrection);
	TestTrue(TEXT("Una línea normal no es nada"), FTNTestLogSink::Classify(TEXT("[Playa] todo bien"), Tortunabo, ELogVerbosity::Log) == EKind::Other);

	FTNTestLogSink Sink;
	Sink.Serialize(TEXT("un error cualquiera 12"), ELogVerbosity::Error, Tortunabo);
	Sink.Serialize(TEXT("un error cualquiera 34"), ELogVerbosity::Error, Tortunabo);
	Sink.Serialize(TEXT("Ensure condition failed: a"), ELogVerbosity::Error, FName(TEXT("LogOutputDevice")));
	TestEqual(TEXT("Errores contados"), Sink.GetErrorCount(), 2);
	TestEqual(TEXT("Ensures aparte de los errores"), Sink.GetEnsureCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNStressScenarioTest, "Tortunabo.Monkey.StressScenarios",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNStressScenarioTest::RunTest(const FString& Parameters)
{
	TNStress::FScenario Light;
	TNStress::FScenario Heavy;
	TNStress::FScenario Tortugas8;
	TNStress::FScenario Nope;
	TestTrue(TEXT("light existe"), TNStress::Parse(TEXT("light"), Light));
	TestTrue(TEXT("heavy existe"), TNStress::Parse(TEXT("HEAVY"), Heavy));
	TestTrue(TEXT("tortugas8 existe"), TNStress::Parse(TEXT("tortugas8"), Tortugas8));
	TestFalse(TEXT("otro nombre no existe"), TNStress::Parse(TEXT("extreme"), Nope));
	TestEqual(TEXT("light = 50 enemigos"), Light.Enemies, 50);
	TestEqual(TEXT("heavy = 200 enemigos"), Heavy.Enemies, 200);
	TestEqual(TEXT("tortugas8 son ocho tortugas"), Tortugas8.Turtles, 8);

	for (const int32 Enemies : { 0, 1, 3, 50, 200 })
	{
		const TNStress::FEnemySplit Split = TNStress::SplitEnemies(Enemies);
		TestEqual(FString::Printf(TEXT("El reparto de %d suma %d"), Enemies, Enemies), Split.Total(), Enemies);
	}
	const TArray<TNStress::FPhase> Timeline = TNStress::BuildTimeline(Heavy, 60.f);
	TestEqual(TEXT("heavy: referencia + 3 de enemigos"), Timeline.Num(), 4);
	if (Timeline.Num() == 4)
	{
		TestTrue(TEXT("La primera fase es la de referencia"), Timeline[0].Group == TNStress::EGroup::Baseline);
		TestEqual(TEXT("La última acaba a los 60 s"), Timeline.Last().End, 60.f, 0.001f);
		TestEqual(TEXT("Cada fase dura 15 s"), Timeline[3].End - Timeline[3].Start, 15.f, 0.001f);
	}
	TestEqual(TEXT("tortugas8 solo tiene la fase de referencia"), TNStress::BuildTimeline(Tortugas8, 60.f).Num(), 1);
	TNStress::FScenario Control;
	TestTrue(TEXT("control existe"), TNStress::Parse(TEXT("control"), Control));
	TestEqual(TEXT("control son seis fases de referencia"), TNStress::BuildTimeline(Control, 60.f).Num(), 6);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// De punta a punta: 30 s de monkey en headless sobre LVL_Demo01 (proceso hijo)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMonkeyHeadlessTest, "Tortunabo.Monkey.Headless30s",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNMonkeyHeadlessTest::RunTest(const FString& Parameters)
{
	const FString Exe = FPlatformProcess::ExecutablePath();
	const FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString Report = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Monkey"), TEXT("automation-headless30s.json")));
	const FString ChildLog = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Monkey"), TEXT("automation-headless30s.log")));
	IFileManager::Get().Delete(*Report, false, true);

	// Con -nullrhi el mapa carga y corre entero (probado): no hace falta un mapa alternativo.
	const FString Args = FString::Printf(
		TEXT("\"%s\" /Game/Maps/Run/LVL_Demo01 -game -nullrhi -unattended -nosplash -nosound -NoVerifyGC -log -abslog=\"%s\" -TNMonkey=30:11 -TNMonkeyWarmup=10 -TNMonkeyOut=\"%s\" -TNQuitWhenDone"),
		*Project, *ChildLog, *Report);
	uint32 ProcessId = 0;
	FProcHandle Handle = FPlatformProcess::CreateProc(*Exe, *Args, true, true, true, &ProcessId, 0, nullptr, nullptr);
	if (!TestTrue(TEXT("Arranca el proceso hijo"), Handle.IsValid()))
	{
		return false;
	}
	const double Deadline = FPlatformTime::Seconds() + 420.0;
	while (FPlatformProcess::IsProcRunning(Handle) && FPlatformTime::Seconds() < Deadline)
	{
		FPlatformProcess::Sleep(0.5f);
	}
	if (FPlatformProcess::IsProcRunning(Handle))
	{
		FPlatformProcess::TerminateProc(Handle, true);
		AddError(TEXT("El monkey no ha terminado en 420 s (proceso matado)."));
	}
	int32 ExitCode = -1;
	FPlatformProcess::GetProcReturnCode(Handle, &ExitCode);
	FPlatformProcess::CloseProc(Handle);

	FString Text;
	if (!TestTrue(FString::Printf(TEXT("El informe existe (%s, código de salida %d, registro %s)"), *Report, ExitCode, *ChildLog), FFileHelper::LoadFileToString(Text, *Report)))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Json;
	if (!TestTrue(TEXT("El informe es JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid()))
	{
		return false;
	}
	TestFalse(TEXT("El proceso no ha caído"), Json->GetBoolField(TEXT("crashed")));
	TestEqual(TEXT("Cero asserts o ensures"), static_cast<int32>(Json->GetNumberField(TEXT("asserts_ensures"))), 0);
	TestEqual(TEXT("Cero caídas sin rescatar"), static_cast<int32>(Json->GetNumberField(TEXT("unrescued_falls"))), 0);
	TestTrue(TEXT("Hay al menos un jugador"), Json->GetNumberField(TEXT("players")) >= 1.0);
	TestTrue(TEXT("Ha corrido casi los 30 s"), Json->GetNumberField(TEXT("elapsed_seconds")) >= 28.0);
	const TSharedPtr<FJsonObject>* Actions = nullptr;
	if (TestTrue(TEXT("Cuenta las acciones"), Json->TryGetObjectField(TEXT("actions_total"), Actions)) && Actions)
	{
		TestTrue(TEXT("Ha metido entrada de movimiento"), (*Actions)->GetNumberField(TEXT("Walk")) + (*Actions)->GetNumberField(TEXT("Sprint")) > 0.0);
	}
	const TArray<TSharedPtr<FJsonValue>>* Failures = nullptr;
	if (Json->TryGetArrayField(TEXT("failures"), Failures) && Failures)
	{
		for (const TSharedPtr<FJsonValue>& Failure : *Failures)
		{
			AddError(FString::Printf(TEXT("Monkey: %s (semilla 11; informe %s)"), *Failure->AsString(), *Report));
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
