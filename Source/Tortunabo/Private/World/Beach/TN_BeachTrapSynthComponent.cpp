#include "World/Beach/TN_BeachTrapSynthComponent.h"
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
 * Motor de efectos de las trampas de la playa. Hilos: la cola la llena el hilo de juego (TriggerSound) y la vacía el hilo
 * de render de audio al principio de cada bloque; todo lo demás (voces, filtros, osciladores) vive solo en el hilo de
 * audio, sin asignaciones ni bloqueos. Los parámetros lentos (envolventes, tonos, formantes) se calculan una vez por
 * bloque de 16 muestras.
 */
namespace TNBeachTrapDSP
{
	constexpr float TrapPi = 3.14159265358979323846f;
	constexpr float TrapTwoPi = 6.28318530717958647692f;
	constexpr int32 TrapMaxVoices = 10;
	constexpr int32 TrapBlock = 16;

	/** Tipos de efecto (el orden es el de ETNBeachTrapSound). */
	constexpr uint8 KindSquelch = 0;
	constexpr uint8 KindCreak = 1;
	constexpr uint8 KindCrack = 2;
	constexpr uint8 KindThud = 3;

	/** Un disparo: tipo, multiplicador de tono y ganancia. */
	struct FTrapSfxEvent
	{
		uint8 Kind = 0;
		float Pitch = 1.f;
		float Gain = 1.f;
	};

	/** Cola circular sin bloqueos de un productor (hilo de juego) y un consumidor (hilo de audio). */
	struct FTrapSfxQueue
	{
		static constexpr uint32 Capacity = 32;

		FTrapSfxEvent Events[Capacity];
		std::atomic<uint32> WriteIndex{ 0 };
		std::atomic<uint32> ReadIndex{ 0 };
		std::atomic<float> Master{ 1.f };

		/** Hilo de juego. false si la cola está llena (el disparo se pierde). */
		bool Push(const FTrapSfxEvent& InEvent)
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
		bool Pop(FTrapSfxEvent& OutEvent)
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
	inline float TrapNoise(uint32& State)
	{
		State ^= State << 13;
		State ^= State >> 17;
		State ^= State << 5;
		return static_cast<float>(State >> 8) * (1.f / 8388608.f) - 1.f;
	}

	/** Número al azar en [0, 1). */
	inline float TrapUnit(uint32& State)
	{
		return 0.5f + 0.5f * TrapNoise(State);
	}

	/** Coeficiente del filtro de estado variable (Chamberlin) para un corte en Hz; estable por debajo de ~1/6 de la frecuencia de muestreo. */
	inline float TrapSvfCoef(float CutHz, float Rate)
	{
		return 2.f * std::sin(TrapPi * FMath::Clamp(CutHz, 20.f, Rate * 0.16f) / Rate);
	}

	/** Corrección polyBLEP del salto del diente de sierra (quita casi todo el aliasing de la voz). */
	inline float TrapPolyBlep(float T, float Dt)
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

