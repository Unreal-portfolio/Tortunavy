#include "Rally/TN_RallyCameraDirector.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Rally/TN_RallyGameState.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyPodium.h"
#include "Vehicles/TN_Buggy.h"
#include "VR/TN_VRMode.h"

namespace TNRallyCameraDirectorDetail
{
	/** Fundidos (s) sin VR; con VR siempre corte seco (TNVR::ViewBlendTime). */
	constexpr float ToPodiumBlend = 0.6f;
	constexpr float ToBuggyBlend = 0.4f;
	constexpr float BackToOwnBlend = 0.3f;
	/** Suavizado del dron (1/s) y FOV de las cámaras propias. */
	constexpr float DroneFollowSpeed = 2.5f;
	constexpr float SideShotFov = 70.f;
	constexpr float PodiumFov = 60.f;
	/** Ojos de la conductora sobre su asiento (VR: espectador desde el asiento del buggy seguido; cm). */
	constexpr double SeatEyeHeightCm = 75.0;

	FRotator LookAt(const FVector& From, const FVector& To)
	{
		return (To - From).Rotation();
	}
}

UTN_RallyCameraDirector::UTN_RallyCameraDirector()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(false);
}

APlayerController* UTN_RallyCameraDirector::GetPlayer() const
{
	return Cast<APlayerController>(GetOwner());
}

bool UTN_RallyCameraDirector::IsVR() const
{
	return TNVR::KeepFirstPersonView();
}

const FTNRallyStanding* UTN_RallyCameraDirector::FindMine(const ATN_RallyGameState& RallyState) const
{
	const APlayerController* Player = GetPlayer();
	return Player ? RallyState.FindStandingForPlayer(Player->PlayerState) : nullptr;
}

void UTN_RallyCameraDirector::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const APlayerController* Player = GetPlayer();
	const UWorld* World = GetWorld();
	const ATN_RallyGameState* RallyState = World ? World->GetGameState<ATN_RallyGameState>() : nullptr;
	if (!Player || !Player->IsLocalController() || !RallyState)
	{
		return;
	}
	const double RealNow = World->GetRealTimeSeconds();
	const FTNRallyStanding* Mine = FindMine(*RallyState);
	TrackFinish(Mine, RealNow);

	TNRallyCamera::FShotInput In;
	In.Phase = RallyState->Phase;
	In.bSeated = Mine != nullptr;
	In.bFinished = Mine && Mine->bFinished;
	In.SecondsSinceFinish = FinishSeenRealTime >= 0.0 ? RealNow - FinishSeenRealTime : -1.0;
	In.bVR = IsVR();
	const TNRallyCamera::EShot Wanted = TNRallyCamera::DecideShot(In);
	if (Wanted != Shot)
	{
		SwitchShot(Wanted, Mine);
	}
	switch (Shot)
	{
	case TNRallyCamera::EShot::FinishSide: UpdateFinishSide(); break;
	case TNRallyCamera::EShot::Podium: UpdatePodium(RealNow); break;
	case TNRallyCamera::EShot::Spectate: UpdateSpectate(*RallyState, DeltaTime); break;
	default: break;
	}
}

void UTN_RallyCameraDirector::TrackFinish(const FTNRallyStanding* Mine, double RealNow)
{
	const bool bFinished = Mine && Mine->bFinished;
	if (!bFinished)
	{
		FinishSeenRealTime = -1.0;
		bSawStanding = Mine != nullptr;
		return;
	}
	if (FinishSeenRealTime < 0.0)
	{
		// Si ya estaba en meta la primera vez que se ve su fila (entró tarde), nada de plano de llegada: al espectador.
		FinishSeenRealTime = bSawStanding ? RealNow : RealNow - 1000.0;
	}
	bSawStanding = true;
}

void UTN_RallyCameraDirector::SwitchShot(TNRallyCamera::EShot NewShot, const FTNRallyStanding* Mine)
{
	const TNRallyCamera::EShot Previous = Shot;
	Shot = NewShot;
	if (Previous == TNRallyCamera::EShot::FinishSide)
	{
		SetSlowMotion(false);
	}
	UE_LOG(LogTNRally, Log, TEXT("[RallyCamera] %s: plano %d -> %d."), *GetNameSafe(GetOwner()), static_cast<int32>(Previous),
		static_cast<int32>(NewShot));
	switch (NewShot)
	{
	case TNRallyCamera::EShot::Own: ReturnToOwnView(); break;
	case TNRallyCamera::EShot::FinishSide: BeginFinishSide(Mine); break;
	case TNRallyCamera::EShot::Podium:
		ViewCamera(TNVR::ViewBlendTime(TNRallyCameraDirectorDetail::ToPodiumBlend));
		break;
	case TNRallyCamera::EShot::Spectate:
		ViewedActor.Reset();
		bHasSpectateTarget = false;
		break;
	}
}

