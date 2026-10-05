#include "UI/Race/TN_RaceCueSynthComponent.h"
#include "Audio/TN_AudioVoices.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"
#include "Sound/SoundGenerator.h"
#include <atomic>
#include <cmath>

/**
 * Motor de los avisos de la carrera. La cola la llena el hilo de juego (Play) y la vacía el hilo de audio al principio de
 * cada bloque; voces, filtros y osciladores viven solo en el hilo de audio, sin asignaciones ni bloqueos. Lo lento
 * (envolventes, notas, cortes) se calcula una vez por bloque de 16 muestras. Sale en estéreo (el mismo en los dos
 * canales): es sonido de interfaz.
 */
namespace TNRaceCueDSP
{
	constexpr float CuePi = 3.14159265358979323846f;
	constexpr float CueTwoPi = 6.28318530717958647692f;
	constexpr int32 CueMaxVoices = 6;
	constexpr int32 CueBlock = 16;

	/** Tipos de aviso (el orden es el de ETNRaceCue). */
	constexpr uint8 KindTick = 0;
	constexpr uint8 KindTimeUp = 1;
	constexpr uint8 KindFanfare = 2;
	constexpr uint8 KindSlam = 3;
	constexpr uint8 KindSadTrombone = 4;

	struct FCueEvent
	{
		uint8 Kind = 0;
		float Pitch = 1.f;
		float Gain = 1.f;
	};

	/** Cola circular sin bloqueos de un productor (hilo de juego) y un consumidor (hilo de audio). */
	struct FCueQueue
	{
		static constexpr uint32 Capacity = 32;

		FCueEvent Events[Capacity];
		std::atomic<uint32> WriteIndex{ 0 };
		std::atomic<uint32> ReadIndex{ 0 };
		std::atomic<float> Master{ 1.f };

		bool Push(const FCueEvent& InEvent)
		{
			const uint32 W = WriteIndex.load(std::memory_order_relaxed);
			const uint32 R = ReadIndex.load(std::memory_order_acquire);
			if (W - R >= Capacity)
			{
				return false;
			}
			Events[W % Capacity] = InEvent;
			WriteIndex.store(W + 1, std::memory_order_release);
			return true;
		}

		bool Pop(FCueEvent& OutEvent)
		{
			const uint32 R = ReadIndex.load(std::memory_order_relaxed);
			const uint32 W = WriteIndex.load(std::memory_order_acquire);
			if (R == W)
			{
				return false;
			}
			OutEvent = Events[R % Capacity];
			ReadIndex.store(R + 1, std::memory_order_release);
			return true;
		}
	};

	/** Ruido blanco barato (xorshift32) en [-1, 1). */
	inline float CueNoise(uint32& State)
	{
		State ^= State << 13;
		State ^= State >> 17;
		State ^= State << 5;
		return static_cast<float>(State >> 8) * (1.f / 8388608.f) - 1.f;
	}

	/** Coeficiente del filtro de estado variable (Chamberlin), limitado a ~1/6 de la frecuencia de muestreo. */
	inline float CueSvfCoef(float CutHz, float Rate)
	{
		return 2.f * std::sin(CuePi * FMath::Clamp(CutHz, 20.f, Rate * 0.16f) / Rate);
	}

	/** Corrección polyBLEP del salto del diente de sierra. */
	inline float CuePolyBlep(float T, float Dt)
	{
		if (T < Dt)
		{
			const float X = T / Dt;
			return X + X - X * X - 1.f;
		}
		if (T > 1.f - Dt)
		{
			const float X = (T - 1.f) / Dt;
			return X * X + X + X + 1.f;
		}
		return 0.f;
	}

	/** Filtro de estado variable de Chamberlin (amortiguamiento de 1 o menos: siempre estable con CueSvfCoef). */
	struct FCueSvf
	{
		float Low = 0.f;
		float Band = 0.f;
		float High = 0.f;

		void Process(float In, float G, float Damp)
		{
			Low += G * Band;
			High = In - Low - Damp * Band;
			Band += G * High;
		}
	};

	/** Resonador de dos polos (la caja china) excitado por un golpecito de ruido. */
	struct FCueResonator
	{
		float Y1 = 0.f;
		float Y2 = 0.f;
		float C = 0.f;
		float R2 = 0.f;
		float Norm = 1.f;

