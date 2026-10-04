// ATN_VRRig: las manos (Docs/Modo_VR.md, «Coger y lanzar» y «Manos, vibración y confort»). Coger con la mano y lanzar con
// el gesto, manos que no atraviesan el escenario, vibración de los mandos y viñeta de confort al moverse. El resto del rig
// (vista, panel de la interfaz, puntero, mandos) está en TN_VRRig.cpp.

#include "VR/TN_VRRig.h"
#include "VR/TN_VRGrabComponent.h"
#include "VR/TN_VRHandMath.h"
#include "VR/TN_VRMath.h"
#include "Player/MP_GamePlayerController.h"
#include "Player/TortugaCharacter.h"
#include "World/TN_ButtonInteractable.h"
#include "World/ProcMap/TN_ProcPuzzleActors.h"
#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/HitResult.h"
#include "Engine/LocalPlayer.h"
#include "Engine/OverlapResult.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "MotionControllerComponent.h"

static TAutoConsoleVariable<float> CVarTNVRHaptics(TEXT("TN.VR.Haptics"), 1.f,
	TEXT("Fuerza de la vibración de los mandos en VR (0 = sin vibración, 1 = la de serie)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarTNVRComfortVignette(TEXT("TN.VR.ComfortVignette"), 1.f,
	TEXT("Viñeta de confort en VR: bordes oscuros al andar, caer, salir lanzado o girar suave (0 = sin ella, 1 = la de serie, 2 = el doble)."),
	ECVF_Default);

namespace TNVRRigHandsDetail
{
	/** Holgura (cm) de la punta de la aleta contra el escenario: se queda en la superficie sin meterse. */
	constexpr float HandRadius = 4.f;
	/** Rapidez con la que aparece la viñeta de confort y con la que se va (FInterpTo). */
	constexpr float VignetteInSpeed = 8.f;
	constexpr float VignetteOutSpeed = 3.f;
	/** Por debajo de esto, la viñeta de confort no se pinta. */
	constexpr float VignetteMinVisible = 0.01f;

	EControllerHand ToControllerHand(int32 Hand)
	{
		return Hand == 0 ? EControllerHand::Left : EControllerHand::Right;
	}

	/**
	 * Viñeta de la escena en ViewLocation sin la de la cámara: la del motor (0,4) con los volúmenes de posproceso encima,
	 * mezclados como los mezcla el motor (UWorld::AddPostProcessingSettings, de menos a más prioridad). Las tormentas la suben.
	 */
	float SceneVignetteAt(UWorld* World, const FVector& ViewLocation)
	{
		static const float EngineDefault = FPostProcessSettings().VignetteIntensity;
		float Vignette = EngineDefault;
		if (!World)
		{
			return Vignette;
		}
		for (IInterface_PostProcessVolume* Volume : World->PostProcessVolumes)
		{
			if (!Volume)
			{
				continue;
			}
			const FPostProcessVolumeProperties Properties = Volume->GetProperties();
			if (!Properties.bIsEnabled || !Properties.Settings || !Properties.Settings->bOverride_VignetteIntensity)
			{
				continue;
			}
			float Distance = 0.f;
			if (!Properties.bIsUnbound)
			{
				Volume->EncompassesPoint(ViewLocation, 0.f, &Distance);
			}
			const float Weight = TNVRHands::PostProcessVolumeWeight(Properties.BlendWeight, Properties.bIsUnbound, Distance, Properties.BlendRadius);
			if (Weight > 0.f)
			{
				Vignette = FMath::Lerp(Vignette, Properties.Settings->VignetteIntensity, Weight);
			}
		}
		return Vignette;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Punto de agarre y manos contra el escenario
// ─────────────────────────────────────────────────────────────────────────────

FTransform ATN_VRRig::GetGrabPoint(bool bRight) const
{
	const USceneComponent* Hand = bRight ? RightHand.Get() : LeftHand.Get();
	FTransform Point = Hand ? Hand->GetComponentTransform() : GetActorTransform();
	// Hacia la punta de la aleta (+X), donde se agarra.
	Point.SetLocation(Point.GetLocation() + Point.GetRotation().GetForwardVector() * 10.0);
	Point.SetScale3D(FVector::OneVector);
	return Point;
}

bool ATN_VRRig::BlockHandLocation(const UWorld* World, const FVector& From, const FVector& To, float Radius,
	const FCollisionQueryParams& Params, FVector& OutLocation)
{
	OutLocation = To;
	if (!World || From.Equals(To, 0.1))
	{
		return false;
	}
	FHitResult Hit;
	if (!World->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(Radius), Params))
	{
		return false;
	}
	// Los ojos ya dentro de algo (la cabeza metida en una pared): no se sabe dónde está el otro lado; la mano va a su mando.
	if (Hit.bStartPenetrating)
	{
		return false;
	}
	OutLocation = Hit.Location;
	return true;
}

void ATN_VRRig::BlockHandsByWorld(const ATortugaCharacter* Turtle, bool bTurtleView)
{
	using namespace TNVRRigHandsDetail;
	LeftHand->SetRelativeLocation(FVector::ZeroVector);
	RightHand->SetRelativeLocation(FVector::ZeroVector);
	const UCameraComponent* Eyes = Turtle && bTurtleView ? Turtle->GetVRCamera() : nullptr;
	if (!Eyes)
	{
		bHandBlocked[0] = bHandBlocked[1] = false;
		return;
	}
	// Solo el escenario: ni la propia tortuga, ni lo que lleva encima o en las manos.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNVRHandBlock), false, this);
	Params.AddIgnoredActor(Turtle);
	TArray<AActor*> Attached;
	Turtle->GetAttachedActors(Attached);
	Params.AddIgnoredActors(Attached);
	if (const UTN_VRGrabComponent* Grab = Turtle->GetVRGrabComponent())
	{
		for (int32 Hand = 0; Hand < 2; ++Hand)
		{
			if (const UPrimitiveComponent* Held = Grab->GetHeld(Hand))
			{
				Params.AddIgnoredComponent(Held);
			}
		}
	}
	const FVector From = Eyes->GetComponentLocation();
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		const bool bRight = Hand == 1;
		USceneComponent* HandComponent = bRight ? RightHand.Get() : LeftHand.Get();
		const FVector Tip = GetGrabPoint(bRight).GetLocation();
		FVector Stopped;
		const bool bBlocked = BlockHandLocation(GetWorld(), From, Tip, HandRadius, Params, Stopped);
		if (bBlocked)
		{
			// La mano entera (y lo que cuelga de ella: la aleta, el objeto de la mano) va con la punta.
			HandComponent->SetWorldLocation(HandComponent->GetComponentLocation() + (Stopped - Tip));
			if (!bHandBlocked[Hand])
			{
				PulseHaptic(Hand, TNVRHands::Haptics::Bump);
			}
		}
		bHandBlocked[Hand] = bBlocked;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Agarres: coger con la mano, lanzar con el gesto
// ─────────────────────────────────────────────────────────────────────────────

void ATN_VRRig::UpdateHandVelocity(int32 Hand, const FTransform& Origin, const FVector& Point, float DeltaSeconds)
{
	// Respecto del origen de la vista (lo que se mueve la mano, no el cuerpo al andar o saltar), con la media de los últimos
	// fotogramas: decide si soltar es lanzar y hacia dónde.
	if (bPrevGrabPointValid[Hand] && DeltaSeconds > KINDA_SMALL_NUMBER)
	{
		HandVelocityWindow[Hand].Add(TNVRMath::RelativeHandVelocity(PrevGrabOrigin[Hand], PrevGrabPoint[Hand], Origin, Point, DeltaSeconds), DeltaSeconds);
		HandVelocity[Hand] = HandVelocityWindow[Hand].Average();
	}
	PrevGrabPoint[Hand] = Point;
	PrevGrabOrigin[Hand] = Origin;
	bPrevGrabPointValid[Hand] = true;
}

void ATN_VRRig::UpdateGrips(APlayerController* PC, ATortugaCharacter* Turtle, float DeltaSeconds)
{
	const AMP_GamePlayerController* GamePC = Cast<AMP_GamePlayerController>(PC);
	const bool bHeadset = Mode == ETNVRMode::Headset;
	// Sin menú ni rueda delante y con la tortuga en condiciones de usar las manos (ni derribada, ni en el caparazón, ni
	// llevada): si no, los agarres se anulan.
	const bool bActive = bHeadset && Turtle && PC->GetViewTarget() == Turtle && !bMenuMode && !(GamePC && GamePC->IsRadialWheelOpen())
		&& UTN_VRGrabComponent::CanOwnerGrab(Turtle);
	// Tras un giro de golpe (a pasos, al reaparecer) la velocidad de antes va en otros ejes: se empieza de cero.
	if (Turtle && Turtle->GetVRTurnSerial() != LastTurnSerial)
	{
		LastTurnSerial = Turtle->GetVRTurnSerial();
		ResetHandVelocity();
	}
	const FTransform Origin = RigRoot->GetComponentTransform();
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		const bool bRight = Hand == 1;
		const FTransform Point = GetGrabPoint(bRight);
		UpdateHandVelocity(Hand, Origin, Point.GetLocation(), DeltaSeconds);
		const float Value = bHeadset ? FMath::Max(PC->GetInputAnalogKeyState(bRight ? FTNVRKeys::RightGripAxis : FTNVRKeys::LeftGripAxis),
			PC->IsInputKeyDown(bRight ? FTNVRKeys::RightGrip : FTNVRKeys::LeftGrip) ? 1.f : 0.f) : 0.f;
		if (!bActive)
		{
			// Lo que hubiera se anula y, al volver, el agarre cuenta como apretado: no coge nada hasta abrir la mano y volver a
			// apretar (cerrar un menú o levantarse con el agarre apretado no coge lo que haya al alcance).
			CancelGrip(Hand, Turtle);
			bGripHeld[Hand] = true;
			continue;
		}
		const int32 Edge = TNVRMath::AnalogButton(Value, bGripHeld[Hand]);
		if (Edge > 0)
		{
			PressGrip(Hand, Turtle, Point);
		}
		else if (Edge < 0)
		{
			ReleaseGrip(Hand, Turtle);
		}
		HoldGrip(Hand, PC, Turtle, Point);
		if (GripUse[Hand] == EGripUse::None && !bGripHeld[Hand])
		{
			UpdatePoke(Hand, Turtle, Point.GetLocation(), DeltaSeconds);
		}
	}
}

void ATN_VRRig::UpdatePoke(int32 Hand, ATortugaCharacter* Turtle, const FVector& Tip, float DeltaSeconds)
{
	using EVRGrip = ATortugaCharacter::EVRGrip;
	// El botón o el interruptor más cercano a la punta (los demás interactuables abren menús o se cogen: con el agarre).
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNVRPoke), false, Turtle);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Tip, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldDynamic),
		FCollisionShape::MakeSphere(TNVRHands::PokeRearmDistance), Params);
	float Nearest = -1.f;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const ATN_InteractableBase* Button = Cast<ATN_InteractableBase>(Overlap.GetActor());
		const UPrimitiveComponent* Shape = Overlap.GetComponent();
		if (!Shape || !(Cast<ATN_ButtonInteractable>(Button) || Cast<ATN_ProcSwitch>(Button)) || !Button->CanInteract(Turtle))
		{
			continue;
		}
		FVector Closest;
		const float Distance = Shape->GetClosestPointOnCollision(Tip, Closest);
		if (Distance >= 0.f && (Nearest < 0.f || Distance < Nearest))
		{
			Nearest = Distance;
		}
	}
	if (!TNVRHands::UpdatePoke(PokeState[Hand], Nearest, DeltaSeconds, FPlatformTime::Seconds()))
	{
		return;
	}
	// Como con el agarre: interactúa con lo que toca esa mano (el botón, que está pegado a la punta) y lo suelta al momento.
	if (Turtle->VRGripPressed(Hand == 1, Tip, false) == EVRGrip::Touched)
	{
		Turtle->VRGripReleased(EVRGrip::Touched, FVector::ZeroVector);
		PulseHaptic(Hand, TNVRHands::Haptics::Grab);
	}
}


