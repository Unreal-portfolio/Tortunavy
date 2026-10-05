#include "Audio/TN_ShellImpactSynth.h"
#include "Audio/TN_AudioVoices.h"
#include "TN_ShellImpactDSP.h"
#include "AudioDevice.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundGenerator.h"

// ETNShellImpactSound (reflejado) y TNShellImpact::EKind (motor DSP) van en el mismo orden.
static_assert(static_cast<uint8>(ETNShellImpactSound::Sand) == TNShellImpact::Sand, "ETNShellImpactSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNShellImpactSound::Rock) == TNShellImpact::Rock, "ETNShellImpactSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNShellImpactSound::Wood) == TNShellImpact::Wood, "ETNShellImpactSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNShellImpactSound::Water) == TNShellImpact::Water, "ETNShellImpactSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNShellImpactSound::Turtle) == TNShellImpact::Turtle, "ETNShellImpactSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNShellImpactSound::Enemy) == TNShellImpact::Enemy, "ETNShellImpactSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNShellImpactSound::Junk) == TNShellImpact::Junk, "ETNShellImpactSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNShellImpactSound::Count) == TNShellImpact::NumKinds, "ETNShellImpactSound y el motor DSP deben coincidir");

namespace
{
	/** Generador del hilo de render de audio: solo C++ puro (TNShellImpact::FCore) y la cola compartida. */
	class FTNShellImpactGenerator final : public ISoundGenerator
	{
	public:
		FTNShellImpactGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<TNShellImpact::FQueue, ESPMode::ThreadSafe>& InQueue)
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
		TSharedPtr<TNShellImpact::FQueue, ESPMode::ThreadSafe> Queue;
		int32 OutChannels = 1;
		TNShellImpact::FCore Core;
	};
}

// ─────────────────────────────────────────────────────────────────────────────
// UTN_ShellImpactSynthComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_ShellImpactSynthComponent::UTN_ShellImpactSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// El tick solo cuenta el silencio para parar el sintetizador, dos veces por segundo.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.5f;
	bAutoActivate = false;
	NumChannels = 1;
	Queue = MakeShared<TNShellImpact::FQueue, ESPMode::ThreadSafe>();
}

void UTN_ShellImpactSynthComponent::OnRegister()
{
	// Antes de que el padre cree el componente de audio.
	ConfigureSpatial();
	Super::OnRegister();
}

void UTN_ShellImpactSynthComponent::ConfigureSpatial()
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

bool UTN_ShellImpactSynthComponent::Init(int32& /*SampleRate*/)
{
	NumChannels = 1;
	return true;
}

ISoundGeneratorPtr UTN_ShellImpactSynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<FTNShellImpactGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, Queue);
}

bool UTN_ShellImpactSynthComponent::IsListenerNear() const
{
	FAudioDevice* Device = GetAudioDevice();
	if (!Device)
	{
		return false;
	}
	return Device->GetDistanceToNearestListener(GetComponentLocation()) < InnerRadius + FalloffDistance + 300.f;
}

bool UTN_ShellImpactSynthComponent::Play(ETNShellImpactSound Sound, float Strength, float Pitch, float Volume)
{
	if (!Queue.IsValid() || Volume <= 0.f || Sound >= ETNShellImpactSound::Count || !IsListenerNear())
	{
		return false;
	}
	if (!IsPlaying())
	{
		// Los golpes de la tortuga propia tienen voz reservada; los de las demás compiten con el resto del mundo.
		TNAudioVoices::Apply(*this, TNAudioVoices::RankForOwner(GetOwner()));
		Start();
	}
	// Un disparo por cola: 0,42 deja un golpe fuerte alrededor de -8 dBFS, por encima de los pasos y por debajo de un aterrizaje duro.
	Queue->Master.store(0.42f * FMath::Clamp(Loudness, 0.f, 2.f), std::memory_order_relaxed);
	TNShellImpact::FEvent Shot;
	Shot.Kind = static_cast<uint8>(Sound);
	Shot.Strength = FMath::Clamp(Strength, 0.f, 1.f);
	Shot.Pitch = Pitch;
	Shot.Gain = Volume;
	NextSeed = NextSeed * 1664525u + 1013904223u;
	Shot.Seed = NextSeed;
	const bool bQueued = Queue->Push(Shot);
	// El golpe más largo dura medio segundo: con 2 s de margen no se corta ninguna cola.
	SilenceLeft = 2.f;
	SetComponentTickEnabled(true);
	return bQueued;
}

void UTN_ShellImpactSynthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	SilenceLeft -= DeltaTime;
	if (SilenceLeft <= 0.f)
	{
		// Callado: se libera la voz del mezclador hasta el próximo golpe.
		if (IsPlaying())
		{
			Stop();
		}
		SetComponentTickEnabled(false);
	}
}

UTN_ShellImpactSynthComponent* UTN_ShellImpactSynthComponent::AttachTo(AActor* InOwner)
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

	UTN_ShellImpactSynthComponent* Comp = NewObject<UTN_ShellImpactSynthComponent>(InOwner, NAME_None, RF_Transient);
	if (USceneComponent* RootComp = InOwner->GetRootComponent())
	{
		Comp->SetupAttachment(RootComp);
	}
	Comp->RegisterComponent();
	InOwner->AddInstanceComponent(Comp);
	return Comp;
}
