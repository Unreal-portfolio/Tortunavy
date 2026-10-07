#include "World/Beach/TN_BeachEnemySynth.h"
#include "Audio/TN_AudioVoices.h"
#include "AudioDevice.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundGenerator.h"
#include <atomic>
#include <cmath>

/**
 * Motor de sonido de los enemigos de la playa. Hilos: la cola y los niveles continuos los escribe el hilo de juego; el
 * hilo de render de audio vacía la cola al principio de cada bloque y lee los niveles. Voces, filtros y osciladores
 * viven solo en el hilo de audio, sin asignaciones ni bloqueos. Los parámetros lentos se calculan una vez por bloque de
 * 16 muestras (unos 0,3 ms a 48 kHz).
 */
namespace TNBeachSynthDSP
{
	constexpr float BsPi = 3.14159265358979323846f;
	constexpr float BsTwoPi = 6.28318530717958647692f;
	constexpr int32 BsMaxVoices = 10;
	constexpr int32 BsBlock = 16;

	/** Tipos de efecto (el orden es el de ETNBeachSfx). */
	constexpr uint8 KindClack = 0;
	constexpr uint8 KindSlam = 1;
	constexpr uint8 KindSquawk = 2;
	constexpr uint8 KindSplat = 3;
	constexpr uint8 KindSwoop = 4;
	constexpr uint8 KindCrunch = 5;
	constexpr uint8 KindStomp = 6;

	struct FEvent
	{
		uint8 Kind = 0;
		float Pitch = 1.f;
		float Gain = 1.f;
	};

	/** Cola circular sin bloqueos (un productor, un consumidor) y niveles continuos atómicos. */
	struct FShared
	{
		static constexpr uint32 Capacity = 32;

		FEvent Events[Capacity];
		std::atomic<uint32> WriteIndex{ 0 };
		std::atomic<uint32> ReadIndex{ 0 };
		std::atomic<float> Master{ 1.f };
		std::atomic<float> EngineLevel{ 0.f };
		std::atomic<float> EngineRpm{ 0.f };
		std::atomic<float> WindLevel{ 0.f };

		/** Hilo de juego. false si la cola está llena (el disparo se pierde). */
		bool Push(const FEvent& InEvent)
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

		/** Hilo de audio. */
		bool Pop(FEvent& OutEvent)
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
	inline float BsNoise(uint32& State)
	{
		State ^= State << 13;
		State ^= State >> 17;
		State ^= State << 5;
		return static_cast<float>(State >> 8) * (1.f / 8388608.f) - 1.f;
	}

	/** Coeficiente del filtro de estado variable (Chamberlin); estable por debajo de ~1/6 de la frecuencia de muestreo. */
	inline float BsSvfCoef(float CutHz, float Rate)
	{
		return 2.f * std::sin(BsPi * FMath::Clamp(CutHz, 20.f, Rate * 0.16f) / Rate);
	}

	/** Saturación suave (aproximación racional de tanh). */
	inline float BsSoftClip(float X)
	{
		const float C = FMath::Clamp(X, -3.f, 3.f);
		return C * (27.f + C * C) / (27.f + 9.f * C * C);
	}

	/** Filtro de estado variable: paso bajo y paso banda. */
	struct FSvf
	{
		float Low = 0.f;
		float Band = 0.f;

		void Tick(float In, float G, float Damp)
		{
			Low += G * Band;
			const float High = In - Low - Damp * Band;
			Band += G * High;
		}
	};

	/** Resonador de dos polos normalizado (chasquidos de caparazón, madera, arena). */
	struct FReso
	{
		float Y1 = 0.f;
		float Y2 = 0.f;
		float C = 0.f;
		float R2 = 0.f;
		float Norm = 1.f;

		void Set(float Hz, float DecaySeconds, float Rate)
		{
			const float W = BsTwoPi * FMath::Min(Hz, Rate * 0.45f) / Rate;
			const float R = std::exp(-1.f / (FMath::Max(0.001f, DecaySeconds) * Rate));
			C = 2.f * R * std::cos(W);
			R2 = R * R;
			Norm = std::sin(W);
		}

