// Asiento VR de los vehículos (Docs/Modo_VR.md, «Vehículos»). Ver TN_VRSeatComponent.h.

#include "VR/TN_VRSeatComponent.h"
#include "VR/TN_VRMath.h"
#include "VR/TN_VRSubsystem.h"
#include "Core/TN_Log.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

namespace TNVRSeatDetail
{
	/** Veces por segundo que el dueño manda sus manos al servidor (como la tortuga a pie). */
	constexpr double SendRate = 15.0;
	/** Lo más lejos que una mano puede estar de los ojos (cm): lo que llegue más lejos se recorta. */
	constexpr float MaxHandDistance = 150.f;
}

// Ojos de la tortuga sentada (TotugaDemo_Rig a escala 2,5): unos 53 cm por encima de la cadera y algo por delante.
const FVector UTN_VRSeatComponent::EyeAboveHip(14.f, 0.f, 55.f);

UTN_VRSeatComponent::UTN_VRSeatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

UTN_VRSeatComponent* UTN_VRSeatComponent::FindOn(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UTN_VRSeatComponent>() : nullptr;
}

void UTN_VRSeatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTN_VRSeatComponent, bRepVROccupied);
	// El dueño ya tiene sus manos: solo las reciben los demás.
	DOREPLIFETIME_CONDITION(UTN_VRSeatComponent, RepHandLeft, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UTN_VRSeatComponent, RepHandRight, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UTN_VRSeatComponent, RepHandMask, COND_SkipOwner);
}

void UTN_VRSeatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// El vehículo se va: sin avisar al servidor (ya no hace falta) ni volver a encender sus cámaras.
	bVRView = false;
	bHeadsetView = false;
	CamerasTurnedOff.Reset();
	DebugPose.Reset();
	Super::EndPlay(EndPlayReason);
}

bool UTN_VRSeatComponent::IsLocalOwner() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn && Pawn->IsLocallyControlled() && Pawn->IsPlayerControlled();
}

void UTN_VRSeatComponent::EnsureCamera()
{
	if (VRCamera || !GetOwner())
	{
		return;
	}
	VRCamera = NewObject<UCameraComponent>(GetOwner(), TEXT("VRSeatCamera"), RF_Transient);
	VRCamera->SetupAttachment(this);
	VRCamera->SetAutoActivate(false);
	VRCamera->bUsePawnControlRotation = false;
	VRCamera->SetFieldOfView(90.f);
	VRCamera->RegisterComponent();
	VRCamera->SetActive(false);
}

void UTN_VRSeatComponent::SetVRView(bool bOn, bool bHeadset)
{
	if (bOn && !IsLocalOwner())
	{
		bOn = false;
	}
	if (!bOn)
	{
		bHeadset = false;
	}
	if (bOn == bVRView && bHeadset == bHeadsetView)
	{
		return;
	}
	const bool bWasHeadset = bVRView && bHeadsetView;
	bVRView = bOn;
	bHeadsetView = bHeadset;
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (bOn)
	{
		EnsureCamera();
		// Las cámaras del vehículo (persecución con brazo, hombro de la artillera) se apagan: manda la del asiento.
		TInlineComponentArray<UCameraComponent*> Cameras(Owner);
		for (UCameraComponent* Camera : Cameras)
		{
			if (Camera && Camera != VRCamera && Camera->IsActive())
			{
				Camera->SetActive(false);
				CamerasTurnedOff.AddUnique(Camera);
			}
		}
		if (VRCamera)
		{
			// Con gafas la cabeza la pone el seguimiento dentro del asiento; simulado, mira hacia SetSimulatedLook.
			VRCamera->bLockToHmd = bHeadset;
			VRCamera->SetRelativeLocationAndRotation(FVector::ZeroVector, bHeadset ? FRotator::ZeroRotator : SimulatedLook);
			VRCamera->SetActive(true);
		}
		// Sentarse con gafas recentra: la cabeza queda en los ojos de la tortuga mirando al morro.
		if (bHeadset && !bWasHeadset)
		{
			if (UTN_VRSubsystem* VR = UTN_VRSubsystem::Get(this))
			{
				VR->Recenter();
			}
		}
	}
	else
	{
		if (VRCamera)
		{
			VRCamera->SetActive(false);
			VRCamera->bLockToHmd = false;
		}
		for (UCameraComponent* Camera : CamerasTurnedOff)
		{
			if (Camera)
			{
				Camera->SetActive(true);
			}
		}
		CamerasTurnedOff.Reset();
		for (int32 Hand = 0; Hand < 2; ++Hand)
		{
			Hands[Hand] = FTNVRSeatHand();
			bGripHeld[Hand] = false;
			bDisplayHand[Hand] = false;
		}
	}

	// Los demás lo saben por el servidor (las asas de la torreta, los brazos de la tortuga sentada).
	if (Owner->HasAuthority())
	{
		bRepVROccupied = bOn;
		if (!bOn)
		{
			RepHandMask = 0;
		}
	}
	else
	{
		ServerSetVROccupied(bOn);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[VR] %s: vista sentada %s%s."), *Owner->GetName(), bOn ? TEXT("encendida") : TEXT("apagada"),
		bOn ? (bHeadset ? TEXT(" (gafas)") : TEXT(" (simulada)")) : TEXT(""));
}

