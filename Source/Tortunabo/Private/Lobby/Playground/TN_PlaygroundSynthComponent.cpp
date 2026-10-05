#include "Lobby/Playground/TN_PlaygroundSynthComponent.h"
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
 * Motor de efectos del parque de pruebas. Hilos: la cola la llena el hilo de juego (TriggerSound) y la vacía el hilo de
 * render de audio al principio de cada bloque; todo lo demás (voces, filtros, osciladores) vive solo en el hilo de audio,
 * sin asignaciones ni bloqueos. Los parámetros lentos (envolventes, tono, corte de los filtros) se calculan una vez por
 * bloque de 16 muestras (unos 0,3 ms a 48 kHz): sin escalones audibles y con muchas menos exponenciales y senos.
 */
namespace TNPlaygroundSynthDSP
{
	constexpr float SfxPi = 3.14159265358979323846f;
	constexpr float SfxTwoPi = 6.28318530717958647692f;
	constexpr int32 SfxMaxVoices = 8;
	constexpr int32 SfxBlock = 16;

	/** Tipos de efecto (el orden es el de ETNPlaygroundSound). */
	constexpr uint8 KindBoing = 0;
	constexpr uint8 KindCreak = 1;
	constexpr uint8 KindBonk = 2;
	constexpr uint8 KindWhoosh = 3;

	/** Un disparo: tipo, multiplicador de tono y ganancia. */
	struct FSfxEvent
	{
		uint8 Kind = 0;
		float Pitch = 1.f;
		float Gain = 1.f;
	};

	/** Cola circular sin bloqueos de un productor (hilo de juego) y un consumidor (hilo de audio). */
	struct FSfxShared
	{
		static constexpr uint32 Capacity = 32;

		FSfxEvent Events[Capacity];
		std::atomic<uint32> WriteIndex{ 0 };
		std::atomic<uint32> ReadIndex{ 0 };
		std::atomic<float> Master{ 1.f };

		/** Hilo de juego. false si la cola está llena (el disparo se pierde). */
		bool Push(const FSfxEvent& InEvent)
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
		bool Pop(FSfxEvent& OutEvent)
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
	inline float SfxNoise(uint32& State)
	{
		State ^= State << 13;
		State ^= State >> 17;
		State ^= State << 5;
		return static_cast<float>(State >> 8) * (1.f / 8388608.f) - 1.f;
	}

	/** Coeficiente del filtro de estado variable (Chamberlin) para un corte en Hz; estable por debajo de ~1/6 de la frecuencia de muestreo. */
	inline float SvfCoef(float CutHz, float Rate)
	{
		return 2.f * std::sin(SfxPi * FMath::Clamp(CutHz, 20.f, Rate * 0.16f) / Rate);
	}

	/** Una voz sonando. */
	struct FSfxVoice
	{
		bool bActive = false;
		uint8 Kind = 0;
		float Age = 0.f;
		float Duration = 1.f;
		float Pitch = 1.f;
		float Gain = 1.f;
		float PhaseA = 0.f;
		float PhaseB = 0.f;
		/** Filtro de estado variable: salidas paso bajo y paso banda. */
		float SvfLow = 0.f;
		float SvfBand = 0.f;
		/** Paso bajo de un polo (ruido del chapoteo). */
		float NoiseLp = 0.f;
		/** Dos resonadores de madera (crujido). */
		float ResA1 = 0.f;
		float ResA2 = 0.f;
		float ResB1 = 0.f;
		float ResB2 = 0.f;
		/** Reloj de los impulsos de fricción (crujido). */
		float PulseClock = 0.f;
		float Jitter = 0.f;
		uint32 NoiseState = 0x2545F491u;
	};

	/** Mezclador de voces con limitador suave al final. */
	class FSfxCore
	{
	public:
		void Init(float InRate)
		{
			Rate = FMath::Max(8000.f, InRate);
			InvRate = 1.f / Rate;
		}