		float Tick(float In)
		{
			const float Y = In + C * Y1 - R2 * Y2;
			Y2 = Y1;
			Y1 = Y;
			return Y * Norm;
		}
	};

	/** Una voz sonando. */
	struct FVoice
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
		FSvf SvfA;
		FSvf SvfB;
		float Lp = 0.f;
		float Clock = 0.f;
		float NextEvent = 0.f;
		int32 Count = 0;
		float Jitter = 0.f;
		FReso ResA;
		FReso ResB;
		uint32 NoiseState = 0x2545F491u;
	};

	/** Mezclador de voces, motor y viento, con limitador suave al final. */
	class FCore
	{
	public:
		void Init(float InRate)
		{
			Rate = FMath::Max(8000.f, InRate);
			InvRate = 1.f / Rate;
		}

		/** Mono repetido en todos los canales (el componente es mono y espacializado). */
		void Render(float* Out, int32 Frames, int32 Channels, FShared& Shared)
		{
			FEvent Pending;
			while (Shared.Pop(Pending))
			{
				StartVoice(Pending);
			}
			const float EngineTarget = FMath::Clamp(Shared.EngineLevel.load(std::memory_order_relaxed), 0.f, 1.5f);
			const float RpmTarget = FMath::Clamp(Shared.EngineRpm.load(std::memory_order_relaxed), 0.f, 1.f);
			const float WindTarget = FMath::Clamp(Shared.WindLevel.load(std::memory_order_relaxed), 0.f, 1.5f);
			bool bAny = EngineTarget > 1e-4f || EngineLevel > 1e-4f || WindTarget > 1e-4f || WindLevel > 1e-4f;
			for (const FVoice& Voice : Voices)
			{
				bAny |= Voice.bActive;
			}
			if (!bAny)
			{
				FMemory::Memzero(Out, sizeof(float) * Frames * Channels);
				return;
			}
			const float MasterGain = Shared.Master.load(std::memory_order_relaxed);
			const float BlockDt = BsBlock * InvRate;
			const float EngineK = 1.f - std::exp(-BlockDt / 0.15f);
			const float RpmK = 1.f - std::exp(-BlockDt / 0.3f);
			const float WindK = 1.f - std::exp(-BlockDt / 0.6f);
			for (int32 Frame = 0; Frame < Frames; Frame += BsBlock)
			{
				const int32 Count = FMath::Min(BsBlock, Frames - Frame);
				float MixBuf[BsBlock] = {};
				for (FVoice& Voice : Voices)
				{
					if (Voice.bActive)
					{
						RenderVoice(Voice, MixBuf, Count);
					}
				}
				EngineLevel += (EngineTarget - EngineLevel) * EngineK;
				EngineRpm += (RpmTarget - EngineRpm) * RpmK;
				WindLevel += (WindTarget - WindLevel) * WindK;
				if (EngineLevel > 1e-4f)
				{
					RenderEngine(MixBuf, Count);
				}
				if (WindLevel > 1e-4f)
				{
					RenderWind(MixBuf, Count, BlockDt);
				}
				for (int32 i = 0; i < Count; ++i)
				{
					const float Soft = BsSoftClip(MixBuf[i] * MasterGain);
					for (int32 Ch = 0; Ch < Channels; ++Ch)
					{
						Out[(Frame + i) * Channels + Ch] = Soft;
					}
				}
			}
		}

	private:
		float Rate = 48000.f;
		float InvRate = 1.f / 48000.f;
		FVoice Voices[BsMaxVoices];
		uint32 SeedState = 0x9E3779B9u;

		// Motor del quad.
		float EngineLevel = 0.f;
		float EngineRpm = 0.f;
		float EnginePhase = 0.f;
		float EnginePhase2 = 0.f;
		float EngineSub = 0.f;
		FSvf EngineSvf;
		float EngineLp = 0.f;
		uint32 EngineNoise = 0x68E31DA4u;

