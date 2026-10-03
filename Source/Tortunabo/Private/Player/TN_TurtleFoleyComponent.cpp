#include "Player/TN_TurtleFoleyComponent.h"
#include "TN_TurtleFoleyDSP.h"
#include "Core/TN_Log.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_StaminaComponent.h"
#include "Player/TN_TurtleSurface.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "World/ProcMap/TN_ProcMapLayout.h"
#include "World/ProcMap/TN_ProcMapTerrain.h"
#include "World/ProcMap/TN_StormCough.h"
#include "AudioDevice.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundGenerator.h"
#include <atomic>

namespace TNTurtleFoley
{
	// ─────────────────────────────────────────────────────────────────────────
	// Consola
	// ─────────────────────────────────────────────────────────────────────────

	static float GVoiceVolume = 1.f;
	static FAutoConsoleVariableRef CVarVoiceVolume(
		TEXT("TN.Voice.Volume"),
		GVoiceVolume,
		TEXT("Multiplicador del volumen de los pasos y el jadeo sintetizados de las tortugas (1 = normal, 0 = apagado)."));

	static int32 GVoiceSurface = -1;
	static FAutoConsoleVariableRef CVarVoiceSurface(
		TEXT("TN.Voice.Surface"),
		GVoiceSurface,
		TEXT("Fuerza la superficie de los pasos de todas las tortugas: -1 = la de verdad, 0 = arena, 1 = tierra, 2 = roca, 3 = madera, 4 = agua."),
		ECVF_Cheat);

	static int32 GVoiceDebug = 0;
	static FAutoConsoleVariableRef CVarVoiceDebug(
		TEXT("TN.Voice.Debug"),
		GVoiceDebug,
		TEXT("1 = muestra en pantalla, por cada tortuga que se oye, la velocidad, de dónde salen los pasos, la superficie y el jadeo."),
		ECVF_Cheat);

	// ─────────────────────────────────────────────────────────────────────────
	// Ajustes
	// ─────────────────────────────────────────────────────────────────────────

	/** Pie levantado por encima de esto (cm sobre el suelo de los pies): su siguiente llegada al suelo es un paso. */
	constexpr float FootLiftArm = 2.f;
	/** Pie de vuelta al suelo por debajo de esto (cm). */
	constexpr float FootContact = 0.9f;
	/** Subida lenta del suelo de los pies (cm/s): el más bajo lo baja al momento; así sigue el bamboleo de la cadera. */
	constexpr float FloorRise = 8.f;
	/** Pasos del mismo pie más seguidos que esto no cuentan (s); entre dos pasos cualesquiera, la mitad. */
	constexpr double MinSameFootInterval = 0.12;
	/** Arranca a esta distancia del oyente (cm, más allá del alcance) y se para a esta otra (margen para no titubear). */
	constexpr float StartMargin = 400.f;
	constexpr float StopMargin = 1200.f;
	/** Segundos sin pasos ni jadeo antes de parar el sintetizador. */
	constexpr double IdleStopSeconds = 2.5;

	// La superficie (biomas, nombres, estructuras, agua poco profunda) la resuelve TNTurtleSurface, compartida con el
	// arrastre del panzazo y su polvo: los índices tienen que coincidir con los del motor de sonido.
	static_assert(TNTurtleSurface::Num == Surface::Num, "TNTurtleSurface y TNTurtleFoley::Surface tienen las mismas superficies");
	static_assert(TNTurtleSurface::Sand == Surface::Sand && TNTurtleSurface::Soil == Surface::Soil && TNTurtleSurface::Rock == Surface::Rock
		&& TNTurtleSurface::Wood == Surface::Wood && TNTurtleSurface::Water == Surface::Water, "Mismo orden de superficies");

	/** Estado de la tortuga leído en cada fotograma (todo sale de estado replicado: vale igual en todas las máquinas). */
	struct FTurtleState
	{
		bool bValid = false;
		bool bLocal = false;
		/** El oyente está a tiro (la tortuga local, siempre). */
		bool bNear = false;
		float Speed = 0.f;
		float VelZ = 0.f;
		/** Velocidad horizontal (para los choques arrastrándose). */
		FVector2D Vel2D = FVector2D::ZeroVector;
		bool bGrounded = false;
		bool bFalling = false;
		/** Pose de panzazo (en el aire o sobre la tripa) y sobre la tripa en el suelo. */
		bool bBellyPose = false;
		bool bBellyGround = false;
		/** Muerta, derribada, en el caparazón, en panzazo, llevada por otra o nadando: sin pasos. */
		bool bBlockedSteps = true;
		bool bSprinting = false;
		bool bRunGait = false;
		float WalkSpeed = 450.f;
		float SprintSpeed = 800.f;
		/** 1 = normal; más si lleva a otra tortuga en alto. */
		float Heavy = 1.f;
		/** Estamina (0..1 de la máxima con el peso que lleva). */
		float Stamina = 1.f;
		bool bExhausted = false;
		bool bUnlimited = false;
		/** Muerta o derribada: sin jadeo. */
		bool bBlockedPant = false;
		/** Tose en la tormenta: el jadeo calla. */
		bool bCoughing = false;
		/** El Blueprint tiene FootstepSound: los pasos de siempre suenan y los sintetizados callan. */
		bool bLegacySteps = false;
		/** Base de la cápsula (donde pisa). */
		FVector FootLocation = FVector::ZeroVector;
	};

