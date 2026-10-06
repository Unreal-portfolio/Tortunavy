#include "Player/TN_GhostCameraModifier.h"
#include "Player/TN_SpectatorGhost.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Character.h"
#include "GameFramework/SpectatorPawn.h"
#include "VR/TN_VRMath.h"
#include "VR/TN_VRMode.h"
#include "World/Beach/TN_BeachSandWorm.h"

namespace TNGhostCameraDetail
{
	/** Altura (cm) sobre el centro de la tortuga del punto al que mira la órbita. */
	constexpr float FocusHeight = 55.f;
	/** Distancia de la órbita (cm): mínima, máxima y la de partida si no se sabe otra. */
	constexpr float MinDistance = 170.f;
	constexpr float MaxDistance = 900.f;
	constexpr float DefaultDistance = 420.f;
	/** Cabeceo de la órbita: de mirar bastante hacia abajo a mirar un poco hacia arriba. */
	constexpr float MinPitch = -70.f;
	constexpr float MaxPitch = 30.f;
	/** Radio de la esfera con la que la cámara libre tantea las paredes, y altura mínima sobre el suelo que tiene debajo. */
	constexpr float ProbeRadius = 14.f;
	constexpr float MinGroundClearance = 40.f;
	constexpr float FreeFOV = 80.f;
	/** Segundos de la mezcla entre la cámara fija y la libre. */
	constexpr float BlendSeconds = 0.45f;
	/** Cada paso de rueda acerca o aleja este factor. */
	constexpr float ZoomStepFactor = 0.88f;
	/** Segundos de la mezcla hacia la vista lejana del gusano (la de la cámara de la tortuga comida) y de vuelta. */
	constexpr float WormBlendSeconds = 0.6f;

	/** Cámara libre (true) o fija de cada jugador local, para la próxima vez que sea fantasma (todos los modos y mapas). */
	TMap<TWeakObjectPtr<const UObject>, bool>& FreePreference()
	{
		static TMap<TWeakObjectPtr<const UObject>, bool> Preference;
		return Preference;
	}

	const UObject* PreferenceKey(const APlayerCameraManager* Camera)
	{
		const APlayerController* PC = Camera ? Camera->GetOwningPlayerController() : nullptr;
		return PC ? static_cast<const UObject*>(PC->GetLocalPlayer()) : nullptr;
	}
}

UTN_GhostCameraModifier::UTN_GhostCameraModifier()
{
	// Antes que los temblores: la órbita sustituye la vista y los golpes siguen sumándose encima.
	Priority = 0;
}

void UTN_GhostCameraModifier::SetGhost(ATN_SpectatorGhost* InGhost)
{
	Ghost = InGhost;
	if (const UObject* Key = TNGhostCameraDetail::PreferenceKey(CameraOwner))
	{
		if (const bool* Saved = TNGhostCameraDetail::FreePreference().Find(Key))
		{
			bFree = *Saved;
		}
	}
	FreeBlend = bFree ? 1.f : 0.f;
	bOrbitReady = false;
	bFocusReady = false;
	bHasLastView = false;
	bReviveViewReady = false;
	WormBlend = 0.f;
	bVRYawReady = false;
	bVRViewActive = false;
}

void UTN_GhostCameraModifier::AddOrbitInput(float YawDegrees, float PitchDegrees)
{
	if (!bFree || !bOrbitReady)
	{
		return;
	}
	OrbitYaw = FRotator3f::NormalizeAxis(OrbitYaw + YawDegrees);
	OrbitPitch = FMath::Clamp(OrbitPitch + PitchDegrees, TNGhostCameraDetail::MinPitch, TNGhostCameraDetail::MaxPitch);
}

void UTN_GhostCameraModifier::AddZoom(float Steps)
{
	if (!bFree)
	{
		return;
	}
	TargetDistance = FMath::Clamp(TargetDistance * FMath::Pow(TNGhostCameraDetail::ZoomStepFactor, Steps), TNGhostCameraDetail::MinDistance,
		TNGhostCameraDetail::MaxDistance);
}

void UTN_GhostCameraModifier::ToggleFree()
{
	bFree = !bFree;
	// A la libre: la órbita arranca donde está la cámara fija, sin salto.
	if (bFree)
	{
		bOrbitReady = false;
	}
	if (const UObject* Key = TNGhostCameraDetail::PreferenceKey(CameraOwner))
	{
		TNGhostCameraDetail::FreePreference().Add(Key, bFree);
	}
}

APawn* UTN_GhostCameraModifier::GetFollowedPawn() const
{
	APawn* Pawn = CameraOwner ? Cast<APawn>(CameraOwner->GetViewTarget()) : nullptr;
	const APlayerController* PC = CameraOwner ? CameraOwner->GetOwningPlayerController() : nullptr;
	if (!Pawn || !PC || Pawn == PC->GetPawn() || Pawn->IsA<ASpectatorPawn>())
	{
		return nullptr;
	}
	const APlayerState* PS = Pawn->GetPlayerState();
	return (PS && PS != PC->PlayerState) ? Pawn : nullptr;
}