		// Viento de la tormenta.
		float WindLevel = 0.f;
		float Gust = 0.8f;
		float GustTarget = 0.8f;
		float GustTimer = 0.f;
		float WhistlePhase = 0.f;
		FSvf WindA;
		FSvf WindB;
		uint32 WindNoise = 0x1B873593u;

		/** Coge una voz libre (o la más vieja) y la prepara. */
		void StartVoice(const FEvent& InEvent)
		{
			int32 Best = 0;
			float Oldest = -1.f;
			for (int32 i = 0; i < BsMaxVoices; ++i)
			{
				if (!Voices[i].bActive)
				{
					Best = i;
					break;
				}
				if (Voices[i].Age > Oldest)
				{
					Oldest = Voices[i].Age;
					Best = i;
				}
			}
			FVoice& Voice = Voices[Best];
			Voice = FVoice();
			Voice.bActive = true;
			Voice.Kind = InEvent.Kind;
			Voice.Pitch = FMath::Clamp(InEvent.Pitch, 0.25f, 4.f);
			Voice.Gain = FMath::Clamp(InEvent.Gain, 0.f, 2.f);
			SeedState = SeedState * 1664525u + 1013904223u;
			Voice.NoiseState = SeedState | 1u;
			Voice.Jitter = static_cast<float>((SeedState >> 9) & 1023u) / 1023.f;
			const float P = Voice.Pitch;
			switch (InEvent.Kind)
			{
			case KindClack:
				Voice.Duration = 0.24f;
				Voice.ResA.Set(1150.f * P, 0.03f, Rate);
				Voice.ResB.Set(2700.f * P, 0.012f, Rate);
				break;
			case KindSlam: Voice.Duration = 1.1f; break;
			case KindSquawk: Voice.Duration = 0.55f; break;
			case KindSplat: Voice.Duration = 0.42f; break;
			case KindSwoop: Voice.Duration = 1.1f; break;
			case KindCrunch:
				Voice.Duration = 1.4f;
				Voice.ResA.Set(430.f * P, 0.045f, Rate);
				Voice.ResB.Set(980.f * P, 0.02f, Rate);
				break;
			case KindStomp: Voice.Duration = 1.0f; break;
			default:
				Voice.Duration = 0.f;
				break;
			}
		}

