#include "World/Beach/TN_BeachSplashSynthComponent.h"
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
 * Motor del chapuzón. Hilos: la cola la llena el hilo de juego (PlaySplashAt) y la vacía el hilo de render de audio al
 * principio de cada bloque; las voces, filtros y osciladores viven solo en el hilo de audio, sin asignaciones ni
 * bloqueos. Los parámetros lentos (envolventes, cortes de los filtros, tonos) se calculan una vez por bloque de 16
 * muestras.
 */
namespace TNBeachSplashDSP
{
	constexpr float SplashPi = 3.14159265358979323846f;
	constexpr float SplashTwoPi = 6.28318530717958647692f;
	constexpr int32 SplashMaxVoices = 4;
	constexpr int32 SplashBlock = 16;
	constexpr int32 SplashMaxBubbles = 6;

	/** Un disparo: tamaño del chapuzón, multiplicador de tono y ganancia. */
	struct FSplashEvent
	{
		float Size = 1.f;
		float Pitch = 1.f;
		float Gain = 1.f;
	};

	/** Cola circular sin bloqueos de un productor (hilo de juego) y un consumidor (hilo de audio). */
	struct FSplashQueue
	{
		static constexpr uint32 Capacity = 16;

		FSplashEvent Events[Capacity];
		std::atomic<uint32> WriteIndex{ 0 };
		std::atomic<uint32> ReadIndex{ 0 };
		std::atomic<float> Master{ 1.f };

		/** Hilo de juego. false si la cola está llena (el disparo se pierde). */
		bool Push(const FSplashEvent& InEvent)
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
		bool Pop(FSplashEvent& OutEvent)
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
	inline float SplashNoise(uint32& State)
	{
		State ^= State << 13;
		State ^= State >> 17;
		State ^= State << 5;
		return static_cast<float>(State >> 8) * (1.f / 8388608.f) - 1.f;
	}

	/** Número al azar en [0, 1). */
	inline float SplashUnit(uint32& State)
	{
		return 0.5f + 0.5f * SplashNoise(State);
	}

	/**
	 * Coeficiente del filtro de estado variable (Chamberlin) para un corte en Hz, limitado a ~1/6 de la frecuencia de
	 * muestreo (coeficiente < 0,97). Estable si el coeficiente es menor que 2 - amortiguamiento: por eso los tres filtros
	 * del chapuzón usan amortiguamientos de 1 o menos.
	 */
	inline float SplashSvfCoef(float CutHz, float Rate)
	{
		return 2.f * std::sin(SplashPi * FMath::Clamp(CutHz, 20.f, Rate * 0.16f) / Rate);
	}