FVector UTN_VRSeatComponent::GetHeadLocal() const
{
	return VRCamera && bVRView ? VRCamera->GetRelativeLocation() : FVector::ZeroVector;
}

FRotator UTN_VRSeatComponent::GetHeadRelativeRotation() const
{
	return VRCamera && bVRView ? VRCamera->GetRelativeRotation() : FRotator::ZeroRotator;
}

void UTN_VRSeatComponent::SetSimulatedLook(const FRotator& Relative)
{
	SimulatedLook = FRotator(Relative.Pitch, Relative.Yaw, 0.0);
	if (VRCamera && bVRView && !bHeadsetView)
	{
		VRCamera->SetRelativeRotation(SimulatedLook);
	}
}

void UTN_VRSeatComponent::SetLocalHands(const FTNVRSeatHand& Left, const FTNVRSeatHand& Right)
{
	if (DebugPose)
	{
		const UWorld* World = GetWorld();
		const double Elapsed = World ? World->GetRealTimeSeconds() - DebugPoseStart : 0.0;
		if (Elapsed <= DebugPoseDuration)
		{
			// La pose de pruebas viene en los ejes del asiento.
			FTNVRSeatHand Posed[2];
			DebugPose(static_cast<float>(Elapsed), Posed[0], Posed[1]);
			const FTransform& ToWorld = GetComponentTransform();
			for (int32 Hand = 0; Hand < 2; ++Hand)
			{
				Hands[Hand] = Posed[Hand];
				Hands[Hand].Location = ToWorld.TransformPosition(Posed[Hand].Location);
				Hands[Hand].AimDir = ToWorld.TransformVectorNoScale(Posed[Hand].AimDir).GetSafeNormal();
			}
			return;
		}
		DebugPose.Reset();
		UE_LOG(LogTortunabo, Log, TEXT("[VR] %s: fin de la pose de pruebas de las manos."), *GetNameSafe(GetOwner()));
	}
	Hands[0] = Left;
	Hands[1] = Right;
}

const FTNVRSeatHand& UTN_VRSeatComponent::GetHand(int32 Index) const
{
	return Hands[FMath::Clamp(Index, 0, 1)];
}

int32 UTN_VRSeatComponent::StepGrip(int32 Index)
{
	const int32 Hand = FMath::Clamp(Index, 0, 1);
	const float Value = bVRView && Hands[Hand].bTracked ? Hands[Hand].Grip : 0.f;
	return TNVRMath::AnalogButton(Value, bGripHeld[Hand]);
}

