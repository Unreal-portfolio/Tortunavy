// Modo VR de la tortuga (Docs/Modo_VR.md): primera persona con gafas o simulada, giro con la cabeza y puntería con la
// aleta. El resto del modo VR (aletas, panel de la interfaz, mandos) vive en ATN_VRRig y UTN_VRSubsystem.

#include "Player/TortugaCharacter.h"
#include "Core/TN_Log.h"
#include "Core/TN_InventoryTypes.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_InventoryComponent.h"
#include "World/TN_InteractableBase.h"
#include "VR/TN_VRGrabComponent.h"
#include "VR/TN_VRMath.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "HeadMountedDisplayFunctionLibrary.h"

void ATortugaCharacter::SetVRView(bool bOn, bool bHeadset)
{
	if (!IsLocallyControlled())
	{
		bOn = false;
	}
	if (!bOn)
	{
		bHeadset = false;
	}
	if (bOn == bVRViewActive && bHeadset == bVRHeadsetView)
	{
		return;
	}
	bVRViewActive = bOn;
	bVRHeadsetView = bHeadset;

	if (bOn)
	{
		if (!VROrigin)
		{
			VROrigin = NewObject<USceneComponent>(this, TEXT("VROrigin"), RF_Transient);
			VROrigin->SetupAttachment(GetCapsuleComponent());
			// Los ojos se ponen a mano cada fotograma (TickFirstPersonView), respecto de la cápsula: en la cabeza, también en
			// el ragdoll, y sin quedarse atrás si algo mueve la cápsula después.
			VROrigin->RegisterComponent();
		}
		if (!VRCamera)
		{
			VRCamera = NewObject<UCameraComponent>(this, TEXT("VRCamera"), RF_Transient);
			VRCamera->SetupAttachment(VROrigin);
			VRCamera->RegisterComponent();
		}
		// Con gafas, la posición de la cabeza respecto del origen del seguimiento se mide de nuevo (#916).
		VRHeadCalibration.Reset();
		VROrigin->SetWorldLocation(ComputeFirstPersonEye(bHeadset));
		bFirstPersonEyeValid = false;
		// Con gafas el origen no gira con la cápsula: lo gira el stick (y la cabeza gira la cámara dentro de él).
		VROrigin->SetUsingAbsoluteRotation(bHeadset);
		VRYaw = Controller ? static_cast<float>(Controller->GetControlRotation().Yaw) : static_cast<float>(GetActorRotation().Yaw);
		if (bHeadset)
		{
			VROrigin->SetWorldRotation(FRotator(0.0, VRYaw, 0.0));
		}
		else
		{
			VROrigin->SetRelativeRotation(FRotator::ZeroRotator);
		}
		VRCamera->bUsePawnControlRotation = !bHeadset;
		VRCamera->bLockToHmd = bHeadset;
		VRCamera->SetRelativeTransform(FTransform::Identity);
		VRCamera->SetFieldOfView(90.f);
		// La cámara activa es la que ve el juego (AActor::CalcCamera coge la primera activa): la de VR manda sobre la
		// primera persona sin gafas.
		SetFirstPersonView(false);
		if (FollowCamera)
		{
			FollowCamera->SetActive(false);
		}
		VRCamera->SetActive(true);
		bVRControlYawValid = false;
	}
	else
	{
		if (VRCamera)
		{
			ApplyShellDarkness(VRCamera, 0.f);
			VRCamera->SetActive(false);
		}
		if (FollowCamera)
		{
			FollowCamera->SetActive(true);
		}
		bLocalVRAimValid = false;
	}
	// Lo que se ve del cuerpo propio (sin la cabeza, con los brazos de las aletas) lo pone TickFirstPersonView.

	// La tortuga mira hacia donde mira la cabeza: aquí al momento y en el servidor (y de ahí a los demás).
	if (bVRPlayer != bOn)
	{
		bVRPlayer = bOn;
		ApplyVRRotationMode();
		if (!HasAuthority())
		{
			ServerSetVRPlayer(bOn);
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[VR] %s: primera persona %s%s."), *GetName(), bOn ? TEXT("encendida") : TEXT("apagada"),
		bOn ? (bHeadset ? TEXT(" (gafas)") : TEXT(" (simulada)")) : TEXT(""));
}

void ATortugaCharacter::AddVRYaw(float DeltaYaw)
{
	// Un giro de golpe (a pasos, al reaparecer mirando al frente): ATN_VRRig vuelve a medir desde cero la velocidad de las
	// manos. El giro suave (unos pocos grados por fotograma) no cuenta.
	if (FMath::Abs(DeltaYaw) > 5.f)
	{
		++VRTurnSerial;
	}
	const FVector OldShift = VRHeadCalibration.OriginShift(VRYaw);
	VRYaw = static_cast<float>(FRotator::NormalizeAxis(static_cast<double>(VRYaw + DeltaYaw)));
	if (VROrigin && bVRHeadsetView)
	{
		VROrigin->SetWorldRotation(FRotator(0.0, VRYaw, 0.0));
		// Girar el origen mueve la cabeza calibrada (gira alrededor de él): el origen se corre lo que haga falta para que
		// siga en los ojos de la tortuga en este mismo fotograma.
		VROrigin->AddWorldOffset(OldShift - VRHeadCalibration.OriginShift(VRYaw));
	}
}

void ATortugaCharacter::RecalibrateVRHead()
{
	VRHeadCalibration.Request();
}

void ATortugaCharacter::UpdateVRHeadCalibration(float DeltaTime)
{
	if (!UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled())
	{
		return;
	}
	// Ponerse las gafas (de quitadas a puestas) cambia dónde está la cabeza: se mide otra vez.
	const EHMDWornState::Type Worn = UHeadMountedDisplayFunctionLibrary::GetHMDWornState();
	if (Worn == EHMDWornState::Worn && VRPrevWornState == static_cast<uint8>(EHMDWornState::NotWorn))
	{
		VRHeadCalibration.Request();
	}
	VRPrevWornState = static_cast<uint8>(Worn);
	FRotator DeviceRotation = FRotator::ZeroRotator;
	FVector DevicePosition = FVector::ZeroVector;
	UHeadMountedDisplayFunctionLibrary::GetOrientationAndPosition(DeviceRotation, DevicePosition);
	if (VRHeadCalibration.Update(DevicePosition, Worn != EHMDWornState::NotWorn, DeltaTime))
	{
		UE_LOG(LogTortunabo, Log, TEXT("[VR] %s: cabeza calibrada, las gafas estaban a %s del origen del seguimiento."), *GetName(),
			*DevicePosition.ToCompactString());
	}
}

void ATortugaCharacter::SetLocalVRAim(const FRotator& Aim, bool bValid)
{
	LocalVRAim = Aim;
	bLocalVRAimValid = bValid && bVRViewActive;
}

FRotator ATortugaCharacter::GetTurtleAimRotation() const
{
	// El dueño en VR: la aleta de ahora mismo.
	if (bVRViewActive && bLocalVRAimValid && IsLocallyControlled())
	{
		return LocalVRAim;
	}
	// El servidor, con un dueño en VR: la última aleta que mandó (si es reciente).
	const UWorld* World = GetWorld();
	if (bVRPlayer && ServerVRAimTime >= 0.0 && World && World->GetTimeSeconds() - ServerVRAimTime < 2.0)
	{
		return ServerVRAim;
	}
	return Controller ? Controller->GetControlRotation() : GetActorRotation();
}

void ATortugaCharacter::SendVRAimToServer()
{
	if (bVRViewActive && bLocalVRAimValid && !HasAuthority())
	{
		ServerSetVRAim(LocalVRAim);
	}
}

void ATortugaCharacter::ServerSetVRAim_Implementation(FRotator Aim)
{
	ServerVRAim = Aim;
	ServerVRAimTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

void ATortugaCharacter::ServerSetVRPlayer_Implementation(bool bOn)
{
	bVRPlayer = bOn;
	ApplyVRRotationMode();
}

void ATortugaCharacter::OnRep_VRPlayer()
{
	ApplyVRRotationMode();
}

void ATortugaCharacter::ApplyVRRotationMode()
{
	// En VR y en primera persona sin gafas la tortuga mira hacia donde mira la vista (también en el servidor, que mueve
	// la cápsula con el giro del mando que le llega del cliente).
	const bool bFacesView = bVRPlayer || bFirstPersonPlayer;
	bUseControllerRotationYaw = bFacesView;
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = !bFacesView;
	}
}

void ATortugaCharacter::TickVRView(float DeltaTime)
{
	if (!bVRViewActive || !bVRHeadsetView || !VRCamera || !VROrigin || !Controller)
	{
		return;
	}
	UpdateVRHeadCalibration(DeltaTime);
	VROrigin->SetWorldRotation(FRotator(0.0, VRYaw, 0.0));
	const FRotator Head = VRCamera->GetComponentRotation();
	float TargetYaw = static_cast<float>(Head.Yaw);
	if (bVRControlYawValid)
	{
		// Algo giró la vista a propósito (reaparecer mirando al frente, ClientSetRotation...): el seguimiento gira con ella
		// para que la cabeza mire hacia allí. Los giros pequeños de cada fotograma (ratón, animaciones) no cuentan.
		const float Wanted = static_cast<float>(Controller->GetControlRotation().Yaw);
		if (FMath::Abs(static_cast<float>(FRotator::NormalizeAxis(static_cast<double>(Wanted - VRLastControlYaw)))) > 20.f)
		{
			AddVRYaw(static_cast<float>(FRotator::NormalizeAxis(static_cast<double>(Wanted - TargetYaw))));
			TargetYaw = Wanted;
		}
	}
	const float Pitch = FMath::Clamp(static_cast<float>(FRotator::NormalizeAxis(Head.Pitch)), -80.f, 80.f);
	Controller->SetControlRotation(FRotator(Pitch, TargetYaw, 0.f));
	VRLastControlYaw = TargetYaw;
	bVRControlYawValid = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Manos VR: brazos del cuerpo que siguen a los mandos, coger con la mano y lanzar con el gesto
// ─────────────────────────────────────────────────────────────────────────────

namespace TNVRHandsDetail
{
	/** Veces por segundo que el dueño manda sus manos al servidor (para que los demás vean los brazos). */
	constexpr double SendRate = 15.0;
	/** Lo más lejos que una mano puede estar de su tortuga (cm): lo que llegue más lejos se recorta. */
	constexpr float MaxHandDistance = 150.f;

	/** Lo que se lanza con un gesto (soltar el agarre con impulso). El resto se usa con el gatillo. */
	bool IsThrownByGesture(ETN_ItemUseType UseType)
	{
		return UseType == ETN_ItemUseType::Throwable || UseType == ETN_ItemUseType::InkThrower || UseType == ETN_ItemUseType::RaceItem
			|| UseType == ETN_ItemUseType::Conch;
	}

	/**
	 * Distancia de un punto a una cápsula vertical (centro, radio y media altura) y el punto de su superficie más cercano
	 * (OutClosest; el propio punto si está dentro).
	 */
	float DistanceToCapsule(const FVector& Point, const FVector& Center, float Radius, float HalfHeight, FVector& OutClosest)
	{
		const FVector Axis(0.0, 0.0, FMath::Max(0.f, HalfHeight - Radius));
		const FVector OnAxis = FMath::ClosestPointOnSegment(Point, Center - Axis, Center + Axis);
		const float FromAxis = static_cast<float>(FVector::Dist(Point, OnAxis));
		OutClosest = FromAxis > Radius ? OnAxis + (Point - OnAxis) * (Radius / FromAxis) : Point;
		return FMath::Max(0.f, FromAxis - Radius);
	}
}

void ATortugaCharacter::SetLocalVRHands(const FVector& Left, const FVector& Right, bool bLeftValid, bool bRightValid,
	const FRotator& LeftRotation, const FRotator& RightRotation)
{
	LocalVRHand[0] = Left;
	LocalVRHand[1] = Right;
	LocalVRHandRot[0] = LeftRotation;
	LocalVRHandRot[1] = RightRotation;
	bLocalVRHandValid[0] = bLeftValid && bVRViewActive;
	bLocalVRHandValid[1] = bRightValid && bVRViewActive;

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetRealTimeSeconds() : 0.0;
	if (LastVRHandsSent >= 0.0 && Now - LastVRHandsSent < 1.0 / TNVRHandsDetail::SendRate)
	{
		return;
	}
	LastVRHandsSent = Now;
	const FTransform& ToWorld = GetActorTransform();
	const FVector LeftLocal = ToWorld.InverseTransformPosition(Left).GetClampedToMaxSize(TNVRHandsDetail::MaxHandDistance);
	const FVector RightLocal = ToWorld.InverseTransformPosition(Right).GetClampedToMaxSize(TNVRHandsDetail::MaxHandDistance);
	// El giro, respecto de la tortuga como la posición (el dueño la gira con la cabeza).
	const FRotator LeftRotLocal = ToWorld.InverseTransformRotation(LeftRotation.Quaternion()).Rotator();
	const FRotator RightRotLocal = ToWorld.InverseTransformRotation(RightRotation.Quaternion()).Rotator();
	const uint8 Valid = (bLocalVRHandValid[0] ? 1 : 0) | (bLocalVRHandValid[1] ? 2 : 0);
	if (HasAuthority())
	{
		RepVRHandLeft = LeftLocal;
		RepVRHandRight = RightLocal;
		RepVRHandRotLeft = LeftRotLocal;
		RepVRHandRotRight = RightRotLocal;
		RepVRHandsValid = Valid;
	}
	else
	{
		ServerSetVRHands(LeftLocal, RightLocal, Valid, LeftRotLocal, RightRotLocal);
	}
}

void ATortugaCharacter::ServerSetVRHands_Implementation(FVector_NetQuantize10 Left, FVector_NetQuantize10 Right, uint8 Valid,
	FRotator LeftRotation, FRotator RightRotation)
{
	RepVRHandLeft = Left.GetClampedToMaxSize(TNVRHandsDetail::MaxHandDistance);
	RepVRHandRight = Right.GetClampedToMaxSize(TNVRHandsDetail::MaxHandDistance);
	RepVRHandRotLeft = LeftRotation.GetNormalized();
	RepVRHandRotRight = RightRotation.GetNormalized();
	RepVRHandsValid = bVRPlayer ? (Valid & 3) : 0;
}

bool ATortugaCharacter::GetVRHandRotations(FQuat& OutLeft, FQuat& OutRight) const
{
	if (IsLocallyControlled())
	{
		if (!bVRViewActive)
		{
			return false;
		}
		OutLeft = LocalVRHandRot[0].Quaternion();
		OutRight = LocalVRHandRot[1].Quaternion();
		return true;
	}
	if (!bVRPlayer || RepVRHandsValid == 0)
	{
		return false;
	}
	const FTransform& ToWorld = GetActorTransform();
	OutLeft = ToWorld.TransformRotation(RepVRHandRotLeft.Quaternion());
	OutRight = ToWorld.TransformRotation(RepVRHandRotRight.Quaternion());
	return true;
}

bool ATortugaCharacter::GetVRHandTargets(FVector& OutLeft, FVector& OutRight, bool& bOutLeft, bool& bOutRight) const
{
	if (IsLocallyControlled())
	{
		if (!bVRViewActive)
		{
			return false;
		}
		OutLeft = LocalVRHand[0];
		OutRight = LocalVRHand[1];
		bOutLeft = bLocalVRHandValid[0];
		bOutRight = bLocalVRHandValid[1];
		return bOutLeft || bOutRight;
	}
	if (!bVRPlayer || RepVRHandsValid == 0)
	{
		return false;
	}
	const FTransform& ToWorld = GetActorTransform();
	OutLeft = ToWorld.TransformPosition(RepVRHandLeft);
	OutRight = ToWorld.TransformPosition(RepVRHandRight);
	bOutLeft = (RepVRHandsValid & 1) != 0;
	bOutRight = (RepVRHandsValid & 2) != 0;
	return true;
}

bool ATortugaCharacter::AreVRArmsFollowing() const
{
	const bool bVR = IsLocallyControlled() ? bVRViewActive : bVRPlayer;
	if (!bVR || ActiveEmoteIndex >= 0 || IsInShell() || bIsKnockedDown || bIsDead)
	{
		return false;
	}
	if (CarryComponent && (CarryComponent->IsCarrying() || CarryComponent->IsBeingCarried()))
	{
		return false;
	}
	const USkeletalMeshComponent* Body = GetMesh();
	return !(Body && Body->IsSimulatingPhysics());
}

ATN_InteractableBase* ATortugaCharacter::FindInteractableNearHand(const FVector& HandLocation)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNVRHandInteract), false, this);
	World->OverlapMultiByObjectType(Overlaps, HandLocation, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldDynamic),
		FCollisionShape::MakeSphere(VRHandReach), Params);
	ATN_InteractableBase* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		ATN_InteractableBase* Interactable = Cast<ATN_InteractableBase>(Overlap.GetActor());
		if (!Interactable || !Interactable->CanInteract(this))
		{
			continue;
		}
		// Lo que cuenta es lo cerca que está la mano de lo que toca (su colisión) o de su punto de interacción.
		FVector Point = Interactable->GetInteractionPointFor(this);
		float Distance = static_cast<float>(FVector::Dist(HandLocation, Point));
		if (const UPrimitiveComponent* Touched = Overlap.GetComponent())
		{
			FVector Closest;
			const float ToCollision = Touched->GetClosestPointOnCollision(HandLocation, Closest);
			if (ToCollision >= 0.f && ToCollision < Distance)
			{
				Distance = ToCollision;
				// Con la mano dentro de una colisión solo de consulta (la esfera de escaneo de metro y medio de un decorado que
				// se rebusca), el punto de la mano se vería siempre, aunque el decorado esté detrás de una pared: se mira su
				// punto de interacción.
				if (ToCollision > 0.f || Touched->GetCollisionEnabled() != ECollisionEnabled::QueryOnly)
				{
					Point = Closest;
				}
			}
		}
		// La mano se para en la pared, pero VRHandReach la pasa: lo que queda al otro lado no se toca.
		if (Distance <= VRHandReach && Distance < BestDistance && (!VRGrabComponent || VRGrabComponent->CanReach(Interactable, Point)))
		{
			BestDistance = Distance;
			Best = Interactable;
		}
	}
	return Best;
}

