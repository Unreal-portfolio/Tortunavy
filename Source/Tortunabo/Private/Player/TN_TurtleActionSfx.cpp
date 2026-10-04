#include "Player/TN_TurtleActionSfx.h"

#include "Audio/TN_ScoreShellSynthComponent.h"
#include "Core/TN_Log.h"
#include "Audio/TN_ShellImpactSynth.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Player/TortugaCharacter.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace TNTurtleActionSfxDetail
{
	// Recursos de Content/Audio/EffectSounds. Derribo, reanimar, latido y tótem no tienen archivo: los sintetiza
	// UTN_TurtleActionSynthComponent.
	const TCHAR* const KillPath = TEXT("/Game/Audio/EffectSounds/Kill/KillSound.KillSound");
	const TCHAR* const PickupPath = TEXT("/Game/Audio/EffectSounds/Pickup/Pickup.Pickup");
	const TCHAR* const ThrowPath = TEXT("/Game/Audio/EffectSounds/Throw/SC_Throw.SC_Throw");
	const TCHAR* const ConsumePath = TEXT("/Game/Audio/EffectSounds/Consume/Consume.Consume");

	// Tamaños de concha del arpegio (TNScoreShells::ETier): grande al reanimar, reina con el tótem.
	constexpr uint8 ReviveChimeTier = 2;
	constexpr uint8 TotemChimeTier = 3;

	// El latido es el «pom» del contador (do5) dos octavas abajo; el segundo golpe, más agudo y flojo.
	constexpr float HeartLubSemitones = -24.f;
	constexpr float HeartDubSemitones = -21.f;
	constexpr float HeartDubVolumeScale = 0.6f;

	// Caída mínima (cm) de la atenuación natural de PlayAt, como la de los emotes.
	constexpr float MinFalloff = 100.f;

	/** Busca el recurso con la ruta fija; un static por ruta para no repetir la búsqueda en cada tortuga. */
	template <int32 Index>
	USoundBase* FindStatic(const TCHAR* Path)
	{
		static ConstructorHelpers::FObjectFinderOptional<USoundBase> Finder(Path);
		return Finder.Get();
	}

	bool CanPlayAudio(const AActor* Owner)
	{
		const UWorld* World = Owner ? Owner->GetWorld() : nullptr;
		return World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer && FApp::CanEverRenderAudio();
	}
}

FName TNTurtleActionSfx::PropertyName(ETNTurtleActionSfx Sfx)
{
	switch (Sfx)
	{
	case ETNTurtleActionSfx::Knockdown:       return TEXT("KnockdownSound");
	case ETNTurtleActionSfx::Kill:            return TEXT("KillSound");
	case ETNTurtleActionSfx::Pickup:          return TEXT("PickupSound");
	case ETNTurtleActionSfx::Throw:           return TEXT("ThrowSound");
	case ETNTurtleActionSfx::Consume:         return TEXT("ConsumeSound");
	case ETNTurtleActionSfx::ReviveSuccess:   return TEXT("ReviveSuccessSound");
	case ETNTurtleActionSfx::DBNOHeartbeat:   return TEXT("DBNOHeartbeatSound");
	case ETNTurtleActionSfx::TotemSelfRevive: return TEXT("TotemSelfReviveSound");
	default:                                  return NAME_None;
	}
}

const TCHAR* TNTurtleActionSfx::DefaultAssetPath(ETNTurtleActionSfx Sfx)
{
	using namespace TNTurtleActionSfxDetail;
	switch (Sfx)
	{
	case ETNTurtleActionSfx::Kill:    return KillPath;
	case ETNTurtleActionSfx::Pickup:  return PickupPath;
	case ETNTurtleActionSfx::Throw:   return ThrowPath;
	case ETNTurtleActionSfx::Consume: return ConsumePath;
	default:                          return TEXT("");
	}
}

bool TNTurtleActionSfx::HasSynthFallback(ETNTurtleActionSfx Sfx)
{
	return Sfx == ETNTurtleActionSfx::Knockdown || Sfx == ETNTurtleActionSfx::ReviveSuccess
		|| Sfx == ETNTurtleActionSfx::DBNOHeartbeat || Sfx == ETNTurtleActionSfx::TotemSelfRevive;
}

