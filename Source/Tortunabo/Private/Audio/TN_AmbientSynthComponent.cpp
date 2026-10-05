#include "Audio/TN_AmbientSynthComponent.h"
#include "Audio/TN_AudioVoices.h"
#include "TN_AmbientSynthDSP.h"
#include "AudioDevice.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundGenerator.h"

// Los enums reflejados y las constantes del motor DSP van en el mismo orden.
static_assert(static_cast<int32>(ETNAmbientLayer::Count) == TNAmbientDSP::Layer::Count, "ETNAmbientLayer y TNAmbientDSP::Layer deben coincidir");
static_assert(static_cast<int32>(ETNAmbientLayer::Surf) == TNAmbientDSP::Layer::Surf, "ETNAmbientLayer y TNAmbientDSP::Layer deben coincidir");
static_assert(static_cast<int32>(ETNAmbientLayer::Bells) == TNAmbientDSP::Layer::Bells, "ETNAmbientLayer y TNAmbientDSP::Layer deben coincidir");
static_assert(static_cast<int32>(ETNAmbientBird::Count) == TNAmbientDSP::Bird::Count, "ETNAmbientBird y TNAmbientDSP::Bird deben coincidir");
static_assert(static_cast<int32>(ETNAmbientBird::Hen) == TNAmbientDSP::Bird::Hen, "ETNAmbientBird y TNAmbientDSP::Bird deben coincidir");
static_assert(static_cast<int32>(ETNAmbientSourceKind::Waterfall) == TNAmbientDSP::Kind::Waterfall, "ETNAmbientSourceKind y TNAmbientDSP::Kind deben coincidir");
static_assert(static_cast<int32>(ETNAmbientSourceKind::LavaPool) == TNAmbientDSP::Kind::LavaPool, "ETNAmbientSourceKind y TNAmbientDSP::Kind deben coincidir");
static_assert(static_cast<int32>(ETNProcBiome::Count) == TNAmbientDSP::Preset::Generic, "Un preajuste por bioma y el genérico detrás");
static_assert(static_cast<int32>(ETNProcBiome::Human) == TNAmbientDSP::Preset::Human, "Los preajustes siguen el orden de ETNProcBiome");

namespace
{
	/**
	 * Generador del hilo de render de audio: solo C++ puro (TNAmbientDSP::FEngine) y los parámetros atómicos, que
	 * comparte con el componente por un puntero compartido: si el componente se destruye mientras suena, no queda nada
	 * colgando.
	 */
	class FTNAmbientSynthGenerator final : public ISoundGenerator
	{
	public:
		FTNAmbientSynthGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<TNAmbientDSP::FSharedParams, ESPMode::ThreadSafe>& InParams)
			: Params(InParams)
			, OutChannels(FMath::Max(1, InNumChannels))
		{
			DspEngine.Init(InSampleRate, Params->SourceKind.load(std::memory_order_relaxed), Params->Seed.load(std::memory_order_relaxed));
		}

		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			// Estéreo intercalado (paisaje 2D) o mono (fuentes 3D).
			const int32 Frames = NumSamples / OutChannels;
			DspEngine.Render(OutAudio, Frames, OutChannels, *Params);
			for (int32 i = Frames * OutChannels; i < NumSamples; ++i)
			{
				OutAudio[i] = 0.f;
			}
			return NumSamples;
		}

	private:
		TSharedPtr<TNAmbientDSP::FSharedParams, ESPMode::ThreadSafe> Params;
		int32 OutChannels = 2;
		TNAmbientDSP::FEngine DspEngine;
	};

	void TNAmbSynthPresetToMix(const TNAmbientDSP::FBiomePreset& Src, FTNAmbientMix& Out)
	{
		Out = FTNAmbientMix();
		for (int32 k = 0; k < FTNAmbientMix::NumLayers; ++k) { Out.Layers[k] = Src.Layers[k]; }
		for (int32 k = 0; k < FTNAmbientMix::NumBirds; ++k) { Out.Birds[k] = Src.Birds[k]; }
		Out.BirdRate = Src.BirdRate;
		Out.BirdDistance = Src.BirdDistance;
		Out.WindGust = Src.WindGust;
		Out.WindWhistle = Src.WindWhistle;
		Out.WindBright = Src.WindBright;
		Out.SurfSize = Src.SurfSize;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// FTNAmbientMix
// ─────────────────────────────────────────────────────────────────────────────

FTNAmbientMix FTNAmbientMix::Zero()
{
	FTNAmbientMix Out;
	Out.BirdDistance = 0.f;
	Out.WindGust = 0.f;
	Out.WindBright = 0.f;
	Out.SurfSize = 0.f;
	return Out;
}

void FTNAmbientMix::AddWeighted(const FTNAmbientMix& Other, float Weight)
{
	for (int32 k = 0; k < NumLayers; ++k) { Layers[k] += Other.Layers[k] * Weight; }
	for (int32 k = 0; k < NumBirds; ++k) { Birds[k] += Other.Birds[k] * Weight; }
	BirdRate += Other.BirdRate * Weight;
	BirdDistance += Other.BirdDistance * Weight;
	WindGust += Other.WindGust * Weight;
	WindWhistle += Other.WindWhistle * Weight;
	WindBright += Other.WindBright * Weight;
	SurfSize += Other.SurfSize * Weight;
	Storm += Other.Storm * Weight;
	Enclosure += Other.Enclosure * Weight;
}

void FTNAmbientMix::Scale(float Factor)
{
	for (float& Value : Layers) { Value *= Factor; }
	for (float& Value : Birds) { Value *= Factor; }
	BirdRate *= Factor;
	BirdDistance *= Factor;
	WindGust *= Factor;
	WindWhistle *= Factor;
	WindBright *= Factor;
	SurfSize *= Factor;
	Storm *= Factor;
	Enclosure *= Factor;
}

// ─────────────────────────────────────────────────────────────────────────────
// UTN_AmbientSynthComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_AmbientSynthComponent::UTN_AmbientSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// El tick solo lo usan las fuentes 3D (arrancar y parar según la distancia), dos veces por segundo.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.5f;
	bAutoActivate = false;
	NumChannels = 2;
	SharedParams = MakeShared<TNAmbientDSP::FSharedParams, ESPMode::ThreadSafe>();
}