void UTN_RallyCameraDirector::ReturnToOwnView()
{
	SetSlowMotion(false);
	if (!bControlling)
	{
		return;
	}
	bControlling = false;
	ViewedActor.Reset();
	bHasSpectateTarget = false;
	if (APlayerController* Player = GetPlayer())
	{
		AActor* Own = Player->GetPawn() ? static_cast<AActor*>(Player->GetPawn()) : static_cast<AActor*>(Player);
		Player->SetViewTargetWithBlend(Own, TNVR::ViewBlendTime(TNRallyCameraDirectorDetail::BackToOwnBlend));
	}
}

void UTN_RallyCameraDirector::BeginFinishSide(const FTNRallyStanding* Mine)
{
	APawn* Vehicle = Mine ? Mine->Vehicle.Get() : nullptr;
	SideShotVehicle = Vehicle;
	ACameraActor* Cam = EnsureCamera();
	if (!Vehicle || !Cam)
	{
		return;
	}
	const FVector Where = TNRallyCamera::FinishSideLocation(Vehicle->GetActorLocation(), Vehicle->GetActorForwardVector());
	Cam->SetActorLocationAndRotation(Where, TNRallyCameraDirectorDetail::LookAt(Where, Vehicle->GetActorLocation()));
	Cam->GetCameraComponent()->SetFieldOfView(TNRallyCameraDirectorDetail::SideShotFov);
	ViewCamera(0.f);
	const UWorld* World = GetWorld();
	const bool bStandalone = World->GetNetMode() == NM_Standalone;
	const bool bHostsWorld = bStandalone || World->GetNetMode() == NM_ListenServer;
	SetSlowMotion(TNRallyCamera::CanUseSlowMotion(IsVR(), bStandalone, bHostsWorld ? World->GetNumPlayerControllers() : MAX_int32));
}

void UTN_RallyCameraDirector::UpdateFinishSide()
{
	// Cámara quieta junto a la meta que sigue al buggy con la mirada.
	const APawn* Vehicle = SideShotVehicle.Get();
	if (Camera && Vehicle)
	{
		Camera->SetActorRotation(TNRallyCameraDirectorDetail::LookAt(Camera->GetActorLocation(), Vehicle->GetActorLocation()));
	}
}

void UTN_RallyCameraDirector::UpdatePodium(double RealNow)
{
	const ATN_RallyPodium* Podium = ATN_RallyPodium::Find(GetWorld());
	ACameraActor* Cam = EnsureCamera();
	if (!Podium || !Cam)
	{
		return;
	}
	const TNRallyCamera::FPodiumFrame Frame = Podium->GetFrame();
	const FVector Where = TNRallyCamera::PodiumCameraLocation(Frame, IsVR(), RealNow);
	Cam->SetActorLocationAndRotation(Where, TNRallyCameraDirectorDetail::LookAt(Where, TNRallyCamera::PodiumLookAt(Frame)));
	Cam->GetCameraComponent()->SetFieldOfView(TNRallyCameraDirectorDetail::PodiumFov);
	if (!bControlling || ViewedActor.Get() != Cam)
	{
		ViewCamera(TNVR::ViewBlendTime(TNRallyCameraDirectorDetail::ToPodiumBlend));
	}
}

TArray<const FTNRallyStanding*> UTN_RallyCameraDirector::RacingStandings(const ATN_RallyGameState& RallyState) const
{
	TArray<const FTNRallyStanding*> Racing;
	for (const FTNRallyStanding& Entry : RallyState.Standings)
	{
		if (Entry.Vehicle && !Entry.bFinished && !Entry.bRetired)
		{
			Racing.Add(&Entry);
		}
	}
	return Racing;
}

void UTN_RallyCameraDirector::CycleSpectate(int32 Delta)
{
	const UWorld* World = GetWorld();
	const ATN_RallyGameState* RallyState = World ? World->GetGameState<ATN_RallyGameState>() : nullptr;
	if (Shot != TNRallyCamera::EShot::Spectate || !RallyState)
	{
		return;
	}
	const TArray<const FTNRallyStanding*> Racing = RacingStandings(*RallyState);
	SpectateSlot = TNRallyCamera::CycleSpectate(SpectateSlot, Delta, Racing.Num(), !IsVR());
	SpectateTeam = Racing.IsValidIndex(SpectateSlot) ? Racing[SpectateSlot]->TeamIndex : INDEX_NONE;
}