		void Tune(float FreqHz, float BandwidthHz, float Rate)
		{
			const float W = CueTwoPi * FMath::Clamp(FreqHz, 20.f, Rate * 0.45f) / Rate;
			const float R = std::exp(-CuePi * BandwidthHz / Rate);
			C = 2.f * R * std::cos(W);
			R2 = R * R;
			Norm = std::sin(W);
		}

		float Process(float Excite)
		{
			const float Y = Excite + C * Y1 - R2 * Y2;
			Y2 = Y1;
			Y1 = Y;
			return Y * Norm;
		}
	};

	struct FCueVoice
	{
		bool bActive = false;
		uint8 Kind = 0;
		float Age = 0.f;
		float Duration = 1.f;
		float Pitch = 1.f;
		float Gain = 1.f;
		float PhaseA = 0.f;
		float PhaseB = 0.f;
		float PhaseC = 0.f;
		float Lp1 = 0.f;
		float Lp2 = 0.f;
		FCueSvf FiltA;
		FCueSvf FiltB;
		FCueResonator ResA;
		FCueResonator ResB;
		float PulseClock = 0.f;
		float Burst = 0.f;
		uint32 NoiseState = 0x2545F491u;
	};

	/** Fanfarria: tres notas cortas y una larga (sol, sol, sol, do), en s desde el inicio. */
	constexpr float FanfareStarts[4] = { 0.f, 0.155f, 0.31f, 0.465f };
	constexpr float FanfareLengths[4] = { 0.12f, 0.12f, 0.12f, 1.25f };
	constexpr float FanfareHz[4] = { 392.f, 392.f, 392.f, 523.25f };

	/** Trombón triste: si bemol, la, la bemol y sol (la larga), en s desde el inicio. */
	constexpr float TromboneStarts[4] = { 0.f, 0.36f, 0.72f, 1.08f };
	constexpr float TromboneLengths[4] = { 0.3f, 0.3f, 0.3f, 1.15f };
	constexpr float TromboneHz[4] = { 233.08f, 220.f, 207.65f, 196.f };

	class FCueCore
	{
	public:
		void Init(float InRate)
		{
			SampleRate = FMath::Max(8000.f, InRate);
			InvRate = 1.f / SampleRate;
		}

		void Render(float* Out, int32 Frames, int32 Channels, FCueQueue& Queue)
		{
			FCueEvent Pending;
			while (Queue.Pop(Pending))
			{
				StartVoice(Pending);
			}
			bool bAnyVoice = false;
			for (const FCueVoice& Voice : Voices)
			{
				bAnyVoice |= Voice.bActive;
			}
			if (!bAnyVoice)
			{
				FMemory::Memzero(Out, sizeof(float) * Frames * Channels);
				return;
			}
			const float MasterGain = Queue.Master.load(std::memory_order_relaxed);
			for (int32 Frame = 0; Frame < Frames; Frame += CueBlock)
			{
				const int32 Count = FMath::Min(CueBlock, Frames - Frame);
				float MixBuf[CueBlock] = {};
				for (FCueVoice& Voice : Voices)
				{
					if (Voice.bActive)
					{
						RenderVoice(Voice, MixBuf, Count);
					}
				}
				for (int32 i = 0; i < Count; ++i)
				{
					const float X = FMath::Clamp(MixBuf[i] * MasterGain, -3.f, 3.f);
					const float Soft = X * (27.f + X * X) / (27.f + 9.f * X * X);
					for (int32 Ch = 0; Ch < Channels; ++Ch)
					{
						Out[(Frame + i) * Channels + Ch] = Soft;
					}
				}
			}
		}

	private:
		float SampleRate = 48000.f;
		float InvRate = 1.f / 48000.f;
		FCueVoice Voices[CueMaxVoices];
		uint32 SeedState = 0x9E3779B9u;

