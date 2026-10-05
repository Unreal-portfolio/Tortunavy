#include "World/Beach/TN_BeachSandWormSynth.h"
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
 * Motor de sonido del gusano de arena. Hilos: la cola y el nivel del retumbar los escribe el hilo de juego; el hilo de
 * render de audio vacía la cola al principio de cada bloque y lee el nivel. Voces, filtros y osciladores viven solo en el
 * hilo de audio, sin asignaciones ni bloqueos. Los parámetros lentos se calculan una vez por bloque de 16 muestras.
 */
namespace TNSandWormDSP
{
	constexpr float SwPi = 3.14159265358979323846f;
	constexpr float SwTwoPi = 6.28318530717958647692f;
	constexpr int32 SwMaxVoices = 8;
	constexpr int32 SwBlock = 16;

	/** Tipos de efecto (el orden es el de ETNSandWormSfx). */
	constexpr uint8 KindRoar = 0;
	constexpr uint8 KindSand = 1;
	constexpr uint8 KindBite = 2;
	constexpr uint8 KindGulp = 3;
	constexpr uint8 KindBurp = 4;

	/** Del primer mordisco del bocado al segundo (s). */
	constexpr float BiteGap = 0.17f;
	/** Del primer «glup» del trago al segundo (s). */
	constexpr float GulpGap = 0.2f;

	struct FEvent
	{
		uint8 Kind = 0;
		float Pitch = 1.f;
		float Gain = 1.f;
	};

	/** Cola circular sin bloqueos (un productor, un consumidor) y nivel del retumbar atómico. */
	struct FShared
	{
		static constexpr uint32 Capacity = 16;

		FEvent Events[Capacity];
		std::atomic<uint32> WriteIndex{ 0 };
		std::atomic<uint32> ReadIndex{ 0 };
		std::atomic<float> Master{ 1.f };
		std::atomic<float> RumbleLevel{ 0.f };

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
	inline float SwNoise(uint32& State)
	{
		State ^= State << 13;
		State ^= State >> 17;
		State ^= State << 5;
		return static_cast<float>(State >> 8) * (1.f / 8388608.f) - 1.f;
	}

	/** Coeficiente del filtro de estado variable (Chamberlin); estable por debajo de ~1/6 de la frecuencia de muestreo. */
	inline float SwSvfCoef(float CutHz, float Rate)
	{
		return 2.f * std::sin(SwPi * FMath::Clamp(CutHz, 20.f, Rate * 0.16f) / Rate);
	}

	/** Saturación suave (aproximación racional de tanh). */
	inline float SwSoftClip(float X)
	{
		const float C = FMath::Clamp(X, -3.f, 3.f);
		return C * (27.f + C * C) / (27.f + 9.f * C * C);
	}