		/** Mono repetido en todos los canales (el componente es mono y espacializado). */
		void Render(float* Out, int32 Frames, int32 Channels, FSfxShared& Queue)
		{
			FSfxEvent Pending;
			while (Queue.Pop(Pending))
			{
				StartVoice(Pending);
			}
			bool bAnyVoice = false;
			for (const FSfxVoice& Voice : Voices)
			{
				bAnyVoice |= Voice.bActive;
			}
			if (!bAnyVoice)
			{
				FMemory::Memzero(Out, sizeof(float) * Frames * Channels);
				return;
			}
			const float MasterGain = Queue.Master.load(std::memory_order_relaxed);
			for (int32 Frame = 0; Frame < Frames; Frame += SfxBlock)
			{
				const int32 Count = FMath::Min(SfxBlock, Frames - Frame);
				float MixBuf[SfxBlock] = {};
				for (FSfxVoice& Voice : Voices)
				{
					if (Voice.bActive)
					{
						RenderVoice(Voice, MixBuf, Count);
					}
				}
				for (int32 i = 0; i < Count; ++i)
				{
					// Saturación suave (aproximación racional de tanh): los golpes simultáneos no recortan.
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
		FSfxVoice Voices[SfxMaxVoices];
		uint32 SeedState = 0x9E3779B9u;

		/** Coge una voz libre (o la más vieja) y la prepara. */
		void StartVoice(const FSfxEvent& InEvent)
		{
			int32 Best = 0;
			float Oldest = -1.f;
			for (int32 i = 0; i < SfxMaxVoices; ++i)
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
			FSfxVoice& Voice = Voices[Best];
			Voice = FSfxVoice();
			Voice.bActive = true;
			Voice.Kind = InEvent.Kind;
			Voice.Pitch = FMath::Clamp(InEvent.Pitch, 0.25f, 4.f);
			Voice.Gain = FMath::Clamp(InEvent.Gain, 0.f, 2.f);
			SeedState = SeedState * 1664525u + 1013904223u;
			Voice.NoiseState = SeedState | 1u;
			Voice.Jitter = static_cast<float>((SeedState >> 9) & 1023u) / 1023.f;
			switch (InEvent.Kind)
			{
			case KindBoing: Voice.Duration = 1.25f; break;
			case KindCreak: Voice.Duration = 0.45f + 0.35f * Voice.Jitter; break;
			case KindBonk: Voice.Duration = 0.5f; break;
			default: Voice.Duration = 0.42f; break;
			}
		}

		void RenderVoice(FSfxVoice& Voice, float* MixBuf, int32 Count)
		{
			const float T = Voice.Age;
			const float Dt = InvRate;
			switch (Voice.Kind)
			{
			case KindBoing:
			{
				// Muelle cartoon: tono que entra agudo y cae, vibrato de muelle que se apaga y un filtro resonante que se
				// cierra (el «oi» de «boing»); debajo, el golpe grave de la gelatina y un chapoteo de ruido.
				const float Env = T < 0.004f ? T / 0.004f : std::exp(-(T - 0.004f) / 0.30f);
				const float Glide = 1.f + 0.4f * std::exp(-T / 0.045f);
				const float Wobble = 1.f + 0.22f * std::exp(-T / 0.28f) * std::sin(SfxTwoPi * 12.5f * T);
				const float Rise = 1.f + 0.1f * (1.f - std::exp(-T / 0.2f));
				const float Freq = 185.f * Voice.Pitch * Glide * Wobble * Rise;
				const float G = SvfCoef((650.f + 1950.f * std::exp(-T / 0.18f)) * std::sqrt(Voice.Pitch), Rate);
				const float ThumpEnv = std::exp(-T / 0.08f);
				const float ThumpFreq = 90.f * Voice.Pitch * (1.f + 0.8f * std::exp(-T / 0.02f));
				const float SquelchEnv = std::exp(-T / 0.05f);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.PhaseA += Freq * Dt;
					Voice.PhaseA -= std::floor(Voice.PhaseA);
					const float Ph = SfxTwoPi * Voice.PhaseA;
					const float Src = 0.45f * (std::sin(Ph) + 0.5f * std::sin(2.f * Ph + 0.4f) + 0.22f * std::sin(3.f * Ph + 1.1f));
					Voice.SvfLow += G * Voice.SvfBand;
					const float High = Src - Voice.SvfLow - 0.35f * Voice.SvfBand;
					Voice.SvfBand += G * High;
					const float Spring = (0.8f * Voice.SvfLow + 0.4f * Voice.SvfBand) * Env;
					Voice.PhaseB += ThumpFreq * Dt;
					Voice.PhaseB -= std::floor(Voice.PhaseB);
					const float Thump = std::sin(SfxTwoPi * Voice.PhaseB) * ThumpEnv;
					Voice.NoiseLp += 0.12f * (SfxNoise(Voice.NoiseState) - Voice.NoiseLp);
					const float Squelch = Voice.NoiseLp * SquelchEnv;
					MixBuf[i] += (0.5f * Spring + 0.45f * Thump + 0.3f * Squelch) * Voice.Gain;
				}
				break;
			}
			case KindCreak:
			{
				// Fricción a trompicones (stick-slip): impulsos irregulares de 30-55 por segundo que hacen sonar dos
				// resonadores de madera; entra rápido y se apaga en el último tercio.
				const float Fade = FMath::Clamp((Voice.Duration - T) / (0.35f * Voice.Duration), 0.f, 1.f);
				const float Env = FMath::Min(1.f, T / 0.03f) * Fade;
				const float PulseRate = (30.f + 24.f * std::sin(SfxTwoPi * 1.1f * T + 6.f * Voice.Jitter)) * Voice.Pitch;
				const float Wa = SfxTwoPi * 480.f * Voice.Pitch * Dt;
				const float Wb = SfxTwoPi * 1270.f * Voice.Pitch * Dt;
				const float Ra = std::exp(-SfxPi * 60.f * Dt);
				const float Rb = std::exp(-SfxPi * 110.f * Dt);
				const float Ca = 2.f * Ra * std::cos(Wa);
				const float Cb = 2.f * Rb * std::cos(Wb);
				// Normalización: la respuesta al impulso de un resonador de dos polos llega a 1/sin(w).
				const float Na = std::sin(Wa);
				const float Nb = std::sin(Wb);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.PulseClock += PulseRate * Dt;
					float Excite = 0.03f * SfxNoise(Voice.NoiseState);
					if (Voice.PulseClock >= 1.f)
					{
						Voice.PulseClock -= 1.f + 0.125f * SfxNoise(Voice.NoiseState);
						Excite += 1.f;
					}
					const float Ya = Excite + Ca * Voice.ResA1 - Ra * Ra * Voice.ResA2;
					Voice.ResA2 = Voice.ResA1;
					Voice.ResA1 = Ya;
					const float Yb = Excite + Cb * Voice.ResB1 - Rb * Rb * Voice.ResB2;
					Voice.ResB2 = Voice.ResB1;
					Voice.ResB1 = Yb;
					MixBuf[i] += (0.55f * Ya * Na + 0.35f * Yb * Nb) * Env * Voice.Gain;
				}
				break;
			}
			case KindBonk:
			{
				// Plástico hueco: parcial grave que cae un poco, parcial agudo corto y un clic de ruido.
				const float F1 = 380.f * Voice.Pitch * (1.f + 0.6f * std::exp(-T / 0.012f));
				const float E1 = std::exp(-T / 0.10f);
				const float F2 = 1040.f * Voice.Pitch;
				const float E2 = 0.5f * std::exp(-T / 0.035f);
				const float Ec = 0.6f * std::exp(-T / 0.004f);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.PhaseA += F1 * Dt;
					Voice.PhaseA -= std::floor(Voice.PhaseA);
					Voice.PhaseB += F2 * Dt;
					Voice.PhaseB -= std::floor(Voice.PhaseB);
					const float Hit = 0.8f * std::sin(SfxTwoPi * Voice.PhaseA) * E1 + std::sin(SfxTwoPi * Voice.PhaseB) * E2
						+ SfxNoise(Voice.NoiseState) * Ec;
					MixBuf[i] += 0.7f * Hit * Voice.Gain;
				}
				break;
			}
			default:
			{
				// Barrido: ruido por un paso banda que sube y baja con la envolvente.
				const float X = FMath::Clamp(T / FMath::Max(0.05f, Voice.Duration), 0.f, 1.f);
				const float Arc = std::sin(SfxPi * X);
				const float Env = Arc * Arc;
				const float G = SvfCoef((700.f + 1100.f * Arc) * Voice.Pitch, Rate);
				for (int32 i = 0; i < Count; ++i)
				{
					const float Noise = SfxNoise(Voice.NoiseState);
					Voice.SvfLow += G * Voice.SvfBand;
					const float High = Noise - Voice.SvfLow - 0.5f * Voice.SvfBand;
					Voice.SvfBand += G * High;
					MixBuf[i] += 0.45f * Voice.SvfBand * Env * Voice.Gain;
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
	class FSfxGenerator final : public ISoundGenerator
	{
	public:
		FSfxGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<FSfxShared, ESPMode::ThreadSafe>& InQueue)
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
		TSharedPtr<FSfxShared, ESPMode::ThreadSafe> Queue;
		int32 OutChannels = 1;
		FSfxCore Core;
	};
}

static_assert(static_cast<uint8>(ETNPlaygroundSound::Boing) == TNPlaygroundSynthDSP::KindBoing, "ETNPlaygroundSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNPlaygroundSound::Creak) == TNPlaygroundSynthDSP::KindCreak, "ETNPlaygroundSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNPlaygroundSound::Bonk) == TNPlaygroundSynthDSP::KindBonk, "ETNPlaygroundSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNPlaygroundSound::Whoosh) == TNPlaygroundSynthDSP::KindWhoosh, "ETNPlaygroundSound y el motor DSP deben coincidir");

// ─────────────────────────────────────────────────────────────────────────────
// UTN_PlaygroundSynthComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_PlaygroundSynthComponent::UTN_PlaygroundSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// El tick solo cuenta el silencio para parar el sintetizador, dos veces por segundo.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.5f;
	bAutoActivate = false;
	NumChannels = 1;
	SfxQueue = MakeShared<TNPlaygroundSynthDSP::FSfxShared, ESPMode::ThreadSafe>();
}

void UTN_PlaygroundSynthComponent::OnRegister()
{
	// Antes de que el padre cree el componente de audio.
	ConfigureSpatial();
	Super::OnRegister();
}

void UTN_PlaygroundSynthComponent::ConfigureSpatial()
{
	NumChannels = 1;
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	// Como las fuentes puntuales de UTN_AmbientSynthComponent: volumen pleno dentro de InnerRadius, caída natural hasta
	// FalloffDistance más allá y agudos que se apagan con la distancia.
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
	Att.LPFFrequencyAtMax = 3500.f;
	Att.NonSpatializedRadiusStart = InnerRadius * 0.6f;
	Att.NonSpatializedRadiusEnd = InnerRadius * 0.25f;
}

bool UTN_PlaygroundSynthComponent::Init(int32& /*SampleRate*/)
{
	// Hilo de juego, al arrancar (Start -> Initialize), antes de crear el generador.
	NumChannels = 1;
	return true;
}

ISoundGeneratorPtr UTN_PlaygroundSynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<TNPlaygroundSynthDSP::FSfxGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, SfxQueue);
}

bool UTN_PlaygroundSynthComponent::IsListenerNear() const
{
	FAudioDevice* Device = GetAudioDevice();
	if (!Device)
	{
		return false;
	}
	return Device->GetDistanceToNearestListener(GetComponentLocation()) < InnerRadius + FalloffDistance + 300.f;
}

void UTN_PlaygroundSynthComponent::TriggerSound(ETNPlaygroundSound Sound, float Pitch, float Volume)
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
	TNPlaygroundSynthDSP::FSfxEvent Shot;
	Shot.Kind = static_cast<uint8>(Sound);
	Shot.Pitch = Pitch;
	Shot.Gain = Volume;
	SfxQueue->Push(Shot);
	// El boing más largo dura 1,25 s: con 3 s de margen no se corta ninguna cola.
	SilenceLeft = 3.f;
	SetComponentTickEnabled(true);
}

void UTN_PlaygroundSynthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
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

UTN_PlaygroundSynthComponent* UTN_PlaygroundSynthComponent::AttachTo(AActor* InOwner, const FVector& InWorldLocation, float InInnerRadius, float InFalloff)
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

	UTN_PlaygroundSynthComponent* Comp = NewObject<UTN_PlaygroundSynthComponent>(InOwner, NAME_None, RF_Transient);
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
