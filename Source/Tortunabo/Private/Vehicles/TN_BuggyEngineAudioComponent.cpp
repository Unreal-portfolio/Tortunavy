#include "Vehicles/TN_BuggyEngineAudioComponent.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyMath.h"
#include "Vehicles/TN_RallyTurretLogic.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace TNBuggyAudioDetail
{
	/** Tono de cada capa: ±MaxPitchSwing por cada media unidad de RPM normalizada respecto a su centro. */
	constexpr float PitchPerHalfRpm = 0.35f;
	constexpr float MinPitch = 0.6f;
	constexpr float MaxPitch = 1.6f;
	/** Por debajo de este volumen la capa no se oye y se deja en silencio sin pararla (los bucles no se cortan). */
	constexpr float SilentVolume = 0.001f;
	/** RPM mínima con la que Chaos se considera fiable (rpm). */
	constexpr float MinChaosRpm = 1.f;
	/** Ralentí: el motor nunca baja de aquí aunque el buggy esté parado. */
	constexpr float IdleRpm01 = 0.05f;
	/** Bajo esta velocidad (cm/s) no hay derrape. */
	constexpr float MinSkidSpeedCms = 150.f;

	const TCHAR* const EngineIdlePath = TEXT("/Game/Audio/Rally/SFX_Buggy_Engine_Idle.SFX_Buggy_Engine_Idle");
	const TCHAR* const EngineMidPath = TEXT("/Game/Audio/Rally/SFX_Buggy_Engine_Mid.SFX_Buggy_Engine_Mid");
	const TCHAR* const EngineHighPath = TEXT("/Game/Audio/Rally/SFX_Buggy_Engine_High.SFX_Buggy_Engine_High");
	const TCHAR* const SkidPath = TEXT("/Game/Audio/Rally/SFX_Buggy_Skid_Loop.SFX_Buggy_Skid_Loop");
}

// ── Lógica pura ───────────────────────────────────────────────────────────────

TNBuggyAudio::FEngineLayerMix TNBuggyAudio::MixEngineLayers(float Rpm01)
{
	using namespace TNBuggyAudioDetail;
	// Posición en [0, 2]: 0 = ralentí, 1 = medio, 2 = alto. Entre dos capas vecinas, fundido de potencia constante.
	const float Position = 2.f * FMath::Clamp(Rpm01, 0.f, 1.f);
	FEngineLayerMix Mix;
	if (Position <= 1.f)
	{
		Mix.Volume[0] = FMath::Cos(Position * HALF_PI);
		Mix.Volume[1] = FMath::Sin(Position * HALF_PI);
	}
	else
	{
		Mix.Volume[1] = FMath::Cos((Position - 1.f) * HALF_PI);
		Mix.Volume[2] = FMath::Sin((Position - 1.f) * HALF_PI);
	}
	for (int32 Layer = 0; Layer < EngineLayerCount; ++Layer)
	{
		Mix.Pitch[Layer] = FMath::Clamp(1.f + PitchPerHalfRpm * (Position - static_cast<float>(Layer)), MinPitch, MaxPitch);
	}
	return Mix;
}

float TNBuggyAudio::EstimateRpm01(float SpeedCms, float TopSpeedCms)
{
	if (TopSpeedCms <= 0.f)
	{
		return TNBuggyAudioDetail::IdleRpm01;
	}
	return FMath::Clamp(FMath::Abs(SpeedCms) / TopSpeedCms, TNBuggyAudioDetail::IdleRpm01, 1.f);
}

float TNBuggyAudio::SkidVolume(float SlipDeg, float SpeedCms, bool bGrounded, float MinSlipDeg, float FullSlipDeg, float FullSpeedCms)
{
	const float Speed = FMath::Abs(SpeedCms);
	if (!bGrounded || Speed < TNBuggyAudioDetail::MinSkidSpeedCms)
	{
		return 0.f;
	}
	const float SlipRange = FMath::Max(FullSlipDeg - MinSlipDeg, 1.f);
	const float SlipAlpha = FMath::Clamp((FMath::Abs(SlipDeg) - MinSlipDeg) / SlipRange, 0.f, 1.f);
	const float SpeedAlpha = FMath::Clamp(Speed / FMath::Max(FullSpeedCms, 1.f), 0.f, 1.f);
	return SlipAlpha * SpeedAlpha;
}

// ── Componente ────────────────────────────────────────────────────────────────

UTN_BuggyEngineAudioComponent::UTN_BuggyEngineAudioComponent()
{
	using namespace TNBuggyAudioDetail;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(false);

	static ConstructorHelpers::FObjectFinder<USoundBase> IdleFinder(EngineIdlePath);
	static ConstructorHelpers::FObjectFinder<USoundBase> MidFinder(EngineMidPath);
	static ConstructorHelpers::FObjectFinder<USoundBase> HighFinder(EngineHighPath);
	static ConstructorHelpers::FObjectFinder<USoundBase> SkidFinder(SkidPath);
	EngineIdleSound = IdleFinder.Object;
	EngineMidSound = MidFinder.Object;
	EngineHighSound = HighFinder.Object;
	SkidSound = SkidFinder.Object;
}

ATN_Buggy* UTN_BuggyEngineAudioComponent::GetBuggy() const
{
	return Cast<ATN_Buggy>(GetOwner());
}