		void RenderVoice(FVoice& Voice, float* MixBuf, int32 Count)
		{
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float P = Voice.Pitch;
			const float X = FMath::Clamp(T / FMath::Max(0.05f, Voice.Duration), 0.f, 1.f);
			switch (Voice.Kind)
			{
			case KindClack:
			{
				// Dos chasquidos secos de caparazón: dos resonadores golpeados con 75 ms de separación.
				for (int32 i = 0; i < Count; ++i)
				{
					const float Ts = T + i * Dt;
					float Excite = 0.f;
					if (Voice.Count < 2 && Ts >= Voice.NextEvent)
					{
						Excite = 1.f;
						++Voice.Count;
						Voice.NextEvent = 0.075f + 0.02f * Voice.Jitter;
					}
					Excite += 0.02f * BsNoise(Voice.NoiseState) * std::exp(-Ts / 0.1f);
					MixBuf[i] += (0.65f * Voice.ResA.Tick(Excite) + 0.45f * Voice.ResB.Tick(Excite)) * Voice.Gain;
				}
				break;
			}
			case KindSlam:
			{
				// Mazazo: golpe grave que cae de tono, arena que salpica (ruido por un paso bajo que se cierra) y un clic.
				const float Env = T < 0.004f ? T / 0.004f : std::exp(-(T - 0.004f) / 0.3f);
				const float Freq = 48.f * P * (1.f + 1.6f * std::exp(-T / 0.035f));
				const float NoiseEnv = 0.7f * std::exp(-T / 0.32f);
				const float G = BsSvfCoef(250.f + 1400.f * std::exp(-T / 0.08f), Rate);
				const float Click = 0.5f * std::exp(-T / 0.006f);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.PhaseA += Freq * Dt;
					Voice.PhaseA -= std::floor(Voice.PhaseA);
					const float N = BsNoise(Voice.NoiseState);
					Voice.SvfA.Tick(N, G, 0.9f);
					MixBuf[i] += (0.95f * std::sin(BsTwoPi * Voice.PhaseA) * Env + Voice.SvfA.Low * NoiseEnv + N * Click) * Voice.Gain;
				}
				break;
			}
			case KindSquawk:
			{
				// Graznido: diente de sierra que sube y cae, con vibrato, carraspeo y dos formantes (el «kyaaa» de la gaviota).
				const float Freq = P * (1000.f + 480.f * std::sin(BsPi * FMath::Min(1.f, X * 1.6f)) - 420.f * X) * (1.f + 0.035f * std::sin(BsTwoPi * 17.f * T));
				const float Att = FMath::Min(1.f, T / 0.012f);
				const float Env = Att * (X < 0.75f ? 1.f : (1.f - X) / 0.25f) * (0.8f + 0.2f * std::sin(BsTwoPi * 9.f * T));
				const float G1 = BsSvfCoef(1750.f * P, Rate);
				const float G2 = BsSvfCoef(2900.f * P, Rate);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.PhaseA += Freq * Dt;
					Voice.PhaseA -= std::floor(Voice.PhaseA);
					const float Src = 2.f * Voice.PhaseA - 1.f + 0.15f * BsNoise(Voice.NoiseState);
					Voice.SvfA.Tick(Src, G1, 0.35f);
					Voice.SvfB.Tick(Src, G2, 0.45f);
					MixBuf[i] += (0.5f * Voice.SvfA.Band + 0.35f * Voice.SvfB.Band + 0.12f * Src) * Env * 0.8f * Voice.Gain;
				}
				break;
			}
			case KindSplat:
			{
				// Cagada: dos chapoteos húmedos de ruido apagado y un «blop» grave que cae.
				const float Env1 = std::exp(-T / 0.05f);
				const float Env2 = T > 0.06f ? 0.6f * std::exp(-(T - 0.06f) / 0.07f) : 0.f;
				const float BlopFreq = P * (80.f + 160.f * std::exp(-T / 0.03f));
				const float BlopEnv = 0.7f * std::exp(-T / 0.1f);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.Lp += 0.25f * (BsNoise(Voice.NoiseState) - Voice.Lp);
					Voice.PhaseA += BlopFreq * Dt;
					Voice.PhaseA -= std::floor(Voice.PhaseA);
					MixBuf[i] += (Voice.Lp * (Env1 + Env2) * 1.4f + std::sin(BsTwoPi * Voice.PhaseA) * BlopEnv) * Voice.Gain;
				}
				break;
			}
			case KindSwoop:
			{
				// Picado: ruido por un paso banda que sube hasta el 60 % y vuelve a bajar.
				const float Arc = X < 0.6f ? std::sin(0.5f * BsPi * X / 0.6f) : std::cos(0.5f * BsPi * (X - 0.6f) / 0.4f);
				const float Env = Arc * Arc;
				const float G = BsSvfCoef((380.f + 2400.f * Arc) * P, Rate);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.SvfA.Tick(BsNoise(Voice.NoiseState), G, 0.4f);
					MixBuf[i] += 0.7f * Voice.SvfA.Band * Env * Voice.Gain;
				}
				break;
			}
			case KindCrunch:
			{
				// Palmeras que crujen: chasquidos de madera cada vez más espaciados y un siseo de hojas.
				const float Env = std::pow(1.f - X, 0.7f);
				const float PulseRate = (45.f * (1.f - X) + 8.f) * P;
				const float Rustle = 0.25f * std::sin(BsPi * X);
				const float G = BsSvfCoef(2800.f * P, Rate);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.Clock += PulseRate * Dt;
					float Excite = 0.f;
					if (Voice.Clock >= 1.f)
					{
						Voice.Clock -= 1.f + 0.5f * BsNoise(Voice.NoiseState);
						Excite = 0.5f + 0.5f * std::abs(BsNoise(Voice.NoiseState));
					}
					const float N = BsNoise(Voice.NoiseState);
					Voice.SvfA.Tick(N, G, 0.7f);
					MixBuf[i] += ((0.6f * Voice.ResA.Tick(Excite) + 0.4f * Voice.ResB.Tick(Excite)) * Env + Voice.SvfA.Band * Rustle) * Voice.Gain;
				}
				break;
			}
			case KindStomp:
			{
				// Pisotón gigante: golpe muy grave y arena.
				const float Env = T < 0.006f ? T / 0.006f : std::exp(-(T - 0.006f) / 0.35f);
				const float Freq = 34.f * P * (1.f + 1.2f * std::exp(-T / 0.05f));
				const float NoiseEnv = 0.5f * std::exp(-T / 0.25f);
				const float G = BsSvfCoef(600.f, Rate);
				const float Click = 0.35f * std::exp(-T / 0.008f);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.PhaseA += Freq * Dt;
					Voice.PhaseA -= std::floor(Voice.PhaseA);
					const float N = BsNoise(Voice.NoiseState);
					Voice.SvfA.Tick(N, G, 0.9f);
					MixBuf[i] += (std::sin(BsTwoPi * Voice.PhaseA) * Env + Voice.SvfA.Low * NoiseEnv + N * Click) * Voice.Gain;
				}
				break;
			}
			default:
				break;
			}
			Voice.Age += static_cast<float>(Count) * Dt;
			if (Voice.Age >= Voice.Duration)
			{
				Voice.bActive = false;
			}
		}

		/** Motor de dos tiempos enorme: pulsos saturados por un paso bajo resonante, retumbar grave y escape. */
		void RenderEngine(float* MixBuf, int32 Count)
		{
			const float Dt = InvRate;
			const float Freq = 24.f + 55.f * EngineRpm;
			const float G = BsSvfCoef(260.f + 1300.f * EngineRpm, Rate);
			const float ExhaustGain = 0.15f + 0.25f * EngineRpm;
			for (int32 i = 0; i < Count; ++i)
			{
				EnginePhase += Freq * Dt;
				EnginePhase -= std::floor(EnginePhase);
				EnginePhase2 += Freq * 2.01f * Dt;
				EnginePhase2 -= std::floor(EnginePhase2);
				EngineSub += Freq * 0.5f * Dt;
				EngineSub -= std::floor(EngineSub);
				const float Pulse = EnginePhase < 0.28f ? 1.f : -0.39f;
				const float Src = BsSoftClip((0.6f * Pulse + 0.4f * (2.f * EnginePhase - 1.f) + 0.3f * (2.f * EnginePhase2 - 1.f)) * 1.8f);
				EngineSvf.Tick(Src, G, 0.5f);
				EngineLp += 0.08f * (BsNoise(EngineNoise) - EngineLp);
				const float Rumble = std::sin(BsTwoPi * EngineSub);
				MixBuf[i] += (0.7f * EngineSvf.Low + 0.35f * Rumble + EngineLp * ExhaustGain) * EngineLevel * 0.9f;
			}
		}

		/** Viento: dos bandas de ruido con rachas al azar y un silbido suave. */
		void RenderWind(float* MixBuf, int32 Count, float BlockDt)
		{
			GustTimer -= BlockDt;
			if (GustTimer <= 0.f)
			{
				GustTimer = 0.6f + 1.2f * (0.5f + 0.5f * BsNoise(WindNoise));
				GustTarget = 0.45f + 0.7f * (0.5f + 0.5f * BsNoise(WindNoise));
			}
			Gust += (GustTarget - Gust) * (1.f - std::exp(-BlockDt / 0.5f));
			const float Dt = InvRate;
			// Banda alta más grave y más floja (antes 1050 Hz y 0,45): a ese nivel y siempre encendido, el siseo ancho de
			// ruido sonaba a agua corriente detrás de la carrera, no a viento.
			const float GA = BsSvfCoef(320.f * Gust, Rate);
			const float GB = BsSvfCoef(760.f * Gust, Rate);
			const float WhistleFreq = 560.f + 140.f * Gust;
			const float WhistleGain = 0.04f * Gust * Gust;
			// Curva cuadrática hasta 1: el frente lejano (nivel 0,3) queda unos 10 dB más bajo que antes y crece según se
			// acerca; dentro de la tormenta (nivel 1) suena igual que siempre.
			const float Amp = WindLevel * FMath::Min(WindLevel, 1.f);
			for (int32 i = 0; i < Count; ++i)
			{
				const float N = BsNoise(WindNoise);
				WindA.Tick(N, GA, 0.9f);
				WindB.Tick(N, GB, 0.6f);
				WhistlePhase += WhistleFreq * Dt;
				WhistlePhase -= std::floor(WhistlePhase);
				MixBuf[i] += (0.9f * WindA.Band + 0.32f * WindB.Band + WhistleGain * std::sin(BsTwoPi * WhistlePhase)) * Amp * Gust;
			}
		}
	};

	/** Generador del hilo de render de audio: solo C++ puro y el estado compartido. */
	class FGenerator final : public ISoundGenerator
	{
	public:
		FGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<FShared, ESPMode::ThreadSafe>& InShared)
			: Shared(InShared)
			, OutChannels(FMath::Max(1, InNumChannels))
		{
			Core.Init(InSampleRate);
		}

		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			const int32 Frames = NumSamples / OutChannels;
			if (Shared.IsValid())
			{
				Core.Render(OutAudio, Frames, OutChannels, *Shared);
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
		TSharedPtr<FShared, ESPMode::ThreadSafe> Shared;
		int32 OutChannels = 1;
		FCore Core;
	};
}