ATortugaCharacter::EVRGrip ATortugaCharacter::VRGripPressed(bool bRight, const FVector& HandLocation, bool bHeldItem)
{
	if (!IsLocallyControlled() || !bVRViewActive || bIsKnockedDown || bIsDead || IsInShell())
	{
		return EVRGrip::None;
	}
	if (bHeldItem)
	{
		// Nada que coger: el objeto que ya lleva en la aleta derecha (para lanzarlo o soltarlo al abrir la mano).
		return bRight && InventoryComponent && InventoryComponent->HasEquippedItem() ? EVRGrip::HeldItem : EVRGrip::None;
	}
	if (CarryComponent && CarryComponent->IsCarrying())
	{
		return EVRGrip::Partner;
	}
	// Un objeto del suelo o algo con lo que interactuar, al alcance de ESTA mano.
	if (ATN_InteractableBase* Touched = FindInteractableNearHand(HandLocation))
	{
		FocusedInteractable = Touched;
		// Lo coge el agarre: TryInteract no lo descarta como haría con el gatillo (#916).
		bVRGripInteract = true;
		TryInteract();
		bVRGripInteract = false;
		return EVRGrip::Touched;
	}
	// Un compañero en el caparazón o aturdido, al alcance de la mano y que no esté al otro lado de una pared.
	for (TActorIterator<ATortugaCharacter> It(GetWorld()); It; ++It)
	{
		const ATortugaCharacter* Other = *It;
		const UCapsuleComponent* Capsule = Other ? Other->GetCapsuleComponent() : nullptr;
		if (!Other || Other == this || !Capsule)
		{
			continue;
		}
		FVector Closest;
		const float Distance = TNVRHandsDetail::DistanceToCapsule(HandLocation, Other->GetActorLocation(),
			Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight(), Closest);
		if (Distance <= VRHandReach && (!VRGrabComponent || VRGrabComponent->CanReach(Other, Closest)))
		{
			return CarryComponent && CarryComponent->TryGrabNearest() ? EVRGrip::Partner : EVRGrip::None;
		}
	}
	return EVRGrip::None;
}