		void StartVoice(const FCueEvent& InEvent)
		{
			int32 Best = 0;
			float Oldest = -1.f;
			for (int32 v = 0; v < CueMaxVoices; ++v)
			{
				if (!Voices[v].bActive)
				{
					Best = v;
					break;
				}
				if (Voices[v].Age > Oldest)
				{
					Oldest = Voices[v].Age;
					Best = v;
				}
			}
			FCueVoice& Voice = Voices[Best];
			Voice = FCueVoice();
			Voice.bActive = true;
			Voice.Kind = InEvent.Kind;
			Voice.Pitch = FMath::Clamp(InEvent.Pitch, 0.25f, 4.f);
			Voice.Gain = FMath::Clamp(InEvent.Gain, 0.f, 2.f);
			SeedState = SeedState * 1664525u + 1013904223u;
			Voice.NoiseState = SeedState | 1u;
			switch (InEvent.Kind)
			{
			case KindTick:
				Voice.Duration = 0.14f;
				Voice.ResA.Tune(1150.f * Voice.Pitch, 90.f, SampleRate);
				Voice.ResB.Tune(2750.f * Voice.Pitch, 260.f, SampleRate);
				break;
			case KindTimeUp: Voice.Duration = 0.9f; break;
			case KindFanfare: Voice.Duration = 2.1f; break;
			case KindSlam: Voice.Duration = 0.5f; break;
			case KindSadTrombone: Voice.Duration = 2.3f; break;
			default: Voice.Duration = 0.3f; break;
			}
		}