	/**
	 * Generador del hilo de render de audio: solo el motor en C++ puro y los parámetros atómicos, que comparte con el
	 * componente por un puntero compartido: si el componente se destruye mientras suena, no queda nada colgando.
	 */
	class FGenerator final : public ISoundGenerator
	{
	public:
		FGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<FSharedParams, ESPMode::ThreadSafe>& InParams)
			: Params(InParams)
			, OutChannels(FMath::Max(1, InNumChannels))
		{
			// El arranque vigente y su cursor del anillo los fijó Init en el hilo de juego antes de pedir el generador.
			const uint32 Run = Params->RunId.load(std::memory_order_acquire);
			DspEngine.Init(InSampleRate, Params->VoiceSeed.load(std::memory_order_relaxed), Params->RunSeed.load(std::memory_order_relaxed),
				Run, Params->RunStartSeq.load(std::memory_order_relaxed));
		}

		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			const int32 Frames = NumSamples / OutChannels;
			DspEngine.Render(OutAudio, Frames, OutChannels, *Params);
			for (int32 i = Frames * OutChannels; i < NumSamples; ++i)
			{
				OutAudio[i] = 0.f;
			}
			return NumSamples;
		}

	private:
		TSharedPtr<FSharedParams, ESPMode::ThreadSafe> Params;
		int32 OutChannels = 1;
		FEngine DspEngine;
	};
}

// ─────────────────────────────────────────────────────────────────────────────
// UTN_TurtleFoleyComponent
// ─────────────────────────────────────────────────────────────────────────────

static_assert(TNTurtleFoley::Surface::Num == 5, "UTN_TurtleFoleyComponent::NumSurfaces tiene que coincidir con TNTurtleFoley::Surface::Num");

UTN_TurtleFoleyComponent::UTN_TurtleFoleyComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Cada fotograma después de la animación (los huesos de los pies ya están al día); lejos del oyente, a 4 Hz.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	bAutoActivate = false;
	NumChannels = 1;
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	SharedParams = MakeShared<TNTurtleFoley::FSharedParams, ESPMode::ThreadSafe>();
}

UTN_TurtleFoleyComponent* UTN_TurtleFoleyComponent::FindOrAddTo(AActor* InOwner)
{
	if (!IsValid(InOwner) || InOwner->IsActorBeingDestroyed()) { return nullptr; }
	if (UTN_TurtleFoleyComponent* Existing = InOwner->FindComponentByClass<UTN_TurtleFoleyComponent>())
	{
		return Existing;
	}
	const UWorld* OwnerWorld = InOwner->GetWorld();
	if (!OwnerWorld || !OwnerWorld->IsGameWorld() || OwnerWorld->GetNetMode() == NM_DedicatedServer || !FApp::CanEverRenderAudio())
	{
		return nullptr;
	}

	UTN_TurtleFoleyComponent* Comp = NewObject<UTN_TurtleFoleyComponent>(InOwner, NAME_None, RF_Transient);
	if (USceneComponent* RootComp = InOwner->GetRootComponent())
	{
		Comp->SetupAttachment(RootComp);
	}
	Comp->RegisterComponent();
	InOwner->AddInstanceComponent(Comp);
	UE_LOG(LogTortunabo, Verbose, TEXT("[TurtleFoley] Pasos y jadeo sintetizados en %s."), *InOwner->GetName());
	return Comp;
}

void UTN_TurtleFoleyComponent::SetDebugPant(int32 InLevel)
{
	DebugPant = FMath::Clamp(InLevel, 0, 2);
}

void UTN_TurtleFoleyComponent::SetDebugSteps(int32 InLevel)
{
	DebugSteps = FMath::Clamp(InLevel, 0, 2);
	DebugStepTimer = 0.f;
	if (DebugSteps > 0 && bSlowTick)
	{
		bSlowTick = false;
		SetComponentTickInterval(0.f);
	}
}

void UTN_TurtleFoleyComponent::SetDebugDrag(int32 InLevel)
{
	DebugDrag = FMath::Clamp(InLevel, 0, 2);
	if (DebugDrag > 0 && bSlowTick)
	{
		bSlowTick = false;
		SetComponentTickInterval(0.f);
	}
}

