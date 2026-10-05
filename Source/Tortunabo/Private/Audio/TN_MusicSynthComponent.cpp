#include "Audio/TN_MusicSynthComponent.h"
#include "Audio/TN_AudioVoices.h"
#include "TN_MusicSynthDSP.h"
#include "AudioDevice.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundGenerator.h"

// ETNMusicTrack (reflejado) y TNMusic::ETrack (motor DSP) van en el mismo orden.
static_assert(static_cast<uint8>(ETNMusicTrack::None) == TNMusic::ETrack::None, "ETNMusicTrack y TNMusic::ETrack deben coincidir");
static_assert(static_cast<uint8>(ETNMusicTrack::Shop) == TNMusic::ETrack::Shop, "ETNMusicTrack y TNMusic::ETrack deben coincidir");
static_assert(static_cast<uint8>(ETNMusicTrack::Booth) == TNMusic::ETrack::Booth, "ETNMusicTrack y TNMusic::ETrack deben coincidir");
static_assert(static_cast<uint8>(ETNMusicTrack::Victory) == TNMusic::ETrack::Victory, "ETNMusicTrack y TNMusic::ETrack deben coincidir");
static_assert(static_cast<uint8>(ETNMusicTrack::Defeat) == TNMusic::ETrack::Defeat, "ETNMusicTrack y TNMusic::ETrack deben coincidir");
static_assert(static_cast<uint8>(ETNMusicTrack::Eliminated) == TNMusic::ETrack::Eliminated, "ETNMusicTrack y TNMusic::ETrack deben coincidir");
static_assert(static_cast<uint8>(ETNMusicTrack::Eliminated) + 1 == TNMusic::ETrack::Count, "ETNMusicTrack y TNMusic::ETrack deben coincidir");

namespace
{
	/**
	 * Generador del hilo de render de audio: solo C++ puro (TNMusic::FMusicEngine) y los parámetros atómicos, que
	 * comparte con el componente por un puntero compartido: si el componente se destruye mientras suena, no queda
	 * nada colgando.
	 */
	class FTNMusicSynthGenerator final : public ISoundGenerator
	{
	public:
		FTNMusicSynthGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<TNMusic::FMusicSharedParams, ESPMode::ThreadSafe>& InParams)
			: Params(InParams)
			, OutChannels(FMath::Max(1, InNumChannels))
		{
			DspEngine.Init(InSampleRate);
		}

		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			// Estéreo intercalado (música 2D) o mono (fuente 3D).
			const int32 Frames = NumSamples / OutChannels;
			DspEngine.Render(OutAudio, Frames, OutChannels, *Params);
			for (int32 i = Frames * OutChannels; i < NumSamples; ++i)
			{
				OutAudio[i] = 0.f;
			}
			return NumSamples;
		}

	private:
		TSharedPtr<TNMusic::FMusicSharedParams, ESPMode::ThreadSafe> Params;
		int32 OutChannels = 2;
		TNMusic::FMusicEngine DspEngine;
	};
}

// ─────────────────────────────────────────────────────────────────────────────
// UTN_MusicSynthComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_MusicSynthComponent::UTN_MusicSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// El tick solo lo usa la variante 3D (arrancar y parar según la distancia), dos veces por segundo.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.5f;
	bAutoActivate = false;
	NumChannels = 2;
	SharedParams = MakeShared<TNMusic::FMusicSharedParams, ESPMode::ThreadSafe>();
}

void UTN_MusicSynthComponent::OnRegister()
{
	// Antes de que el padre cree el componente de audio y, si se autoactiva, arranque.
	ConfigureChannels();
	Super::OnRegister();
	// La música del jugador (2D) nunca se queda sin voz; la radio 3D de la tienda compite con el resto del mundo (#737).
	TNAudioVoices::Apply(*this, bSpatial ? TNAudioVoices::ERank::World : TNAudioVoices::ERank::Reserved);
}

void UTN_MusicSynthComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bSpatial && bCullByDistance)
	{
		SetComponentTickEnabled(true);
		UpdateDistanceCulling();
	}
	else if (!IsPlaying())
	{
		Start();
	}
}

void UTN_MusicSynthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateDistanceCulling();
}

void UTN_MusicSynthComponent::ConfigureChannels()
{
	NumChannels = bSpatial ? 1 : 2;
	bAllowSpatialization = bSpatial;
	bOverrideAttenuation = bSpatial;
	if (bSpatial)
	{
		// Atenuación hecha en código, igual que las fuentes puntuales de UTN_AmbientSynthComponent: volumen pleno
		// dentro de InnerRadius, caída natural hasta FalloffDistance más allá y agudos que se apagan con la distancia.
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
}

bool UTN_MusicSynthComponent::Init(int32& /*SampleRate*/)
{
	// Se llama en el hilo de juego al arrancar (Start -> Initialize), antes de crear el generador.
	NumChannels = bSpatial ? 1 : 2;
	return true;
}

ISoundGeneratorPtr UTN_MusicSynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<FTNMusicSynthGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, SharedParams);
}

void UTN_MusicSynthComponent::UpdateDistanceCulling()
{
	if (!bSpatial || !bCullByDistance) { return; }
	FAudioDevice* Device = GetAudioDevice();
	if (!Device) { return; }
	const float Dist = Device->GetDistanceToNearestListener(GetComponentLocation());
	const float Reach = InnerRadius + FalloffDistance;
	// Con margen a los dos lados para no arrancar y parar en el borde.
	if (!IsActive() && Dist < Reach + 1000.f)
	{
		Start();
	}
	else if (IsActive() && Dist > Reach + 2500.f)
	{
		Stop();
	}
}