void UTN_AmbientSynthComponent::OnRegister()
{
	// Antes de que el padre cree el componente de audio y, si se autoactiva, arranque.
	ConfigureForKind();
	Super::OnRegister();
	// El paisaje sonoro del jugador (2D, uno) nunca se queda sin voz; las fuentes 3D (cascadas, géiseres, lava) son fondo
	// y ceden la voz antes que nada (#737).
	TNAudioVoices::Apply(*this, SourceKind == ETNAmbientSourceKind::Soundscape ? TNAudioVoices::ERank::Reserved : TNAudioVoices::ERank::Background);
}

void UTN_AmbientSynthComponent::BeginPlay()
{
	Super::BeginPlay();
	if (SourceKind == ETNAmbientSourceKind::Soundscape)
	{
		if (bApplyPresetOnBeginPlay) { ApplyBiomePreset(PresetBiome); }
		return;
	}
	if (bCullByDistance)
	{
		SetComponentTickEnabled(true);
		UpdateDistanceCulling();
	}
}

void UTN_AmbientSynthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateDistanceCulling();
}

void UTN_AmbientSynthComponent::ConfigureForKind()
{
	const bool bPoint = SourceKind != ETNAmbientSourceKind::Soundscape;
	NumChannels = bPoint ? 1 : 2;
	bAllowSpatialization = bPoint;
	bOverrideAttenuation = bPoint;
	if (bPoint)
	{
		// Atenuación hecha en código: volumen pleno dentro de InnerRadius, caída natural hasta FalloffDistance más allá y
		// agudos que se apagan con la distancia. Pegado a una fuente grande deja de sonar como un punto.
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

	TNAmbientDSP::FSharedParams& P = *SharedParams;
	P.SourceKind.store(static_cast<int32>(SourceKind), std::memory_order_relaxed);
	TNAmbientDSP::FSharedParams::Set(P.Master, Loudness * MasterGain);
	if (bPoint)
	{
		TNAmbientDSP::FSharedParams::Set(P.SmoothSeconds, 0.2f);
	}
}

bool UTN_AmbientSynthComponent::Init(int32& /*SampleRate*/)
{
	// Se llama en el hilo de juego al arrancar (Start -> Initialize), antes de crear el generador: mono para 3D,
	// estéreo para el paisaje, y la semilla del azar.
	NumChannels = SourceKind == ETNAmbientSourceKind::Soundscape ? 2 : 1;
	uint32 UseSeed = static_cast<uint32>(Seed);
	if (UseSeed == 0u)
	{
		// Sin semilla: la de la posición ya colocada (cada cascada suena distinta, pero igual cada partida).
		UseSeed = HashCombine(GetTypeHash(GetComponentLocation()), static_cast<uint32>(SourceKind) + 0x51u);
	}
	SharedParams->SourceKind.store(static_cast<int32>(SourceKind), std::memory_order_relaxed);
	SharedParams->Seed.store(UseSeed != 0u ? UseSeed : 1u, std::memory_order_relaxed);
	return true;
}

ISoundGeneratorPtr UTN_AmbientSynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<FTNAmbientSynthGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, SharedParams);
}