void UTN_TurtleFoleyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const UWorld* CompWorld = GetWorld();
	if (!CompWorld || !SharedParams.IsValid()) { return; }
	const double Now = CompWorld->GetTimeSeconds();

	TNTurtleFoley::FTurtleState Frame;
	ReadFrame(Frame, Now);
	if (!Frame.bValid) { return; }

	// Lejos del oyente basta un tick lento: el cansancio y las caídas se siguen llevando al día.
	const bool bWantSlow = !Frame.bNear && DebugSteps == 0 && DebugDrag == 0;
	if (bWantSlow != bSlowTick)
	{
		bSlowTick = bWantSlow;
		SetComponentTickInterval(bWantSlow ? 0.25f : 0.f);
	}

	UpdatePant(DeltaTime, Now, Frame);
	UpdateJumpAndLanding(DeltaTime, Now, Frame);
	UpdateSteps(DeltaTime, Now, Frame);
	UpdateDrag(DeltaTime, Now, Frame);

	// Objetivos para el hilo de audio.
	TNTurtleFoley::FSharedParams& P = *SharedParams;
	const float Volume = FMath::Max(0.f, TNTurtleFoley::GVoiceVolume);
	TNTurtleFoley::FSharedParams::Set(P.Master, Loudness * Volume * (Frame.bLocal ? LocalPlayerBoost : 1.f));
	TNTurtleFoley::FSharedParams::Set(P.StepGain, StepLoudness);
	TNTurtleFoley::FSharedParams::Set(P.BreathGain, BreathLoudness);
	TNTurtleFoley::FSharedParams::Set(P.Pant, PantSent);
	P.PantHush.store(bPantHushed ? 1 : 0, std::memory_order_relaxed);
	TNTurtleFoley::FSharedParams::Set(P.DragGain, DragLoudness);
	TNTurtleFoley::FSharedParams::Set(P.Drag, DragSent);
	TNTurtleFoley::FSharedParams::Set(P.DragPace, DragPaceSent);
	for (int32 s = 0; s < NumSurfaces; ++s)
	{
		TNTurtleFoley::FSharedParams::Set(P.DragSurf[s], DragSurface[s]);
	}

	// Arranque y parada: los pasos lo arrancan al pisar (EmitStep); el jadeo y el arrastre, aquí. Se para lejos del
	// oyente o tras un rato sin nada que decir, cuando el generador ya ha callado del todo.
	if (Frame.bNear && Volume > 0.f && (PantSent > 0.f || DragSent > 0.f) && !IsActive())
	{
		StartSynth();
	}
	else if (IsActive())
	{
		const bool bIdle = PantSent <= 0.f && DragSent <= 0.f && DebugSteps == 0 && Now - LastActivityTime > TNTurtleFoley::IdleStopSeconds
			&& P.Busy.load(std::memory_order_relaxed) == 0;
		if (!Frame.bNear || Volume <= 0.f || bIdle)
		{
			Stop();
		}
	}

	if (TNTurtleFoley::GVoiceDebug > 0 && Frame.bNear)
	{
		ShowDebug(Frame);
	}
}

void UTN_TurtleFoleyComponent::ReadFrame(TNTurtleFoley::FTurtleState& Out, double Now)
{
	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner());
	if (!Turtle) { return; }
	Out.bValid = true;
	Out.bLocal = IsLocalTurtle();
	const float Reach = InnerRadius + FMath::Max(100.f, FalloffDistance);
	Out.bNear = Out.bLocal || GetListenerDistance() < Reach + (IsActive() ? TNTurtleFoley::StopMargin : TNTurtleFoley::StartMargin);

	const FVector Velocity = Turtle->GetVelocity();
	Out.Speed = static_cast<float>(Velocity.Size2D());
	Out.VelZ = static_cast<float>(Velocity.Z);
	Out.Vel2D = FVector2D(Velocity.X, Velocity.Y);
	const UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
	Out.bGrounded = Move && Move->IsMovingOnGround();
	Out.bFalling = Move && Move->IsFalling();
	const bool bSwimming = Move && Move->IsSwimming();

	const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
	const bool bCarried = Carry && Carry->IsBeingCarried();
	Out.Heavy = (Carry && Carry->IsCarrying()) ? 1.35f : 1.f;
	const bool bDown = Turtle->IsDead() || Turtle->IsKnockedDown();
	// Panzazo: hasta que se levanta (el dueño y el servidor lo saben al momento; los demás, al acabar el panzazo).
	Out.bBellyPose = Turtle->IsBellyPoseActive();
	Out.bBellyGround = Out.bBellyPose && Out.bGrounded;
	Out.bBlockedSteps = bDown || Turtle->IsInShell() || Out.bBellyPose || bCarried || bSwimming;
	Out.bBlockedPant = bDown;

	if (const UTN_StaminaComponent* Stamina = Turtle->GetStaminaComponent())
	{
		Out.WalkSpeed = FMath::Max(100.f, Stamina->GetWalkSpeed());
		Out.bSprinting = Stamina->IsSprinting();
		Out.Stamina = FMath::Clamp(Stamina->GetCurrentStamina() / FMath::Max(1.f, Stamina->GetEffectiveMaxStamina()), 0.f, 1.f);
		Out.bExhausted = Stamina->IsExhausted();
		Out.bUnlimited = Stamina->HasUnlimitedStamina();
	}
	// La velocidad de esprintar no se publica: la del movimiento mientras esprinta (o la proporción de serie, 800/450).
	const float SprintGuess = (Out.bSprinting && Move != nullptr) ? Move->MaxWalkSpeed : Out.WalkSpeed * 1.78f;
	Out.SprintSpeed = FMath::Max(Out.WalkSpeed * 1.2f, SprintGuess);
	// Carrera como la de UTN_TurtleAnimInstance: con el sprint o algo por encima de la velocidad de andar.
	Out.bRunGait = (Out.bSprinting && Out.Speed > Out.WalkSpeed * 0.6f) || Out.Speed > Out.WalkSpeed * 1.15f;

	// La tos de la tormenta se la pone ATN_PathStorm más tarde: se busca cada medio segundo mientras falte.
	if (!Cough.IsValid() && Now >= NextCoughLookup)
	{
		NextCoughLookup = Now + 0.5;
		Cough = Turtle->FindComponentByClass<UTN_StormCoughComponent>();
	}
	const UTN_StormCoughComponent* CoughComp = Cough.Get();
	Out.bCoughing = CoughComp && (CoughComp->GetSeverity() > 0.f || CoughComp->IsActive());

	Out.bLegacySteps = Turtle->FootstepSound != nullptr;
	const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.f;
	Out.FootLocation = Turtle->GetActorLocation() - FVector(0.0, 0.0, static_cast<double>(HalfHeight));
}