	/** Avanza una fase en [0, 1). */
	inline void SwAdvance(float& Phase, float Hz, float Dt)
	{
		Phase += Hz * Dt;
		Phase -= std::floor(Phase);
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

	/** Resonador de dos polos normalizado (dientes que chocan, piedrecitas). */
	struct FReso
	{
		float Y1 = 0.f;
		float Y2 = 0.f;
		float C = 0.f;
		float R2 = 0.f;
		float Norm = 1.f;

		void Set(float Hz, float DecaySeconds, float Rate)
		{
			const float W = SwTwoPi * FMath::Min(Hz, Rate * 0.45f) / Rate;
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

	/** Mezclador de voces y retumbar, con limitador suave al final. */
	class FCore
	{
	public:
		void Init(float InRate)
		{
			Rate = FMath::Max(8000.f, InRate);
			InvRate = 1.f / Rate;
			CrackleRes.Set(1500.f, 0.005f, Rate);
		}

		/** Mono repetido en todos los canales (el componente es mono y espacializado). */
		void Render(float* Out, int32 Frames, int32 Channels, FShared& Shared)
		{
			FEvent Pending;
			while (Shared.Pop(Pending))
			{
				StartVoice(Pending);
			}
			const float RumbleTarget = FMath::Clamp(Shared.RumbleLevel.load(std::memory_order_relaxed), 0.f, 1.5f);
			bool bAny = RumbleTarget > 1e-4f || RumbleLevel > 1e-4f;
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
			const float BlockDt = SwBlock * InvRate;
			const float RumbleK = 1.f - std::exp(-BlockDt / 0.12f);
			for (int32 Frame = 0; Frame < Frames; Frame += SwBlock)
			{
				const int32 Count = FMath::Min(SwBlock, Frames - Frame);
				float MixBuf[SwBlock] = {};
				for (FVoice& Voice : Voices)
				{
					if (Voice.bActive)
					{
						RenderVoice(Voice, MixBuf, Count);
					}
				}
				RumbleLevel += (RumbleTarget - RumbleLevel) * RumbleK;
				if (RumbleLevel > 1e-4f)
				{
					RenderRumble(MixBuf, Count);
				}
				for (int32 i = 0; i < Count; ++i)
				{
					const float Soft = SwSoftClip(MixBuf[i] * MasterGain);
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
		FVoice Voices[SwMaxVoices];
		uint32 SeedState = 0x9E3779B9u;

		// Retumbar de debajo de la arena.
		float RumbleLevel = 0.f;
		float RumbleTime = 0.f;
		float SubPhase = 0.f;
		float GrowlPhase = 0.f;
		float RumbleLp = 0.f;
		FSvf GrowlSvf;
		float CrackleClock = 0.f;
		FReso CrackleRes;
		uint32 RumbleNoise = 0x68E31DA4u;

		/** Coge una voz libre (o la más vieja) y la prepara. */
		void StartVoice(const FEvent& InEvent)
		{
			int32 Best = 0;
			float Oldest = -1.f;
			for (int32 i = 0; i < SwMaxVoices; ++i)
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
			const float P = Voice.Pitch;
			switch (InEvent.Kind)
			{
			case KindRoar: Voice.Duration = 1.6f; break;
			case KindSand:
				Voice.Duration = 1.25f;
				Voice.ResA.Set(2300.f * P, 0.004f, Rate);
				break;
			case KindBite:
				Voice.Duration = 0.55f;
				Voice.ResA.Set(820.f * P, 0.025f, Rate);
				Voice.ResB.Set(1750.f * P, 0.012f, Rate);
				Voice.NextEvent = 0.004f;
				break;
			case KindGulp: Voice.Duration = 0.5f; break;
			default: Voice.Duration = 1.15f; break;
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
			case KindRoar:
			{
				// Rugido grave: dos dientes de sierra casi iguales y un sub, con un gruñido que tiembla (27 Hz), por dos
				// formantes que se abren y se cierran y un paso bajo; saturado para que suene a garganta enorme.
				const float Att = FMath::Min(1.f, T / 0.08f);
				const float Rel = FMath::Clamp((Voice.Duration - T) / 0.55f, 0.f, 1.f);
				const float Env = Att * Rel;
				const float Freq = P * (52.f + 34.f * std::sin(SwPi * FMath::Min(1.f, X * 1.5f)) - 10.f * X) * (1.f + 0.025f * std::sin(SwTwoPi * 6.f * T));
				const float GrowlAm = 0.62f + 0.38f * std::sin(SwTwoPi * 27.f * T);
				const float GA = SwSvfCoef(P * (380.f + 160.f * std::sin(SwPi * X)), Rate);
				const float GB = SwSvfCoef(820.f * P, Rate);
				for (int32 i = 0; i < Count; ++i)
				{
					SwAdvance(Voice.PhaseA, Freq, Dt);
					SwAdvance(Voice.PhaseB, Freq * 1.007f, Dt);
					SwAdvance(Voice.PhaseC, Freq * 0.5f, Dt);
					const float Saw = (2.f * Voice.PhaseA - 1.f) + 0.7f * (2.f * Voice.PhaseB - 1.f);
					const float Src = Saw * GrowlAm + 0.35f * SwNoise(Voice.NoiseState);
					Voice.SvfA.Tick(Src, GA, 0.45f);
					Voice.SvfB.Tick(Src, GB, 0.6f);
					Voice.Lp += 0.02f * (Src - Voice.Lp);
					const float Sub = std::sin(SwTwoPi * Voice.PhaseC);
					MixBuf[i] += SwSoftClip((0.55f * Voice.SvfA.Band + 0.3f * Voice.SvfB.Band + 0.9f * Voice.Lp + 0.5f * Sub) * 1.5f) * Env * 0.85f * Voice.Gain;
				}
				break;
			}
			case KindSand:
			{
				// Arena que revienta: soplo de ruido por un paso banda que baja de agudo a medio, un golpe grave y granitos
				// que caen cada vez más despacio (un resonador agudo golpeado al azar).
				const float Env = T < 0.012f ? T / 0.012f : std::exp(-(T - 0.012f) / 0.38f);
				const float G = SwSvfCoef(P * (650.f + 2600.f * std::exp(-T / 0.22f)), Rate);
				const float ThumpFreq = 42.f * P * (1.f + 0.8f * std::exp(-T / 0.05f));
				const float ThumpEnv = 0.9f * std::exp(-T / 0.18f);
				const float GrainRate = 70.f * (1.f - 0.6f * X);
				for (int32 i = 0; i < Count; ++i)
				{
					const float N = SwNoise(Voice.NoiseState);
					Voice.SvfA.Tick(N, G, 0.7f);
					Voice.Clock += GrainRate * Dt;
					float Excite = 0.f;
					if (Voice.Clock >= 1.f)
					{
						Voice.Clock -= 1.f + 0.6f * SwNoise(Voice.NoiseState);
						Excite = 0.5f + 0.5f * std::abs(SwNoise(Voice.NoiseState));
					}
					SwAdvance(Voice.PhaseA, ThumpFreq, Dt);
					const float Grains = Voice.ResA.Tick(Excite);
					MixBuf[i] += (0.75f * Voice.SvfA.Band * Env + 0.35f * Grains * Env + std::sin(SwTwoPi * Voice.PhaseA) * ThumpEnv) * Voice.Gain;
				}
				break;
			}
			case KindBite:
			{
				// «¡ÑAM!»: dos mordiscos húmedos (golpe grave que cae de tono y chapoteo de ruido apagado) con el chasquido
				// de los dientes al chocar en cada uno.
				for (int32 i = 0; i < Count; ++i)
				{
					const float Ts = T + i * Dt;
					const float Tb = Ts - BiteGap;
					SwAdvance(Voice.PhaseA, P * (55.f + 120.f * std::exp(-Ts / 0.06f)), Dt);
					float Thump = std::sin(SwTwoPi * Voice.PhaseA) * std::exp(-Ts / 0.09f);
					float Wet = std::exp(-Ts / 0.05f);
					if (Tb >= 0.f)
					{
						SwAdvance(Voice.PhaseB, P * (50.f + 100.f * std::exp(-Tb / 0.06f)), Dt);
						Thump += 0.7f * std::sin(SwTwoPi * Voice.PhaseB) * std::exp(-Tb / 0.08f);
						Wet += 0.6f * std::exp(-Tb / 0.045f);
					}
					float Excite = 0.f;
					if (Voice.Count < 2 && Ts >= Voice.NextEvent)
					{
						Excite = Voice.Count == 0 ? 1.f : 0.75f;
						++Voice.Count;
						Voice.NextEvent = BiteGap + 0.004f;
					}
					Voice.Lp += 0.18f * (SwNoise(Voice.NoiseState) - Voice.Lp);
					const float Clack = 0.5f * Voice.ResA.Tick(Excite) + 0.35f * Voice.ResB.Tick(Excite);
					MixBuf[i] += (0.95f * Thump + 1.1f * Voice.Lp * Wet + Clack) * Voice.Gain;
				}
				break;
			}
			case KindGulp:
			{
				// Trago de dibujos: dos «glup» (un seno que cae de tono con un pelo de ruido húmedo).
				for (int32 i = 0; i < Count; ++i)
				{
					const float Ts = T + i * Dt;
					const float Tg = Ts - GulpGap;
					const float EnvA = Ts < 0.01f ? Ts / 0.01f : std::exp(-(Ts - 0.01f) / 0.09f);
					SwAdvance(Voice.PhaseA, P * (90.f + 210.f * std::exp(-Ts / 0.07f)), Dt);
					float Out = std::sin(SwTwoPi * Voice.PhaseA) * EnvA;
					float Wet = EnvA;
					if (Tg >= 0.f)
					{
						const float EnvB = 0.8f * (Tg < 0.01f ? Tg / 0.01f : std::exp(-(Tg - 0.01f) / 0.1f));
						SwAdvance(Voice.PhaseB, P * (70.f + 150.f * std::exp(-Tg / 0.08f)), Dt);
						Out += std::sin(SwTwoPi * Voice.PhaseB) * EnvB;
						Wet += EnvB;
					}
					Voice.Lp += 0.1f * (SwNoise(Voice.NoiseState) - Voice.Lp);
					MixBuf[i] += (0.9f * Out + 0.5f * Voice.Lp * Wet) * Voice.Gain;
				}
				break;
			}
			default:
			{
				// Eructo: pulso ronco (~80 Hz) con el periodo temblón, aleteo de 23 Hz y dos formantes que pasan de «u» a «a»
				// y vuelven, más un soplo de arena.
				const float Att = FMath::Min(1.f, T / 0.05f);
				const float Rel = FMath::Clamp((Voice.Duration - T) / 0.4f, 0.f, 1.f);
				const float Env = Att * Rel;
				const float Freq = P * (82.f + 10.f * std::sin(SwPi * X) - 18.f * X);
				const float Flutter = 0.6f + 0.4f * std::sin(SwTwoPi * 23.f * T);
				const float GA = SwSvfCoef(P * (330.f + 320.f * std::sin(SwPi * FMath::Min(1.f, X * 1.3f))), Rate);
				const float GB = SwSvfCoef(P * (780.f + 420.f * std::sin(SwPi * FMath::Min(1.f, X * 1.2f))), Rate);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.PhaseA += Freq * (1.f + Voice.Jitter) * Dt;
					if (Voice.PhaseA >= 1.f)
					{
						Voice.PhaseA -= std::floor(Voice.PhaseA);
						Voice.Jitter = 0.1f * SwNoise(Voice.NoiseState);
					}
					const float Pulse = Voice.PhaseA < 0.28f ? 1.f : -0.39f;
					Voice.SvfA.Tick(Pulse, GA, 0.35f);
					Voice.SvfB.Tick(Pulse, GB, 0.45f);
					Voice.Lp += 0.08f * (SwNoise(Voice.NoiseState) - Voice.Lp);
					const float Voiced = SwSoftClip((0.7f * Voice.SvfA.Band + 0.45f * Voice.SvfB.Band + 0.08f * Pulse) * 1.4f);
					MixBuf[i] += (Voiced * Flutter + 0.25f * Voice.Lp) * Env * Voice.Gain;
				}
				break;
			}
			}
			Voice.Age += static_cast<float>(Count) * Dt;
			if (Voice.Age >= Voice.Duration)
			{
				Voice.bActive = false;
			}
		}

		/** Retumbar: ruido muy grave, un sub que oscila despacio, un gruñido que rechina y crujidos de piedrecitas. */
		void RenderRumble(float* MixBuf, int32 Count)
		{
			const float Dt = InvRate;
			const float Level = RumbleLevel;
			const float GrowlG = SwSvfCoef(260.f, Rate);
			const float CrackleRate = 18.f + 30.f * Level;
			for (int32 i = 0; i < Count; ++i)
			{
				RumbleTime += Dt;
				if (RumbleTime > 1000.f)
				{
					RumbleTime -= 1000.f;
				}
				SwAdvance(SubPhase, 31.f * (1.f + 0.1f * std::sin(SwTwoPi * 0.8f * RumbleTime)), Dt);
				SwAdvance(GrowlPhase, 47.f, Dt);
				const float N = SwNoise(RumbleNoise);
				RumbleLp += 0.012f * (N - RumbleLp);
				GrowlSvf.Tick(2.f * GrowlPhase - 1.f, GrowlG, 0.6f);
				const float Grind = 0.5f + 0.5f * std::sin(SwTwoPi * 9.f * RumbleTime);
				CrackleClock += CrackleRate * Dt;
				float Excite = 0.f;
				if (CrackleClock >= 1.f)
				{
					CrackleClock -= 1.f + 0.7f * SwNoise(RumbleNoise);
					Excite = 0.4f + 0.6f * std::abs(SwNoise(RumbleNoise));
				}
				const float Crackle = CrackleRes.Tick(Excite);
				MixBuf[i] += (0.8f * std::sin(SwTwoPi * SubPhase) + 2.2f * RumbleLp + 0.45f * GrowlSvf.Low * Grind + 0.3f * Crackle) * Level;
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

static_assert(static_cast<uint8>(ETNSandWormSfx::Roar) == TNSandWormDSP::KindRoar, "ETNSandWormSfx y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNSandWormSfx::Bite) == TNSandWormDSP::KindBite, "ETNSandWormSfx y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNSandWormSfx::Burp) == TNSandWormDSP::KindBurp, "ETNSandWormSfx y el motor DSP deben coincidir");

// ─────────────────────────────────────────────────────────────────────────────
// UTN_BeachSandWormSynthComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_BeachSandWormSynthComponent::UTN_BeachSandWormSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// El tick solo vigila el silencio y la distancia al oyente, cuatro veces por segundo.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.25f;
	bAutoActivate = false;
	NumChannels = 1;
	Shared = MakeShared<TNSandWormDSP::FShared, ESPMode::ThreadSafe>();
}

void UTN_BeachSandWormSynthComponent::OnRegister()
{
	// Antes de que el padre cree el componente de audio.
	ConfigureSpatial();
	Super::OnRegister();
}

void UTN_BeachSandWormSynthComponent::ConfigureSpatial()
{
	NumChannels = 1;
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	// Un bicho de 25 m: se oye de lejos, con caída natural y agudos que se apagan con la distancia.
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

bool UTN_BeachSandWormSynthComponent::Init(int32& /*SampleRate*/)
{
	NumChannels = 1;
	return true;
}

ISoundGeneratorPtr UTN_BeachSandWormSynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<TNSandWormDSP::FGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, Shared);
}

bool UTN_BeachSandWormSynthComponent::IsListenerNear() const
{
	const FAudioDevice* Device = GetAudioDevice();
	if (!Device)
	{
		return false;
	}
	return Device->GetDistanceToNearestListener(GetComponentLocation()) < InnerRadius + FalloffDistance + 500.f;
}

void UTN_BeachSandWormSynthComponent::EnsurePlaying()
{
	if (!IsPlaying())
	{
		TNAudioVoices::Apply(*this, TNAudioVoices::ERank::World);
		Start();
	}
	SetComponentTickEnabled(true);
}

void UTN_BeachSandWormSynthComponent::Play(ETNSandWormSfx Sound, float Pitch, float Volume)
{
	if (!Shared.IsValid() || Volume <= 0.f || !IsListenerNear())
	{
		return;
	}
	EnsurePlaying();
	Shared->Master.store(FMath::Clamp(Loudness, 0.f, 2.f), std::memory_order_relaxed);
	TNSandWormDSP::FEvent Shot;
	Shot.Kind = static_cast<uint8>(Sound);
	Shot.Pitch = Pitch;
	Shot.Gain = Volume;
	Shared->Push(Shot);
	// El más largo (rugido) dura 1,6 s: con 3 s de margen no se corta nada.
	SilenceLeft = FMath::Max(SilenceLeft, 3.f);
}

void UTN_BeachSandWormSynthComponent::SetRumble(float Level)
{
	if (!Shared.IsValid())
	{
		return;
	}
	RumbleLevel = FMath::Clamp(Level, 0.f, 1.5f);
	Shared->RumbleLevel.store(RumbleLevel, std::memory_order_relaxed);
	if (RumbleLevel > 0.001f && IsListenerNear())
	{
		Shared->Master.store(FMath::Clamp(Loudness, 0.f, 2.f), std::memory_order_relaxed);
		EnsurePlaying();
		SilenceLeft = FMath::Max(SilenceLeft, 2.f);
	}
}

void UTN_BeachSandWormSynthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (RumbleLevel > 0.001f && IsListenerNear())
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

UTN_BeachSandWormSynthComponent* UTN_BeachSandWormSynthComponent::AttachTo(AActor* InOwner, USceneComponent* InParent, float InInnerRadius, float InFalloff)
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
	UTN_BeachSandWormSynthComponent* Comp = NewObject<UTN_BeachSandWormSynthComponent>(InOwner, NAME_None, RF_Transient | RF_DuplicateTransient);
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
