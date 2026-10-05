#include "Audio/TN_ScoreShellSynthComponent.h"
#include "Audio/TN_AudioVoices.h"
#include "AudioDevice.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundGenerator.h"
#include <atomic>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Motor de sonido (hilo de render de audio)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Hilos como en UTN_SearchSynthComponent: la cola la llena el hilo de juego (TriggerSound) y la vacía el hilo de audio
 * al principio de cada bloque; las voces viven solo en el hilo de audio, sin asignaciones ni bloqueos. Las envolventes
 * se calculan una vez por bloque de 16 muestras y los senos con una parábola corregida (sin llamar a sin()).
 */
namespace TNScoreShellDSP
{
	constexpr float ShellPi = 3.14159265358979323846f;
	constexpr int32 ShellMaxVoices = 12;
	constexpr int32 ShellBlock = 16;

	/** Tipos de sonido (el orden es el de ETNScoreShellSound). */
	constexpr uint8 KindPlin = 0;
	constexpr uint8 KindPom = 1;

	/** Notas de campana y del acorde que queda como mucho por voz, y parciales de cada nota de campana. */
	constexpr int32 MaxBellNotes = 6;
	constexpr int32 MaxPadNotes = 5;
	constexpr int32 BellPartials = 4;

	/** Parciales de la campanita de cristal: frecuencia relativa, nivel y lo que dura cada uno respecto a la nota. */
	constexpr float PartialRatio[BellPartials] = { 1.f, 2.f, 3.f, 5.4f };
	constexpr float PartialLevel[BellPartials] = { 1.f, 0.32f, 0.12f, 0.08f };
	constexpr float PartialDecay[BellPartials] = { 1.f, 0.6f, 0.35f, 0.08f };

	struct FBellNote
	{
		float Hz = 1000.f;
		/** Cuándo suena (s desde el disparo), cuánto dura (constante de caída, s) y a qué volumen. */
		float Start = 0.f;
		float Decay = 0.3f;
		float Level = 1.f;
	};

	/** Un «¡plin!»: arpegio de campanitas, acorde que queda (pad) y chispitas agudas al azar. */
	struct FPlinSpec
	{
		const FBellNote* Notes = nullptr;
		int32 NumNotes = 0;
		const float* Pad = nullptr;
		int32 NumPad = 0;
		float PadStart = 0.f;
		float PadAttack = 0.1f;
		float PadDecay = 1.f;
		float PadLevel = 0.f;
		/** Hasta cuándo saltan chispitas y cuántas por segundo. */
		float SparkleUntil = 0.f;
		float SparkleRate = 0.f;
		float Duration = 1.f;
		/** Volumen de la voz entera (las de más notas suman más). */
		float Master = 0.5f;
	};

	// Pequeña: un mi6 corto con su octava de brillo.
	const FBellNote SmallNotes[] = { { 1318.51f, 0.f, 0.13f, 1.f }, { 2637.02f, 0.f, 0.05f, 0.25f } };
	// Normal: si5 y mi6, la moneda de siempre.
	const FBellNote NormalNotes[] = { { 987.77f, 0.f, 0.08f, 0.75f }, { 1318.51f, 0.075f, 0.3f, 1.f } };
	// Grande: arpegio do-mi-sol-do y el acorde de do mayor que queda.
	const FBellNote BigNotes[] = { { 1046.5f, 0.f, 0.3f, 0.75f }, { 1318.51f, 0.06f, 0.35f, 0.75f }, { 1567.98f, 0.12f, 0.45f, 0.8f },
		{ 2093.f, 0.18f, 0.75f, 0.9f } };
	const float BigPad[] = { 523.25f, 659.26f, 783.99f };
	// Reina: arpegio sol-do-mi-sol-do-mi y un acorde de do mayor con séptima y novena que se abre.
	const FBellNote GrandNotes[] = { { 783.99f, 0.f, 0.35f, 0.65f }, { 1046.5f, 0.05f, 0.4f, 0.7f }, { 1318.51f, 0.1f, 0.45f, 0.75f },
		{ 1567.98f, 0.15f, 0.55f, 0.8f }, { 2093.f, 0.2f, 0.85f, 0.9f }, { 2637.02f, 0.26f, 1.f, 0.7f } };
	const float GrandPad[] = { 523.25f, 659.26f, 783.99f, 987.77f, 1174.66f };