		void RenderVoice(FCueVoice& Voice, float* MixBuf, int32 Count)
		{
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float BlockSeconds = static_cast<float>(Count) * Dt;
			switch (Voice.Kind)
			{
			case KindTick:
			{
				// Caja china: un golpecito de ruido de 1,2 ms en dos resonadores (el cuerpo y el brillo de la madera).
				for (int32 i = 0; i < Count; ++i)
				{
					const float Tn = T + static_cast<float>(i) * Dt;
					const float Excite = Tn < 0.0012f ? CueNoise(Voice.NoiseState) * (1.f - Tn / 0.0012f) : 0.f;
					const float Y = 0.9f * Voice.ResA.Process(Excite) + 0.35f * Voice.ResB.Process(Excite);
					MixBuf[i] += 0.8f * Y * Voice.Gain;
				}
				break;
			}
			case KindTimeUp:
			{
				// Silbato de árbitro: dos pitidos (corto y largo) de un tono agudo con el trino de la bolita y el soplido.
				const auto Blast = [](float Tb, float Start, float Length)
				{
					const float L = Tb - Start;
					if (L < 0.f || L > Length) { return 0.f; }
					return FMath::Min(1.f, L / 0.012f) * FMath::Min(1.f, (Length - L) / 0.05f);
				};
				const float Env = FMath::Max(Blast(T, 0.f, 0.17f), Blast(T, 0.23f, 0.6f));
				const float Trill = 1.f - 0.55f * (0.5f + 0.5f * std::sin(CueTwoPi * 27.f * T));
				const float Freq = (2850.f + 70.f * std::sin(CueTwoPi * 9.f * T)) * Voice.Pitch;
				const float Step = Freq * Dt;
				const float G = CueSvfCoef(3000.f * Voice.Pitch, SampleRate);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.PhaseA += Step;
					Voice.PhaseA -= std::floor(Voice.PhaseA);
					Voice.FiltA.Process(CueNoise(Voice.NoiseState), G, 0.6f);
					const float Y = Env * (0.5f * std::sin(CueTwoPi * Voice.PhaseA) * Trill + 0.35f * Voice.FiltA.Band);
					MixBuf[i] += Y * Voice.Gain;
				}
				break;
			}
			case KindFanfare:
			{
				// Metales: diente de sierra con otro una octava por debajo, por dos polos que se abren al atacar cada nota;
				// debajo, redoble de caja en las tres cortas y, en la larga, golpe de caja, timbal y platillo.
				int32 Note = -1;
				for (int32 n = 3; n >= 0; --n)
				{
					if (T >= FanfareStarts[n]) { Note = n; break; }
				}
				float NoteEnv = 0.f;
				float Freq = FanfareHz[0] * Voice.Pitch;
				float Bright = 0.f;
				if (Note >= 0)
				{
					const float Lt = T - FanfareStarts[Note];
					const float Len = FanfareLengths[Note];
					const bool bLong = Note == 3;
					const float Release = bLong ? 0.3f : 0.04f;
					NoteEnv = Lt > Len ? 0.f : FMath::Min(1.f, Lt / 0.012f) * FMath::Min(1.f, (Len - Lt) / Release) * (bLong ? 1.f - 0.2f * FMath::Min(1.f, Lt / Len) : 1.f);
					Freq = FanfareHz[Note] * Voice.Pitch * (bLong && Lt > 0.25f ? 1.f + 0.012f * std::sin(CueTwoPi * 5.5f * Lt) : 1.f);
					Bright = FMath::Min(1.f, Lt / 0.03f) * (0.6f + 0.4f * std::exp(-Lt / 0.15f));
				}
				const float Cut = 400.f + 3400.f * Bright;
				const float LpCoef = 1.f - std::exp(-CueTwoPi * Cut / SampleRate);
				const float StepA = Freq * Dt;
				const float StepB = 0.5f * Freq * Dt;
				const float HitT = T - FanfareStarts[3];
				const bool bRoll = T < FanfareStarts[3];
				const float SnareG = CueSvfCoef(2200.f, SampleRate);
				const float CrashG = CueSvfCoef(5200.f, SampleRate);
				const float CrashEnv = HitT >= 0.f ? 0.28f * std::exp(-HitT / 0.9f) : 0.f;
				const float TimpEnv = HitT >= 0.f ? 0.55f * std::exp(-HitT / 0.5f) : 0.f;
				const float TimpStep = (92.f + 8.f * std::exp(-FMath::Max(0.f, HitT) / 0.08f)) * Voice.Pitch * Dt;
				const float BurstK = std::exp(-Dt / 0.015f);
				for (int32 i = 0; i < Count; ++i)
				{
					// Metales.
					Voice.PhaseA += StepA;
					if (Voice.PhaseA >= 1.f) { Voice.PhaseA -= 1.f; }
					Voice.PhaseB += StepB;
					if (Voice.PhaseB >= 1.f) { Voice.PhaseB -= 1.f; }
					const float SawA = 2.f * Voice.PhaseA - 1.f - CuePolyBlep(Voice.PhaseA, StepA);
					const float SawB = 2.f * Voice.PhaseB - 1.f - CuePolyBlep(Voice.PhaseB, StepB);
					Voice.Lp1 += LpCoef * ((SawA + 0.5f * SawB) - Voice.Lp1);
					Voice.Lp2 += LpCoef * (Voice.Lp1 - Voice.Lp2);
					float Y = 0.45f * NoteEnv * Voice.Lp2;
					// Caja: redoble (golpes a 22 por segundo) y un golpe fuerte al empezar la nota larga.
					const float Noise = CueNoise(Voice.NoiseState);
					Voice.PulseClock -= Dt;
					if (bRoll && Voice.PulseClock <= 0.f)
					{
						Voice.PulseClock = 1.f / 22.f;
						Voice.Burst = 0.55f;
					}
					if (HitT >= 0.f && HitT < Dt * 1.5f)
					{
						Voice.Burst = 1.f;
					}
					Voice.Burst *= BurstK;
					Voice.FiltA.Process(Noise, SnareG, 0.9f);
					Y += 0.3f * Voice.Burst * Voice.FiltA.Band;
					// Platillo y timbal en la nota larga.
					if (HitT >= 0.f)
					{
						Voice.FiltB.Process(Noise, CrashG, 0.8f);
						Y += CrashEnv * Voice.FiltB.High;
						Voice.PhaseC += TimpStep;
						Voice.PhaseC -= std::floor(Voice.PhaseC);
						Y += TimpEnv * std::sin(CueTwoPi * Voice.PhaseC);
					}
					MixBuf[i] += Y * Voice.Gain;
				}
				break;
			}
			case KindSadTrombone:
			{
				// Trombón con sordina: diente de sierra con otro una octava por debajo, por dos polos cuyo corte sube y baja en
				// cada nota («buaa»); la larga tiembla cada vez más, se apaga la sordina y se cae de tono al final.
				int32 Note = -1;
				for (int32 n = 3; n >= 0; --n)
				{
					if (T >= TromboneStarts[n]) { Note = n; break; }
				}
				float NoteEnv = 0.f;
				float Freq = TromboneHz[0] * Voice.Pitch;
				float Wah = 0.f;
				if (Note >= 0)
				{
					const float Lt = T - TromboneStarts[Note];
					const float Len = TromboneLengths[Note];
					const bool bLong = Note == 3;
					NoteEnv = Lt > Len ? 0.f : FMath::Min(1.f, Lt / 0.035f) * FMath::Min(1.f, (Len - Lt) / (bLong ? 0.25f : 0.06f));
					float Bend = 1.f;
					if (bLong)
					{
						const float Depth = 0.004f + 0.02f * FMath::Min(1.f, Lt / Len);
						const float Fall = FMath::Square(FMath::Max(0.f, (Lt - 0.7f) / 0.45f));
						Bend = (1.f + Depth * std::sin(CueTwoPi * 5.2f * Lt)) * (1.f - 0.07f * FMath::Min(1.f, Fall));
					}
					Freq = TromboneHz[Note] * Voice.Pitch * Bend;
					Wah = std::sin(CuePi * FMath::Clamp(Lt / (bLong ? Len * 0.6f : Len), 0.f, 1.f));
				}
				const float Cut = 280.f + 1500.f * Wah;
				const float LpCoef = 1.f - std::exp(-CueTwoPi * Cut / SampleRate);
				const float StepA = Freq * Dt;
				const float StepB = 0.5f * Freq * Dt;
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.PhaseA += StepA;
					if (Voice.PhaseA >= 1.f) { Voice.PhaseA -= 1.f; }
					Voice.PhaseB += StepB;
					if (Voice.PhaseB >= 1.f) { Voice.PhaseB -= 1.f; }
					const float SawA = 2.f * Voice.PhaseA - 1.f - CuePolyBlep(Voice.PhaseA, StepA);
					const float SawB = 2.f * Voice.PhaseB - 1.f - CuePolyBlep(Voice.PhaseB, StepB);
					Voice.Lp1 += LpCoef * ((SawA + 0.35f * SawB) - Voice.Lp1);
					Voice.Lp2 += LpCoef * (Voice.Lp1 - Voice.Lp2);
					MixBuf[i] += 0.55f * NoteEnv * Voice.Lp2 * Voice.Gain;
				}
				break;
			}
			case KindSlam:
			default:
			{
				// «¡Pum!»: seno grave que cae de tono, golpe de ruido sordo y un chasquido al principio.
				const float Env = std::exp(-T / 0.16f);
				const float Step = (48.f + 110.f * std::exp(-T / 0.06f)) * Voice.Pitch * Dt;
				const float ThumpEnv = 0.6f * std::exp(-T / 0.05f);
				const float LpCoef = 1.f - std::exp(-CueTwoPi * 300.f / SampleRate);
				for (int32 i = 0; i < Count; ++i)
				{
					const float Tn = T + static_cast<float>(i) * Dt;
					Voice.PhaseA += Step;
					Voice.PhaseA -= std::floor(Voice.PhaseA);
					const float Noise = CueNoise(Voice.NoiseState);
					Voice.Lp1 += LpCoef * (Noise - Voice.Lp1);
					float Y = 0.9f * Env * std::sin(CueTwoPi * Voice.PhaseA) + ThumpEnv * 3.f * Voice.Lp1;
					if (Tn < 0.004f) { Y += 0.4f * Noise * (1.f - Tn / 0.004f); }
					MixBuf[i] += Y * Voice.Gain;
				}
				break;
			}
			}
			Voice.Age += BlockSeconds;
			if (Voice.Age >= Voice.Duration)
			{
				Voice.bActive = false;
			}
		}
	};

	class FCueGenerator : public ISoundGenerator
	{
	public:
		FCueGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<FCueQueue, ESPMode::ThreadSafe>& InQueue)
			: Queue(InQueue)
			, OutChannels(FMath::Max(1, InNumChannels))
		{
			Core.Init(InSampleRate);
		}

		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			const int32 Frames = NumSamples / OutChannels;
			if (Queue.IsValid())
			{
				Core.Render(OutAudio, Frames, OutChannels, *Queue);
			}
			else
			{
				FMemory::Memzero(OutAudio, sizeof(float) * Frames * OutChannels);
			}
			for (int32 i = Frames * OutChannels; i < NumSamples; ++i)
			{
				OutAudio[i] = 0.f;
			}
			return NumSamples;
		}

	private:
		TSharedPtr<FCueQueue, ESPMode::ThreadSafe> Queue;
		int32 OutChannels = 2;
		FCueCore Core;
	};
}