static_assert(static_cast<uint8>(ETNBeachSfx::Clack) == TNBeachSynthDSP::KindClack, "ETNBeachSfx y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNBeachSfx::Squawk) == TNBeachSynthDSP::KindSquawk, "ETNBeachSfx y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNBeachSfx::Stomp) == TNBeachSynthDSP::KindStomp, "ETNBeachSfx y el motor DSP deben coincidir");

// ─────────────────────────────────────────────────────────────────────────────
// UTN_BeachEnemySynthComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_BeachEnemySynthComponent::UTN_BeachEnemySynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// El tick solo vigila el silencio y la distancia al oyente, cuatro veces por segundo.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.25f;
	bAutoActivate = false;
	NumChannels = 1;
	Shared = MakeShared<TNBeachSynthDSP::FShared, ESPMode::ThreadSafe>();
}

void UTN_BeachEnemySynthComponent::OnRegister()
{
	// Antes de que el padre cree el componente de audio.
	ConfigureSpatial();
	Super::OnRegister();
}

void UTN_BeachEnemySynthComponent::ConfigureSpatial()
{
	NumChannels = 1;
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	// Todo es enorme (28 veces su tamaño): radios grandes, caída natural y agudos que se apagan con la distancia.
	FSoundAttenuationSettings& Att = AttenuationOverrides;
	Att.bAttenuate = true;
	Att.bSpatialize = true;
	Att.AttenuationShape = EAttenuationShape::Sphere;
	Att.AttenuationShapeExtents = FVector(InnerRadius, 0.f, 0.f);
	Att.FalloffDistance = FMath::Max(100.f, FalloffDistance);
	Att.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	Att.dBAttenuationAtMax = -50.f;
	Att.bAttenuateWithLPF = true;
	Att.LPFRadiusMin = InnerRadius;
	Att.LPFRadiusMax = InnerRadius + Att.FalloffDistance;
	Att.LPFFrequencyAtMin = 20000.f;
	Att.LPFFrequencyAtMax = 2500.f;
	Att.NonSpatializedRadiusStart = InnerRadius * 0.5f;
	Att.NonSpatializedRadiusEnd = InnerRadius * 0.2f;
}