void UTN_BuggyEngineAudioComponent::BeginPlay()
{
	Super::BeginPlay();
	// Sin audio (servidor dedicado o -nosound) no hay nada que mezclar.
	if (GetNetMode() == NM_DedicatedServer || !GetBuggy())
	{
		return;
	}
	EngineLayers.Reset();
	EngineLayers.Add(CreateLoop(EngineIdleSound, TEXT("EngineIdleLoop")));
	EngineLayers.Add(CreateLoop(EngineMidSound, TEXT("EngineMidLoop")));
	EngineLayers.Add(CreateLoop(EngineHighSound, TEXT("EngineHighLoop")));
	SkidLoop = CreateLoop(SkidSound, TEXT("SkidLoop"));
	SmoothedRpm01 = TNBuggyAudioDetail::IdleRpm01;
	SetComponentTickEnabled(true);
}

void UTN_BuggyEngineAudioComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (UAudioComponent* Loop : EngineLayers)
	{
		if (Loop)
		{
			Loop->Stop();
		}
	}
	if (SkidLoop)
	{
		SkidLoop->Stop();
	}
	EngineLayers.Reset();
	SkidLoop = nullptr;
	Super::EndPlay(EndPlayReason);
}

UAudioComponent* UTN_BuggyEngineAudioComponent::CreateLoop(USoundBase* Sound, const TCHAR* Name)
{
	ATN_Buggy* Buggy = GetBuggy();
	if (!Sound || !Buggy)
	{
		return nullptr;
	}
	USceneComponent* AttachTo = Buggy->GetBody() ? static_cast<USceneComponent*>(Buggy->GetBody()) : Buggy->GetRootComponent();
	UAudioComponent* Loop = NewObject<UAudioComponent>(Buggy, Name);
	Loop->SetSound(Sound);
	Loop->bAutoActivate = false;
	Loop->bAutoDestroy = false;
	Loop->SetupAttachment(AttachTo);
	Loop->RegisterComponent();
	Loop->SetVolumeMultiplier(0.f);
	Loop->Play();
	return Loop;
}

float UTN_BuggyEngineAudioComponent::ReadRpm01() const
{
	const ATN_Buggy* Buggy = GetBuggy();
	const UChaosWheeledVehicleMovementComponent* Move = Buggy ? Buggy->GetWheeledMovement() : nullptr;
	if (Move)
	{
		const float MaxRpm = Move->GetEngineMaxRotationSpeed();
		const float Rpm = Move->GetEngineRotationSpeed();
		if (MaxRpm > TNBuggyAudioDetail::MinChaosRpm && Rpm > TNBuggyAudioDetail::MinChaosRpm)
		{
			return FMath::Clamp(Rpm / MaxRpm, TNBuggyAudioDetail::IdleRpm01, 1.f);
		}
	}
	return Buggy ? TNBuggyAudio::EstimateRpm01(Buggy->GetForwardSpeedCms(), TNRallyTurret::BuggyTopSpeedCms)
		: TNBuggyAudioDetail::IdleRpm01;
}

bool UTN_BuggyEngineAudioComponent::IsAnyWheelInContact() const
{
	const ATN_Buggy* Buggy = GetBuggy();
	const UChaosWheeledVehicleMovementComponent* Move = Buggy ? Buggy->GetWheeledMovement() : nullptr;
	if (!Move || !Move->HasValidPhysicsState())
	{
		// Sin estado de ruedas (proxy sin simulación): se supone en el suelo para no callar el derrape.
		return true;
	}
	for (int32 Index = 0; Index < Move->Wheels.Num(); ++Index)
	{
		if (Move->GetWheelState(Index).bInContact)
		{
			return true;
		}
	}
	return false;
}

void UTN_BuggyEngineAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const ATN_Buggy* Buggy = GetBuggy();
	if (!Buggy)
	{
		return;
	}
	SmoothedRpm01 = FMath::FInterpTo(SmoothedRpm01, ReadRpm01(), DeltaTime, SmoothingSpeed);
	const TNBuggyAudio::FEngineLayerMix Mix = TNBuggyAudio::MixEngineLayers(SmoothedRpm01);
	for (int32 Layer = 0; Layer < EngineLayers.Num(); ++Layer)
	{
		if (UAudioComponent* Loop = EngineLayers[Layer])
		{
			const float Volume = Mix.Volume[Layer] * EngineVolume;
			Loop->SetVolumeMultiplier(Volume > TNBuggyAudioDetail::SilentVolume ? Volume : 0.f);
			Loop->SetPitchMultiplier(Mix.Pitch[Layer]);
		}
	}

	const FVector Velocity = Buggy->GetVelocity();
	const float Skid = TNBuggyAudio::SkidVolume(TNBuggy::SlipAngleDeg(Buggy->GetActorForwardVector(), Velocity),
		static_cast<float>(Velocity.Size2D()), IsAnyWheelInContact(), SkidMinSlipDeg, SkidFullSlipDeg, SkidFullSpeedCms);
	SmoothedSkid = FMath::FInterpTo(SmoothedSkid, Skid, DeltaTime, SmoothingSpeed);
	if (SkidLoop)
	{
		SkidLoop->SetVolumeMultiplier(SmoothedSkid * SkidMaxVolume);
	}
}