void ATN_VRRig::PressGrip(int32 Hand, ATortugaCharacter* Turtle, const FTransform& Point)
{
	using EVRGrip = ATortugaCharacter::EVRGrip;
	const bool bRight = Hand == 1;
	UTN_VRGrabComponent* Grab = Turtle ? Turtle->GetVRGrabComponent() : nullptr;
	// La otra mano ya lleva al compañero: esta no lo toca (soltarla lo dejaría caer); la izquierda corre, como sin nada.
	const int32 Other = 1 - Hand;
	if (GripUse[Other] == EGripUse::Turtle && static_cast<EVRGrip>(GripTurtle[Other]) == EVRGrip::Partner)
	{
		GripUse[Hand] = bRight ? EGripUse::None : EGripUse::Sprint;
		return;
	}
	// 1) Lo que toca esa mano: un objeto del suelo, algo con lo que interactuar o un compañero.

	EVRGrip Result = Turtle ? Turtle->VRGripPressed(bRight, Point.GetLocation(), false) : EVRGrip::None;
	// 2) Un objeto con física.
	if (Result == EVRGrip::None && Grab && Grab->TryGrab(Hand, Point))
	{
		GripUse[Hand] = EGripUse::Grab;
		PulseHaptic(Hand, TNVRHands::Haptics::Grab);
		return;
	}
	// 3) Lo que ya lleva en la aleta derecha (para lanzarlo o soltarlo).
	if (Result == EVRGrip::None && Turtle)
	{
		Result = Turtle->VRGripPressed(bRight, Point.GetLocation(), true);
	}
	if (Result != EVRGrip::None)
	{
		GripUse[Hand] = EGripUse::Turtle;
		GripTurtle[Hand] = static_cast<uint8>(Result);
		PulseHaptic(Hand, TNVRHands::Haptics::Grab);
		return;
	}
	// 4) Nada: con la izquierda, correr mientras se mantiene.
	GripUse[Hand] = bRight ? EGripUse::None : EGripUse::Sprint;
}