void UTN_RallyCameraDirector::UpdateSpectate(const ATN_RallyGameState& RallyState, float DeltaTime)
{
	const TArray<const FTNRallyStanding*> Racing = RacingStandings(RallyState);
	// Se sigue al mismo equipo aunque cambie de puesto.
	const int32 TeamSlot = Racing.IndexOfByPredicate([this](const FTNRallyStanding* Entry) { return Entry->TeamIndex == SpectateTeam; });
	const bool bWasDrone = SpectateSlot >= 0 && SpectateSlot == Racing.Num();
	SpectateSlot = TeamSlot != INDEX_NONE ? TeamSlot : TNRallyCamera::ClampSpectate(bWasDrone ? Racing.Num() : SpectateSlot,
		Racing.Num(), !IsVR());
	// Sin nadie corriendo no hay a quién mirar (ni líder para el dron): el podio.
	bHasSpectateTarget = SpectateSlot != INDEX_NONE && Racing.Num() > 0;
	bDrone = bHasSpectateTarget && SpectateSlot == Racing.Num();
	if (!bHasSpectateTarget)
	{
		UpdatePodium(GetWorld()->GetRealTimeSeconds());
		return;
	}
	if (bDrone)
	{
		SpectateTeam = INDEX_NONE;
		ViewDrone(*Racing[0]->Vehicle, DeltaTime);
		return;
	}
	SpectateTeam = Racing[SpectateSlot]->TeamIndex;
	ViewBuggy(Racing[SpectateSlot]->Vehicle);
}

void UTN_RallyCameraDirector::ViewBuggy(APawn* Vehicle)
{
	APlayerController* Player = GetPlayer();
	if (!Player || !Vehicle)
	{
		return;
	}
	if (!IsVR())
	{
		// La cámara de persecución del propio buggy seguido.
		if (ViewedActor.Get() != Vehicle)
		{
			bControlling = true;
			ViewedActor = Vehicle;
			Player->SetViewTargetWithBlend(Vehicle, TNRallyCameraDirectorDetail::ToBuggyBlend);
		}
		return;
	}
	// VR: desde el asiento de la conductora del buggy seguido, pegada a él (sin travelling propio).
	ACameraActor* Cam = EnsureCamera();
	if (!Cam || (bCameraAttached && Cam->GetAttachParentActor() == Vehicle))
	{
		return;
	}
	Cam->AttachToActor(Vehicle, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	Cam->SetActorRelativeLocation(ATN_Buggy::DriverSeatLocal + FVector(0.0, 0.0, TNRallyCameraDirectorDetail::SeatEyeHeightCm));
	Cam->SetActorRelativeRotation(FRotator::ZeroRotator);
	bCameraAttached = true;
	ViewCamera(0.f);
}

void UTN_RallyCameraDirector::ViewDrone(const APawn& Leader, float DeltaTime)
{
	ACameraActor* Cam = EnsureCamera();
	if (!Cam)
	{
		return;
	}
	const FVector Target = TNRallyCamera::DroneLocation(Leader.GetActorLocation(), Leader.GetActorForwardVector());
	const bool bJustArrived = !bControlling || ViewedActor.Get() != Cam;
	const FVector Where = bJustArrived ? Target
		: TNRallyCamera::SmoothFollow(Cam->GetActorLocation(), Target, DeltaTime, TNRallyCameraDirectorDetail::DroneFollowSpeed);
	Cam->SetActorLocationAndRotation(Where, TNRallyCameraDirectorDetail::LookAt(Where, Leader.GetActorLocation()));
	if (bJustArrived)
	{
		ViewCamera(TNRallyCameraDirectorDetail::ToBuggyBlend);
	}
}

ACameraActor* UTN_RallyCameraDirector::EnsureCamera()
{
	if (Camera)
	{
		return Camera;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = GetOwner();
	Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
	if (Camera)
	{
		// Cámara solo de esta máquina.
		Camera->SetReplicates(false);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
	}
	return Camera;
}

void UTN_RallyCameraDirector::ViewCamera(float BlendSeconds)
{
	APlayerController* Player = GetPlayer();
	ACameraActor* Cam = EnsureCamera();
	if (!Player || !Cam)
	{
		return;
	}
	if (bCameraAttached && Shot != TNRallyCamera::EShot::Spectate)
	{
		Cam->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		bCameraAttached = false;
	}
	bControlling = true;
	ViewedActor = Cam;
	Player->SetViewTargetWithBlend(Cam, BlendSeconds);
}

void UTN_RallyCameraDirector::SetSlowMotion(bool bEnable)
{
	if (bSlowMotion == bEnable)
	{
		return;
	}
	bSlowMotion = bEnable;
	UGameplayStatics::SetGlobalTimeDilation(this, bEnable ? TNRallyCamera::SlowMotionDilation : 1.f);
}

void UTN_RallyCameraDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetSlowMotion(false);
	if (Camera)
	{
		Camera->Destroy();
		Camera = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}