void UTN_TurtleFoleyComponent::UpdatePant(float DeltaTime, double Now, const TNTurtleFoley::FTurtleState& Frame)
{
	// Cansancio: nada por encima de PantBelowStamina; cuanto más baja, más fuerte; agotada, a tope.
	float Target = 0.f;
	if (DebugPant > 0)
	{
		Target = DebugPant == 1 ? 0.45f : 1.f;
	}
	else if (!Frame.bBlockedPant && !Frame.bUnlimited)
	{
		const float Span = FMath::Max(0.05f, PantBelowStamina - 0.05f);
		Target = TNTurtleFoley::SmoothStep01((PantBelowStamina - Frame.Stamina) / Span);
		if (Frame.bExhausted) { Target = 1.f; }
	}
	// En la tormenta manda la tos; muerta o derribada, nada.
	bPantHushed = Frame.bCoughing || (Frame.bBlockedPant && DebugPant == 0);
	if (bPantHushed) { Target = 0.f; }

	// Sube en medio segundo y se calma despacio (la estamina se recarga deprisa; el jadeo, no).
	const float Tau = Target > Fatigue ? 0.5f : FMath::Max(0.1f, PantCalmSeconds);
	Fatigue += (Target - Fatigue) * TNTurtleFoley::TimeCoef(Tau, FMath::Max(0.f, DeltaTime));
	PantSent = Fatigue < 0.04f ? 0.f : FMath::Clamp(Fatigue, 0.f, 1.f);
	if (PantSent > 0.f) { LastActivityTime = Now; }
}

void UTN_TurtleFoleyComponent::UpdateJumpAndLanding(float DeltaTime, double Now, const TNTurtleFoley::FTurtleState& Frame)
{
	if (Frame.bFalling)
	{
		AirTime += DeltaTime;
		FallPeakSpeed = FMath::Max(FallPeakSpeed, -Frame.VelZ);
	}
	const bool bFree = Frame.bNear && !Frame.bBlockedSteps && DebugSteps == 0;

	// Aterriza: golpe de las dos patas, más fuerte cuanto más rápido caía (un escalón o un bordillo no cuentan).
	if (bWasFalling && Frame.bGrounded && bFree && (AirTime > 0.12f || FallPeakSpeed > 250.f))
	{
		const float Force = FMath::Clamp(0.5f + (FallPeakSpeed - 200.f) / 1000.f, 0.45f, 1.35f);
		EmitStep(TNTurtleFoley::StepKind::Land, NextFoot, Force, 0.3f, Now, Frame);
		ResetFeet();
	}
	// Salta: roce de las patas al despegar.
	if (bWasGrounded && Frame.bFalling && Frame.VelZ > 150.f && bFree)
	{
		EmitStep(TNTurtleFoley::StepKind::Scuff, NextFoot, 0.45f, 0.3f, Now, Frame);
		ResetFeet();
	}
	// Panzazo contra el suelo: «plaf» de tripa, más fuerte cuanto más rápido caía y corría (un escalón bajado
	// arrastrándose no cuenta).
	if (bWasFalling && Frame.bBellyGround && Frame.bNear && DebugSteps == 0 && (AirTime > 0.1f || FallPeakSpeed > 200.f))
	{
		const float Force = FMath::Clamp(0.55f + (FallPeakSpeed - 150.f) / 900.f + Frame.Speed / 2500.f, 0.5f, 1.4f);
		EmitStep(TNTurtleFoley::StepKind::Belly, NextFoot, Force, 0.35f, Now, Frame);
		ResetFeet();
	}

	if (!Frame.bFalling)
	{
		AirTime = 0.f;
		FallPeakSpeed = 0.f;
	}
	bWasFalling = Frame.bFalling;
	bWasGrounded = Frame.bGrounded;
}

