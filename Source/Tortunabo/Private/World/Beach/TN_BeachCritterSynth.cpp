#include "World/Beach/TN_BeachCritterSynth.h"
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
 * Motor de sonido de los enemigos de la ronda 3 (ermitaño, pulpo, pulgas y tanque). Hilos: la cola y los niveles
 * continuos los escribe el hilo de juego; el hilo de render de audio vacía la cola al principio de cada bloque y lee los
 * niveles. Voces, filtros y osciladores viven solo en el hilo de audio, sin asignaciones ni bloqueos. Los parámetros
 * lentos se calculan una vez por bloque de 16 muestras (unos 0,3 ms a 48 kHz).
 */
namespace TNBeachCritterDSP
{
	constexpr float CsPi = 3.14159265358979323846f;
	constexpr float CsTwoPi = 6.28318530717958647692f;
	constexpr int32 CsMaxVoices = 10;
	constexpr int32 CsBlock = 16;

	/** Tipos de efecto (el orden es el de ETNBeachCritterSfx). */
	constexpr uint8 KindShellPop = 0;
	constexpr uint8 KindStrike = 1;
	constexpr uint8 KindThud = 2;
	constexpr uint8 KindRattle = 3;
	constexpr uint8 KindTap = 4;
	constexpr uint8 KindSplash = 5;
	constexpr uint8 KindBubble = 6;
	constexpr uint8 KindSlap = 7;
	constexpr uint8 KindInk = 8;
	constexpr uint8 KindItch = 9;
	constexpr uint8 KindFoamPop = 10;
	constexpr uint8 KindFoamHit = 11;
	constexpr uint8 KindSputter = 12;
	constexpr uint8 KindBoing = 13;
	constexpr uint8 KindServo = 14;

	struct FCritterEvent
	{
		uint8 Kind = 0;
		float Pitch = 1.f;
		float Gain = 1.f;
	};

	/** Cola circular sin bloqueos (un productor, un consumidor) y niveles continuos atómicos. */
	struct FCritterShared
	{
		static constexpr uint32 Capacity = 32;

		FCritterEvent Events[Capacity];
		std::atomic<uint32> WriteIndex{ 0 };
		std::atomic<uint32> ReadIndex{ 0 };
		std::atomic<float> Master{ 1.f };
		std::atomic<float> RollLevel{ 0.f };
		std::atomic<float> RollSpeed{ 0.f };
		std::atomic<float> SwarmLevel{ 0.f };
		std::atomic<float> MotorLevel{ 0.f };
		std::atomic<float> MotorRpm{ 0.f };

		/** Hilo de juego. false si la cola está llena (el disparo se pierde). */
		bool Push(const FCritterEvent& InEvent)
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
		bool Pop(FCritterEvent& OutEvent)
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
	inline float CsNoise(uint32& State)
	{
		State ^= State << 13;
		State ^= State >> 17;
		State ^= State << 5;
		return static_cast<float>(State >> 8) * (1.f / 8388608.f) - 1.f;
	}

	/** Coeficiente del filtro de estado variable (Chamberlin); estable por debajo de ~1/6 de la frecuencia de muestreo. */
	inline float CsSvfCoef(float CutHz, float SampleRate)
	{
		return 2.f * std::sin(CsPi * FMath::Clamp(CutHz, 20.f, SampleRate * 0.16f) / SampleRate);
	}

	/** Saturación suave (aproximación racional de tanh). */
	inline float CsSoftClip(float X)
	{
		const float C = FMath::Clamp(X, -3.f, 3.f);
		return C * (27.f + C * C) / (27.f + 9.f * C * C);
	}

	/** Avanza una fase normalizada [0, 1). */
	inline float CsAdvance(float& Phase, float Hz, float Dt)
	{
		Phase += Hz * Dt;
		Phase -= std::floor(Phase);
		return Phase;
	}

	/** Filtro de estado variable: paso bajo y paso banda. */
	struct FCsSvf
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

	/** Resonador de dos polos normalizado (golpes huecos, chasquidos). */
	struct FCsReso
	{
		float Y1 = 0.f;
		float Y2 = 0.f;
		float C = 0.f;
		float R2 = 0.f;
		float Norm = 1.f;

