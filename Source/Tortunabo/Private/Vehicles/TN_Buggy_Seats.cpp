// ATN_Buggy: plazas (conductora y artillera) y tortugas visuales sentadas con el aspecto de cada ocupante.

#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyGunnerPawn.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_CosmeticLook.h"
#include "Core/TN_CosmeticsTypes.h"
#include "Player/TN_TurtleAnimInstance.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "AnimationRuntime.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

namespace TNBuggySeats
{
	constexpr int32 DriverIndex = 0;
	constexpr int32 GunnerIndex = 1;

	/** Giro de la tortuga sentada (el del constructor): su malla mira a +Y y así mira al morro. */
	const FRotator SeatedTurtleRotation(0.f, -90.f, 0.f);
	/** Hueso de la cadera de TotugaDemo_Rig y su sitio en la postura de referencia si faltara (unidades de malla). */
	const FName HipsBone(TEXT("Hips"));
	const FVector FallbackHipsMesh(0.f, -0.39f, 24.566f);
	/** Artillera noqueada: grados que cae hacia atrás, cm que se desliza hacia la cola y velocidades de caída y vuelta (1/s). */
	constexpr float KnockBackPitchDeg = 65.f;
	constexpr float KnockSlideBackCm = 15.f;
	constexpr float KnockFallPerSecond = 6.f;
	constexpr float KnockRecoverPerSecond = 2.5f;

	FTN_TurtleLook LookOf(const APlayerState* PlayerState)
	{
		FTN_TurtleLook Look;
		if (const ATN_CoopPlayerState* Coop = Cast<ATN_CoopPlayerState>(PlayerState))
		{
			Look.HelmetId = Coop->EquippedHelmetId;
			Look.SkinId = Coop->EquippedSkinId;
			Look.ShellId = Coop->EquippedShellId;
			Look.EyesId = Coop->EquippedEyesId;
		}
		return Look;
	}

	FString LookKey(const FTN_TurtleLook& Look)
	{
		return FString::Printf(TEXT("%s|%s|%s|%s"), *Look.HelmetId.ToString(), *Look.SkinId.ToString(),
			*Look.ShellId.ToString(), *Look.EyesId.ToString());
	}
}

bool ATN_Buggy::SeatController(AController* InController, ETNRallySeat Seat)
{
	if (!HasAuthority() || !InController)
	{
		return false;
	}
	if (GetSeatController(Seat) == InController)
	{
		return true;
	}
	if (!HasFreeSeat(Seat))
	{
		return false;
	}
	// Si ya iba en la otra plaza, la deja (sin pasar a la artillera al volante: se cambia de sitio).
	if (Seat == ETNRallySeat::Driver && GunnerController == InController)
	{
		SetGunnerSeat(nullptr);
	}
	else if (Seat == ETNRallySeat::Gunner && DriverController == InController)
	{
		InController->UnPossess();
		SetDriverSeat(nullptr);
	}

	if (Seat == ETNRallySeat::Driver)
	{
		InController->Possess(this);
		// PossessedBy ya ha llamado a SetDriverSeat; se repite por si el Possess de un controlador propio no lo hace.
		SetDriverSeat(InController);
		return Controller == InController;
	}
	SetGunnerSeat(InController);
	return GunnerController == InController;
}

void ATN_Buggy::UnseatController(AController* InController)
{
	if (!HasAuthority() || !InController)
	{
		return;
	}
	if (InController == GunnerController)
	{
		SetGunnerSeat(nullptr);
		return;
	}
	if (InController != DriverController)
	{
		return;
	}
	InController->UnPossess();
	SetDriverSeat(nullptr);
	// La artillera pasa a conducir.
	if (AController* Promoted = GunnerController)
	{
		SetGunnerSeat(nullptr);
		Promoted->Possess(this);
		SetDriverSeat(Promoted);
		UE_LOG(LogTNBuggy, Log, TEXT("%s: la artillera %s pasa a conducir"), *GetName(), *Promoted->GetName());
	}
}

AController* ATN_Buggy::GetSeatController(ETNRallySeat Seat) const
{
	return Seat == ETNRallySeat::Driver ? DriverController.Get() : GunnerController.Get();
}

bool ATN_Buggy::HasFreeSeat(ETNRallySeat Seat) const
{
	return GetSeatController(Seat) == nullptr;
}