USoundBase* TNTurtleActionSfx::FindDefaultSound(ETNTurtleActionSfx Sfx)
{
	using namespace TNTurtleActionSfxDetail;
	switch (Sfx)
	{
	case ETNTurtleActionSfx::Kill:    return FindStatic<0>(KillPath);
	case ETNTurtleActionSfx::Pickup:  return FindStatic<1>(PickupPath);
	case ETNTurtleActionSfx::Throw:   return FindStatic<2>(ThrowPath);
	case ETNTurtleActionSfx::Consume: return FindStatic<3>(ConsumePath);
	default:                          return nullptr;
	}
}

void TNTurtleActionSfx::PlayAt(UWorld* World, USoundBase* Sound, const FVector& Location, float InnerRadius, float OuterRadius)
{
	if (!Sound || !World || !World->bAllowAudioPlayback || World->IsNetMode(NM_DedicatedServer) || !GEngine || !GEngine->UseSound())
	{
		return;
	}
	if (Sound->AttenuationSettings)
	{
		UGameplayStatics::SpawnSoundAtLocation(World, Sound, Location);
		return;
	}
	FAudioDevice::FCreateComponentParams Params(World);
	Params.SetLocation(Location);
	UAudioComponent* Audio = FAudioDevice::CreateComponent(Sound, Params);
	if (!Audio)
	{
		return;
	}
	Audio->SetWorldLocation(Location);
	Audio->bAllowSpatialization = true;
	Audio->bOverrideAttenuation = true;
	Audio->AttenuationOverrides.bAttenuate = true;
	Audio->AttenuationOverrides.bSpatialize = true;
	Audio->AttenuationOverrides.AttenuationShape = EAttenuationShape::Sphere;
	Audio->AttenuationOverrides.AttenuationShapeExtents = FVector(FMath::Max(0.f, InnerRadius));
	Audio->AttenuationOverrides.FalloffDistance = FMath::Max(OuterRadius - InnerRadius, TNTurtleActionSfxDetail::MinFalloff);
	Audio->AttenuationOverrides.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	Audio->bAutoDestroy = true;
	Audio->bStopWhenOwnerDestroyed = false;
	Audio->Play();
	UE_LOG(LogTortunabo, Verbose, TEXT("[ActionSfx] %s con atenuación natural (%.0f-%.0f cm)"), *GetNameSafe(Sound), InnerRadius, OuterRadius);
}

bool TNTurtleActionSfx::ShouldKeepHeartbeat(const AActor* Owner)
{
	const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Owner);
	return Turtle && Turtle->IsKnockedDown() && Turtle->IsLocallyControlled();
}

// ─────────────────────────────────────────────────────────────────────────────
// UTN_TurtleActionSynthComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_TurtleActionSynthComponent::UTN_TurtleActionSynthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

UTN_TurtleActionSynthComponent* UTN_TurtleActionSynthComponent::FindOrAddTo(AActor* InOwner)
{
	if (!TNTurtleActionSfxDetail::CanPlayAudio(InOwner))
	{
		return nullptr;
	}
	if (UTN_TurtleActionSynthComponent* Existing = InOwner->FindComponentByClass<UTN_TurtleActionSynthComponent>())
	{
		return Existing;
	}
	UTN_TurtleActionSynthComponent* Comp = NewObject<UTN_TurtleActionSynthComponent>(InOwner, NAME_None, RF_Transient);
	Comp->RegisterComponent();
	InOwner->AddInstanceComponent(Comp);
	return Comp;
}

void UTN_TurtleActionSynthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopHeartbeat();
	Super::EndPlay(EndPlayReason);
}

void UTN_TurtleActionSynthComponent::PlayKnockdown()
{
	if (UTN_ShellImpactSynthComponent* Synth = GetImpactSynth())
	{
		const bool bPlayed = Synth->Play(ETNShellImpactSound::Turtle, KnockdownStrength, KnockdownPitch);
		UE_LOG(LogTortunabo, Verbose, TEXT("[ActionSfx] %s derribo sintetizado (%s)"), *GetNameSafe(GetOwner()), bPlayed ? TEXT("suena") : TEXT("sin oyente"));
	}
}