	/** Hermite 0..1. */
	inline float SplashSmooth(float X)
	{
		const float T = FMath::Clamp(X, 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	/** Filtro de estado variable de Chamberlin con sus tres salidas (paso bajo, banda y alto). */
	struct FSplashSvf
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

	/** Una burbuja: seno que sube de tono mientras sube (la burbuja se encoge) y se apaga deprisa. */
	struct FSplashBubble
	{
		bool bActive = false;
		float Age = 0.f;
		float Freq = 600.f;
		/** Cuánto sube el tono (0,5 = media octava larga) en sus primeros milisegundos. */
		float Rise = 0.5f;
		float Decay = 0.04f;
		float Amp = 0.2f;
		float Phase = 0.f;
	};

	/** Un chapuzón sonando. */
	struct FSplashVoice
	{
		bool bActive = false;
		float Age = 0.f;
		float Duration = 1.4f;
		float Size = 1.f;
		float Pitch = 1.f;
		float Gain = 1.f;
		/** Chasquido del golpe, lámina de agua y lluvia de gotas (tres filtros sobre el mismo ruido). */
		FSplashSvf Slap;
		FSplashSvf Sheet;
		FSplashSvf Spray;
		float BoomPhase = 0.f;
		float BubbleClock = 0.04f;
		float RainClock = 0.f;
		float RainBurst = 0.f;
		FSplashBubble Bubbles[SplashMaxBubbles];
		uint32 NoiseState = 0x2545F491u;
	};

	/** Mezclador de voces con limitador suave al final. */
	class FSplashCore
	{
	public:
		void Init(float InRate)
		{
			SampleRate = FMath::Max(8000.f, InRate);
			InvRate = 1.f / SampleRate;
		}

		/** Mono repetido en todos los canales (el componente es mono y espacializado). */
		void Render(float* Out, int32 Frames, int32 Channels, FSplashQueue& Queue)
		{
			FSplashEvent Pending;
			while (Queue.Pop(Pending))
			{
				StartVoice(Pending);
			}
			bool bAnyVoice = false;
			for (const FSplashVoice& Voice : Voices)
			{
				bAnyVoice |= Voice.bActive;
			}
			if (!bAnyVoice)
			{
				FMemory::Memzero(Out, sizeof(float) * Frames * Channels);
				return;
			}
			const float MasterGain = Queue.Master.load(std::memory_order_relaxed);
			for (int32 Frame = 0; Frame < Frames; Frame += SplashBlock)
			{
				const int32 Count = FMath::Min(SplashBlock, Frames - Frame);
				float MixBuf[SplashBlock] = {};
				for (FSplashVoice& Voice : Voices)
				{
					if (Voice.bActive)
					{
						RenderVoice(Voice, MixBuf, Count);
					}
				}
				for (int32 i = 0; i < Count; ++i)
				{
					// Saturación suave (aproximación racional de tanh): dos chapuzones a la vez no recortan.
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
		FSplashVoice Voices[SplashMaxVoices];
		uint32 SeedState = 0x9E3779B9u;

		/** Coge una voz libre (o la más vieja) y la prepara. */
		void StartVoice(const FSplashEvent& InEvent)
		{
			int32 Best = 0;
			float Oldest = -1.f;
			for (int32 v = 0; v < SplashMaxVoices; ++v)
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
			FSplashVoice& Voice = Voices[Best];
			Voice = FSplashVoice();
			Voice.bActive = true;
			Voice.Size = FMath::Clamp(InEvent.Size, 0.3f, 1.5f);
			Voice.Pitch = FMath::Clamp(InEvent.Pitch, 0.5f, 2.f);
			// Los pequeños suenan menos (una tortuga que resbala al agua no es un salto de 15 m).
			Voice.Gain = FMath::Clamp(InEvent.Gain, 0.f, 2.f) * (0.5f + 0.5f * FMath::Min(1.2f, Voice.Size));
			Voice.Duration = 0.9f + 0.55f * Voice.Size;
			SeedState = SeedState * 1664525u + 1013904223u;
			Voice.NoiseState = SeedState | 1u;
		}

		/** Una burbuja nueva en un hueco libre (o en el de la más vieja). */
		static void SpawnBubble(FSplashVoice& Voice)
		{
			int32 Slot = 0;
			float Oldest = -1.f;
			for (int32 b = 0; b < SplashMaxBubbles; ++b)
			{
				if (!Voice.Bubbles[b].bActive)
				{
					Slot = b;
					break;
				}
				if (Voice.Bubbles[b].Age > Oldest)
				{
					Oldest = Voice.Bubbles[b].Age;
					Slot = b;
				}
			}
			FSplashBubble& Bubble = Voice.Bubbles[Slot];
			const float U = SplashUnit(Voice.NoiseState);
			const float Late = FMath::Clamp(Voice.Age / 0.8f, 0.f, 1.f);
			Bubble = FSplashBubble();
			Bubble.bActive = true;
			// Al principio, burbujas gordas y graves; luego, más pequeñas y agudas.
			Bubble.Freq = (260.f + 900.f * U * U + 520.f * Late) * Voice.Pitch / FMath::Sqrt(FMath::Max(0.3f, Voice.Size));
			Bubble.Rise = 0.35f + 0.6f * SplashUnit(Voice.NoiseState);
			Bubble.Decay = 0.02f + 0.045f * (1.f - U);
			Bubble.Amp = (0.12f + 0.2f * SplashUnit(Voice.NoiseState)) * (1.f - 0.6f * Late);
			Bubble.Phase = SplashUnit(Voice.NoiseState);
		}

		void RenderVoice(FSplashVoice& Voice, float* MixBuf, int32 Count)
		{
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float BlockSeconds = static_cast<float>(Count) * Dt;
			const float S = Voice.Size;
			// Más grande, más grave.
			const float Deep = Voice.Pitch / FMath::Sqrt(FMath::Max(0.3f, S));

			// 1) Chasquido del golpe contra el agua (primeros ~60 ms): ruido brillante que se oscurece enseguida.
			const float SlapEnv = T < 0.0015f ? T / 0.0015f : std::exp(-(T - 0.0015f) / (0.016f + 0.014f * S));
			const float SlapG = SplashSvfCoef((2800.f - 1500.f * SplashSmooth(T / 0.05f)) * Deep, SampleRate);

			// 2) Lámina de agua que se abre y vuelve a caer (hasta ~0,5 s): ruido que baja de ~1,9 kHz a ~420 Hz.
			const float SheetEnv = SplashSmooth(T / 0.01f) * std::exp(-T / (0.1f + 0.1f * S));
			const float SheetG = SplashSvfCoef((420.f + 1500.f * std::exp(-T / 0.08f)) * Deep, SampleRate);

			// 3) «Plom» de la cavidad de aire que se cierra tras la tortuga: seno grave que cae de tono.
			const float BoomT = T - 0.025f;
			const float BoomEnv = BoomT <= 0.f ? 0.f : SplashSmooth(BoomT / 0.006f) * std::exp(-BoomT / (0.06f + 0.06f * S));
			const float BoomStep = (62.f + 130.f * std::exp(-FMath::Max(0.f, BoomT) / 0.045f)) * Deep * Dt;

			// 4) Lluvia de gotas del chorro al volver a caer (desde ~0,3 s): chasquidos agudos cada vez más espaciados.
			const float RainT = T - 0.32f;
			const float RainEnv = RainT <= 0.f ? 0.f : SplashSmooth(RainT / 0.08f) * std::exp(-RainT / (0.3f + 0.2f * S));
			const float SprayG = SplashSvfCoef(4200.f * Voice.Pitch, SampleRate);
			const float RainK = std::exp(-Dt / 0.0035f);

			// 5) Burbujas que suben: durante el primer medio segundo largo (más cuanto mayor es el chapuzón).
			Voice.BubbleClock -= BlockSeconds;
			if (Voice.BubbleClock <= 0.f && T > 0.04f && T < 0.55f + 0.45f * S)
			{
				SpawnBubble(Voice);
				Voice.BubbleClock = (0.018f + 0.07f * SplashUnit(Voice.NoiseState)) * (1.f + 2.2f * T) / FMath::Max(0.5f, S);
			}
			float BubbleStep[SplashMaxBubbles];
			float BubbleAmp[SplashMaxBubbles];
			for (int32 b = 0; b < SplashMaxBubbles; ++b)
			{
				FSplashBubble& Bubble = Voice.Bubbles[b];
				BubbleStep[b] = 0.f;
				BubbleAmp[b] = 0.f;
				if (!Bubble.bActive)
				{
					continue;
				}
				const float Glide = 1.f + Bubble.Rise * (1.f - std::exp(-Bubble.Age / 0.018f));
				BubbleStep[b] = Bubble.Freq * Glide * Dt;
				BubbleAmp[b] = Bubble.Amp * SplashSmooth(Bubble.Age / 0.0015f) * std::exp(-Bubble.Age / Bubble.Decay);
				Bubble.Age += BlockSeconds;
				if (Bubble.Age > Bubble.Decay * 7.f)
				{
					Bubble.bActive = false;
				}
			}

			for (int32 i = 0; i < Count; ++i)
			{
				const float Noise = SplashNoise(Voice.NoiseState);
				float Y = 0.f;
				if (SlapEnv > 0.0005f)
				{
					Voice.Slap.Process(Noise, SlapG, 0.8f);
					Y += 2.4f * SlapEnv * Voice.Slap.Band;
				}
				Voice.Sheet.Process(Noise, SheetG, 1.f);
				Y += 1.7f * SheetEnv * Voice.Sheet.Band;
				if (BoomEnv > 0.f)
				{
					Voice.BoomPhase += BoomStep;
					Voice.BoomPhase -= std::floor(Voice.BoomPhase);
					Y += 0.75f * BoomEnv * std::sin(SplashTwoPi * Voice.BoomPhase);
				}
				if (RainEnv > 0.f)
				{
					Voice.RainClock -= Dt;
					if (Voice.RainClock <= 0.f)
					{
						Voice.RainClock = 0.004f + (0.012f + 0.05f * RainT) * SplashUnit(Voice.NoiseState);
						Voice.RainBurst = 0.4f + 0.6f * SplashUnit(Voice.NoiseState);
					}
					Voice.RainBurst *= RainK;
					Voice.Spray.Process(Noise, SprayG, 0.9f);
					Y += 0.55f * RainEnv * Voice.RainBurst * Voice.Spray.High;
				}
				for (int32 b = 0; b < SplashMaxBubbles; ++b)
				{
					if (BubbleAmp[b] > 0.0005f)
					{
						FSplashBubble& Bubble = Voice.Bubbles[b];
						Bubble.Phase += BubbleStep[b];
						Bubble.Phase -= std::floor(Bubble.Phase);
						Y += BubbleAmp[b] * std::sin(SplashTwoPi * Bubble.Phase);
					}
				}
				MixBuf[i] += Y * Voice.Gain;
			}
			Voice.Age += BlockSeconds;
			if (Voice.Age >= Voice.Duration)
			{
				Voice.bActive = false;
			}
		}
	};

	/** El generador que el motor de audio llama en su hilo. */
	class FSplashGenerator : public ISoundGenerator
	{
	public:
		FSplashGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<FSplashQueue, ESPMode::ThreadSafe>& InQueue)
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
		TSharedPtr<FSplashQueue, ESPMode::ThreadSafe> Queue;
		int32 OutChannels = 1;
		FSplashCore Core;
	};
}

// ─────────────────────────────────────────────────────────────────────────────
// UTN_BeachSplashSynthComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_BeachSplashSynthComponent::UTN_BeachSplashSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// El tick solo cuenta el silencio para parar el sintetizador, dos veces por segundo.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.5f;
	bAutoActivate = false;
	NumChannels = 1;
	SplashQueue = MakeShared<TNBeachSplashDSP::FSplashQueue, ESPMode::ThreadSafe>();
}

void UTN_BeachSplashSynthComponent::OnRegister()
{
	// Antes de que el padre cree el componente de audio.
	ConfigureSpatial();
	Super::OnRegister();
}

void UTN_BeachSplashSynthComponent::ConfigureSpatial()
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
	Att.LPFFrequencyAtMax = 3000.f;
	Att.NonSpatializedRadiusStart = InnerRadius * 0.6f;
	Att.NonSpatializedRadiusEnd = InnerRadius * 0.25f;
}

bool UTN_BeachSplashSynthComponent::Init(int32& /*SampleRate*/)
{
	NumChannels = 1;
	return true;
}

ISoundGeneratorPtr UTN_BeachSplashSynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<TNBeachSplashDSP::FSplashGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, SplashQueue);
}

bool UTN_BeachSplashSynthComponent::IsListenerNear(const FVector& WorldAt) const
{
	FAudioDevice* Device = GetAudioDevice();
	if (!Device)
	{
		return false;
	}
	return Device->GetDistanceToNearestListener(WorldAt) < InnerRadius + FalloffDistance + 300.f;
}

void UTN_BeachSplashSynthComponent::PlaySplashAt(const FVector& WorldAt, float Size, float Pitch, float Volume)
{
	if (!SplashQueue.IsValid() || Volume <= 0.f || !IsListenerNear(WorldAt))
	{
		return;
	}
	SetWorldLocation(WorldAt);
	if (!IsPlaying())
	{
		TNAudioVoices::Apply(*this, TNAudioVoices::ERank::World);
		Start();
	}
	SplashQueue->Master.store(FMath::Clamp(Loudness, 0.f, 2.f), std::memory_order_relaxed);
	TNBeachSplashDSP::FSplashEvent Shot;
	Shot.Size = Size;
	Shot.Pitch = Pitch;
	Shot.Gain = Volume;
	SplashQueue->Push(Shot);
	// El chapuzón más largo dura 1,7 s: con 3 s de margen no se corta ninguna cola.
	SilenceLeft = 3.f;
	SetComponentTickEnabled(true);
}

void UTN_BeachSplashSynthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	SilenceLeft -= DeltaTime;
	if (SilenceLeft <= 0.f)
	{
		// Callado: se libera la voz del mezclador hasta el próximo chapuzón.
		if (IsPlaying())
		{
			Stop();
		}
		SetComponentTickEnabled(false);
	}
}

UTN_BeachSplashSynthComponent* UTN_BeachSplashSynthComponent::AttachTo(AActor* InOwner, float InInnerRadius, float InFalloff)
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

	UTN_BeachSplashSynthComponent* Comp = NewObject<UTN_BeachSplashSynthComponent>(InOwner, NAME_None, RF_Transient);
	Comp->InnerRadius = FMath::Max(0.f, InInnerRadius);
	Comp->FalloffDistance = FMath::Max(100.f, InFalloff);
	if (USceneComponent* RootComp = InOwner->GetRootComponent())
	{
		Comp->SetupAttachment(RootComp);
	}
	Comp->RegisterComponent();
	InOwner->AddInstanceComponent(Comp);
	return Comp;
}