static_assert(static_cast<uint8>(ETNRaceCue::Tick) == TNRaceCueDSP::KindTick, "ETNRaceCue y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceCue::TimeUp) == TNRaceCueDSP::KindTimeUp, "ETNRaceCue y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceCue::Fanfare) == TNRaceCueDSP::KindFanfare, "ETNRaceCue y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceCue::Slam) == TNRaceCueDSP::KindSlam, "ETNRaceCue y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceCue::SadTrombone) == TNRaceCueDSP::KindSadTrombone, "ETNRaceCue y el motor DSP deben coincidir");

// ─────────────────────────────────────────────────────────────────────────────
// UTN_RaceCueSynthComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_RaceCueSynthComponent::UTN_RaceCueSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// El tick solo cuenta el silencio para parar el sintetizador, dos veces por segundo.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.5f;
	bAutoActivate = false;
	// Sonido de interfaz: estéreo, sin espacializar ni atenuar.
	NumChannels = 2;
	bAllowSpatialization = false;
	bOverrideAttenuation = false;
	CueQueue = MakeShared<TNRaceCueDSP::FCueQueue, ESPMode::ThreadSafe>();
}

bool UTN_RaceCueSynthComponent::Init(int32& /*SampleRate*/)
{
	NumChannels = 2;
	return true;
}