void ATN_VRRig::ReleaseGrip(int32 Hand, ATortugaCharacter* Turtle)
{
	using EVRGrip = ATortugaCharacter::EVRGrip;
	const bool bSwing = Turtle && TNVRMath::IsThrowSwing(HandVelocity[Hand], Turtle->VRThrowSpeed);
	if (GripUse[Hand] == EGripUse::Turtle && Turtle)
	{
		const EVRGrip Held = static_cast<EVRGrip>(GripTurtle[Hand]);
		Turtle->VRGripReleased(Held, HandVelocity[Hand]);
		// Soltar despacio lo recién cogido no hace nada (se queda en la aleta): no vibra.
		if (bSwing || Held != EVRGrip::Touched)
		{
			PulseHaptic(Hand, bSwing ? TNVRHands::Haptics::Throw : TNVRHands::Haptics::Drop);
		}
	}
	else if (GripUse[Hand] == EGripUse::Grab && Turtle)
	{
		if (UTN_VRGrabComponent* Grab = Turtle->GetVRGrabComponent())
		{
			// Un objeto con física sale con la mano y con lo que llevaba el cuerpo (andando, lo soltado sigue contigo).
			Grab->Release(Hand, TNVRMath::ReleaseVelocity(HandVelocity[Hand], Turtle->GetVelocity(), true));
			PulseHaptic(Hand, bSwing ? TNVRHands::Haptics::Throw : TNVRHands::Haptics::Drop);
		}
	}
	GripUse[Hand] = EGripUse::None;
}