	inline FPlinSpec PlinSpecFor(uint8 Tier)
	{
		FPlinSpec Spec;
		switch (Tier)
		{
			case 0:
				Spec.Notes = SmallNotes;
				Spec.NumNotes = static_cast<int32>(UE_ARRAY_COUNT(SmallNotes));
				Spec.Duration = 0.45f;
				Spec.Master = 0.55f;
				break;
			case 1:
				Spec.Notes = NormalNotes;
				Spec.NumNotes = static_cast<int32>(UE_ARRAY_COUNT(NormalNotes));
				Spec.Duration = 0.9f;
				Spec.Master = 0.5f;
				break;
			case 2:
				Spec.Notes = BigNotes;
				Spec.NumNotes = static_cast<int32>(UE_ARRAY_COUNT(BigNotes));
				Spec.Pad = BigPad;
				Spec.NumPad = static_cast<int32>(UE_ARRAY_COUNT(BigPad));
				Spec.PadStart = 0.14f;
				Spec.PadAttack = 0.08f;
				Spec.PadDecay = 0.9f;
				Spec.PadLevel = 0.2f;
				Spec.SparkleUntil = 0.9f;
				Spec.SparkleRate = 22.f;
				Spec.Duration = 1.8f;
				Spec.Master = 0.42f;
				break;
			default:
				Spec.Notes = GrandNotes;
				Spec.NumNotes = static_cast<int32>(UE_ARRAY_COUNT(GrandNotes));
				Spec.Pad = GrandPad;
				Spec.NumPad = static_cast<int32>(UE_ARRAY_COUNT(GrandPad));
				Spec.PadStart = 0.22f;
				Spec.PadAttack = 0.14f;
				Spec.PadDecay = 1.5f;
				Spec.PadLevel = 0.18f;
				Spec.SparkleUntil = 1.4f;
				Spec.SparkleRate = 30.f;
				Spec.Duration = 2.6f;
				Spec.Master = 0.38f;
				break;
		}
		return Spec;
	}

	struct FShellEvent
	{
		uint8 Kind = 0;
		uint8 Tier = 0;
		float Semitones = 0.f;
		float Gain = 1.f;
	};

	/** Cola circular sin bloqueos de un productor (hilo de juego) y un consumidor (hilo de audio). */
	struct FShellShared
	{
		static constexpr uint32 Capacity = 64;

		FShellEvent Events[Capacity];
		std::atomic<uint32> WriteIndex{ 0 };
		std::atomic<uint32> ReadIndex{ 0 };
		std::atomic<float> Master{ 1.f };

		bool Push(const FShellEvent& InEvent)
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

		bool Pop(FShellEvent& OutEvent)
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
	inline float ShellNoise(uint32& Seed)
	{
		Seed ^= Seed << 13;
		Seed ^= Seed >> 17;
		Seed ^= Seed << 5;
		return static_cast<float>(Seed >> 8) * (1.f / 8388608.f) - 1.f;
	}

	/**
	 * Seno de una fase en vueltas ya en [0, 1): la parábola de Bhaskara corregida (error menor del 0,1 %) sobre
	 * x = 2π·Phase - π, cuyo seno es el opuesto del que se busca.
	 */
	inline float PolySin(float Phase)
	{
		const float X = 2.f * ShellPi * Phase - ShellPi;
		const float B = 4.f / ShellPi;
		const float C = -4.f / (ShellPi * ShellPi);
		float Y = B * X + C * X * std::fabs(X);
		Y = 0.225f * (Y * std::fabs(Y) - Y) + Y;
		return -Y;
	}

	/** Avanza una fase en vueltas y la deja en [0, 1). */
	inline void Advance(float& Phase, float Step)
	{
		Phase += Step;
		if (Phase >= 1.f) { Phase -= std::floor(Phase); }
	}

	struct FShellVoice
	{
		bool bActive = false;
		uint8 Kind = 0;
		uint8 Tier = 0;
		float Age = 0.f;
		float Duration = 1.f;
		float Gain = 1.f;
		/** Transporte de toda la voz (2^(semitonos/12)) con una pizca de desafinado al azar. */
		float Ratio = 1.f;
		float BellPhase[MaxBellNotes][BellPartials] = {};
		float PadPhase[MaxPadNotes][2] = {};
		float PomPhase = 0.f;
		float SparkPhase = 0.f;
		float SparkHz = 4000.f;
		float SparkAge = 10.f;
		uint32 NoiseState = 0x2545F491u;
	};

