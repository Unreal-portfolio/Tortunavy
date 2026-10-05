#include "World/Beach/TN_BeachMineSynth.h"
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
 * Motor de efectos de la mina. Hilos: la cola la llena el hilo de juego (Play) y la vacía el hilo de render de audio al
 * principio de cada bloque; todo lo demás (voces, filtros, osciladores) vive solo en el hilo de audio, sin asignaciones
 * ni bloqueos. Las envolventes se calculan una vez por bloque de 16 muestras.
 */
namespace TNBeachMineDSP
{
	constexpr float MinePi = 3.14159265358979323846f;
	constexpr float MineTwoPi = 6.28318530717958647692f;
	constexpr int32 MineMaxVoices = 8;
	constexpr int32 MineBlock = 16;

	/** Tipos de efecto (el orden es el de ETNBeachMineSound). */
	constexpr uint8 KindClick = 0;
	constexpr uint8 KindBeep = 1;
	constexpr uint8 KindBoom = 2;
	constexpr uint8 KindDebris = 3;
	constexpr uint8 KindRearm = 4;

	/** Un disparo: tipo, multiplicador de tono y ganancia. */
	struct FMineSfxEvent
	{
		uint8 Kind = 0;
		float Pitch = 1.f;
		float Gain = 1.f;
	};

	/** Cola circular sin bloqueos de un productor (hilo de juego) y un consumidor (hilo de audio). */
	struct FMineSfxQueue
	{
		static constexpr uint32 Capacity = 32;

		FMineSfxEvent Events[Capacity];
		std::atomic<uint32> WriteIndex{ 0 };
		std::atomic<uint32> ReadIndex{ 0 };
		std::atomic<float> Master{ 1.f };

		/** Hilo de juego. false si la cola está llena (el disparo se pierde). */
		bool Push(const FMineSfxEvent& InEvent)
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
		bool Pop(FMineSfxEvent& OutEvent)
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
	inline float MineNoise(uint32& State)
	{
		State ^= State << 13;
		State ^= State >> 17;
		State ^= State << 5;
		return static_cast<float>(State >> 8) * (1.f / 8388608.f) - 1.f;
	}

	/** Número al azar en [0, 1). */
	inline float MineUnit(uint32& State)
	{
		return 0.5f + 0.5f * MineNoise(State);
	}

	/** Coeficiente del filtro de estado variable (Chamberlin) para un corte en Hz; estable por debajo de ~1/6 de la frecuencia de muestreo. */
	inline float MineSvfCoef(float CutHz, float Rate)
	{
		return 2.f * std::sin(MinePi * FMath::Clamp(CutHz, 20.f, Rate * 0.16f) / Rate);
	}