void ATN_Buggy::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	SetDriverSeat(NewController);
}

void ATN_Buggy::UnPossessed()
{
	Super::UnPossessed();
	SetDriverSeat(nullptr);
	SteerRequest = 0.f;
	if (UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement())
	{
		Move->SetThrottleInput(0.f);
		Move->SetBrakeInput(0.f);
	}
}

void ATN_Buggy::SetDriverSeat(AController* NewDriver)
{
	if (!HasAuthority())
	{
		return;
	}
	DriverController = NewDriver;
	bDriverSeated = NewDriver != nullptr;
	DriverPlayerState = NewDriver ? NewDriver->PlayerState.Get() : nullptr;
	RefreshSeatVisuals(true);
	ForceNetUpdate();
}

void ATN_Buggy::SetGunnerSeat(AController* NewGunner)
{
	if (!HasAuthority())
	{
		return;
	}
	if (GunnerController && GunnerController != NewGunner)
	{
		if (GunnerPawn && GunnerController->GetPawn() == GunnerPawn)
		{
			GunnerController->UnPossess();
		}
		DestroyGunnerPawn();
	}
	GunnerController = NewGunner;
	if (NewGunner)
	{
		if (ATN_BuggyGunnerPawn* Pawn = SpawnGunnerPawn())
		{
			NewGunner->Possess(Pawn);
		}
	}
	bGunnerSeated = NewGunner != nullptr;
	GunnerPlayerState = NewGunner ? NewGunner->PlayerState.Get() : nullptr;
	RefreshSeatVisuals(true);
	ForceNetUpdate();
}

ATN_BuggyGunnerPawn* ATN_Buggy::SpawnGunnerPawn()
{
	if (GunnerPawn)
	{
		return GunnerPawn;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UClass* PawnClass = GunnerPawnClass ? GunnerPawnClass.Get() : ATN_BuggyGunnerPawn::StaticClass();
	GunnerPawn = World->SpawnActor<ATN_BuggyGunnerPawn>(PawnClass, GetActorTransform(), Params);
	if (GunnerPawn)
	{
		GunnerPawn->SetBuggy(this);
	}
	else
	{
		UE_LOG(LogTNBuggy, Error, TEXT("%s: no se ha podido crear el peón de la artillera"), *GetName());
	}
	return GunnerPawn;
}

void ATN_Buggy::DestroyGunnerPawn()
{
	if (GunnerPawn)
	{
		GunnerPawn->Destroy();
		GunnerPawn = nullptr;
	}
}

void ATN_Buggy::OnRep_Seats()
{
	RefreshSeatVisuals(true);
}

void ATN_Buggy::RefreshSeatVisuals(bool bForce)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	ApplySeatLook(TNBuggySeats::DriverIndex, bForce);
	ApplySeatLook(TNBuggySeats::GunnerIndex, bForce);
}

void ATN_Buggy::ApplySeatLook(int32 SeatIndex, bool bForce)
{
	if (!SeatTurtles.IsValidIndex(SeatIndex) || !SeatHelmets.IsValidIndex(SeatIndex))
	{
		return;
	}
	USkeletalMeshComponent* Turtle = SeatTurtles[SeatIndex];
	UStaticMeshComponent* Helmet = SeatHelmets[SeatIndex];
	const bool bDriver = SeatIndex == TNBuggySeats::DriverIndex;
	const bool bSeated = bDriver ? bDriverSeated : bGunnerSeated;
	if (!Turtle || !Helmet)
	{
		return;
	}
	Turtle->SetHiddenInGame(!bSeated, true);
	if (!bSeated)
	{
		return;
	}
	if (!Turtle->GetAnimInstance() || !Turtle->GetAnimInstance()->IsA<UTN_TurtleAnimInstance>())
	{
		if (USkeletalMesh* Wanted = TurtleMeshAsset.LoadSynchronous(); Wanted && Turtle->GetSkeletalMeshAsset() != Wanted)
		{
			Turtle->SetSkeletalMesh(Wanted);
		}
		Turtle->SetAnimInstanceClass(UTN_TurtleAnimInstance::StaticClass());
		FitTurtle(SeatIndex);
		bForce = true;
	}
	const FTN_TurtleLook Look = TNBuggySeats::LookOf(bDriver ? DriverPlayerState.Get() : GunnerPlayerState.Get());
	const FString Key = TNBuggySeats::LookKey(Look);
	AppliedLookKeys.SetNum(2);
	if (!bForce && AppliedLookKeys[SeatIndex] == Key)
	{
		return;
	}
	AppliedLookKeys[SeatIndex] = Key;
	TArray<TObjectPtr<UMaterialInterface>>& Defaults = bDriver ? DriverTurtleDefaults : GunnerTurtleDefaults;
	UTN_CosmeticLook::ApplyLook(this, Turtle, Helmet, Look, Defaults);
}