void UTN_TurtleFoleyComponent::UpdateSteps(float DeltaTime, double Now, const TNTurtleFoley::FTurtleState& Frame)
{
	// Pasos de prueba en el sitio (TN.Voice.Steps).
	if (DebugSteps > 0)
	{
		DebugStepTimer -= DeltaTime;
		if (DebugStepTimer <= 0.f)
		{
			const bool bRun = DebugSteps >= 2;
			DebugStepTimer = (bRun ? 0.17f : 0.3f) * FMath::FRandRange(0.95f, 1.05f);
			EmitStep(TNTurtleFoley::StepKind::Step, NextFoot, bRun ? 0.95f : 0.62f, bRun ? 1.f : 0.2f, Now, Frame);
		}
		return;
	}

	const bool bCanStep = Frame.bNear && Frame.bGrounded && !Frame.bBlockedSteps && !Frame.bLegacySteps && Frame.Speed > MinStepSpeed;
	if (!bCanStep)
	{
		ResetFeet();
		MovingSince = -1.0;
		return;
	}
	if (MovingSince < 0.0) { MovingSince = Now; }

	// Fuerza y viveza del paso: andando, según la velocidad; corriendo, fuertes y cortos; agotada, algo más pesados.
	const float Walk = Frame.WalkSpeed;
	float Force = 0.f;
	float Pace = 0.f;
	if (Frame.bRunGait)
	{
		const float Over = FMath::Clamp((Frame.Speed - Walk) / FMath::Max(50.f, Frame.SprintSpeed - Walk), 0.f, 1.f);
		Force = 0.88f + 0.12f * Over;
		Pace = 0.65f + 0.35f * Over;
	}
	else
	{
		const float Rel = FMath::Clamp(Frame.Speed / Walk, 0.f, 1.f);
		Force = FMath::Lerp(0.35f, 0.66f, Rel);
		Pace = 0.25f * Rel;
	}
	if (Frame.bExhausted) { Force *= 1.06f; }

	// Ritmo esperado: el de la carrera de UTN_TurtleAnimInstance (dos pasos por ciclo) o el largo de paso aprendido.
	const float Expected = Frame.bRunGait
		? 1.f / (2.f * FMath::Clamp(0.8f + Frame.Speed / 300.f, 2.f, 3.4f))
		: FMath::Clamp(WalkStepLength / FMath::Max(1.f, Frame.Speed), 0.12f, 0.7f);

	// 1) Huesos de los pies, en el espacio de la malla (no les afectan las cuestas ni el suavizado de red): un pie que se
	// había levantado suena al volver al suelo o, si la zancada larga lo deja un poco en el aire, al dejar de bajar.
	bool bBonesOk = false;
	const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner());
	const USkeletalMeshComponent* SkelMesh = Turtle ? Turtle->GetMesh() : nullptr;
	if (SkelMesh && ResolveFootBones())
	{
		const TArray<FTransform>& Pose = SkelMesh->GetComponentSpaceTransforms();
		if (Pose.IsValidIndex(FootBone[0]) && Pose.IsValidIndex(FootBone[1]))
		{
			bBonesOk = true;
			const float MeshScale = static_cast<float>(SkelMesh->GetComponentScale().Z);
			float FootZ[2];
			for (int32 f = 0; f < 2; ++f)
			{
				FootZ[f] = static_cast<float>(Pose[FootBone[f]].GetLocation().Z) * MeshScale;
			}
			const float Lowest = FMath::Min(FootZ[0], FootZ[1]);
			if (!bFloorValid)
			{
				FloorZ = Lowest;
				bFloorValid = true;
				for (int32 f = 0; f < 2; ++f) { FootPrevH[f] = FootZ[f] - FloorZ; }
			}
			FloorZ = FMath::Min(FloorZ + TNTurtleFoley::FloorRise * DeltaTime, Lowest);
			for (int32 f = 0; f < 2; ++f)
			{
				const float H = FootZ[f] - FloorZ;
				if (!bFootArmed[f])
				{
					if (H > TNTurtleFoley::FootLiftArm)
					{
						bFootArmed[f] = true;
						FootPeak[f] = H;
						FootMaxDescent[f] = 0.f;
					}
				}
				else
				{
					FootPeak[f] = FMath::Max(FootPeak[f], H);
					const float Descent = (FootPrevH[f] - H) / FMath::Max(1e-3f, DeltaTime);
					FootMaxDescent[f] = FMath::Max(FootMaxDescent[f], Descent);
					const bool bOnFloor = H < TNTurtleFoley::FootContact;
					const bool bStopped = H < FMath::Min(0.35f * FootPeak[f], 5.f) && FootMaxDescent[f] > 15.f
						&& Descent < 0.2f * FootMaxDescent[f];
					if (bOnFloor || bStopped)
					{
						bFootArmed[f] = false;
						if (Now - FootLastStep[f] >= TNTurtleFoley::MinSameFootInterval
							&& Now - LastStepTime >= 0.5 * TNTurtleFoley::MinSameFootInterval)
						{
							// Aprende el largo del paso andando: da el ritmo del reloj de reserva.
							if (!Frame.bRunGait && Now - LastBoneStepTime < 1.0)
							{
								const float StepLength = Frame.Speed * static_cast<float>(Now - LastBoneStepTime);
								WalkStepLength = FMath::Lerp(WalkStepLength, FMath::Clamp(StepLength, 25.f, 160.f), 0.2f);
							}
							FootLastStep[f] = Now;
							LastBoneStepTime = Now;
							bLastStepFromBones = true;
							FallbackTimer = Expected;
							EmitStep(TNTurtleFoley::StepKind::Step, static_cast<uint8>(f), Force, Pace, Now, Frame);
						}
					}
				}
				FootPrevH[f] = H;
			}
		}
	}

	// 2) Reloj de reserva: sin huesos, o andando un rato sin que los huesos den pasos (malla sin animar).
	const bool bFallback = !bBonesOk
		|| (Now - MovingSince > 0.8 && Now - LastBoneStepTime > FMath::Max(0.8, 2.5 * static_cast<double>(Expected)));
	if (bFallback)
	{
		FallbackTimer -= DeltaTime;
		if (FallbackTimer <= 0.f)
		{
			FallbackTimer = Expected * FMath::FRandRange(0.93f, 1.07f);
			bLastStepFromBones = false;
			EmitStep(TNTurtleFoley::StepKind::Step, NextFoot, Force, Pace, Now, Frame);
		}
	}
}