	/** Hermite 0..1. */
	inline float MineSmooth(float X)
	{
		const float T = FMath::Clamp(X, 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	/** Filtro de estado variable de Chamberlin: devuelve el paso banda normalizado (ganancia ~1 en el corte). */
	struct FMineSvf
	{
		float Low = 0.f;
		float Band = 0.f;

		float Process(float In, float G, float Damp)
		{
			Low += G * Band;
			const float High = In - Low - Damp * Band;
			Band += G * High;
			return Band * Damp;
		}
	};

	/** Resonador de dos polos (plástico, piedrecitas) excitado por impulsos. */
	struct FMineResonator
	{
		float Y1 = 0.f;
		float Y2 = 0.f;
		float C = 0.f;
		float R2 = 0.f;
		float Norm = 1.f;

		void Tune(float FreqHz, float BandwidthHz, float Rate)
		{
			const float W = MineTwoPi * FMath::Clamp(FreqHz, 20.f, Rate * 0.45f) / Rate;
			const float R = std::exp(-MinePi * BandwidthHz / Rate);
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

	/** Una voz sonando. */
	struct FMineVoice
	{
		bool bActive = false;
		uint8 Kind = 0;
		float Age = 0.f;
		float Duration = 1.f;
		float Pitch = 1.f;
		float Gain = 1.f;
		float PhaseA = 0.f;
		float PhaseB = 0.f;
		FMineSvf Filt[2];
		FMineResonator ResA;
		FMineResonator ResB;
		/** Dos pasos bajos de un polo en serie (retumbo). */
		float NoiseLp = 0.f;
		float NoiseLp2 = 0.f;
		/** Reloj de los impulsos (crepitar, piedrecitas, trinquete). */
		float PulseClock = 0.f;
		/** Envolvente del último chasquido. */
		float Burst = 0.f;
		int32 Pulses = 0;
		uint32 NoiseState = 0x2545F491u;
	};

	/** Mezclador de voces con limitador suave al final. */
	class FMineSfxCore
	{
	public:
		void Init(float InRate)
		{
			Rate = FMath::Max(8000.f, InRate);
			InvRate = 1.f / Rate;
		}

		/** Mono repetido en todos los canales (el componente es mono y espacializado). */
		void Render(float* Out, int32 Frames, int32 Channels, FMineSfxQueue& Queue)
		{
			FMineSfxEvent Pending;
			while (Queue.Pop(Pending))
			{
				StartVoice(Pending);
			}
			bool bAnyVoice = false;
			for (const FMineVoice& Voice : Voices)
			{
				bAnyVoice |= Voice.bActive;
			}
			if (!bAnyVoice)
			{
				FMemory::Memzero(Out, sizeof(float) * Frames * Channels);
				return;
			}
			const float MasterGain = Queue.Master.load(std::memory_order_relaxed);
			for (int32 Frame = 0; Frame < Frames; Frame += MineBlock)
			{
				const int32 Count = FMath::Min(MineBlock, Frames - Frame);
				float MixBuf[MineBlock] = {};
				for (FMineVoice& Voice : Voices)
				{
					if (Voice.bActive)
					{
						RenderVoice(Voice, MixBuf, Count);
					}
				}
				for (int32 i = 0; i < Count; ++i)
				{
					// Saturación suave (aproximación racional de tanh): la explosión satura sin recortar.
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
		float Rate = 48000.f;
		float InvRate = 1.f / 48000.f;
		FMineVoice Voices[MineMaxVoices];
		uint32 SeedState = 0x9E3779B9u;

		/** Coge una voz libre (o la más vieja) y la prepara. */
		void StartVoice(const FMineSfxEvent& InEvent)
		{
			int32 Best = 0;
			float Oldest = -1.f;
			for (int32 i = 0; i < MineMaxVoices; ++i)
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
			FMineVoice& Voice = Voices[Best];
			Voice = FMineVoice();
			Voice.bActive = true;
			Voice.Kind = InEvent.Kind;
			Voice.Pitch = FMath::Clamp(InEvent.Pitch, 0.25f, 4.f);
			Voice.Gain = FMath::Clamp(InEvent.Gain, 0.f, 2.f);
			SeedState = SeedState * 1664525u + 1013904223u;
			Voice.NoiseState = SeedState | 1u;
			switch (InEvent.Kind)
			{
			case KindClick:
				Voice.Duration = 0.16f;
				Voice.ResA.Tune(2600.f * Voice.Pitch, 350.f, Rate);
				Voice.ResB.Tune(4300.f * Voice.Pitch, 600.f, Rate);
				break;
			case KindBeep: Voice.Duration = 0.055f; break;
			case KindBoom: Voice.Duration = 2.4f; break;
			case KindDebris:
				Voice.Duration = 1.9f;
				Voice.ResA.Tune(1700.f * Voice.Pitch, 420.f, Rate);
				break;
			default:
				Voice.Duration = 0.55f;
				Voice.ResA.Tune(1500.f * Voice.Pitch, 260.f, Rate);
				Voice.ResB.Tune(3100.f * Voice.Pitch, 420.f, Rate);
				break;
			}
		}

		void RenderVoice(FMineVoice& Voice, float* MixBuf, int32 Count)
		{
			const float T = Voice.Age;
			const float Dt = InvRate;
			switch (Voice.Kind)
			{
			case KindClick:
			{
				// Doble clic de plástico (la tapa se hunde y engancha la espoleta) con un golpecito grave.
				const float BurstK = std::exp(-Dt / 0.0015f);
				const float ThunkEnv = std::exp(-T / 0.03f);
				for (int32 i = 0; i < Count; ++i)
				{
					float Excite = 0.f;
					const float Tl = T + i * Dt;
					if ((Voice.Pulses == 0 && Tl >= 0.f) || (Voice.Pulses == 1 && Tl >= 0.05f))
					{
						Excite = Voice.Pulses == 0 ? 1.f : 0.8f;
						Voice.Burst = 1.f;
						++Voice.Pulses;
					}
					Voice.Burst *= BurstK;
					const float Snap = MineNoise(Voice.NoiseState) * Voice.Burst;
					const float Ring = 0.55f * Voice.ResA.Process(Snap * 0.5f + Excite * 0.4f) + 0.35f * Voice.ResB.Process(Snap * 0.5f);
					Voice.PhaseA += 190.f * Voice.Pitch * Dt;
					Voice.PhaseA -= std::floor(Voice.PhaseA);
					MixBuf[i] += (Ring + 0.5f * Snap + 0.3f * std::sin(MineTwoPi * Voice.PhaseA) * ThunkEnv) * 0.8f * Voice.Gain;
				}
				break;
			}
			case KindBeep:
			{
				// Pitido electrónico de juguete: casi cuadrado, con los bordes suaves.
				const float Env = MineSmooth(T / 0.004f) * MineSmooth((Voice.Duration - T) / 0.012f);
				const float F = 2150.f * Voice.Pitch;
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.PhaseA += F * Dt;
					Voice.PhaseA -= std::floor(Voice.PhaseA);
					const float Ph = MineTwoPi * Voice.PhaseA;
					const float Square = std::sin(Ph) + 0.33f * std::sin(3.f * Ph) + 0.2f * std::sin(5.f * Ph);
					MixBuf[i] += Square * Env * 0.3f * Voice.Gain;
				}
				break;
			}
			case KindBoom:
			{
				// Explosión: chasquido que rasga, golpe grave que cae, retumbo de ruido grave y crepitar que se apaga.
				const float CrackEnv = std::exp(-T / 0.014f);
				const float BodyF = (34.f + 110.f * std::exp(-T / 0.06f)) * Voice.Pitch;
				const float BodyEnv = MineSmooth(T / 0.003f) * std::exp(-T / 0.34f);
				const float RumbleEnv = MineSmooth(T / 0.02f) * std::exp(-T / 0.75f);
				const float LpK = 1.f - std::exp(-MineTwoPi * 220.f * Voice.Pitch * Dt);
				const float CrackleG = MineSvfCoef(1400.f * Voice.Pitch, Rate);
				const float CrackleFade = 1.f - MineSmooth(T / 1.2f);
				const float BurstK = std::exp(-Dt / 0.004f);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.PhaseA += BodyF * Dt;
					Voice.PhaseA -= std::floor(Voice.PhaseA);
					const float Nz = MineNoise(Voice.NoiseState);
					Voice.NoiseLp += LpK * (Nz - Voice.NoiseLp);
					Voice.NoiseLp2 += LpK * (Voice.NoiseLp - Voice.NoiseLp2);
					Voice.PulseClock -= Dt;
					if (Voice.PulseClock <= 0.f && T < 1.1f)
					{
						Voice.PulseClock = 0.006f + (0.01f + 0.09f * T) * MineUnit(Voice.NoiseState);
						Voice.Burst = 0.6f + 0.4f * MineUnit(Voice.NoiseState);
					}
					Voice.Burst *= BurstK;
					const float Crackle = Voice.Filt[0].Process(Nz, CrackleG, 0.5f) * 2.f * Voice.Burst * CrackleFade;
					const float Body = std::sin(MineTwoPi * Voice.PhaseA) * BodyEnv;
					const float Rumble = Voice.NoiseLp2 * 8.f * RumbleEnv;
					MixBuf[i] += (0.9f * Body + Rumble + 0.45f * Crackle + 0.8f * Nz * CrackEnv) * Voice.Gain;
				}
				break;
			}
			case KindDebris:
			{
				// Arena y piedrecitas que caen tras la explosión (empiezan a los 0,22 s): siseo que se apaga y golpecitos
				// al azar cada vez más espaciados, cada uno con su tono.
				constexpr float Start = 0.22f;
				if (T >= Start)
				{
					const float U = T - Start;
					const float HissEnv = MineSmooth(U / 0.08f) * std::exp(-U / 0.45f);
					const float Fade = 1.f - MineSmooth((U - 1.2f) / 0.4f);
					const float G = MineSvfCoef(3400.f * Voice.Pitch, Rate);
					for (int32 i = 0; i < Count; ++i)
					{
						Voice.PulseClock -= Dt;
						float Excite = 0.f;
						if (Voice.PulseClock <= 0.f && U < 1.4f)
						{
							Voice.PulseClock = 0.012f + (0.02f + 0.2f * U) * MineUnit(Voice.NoiseState);
							Excite = 0.5f + 0.5f * MineUnit(Voice.NoiseState);
							Voice.ResA.Tune((900.f + 1700.f * MineUnit(Voice.NoiseState)) * Voice.Pitch, 420.f, Rate);
						}
						const float Pebble = Voice.ResA.Process(Excite) * 0.6f;
						const float Hiss = Voice.Filt[0].Process(MineNoise(Voice.NoiseState), G, 0.4f) * 1.6f * HissEnv;
						MixBuf[i] += (Pebble + Hiss * 0.5f) * Fade * Voice.Gain;
					}
				}
				break;
			}
			default:
			{
				// Rearme: tres clics de trinquete y un roce de arena.
				const float GrindEnv = MineSmooth(T / 0.05f) * std::exp(-T / 0.2f);
				const float G = MineSvfCoef(450.f * Voice.Pitch, Rate);
				for (int32 i = 0; i < Count; ++i)
				{
					const float Tl = T + i * Dt;
					float Excite = 0.f;
					if (Voice.Pulses < 3 && Tl >= 0.07f * Voice.Pulses)
					{
						Excite = 0.9f - 0.15f * Voice.Pulses;
						++Voice.Pulses;
					}
					const float Ratchet = 0.55f * Voice.ResA.Process(Excite) + 0.35f * Voice.ResB.Process(Excite);
					const float Grind = Voice.Filt[0].Process(MineNoise(Voice.NoiseState), G, 0.35f) * 2.2f * GrindEnv;
					MixBuf[i] += (Ratchet + Grind * 0.5f) * 0.8f * Voice.Gain;
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
	};

	/** Generador del hilo de render de audio: solo C++ puro y la cola compartida. */
	class FMineSfxGenerator final : public ISoundGenerator
	{
	public:
		FMineSfxGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<FMineSfxQueue, ESPMode::ThreadSafe>& InQueue)
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
		TSharedPtr<FMineSfxQueue, ESPMode::ThreadSafe> Queue;
		int32 OutChannels = 1;
		FMineSfxCore Core;
	};
}

static_assert(static_cast<uint8>(ETNBeachMineSound::Click) == TNBeachMineDSP::KindClick, "ETNBeachMineSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNBeachMineSound::Beep) == TNBeachMineDSP::KindBeep, "ETNBeachMineSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNBeachMineSound::Boom) == TNBeachMineDSP::KindBoom, "ETNBeachMineSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNBeachMineSound::Debris) == TNBeachMineDSP::KindDebris, "ETNBeachMineSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNBeachMineSound::Rearm) == TNBeachMineDSP::KindRearm, "ETNBeachMineSound y el motor DSP deben coincidir");

// ─────────────────────────────────────────────────────────────────────────────
// UTN_BeachMineSynthComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_BeachMineSynthComponent::UTN_BeachMineSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// El tick solo cuenta el silencio para parar el sintetizador, dos veces por segundo.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.5f;
	bAutoActivate = false;
	NumChannels = 1;
	SfxQueue = MakeShared<TNBeachMineDSP::FMineSfxQueue, ESPMode::ThreadSafe>();
}

void UTN_BeachMineSynthComponent::OnRegister()
{
	// Antes de que el padre cree el componente de audio.
	ConfigureSpatial();
	Super::OnRegister();
}

void UTN_BeachMineSynthComponent::ConfigureSpatial()
{
	NumChannels = 1;
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	// Volumen pleno dentro de InnerRadius, caída natural hasta FalloffDistance más allá y agudos que se apagan lejos.
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
	Att.NonSpatializedRadiusStart = InnerRadius * 0.3f;
	Att.NonSpatializedRadiusEnd = InnerRadius * 0.1f;
}

bool UTN_BeachMineSynthComponent::Init(int32& /*SampleRate*/)
{
	NumChannels = 1;
	return true;
}

ISoundGeneratorPtr UTN_BeachMineSynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<TNBeachMineDSP::FMineSfxGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, SfxQueue);
}

bool UTN_BeachMineSynthComponent::IsListenerNear() const
{
	FAudioDevice* Device = GetAudioDevice();
	if (!Device)
	{
		return false;
	}
	return Device->GetDistanceToNearestListener(GetComponentLocation()) < InnerRadius + FalloffDistance + 300.f;
}

void UTN_BeachMineSynthComponent::Play(ETNBeachMineSound Sound, float Pitch, float Volume)
{
	if (!SfxQueue.IsValid() || Volume <= 0.f || !IsListenerNear())
	{
		return;
	}
	if (!IsPlaying())
	{
		TNAudioVoices::Apply(*this, TNAudioVoices::ERank::World);
		Start();
	}
	SfxQueue->Master.store(FMath::Clamp(Loudness, 0.f, 2.f), std::memory_order_relaxed);
	TNBeachMineDSP::FMineSfxEvent Shot;
	Shot.Kind = static_cast<uint8>(Sound);
	Shot.Pitch = Pitch;
	Shot.Gain = Volume;
	SfxQueue->Push(Shot);
	// El efecto más largo dura 2,4 s: con 4 s de margen no se corta ninguna cola.
	SilenceLeft = 4.f;
	SetComponentTickEnabled(true);
}

void UTN_BeachMineSynthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	SilenceLeft -= DeltaTime;
	if (SilenceLeft <= 0.f)
	{
		// Callado: se libera la voz del mezclador hasta el próximo disparo.
		if (IsPlaying())
		{
			Stop();
		}
		SetComponentTickEnabled(false);
	}
}

UTN_BeachMineSynthComponent* UTN_BeachMineSynthComponent::AttachTo(AActor* InOwner, const FVector& InWorldLocation, float InInnerRadius, float InFalloff)
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

	UTN_BeachMineSynthComponent* Comp = NewObject<UTN_BeachMineSynthComponent>(InOwner, NAME_None, RF_Transient);
	Comp->InnerRadius = FMath::Max(0.f, InInnerRadius);
	Comp->FalloffDistance = FMath::Max(100.f, InFalloff);
	if (USceneComponent* RootComp = InOwner->GetRootComponent())
	{
		Comp->SetupAttachment(RootComp);
		// Colocado antes de registrar: la distancia al oyente ya sale del sitio bueno desde el primer disparo.
		Comp->SetRelativeLocation(RootComp->GetComponentTransform().InverseTransformPosition(InWorldLocation));
	}
	Comp->RegisterComponent();
	if (!Comp->GetAttachParent())
	{
		Comp->SetWorldLocation(InWorldLocation);
	}
	InOwner->AddInstanceComponent(Comp);
	return Comp;
}