FVector UTN_GhostCameraModifier::ResolveCollision(const FVector& From, const FVector& To, const AActor* IgnoreActor) const
{
	UWorld* World = CameraOwner ? CameraOwner->GetWorld() : nullptr;
	if (!World)
	{
		return To;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNGhostCamera), false);
	if (IgnoreActor)
	{
		Params.AddIgnoredActor(IgnoreActor);
	}
	if (const APlayerController* PC = CameraOwner->GetOwningPlayerController())
	{
		if (const ASpectatorPawn* Spectator = PC->GetSpectatorPawn())
		{
			Params.AddIgnoredActor(Spectator);
		}
	}
	FVector Result = To;
	FHitResult Hit;
	if (World->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(TNGhostCameraDetail::ProbeRadius), Params))
	{
		Result = Hit.Location;
	}
	// Nunca por debajo del suelo que tiene debajo (por si el terreno no para el canal de cámara).
	FHitResult Floor;
	if (World->LineTraceSingleByChannel(Floor, Result + FVector(0.0, 0.0, 30.0), Result - FVector(0.0, 0.0, TNGhostCameraDetail::MinGroundClearance),
		ECC_Visibility, Params))
	{
		Result.Z = FMath::Max(Result.Z, Floor.ImpactPoint.Z + TNGhostCameraDetail::MinGroundClearance);
	}
	return Result;
}