void ATortugaCharacter::VRGripReleased(EVRGrip Held, const FVector& HandVelocity)
{
	if (!IsLocallyControlled())
	{
		return;
	}
	// La velocidad de la mano respecto del cuerpo (ATN_VRRig): andando con la mano quieta no se lanza nada.
	const bool bSwing = TNVRMath::IsThrowSwing(HandVelocity, VRThrowSpeed);
	// Con impulso, lo lanzado sale hacia donde va la mano (llega al servidor antes que la acción).
	auto AimAlongHand = [this, &HandVelocity]()
	{
		SetLocalVRAim(HandVelocity.Rotation(), true);
		SendVRAimToServer();
	};
	const bool bHasItem = InventoryComponent && InventoryComponent->HasEquippedItem();
	switch (Held)
	{
	case EVRGrip::Partner:
		if (CarryComponent && CarryComponent->IsCarrying())
		{
			if (bSwing)
			{
				AimAlongHand();
				CarryComponent->RequestThrow();
			}
			else
			{
				CarryComponent->RequestDrop();
			}
		}
		break;
	case EVRGrip::Touched:
		// Fin de las interacciones de mantener (rebuscar); lo recién cogido se queda en la aleta si no se lanza.
		ReleaseInteract();
		if (bSwing && bHasItem && TNVRHandsDetail::IsThrownByGesture(InventoryComponent->GetEquippedItem().UseType))
		{
			AimAlongHand();
			TryUseEquippedItem();
		}
		break;
	case EVRGrip::HeldItem:
		if (bHasItem)
		{
			if (bSwing && TNVRHandsDetail::IsThrownByGesture(InventoryComponent->GetEquippedItem().UseType))
			{
				AimAlongHand();
				TryUseEquippedItem();
			}
			else if (!bSwing)
			{
				// Abrir la mano despacio: el objeto se suelta al suelo.
				ServerDropEquippedItem();
			}
		}
		break;
	default:
		break;
	}
}