void UTN_TurtleFoleyComponent::UpdateDrag(float /*DeltaTime*/, double Now, const TNTurtleFoley::FTurtleState& Frame)
{
	// Fuerza del arrastre: sube con la velocidad sobre la tripa (a tope a DragFullSpeed) y calla al pararse.
	float Level = 0.f;
	float Pace = 0.f;
	if (DebugDrag > 0)
	{
		Level = DebugDrag == 1 ? 0.35f : 1.f;
		Pace = DebugDrag == 1 ? 0.2f : 0.85f;
	}
	else if (Frame.bNear && Frame.bBellyGround && Frame.Speed > MinDragSpeed)
	{
		const float Span = FMath::Max(50.f, DragFullSpeed - MinDragSpeed);
		Level = FMath::Pow(TNTurtleFoley::SmoothStep01((Frame.Speed - MinDragSpeed) / Span), 0.8f);
		Pace = FMath::Clamp(Frame.Speed / (DragFullSpeed * 1.3f), 0.f, 1.f);
	}
	DragSent = Level;
	DragPaceSent = Pace;
	if (Level > 0.f)
	{
		LastActivityTime = Now;
		ResolveSurface(Frame.FootLocation, Now, DragSurface);
	}

	// Choque arrastrándose: la velocidad cambia de golpe (el rebote la da la vuelta contra la pared).
	if (Frame.bNear && Frame.bBellyGround && bWasBellyGround && DebugSteps == 0)
	{
		const float Change = static_cast<float>((Frame.Vel2D - PrevBellyVelocity).Size());
		if (Change > 260.f && PrevBellyVelocity.Size() > 180.0 && Now - LastBumpTime > 0.25)
		{
			LastBumpTime = Now;
			EmitStep(TNTurtleFoley::StepKind::Bump, NextFoot, FMath::Clamp(Change / 700.f, 0.4f, 1.3f), 0.5f, Now, Frame);
		}
	}
	PrevBellyVelocity = Frame.Vel2D;
	bWasBellyGround = Frame.bBellyGround;
}

void UTN_TurtleFoleyComponent::PlayStash(bool bIntoShell)
{
	const UWorld* CompWorld = GetWorld();
	if (!CompWorld || !SharedParams.IsValid())
	{
		return;
	}
	const double Now = CompWorld->GetTimeSeconds();
	TNTurtleFoley::FTurtleState Frame;
	ReadFrame(Frame, Now);
	if (!Frame.bValid)
	{
		return;
	}
	// El pie elige el tono (0 = guardar, más grave; 1 = sacar, más agudo).
	EmitStep(TNTurtleFoley::StepKind::Stash, static_cast<uint8>(bIntoShell ? 0 : 1), bIntoShell ? 0.6f : 0.5f, 0.4f, Now, Frame);
}

void UTN_TurtleFoleyComponent::EmitStep(uint8 Kind, uint8 Foot, float Force, float Pace, double Now, const TNTurtleFoley::FTurtleState& Frame)
{
	LastStepTime = Now;
	LastActivityTime = Now;
	NextFoot = static_cast<uint8>((Foot & 1u) ^ 1u);
	// Con los pasos de siempre del Blueprint, los sintetizados callan; los del panzazo (y guardar en el caparazón) no son
	// pasos y suenan igual.
	const bool bPanzazo = Kind == TNTurtleFoley::StepKind::Belly || Kind == TNTurtleFoley::StepKind::Bump
		|| Kind == TNTurtleFoley::StepKind::Stash;
	if (!SharedParams.IsValid() || !Frame.bNear || (Frame.bLegacySteps && !bPanzazo) || TNTurtleFoley::GVoiceVolume <= 0.f) { return; }
	if (!IsActive())
	{
		StartSynth();
		if (!IsActive()) { return; }
	}
	TNTurtleFoley::FStepEvent Ev;
	Ev.Kind = Kind;
	Ev.Foot = static_cast<uint8>(Foot & 1u);
	Ev.Force = Force;
	Ev.Pace = Pace;
	Ev.Heavy = Frame.Heavy;
	ResolveSurface(Frame.FootLocation, Now, Ev.Surf);
	SharedParams->PushStep(Ev);
}

void UTN_TurtleFoleyComponent::ResolveSurface(const FVector& FootLocation, double Now, float OutWeights[NumSurfaces])
{
	const int32 Forced = TNTurtleFoley::GVoiceSurface;
	if (Forced >= 0 && Forced < NumSurfaces)
	{
		TNTurtleSurface::PresetWeights(static_cast<uint8>(Forced), OutWeights);
		FMemory::Memcpy(LastSurface, OutWeights, sizeof(LastSurface));
		return;
	}
	// Los dos pies de una zancada pisan casi lo mismo: una traza cada 0,15 s o cada 30 cm basta.
	if (Now - LastProbeTime < 0.15 && FVector::DistSquared(FootLocation, LastProbeLocation) < FMath::Square(30.0))
	{
		FMemory::Memcpy(OutWeights, LastSurface, sizeof(LastSurface));
		return;
	}
	LastProbeTime = Now;
	LastProbeLocation = FootLocation;

	// Traza compleja desde el centro de la tortuga hasta algo por debajo del pie y la superficie de lo que toque
	// (biomas del mapa, estructuras, nombres, agua poco profunda): TNTurtleSurface, la misma que usan el arrastre del
	// panzazo y su polvo.
	float W[NumSurfaces];
	UWorld* CompWorld = GetWorld();
	const AActor* OwnerActor = GetOwner();
	if (CompWorld && OwnerActor)
	{
		TNTurtleSurface::Probe(CompWorld, OwnerActor, OwnerActor->GetActorLocation(), FootLocation, FindGenerator(Now),
			&NameSurfaceCache, W);
	}
	else
	{
		TNTurtleSurface::PresetWeights(TNTurtleSurface::PresetUnknown, W);
	}
	for (int32 s = 0; s < NumSurfaces; ++s)
	{
		OutWeights[s] = W[s];
		LastSurface[s] = W[s];
	}
}

ATN_ProcMapGenerator* UTN_TurtleFoleyComponent::FindGenerator(double Now)
{
	if (!Generator.IsValid() && Now >= NextGeneratorLookup)
	{
		NextGeneratorLookup = Now + 2.0;
		Generator = TNTurtleSurface::FindGenerator(GetWorld());
	}
	return Generator.Get();
}