void UTN_TurtleActionSynthComponent::PlayRevive(bool bTotem)
{
	using namespace TNTurtleActionSfxDetail;
	if (UTN_ScoreShellSynthComponent* Synth = GetChimeSynth())
	{
		Synth->TriggerSound(ETNScoreShellSound::Plin, bTotem ? TotemChimeTier : ReviveChimeTier, ReviveSemitones);
		UE_LOG(LogTortunabo, Verbose, TEXT("[ActionSfx] %s %s sintetizado"), *GetNameSafe(GetOwner()), bTotem ? TEXT("tótem") : TEXT("reanimar"));
	}
}

void UTN_TurtleActionSynthComponent::StartHeartbeat()
{
	UWorld* World = GetWorld();
	if (!World || IsHeartbeatActive())
	{
		return;
	}
	UE_LOG(LogTortunabo, Verbose, TEXT("[ActionSfx] %s latido sintetizado: empieza"), *GetNameSafe(GetOwner()));
	Beat();
	World->GetTimerManager().SetTimer(HeartbeatTimer, this, &UTN_TurtleActionSynthComponent::Beat, HeartbeatPeriod, true);
}

void UTN_TurtleActionSynthComponent::StopHeartbeat()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HeartbeatTimer);
		World->GetTimerManager().ClearTimer(HeartbeatEchoTimer);
	}
}

bool UTN_TurtleActionSynthComponent::IsHeartbeatActive() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimerManager().IsTimerActive(HeartbeatTimer);
}

void UTN_TurtleActionSynthComponent::Beat()
{
	using namespace TNTurtleActionSfxDetail;
	// Se para solo si la tortuga ya no está derribada o ha cambiado de dueño, aunque nadie llame a StopHeartbeat
	// (como OnDBNOAudioFinished con el latido de recurso).
	if (!TNTurtleActionSfx::ShouldKeepHeartbeat(GetOwner()))
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[ActionSfx] %s latido sintetizado: se para solo"), *GetNameSafe(GetOwner()));
		StopHeartbeat();
		return;
	}
	if (UTN_ScoreShellSynthComponent* Synth = GetHeartSynth())
	{
		Synth->TriggerSound(ETNScoreShellSound::Pom, 0, HeartLubSemitones, HeartbeatVolume);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(HeartbeatEchoTimer, this, &UTN_TurtleActionSynthComponent::BeatEcho,
			HeartbeatEchoDelay, false);
	}
}

void UTN_TurtleActionSynthComponent::BeatEcho()
{
	using namespace TNTurtleActionSfxDetail;
	if (UTN_ScoreShellSynthComponent* Synth = GetHeartSynth())
	{
		Synth->TriggerSound(ETNScoreShellSound::Pom, 0, HeartDubSemitones, HeartbeatVolume * HeartDubVolumeScale);
	}
}

UTN_ShellImpactSynthComponent* UTN_TurtleActionSynthComponent::GetImpactSynth()
{
	if (!ImpactSynth)
	{
		ImpactSynth = UTN_ShellImpactSynthComponent::AttachTo(GetOwner());
	}
	return ImpactSynth;
}

UTN_ScoreShellSynthComponent* UTN_TurtleActionSynthComponent::GetChimeSynth()
{
	if (!ChimeSynth)
	{
		AActor* Owner = GetOwner();
		ChimeSynth = Owner ? UTN_ScoreShellSynthComponent::Attach3D(Owner, Owner->GetActorLocation()) : nullptr;
	}
	return ChimeSynth;
}

UTN_ScoreShellSynthComponent* UTN_TurtleActionSynthComponent::GetHeartSynth()
{
	if (!HeartSynth)
	{
		HeartSynth = UTN_ScoreShellSynthComponent::Attach2D(GetOwner());
	}
	return HeartSynth;
}