void UTN_AmbientSynthComponent::UpdateDistanceCulling()
{
	if (SourceKind == ETNAmbientSourceKind::Soundscape || !bCullByDistance) { return; }
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

UTN_AmbientSynthComponent* UTN_AmbientSynthComponent::AttachWaterSound(AActor* InOwner, float InLoudness, bool bGeyser)
{
	return CreatePointSound(InOwner, nullptr, bGeyser ? ETNAmbientSourceKind::Geyser : ETNAmbientSourceKind::Waterfall, InLoudness, 500.f, 3500.f);
}

UTN_AmbientSynthComponent* UTN_AmbientSynthComponent::AttachWaterSoundAt(AActor* InOwner, const FVector& InWorldLocation, float InLoudness, bool bGeyser)
{
	return CreatePointSound(InOwner, &InWorldLocation, bGeyser ? ETNAmbientSourceKind::Geyser : ETNAmbientSourceKind::Waterfall, InLoudness, 500.f, 3500.f);
}

UTN_AmbientSynthComponent* UTN_AmbientSynthComponent::AttachPointSound(AActor* InOwner, ETNAmbientSourceKind InKind, float InLoudness,
	float InInnerRadius, float InFalloffDistance)
{
	return CreatePointSound(InOwner, nullptr, InKind, InLoudness, InInnerRadius, InFalloffDistance);
}

UTN_AmbientSynthComponent* UTN_AmbientSynthComponent::AttachPointSoundAt(AActor* InOwner, const FVector& InWorldLocation, ETNAmbientSourceKind InKind,
	float InLoudness, float InInnerRadius, float InFalloffDistance)
{
	return CreatePointSound(InOwner, &InWorldLocation, InKind, InLoudness, InInnerRadius, InFalloffDistance);
}

UTN_AmbientSynthComponent* UTN_AmbientSynthComponent::CreatePointSound(AActor* InOwner, const FVector* InWorldLocation, ETNAmbientSourceKind InKind,
	float InLoudness, float InInnerRadius, float InFalloffDistance)
{
	if (!InOwner) { return nullptr; }
	const UWorld* OwnerWorld = InOwner->GetWorld();
	if (!OwnerWorld || !OwnerWorld->IsGameWorld() || OwnerWorld->GetNetMode() == NM_DedicatedServer || !FApp::CanEverRenderAudio())
	{
		return nullptr;
	}

	UTN_AmbientSynthComponent* Comp = NewObject<UTN_AmbientSynthComponent>(InOwner, NAME_None, RF_Transient);
	Comp->SourceKind = InKind == ETNAmbientSourceKind::Soundscape ? ETNAmbientSourceKind::Waterfall : InKind;
	Comp->Loudness = FMath::Max(0.f, InLoudness);
	Comp->InnerRadius = FMath::Max(0.f, InInnerRadius);
	Comp->FalloffDistance = FMath::Max(100.f, InFalloffDistance);
	Comp->bCullByDistance = true;
	if (USceneComponent* RootComp = InOwner->GetRootComponent())
	{
		Comp->SetupAttachment(RootComp);
		// Colocado antes de registrar: la distancia al oyente y la semilla ya salen del sitio bueno.
		if (InWorldLocation)
		{
			Comp->SetRelativeLocation(RootComp->GetComponentTransform().InverseTransformPosition(*InWorldLocation));
		}
	}
	Comp->RegisterComponent();
	if (InWorldLocation && !Comp->GetAttachParent())
	{
		Comp->SetWorldLocation(*InWorldLocation);
	}
	InOwner->AddInstanceComponent(Comp);
	Comp->SetComponentTickEnabled(true);
	Comp->UpdateDistanceCulling();
	return Comp;
}

void UTN_AmbientSynthComponent::GetBiomeMix(ETNProcBiome Biome, FTNAmbientMix& OutMix)
{
	const int32 Index = FMath::Clamp(static_cast<int32>(Biome), 0, TNAmbientDSP::Preset::Generic - 1);
	TNAmbSynthPresetToMix(TNAmbientDSP::GetPreset(Index), OutMix);
}

void UTN_AmbientSynthComponent::GetGenericMix(FTNAmbientMix& OutMix)
{
	TNAmbSynthPresetToMix(TNAmbientDSP::GetPreset(TNAmbientDSP::Preset::Generic), OutMix);
}

void UTN_AmbientSynthComponent::ApplyMix(const FTNAmbientMix& Mix)
{
	CurrentMix = Mix;
	TNAmbientDSP::FSharedParams& P = *SharedParams;
	for (int32 k = 0; k < FTNAmbientMix::NumLayers; ++k)
	{
		TNAmbientDSP::FSharedParams::Set(P.Layers[k], FMath::Max(0.f, Mix.Layers[k]));
	}
	for (int32 k = 0; k < FTNAmbientMix::NumBirds; ++k)
	{
		TNAmbientDSP::FSharedParams::Set(P.Birds[k], FMath::Max(0.f, Mix.Birds[k]));
	}
	TNAmbientDSP::FSharedParams::Set(P.BirdRate, FMath::Max(0.f, Mix.BirdRate));
	TNAmbientDSP::FSharedParams::Set(P.BirdDistance, FMath::Clamp(Mix.BirdDistance, 0.f, 1.f));
	TNAmbientDSP::FSharedParams::Set(P.WindGust, FMath::Clamp(Mix.WindGust, 0.f, 1.f));
	TNAmbientDSP::FSharedParams::Set(P.WindWhistle, FMath::Clamp(Mix.WindWhistle, 0.f, 1.f));
	TNAmbientDSP::FSharedParams::Set(P.WindBright, FMath::Clamp(Mix.WindBright, 0.f, 1.f));
	TNAmbientDSP::FSharedParams::Set(P.SurfSize, FMath::Clamp(Mix.SurfSize, 0.f, 1.f));
	TNAmbientDSP::FSharedParams::Set(P.Storm, FMath::Clamp(Mix.Storm, 0.f, 1.f));
	TNAmbientDSP::FSharedParams::Set(P.Enclosure, FMath::Clamp(Mix.Enclosure, 0.f, 1.f));
}

void UTN_AmbientSynthComponent::ApplyBiomePreset(ETNProcBiome Biome)
{
	FTNAmbientMix Mix;
	GetBiomeMix(Biome, Mix);
	ApplyMix(Mix);
}

void UTN_AmbientSynthComponent::SetLayerLevel(ETNAmbientLayer InLayer, float Level)
{
	const int32 Index = static_cast<int32>(InLayer);
	if (Index < 0 || Index >= FTNAmbientMix::NumLayers) { return; }
	CurrentMix.Layers[Index] = FMath::Max(0.f, Level);
	TNAmbientDSP::FSharedParams::Set(SharedParams->Layers[Index], CurrentMix.Layers[Index]);
}

float UTN_AmbientSynthComponent::GetLayerLevel(ETNAmbientLayer InLayer) const
{
	const int32 Index = static_cast<int32>(InLayer);
	return Index >= 0 && Index < FTNAmbientMix::NumLayers ? CurrentMix.Layers[Index] : 0.f;
}

void UTN_AmbientSynthComponent::SetBirdWeight(ETNAmbientBird Species, float Weight)
{
	const int32 Index = static_cast<int32>(Species);
	if (Index < 0 || Index >= FTNAmbientMix::NumBirds) { return; }
	CurrentMix.Birds[Index] = FMath::Max(0.f, Weight);
	TNAmbientDSP::FSharedParams::Set(SharedParams->Birds[Index], CurrentMix.Birds[Index]);
}

void UTN_AmbientSynthComponent::SetIntensity(float InIntensity)
{
	TNAmbientDSP::FSharedParams::Set(SharedParams->Intensity, FMath::Clamp(InIntensity, 0.f, 1.5f));
}

void UTN_AmbientSynthComponent::SetStormAmount(float Amount)
{
	CurrentMix.Storm = FMath::Clamp(Amount, 0.f, 1.f);
	TNAmbientDSP::FSharedParams::Set(SharedParams->Storm, CurrentMix.Storm);
}

void UTN_AmbientSynthComponent::SetEnclosure(float Amount)
{
	CurrentMix.Enclosure = FMath::Clamp(Amount, 0.f, 1.f);
	TNAmbientDSP::FSharedParams::Set(SharedParams->Enclosure, CurrentMix.Enclosure);
}

void UTN_AmbientSynthComponent::SetMasterGain(float Gain)
{
	MasterGain = FMath::Max(0.f, Gain);
	TNAmbientDSP::FSharedParams::Set(SharedParams->Master, Loudness * MasterGain);
}

void UTN_AmbientSynthComponent::SetSmoothingSeconds(float Seconds)
{
	TNAmbientDSP::FSharedParams::Set(SharedParams->SmoothSeconds, FMath::Clamp(Seconds, 0.05f, 30.f));
}