bool UTN_TurtleFoleyComponent::ResolveFootBones()
{
	const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner());
	const USkeletalMeshComponent* SkelMesh = Turtle ? Turtle->GetMesh() : nullptr;
	const UObject* MeshAsset = SkelMesh ? SkelMesh->GetSkinnedAsset() : nullptr;
	if (!MeshAsset)
	{
		FootBone[0] = INDEX_NONE;
		FootBone[1] = INDEX_NONE;
		BonesFor.Reset();
		return false;
	}
	if (BonesFor.Get() != MeshAsset)
	{
		// Esqueleto Mixamo de TotugaDemo_Rig; los del maniquí del motor y los dedos, por si cambia la malla.
		static const FName FootNames[2][3] = {
			{ FName(TEXT("LeftFoot")), FName(TEXT("foot_l")), FName(TEXT("LeftToeBase")) },
			{ FName(TEXT("RightFoot")), FName(TEXT("foot_r")), FName(TEXT("RightToeBase")) } };
		BonesFor = MeshAsset;
		for (int32 f = 0; f < 2; ++f)
		{
			FootBone[f] = INDEX_NONE;
			for (const FName& BoneName : FootNames[f])
			{
				const int32 Index = SkelMesh->GetBoneIndex(BoneName);
				if (Index != INDEX_NONE)
				{
					FootBone[f] = Index;
					break;
				}
			}
		}
		ResetFeet();
		if (FootBone[0] == INDEX_NONE || FootBone[1] == INDEX_NONE)
		{
			UE_LOG(LogTortunabo, Verbose, TEXT("[TurtleFoley] %s: la malla no tiene huesos de pies conocidos; pasos con el reloj de reserva."),
				*GetNameSafe(GetOwner()));
		}
	}
	return FootBone[0] != INDEX_NONE && FootBone[1] != INDEX_NONE;
}

void UTN_TurtleFoleyComponent::ResetFeet()
{
	bFloorValid = false;
	for (int32 f = 0; f < 2; ++f)
	{
		bFootArmed[f] = false;
		FootPeak[f] = 0.f;
		FootPrevH[f] = 0.f;
		FootMaxDescent[f] = 0.f;
	}
}

void UTN_TurtleFoleyComponent::ShowDebug(const TNTurtleFoley::FTurtleState& Frame) const
{
	if (!GEngine) { return; }
	static const TCHAR* const SurfaceNames[NumSurfaces] = { TEXT("arena"), TEXT("tierra"), TEXT("roca"), TEXT("madera"), TEXT("agua") };
	FString SurfaceText;
	for (int32 s = 0; s < NumSurfaces; ++s)
	{
		if (LastSurface[s] >= 0.05f) { SurfaceText += FString::Printf(TEXT("%s %.2f "), SurfaceNames[s], LastSurface[s]); }
	}
	// La tos de la tormenta: si la tortuga ya tiene el componente (se lo da ATN_PathStorm), su intensidad y si suena.
	FString CoughText = TEXT(" · sin tos");
	if (const UTN_StormCoughComponent* CoughComp = Cough.Get())
	{
		CoughText = FString::Printf(TEXT(" · tos %.2f%s"), CoughComp->GetSeverity(), CoughComp->IsActive() ? TEXT(" sonando") : TEXT(""));
	}
	const FString DragText = DragSent > 0.f ? FString::Printf(TEXT(" · arrastre %.2f"), DragSent)
		: (Frame.bBellyPose ? FString(TEXT(" · panzazo")) : FString());
	const FString Text = FString::Printf(TEXT("[Voz] %s%s · %.0f cm/s%s · pasos: %s · %s· estamina %.0f%% · jadeo %.2f%s%s%s%s"),
		*GetNameSafe(GetOwner()), Frame.bLocal ? TEXT(" (local)") : TEXT(""), Frame.Speed, Frame.bRunGait ? TEXT(" corriendo") : TEXT(""),
		bLastStepFromBones ? TEXT("huesos") : TEXT("reloj"), *SurfaceText, Frame.Stamina * 100.f, PantSent,
		Frame.bCoughing ? TEXT(" (callado: tose)") : TEXT(""), *CoughText, *DragText, IsActive() ? TEXT(" · sintetizador en marcha") : TEXT(""));
	GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()) + 0x7A11F00Dull, 0.f, Frame.bLocal ? FColor::Cyan : FColor::Silver, Text);
}

bool UTN_TurtleFoleyComponent::IsLocalTurtle() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn && OwnerPawn->IsLocallyControlled();
}

float UTN_TurtleFoleyComponent::GetListenerDistance() const
{
	const FAudioDevice* Device = GetAudioDevice();
	return Device ? Device->GetDistanceToNearestListener(GetComponentLocation()) : 0.f;
}

void UTN_TurtleFoleyComponent::StartSynth()
{
	if (IsActive()) { return; }
	ConfigureAttenuation();
	Start();
}

void UTN_TurtleFoleyComponent::ConfigureAttenuation()
{
	NumChannels = 1;
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	// Como la tos y las fuentes puntuales del ambiente: volumen pleno dentro de InnerRadius, caída natural hasta
	// FalloffDistance más allá y agudos que se apagan con la distancia. Pegada al oyente deja de ser un punto.
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
	Att.NonSpatializedRadiusStart = InnerRadius * 0.5f;
	Att.NonSpatializedRadiusEnd = InnerRadius * 0.2f;
}