void ATN_VRRig::HoldGrip(int32 Hand, APlayerController* PC, ATortugaCharacter* Turtle, const FTransform& Point)
{
	UTN_VRGrabComponent* Grab = Turtle ? Turtle->GetVRGrabComponent() : nullptr;
	if (GripUse[Hand] == EGripUse::Grab && Grab)
	{
		if (Grab->IsGrabbing(Hand))
		{
			Grab->UpdateGrab(Hand, Point);
		}
		else
		{
			// Se ha escapado: enganchado, rechazado por el servidor, roto o destruido (lo que quedara del agarre, fuera).
			Grab->Release(Hand, FVector::ZeroVector);
			GripUse[Hand] = EGripUse::None;
			PulseHaptic(Hand, TNVRHands::Haptics::Slip);
		}
		return;
	}
	if (GripUse[Hand] == EGripUse::Sprint && SprintAction)
	{
		// Correr mientras se mantiene: la acción de siempre, inyectada (Completed al dejar de inyectarla).
		ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
		if (UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr)
		{
			Input->InjectInputForAction(SprintAction, FInputActionValue(true), {}, {});
		}
	}
}

void ATN_VRRig::CancelGrip(int32 Hand, ATortugaCharacter* Turtle)
{
	using EVRGrip = ATortugaCharacter::EVRGrip;
	if (GripUse[Hand] == EGripUse::Grab && Turtle)
	{
		if (UTN_VRGrabComponent* Grab = Turtle->GetVRGrabComponent())
		{
			Grab->Release(Hand, FVector::ZeroVector);
		}
	}
	else if (GripUse[Hand] == EGripUse::Turtle && Turtle && static_cast<EVRGrip>(GripTurtle[Hand]) == EVRGrip::Touched)
	{
		// Solo acaba la interacción de mantener (rebuscar); sin velocidad no lanza nada. El compañero y lo de la aleta se
		// quedan como estaban (abrir la pausa no los suelta).
		Turtle->VRGripReleased(EVRGrip::Touched, FVector::ZeroVector);
	}
	GripUse[Hand] = EGripUse::None;
}

