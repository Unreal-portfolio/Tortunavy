// Motor DSP de los ruidos del cuerpo de la tortuga (TN_TurtleFoleyDSP.h), fuera del motor de audio: las brazadas al
// nadar y meterse o salir del caparazón (#635), con las pisadas de siempre como referencia. Sin mundo ni dispositivo
// de audio. Correr desde Session Frontend (categoría "Tortunabo.Audio.TurtleFoley") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Audio.TurtleFoley; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_TurtleAnimInstance.h"
#include "Player/TN_TurtleFoleyDSP.h"
#include "Templates/UniquePtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTurtleFoleyDSPTest
{
	constexpr float SampleRate = 48000.f;
	constexpr int32 ChunkFrames = 480;

	/** Lo que sale de una tirada: pico, si todo es finito, pico de los últimos 0,2 s y si el motor sigue ocupado. */
	struct FRenderStats
	{
		float Peak = 0.f;
		float TailPeak = 0.f;
		bool bFinite = true;
		bool bBusyAtEnd = true;
	};

	/** Un evento a la fuerza dada (sin superficie: la brazada y el caparazón no la usan; el paso, roca). */
	static TNTurtleFoley::FStepEvent MakeEvent(uint8 Kind, uint8 Foot, float Force, float Pace)
	{
		TNTurtleFoley::FStepEvent Ev;
		Ev.Kind = Kind;
		Ev.Foot = Foot;
		Ev.Force = Force;
		Ev.Pace = Pace;
		return Ev;
	}

	/**
	 * Arranca un motor con la semilla dada, deja cada evento en el anillo en su segundo (Times) y renderiza Seconds
	 * segundos con Master de ganancia final.
	 */
	static FRenderStats Render(const TArray<TNTurtleFoley::FStepEvent>& Events, const TArray<float>& Times, float Master,
		uint32 Seed, float Seconds)
	{
		TUniquePtr<TNTurtleFoley::FSharedParams> Params = MakeUnique<TNTurtleFoley::FSharedParams>();
		TUniquePtr<TNTurtleFoley::FEngine> Engine = MakeUnique<TNTurtleFoley::FEngine>();
		TNTurtleFoley::FSharedParams::Set(Params->Master, Master);
		Engine->Init(SampleRate, Seed, TNTurtleFoley::MixSeed(Seed, 77u), 0u, 0u);

		FRenderStats Stats;
		TArray<float> Chunk;
		Chunk.SetNumZeroed(ChunkFrames);
		const int32 TotalFrames = static_cast<int32>(Seconds * SampleRate);
		const int32 TailStart = TotalFrames - static_cast<int32>(0.2f * SampleRate);
		int32 NextEvent = 0;
		for (int32 Done = 0; Done < TotalFrames; Done += ChunkFrames)
		{
			const float Now = static_cast<float>(Done) / SampleRate;
			while (NextEvent < Events.Num() && Times[NextEvent] <= Now)
			{
				Params->PushStep(Events[NextEvent]);
				++NextEvent;
			}
			Engine->Render(Chunk.GetData(), ChunkFrames, 1, *Params);
			for (int32 i = 0; i < ChunkFrames; ++i)
			{
				const float X = Chunk[i];
				if (!FMath::IsFinite(X))
				{
					Stats.bFinite = false;
					continue;
				}
				Stats.Peak = FMath::Max(Stats.Peak, FMath::Abs(X));
				if (Done + i >= TailStart)
				{
					Stats.TailPeak = FMath::Max(Stats.TailPeak, FMath::Abs(X));
				}
			}
		}
		Stats.bBusyAtEnd = Params->Busy.load() != 0;
		return Stats;
	}

	static FRenderStats RenderOne(const TNTurtleFoley::FStepEvent& Ev, float Master, uint32 Seed)
	{
		return Render({ Ev }, { 0.f }, Master, Seed, 2.f);
	}

	static float ToDb(float Peak)
	{
		return 20.f * FMath::LogX(10.f, FMath::Max(Peak, 1e-6f));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Brazadas y caparazón: sin NaN, sin saturación, que suenen y que acaben
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTurtleFoleySwimShellLevelsTest,
	"Tortunabo.Audio.TurtleFoley.SwimAndShellLevels",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTurtleFoleySwimShellLevelsTest::RunTest(const FString& Parameters)
{
	using namespace TNTurtleFoleyDSPTest;
	namespace Kind = TNTurtleFoley::StepKind;

	struct FCase
	{
		const TCHAR* Name;
		TNTurtleFoley::FStepEvent Ev;
	};
	const FCase Cases[] = {
		{ TEXT("Brazada flotando (0,35)"), MakeEvent(Kind::Stroke, 0, 0.35f, 0.f) },
		{ TEXT("Brazada a toda velocidad (0,85)"), MakeEvent(Kind::Stroke, 1, 0.85f, 1.f) },
		{ TEXT("Brazada fuera de rango (1,5)"), MakeEvent(Kind::Stroke, 0, 1.5f, 1.f) },
		{ TEXT("Meterse en el caparazón"), MakeEvent(Kind::Shell, 0, 0.75f, 0.4f) },
		{ TEXT("Salir del caparazón"), MakeEvent(Kind::Shell, 1, 0.6f, 0.4f) },
		{ TEXT("Paso andando (referencia)"), MakeEvent(Kind::Step, 0, 0.65f, 0.2f) },
		{ TEXT("Aterrizaje fuerte (referencia)"), MakeEvent(Kind::Land, 0, 1.35f, 0.5f) },
	};

	for (const FCase& Case : Cases)
	{
		float WorstPeak = 0.f;
		float QuietestPeak = 1.f;
		for (uint32 Seed = 1u; Seed <= 12u; ++Seed)
		{
			const FRenderStats Stats = RenderOne(Case.Ev, 1.f, Seed * 2654435761u);
			if (!Stats.bFinite)
			{
				AddError(FString::Printf(TEXT("%s (semilla %u): sale NaN o infinito."), Case.Name, Seed));
			}
			WorstPeak = FMath::Max(WorstPeak, Stats.Peak);
			QuietestPeak = FMath::Min(QuietestPeak, Stats.Peak);
			// Acaba: la voz se libera, el motor deja de estar ocupado y la cola llega al silencio.
			TestFalse(FString::Printf(TEXT("%s (semilla %u): el motor deja de estar ocupado"), Case.Name, Seed), Stats.bBusyAtEnd);
			TestTrue(FString::Printf(TEXT("%s (semilla %u): silencio al final (pico %.5f)"), Case.Name, Seed, Stats.TailPeak),
				Stats.TailPeak < 1e-3f);
		}
		// Sin saturación: con Master 1 ni siquiera llega al umbral del limitador (-2 dBFS).
		TestTrue(FString::Printf(TEXT("%s: pico %.1f dBFS por debajo del limitador"), Case.Name, ToDb(WorstPeak)),
			WorstPeak <= TNTurtleFoley::LimThreshold);
		// Que se oiga: por encima de -45 dBFS en la tirada más floja.
		TestTrue(FString::Printf(TEXT("%s: suena (pico más flojo %.1f dBFS)"), Case.Name, ToDb(QuietestPeak)), QuietestPeak > 0.0056f);
		AddInfo(FString::Printf(TEXT("%s: picos de %.1f a %.1f dBFS"), Case.Name, ToDb(QuietestPeak), ToDb(WorstPeak)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTurtleFoleySwimShellStressTest,
	"Tortunabo.Audio.TurtleFoley.SwimAndShellStress",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTurtleFoleySwimShellStressTest::RunTest(const FString& Parameters)
{
	using namespace TNTurtleFoleyDSPTest;
	namespace Kind = TNTurtleFoley::StepKind;

	// Brazadas cada 0,25 s (mucho más seguidas que el nado, 1,18 s) con caparazones entre medias: se pisan las seis voces.
	TArray<TNTurtleFoley::FStepEvent> Events;
	TArray<float> Times;
	for (int32 k = 0; k < 16; ++k)
	{
		Events.Add(MakeEvent(Kind::Stroke, static_cast<uint8>(k & 1), 0.85f, 1.f));
		Times.Add(0.25f * static_cast<float>(k));
		if (k % 3 == 0)
		{
			Events.Add(MakeEvent(Kind::Shell, static_cast<uint8>((k / 3) & 1), 0.75f, 0.4f));
			Times.Add(0.25f * static_cast<float>(k) + 0.1f);
		}
	}

	// Volumen normal y el más alto posible (Loudness 4 × refuerzo de la tortuga local 1,3).
	const float Masters[] = { 1.f, 5.2f };
	for (const float Master : Masters)
	{
		const FRenderStats Stats = Render(Events, Times, Master, 0xBEEFu, 6.f);
		TestTrue(FString::Printf(TEXT("Master %.1f: todo finito"), Master), Stats.bFinite);
		TestTrue(FString::Printf(TEXT("Master %.1f: nunca pasa de ±1 (pico %.3f)"), Master, Stats.Peak), Stats.Peak <= 1.f);
		TestFalse(FString::Printf(TEXT("Master %.1f: acaba callado"), Master), Stats.bBusyAtEnd);
		if (Master <= 1.f)
		{
			TestTrue(FString::Printf(TEXT("Master 1: sin llegar al limitador (pico %.1f dBFS)"), ToDb(Stats.Peak)),
				Stats.Peak <= TNTurtleFoley::LimThreshold);
		}
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Ritmo de las brazadas: una por ciclo de la animación
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTurtleFoleyStrokePhaseTest,
	"Tortunabo.Audio.TurtleFoley.StrokePhase",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTurtleFoleyStrokePhaseTest::RunTest(const FString& Parameters)
{
	using TNTurtleFoley::CrossedPhase;
	const float At = TNTurtleFoley::SwimStrokeAt;
	TestTrue(TEXT("De 0,20 a 0,30 pasa por 0,25"), CrossedPhase(0.2f, 0.3f, At));
	TestTrue(TEXT("Llegar justo a 0,25 cuenta"), CrossedPhase(0.24f, 0.25f, At));
	TestFalse(TEXT("Salir justo de 0,25 no vuelve a contar"), CrossedPhase(0.25f, 0.3f, At));
	TestFalse(TEXT("De 0,26 a 0,30 ya había pasado"), CrossedPhase(0.26f, 0.3f, At));
	TestFalse(TEXT("Parada (misma fase) no cuenta"), CrossedPhase(0.2f, 0.2f, At));
	TestTrue(TEXT("Con la vuelta de 0,9 a 0,3 pasa por 0,25"), CrossedPhase(0.9f, 0.3f, At));
	TestFalse(TEXT("Con la vuelta de 0,9 a 0,1 aún no"), CrossedPhase(0.9f, 0.1f, At));

	// Un minuto a 60 fps al ritmo de la animación: una brazada por ciclo (0,85 por segundo → 51).
	const float StrokeHz = UTN_TurtleAnimInstance::SwimStrokeHz;
	const float Dt = 1.f / 60.f;
	float Phase = 0.f;
	int32 Strokes = 0;
	for (int32 Frame = 0; Frame < 3600; ++Frame)
	{
		float Next = Phase + Dt * StrokeHz;
		Next -= FMath::FloorToFloat(Next);
		Strokes += CrossedPhase(Phase, Next, At) ? 1 : 0;
		Phase = Next;
	}
	TestEqual(TEXT("Un minuto nadando: 51 brazadas"), Strokes, 51);
	return true;
}

#endif