void UTN_MusicSynthComponent::PlayTrack(ETNMusicTrack InTrack, float InFadeSeconds)
{
	if (!SharedParams.IsValid()) { return; }
	const uint8 Requested = static_cast<uint8>(InTrack);
	const uint32 Serial = SharedParams->RequestSerial.load(std::memory_order_relaxed);
	bool bNewRequest = true;
	if (SharedParams->RequestedTrack.load(std::memory_order_relaxed) == Requested)
	{
		// Ya es la pista sonando o a la que se está llegando: no la reinicia (tampoco un jingle que acaba en bucle).
		// Un jingle sin bucle que ya ha sonado entero sí se vuelve a tocar.
		const bool bFinishedJingle = TNMusic::MusicIsOneShotTrack(Requested)
			&& SharedParams->FinishedSerial.load(std::memory_order_acquire) == Serial;
		bNewRequest = bFinishedJingle;
	}
	if (bNewRequest)
	{
		// El número de petición se publica el último (liberación): el hilo de audio ve la pista y el fundido nuevos.
		TNMusic::FMusicSharedParams::Set(SharedParams->FadeSeconds, FMath::Max(0.02f, InFadeSeconds));
		SharedParams->RequestedTrack.store(Requested, std::memory_order_relaxed);
		SharedParams->RequestSerial.store(Serial + 1u, std::memory_order_release);
	}
	// Música 2D parada con algo que tocar: tras un viaje sin cortes (seamless) el componente llega al mundo nuevo
	// registrado pero parado (el motor lo para al cambiar de nivel y BeginPlay no se repite), así que se vuelve a
	// arrancar aquí; el generador nuevo empieza por la última petición.
	if (!bSpatial && InTrack != ETNMusicTrack::None && IsRegistered() && !IsPlaying())
	{
		Start();
	}
}

void UTN_MusicSynthComponent::StopMusic(float InFadeSeconds)
{
	PlayTrack(ETNMusicTrack::None, InFadeSeconds);
}

void UTN_MusicSynthComponent::SetMusicVolume(float InVolume)
{
	RequestedVolume = FMath::Clamp(InVolume, 0.f, 1.5f);
	if (SharedParams.IsValid())
	{
		TNMusic::FMusicSharedParams::Set(SharedParams->Volume, RequestedVolume);
	}
}

ETNMusicTrack UTN_MusicSynthComponent::GetRequestedTrack() const
{
	return SharedParams.IsValid() ? static_cast<ETNMusicTrack>(SharedParams->RequestedTrack.load(std::memory_order_relaxed)) : ETNMusicTrack::None;
}

UTN_MusicSynthComponent* UTN_MusicSynthComponent::AttachMusic2D(AActor* InOwner)
{
	if (!InOwner) { return nullptr; }
	const UWorld* OwnerWorld = InOwner->GetWorld();
	if (!OwnerWorld || !OwnerWorld->IsGameWorld() || OwnerWorld->GetNetMode() == NM_DedicatedServer || !FApp::CanEverRenderAudio())
	{
		return nullptr;
	}

	UTN_MusicSynthComponent* Comp = NewObject<UTN_MusicSynthComponent>(InOwner, NAME_None, RF_Transient);
	Comp->bSpatial = false;
	if (USceneComponent* RootComp = InOwner->GetRootComponent())
	{
		Comp->SetupAttachment(RootComp);
	}
	Comp->RegisterComponent();
	InOwner->AddInstanceComponent(Comp);
	if (!Comp->IsPlaying())
	{
		Comp->Start();
	}
	return Comp;
}

UTN_MusicSynthComponent* UTN_MusicSynthComponent::AttachMusic3D(AActor* InOwner, const FVector& InWorldLocation, ETNMusicTrack InTrack,
	float InVolume, float InInnerRadius, float InFalloff)
{
	if (!InOwner) { return nullptr; }
	const UWorld* OwnerWorld = InOwner->GetWorld();
	if (!OwnerWorld || !OwnerWorld->IsGameWorld() || OwnerWorld->GetNetMode() == NM_DedicatedServer || !FApp::CanEverRenderAudio())
	{
		return nullptr;
	}

	UTN_MusicSynthComponent* Comp = NewObject<UTN_MusicSynthComponent>(InOwner, NAME_None, RF_Transient);
	Comp->bSpatial = true;
	Comp->InnerRadius = FMath::Max(0.f, InInnerRadius);
	Comp->FalloffDistance = FMath::Max(100.f, InFalloff);
	Comp->bCullByDistance = true;
	if (USceneComponent* RootComp = InOwner->GetRootComponent())
	{
		Comp->SetupAttachment(RootComp);
		// Colocado antes de registrar: la distancia al oyente ya sale del sitio bueno desde el primer bloque.
		Comp->SetRelativeLocation(RootComp->GetComponentTransform().InverseTransformPosition(InWorldLocation));
	}
	Comp->RegisterComponent();
	if (!Comp->GetAttachParent())
	{
		Comp->SetWorldLocation(InWorldLocation);
	}
	InOwner->AddInstanceComponent(Comp);
	Comp->SetComponentTickEnabled(true);
	Comp->SetMusicVolume(InVolume);
	// Fundido corto en vez de arrancar a todo volumen de golpe.
	Comp->PlayTrack(InTrack, 1.0f);
	Comp->UpdateDistanceCulling();
	return Comp;
}