bool UTN_GhostCameraModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	ATN_SpectatorGhost* GhostActor = Ghost.Get();
	const APlayerController* PC = CameraOwner ? CameraOwner->GetOwningPlayerController() : nullptr;
	// Sin fantasma, o ya con tortuga propia (acaba de salir del huevo): la cámara de siempre. Simulado también: una cámara que
	// orbita sola no sigue a la cabeza y marea; se ve desde la cámara del jugador seguido (Docs/Modo_VR.md). Con gafas, la
	// vista propia de más abajo (#646).
	if (!GhostActor || !PC || !GhostActor->IsActiveGhost() || PC->GetPawn() || TNVR::IsSimulated())
	{
		bVRViewActive = false;
		return false;
	}
	// Con un cambio de ViewTarget en curso se llama dos veces por fotograma: el mismo resultado para las dos.
	if (LastFrame == GFrameCounter)
	{
		if (bOverrodeThisFrame)
		{
			InOutPOV = FrameView;
		}
		return false;
	}
	LastFrame = GFrameCounter;
	bOverrodeThisFrame = false;
	bVRViewActive = false;
	const FMinimalViewInfo FixedView = InOutPOV;

	// ── Con gafas (#646): sigue la posición de la tortuga seguida y gira solo con la cabeza propia ──
	// La base de la vista mira a un rumbo fijo y el motor le pone encima la pose de las gafas. Volviendo a la vida no se
	// mueve la cámara (una cámara que se mueve sola marea): la tapa la cáscara. Sin nadie a quien seguir, la de siempre.
	if (TNVR::IsHeadset())
	{
		APawn* FollowedPawn = GhostActor->IsReviving() ? nullptr : GetFollowedPawn();
		if (!FollowedPawn)
		{
			return false;
		}
		if (!bVRYawReady)
		{
			VRYaw = static_cast<float>(FixedView.Rotation.Yaw);
			bVRYawReady = true;
		}
		const FVector VRFocus = FollowedPawn->GetActorLocation() + FVector(0.0, 0.0, TNGhostCameraDetail::FocusHeight);
		const FVector Wish = TNVRMath::GhostViewLocation(FollowedPawn->GetActorLocation(), VRYaw);
		FMinimalViewInfo Result = FixedView;
		Result.Location = ResolveCollision(VRFocus, Wish, FollowedPawn);
		Result.Rotation = TNVRMath::GhostViewRotation(VRYaw);
		InOutPOV = Result;
		FrameView = Result;
		bOverrodeThisFrame = true;
		LastView = Result;
		bHasLastView = true;
		bVRViewActive = true;
		return false;
	}

	// ── Volviendo a la vida: la cámara se aparta un poco y mira el vuelo del fantasma hasta el huevo ──
	if (GhostActor->IsReviving())
	{
		if (!bReviveViewReady)
		{
			ReviveFrom = bHasLastView ? LastView : FixedView;
			ReviveLookAt = GhostActor->GetVisualLocation();
			ReviveElapsed = 0.f;
			bReviveViewReady = true;
		}
		ReviveElapsed += DeltaTime;
		const FVector EggPoint = GhostActor->GetReviveInfo().EggLocation;
		FVector Away = (ReviveFrom.Location - EggPoint).GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = -ReviveFrom.Rotation.Vector().GetSafeNormal2D();
		}
		const float Pull = FMath::SmoothStep(0.f, 0.7f, ReviveElapsed);
		const FVector Wish = ReviveFrom.Location + (Away * 240.0 + FVector(0.0, 0.0, 110.0)) * Pull;
		const FVector CameraPoint = ResolveCollision(ReviveFrom.Location, Wish, nullptr);
		ReviveLookAt = FMath::VInterpTo(ReviveLookAt, GhostActor->GetVisualLocation(), DeltaTime, 7.f);
		FMinimalViewInfo Result = ReviveFrom;
		Result.Location = CameraPoint;
		Result.Rotation = (ReviveLookAt - CameraPoint).Rotation();
		InOutPOV = Result;
		FrameView = Result;
		bOverrodeThisFrame = true;
		LastView = Result;
		bHasLastView = true;
		return false;
	}
	bReviveViewReady = false;

	// ── Espectador: fija (la cámara de la tortuga seguida, tal cual) o libre (órbita propia), con mezcla ──
	APawn* Followed = GetFollowedPawn();
	const float Wanted = (bFree || !Followed) ? 1.f : 0.f;
	FreeBlend = FMath::FInterpConstantTo(FreeBlend, Wanted, DeltaTime, 1.f / TNGhostCameraDetail::BlendSeconds);

	// Punto al que mira la órbita: la tortuga seguida (suavizado, también al cambiar de tortuga); sin ella, el último.
	if (Followed)
	{
		const FVector Desired = Followed->GetActorLocation() + FVector(0.0, 0.0, TNGhostCameraDetail::FocusHeight);
		Focus = bFocusReady ? FMath::VInterpTo(Focus, Desired, DeltaTime, 9.f) : Desired;
		bFocusReady = true;
	}
	else if (!bFocusReady)
	{
		Focus = FixedView.Location + FixedView.Rotation.Vector() * TNGhostCameraDetail::DefaultDistance;
		bFocusReady = true;
	}
	if (!bOrbitReady)
	{
		// La órbita empieza donde está la cámara, para que pasar a la libre no dé un salto.
		const FVector ToFocus = Focus - FixedView.Location;
		const FRotator Aim = ToFocus.Rotation();
		OrbitYaw = static_cast<float>(Aim.Yaw);
		OrbitPitch = FMath::Clamp(static_cast<float>(FRotator::NormalizeAxis(Aim.Pitch)), TNGhostCameraDetail::MinPitch, TNGhostCameraDetail::MaxPitch);
		Distance = FMath::Clamp(static_cast<float>(ToFocus.Size()), TNGhostCameraDetail::MinDistance, TNGhostCameraDetail::MaxDistance);
		TargetDistance = Distance;
		bOrbitReady = true;
	}
	Distance = FMath::FInterpTo(Distance, TargetDistance, DeltaTime, 8.f);

	if (FreeBlend > 0.f)
	{
		const FRotator OrbitRotation(OrbitPitch, OrbitYaw, 0.f);
		const FVector Wish = Focus - OrbitRotation.Vector() * Distance;
		const FVector CameraPoint = ResolveCollision(Focus, Wish, Followed);
		const float BlendAlpha = FMath::SmoothStep(0.f, 1.f, FreeBlend);
		FMinimalViewInfo Result = FixedView;
		Result.Location = FMath::Lerp(FixedView.Location, CameraPoint, BlendAlpha);
		Result.Rotation = FQuat::Slerp(FixedView.Rotation.Quaternion(), OrbitRotation.Quaternion(), BlendAlpha).Rotator();
		Result.FOV = FMath::Lerp(FixedView.FOV, TNGhostCameraDetail::FreeFOV, BlendAlpha);
		InOutPOV = Result;
		FrameView = Result;
		bOverrodeThisFrame = true;
	}

	// ── Un gusano de arena se come a la seguida: la vista lejana de la escena (la que ve ella), con el gusano entero ──
	// Con la fija, la cámara de la tortuga se queda dentro de la boca; con la libre, la órbita (9 m como mucho) no lo abarca.
	ATN_BeachSandWorm* Worm = Followed ? ATN_BeachSandWorm::FindEating(Cast<ACharacter>(Followed)) : nullptr;
	FVector WormAt = FVector::ZeroVector;
	FRotator WormAim = FRotator::ZeroRotator;
	const bool bWormView = Worm && Worm->GetSpectatorView(bHasLastView ? LastView.Location : InOutPOV.Location, WormAt, WormAim);
	if (bWormView)
	{
		WormLocation = WormAt;
		WormRotation = WormAim;
	}
	WormBlend = FMath::FInterpConstantTo(WormBlend, bWormView ? 1.f : 0.f, DeltaTime, 1.f / TNGhostCameraDetail::WormBlendSeconds);
	if (WormBlend > 0.f)
	{
		const float BlendAlpha = FMath::SmoothStep(0.f, 1.f, WormBlend);
		FMinimalViewInfo Result = InOutPOV;
		Result.Location = FMath::Lerp(InOutPOV.Location, WormLocation, BlendAlpha);
		Result.Rotation = FQuat::Slerp(InOutPOV.Rotation.Quaternion(), WormRotation.Quaternion(), BlendAlpha).Rotator();
		InOutPOV = Result;
		FrameView = Result;
		bOverrodeThisFrame = true;
	}
	LastView = InOutPOV;
	bHasLastView = true;
	return false;
}