bool UTN_BeachEnemySynthComponent::Init(int32& /*SampleRate*/)
{
	NumChannels = 1;
	return true;
}

ISoundGeneratorPtr UTN_BeachEnemySynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<TNBeachSynthDSP::FGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, Shared);
}

bool UTN_BeachEnemySynthComponent::IsListenerNear() const
{
	const FAudioDevice* Device = GetAudioDevice();
	if (!Device)
	{
		return false;
	}
	return Device->GetDistanceToNearestListener(GetComponentLocation()) < InnerRadius + FalloffDistance + 500.f;
}

void UTN_BeachEnemySynthComponent::EnsurePlaying()
{
	if (!IsPlaying())
	{
		TNAudioVoices::Apply(*this, TNAudioVoices::ERank::World);
		Start();
	}
	SetComponentTickEnabled(true);
}

void UTN_BeachEnemySynthComponent::Play(ETNBeachSfx Sound, float Pitch, float Volume)
{
	if (!Shared.IsValid() || Volume <= 0.f || !IsListenerNear())
	{
		return;
	}
	EnsurePlaying();
	Shared->Master.store(FMath::Clamp(Loudness, 0.f, 2.f), std::memory_order_relaxed);
	TNBeachSynthDSP::FEvent Shot;
	Shot.Kind = static_cast<uint8>(Sound);
	Shot.Pitch = Pitch;
	Shot.Gain = Volume;
	Shared->Push(Shot);
	// La cola más larga (palmeras) dura 1,4 s: con 3 s de margen no se corta nada.
	SilenceLeft = FMath::Max(SilenceLeft, 3.f);
}