	/** Hermite 0..1. */
	inline float TrapSmooth(float X)
	{
		const float T = FMath::Clamp(X, 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	/** Filtro de estado variable de Chamberlin: devuelve el paso banda normalizado (ganancia ~1 en el corte). */
	struct FTrapSvf
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

	/** Resonador de dos polos (madera, conchas) excitado por impulsos. */
	struct FTrapResonator
	{
		float Y1 = 0.f;
		float Y2 = 0.f;
		float C = 0.f;
		float R2 = 0.f;
		float Norm = 1.f;

		void Tune(float FreqHz, float BandwidthHz, float Rate)
		{
			const float W = TrapTwoPi * FMath::Clamp(FreqHz, 20.f, Rate * 0.45f) / Rate;
			const float R = std::exp(-TrapPi * BandwidthHz / Rate);
			C = 2.f * R * std::cos(W);
			R2 = R * R;
			// La respuesta al impulso de un resonador de dos polos llega a 1/sin(w).
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
	struct FTrapVoice
	{
		bool bActive = false;
		uint8 Kind = 0;
		float Age = 0.f;
		float Duration = 1.f;
		float Pitch = 1.f;
		float Gain = 1.f;
		float PhaseA = 0.f;
		float PhaseB = 0.f;
		FTrapSvf Filt[3];
		FTrapResonator ResA;
		FTrapResonator ResB;
		/** Paso bajo de un polo (ruido de arena y de agua). */
		float NoiseLp = 0.f;
		/** Reloj de los impulsos (chasquidos, astillas, clics). */
		float PulseClock = 0.f;
		/** Envolvente del último chasquido. */
		float Burst = 0.f;
		float Jitter = 0.f;
		int32 Pulses = 0;
		uint32 NoiseState = 0x2545F491u;
	};

	/** Mezclador de voces con limitador suave al final. */
	class FTrapSfxCore
	{
	public:
		void Init(float InRate)
		{
			Rate = FMath::Max(8000.f, InRate);
			InvRate = 1.f / Rate;
		}

		/** Mono repetido en todos los canales (el componente es mono y espacializado). */
		void Render(float* Out, int32 Frames, int32 Channels, FTrapSfxQueue& Queue)
		{
			FTrapSfxEvent Pending;
			while (Queue.Pop(Pending))
			{
				StartVoice(Pending);
			}
			bool bAnyVoice = false;
			for (const FTrapVoice& Voice : Voices)
			{
				bAnyVoice |= Voice.bActive;
			}
			if (!bAnyVoice)
			{
				FMemory::Memzero(Out, sizeof(float) * Frames * Channels);
				return;
			}
			const float MasterGain = Queue.Master.load(std::memory_order_relaxed);
			for (int32 Frame = 0; Frame < Frames; Frame += TrapBlock)
			{
				const int32 Count = FMath::Min(TrapBlock, Frames - Frame);
				float MixBuf[TrapBlock] = {};
				for (FTrapVoice& Voice : Voices)
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
		FTrapVoice Voices[TrapMaxVoices];
		uint32 SeedState = 0x9E3779B9u;

		/** Coge una voz libre (o la más vieja) y la prepara. */
		void StartVoice(const FTrapSfxEvent& InEvent)
		{
			int32 Best = 0;
			float Oldest = -1.f;
			for (int32 i = 0; i < TrapMaxVoices; ++i)
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
			FTrapVoice& Voice = Voices[Best];
			Voice = FTrapVoice();
			Voice.bActive = true;
			Voice.Kind = InEvent.Kind;
			Voice.Pitch = FMath::Clamp(InEvent.Pitch, 0.25f, 4.f);
			Voice.Gain = FMath::Clamp(InEvent.Gain, 0.f, 2.f);
			SeedState = SeedState * 1664525u + 1013904223u;
			Voice.NoiseState = SeedState | 1u;
			Voice.Jitter = static_cast<float>((SeedState >> 9) & 1023u) / 1023.f;
			switch (InEvent.Kind)
			{
			case KindSquelch: Voice.Duration = 0.34f; break;
			case KindCreak:
				Voice.Duration = 0.5f + 0.35f * Voice.Jitter;
				Voice.ResA.Tune(330.f * Voice.Pitch, 60.f, Rate);
				Voice.ResB.Tune(910.f * Voice.Pitch, 120.f, Rate);
				break;
			case KindCrack:
				Voice.Duration = 0.75f;
				Voice.ResA.Tune(220.f * Voice.Pitch, 45.f, Rate);
				Voice.ResB.Tune(690.f * Voice.Pitch, 140.f, Rate);
				break;
			case KindThud: Voice.Duration = 0.36f; break;
			default: Voice.Duration = 0.f; break;
			}
		}

		void RenderVoice(FTrapVoice& Voice, float* MixBuf, int32 Count)
		{
			const float T = Voice.Age;
			const float Dt = InvRate;
			switch (Voice.Kind)
			{
			case KindSquelch:
			{
				// Chof mojado: ruido por un paso banda que sube y baja deprisa (burbuja que gorgotea) con un «blup» grave.
				const float X = T / Voice.Duration;
				const float Env = TrapSmooth(T / 0.01f) * std::exp(-T / 0.11f);
				const float G = TrapSvfCoef((520.f + 1400.f * std::sin(TrapPi * FMath::Min(1.f, X * 1.6f))) * Voice.Pitch, Rate);
				const float Gurgle = 1.f + 0.6f * std::sin(TrapTwoPi * 38.f * T + 5.f * Voice.Jitter);
				const float BlubF = (95.f - 40.f * X) * Voice.Pitch;
				const float BlubEnv = std::exp(-T / 0.07f);
				for (int32 i = 0; i < Count; ++i)
				{
					const float Wet = Voice.Filt[0].Process(TrapNoise(Voice.NoiseState), G, 0.22f) * 3.f;
					Voice.PhaseB += BlubF * Dt;
					Voice.PhaseB -= std::floor(Voice.PhaseB);
					const float Blub = std::sin(TrapTwoPi * Voice.PhaseB) * BlubEnv;
					MixBuf[i] += (Wet * Env * Gurgle * 0.55f + Blub * 0.45f) * Voice.Gain;
				}
				break;
			}
			case KindCreak:
			{
				// Fricción a trompicones (stick-slip): impulsos irregulares que hacen sonar dos resonadores de madera.
				const float Fade = FMath::Clamp((Voice.Duration - T) / (0.35f * Voice.Duration), 0.f, 1.f);
				const float Env = FMath::Min(1.f, T / 0.03f) * Fade;
				const float PulseRate = (26.f + 20.f * std::sin(TrapTwoPi * 1.1f * T + 6.f * Voice.Jitter)) * Voice.Pitch;
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.PulseClock += PulseRate * Dt;
					float Excite = 0.03f * TrapNoise(Voice.NoiseState);
					if (Voice.PulseClock >= 1.f)
					{
						Voice.PulseClock -= 1.f + 0.125f * TrapNoise(Voice.NoiseState);
						Excite += 1.f;
					}
					MixBuf[i] += (0.55f * Voice.ResA.Process(Excite) + 0.35f * Voice.ResB.Process(Excite)) * Env * Voice.Gain;
				}
				break;
			}
			case KindCrack:
			{
				// Tabla que se parte: chasquido seco, astillas cada vez más separadas por el cuerpo de la madera y un golpe grave.
				const float Click = std::exp(-T / 0.004f);
				const float ThumpEnv = std::exp(-T / 0.09f);
				const float ThumpF = 68.f * Voice.Pitch * (1.f + 0.6f * std::exp(-T / 0.02f));
				const float Tail = FMath::Clamp((Voice.Duration - T) / 0.25f, 0.f, 1.f);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.PulseClock -= Dt;
					float Excite = 0.02f * TrapNoise(Voice.NoiseState);
					if (Voice.PulseClock <= 0.f && T < 0.45f)
					{
						Voice.PulseClock = 0.005f + (0.012f + 0.12f * T) * TrapUnit(Voice.NoiseState);
						Excite += 0.7f + 0.6f * TrapUnit(Voice.NoiseState);
					}
					const float Wood = 0.6f * Voice.ResA.Process(Excite) + 0.4f * Voice.ResB.Process(Excite);
					Voice.PhaseB += ThumpF * Dt;
					Voice.PhaseB -= std::floor(Voice.PhaseB);
					const float Snap = TrapNoise(Voice.NoiseState) * Click;
					MixBuf[i] += (Wood * Tail + 0.8f * Snap + 0.55f * std::sin(TrapTwoPi * Voice.PhaseB) * ThumpEnv) * 0.8f * Voice.Gain;
				}
				break;
			}
			case KindThud:
			{
				// Golpe sordo en la arena: grave que cae y un soplo de arena apagado.
				const float F = 72.f * Voice.Pitch * (1.f + 0.7f * std::exp(-T / 0.015f));
				const float BodyEnv = std::exp(-T / 0.09f);
				const float SandEnv = TrapSmooth(T / 0.004f) * std::exp(-T / 0.06f);
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.PhaseA += F * Dt;
					Voice.PhaseA -= std::floor(Voice.PhaseA);
					Voice.NoiseLp += 0.06f * (TrapNoise(Voice.NoiseState) - Voice.NoiseLp);
					MixBuf[i] += (0.75f * std::sin(TrapTwoPi * Voice.PhaseA) * BodyEnv + 1.6f * Voice.NoiseLp * SandEnv) * Voice.Gain;
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
	};

	/** Generador del hilo de render de audio: solo C++ puro y la cola compartida. */
	class FTrapSfxGenerator final : public ISoundGenerator
	{
	public:
		FTrapSfxGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<FTrapSfxQueue, ESPMode::ThreadSafe>& InQueue)
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
		TSharedPtr<FTrapSfxQueue, ESPMode::ThreadSafe> Queue;
		int32 OutChannels = 1;
		FTrapSfxCore Core;
	};
}

static_assert(static_cast<uint8>(ETNBeachTrapSound::Squelch) == TNBeachTrapDSP::KindSquelch, "ETNBeachTrapSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNBeachTrapSound::Creak) == TNBeachTrapDSP::KindCreak, "ETNBeachTrapSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNBeachTrapSound::Crack) == TNBeachTrapDSP::KindCrack, "ETNBeachTrapSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNBeachTrapSound::Thud) == TNBeachTrapDSP::KindThud, "ETNBeachTrapSound y el motor DSP deben coincidir");

// ─────────────────────────────────────────────────────────────────────────────
// UTN_BeachTrapSynthComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_BeachTrapSynthComponent::UTN_BeachTrapSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// El tick solo cuenta el silencio para parar el sintetizador, dos veces por segundo.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.5f;
	bAutoActivate = false;
	NumChannels = 1;
	SfxQueue = MakeShared<TNBeachTrapDSP::FTrapSfxQueue, ESPMode::ThreadSafe>();
}

void UTN_BeachTrapSynthComponent::OnRegister()
{
	// Antes de que el padre cree el componente de audio.
	ConfigureSpatial();
	Super::OnRegister();
}

void UTN_BeachTrapSynthComponent::ConfigureSpatial()
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
	Att.LPFFrequencyAtMax = 3500.f;
	Att.NonSpatializedRadiusStart = InnerRadius * 0.6f;
	Att.NonSpatializedRadiusEnd = InnerRadius * 0.25f;
}

bool UTN_BeachTrapSynthComponent::Init(int32& /*SampleRate*/)
{
	NumChannels = 1;
	return true;
}

ISoundGeneratorPtr UTN_BeachTrapSynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<TNBeachTrapDSP::FTrapSfxGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, SfxQueue);
}

bool UTN_BeachTrapSynthComponent::IsListenerNear() const
{
	FAudioDevice* Device = GetAudioDevice();
	if (!Device)
	{
		return false;
	}
	return Device->GetDistanceToNearestListener(GetComponentLocation()) < InnerRadius + FalloffDistance + 300.f;
}

void UTN_BeachTrapSynthComponent::TriggerSound(ETNBeachTrapSound Sound, float Pitch, float Volume)
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
	TNBeachTrapDSP::FTrapSfxEvent Shot;
	Shot.Kind = static_cast<uint8>(Sound);
	Shot.Pitch = Pitch;
	Shot.Gain = Volume;
	SfxQueue->Push(Shot);
	// El efecto más largo dura 0,9 s: con 3 s de margen no se corta ninguna cola.
	SilenceLeft = 3.f;
	SetComponentTickEnabled(true);
}

void UTN_BeachTrapSynthComponent::TriggerSoundAt(ETNBeachTrapSound Sound, const FVector& WorldAt, float Pitch, float Volume)
{
	SetWorldLocation(WorldAt);
	TriggerSound(Sound, Pitch, Volume);
}

void UTN_BeachTrapSynthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
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

UTN_BeachTrapSynthComponent* UTN_BeachTrapSynthComponent::AttachTo(AActor* InOwner, const FVector& InWorldLocation, float InInnerRadius, float InFalloff)
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

	UTN_BeachTrapSynthComponent* Comp = NewObject<UTN_BeachTrapSynthComponent>(InOwner, NAME_None, RF_Transient);
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