void UTN_VRSeatComponent::SetDisplayHands(const FVector& LeftWorld, const FVector& RightWorld, bool bLeft, bool bRight)
{
	DisplayHand[0] = LeftWorld;
	DisplayHand[1] = RightWorld;
	bDisplayHand[0] = bLeft && bVRView;
	bDisplayHand[1] = bRight && bVRView;
	SendHands();
}

void UTN_VRSeatComponent::SendHands()
{
	const UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner || !IsLocalOwner())
	{
		return;
	}
	const double Now = World->GetRealTimeSeconds();
	if (LastHandsSent >= 0.0 && Now - LastHandsSent < 1.0 / TNVRSeatDetail::SendRate)
	{
		return;
	}
	LastHandsSent = Now;
	const FTransform& ToWorld = GetComponentTransform();
	const FVector Left = ToWorld.InverseTransformPosition(DisplayHand[0]).GetClampedToMaxSize(TNVRSeatDetail::MaxHandDistance);
	const FVector Right = ToWorld.InverseTransformPosition(DisplayHand[1]).GetClampedToMaxSize(TNVRSeatDetail::MaxHandDistance);
	const uint8 Mask = static_cast<uint8>((bDisplayHand[0] ? 1 : 0) | (bDisplayHand[1] ? 2 : 0));
	if (Owner->HasAuthority())
	{
		RepHandLeft = Left;
		RepHandRight = Right;
		RepHandMask = bRepVROccupied ? Mask : 0;
	}
	else
	{
		ServerSetHands(Left, Right, Mask);
	}
}

bool UTN_VRSeatComponent::GetDisplayHands(FVector& OutLeft, FVector& OutRight, bool& bOutLeft, bool& bOutRight) const
{
	if (IsLocalOwner())
	{
		OutLeft = DisplayHand[0];
		OutRight = DisplayHand[1];
		bOutLeft = bVRView && bDisplayHand[0];
		bOutRight = bVRView && bDisplayHand[1];
		return bOutLeft || bOutRight;
	}
	if (!bRepVROccupied || RepHandMask == 0)
	{
		return false;
	}
	const FTransform& ToWorld = GetComponentTransform();
	OutLeft = ToWorld.TransformPosition(RepHandLeft);
	OutRight = ToWorld.TransformPosition(RepHandRight);
	bOutLeft = (RepHandMask & 1) != 0;
	bOutRight = (RepHandMask & 2) != 0;
	return true;
}

bool UTN_VRSeatComponent::IsVROccupied() const
{
	return IsLocalOwner() ? bVRView : bRepVROccupied;
}

void UTN_VRSeatComponent::ResetOccupant()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		bRepVROccupied = false;
		RepHandMask = 0;
	}
}

void UTN_VRSeatComponent::ServerSetVROccupied_Implementation(bool bOn)
{
	bRepVROccupied = bOn;
	if (!bOn)
	{
		RepHandMask = 0;
	}
}

void UTN_VRSeatComponent::ServerSetHands_Implementation(FVector_NetQuantize10 Left, FVector_NetQuantize10 Right, uint8 Mask)
{
	// Solo el dueño del vehículo llega aquí (RPC de servidor); las manos se recortan cerca de los ojos.
	RepHandLeft = Left.GetClampedToMaxSize(TNVRSeatDetail::MaxHandDistance);
	RepHandRight = Right.GetClampedToMaxSize(TNVRSeatDetail::MaxHandDistance);
	RepHandMask = bRepVROccupied ? static_cast<uint8>(Mask & 3) : 0;
}

void UTN_VRSeatComponent::SetDebugPose(FDebugPose Pose, float Duration)
{
	DebugPose = MoveTemp(Pose);
	const UWorld* World = GetWorld();
	DebugPoseStart = World ? World->GetRealTimeSeconds() : 0.0;
	DebugPoseDuration = FMath::Max(0.f, Duration);
}

bool UTN_VRSeatComponent::HasDebugPose() const
{
	return static_cast<bool>(DebugPose);
}