void ATN_Buggy::FitTurtle(int32 SeatIndex)
{
	USkeletalMeshComponent* Turtle = SeatTurtles.IsValidIndex(SeatIndex) ? SeatTurtles[SeatIndex].Get() : nullptr;
	const USkeletalMesh* TurtleMesh = Turtle ? Turtle->GetSkeletalMeshAsset() : nullptr;
	if (!TurtleMesh || SeatIndex < 0 || SeatIndex >= UE_ARRAY_COUNT(SeatTurtleBase))
	{
		return;
	}
	const bool bDriver = SeatIndex == TNBuggySeats::DriverIndex;
	const FVector Seat = bDriver ? GetBodySocketLocal(DriverSeatSocket, DriverSeatLocal) : GetBodySocketLocal(GunnerSeatSocket, GunnerSeatLocal);
	// La cadera de la malla (postura de referencia) cae en el socket del asiento, que está medido en la tortuga sentada
	// (Art/Source/Vehicles/Buggy/turtle_pose.py); UTN_BuggyRiderAnimComponent dobla las piernas sobre ella.
	const FReferenceSkeleton& Ref = TurtleMesh->GetRefSkeleton();
	const int32 Hips = Ref.FindBoneIndex(TNBuggySeats::HipsBone);
	const FVector HipsMesh = Hips != INDEX_NONE ? FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Hips).GetLocation()
		: TNBuggySeats::FallbackHipsMesh;
	const FVector HipsLocal = TNBuggySeats::SeatedTurtleRotation.RotateVector(HipsMesh * SeatedTurtleScale);
	SeatTurtleBase[SeatIndex] = Seat - HipsLocal;
	Turtle->SetRelativeScale3D(FVector(SeatedTurtleScale));
	Turtle->SetRelativeLocationAndRotation(SeatTurtleBase[SeatIndex], TNBuggySeats::SeatedTurtleRotation);
}

void ATN_Buggy::UpdateGunnerKnockPose(float DeltaSeconds)
{
	USkeletalMeshComponent* Turtle = SeatTurtles.IsValidIndex(TNBuggySeats::GunnerIndex) ? SeatTurtles[TNBuggySeats::GunnerIndex].Get() : nullptr;
	if (!Turtle || !Turret)
	{
		return;
	}
	// Solo lee estado replicado (hora de fin del noqueo): vale igual en el servidor escucha y en los clientes.
	const float Target = bGunnerSeated && Turret->IsGunnerKnocked() ? 1.f : 0.f;
	if (GunnerKnockLean01 == Target)
	{
		return;
	}
	const float Speed = Target > GunnerKnockLean01 ? TNBuggySeats::KnockFallPerSecond : TNBuggySeats::KnockRecoverPerSecond;
	GunnerKnockLean01 = FMath::FInterpConstantTo(GunnerKnockLean01, Target, DeltaSeconds, Speed);
	const float Ease = FMath::InterpEaseOut(0.f, 1.f, GunnerKnockLean01, 2.f);
	// Cabeceo positivo en el marco del chasis: la cabeza va hacia la cola. Gira sobre la cadera (el socket del asiento).
	// UTN_BuggyRiderAnimComponent anima los huesos (UTN_TurtleAnimInstance), no el componente: no se pisan.
	const FQuat Fall = FRotator(TNBuggySeats::KnockBackPitchDeg * Ease, 0.f, 0.f).Quaternion();
	const FVector Base = SeatTurtleBase[TNBuggySeats::GunnerIndex];
	const FVector Hip = GetBodySocketLocal(GunnerSeatSocket, GunnerSeatLocal);
	Turtle->SetRelativeLocationAndRotation(Hip + Fall.RotateVector(Base - Hip) - FVector(TNBuggySeats::KnockSlideBackCm * Ease, 0.f, 0.f),
		Fall * TNBuggySeats::SeatedTurtleRotation.Quaternion());
}