bool UTN_TurtleFoleyComponent::Init(int32& /*SampleRate*/)
{
	// En el hilo de juego al arrancar (Start -> Initialize), antes de crear el generador: mono, las semillas y el arranque
	// vigente. La voz sale del jugador con la misma cuenta que la tos (UTN_StormCoughComponent::Init): suenan a la misma
	// tortuga. El azar cambia en cada arranque para no repetir la secuencia.
	NumChannels = 1;
	uint32 VoiceSeed = 0x7A11u;
	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		if (const APlayerState* OwnerState = OwnerPawn->GetPlayerState())
		{
			VoiceSeed = HashCombine(GetTypeHash(OwnerState->GetPlayerId()), 0x7A11u);
		}
		else
		{
			VoiceSeed = GetTypeHash(OwnerPawn->GetFName());
		}
	}
	++StartCount;
	TNTurtleFoley::FSharedParams& P = *SharedParams;
	P.VoiceSeed.store(VoiceSeed != 0u ? VoiceSeed : 1u, std::memory_order_relaxed);
	P.RunSeed.store(HashCombine(HashCombine(VoiceSeed, StartCount), FPlatformTime::Cycles()), std::memory_order_relaxed);
	// El generador nuevo lee los pasos desde aquí; el de un arranque anterior que aún suene deja de leerlos.
	P.RunStartSeq.store(P.WriteSeq.load(std::memory_order_relaxed), std::memory_order_relaxed);
	P.Busy.store(1, std::memory_order_relaxed);
	P.RunId.store(StartCount, std::memory_order_release);
	return true;
}

ISoundGeneratorPtr UTN_TurtleFoleyComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<TNTurtleFoley::FGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, SharedParams);
}

// ─────────────────────────────────────────────────────────────────────────────
// Comandos de prueba
// ─────────────────────────────────────────────────────────────────────────────

namespace TNTurtleFoley
{
	/** El componente de la tortuga local (se crea si aún no lo tiene); null sin tortuga o sin audio. */
	static UTN_TurtleFoleyComponent* FindLocalFoley(UWorld* InWorld)
	{
		const APlayerController* PC = InWorld ? InWorld->GetFirstPlayerController() : nullptr;
		ATortugaCharacter* LocalTurtle = PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr;
		return UTN_TurtleFoleyComponent::FindOrAddTo(LocalTurtle);
	}

	/** TN.Voice.Steps <0|1|2>: pasos de prueba en el sitio en la tortuga local. */
	static void RunStepsCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		const int32 Mode = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 0, 2) : 1;
		UTN_TurtleFoleyComponent* Foley = FindLocalFoley(InWorld);
		if (!Foley)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[TurtleFoley] TN.Voice.Steps: no hay tortuga local o esta máquina no tiene audio."));
			return;
		}
		Foley->SetDebugSteps(Mode);
		UE_LOG(LogTortunabo, Log, TEXT("[TurtleFoley] Pasos de prueba: %s."),
			Mode == 0 ? TEXT("apagados (mandan los pies)") : (Mode == 1 ? TEXT("andando en el sitio") : TEXT("corriendo en el sitio")));
	}

	/** TN.Voice.Pant <0|1|2>: jadeo de prueba en la tortuga local. */
	static void RunPantCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		const int32 Mode = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 0, 2) : 2;
		UTN_TurtleFoleyComponent* Foley = FindLocalFoley(InWorld);
		if (!Foley)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[TurtleFoley] TN.Voice.Pant: no hay tortuga local o esta máquina no tiene audio."));
			return;
		}
		Foley->SetDebugPant(Mode);
		UE_LOG(LogTortunabo, Log, TEXT("[TurtleFoley] Jadeo de prueba: %s."),
			Mode == 0 ? TEXT("apagado (manda la estamina)") : (Mode == 1 ? TEXT("suave") : TEXT("agotada")));
	}

	/** TN.Voice.Drag <0|1|2>: arrastre de prueba en el sitio en la tortuga local. */
	static void RunDragCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		const int32 Mode = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 0, 2) : 2;
		UTN_TurtleFoleyComponent* Foley = FindLocalFoley(InWorld);
		if (!Foley)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[TurtleFoley] TN.Voice.Drag: no hay tortuga local o esta máquina no tiene audio."));
			return;
		}
		Foley->SetDebugDrag(Mode);
		UE_LOG(LogTortunabo, Log, TEXT("[TurtleFoley] Arrastre de prueba: %s."),
			Mode == 0 ? TEXT("apagado (manda el panzazo)") : (Mode == 1 ? TEXT("lento") : TEXT("rápido")));
	}

	static FAutoConsoleCommandWithWorldAndArgs DragCommand(
		TEXT("TN.Voice.Drag"),
		TEXT("Arrastre del panzazo de prueba en el sitio en la tortuga local: 0 = apagado (manda el panzazo), 1 = lento, 2 = rápido. La superficie es la de debajo (o TN.Voice.Surface)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunDragCommand),
		ECVF_Cheat);

	static FAutoConsoleCommandWithWorldAndArgs StepsCommand(
		TEXT("TN.Voice.Steps"),
		TEXT("Pasos de prueba en el sitio en la tortuga local: 0 = apagados (mandan los pies), 1 = andando, 2 = corriendo. La superficie es la de debajo (o TN.Voice.Surface)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunStepsCommand),
		ECVF_Cheat);

	static FAutoConsoleCommandWithWorldAndArgs PantCommand(
		TEXT("TN.Voice.Pant"),
		TEXT("Jadeo de prueba en la tortuga local: 0 = apagado (manda la estamina), 1 = suave, 2 = agotada."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunPantCommand),
		ECVF_Cheat);
}