void ATN_VRRig::ReleaseGrips(ATortugaCharacter* Turtle)
{
	UTN_VRGrabComponent* Grab = Turtle ? Turtle->GetVRGrabComponent() : nullptr;
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		if (GripUse[Hand] == EGripUse::Grab && Grab)
		{
			Grab->Release(Hand, FVector::ZeroVector);
		}
		GripUse[Hand] = EGripUse::None;
		// Con el agarre aún apretado (al cambiar de tortuga, al reaparecer) no coge nada hasta abrir la mano.
		bGripHeld[Hand] = true;
	}
	ResetHandVelocity();

}

void ATN_VRRig::ResetHandVelocity()
{
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		HandVelocity[Hand] = FVector::ZeroVector;
		HandVelocityWindow[Hand].Reset();
		bPrevGrabPointValid[Hand] = false;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Vibración de los mandos
// ─────────────────────────────────────────────────────────────────────────────

void ATN_VRRig::PulseHaptic(int32 Hand, const TNVRHands::FHapticPulse& Pulse)
{
	if (Mode != ETNVRMode::Headset || (Hand != 0 && Hand != 1))
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	// Un toque más suave no corta uno más fuerte que aún dura.
	if (Now >= HapticUntil[Hand] || Pulse.Amplitude >= HapticAmplitude[Hand])
	{
		HapticAmplitude[Hand] = Pulse.Amplitude;
		HapticUntil[Hand] = Now + Pulse.Seconds;
	}
}

void ATN_VRRig::UpdateHaptics(APlayerController* PC, const ATortugaCharacter* Turtle)
{
	const bool bKnocked = Turtle && Turtle->IsKnockedDown();
	if (bKnocked && !bWasKnockedDown)
	{
		PulseHaptic(0, TNVRHands::Haptics::Knock);
		PulseHaptic(1, TNVRHands::Haptics::Knock);
	}
	bWasKnockedDown = bKnocked;
	const float Scale = FMath::Clamp(CVarTNVRHaptics.GetValueOnGameThread(), 0.f, 1.f);
	if (Mode != ETNVRMode::Headset || Scale <= 0.f)
	{
		StopHaptics(PC);
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const UTN_VRGrabComponent* Grab = Turtle ? Turtle->GetVRGrabComponent() : nullptr;
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		float Amplitude = Now < HapticUntil[Hand] ? HapticAmplitude[Hand] : 0.f;
		// Lo cogido que se engancha tira de la mano: vibra más cuanto más se separa (avisa antes de que se escape).
		if (Grab && GripUse[Hand] == EGripUse::Grab)
		{
			Amplitude = FMath::Max(Amplitude, TNVRHands::StrainAmplitude(Grab->GetStrain(Hand, GetGrabPoint(Hand == 1))));
		}
		Amplitude *= Scale;
		// OpenXR mantiene cada vibración un fotograma: se vuelve a pedir mientras dure y se apaga al acabar.
		if (Amplitude > 0.f)
		{
			PC->SetHapticsByValue(0.f, Amplitude, TNVRRigHandsDetail::ToControllerHand(Hand));
			bHapticOn[Hand] = true;
		}
		else if (bHapticOn[Hand])
		{
			PC->SetHapticsByValue(0.f, 0.f, TNVRRigHandsDetail::ToControllerHand(Hand));
			bHapticOn[Hand] = false;
		}
	}
}

void ATN_VRRig::StopHaptics(APlayerController* PC)
{
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		if (bHapticOn[Hand] && PC)
		{
			PC->SetHapticsByValue(0.f, 0.f, TNVRRigHandsDetail::ToControllerHand(Hand));
		}
		bHapticOn[Hand] = false;
		HapticUntil[Hand] = 0.0;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Viñeta de confort
// ─────────────────────────────────────────────────────────────────────────────

void ATN_VRRig::UpdateComfortVignette(APlayerController* PC, ATortugaCharacter* Turtle, float DeltaSeconds)
{
	using namespace TNVRRigHandsDetail;
	UCameraComponent* Camera = Turtle ? Turtle->GetVRCamera() : nullptr;
	// Con gafas y viendo desde la tortuga: moverse sin mover la cabeza (andar, caer, salir lanzado, el giro suave) marea.
	const bool bApplies = Mode == ETNVRMode::Headset && Camera && PC->GetViewTarget() == Turtle && !bMenuMode;
	const float Wanted = bApplies
		? TNVRHands::ComfortVignette(static_cast<float>(Turtle->GetVelocity().Size()), SmoothTurnRate, CVarTNVRComfortVignette.GetValueOnGameThread())
		: 0.f;
	ComfortVignetteNow = FMath::FInterpTo(ComfortVignetteNow, Wanted, DeltaSeconds, Wanted > ComfortVignetteNow ? VignetteInSpeed : VignetteOutSpeed);
	// Otra cámara (otra tortuga, sin gafas): la de antes se queda como la tenía.
	if (ComfortVignetteCamera.Get() != Camera)
	{
		ApplyComfortVignette(ComfortVignetteCamera.Get(), 0.f);
		ComfortVignetteCamera = Camera;
	}
	ApplyComfortVignette(Camera, ComfortVignetteNow);
}

void ATN_VRRig::ApplyComfortVignette(UCameraComponent* Camera, float Intensity)
{
	if (!Camera)
	{
		ComfortVignetteLayer = TNVRHands::FVignetteLayer();
		return;
	}
	// La tortuga pone la viñeta del caparazón (ATortugaCharacter::ApplyShellDarkness, en su Tick, antes que el rig): se queda
	// la más oscura de las dos, y la capa sabe qué puso ella aunque la tortuga no vuelva a ponerla (en pausa). Sin la del
	// caparazón, la de la escena (0,4 del motor y los volúmenes): la de confort nunca la baja.
	FPostProcessSettings& Post = Camera->PostProcessSettings;
	bool bOverride = Post.bOverride_VignetteIntensity != 0;
	float Value = Post.VignetteIntensity;
	const float SceneBase = Intensity > TNVRRigHandsDetail::VignetteMinVisible && ComfortVignetteLayer.NeedsSceneBase(bOverride, Value)
		? TNVRRigHandsDetail::SceneVignetteAt(GetWorld(), Camera->GetComponentLocation())
		: 0.f;
	ComfortVignetteLayer.Apply(bOverride, Value, Intensity, TNVRRigHandsDetail::VignetteMinVisible, SceneBase);
	Post.bOverride_VignetteIntensity = bOverride;
	Post.VignetteIntensity = Value;
}

FString ATN_VRRig::DescribeHands() const
{
	auto Use = [](EGripUse GripUse)
	{
		switch (GripUse)
		{
			case EGripUse::Turtle: return TEXT("lo de la tortuga");
			case EGripUse::Grab: return TEXT("un objeto con física");
			case EGripUse::Sprint: return TEXT("correr");
			default: return TEXT("nada");
		}
	};
	const double Now = FPlatformTime::Seconds();
	auto Haptic = [this, Now](int32 Hand) { return Now < HapticUntil[Hand] ? HapticAmplitude[Hand] : 0.f; };
	return FString::Printf(TEXT("manos: izquierda %s (agarre: %s), derecha %s (agarre: %s) · viñeta de confort %.2f · vibración %.2f / %.2f"),
		bHandBlocked[0] ? TEXT("parada por el escenario") : TEXT("libre"), Use(GripUse[0]),
		bHandBlocked[1] ? TEXT("parada por el escenario") : TEXT("libre"), Use(GripUse[1]),
		ComfortVignetteNow, Haptic(0), Haptic(1));
}