		void Set(float Hz, float DecaySeconds, float SampleRate)
		{
			const float W = CsTwoPi * FMath::Min(Hz, SampleRate * 0.45f) / SampleRate;
			const float R = std::exp(-1.f / (FMath::Max(0.001f, DecaySeconds) * SampleRate));
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
	struct FCritterVoice
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
		FCsSvf SvfA;
		FCsSvf SvfB;
		float Lp = 0.f;
		float Clock = 0.f;
		float NextEvent = 0.f;
		int32 Count = 0;
		float Jitter = 0.f;
		FCsReso ResA;
		FCsReso ResB;
		uint32 NoiseState = 0x2545F491u;
	};

	/** Mezclador de voces y de los tres continuos (concha, enjambre y motor), con limitador suave al final. */
	class FCritterCore
	{
	public:
		void Init(float InRate)
		{
			Rate = FMath::Max(8000.f, InRate);
			InvRate = 1.f / Rate;
			RollKnock.Set(260.f, 0.05f, Rate);
			SwarmResA.Set(3300.f, 0.004f, Rate);
			SwarmResB.Set(4700.f, 0.003f, Rate);
		}

		/** Mono repetido en todos los canales (el componente es mono y espacializado). */
		void Render(float* Out, int32 Frames, int32 Channels, FCritterShared& Shared)
		{
			FCritterEvent Pending;
			while (Shared.Pop(Pending))
			{
				StartVoice(Pending);
			}
			const float RollTarget = FMath::Clamp(Shared.RollLevel.load(std::memory_order_relaxed), 0.f, 1.5f);
			const float RollSpeedTarget = FMath::Clamp(Shared.RollSpeed.load(std::memory_order_relaxed), 0.f, 1.f);
			const float SwarmTarget = FMath::Clamp(Shared.SwarmLevel.load(std::memory_order_relaxed), 0.f, 1.5f);
			const float MotorTarget = FMath::Clamp(Shared.MotorLevel.load(std::memory_order_relaxed), 0.f, 1.5f);
			const float RpmTarget = FMath::Clamp(Shared.MotorRpm.load(std::memory_order_relaxed), 0.f, 1.f);
			bool bAny = RollTarget > 1e-4f || RollLevel > 1e-4f || SwarmTarget > 1e-4f || SwarmLevel > 1e-4f || MotorTarget > 1e-4f || MotorLevel > 1e-4f;
			for (const FCritterVoice& Voice : Voices)
			{
				bAny |= Voice.bActive;
			}
			if (!bAny)
			{
				FMemory::Memzero(Out, sizeof(float) * Frames * Channels);
				return;
			}
			const float MasterGain = Shared.Master.load(std::memory_order_relaxed);
			const float BlockDt = CsBlock * InvRate;
			const float LevelK = 1.f - std::exp(-BlockDt / 0.12f);
			const float SpeedK = 1.f - std::exp(-BlockDt / 0.25f);
			for (int32 Frame = 0; Frame < Frames; Frame += CsBlock)
			{
				const int32 Count = FMath::Min(CsBlock, Frames - Frame);
				float MixBuf[CsBlock] = {};
				for (FCritterVoice& Voice : Voices)
				{
					if (Voice.bActive)
					{
						RenderVoice(Voice, MixBuf, Count);
					}
				}
				RollLevel += (RollTarget - RollLevel) * LevelK;
				RollSpeed += (RollSpeedTarget - RollSpeed) * SpeedK;
				SwarmLevel += (SwarmTarget - SwarmLevel) * LevelK;
				MotorLevel += (MotorTarget - MotorLevel) * LevelK;
				MotorRpm += (RpmTarget - MotorRpm) * SpeedK;
				if (RollLevel > 1e-4f)
				{
					RenderRoll(MixBuf, Count);
				}
				if (SwarmLevel > 1e-4f)
				{
					RenderSwarm(MixBuf, Count);
				}
				if (MotorLevel > 1e-4f)
				{
					RenderMotor(MixBuf, Count);
				}
				for (int32 i = 0; i < Count; ++i)
				{
					const float Soft = CsSoftClip(MixBuf[i] * MasterGain);
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
		FCritterVoice Voices[CsMaxVoices];
		uint32 SeedState = 0x9E3779B9u;

		// Concha que rueda.
		float RollLevel = 0.f;
		float RollSpeed = 0.f;
		float RollClock = 0.f;
		FCsSvf RollSvf;
		FCsReso RollKnock;
		uint32 RollNoise = 0x68E31DA4u;

		// Enjambre de pulgas.
		float SwarmLevel = 0.f;
		float SwarmClock = 0.f;
		FCsReso SwarmResA;
		FCsReso SwarmResB;
		FCsSvf SwarmHiss;
		uint32 SwarmNoise = 0x1B873593u;

		// Motor eléctrico de juguete.
		float MotorLevel = 0.f;
		float MotorRpm = 0.f;
		float MotorPhase = 0.f;
		float GearPhase = 0.f;
		FCsSvf MotorSvf;
		uint32 MotorNoise = 0x7F4A7C15u;

		/** Coge una voz libre (o la más vieja) y la prepara. */
		void StartVoice(const FCritterEvent& InEvent)
		{
			int32 Best = 0;
			float Oldest = -1.f;
			for (int32 i = 0; i < CsMaxVoices; ++i)
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
			FCritterVoice& Voice = Voices[Best];
			Voice = FCritterVoice();
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
			case KindShellPop: Voice.Duration = 0.3f; break;
			case KindStrike:
				Voice.Duration = 1.1f;
				Voice.ResA.Set(520.f * P, 0.06f, Rate);
				Voice.ResB.Set(1270.f * P, 0.035f, Rate);
				break;
			case KindThud: Voice.Duration = 0.4f; break;
			case KindRattle:
				Voice.Duration = 0.95f;
				Voice.ResA.Set(1650.f * P, 0.008f, Rate);
				Voice.ResB.Set(2900.f * P, 0.005f, Rate);
				break;
			case KindTap:
				Voice.Duration = 0.22f;
				Voice.ResA.Set(2300.f * P, 0.005f, Rate);
				break;
			case KindSplash: Voice.Duration = 0.8f; break;
			case KindBubble: Voice.Duration = 0.2f; break;
			case KindSlap: Voice.Duration = 0.5f; break;
			case KindInk: Voice.Duration = 0.7f; break;
			case KindItch:
				Voice.Duration = 0.85f;
				Voice.ResA.Set(3600.f * P, 0.004f, Rate);
				break;
			case KindFoamPop: Voice.Duration = 0.32f; break;
			case KindFoamHit: Voice.Duration = 0.3f; break;
			case KindSputter: Voice.Duration = 0.65f; break;
			case KindBoing: Voice.Duration = 0.9f; break;
			default: Voice.Duration = 0.45f; break;
			}
		}

		void RenderVoice(FCritterVoice& Voice, float* MixBuf, int32 Count)
		{
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float P = Voice.Pitch;
			const float X = FMath::Clamp(T / FMath::Max(0.05f, Voice.Duration), 0.f, 1.f);
			switch (Voice.Kind)
			{
			case KindShellPop:
			{
				// «¡Plop!»: tono que cae de golpe (ventosa que se suelta) y un clic de caparazón.
				const float Freq = P * (240.f + 760.f * std::exp(-T / 0.035f));
				const float Env = std::exp(-T / 0.07f);
				const float Click = 0.4f * std::exp(-T / 0.004f);
				for (int32 i = 0; i < Count; ++i)
				{
					const float Ph = CsAdvance(Voice.PhaseA, Freq, Dt);
					MixBuf[i] += (0.75f * std::sin(CsTwoPi * Ph) * Env + CsNoise(Voice.NoiseState) * Click) * Voice.Gain;
				}
				break;
			}
			case KindStrike:
			{
				// Bolos: un golpe grave y seis toques huecos cada vez más flojos y seguidos al azar, con traqueteo.
				static constexpr float Times[7] = { 0.f, 0.045f, 0.1f, 0.17f, 0.27f, 0.41f, 0.6f };
				static constexpr float Levels[7] = { 1.f, 0.8f, 0.7f, 0.55f, 0.45f, 0.3f, 0.2f };
				const float Thump = 0.9f * std::exp(-T / 0.1f);
				const float ThumpFreq = 85.f * P * (1.f + 0.8f * std::exp(-T / 0.03f));
				const float Clatter = 0.25f * std::exp(-T / 0.35f);
				const float G = CsSvfCoef(2200.f * P, Rate);
				for (int32 i = 0; i < Count; ++i)
				{
					const float Ts = T + i * Dt;
					float Excite = 0.f;
					if (Voice.Count < 7 && Ts >= Times[Voice.Count] * (0.85f + 0.3f * Voice.Jitter))
					{
						Excite = Levels[Voice.Count];
						++Voice.Count;
					}
					const float N = CsNoise(Voice.NoiseState);
					Voice.SvfA.Tick(N, G, 0.8f);
					const float Ph = CsAdvance(Voice.PhaseA, ThumpFreq, Dt);
					MixBuf[i] += (0.7f * Voice.ResA.Tick(Excite) + 0.5f * Voice.ResB.Tick(Excite) + std::sin(CsTwoPi * Ph) * Thump
						+ Voice.SvfA.Band * Clatter) * Voice.Gain;
				}
				break;
			}
			case KindThud:
			{
				// Bote: golpe sordo que cae de tono y arena.
				const float Env = T < 0.004f ? T / 0.004f : std::exp(-(T - 0.004f) / 0.11f);
				const float Freq = 62.f * P * (1.f + 1.3f * std::exp(-T / 0.03f));
				const float Sand = 0.45f * std::exp(-T / 0.12f);
				const float G = CsSvfCoef(900.f, Rate);
				for (int32 i = 0; i < Count; ++i)
				{
					const float Ph = CsAdvance(Voice.PhaseA, Freq, Dt);
					Voice.SvfA.Tick(CsNoise(Voice.NoiseState), G, 0.9f);
					MixBuf[i] += (std::sin(CsTwoPi * Ph) * Env + Voice.SvfA.Low * Sand) * Voice.Gain;
				}
				break;
			}
			case KindRattle:
			{
				// Sacudida: chasquidos rápidos de concha (unos 22 por segundo) y arena que cae.
				const float Env = std::sqrt(std::sin(CsPi * X));
				const float PulseRate = 22.f * P;
				const float G = CsSvfCoef(3200.f, Rate);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.Clock += PulseRate * Dt;
					float Excite = 0.f;
					if (Voice.Clock >= 1.f)
					{
						Voice.Clock -= 1.f + 0.35f * CsNoise(Voice.NoiseState);
						Excite = 0.5f + 0.5f * std::abs(CsNoise(Voice.NoiseState));
					}
					const float N = CsNoise(Voice.NoiseState);
					Voice.SvfA.Tick(N, G, 0.7f);
					MixBuf[i] += ((0.6f * Voice.ResA.Tick(Excite) + 0.4f * Voice.ResB.Tick(Excite)) + 0.18f * Voice.SvfA.Band) * Env * Voice.Gain;
				}
				break;
			}
			case KindTap:
			{
				// Patitas: tres toques secos y agudos.
				for (int32 i = 0; i < Count; ++i)
				{
					const float Ts = T + i * Dt;
					float Excite = 0.f;
					if (Voice.Count < 3 && Ts >= Voice.NextEvent)
					{
						Excite = 0.7f - 0.15f * Voice.Count;
						++Voice.Count;
						Voice.NextEvent = Ts + 0.045f + 0.03f * Voice.Jitter;
					}
					MixBuf[i] += Voice.ResA.Tick(Excite) * 0.8f * Voice.Gain;
				}
				break;
			}
			case KindSplash:
			{
				// Chapoteo: ruido por un paso banda que cae de agudo a grave y un «bloop» hueco.
				const float Env = (T < 0.01f ? T / 0.01f : std::exp(-(T - 0.01f) / 0.22f));
				const float G = CsSvfCoef((600.f + 2600.f * std::exp(-T / 0.12f)) * P, Rate);
				const float BlopFreq = P * (90.f + 110.f * std::exp(-T / 0.06f));
				const float BlopEnv = 0.5f * std::exp(-T / 0.12f);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.SvfA.Tick(CsNoise(Voice.NoiseState), G, 0.5f);
					const float Ph = CsAdvance(Voice.PhaseA, BlopFreq, Dt);
					MixBuf[i] += (0.9f * Voice.SvfA.Band * Env + std::sin(CsTwoPi * Ph) * BlopEnv) * Voice.Gain;
				}
				break;
			}
			case KindBubble:
			{
				// Burbuja: tono redondo que sube un poco (la resonancia de la burbuja al encogerse) y se apaga enseguida. Más
				// grave, más blanda de ataque y más floja que antes (380-1000 Hz, 0,6): un seno agudo y seco repetido cada
				// par de segundos taladraba y sonaba a fuente.
				const float Freq = P * (290.f + 430.f * X * X);
				const float Env = std::exp(-T / 0.06f) * FMath::Min(1.f, T / 0.006f);
				for (int32 i = 0; i < Count; ++i)
				{
					const float Ph = CsAdvance(Voice.PhaseA, Freq, Dt);
					MixBuf[i] += 0.42f * std::sin(CsTwoPi * Ph) * Env * Voice.Gain;
				}
				break;
			}
			case KindSlap:
			{
				// Tentáculo: palmada mojada (ruido apagado y golpe grave) y, luego, la ventosa que chupa.
				const float SlapEnv = std::exp(-T / 0.035f);
				const float Thump = 0.6f * std::exp(-T / 0.08f);
				const float SuckEnv = T > 0.14f ? 0.45f * std::sin(CsPi * FMath::Clamp((T - 0.14f) / 0.32f, 0.f, 1.f)) : 0.f;
				const float G = CsSvfCoef((700.f + 1400.f * FMath::Clamp((T - 0.14f) / 0.32f, 0.f, 1.f)) * P, Rate);
				for (int32 i = 0; i < Count; ++i)
				{
					const float N = CsNoise(Voice.NoiseState);
					Voice.Lp += 0.3f * (N - Voice.Lp);
					Voice.SvfA.Tick(N, G, 0.25f);
					const float Ph = CsAdvance(Voice.PhaseA, 120.f * P, Dt);
					MixBuf[i] += (1.2f * Voice.Lp * SlapEnv + std::sin(CsTwoPi * Ph) * Thump + Voice.SvfA.Band * SuckEnv) * Voice.Gain;
				}
				break;
			}
			case KindInk:
			{
				// Tinta: «pfsshht» de ruido que baja de agudo a medio y un gorgoteo.
				const float Att = FMath::Min(1.f, T / 0.02f);
				const float Env = Att * std::pow(1.f - X, 1.4f);
				const float G = CsSvfCoef((700.f + 2000.f * (1.f - X)) * P, Rate);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.SvfA.Tick(CsNoise(Voice.NoiseState), G, 0.35f);
					const float Ph = CsAdvance(Voice.PhaseA, 150.f * P, Dt);
					const float Am = CsAdvance(Voice.PhaseB, 28.f, Dt);
					const float Gurgle = std::sin(CsTwoPi * Ph) * (0.5f + 0.5f * std::sin(CsTwoPi * Am));
					MixBuf[i] += (0.8f * Voice.SvfA.Band + 0.25f * Gurgle) * Env * Voice.Gain;
				}
				break;
			}
			case KindItch:
			{
				// Picor: chisporroteo muy rápido de patitas y chillidos diminutos que suben y bajan.
				const float Env = std::sqrt(std::sin(CsPi * X));
				const float PulseRate = 60.f * P;
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.Clock += PulseRate * Dt;
					float Excite = 0.f;
					if (Voice.Clock >= 1.f)
					{
						Voice.Clock -= 1.f + 0.5f * CsNoise(Voice.NoiseState);
						Excite = 0.4f + 0.6f * std::abs(CsNoise(Voice.NoiseState));
					}
					const float Chirp = 3200.f * P + 700.f * std::sin(CsTwoPi * CsAdvance(Voice.PhaseB, 7.5f, Dt));
					const float Ph = CsAdvance(Voice.PhaseA, Chirp, Dt);
					const float Gate = std::sin(CsTwoPi * Voice.PhaseB) > 0.4f ? 0.12f : 0.f;
					MixBuf[i] += (0.7f * Voice.ResA.Tick(Excite) + Gate * std::sin(CsTwoPi * Ph)) * Env * Voice.Gain;
				}
				break;
			}
			case KindFoamPop:
			{
				// «¡Pomp!»: clic, tono de tapón que cae y un soplo de aire.
				const float Freq = P * (160.f + 300.f * std::exp(-T / 0.03f));
				const float Env = std::exp(-T / 0.07f);
				const float Click = 0.6f * std::exp(-T / 0.003f);
				const float Puff = 0.35f * std::exp(-T / 0.1f);
				const float G = CsSvfCoef(1500.f * P, Rate);
				for (int32 i = 0; i < Count; ++i)
				{
					const float N = CsNoise(Voice.NoiseState);
					Voice.SvfA.Tick(N, G, 0.6f);
					const float Ph = CsAdvance(Voice.PhaseA, Freq, Dt);
					MixBuf[i] += (0.8f * std::sin(CsTwoPi * Ph) * Env + N * Click + Voice.SvfA.Band * Puff) * Voice.Gain;
				}
				break;
			}
			case KindFoamHit:
			{
				// «¡Paf!»: golpe blando de espuma.
				const float Env = std::exp(-T / 0.045f);
				const float Body = 0.5f * std::exp(-T / 0.07f);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.Lp += 0.2f * (CsNoise(Voice.NoiseState) - Voice.Lp);
					const float Ph = CsAdvance(Voice.PhaseA, 115.f * P, Dt);
					MixBuf[i] += (1.1f * Voice.Lp * Env + std::sin(CsTwoPi * Ph) * Body) * Voice.Gain;
				}
				break;
			}
			case KindSputter:
			{
				// Tos del motor: cuatro petardeos de zumbido y ruido.
				static constexpr float Coughs[4] = { 0.f, 0.13f, 0.29f, 0.46f };
				float Env = 0.f;
				for (int32 k = 0; k < 4; ++k)
				{
					if (T >= Coughs[k])
					{
						Env += std::exp(-(T - Coughs[k]) / 0.045f) * (1.f - 0.18f * k);
					}
				}
				for (int32 i = 0; i < Count; ++i)
				{
					const float Ph = CsAdvance(Voice.PhaseA, 95.f * P, Dt);
					const float Buzz = Ph < 0.3f ? 1.f : -0.43f;
					Voice.Lp += 0.35f * (CsNoise(Voice.NoiseState) - Voice.Lp);
					MixBuf[i] += CsSoftClip((0.5f * Buzz + 0.8f * Voice.Lp) * 1.5f) * Env * 0.6f * Voice.Gain;
				}
				break;
			}
			case KindBoing:
			{
				// Muelle: tono con vibrato que se abre y se apaga («boiing»).
				const float Wob = 0.16f * std::exp(-T / 0.4f) * std::sin(CsTwoPi * 11.f * T);
				const float Freq = P * 210.f * (1.f + 0.35f * std::exp(-T / 0.12f) + Wob);
				const float Env = FMath::Min(1.f, T / 0.005f) * std::exp(-T / 0.32f);
				for (int32 i = 0; i < Count; ++i)
				{
					const float Ph = CsAdvance(Voice.PhaseA, Freq, Dt);
					const float Ph2 = CsAdvance(Voice.PhaseB, Freq * 2.02f, Dt);
					MixBuf[i] += (0.55f * std::sin(CsTwoPi * Ph) + 0.2f * std::sin(CsTwoPi * Ph2)) * Env * Voice.Gain;
				}
				break;
			}
			default:
			{
				// Servo: zumbido de engranajes que sube un poco de tono.
				const float Env = std::sin(CsPi * X);
				const float Freq = P * (620.f + 260.f * X);
				const float G = CsSvfCoef(1800.f * P, Rate);
				for (int32 i = 0; i < Count; ++i)
				{
					const float Ph = CsAdvance(Voice.PhaseA, Freq, Dt);
					Voice.SvfA.Tick(2.f * Ph - 1.f + 0.1f * CsNoise(Voice.NoiseState), G, 0.5f);
					MixBuf[i] += 0.3f * Voice.SvfA.Band * Env * Voice.Gain;
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

		/** Concha que rueda: retumbar hueco de arena y un golpe por vuelta, más seguidos cuanto más deprisa. */
		void RenderRoll(float* MixBuf, int32 Count)
		{
			const float Dt = InvRate;
			const float G = CsSvfCoef(160.f + 520.f * RollSpeed, Rate);
			const float KnockRate = 1.5f + 6.f * RollSpeed;
			for (int32 i = 0; i < Count; ++i)
			{
				RollClock += KnockRate * Dt;
				float Excite = 0.f;
				if (RollClock >= 1.f)
				{
					RollClock -= 1.f;
					Excite = 0.6f + 0.3f * std::abs(CsNoise(RollNoise));
				}
				RollSvf.Tick(CsNoise(RollNoise), G, 0.8f);
				MixBuf[i] += (0.9f * RollSvf.Low + 0.5f * RollKnock.Tick(Excite)) * RollLevel;
			}
		}

		/** Enjambre: chisporroteo de saltitos (muchos clics agudos al azar) y un siseo muy suave. */
		void RenderSwarm(float* MixBuf, int32 Count)
		{
			const float Dt = InvRate;
			const float ClickRate = 35.f + 70.f * FMath::Min(1.f, SwarmLevel);
			const float G = CsSvfCoef(5200.f, Rate);
			for (int32 i = 0; i < Count; ++i)
			{
				SwarmClock += ClickRate * Dt;
				float Excite = 0.f;
				if (SwarmClock >= 1.f)
				{
					SwarmClock -= 1.f + 0.8f * CsNoise(SwarmNoise);
					Excite = 0.3f + 0.7f * std::abs(CsNoise(SwarmNoise));
				}
				SwarmHiss.Tick(CsNoise(SwarmNoise), G, 0.7f);
				MixBuf[i] += (0.55f * SwarmResA.Tick(Excite) + 0.35f * SwarmResB.Tick(Excite) + 0.05f * SwarmHiss.Band) * SwarmLevel;
			}
		}

		/** Motor eléctrico de juguete: zumbido de pulsos por un paso banda, escobillas y el silbido de los engranajes. */
		void RenderMotor(float* MixBuf, int32 Count)
		{
			const float Dt = InvRate;
			const float Freq = 110.f + 260.f * MotorRpm;
			const float G = CsSvfCoef(900.f + 1900.f * MotorRpm, Rate);
			const float GearGain = 0.05f + 0.08f * MotorRpm;
			for (int32 i = 0; i < Count; ++i)
			{
				const float Ph = CsAdvance(MotorPhase, Freq, Dt);
				const float Gear = std::sin(CsTwoPi * CsAdvance(GearPhase, Freq * 4.1f, Dt));
				const float Pulse = Ph < 0.35f ? 1.f : -0.54f;
				MotorSvf.Tick(Pulse + 0.25f * CsNoise(MotorNoise), G, 0.45f);
				MixBuf[i] += (0.35f * MotorSvf.Band + 0.12f * MotorSvf.Low + GearGain * Gear) * MotorLevel;
			}
		}
	};

	/** Generador del hilo de render de audio: solo C++ puro y el estado compartido. */
	class FCritterGenerator final : public ISoundGenerator
	{
	public:
		FCritterGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<FCritterShared, ESPMode::ThreadSafe>& InShared)
			: SharedState(InShared)
			, OutChannels(FMath::Max(1, InNumChannels))
		{
			Core.Init(InSampleRate);
		}

		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			const int32 Frames = NumSamples / OutChannels;
			if (SharedState.IsValid())
			{
				Core.Render(OutAudio, Frames, OutChannels, *SharedState);
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
		TSharedPtr<FCritterShared, ESPMode::ThreadSafe> SharedState;
		int32 OutChannels = 1;
		FCritterCore Core;
	};
}

static_assert(static_cast<uint8>(ETNBeachCritterSfx::ShellPop) == TNBeachCritterDSP::KindShellPop, "ETNBeachCritterSfx y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNBeachCritterSfx::Splash) == TNBeachCritterDSP::KindSplash, "ETNBeachCritterSfx y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNBeachCritterSfx::FoamPop) == TNBeachCritterDSP::KindFoamPop, "ETNBeachCritterSfx y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNBeachCritterSfx::Servo) == TNBeachCritterDSP::KindServo, "ETNBeachCritterSfx y el motor DSP deben coincidir");

// ─────────────────────────────────────────────────────────────────────────────
// UTN_BeachCritterSynthComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_BeachCritterSynthComponent::UTN_BeachCritterSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// El tick solo vigila el silencio y la distancia al oyente, cuatro veces por segundo.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.25f;
	bAutoActivate = false;
	NumChannels = 1;
	Shared = MakeShared<TNBeachCritterDSP::FCritterShared, ESPMode::ThreadSafe>();
}

void UTN_BeachCritterSynthComponent::OnRegister()
{
	// Antes de que el padre cree el componente de audio.
	ConfigureSpatial();
	Super::OnRegister();
}

void UTN_BeachCritterSynthComponent::ConfigureSpatial()
{
	NumChannels = 1;
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	// Todo va a 28 veces su tamaño: radios grandes, caída natural y agudos que se apagan con la distancia.
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

bool UTN_BeachCritterSynthComponent::Init(int32& /*SampleRate*/)
{
	NumChannels = 1;
	return true;
}

ISoundGeneratorPtr UTN_BeachCritterSynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<TNBeachCritterDSP::FCritterGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, Shared);
}

bool UTN_BeachCritterSynthComponent::IsListenerNear() const
{
	const FAudioDevice* Device = GetAudioDevice();
	if (!Device)
	{
		return false;
	}
	return Device->GetDistanceToNearestListener(GetComponentLocation()) < InnerRadius + FalloffDistance + 500.f;
}

void UTN_BeachCritterSynthComponent::EnsurePlaying()
{
	if (!IsPlaying())
	{
		// Las burbujas y demás fondos ceden la voz antes que los bichos que hacen algo.
		TNAudioVoices::Apply(*this, bAmbientBed ? TNAudioVoices::ERank::Background : TNAudioVoices::ERank::World);
		Start();
	}
	SetComponentTickEnabled(true);
}

void UTN_BeachCritterSynthComponent::KeepAliveFor(float Seconds)
{
	Shared->Master.store(FMath::Clamp(Loudness, 0.f, 2.f), std::memory_order_relaxed);
	EnsurePlaying();
	SilenceLeft = FMath::Max(SilenceLeft, Seconds);
}

void UTN_BeachCritterSynthComponent::Play(ETNBeachCritterSfx Sound, float Pitch, float Volume)
{
	if (!Shared.IsValid() || Volume <= 0.f || !IsListenerNear())
	{
		return;
	}
	TNBeachCritterDSP::FCritterEvent Shot;
	Shot.Kind = static_cast<uint8>(Sound);
	Shot.Pitch = Pitch;
	Shot.Gain = Volume;
	Shared->Push(Shot);
	// La cola más larga (los bolos) dura 1,1 s: con 3 s de margen no se corta nada.
	KeepAliveFor(3.f);
}

void UTN_BeachCritterSynthComponent::SetRoll(float Level, float Speed)
{
	if (!Shared.IsValid())
	{
		return;
	}
	RollLevel = FMath::Clamp(Level, 0.f, 1.5f);
	Shared->RollLevel.store(RollLevel, std::memory_order_relaxed);
	Shared->RollSpeed.store(FMath::Clamp(Speed, 0.f, 1.f), std::memory_order_relaxed);
	if (RollLevel > 0.001f && IsListenerNear())
	{
		KeepAliveFor(2.f);
	}
}

void UTN_BeachCritterSynthComponent::SetSwarm(float Level)
{
	if (!Shared.IsValid())
	{
		return;
	}
	SwarmLevel = FMath::Clamp(Level, 0.f, 1.5f);
	Shared->SwarmLevel.store(SwarmLevel, std::memory_order_relaxed);
	if (SwarmLevel > 0.001f && IsListenerNear())
	{
		KeepAliveFor(2.f);
	}
}

void UTN_BeachCritterSynthComponent::SetMotor(float Level, float Rpm)
{
	if (!Shared.IsValid())
	{
		return;
	}
	MotorLevel = FMath::Clamp(Level, 0.f, 1.5f);
	Shared->MotorLevel.store(MotorLevel, std::memory_order_relaxed);
	Shared->MotorRpm.store(FMath::Clamp(Rpm, 0.f, 1.f), std::memory_order_relaxed);
	if (MotorLevel > 0.001f && IsListenerNear())
	{
		KeepAliveFor(2.f);
	}
}

void UTN_BeachCritterSynthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const bool bContinuous = RollLevel > 0.001f || SwarmLevel > 0.001f || MotorLevel > 0.001f;
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

UTN_BeachCritterSynthComponent* UTN_BeachCritterSynthComponent::AttachTo(AActor* InOwner, USceneComponent* InParent, float InInnerRadius, float InFalloff)
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
	UTN_BeachCritterSynthComponent* Comp = NewObject<UTN_BeachCritterSynthComponent>(InOwner, NAME_None, RF_Transient | RF_DuplicateTransient);
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