ISoundGeneratorPtr UTN_RaceCueSynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<TNRaceCueDSP::FCueGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, CueQueue);
}

void UTN_RaceCueSynthComponent::Play(ETNRaceCue Cue, float Pitch, float Volume)
{
	if (!CueQueue.IsValid() || Volume <= 0.f)
	{
		return;
	}
	if (!IsPlaying())
	{
		// Avisos de la interfaz: voz reservada.
		TNAudioVoices::Apply(*this, TNAudioVoices::ERank::Reserved);
		Start();
	}
	CueQueue->Master.store(FMath::Clamp(Loudness, 0.f, 2.f), std::memory_order_relaxed);
	TNRaceCueDSP::FCueEvent Shot;
	Shot.Kind = static_cast<uint8>(Cue);
	Shot.Pitch = Pitch;
	Shot.Gain = Volume;
	CueQueue->Push(Shot);
	// La fanfarria dura 2,1 s y el trombón triste 2,3: con 3 s de margen no se corta ninguna cola.
	SilenceLeft = 3.f;
	SetComponentTickEnabled(true);
}

void UTN_RaceCueSynthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	SilenceLeft -= DeltaTime;
	if (SilenceLeft <= 0.f)
	{
		if (IsPlaying())
		{
			Stop();
		}
		SetComponentTickEnabled(false);
	}
}

UTN_RaceCueSynthComponent* UTN_RaceCueSynthComponent::Attach2D(AActor* InOwner)
{
	if (!InOwner)
	{
		return nullptr;
	}
	const UWorld* OwnerWorld = InOwner->GetWorld();
	if (!OwnerWorld || !OwnerWorld->IsGameWorld() || OwnerWorld->GetNetMode() == NM_DedicatedServer || !FApp::CanEverRenderAudio())
	{
		return nullptr;
	}
	// Las pantallas van y vienen: el sintetizador del mando se reutiliza.
	if (UTN_RaceCueSynthComponent* Found = InOwner->FindComponentByClass<UTN_RaceCueSynthComponent>())
	{
		if (Found->IsRegistered())
		{
			return Found;
		}
	}
	UTN_RaceCueSynthComponent* Comp = NewObject<UTN_RaceCueSynthComponent>(InOwner, NAME_None, RF_Transient);
	if (USceneComponent* RootComp = InOwner->GetRootComponent())
	{
		Comp->SetupAttachment(RootComp);
	}
	Comp->RegisterComponent();
	InOwner->AddInstanceComponent(Comp);
	return Comp;
}