	/** Mezclador de voces con limitador suave al final. */
	class FShellCore
	{
	public:
		void Init(float InRate)
		{
			Rate = FMath::Max(8000.f, InRate);
			InvRate = 1.f / Rate;
		}

		/** Mono repetido en todos los canales (1 en 3D, 2 en 2D). */
		void Render(float* Out, int32 Frames, int32 Channels, FShellShared& Queue)
		{
			FShellEvent Pending;
			while (Queue.Pop(Pending))
			{
				StartVoice(Pending);
			}
			bool bAnyVoice = false;
			for (const FShellVoice& Voice : Voices)
			{
				bAnyVoice |= Voice.bActive;
			}
			if (!bAnyVoice)
			{
				FMemory::Memzero(Out, sizeof(float) * Frames * Channels);
				return;
			}
			const float MasterGain = Queue.Master.load(std::memory_order_relaxed);
			for (int32 Frame = 0; Frame < Frames; Frame += ShellBlock)
			{
				const int32 Count = FMath::Min(ShellBlock, Frames - Frame);
				float MixBuf[ShellBlock] = {};
				for (FShellVoice& Voice : Voices)
				{
					if (Voice.bActive)
					{
						RenderVoice(Voice, MixBuf, Count);
					}
				}
				for (int32 i = 0; i < Count; ++i)
				{
					// Saturación suave (aproximación racional de tanh): varias recogidas seguidas no recortan.
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
		FShellVoice Voices[ShellMaxVoices];
		uint32 SeedState = 0x3C6EF372u;

		/** Coge una voz libre (o la más vieja) y la prepara. */
		void StartVoice(const FShellEvent& InEvent)
		{
			int32 Best = 0;
			float Oldest = -1.f;
			for (int32 i = 0; i < ShellMaxVoices; ++i)
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
			FShellVoice& Voice = Voices[Best];
			Voice = FShellVoice();
			Voice.bActive = true;
			Voice.Kind = InEvent.Kind;
			Voice.Tier = static_cast<uint8>(FMath::Min<int32>(InEvent.Tier, 3));
			Voice.Gain = FMath::Clamp(InEvent.Gain, 0.f, 2.f);
			SeedState = SeedState * 1664525u + 1013904223u;
			Voice.NoiseState = SeedState | 1u;
			// Cada «¡plin!» algo distinto (±12 céntimos); el «pom» afinado, que va por la escala.
			const float Cents = InEvent.Kind == KindPlin ? 24.f * (static_cast<float>((SeedState >> 9) & 1023u) / 1023.f - 0.5f) : 0.f;
			Voice.Ratio = std::pow(2.f, (FMath::Clamp(InEvent.Semitones, -24.f, 36.f) + Cents * 0.01f) / 12.f);
			Voice.Duration = InEvent.Kind == KindPom ? 0.32f : PlinSpecFor(Voice.Tier).Duration;
		}

		void RenderVoice(FShellVoice& Voice, float* MixBuf, int32 Count)
		{
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float Nyquist = Rate * 0.45f;
			if (Voice.Kind == KindPom)
			{
				// Pom: seno de do5 (transportado) que arranca un poco más agudo y cae en 12 ms, su octava que se apaga
				// antes y un clic de ruido de 4 ms: redondo, corto y de madera.
				const float Hz = 523.25f * Voice.Ratio * (1.f + 0.9f * std::exp(-T / 0.012f));
				const float Env = FMath::Min(1.f, T / 0.002f) * std::exp(-T / 0.085f);
				const float OverEnv = 0.22f * std::exp(-T / 0.03f);
				const float ClickEnv = T < 0.004f ? 0.08f * (1.f - T / 0.004f) : 0.f;
				const float Step = FMath::Min(Hz, Nyquist) * Dt;
				for (int32 i = 0; i < Count; ++i)
				{
					Advance(Voice.PomPhase, Step);
					const float Over = Voice.PomPhase < 0.5f ? 2.f * Voice.PomPhase : 2.f * Voice.PomPhase - 1.f;
					const float Body = PolySin(Voice.PomPhase) * Env;
					MixBuf[i] += (0.85f * Body + PolySin(Over) * OverEnv + ShellNoise(Voice.NoiseState) * ClickEnv) * Voice.Gain;
				}
			}
			else
			{
				const FPlinSpec Spec = PlinSpecFor(Voice.Tier);
				float Env[MaxBellNotes][BellPartials];
				float Step[MaxBellNotes][BellPartials];
				for (int32 n = 0; n < MaxBellNotes; ++n)
				{
					for (int32 p = 0; p < BellPartials; ++p)
					{
						Env[n][p] = 0.f;
						Step[n][p] = 0.f;
						if (n >= Spec.NumNotes) { continue; }
						const FBellNote& Note = Spec.Notes[n];
						const float Hz = Note.Hz * Voice.Ratio * PartialRatio[p];
						const float Tn = T - Note.Start;
						if (Tn <= 0.f || Hz >= Nyquist) { continue; }
						Env[n][p] = Note.Level * PartialLevel[p] * FMath::Min(1.f, Tn / 0.0015f) * std::exp(-Tn / (Note.Decay * PartialDecay[p]));
						Step[n][p] = Hz * Dt;
					}
				}
				// Acorde que queda: dos osciladores algo desafinados por nota (coro) y un trémolo suave.
				const float Tp = T - Spec.PadStart;
				const float PadEnv = (Spec.NumPad > 0 && Tp > 0.f)
					? Spec.PadLevel * FMath::Min(1.f, Tp / Spec.PadAttack) * std::exp(-Tp / Spec.PadDecay) * (1.f + 0.18f * std::sin(2.f * ShellPi * 6.2f * T))
					: 0.f;
				// Chispitas: pitidos muy agudos de 30 ms que saltan al azar.
				const float SparkChance = T < Spec.SparkleUntil ? Spec.SparkleRate * Dt : 0.f;
				for (int32 i = 0; i < Count; ++i)
				{
					float Sum = 0.f;
					for (int32 n = 0; n < Spec.NumNotes; ++n)
					{
						for (int32 p = 0; p < BellPartials; ++p)
						{
							if (Env[n][p] <= 1e-5f) { continue; }
							Advance(Voice.BellPhase[n][p], Step[n][p]);
							Sum += PolySin(Voice.BellPhase[n][p]) * Env[n][p];
						}
					}
					if (PadEnv > 1e-5f)
					{
						for (int32 n = 0; n < Spec.NumPad; ++n)
						{
							const float Hz = FMath::Min(Spec.Pad[n] * Voice.Ratio, Nyquist);
							Advance(Voice.PadPhase[n][0], Hz * Dt);
							Advance(Voice.PadPhase[n][1], Hz * 1.004f * Dt);
							Sum += (PolySin(Voice.PadPhase[n][0]) + PolySin(Voice.PadPhase[n][1])) * 0.5f * PadEnv;
						}
					}
					if (SparkChance > 0.f && 0.5f * (ShellNoise(Voice.NoiseState) + 1.f) < SparkChance)
					{
						Voice.SparkAge = 0.f;
						Voice.SparkHz = 3000.f + 3500.f * 0.5f * (ShellNoise(Voice.NoiseState) + 1.f);
					}
					if (Voice.SparkAge < 0.2f)
					{
						Advance(Voice.SparkPhase, FMath::Min(Voice.SparkHz, Nyquist) * Dt);
						Sum += 0.12f * PolySin(Voice.SparkPhase) * std::exp(-Voice.SparkAge / 0.03f);
						Voice.SparkAge += Dt;
					}
					MixBuf[i] += Sum * Spec.Master * Voice.Gain;
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
	class FShellGenerator final : public ISoundGenerator
	{
	public:
		FShellGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<FShellShared, ESPMode::ThreadSafe>& InQueue)
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
		TSharedPtr<FShellShared, ESPMode::ThreadSafe> Queue;
		int32 OutChannels = 1;
		FShellCore Core;
	};
}

static_assert(static_cast<uint8>(ETNScoreShellSound::Plin) == TNScoreShellDSP::KindPlin, "ETNScoreShellSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNScoreShellSound::Pom) == TNScoreShellDSP::KindPom, "ETNScoreShellSound y el motor DSP deben coincidir");

// ─────────────────────────────────────────────────────────────────────────────
// UTN_ScoreShellSynthComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_ScoreShellSynthComponent::UTN_ScoreShellSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// El tick solo cuenta el silencio para parar el sintetizador, dos veces por segundo.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.5f;
	bAutoActivate = false;
	NumChannels = 1;
	ShellQueue = MakeShared<TNScoreShellDSP::FShellShared, ESPMode::ThreadSafe>();
}

void UTN_ScoreShellSynthComponent::OnRegister()
{
	// Antes de que el padre cree el componente de audio.
	ConfigureSpatial();
	Super::OnRegister();
}

void UTN_ScoreShellSynthComponent::ConfigureSpatial()
{
	NumChannels = bSpatial ? 1 : 2;
	bAllowSpatialization = bSpatial;
	bOverrideAttenuation = bSpatial;
	if (!bSpatial)
	{
		return;
	}
	// Como el resto de efectos del mapa: volumen pleno cerca, caída natural y agudos que se apagan con la distancia.
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

bool UTN_ScoreShellSynthComponent::Init(int32& /*SampleRate*/)
{
	NumChannels = bSpatial ? 1 : 2;
	return true;
}

ISoundGeneratorPtr UTN_ScoreShellSynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<TNScoreShellDSP::FShellGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, ShellQueue);
}

bool UTN_ScoreShellSynthComponent::IsListenerNear() const
{
	FAudioDevice* Device = GetAudioDevice();
	if (!Device)
	{
		return false;
	}
	return Device->GetDistanceToNearestListener(GetComponentLocation()) < InnerRadius + FalloffDistance + 300.f;
}

void UTN_ScoreShellSynthComponent::TriggerSound(ETNScoreShellSound Sound, uint8 Tier, float Semitones, float Volume)
{
	if (!ShellQueue.IsValid() || Volume <= 0.f || (bSpatial && !IsListenerNear()))
	{
		return;
	}
	if (!IsPlaying())
	{
		// 2D (interfaz, latido propio): voz reservada. 3D: la de la tortuga propia también; el resto, como el mundo.
		TNAudioVoices::Apply(*this, bSpatial ? TNAudioVoices::RankForOwner(GetOwner()) : TNAudioVoices::ERank::Reserved);
		Start();
	}
	ShellQueue->Master.store(FMath::Clamp(Loudness, 0.f, 2.f), std::memory_order_relaxed);
	TNScoreShellDSP::FShellEvent Shot;
	Shot.Kind = static_cast<uint8>(Sound);
	Shot.Tier = Tier;
	Shot.Semitones = Semitones;
	Shot.Gain = Volume;
	ShellQueue->Push(Shot);
	// El «¡plin!» de la reina dura 2,6 s: con 3,5 s de margen no se corta ninguna cola.
	SilenceLeft = 3.5f;
	SetComponentTickEnabled(true);
}

void UTN_ScoreShellSynthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
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

UTN_ScoreShellSynthComponent* UTN_ScoreShellSynthComponent::Attach3D(AActor* InOwner, const FVector& InWorldLocation, float InInnerRadius, float InFalloff)
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

	UTN_ScoreShellSynthComponent* Comp = NewObject<UTN_ScoreShellSynthComponent>(InOwner, NAME_None, RF_Transient);
	Comp->bSpatial = true;
	Comp->InnerRadius = FMath::Max(0.f, InInnerRadius);
	Comp->FalloffDistance = FMath::Max(100.f, InFalloff);
	if (USceneComponent* RootComp = InOwner->GetRootComponent())
	{
		Comp->SetupAttachment(RootComp);
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

UTN_ScoreShellSynthComponent* UTN_ScoreShellSynthComponent::Attach2D(AActor* InOwner)
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
	// El HUD se rehace (viajes, reaparición): el sintetizador 2D del mando se reutiliza.
	TArray<UTN_ScoreShellSynthComponent*> Existing;
	InOwner->GetComponents<UTN_ScoreShellSynthComponent>(Existing);
	for (UTN_ScoreShellSynthComponent* Found : Existing)
	{
		if (Found && !Found->bSpatial && Found->IsRegistered())
		{
			return Found;
		}
	}

	UTN_ScoreShellSynthComponent* Comp = NewObject<UTN_ScoreShellSynthComponent>(InOwner, NAME_None, RF_Transient);
	Comp->bSpatial = false;
	// Sonidos de la interfaz: también con la partida parada (el menú de pausa de la partida local para el mundo).
	Comp->bIsUISound = true;
	if (USceneComponent* RootComp = InOwner->GetRootComponent())
	{
		Comp->SetupAttachment(RootComp);
	}
	Comp->RegisterComponent();
	InOwner->AddInstanceComponent(Comp);
	return Comp;
}