void UTN_BeachEnemySynthComponent::SetEngine(float Level, float Rpm)
{
	if (!Shared.IsValid())
	{
		return;
	}
	EngineLevel = FMath::Clamp(Level, 0.f, 1.5f);
	Shared->EngineLevel.store(EngineLevel, std::memory_order_relaxed);
	Shared->EngineRpm.store(FMath::Clamp(Rpm, 0.f, 1.f), std::memory_order_relaxed);
	if (EngineLevel > 0.001f && IsListenerNear())
	{
		Shared->Master.store(FMath::Clamp(Loudness, 0.f, 2.f), std::memory_order_relaxed);
		EnsurePlaying();
		SilenceLeft = FMath::Max(SilenceLeft, 2.f);
	}
}

void UTN_BeachEnemySynthComponent::SetWind(float Level)
{
	if (!Shared.IsValid())
	{
		return;
	}
	WindLevel = FMath::Clamp(Level, 0.f, 1.5f);
	Shared->WindLevel.store(WindLevel, std::memory_order_relaxed);
	if (WindLevel > 0.001f && IsListenerNear())
	{
		Shared->Master.store(FMath::Clamp(Loudness, 0.f, 2.f), std::memory_order_relaxed);
		EnsurePlaying();
		SilenceLeft = FMath::Max(SilenceLeft, 2.f);
	}
}

void UTN_BeachEnemySynthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const bool bContinuous = EngineLevel > 0.001f || WindLevel > 0.001f;
	if (bContinuous && IsListenerNear())
	{
		SilenceLeft = FMath::Max(SilenceLeft, 2.f);
		return;
	}
	SilenceLeft -= DeltaTime;
	if (SilenceLeft <= 0.f)
	{
		// Callado o lejos: se libera la voz del mezclador hasta que vuelva a hacer falta.
		if (IsPlaying())
		{
			Stop();
		}
		SetComponentTickEnabled(false);
	}
}

UTN_BeachEnemySynthComponent* UTN_BeachEnemySynthComponent::AttachTo(AActor* InOwner, USceneComponent* InParent, float InInnerRadius, float InFalloff)
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
	UTN_BeachEnemySynthComponent* Comp = NewObject<UTN_BeachEnemySynthComponent>(InOwner, NAME_None, RF_Transient);
	Comp->InnerRadius = FMath::Max(0.f, InInnerRadius);
	Comp->FalloffDistance = FMath::Max(100.f, InFalloff);
	USceneComponent* Parent = InParent ? InParent : InOwner->GetRootComponent();
	if (Parent)
	{
		Comp->SetupAttachment(Parent);
	}
	Comp->RegisterComponent();
	InOwner->AddInstanceComponent(Comp);
	return Comp;
}
