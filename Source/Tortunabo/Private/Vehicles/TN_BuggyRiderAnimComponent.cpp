// UTN_BuggyRiderAnimComponent (la lógica pura, TNRiderAnim, en TN_BuggyRiderAnimLogic.cpp). Ver TN_BuggyRiderAnimComponent.h.

#include "Vehicles/TN_BuggyRiderAnimComponent.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Player/TN_ProcAnimInstance.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "ReferenceSkeleton.h"

// ─────────────────────────────────────────────────────────────────────────────
// Pose en el espacio de la malla
// ─────────────────────────────────────────────────────────────────────────────

#if !UE_BUILD_SHIPPING
namespace TNRiderAnimDebug
{
	/** Pose del salto sin saltar, para medirla con TN.Rally.DebugTurretFit (#435). */
	TAutoConsoleVariable<int32> CVarForceAirborne(TEXT("TN.Rally.DebugRiderAirborne"), 0,
		TEXT("Rally (fuera de Shipping): 1 = las tortugas del buggy se ponen como en el aire (encogidas y la artillera agarrada)."));
}
#endif

namespace TNRiderAnimPose
{
	/** Ejes de la malla de la tortuga: su izquierda +X, delante +Y, arriba +Z. */
	const FVector MeshLeft(1.0, 0.0, 0.0);
	const FVector MeshForward(0.0, 1.0, 0.0);
	const FVector MeshUp(0.0, 0.0, 1.0);

	/** Huesos del esqueleto Mixamo de TotugaDemo_Rig que mueve la animación (INDEX_NONE si faltan). */
	struct FRiderBones
	{
		int32 Hips = INDEX_NONE;
		int32 Spine = INDEX_NONE;
		int32 Neck = INDEX_NONE;
		int32 Head = INDEX_NONE;
		int32 LeftArm = INDEX_NONE;
		int32 LeftHand = INDEX_NONE;
		int32 RightArm = INDEX_NONE;
		int32 RightHand = INDEX_NONE;
		/** Postura sentada: hombros (raíz de cada brazo), antebrazos y piernas. */
		int32 LeftShoulder = INDEX_NONE;
		int32 RightShoulder = INDEX_NONE;
		int32 LeftForeArm = INDEX_NONE;
		int32 RightForeArm = INDEX_NONE;
		int32 LeftUpLeg = INDEX_NONE;
		int32 RightUpLeg = INDEX_NONE;
		int32 LeftLeg = INDEX_NONE;
		int32 RightLeg = INDEX_NONE;
	};

	FRiderBones ResolveBones(const FReferenceSkeleton& Ref)
	{
		FRiderBones B;
		B.Hips = Ref.FindBoneIndex(TEXT("Hips"));
		B.Spine = Ref.FindBoneIndex(TEXT("Spine"));
		B.Neck = Ref.FindBoneIndex(TEXT("Neck"));
		B.Head = Ref.FindBoneIndex(TEXT("Head"));
		B.LeftArm = Ref.FindBoneIndex(TEXT("LeftArm"));
		B.LeftHand = Ref.FindBoneIndex(TEXT("LeftHand"));
		B.RightArm = Ref.FindBoneIndex(TEXT("RightArm"));
		B.RightHand = Ref.FindBoneIndex(TEXT("RightHand"));
		B.LeftShoulder = Ref.FindBoneIndex(TEXT("LeftShoulder"));
		B.RightShoulder = Ref.FindBoneIndex(TEXT("RightShoulder"));
		B.LeftForeArm = Ref.FindBoneIndex(TEXT("LeftForeArm"));
		B.RightForeArm = Ref.FindBoneIndex(TEXT("RightForeArm"));
		B.LeftUpLeg = Ref.FindBoneIndex(TEXT("LeftUpLeg"));
		B.RightUpLeg = Ref.FindBoneIndex(TEXT("RightUpLeg"));
		B.LeftLeg = Ref.FindBoneIndex(TEXT("LeftLeg"));
		B.RightLeg = Ref.FindBoneIndex(TEXT("RightLeg"));
		return B;
	}

	/** Si Bone cuelga de Root (o es él). En el esqueleto de referencia el padre siempre va antes que el hijo. */
	bool IsInSubtree(const FReferenceSkeleton& Ref, int32 Bone, int32 Root)
	{
		for (int32 Index = Bone; Index != INDEX_NONE; Index = Ref.GetParentIndex(Index))
		{
			if (Index == Root)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Locales de la pose evaluada con brazos y piernas en la postura de referencia (en T): la postura sentada se
	 * construye desde ella, como en Art/Source/Vehicles/Buggy/turtle_pose.py, y no desde la animación de espera.
	 */
	TArray<FTransform> SeatedBaseLocals(TArrayView<const FTransform> Locals, const FReferenceSkeleton& Ref, const FRiderBones& B)
	{
		TArray<FTransform> Out(Locals.GetData(), Locals.Num());
		const TArray<FTransform>& RefPose = Ref.GetRefBonePose();
		const int32 Roots[] = { B.LeftShoulder, B.RightShoulder, B.LeftUpLeg, B.RightUpLeg };
		for (int32 Bone = 0; Bone < Out.Num() && Bone < RefPose.Num(); ++Bone)
		{
			for (const int32 Root : Roots)
			{
				if (Root != INDEX_NONE && IsInSubtree(Ref, Bone, Root))
				{
					Out[Bone] = RefPose[Bone];
					break;
				}
			}
		}
		return Out;
	}

	/** Pose en el espacio de la malla con los huesos que se han tocado (los hijos siguen a su padre). */
	struct FRiderPose
	{
		TArray<FTransform> Space;
		TArray<int32> Parents;
		TArray<bool> Touched;

		bool Build(TArrayView<const FTransform> Locals, const FReferenceSkeleton& Ref)
		{
			const int32 Num = Ref.GetNum();
			if (Num == 0 || Locals.Num() != Num)
			{
				return false;
			}
			Space.SetNum(Num);
			Parents.SetNum(Num);
			Touched.Init(false, Num);
			for (int32 Bone = 0; Bone < Num; ++Bone)
			{
				// En el esqueleto de referencia el padre siempre va antes que el hijo.
				Parents[Bone] = Ref.GetParentIndex(Bone);
				Space[Bone] = Parents[Bone] == INDEX_NONE ? Locals[Bone] : Locals[Bone] * Space[Parents[Bone]];
			}
			return true;
		}

		template <typename FnType>
		void ForSubtree(int32 Bone, FnType&& Fn)
		{
			TArray<bool, TInlineAllocator<64>> Inside;
			Inside.Init(false, Space.Num());
			Inside[Bone] = true;
			for (int32 Index = Bone; Index < Space.Num(); ++Index)
			{
				if (Index != Bone && (Parents[Index] == INDEX_NONE || !Inside[Parents[Index]]))
				{
					continue;
				}
				Inside[Index] = true;
				Touched[Index] = true;
				Fn(Space[Index]);
			}
		}

		/** Gira el hueso y sus hijos Degrees alrededor de Axis (ejes de la malla) por su articulación. */
		void Rotate(int32 Bone, const FVector& Axis, float Degrees)
		{
			if (!Space.IsValidIndex(Bone) || FMath::Abs(Degrees) < 0.01f || Axis.IsNearlyZero())
			{
				return;
			}
			const FQuat Turn(Axis.GetSafeNormal(), FMath::DegreesToRadians(Degrees));
			const FVector Pivot = Space[Bone].GetLocation();
			ForSubtree(Bone, [&Turn, &Pivot](FTransform& T)
			{
				T.SetLocation(Pivot + Turn.RotateVector(T.GetLocation() - Pivot));
				T.SetRotation((Turn * T.GetRotation()).GetNormalized());
			});
		}

		/** Desplaza el hueso y sus hijos (unidades de la malla). */
		void Translate(int32 Bone, const FVector& Offset)
		{
			if (!Space.IsValidIndex(Bone) || Offset.IsNearlyZero(1e-3))
			{
				return;
			}
			ForSubtree(Bone, [&Offset](FTransform& T) { T.AddToTranslation(Offset); });
		}

		/**
		 * Sube la mano Degrees (o la baja si es negativo) girando el brazo por el hombro alrededor del eje horizontal
		 * perpendicular al brazo: vale con el brazo en cruz, hacia abajo o hacia delante al volante.
		 */
		void RaiseArm(int32 Arm, int32 Hand, float Degrees)
		{
			if (!Space.IsValidIndex(Arm))
			{
				return;
			}
			const FVector Reach = Space.IsValidIndex(Hand) ? Space[Hand].GetLocation() - Space[Arm].GetLocation() : FVector::ZeroVector;
			const FVector Axis = Reach ^ MeshUp;
			Rotate(Arm, Axis.IsNearlyZero(1e-3) ? MeshLeft : Axis, Degrees);
		}

		/**
		 * Postura sentada de turtle_pose.py (giros en los ejes de la malla, en el mismo orden): muslos hacia delante y
		 * espinillas hacia abajo; la conductora con las manos al volante y la artillera con el brazo derecho arriba.
		 */
		void Sit(const FRiderBones& B, bool bDriver)
		{
			Rotate(B.LeftUpLeg, MeshLeft, 90.f);
			Rotate(B.LeftLeg, MeshLeft, -90.f);
			Rotate(B.RightUpLeg, MeshLeft, 90.f);
			Rotate(B.RightLeg, MeshLeft, -90.f);
			if (bDriver)
			{
				for (const float Sign : { 1.f, -1.f })
				{
					const bool bLeft = Sign > 0.f;
					Rotate(bLeft ? B.LeftArm : B.RightArm, MeshForward, Sign * 10.f);
					Rotate(bLeft ? B.LeftArm : B.RightArm, MeshUp, Sign * 60.f);
					Rotate(bLeft ? B.LeftForeArm : B.RightForeArm, MeshUp, Sign * 50.f);
					Rotate(bLeft ? B.LeftForeArm : B.RightForeArm, MeshLeft, 25.f);
				}
				return;
			}
			Rotate(B.LeftArm, MeshUp, 80.f);
			Rotate(B.LeftArm, MeshLeft, 25.f);
			Rotate(B.RightArm, MeshForward, 95.f);
			Rotate(B.RightArm, MeshLeft, -20.f);
		}
	};
}

// ─────────────────────────────────────────────────────────────────────────────
// Componente
// ─────────────────────────────────────────────────────────────────────────────


UTN_BuggyRiderAnimComponent::UTN_BuggyRiderAnimComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// Después de la física: la velocidad del chasis ya es la de este fotograma.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(false);
}

void UTN_BuggyRiderAnimComponent::Setup(USkeletalMeshComponent* InRiderMesh, ETNBuggyRiderRole InRole)
{
	if (RiderMesh && RiderMesh != InRiderMesh)
	{
		ResetRider();
	}
	RiderMesh = InRiderMesh;
	Role = InRole;
}

UTN_BuggyRiderAnimComponent* UTN_BuggyRiderAnimComponent::FindForRole(const AActor* Owner, ETNBuggyRiderRole InRole)
{
	if (!Owner)
	{
		return nullptr;
	}
	TInlineComponentArray<UTN_BuggyRiderAnimComponent*> Riders(Owner);
	for (UTN_BuggyRiderAnimComponent* Rider : Riders)
	{
		if (Rider && Rider->Role == InRole)
		{
			return Rider;
		}
	}
	return nullptr;
}

void UTN_BuggyRiderAnimComponent::BeginPlay()
{
	Super::BeginPlay();
	// Solo es aspecto: el servidor dedicado no lo necesita.
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
	}
}

void UTN_BuggyRiderAnimComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetRider();
	Super::EndPlay(EndPlayReason);
}

void UTN_BuggyRiderAnimComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsRiderActive())
	{
		if (bRiderWasActive)
		{
			ResetRider();
		}
		return;
	}
	bRiderWasActive = true;
	const FVehicleSample Sample = SampleVehicle(DeltaTime);
	if (!Sample.bValid)
	{
		return;
	}
	UpdateTurretSignals();
	UpdateChannels(Sample, DeltaTime);
	ApplyPose();
}

bool UTN_BuggyRiderAnimComponent::IsRiderActive() const
{
	return GetOwner() && RiderMesh && !RiderMesh->bHiddenInGame && RiderMesh->GetSkeletalMeshAsset();
}

void UTN_BuggyRiderAnimComponent::ResetRider()
{
	ClearBoneOverrides();
	RestoreComponentPose();
	HeadPitch = LeanRoll = Steer = Crouch = Recoil = Grip = AimYaw = AimPitch = Swap = TNRiderAnim::FSpring();
	SmoothedAccelWorld = FVector::ZeroVector;
	bHasPrevSample = false;
	bWasAirborne = false;
	bRiderWasActive = false;
	bHasTurretSample = false;
	MaxFallSpeed = 0.f;
}

UTN_BuggyRiderAnimComponent::FVehicleSample UTN_BuggyRiderAnimComponent::SampleVehicle(float DeltaTime)
{
	FVehicleSample Sample;
	const AActor* Owner = GetOwner();
	const FVector Location = Owner->GetActorLocation();
	const FVector Velocity = Owner->GetVelocity();
	const bool bTeleported = bHasPrevSample && FVector::DistSquared(Location, PrevLocation) > FMath::Square(TeleportJumpCm);
	PrevLocation = Location;
	if (!bHasPrevSample || bTeleported || DeltaTime <= KINDA_SMALL_NUMBER)
	{
		// Primera medida o teletransporte (reaparición, RallyTeleport): sin aceleración inventada.
		PrevVelocity = Velocity;
		SmoothedAccelWorld = FVector::ZeroVector;
		bHasPrevSample = true;
		return Sample;
	}
	const FVector RawAccel = (Velocity - PrevVelocity) / DeltaTime;
	PrevVelocity = Velocity;
	const float Alpha = 1.f - FMath::Exp(-UE_TWO_PI * AccelSmoothingHz * DeltaTime);
	SmoothedAccelWorld = FMath::Lerp(SmoothedAccelWorld, RawAccel, static_cast<double>(Alpha));

	Sample.AccelMesh = RiderMesh->GetComponentTransform().InverseTransformVectorNoScale(SmoothedAccelWorld);
	Sample.bAirborne = IsVehicleAirborne();
	Sample.Steer01 = ReadSteer01(Velocity);
	Sample.VelocityZ = static_cast<float>(Velocity.Z);
	Sample.bValid = true;
	return Sample;
}

bool UTN_BuggyRiderAnimComponent::IsVehicleAirborne()
{
#if !UE_BUILD_SHIPPING
	if (TNRiderAnimDebug::CVarForceAirborne.GetValueOnGameThread() != 0)
	{
		return true;
	}
#endif
	const UChaosWheeledVehicleMovementComponent* Move = GetVehicleMovement();
	if (!Move || !Move->HasValidPhysicsState() || Move->Wheels.Num() == 0)
	{
		return false;
	}
	for (int32 Index = 0; Index < Move->Wheels.Num(); ++Index)
	{
		if (Move->GetWheelState(Index).bInContact)
		{
			return false;
		}
	}
	return true;
}

float UTN_BuggyRiderAnimComponent::ReadSteer01(const FVector& Velocity)
{
	if (Role != ETNBuggyRiderRole::Driver)
	{
		return 0.f;
	}
	AActor* Owner = GetOwner();
	// La entrada de Chaos solo es real donde se produce: la conductora local o la IA en el servidor.
	const APawn* Pawn = Cast<APawn>(Owner);
	const bool bHasLocalInput = Pawn && (Pawn->IsLocallyControlled() || (Pawn->HasAuthority() && !Pawn->IsPlayerControlled()));
	UChaosWheeledVehicleMovementComponent* Move = GetVehicleMovement();
	if (Move && bHasLocalInput)
	{
		return FMath::Clamp(Move->GetSteeringInput(), -1.f, 1.f);
	}
	// En el resto de máquinas, la guiñada de la física replicada.
	const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
	if (!Root)
	{
		return 0.f;
	}
	const float YawRate = static_cast<float>(Root->GetPhysicsAngularVelocityInDegrees() | Owner->GetActorUpVector());
	const float Forward = static_cast<float>(Velocity | Owner->GetActorForwardVector());
	return TNRiderAnim::SteerFromYawRate(YawRate, Forward, WheelbaseCm, MaxSteerDeg, MinSteerSpeedCms);
}

void UTN_BuggyRiderAnimComponent::UpdateTurretSignals()
{
	if (Role != ETNBuggyRiderRole::Gunner)
	{
		return;
	}
	const UTN_BuggyTurretComponent* Turret = GetTurret();
	if (!Turret)
	{
		return;
	}
	const float Heat = Turret->GetHeat01();
	const int32 Charges = Turret->GetSpecialCharges();
	const uint8 Ammo = static_cast<uint8>(Turret->GetSpecialAmmo());
	const uint8 Selected = static_cast<uint8>(Turret->GetSelectedAmmo());
	if (bAutoDetectShots && bHasTurretSample
		&& TNRiderAnim::IsShotSignal(PrevHeat01, Heat, PrevCharges, Charges, Ammo == PrevAmmo, ShotHeatStep))
	{
		NotifyShot(Turret->GetAimWorldDirection());
	}
	if (TNRiderAnim::IsAmmoSwapSignal(bHasTurretSample, PrevSelectedAmmo, Selected))
	{
		Swap = TNRiderAnim::KickSpring(Swap, SwapKickPerSec);
	}
	PrevHeat01 = Heat;
	PrevCharges = Charges;
	PrevAmmo = Ammo;
	PrevSelectedAmmo = Selected;
	bHasTurretSample = true;
}

void UTN_BuggyRiderAnimComponent::NotifyShot(FVector WorldDirection)
{
	if (!RiderMesh || WorldDirection.IsNearlyZero() || WorldDirection.ContainsNaN())
	{
		return;
	}
	// El torso va hacia atrás respecto al disparo, en horizontal y en los ejes de la malla (se mueven con el buggy).
	FVector Back = -RiderMesh->GetComponentTransform().InverseTransformVectorNoScale(WorldDirection.GetSafeNormal());
	Back.Z = 0.0;
	RecoilDirMesh = Back.IsNearlyZero(1e-3) ? -TNRiderAnimPose::MeshForward : Back.GetSafeNormal();
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Now - LastShotTime < ShotDedupSeconds)
	{
		return;
	}
	LastShotTime = Now;
	Recoil = TNRiderAnim::KickSpring(Recoil, RecoilKickDegPerSec);
}

void UTN_BuggyRiderAnimComponent::UpdateChannels(const FVehicleSample& Sample, float DeltaTime)
{
	using namespace TNRiderAnim;
	const bool bGunner = Role == ETNBuggyRiderRole::Gunner;
	Grip = StepSpring(Grip, bGunner && Sample.bAirborne ? 1.f : 0.f, GripSpring, DeltaTime);
	const FTargets Targets = ReactionTargets(Sample.AccelMesh, Grip.Value, Reaction);
	ApplyImpactKicks(Sample.AccelMesh);
	HeadPitch = StepSpring(HeadPitch, Targets.HeadPitchDeg, HeadSpring, DeltaTime);
	LeanRoll = StepSpring(LeanRoll, Targets.LeanRollDeg, LeanSpring, DeltaTime);
	Steer = StepSpring(Steer, bGunner ? 0.f : Sample.Steer01 * WheelTurnDeg, SteerSpring, DeltaTime);
	Recoil = StepSpring(Recoil, 0.f, RecoilDegSpring, DeltaTime);
	Swap = StepSpring(Swap, 0.f, SwapSpring, DeltaTime);
	UpdateGunnerAim(DeltaTime);
	UpdateCrouch(Sample, DeltaTime);
}

void UTN_BuggyRiderAnimComponent::UpdateGunnerAim(float DeltaTime)
{
	const UTN_BuggyTurretComponent* Turret = Role == ETNBuggyRiderRole::Gunner ? GetTurret() : nullptr;
	TNRiderAnim::FGunnerAim Target;
	if (Turret)
	{
		// El apuntado que se ve en esta máquina: el local en la de la artillera, el replicado en el resto.
		const FRotator Aim = Turret->GetDisplayAim();
		Target = TNRiderAnim::GunnerAimTargets(static_cast<float>(Aim.Yaw), static_cast<float>(Aim.Pitch), Turret->IsGunnerKnocked(),
			GunnerAim);
	}
	AimYaw = TNRiderAnim::StepSpring(AimYaw, Target.YawDeg, AimYawSpring, DeltaTime);
	AimPitch = TNRiderAnim::StepSpring(AimPitch, Target.PitchDeg, AimPitchSpring, DeltaTime);
}

void UTN_BuggyRiderAnimComponent::UpdateCrouch(const FVehicleSample& Sample, float DeltaTime)
{
	const float AirTarget = Role == ETNBuggyRiderRole::Driver ? 1.f : GunnerAirCrouch;
	if (Sample.bAirborne)
	{
		MaxFallSpeed = FMath::Max(MaxFallSpeed, -Sample.VelocityZ);
	}
	else if (bWasAirborne)
	{
		// Aterrizaje: se aplasta un poco más según lo rápido que caía y el muelle la devuelve con rebote.
		const float Kick = FMath::Min(FMath::Max(MaxFallSpeed, 0.f) * LandingKickPerCms, MaxLandingKick);
		Crouch = TNRiderAnim::KickSpring(Crouch, Kick);
		MaxFallSpeed = 0.f;
	}
	bWasAirborne = Sample.bAirborne;
	Crouch = TNRiderAnim::StepSpring(Crouch, Sample.bAirborne ? AirTarget : 0.f, CrouchSpring, DeltaTime);
}

void UTN_BuggyRiderAnimComponent::ApplyImpactKicks(const FVector& AccelMesh)
{
	const float Threshold = FMath::Max(Reaction.ImpactDecel, 1.f);
	const float Decel = static_cast<float>(-AccelMesh.Y);
	const float Lateral = static_cast<float>(AccelMesh.X);
	const float Excess = FMath::Max(Decel, FMath::Abs(Lateral)) / Threshold;
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Excess < 1.f || Now - LastImpactTime < ImpactCooldownSeconds)
	{
		return;
	}
	LastImpactTime = Now;
	const float Soft = 1.f - FMath::Clamp(Grip.Value, 0.f, 1.f) * FMath::Clamp(Reaction.GripRigidity, 0.f, 1.f);
	const float Kick = Reaction.ImpactKickDegPerSec * FMath::Min(Excess, 2.f) * Soft;
	if (Decel >= Threshold)
	{
		HeadPitch = TNRiderAnim::KickSpring(HeadPitch, Kick);
	}
	if (FMath::Abs(Lateral) >= Threshold)
	{
		// Golpe lateral: el cuerpo sale hacia el lado contrario a la aceleración.
		LeanRoll = TNRiderAnim::KickSpring(LeanRoll, -FMath::Sign(Lateral) * Kick * 0.6f);
	}
}

bool UTN_BuggyRiderAnimComponent::IsAtRest() const
{
	constexpr float Epsilon = 0.01f;
	for (const TNRiderAnim::FSpring* Channel : { &HeadPitch, &LeanRoll, &Steer, &Crouch, &Recoil, &Grip, &AimYaw, &AimPitch, &Swap })
	{
		if (FMath::Abs(Channel->Value) > Epsilon || FMath::Abs(Channel->Velocity) > Epsilon)
		{
			return false;
		}
	}
	return true;
}

float UTN_BuggyRiderAnimComponent::HeadPitchWithRecoil() const
{
	// Latigazo del retroceso, mirada al cambiar de munición y apuntado (arriba = cabeceo negativo).
	return HeadPitch.Value + Recoil.Value * RecoilHeadFollow + Swap.Value * SwapHeadDeg - AimPitch.Value;
}

void UTN_BuggyRiderAnimComponent::ApplyPose()
{
	UTN_ProcAnimInstance* Anim = Cast<UTN_ProcAnimInstance>(RiderMesh->GetAnimInstance());
	if (PosedAnim.IsValid() && PosedAnim.Get() != Anim)
	{
		// La clase de animación ha cambiado (ATN_Buggy::ApplySeatLook): se limpia la anterior.
		ClearBoneOverrides();
	}
	// Con la postura sentada, los huesos se escriben también en reposo: si no, la tortuga se queda de pie.
	if (IsAtRest() && (!Anim || !bSeatedPose))
	{
		ClearBoneOverrides();
		RestoreComponentPose();
		return;
	}
	if (Anim)
	{
		RestoreComponentPose();
		// Sin pintarse no se evalúa la pose (OnlyTickPoseWhenRendered): no hace falta escribirla.
		if (RiderMesh->WasRecentlyRendered(0.5f))
		{
			ApplyBonePose(*Anim);
		}
		return;
	}
	ApplyComponentPose();
}

void UTN_BuggyRiderAnimComponent::ApplyBonePose(UTN_ProcAnimInstance& Anim)
{
	const USkeletalMesh* Mesh = RiderMesh->GetSkeletalMeshAsset();
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const TNRiderAnimPose::FRiderBones B = TNRiderAnimPose::ResolveBones(Ref);
	const TArray<FTransform> Locals = bSeatedPose
		? TNRiderAnimPose::SeatedBaseLocals(RiderMesh->GetBoneSpaceTransformsView(), Ref, B)
		: TArray<FTransform>(RiderMesh->GetBoneSpaceTransformsView().GetData(), RiderMesh->GetBoneSpaceTransformsView().Num());
	TNRiderAnimPose::FRiderPose Pose;
	if (!Pose.Build(Locals, Ref))
	{
		ClearBoneOverrides();
		return;
	}
	const bool bDriver = Role == ETNBuggyRiderRole::Driver;
	if (bSeatedPose)
	{
		Pose.Sit(B, bDriver);
		// Las piernas y los brazos salen de la postura de referencia: se escriben aunque la postura no los gire.
		for (const int32 Root : { B.LeftShoulder, B.RightShoulder, B.LeftUpLeg, B.RightUpLeg })
		{
			if (Root != INDEX_NONE)
			{
				Pose.ForSubtree(Root, [](FTransform&) {});
			}
		}
	}
	// Los cm del ajuste pasan a unidades de la malla (ATN_Buggy::FitTurtle la escala).
	const float ToMesh = 1.f / FMath::Max(0.01f, static_cast<float>(RiderMesh->GetComponentScale().Z));
	using TNRiderAnimPose::MeshUp;
	Pose.Translate(B.Hips, MeshUp * (-Crouch.Value * CrouchDropCm * ToMesh));
	Pose.Rotate(B.Spine, TNRiderAnimPose::MeshForward, LeanRoll.Value);
	// Apuntado de la artillera: el torso gira por la columna y el cuello completa el giro (las piernas no se mueven). Un
	// giro positivo sobre la vertical de la malla lleva su delante (+Y) a su derecha (-X), como la guiñada del buggy.
	const float TorsoYaw = bDriver ? 0.f : AimYaw.Value * FMath::Clamp(GunnerAim.TorsoShare, 0.f, 1.f);
	const float NeckYaw = bDriver ? 0.f : AimYaw.Value - TorsoYaw;
	Pose.Rotate(B.Spine, MeshUp, TorsoYaw);
	// Retroceso: el torso se echa hacia el lado contrario al disparo (gira hacia RecoilDirMesh) y se desplaza un poco.
	Pose.Rotate(B.Spine, MeshUp ^ RecoilDirMesh, Recoil.Value);
	Pose.Translate(B.Spine, RecoilDirMesh * (Recoil.Value * RecoilBackCmPerDeg * ToMesh));
	Pose.Rotate(B.Neck, MeshUp, NeckYaw);
	// Su izquierda tras el giro: el eje de los cabeceos del torso y de la cabeza.
	const FVector Left = FQuat(MeshUp, FMath::DegreesToRadians(TorsoYaw + NeckYaw)).RotateVector(TNRiderAnimPose::MeshLeft);
	// Agarrada en el aire, se encorva hacia delante (giro negativo sobre su izquierda).
	Pose.Rotate(B.Spine, Left, bDriver ? 0.f : -Grip.Value * GripHunchDeg);
	// Cabeceo hacia delante = giro negativo sobre su izquierda; se reparte entre el cuello y la cabeza.
	const float HeadDeg = HeadPitchWithRecoil();
	Pose.Rotate(B.Neck, Left, -0.4f * HeadDeg);
	Pose.Rotate(B.Head, Left, -0.6f * HeadDeg);
	Pose.Translate(B.Neck, MeshUp * (-FMath::Max(Crouch.Value, 0.f) * NeckTuckCm * ToMesh));
	// Volante: a la derecha (+) sube la mano izquierda y baja la derecha. En el aire, brazos recogidos o agarrados. Al
	// cambiar de munición, la artillera sube y baja el brazo izquierdo (carga la recámara).
	const float Down = bDriver ? FMath::Max(Crouch.Value, 0.f) * ArmTuckDeg : Grip.Value * GripArmDeg;
	const float SteerDeg = bDriver ? Steer.Value : 0.f;
	const float SwapDeg = bDriver ? 0.f : Swap.Value * SwapArmDeg;
	Pose.RaiseArm(B.LeftArm, B.LeftHand, SteerDeg - Down + SwapDeg);
	Pose.RaiseArm(B.RightArm, B.RightHand, -SteerDeg - Down);

	TSet<FName> Written;
	for (int32 Bone = 0; Bone < Pose.Space.Num(); ++Bone)
	{
		if (!Pose.Touched[Bone])
		{
			continue;
		}
		const FName Name = Ref.GetBoneName(Bone);
		Anim.BoneQuat.Add(Name, Pose.Space[Bone].GetRotation());
		Anim.BoneLoc.Add(Name, Pose.Space[Bone].GetLocation());
		Written.Add(Name);
	}
	for (const FName& Stale : WrittenBones.Difference(Written))
	{
		Anim.BoneQuat.Remove(Stale);
		Anim.BoneLoc.Remove(Stale);
	}
	WrittenBones = MoveTemp(Written);
	PosedAnim = &Anim;
}

void UTN_BuggyRiderAnimComponent::ApplyComponentPose()
{
	if (!bComponentPosed)
	{
		BaseRelative = RiderMesh->GetRelativeTransform();
		bComponentPosed = true;
	}
	// Sin huesos que mover, todo el cuerpo gira (un tercio del apuntado), se inclina, cabecea (un tercio) y se desplaza en
	// su asiento.
	const FQuat BaseRotation = BaseRelative.GetRotation();
	const FQuat Delta = FQuat(TNRiderAnimPose::MeshUp, FMath::DegreesToRadians(AimYaw.Value / 3.f))
		* FQuat(TNRiderAnimPose::MeshForward, FMath::DegreesToRadians(LeanRoll.Value))
		* FQuat(TNRiderAnimPose::MeshLeft, FMath::DegreesToRadians(-HeadPitchWithRecoil() / 3.f));
	const FVector OffsetMesh = TNRiderAnimPose::MeshUp * (-Crouch.Value * CrouchDropCm)
		+ RecoilDirMesh * (Recoil.Value * RecoilBackCmPerDeg);
	RiderMesh->SetRelativeLocationAndRotation(BaseRelative.GetLocation() + BaseRotation.RotateVector(OffsetMesh),
		(BaseRotation * Delta).GetNormalized());
}

void UTN_BuggyRiderAnimComponent::ClearBoneOverrides()
{
	if (UTN_ProcAnimInstance* Anim = PosedAnim.Get())
	{
		for (const FName& Name : WrittenBones)
		{
			Anim->BoneQuat.Remove(Name);
			Anim->BoneLoc.Remove(Name);
		}
	}
	WrittenBones.Reset();
	PosedAnim.Reset();
}

void UTN_BuggyRiderAnimComponent::RestoreComponentPose()
{
	if (!bComponentPosed)
	{
		return;
	}
	bComponentPosed = false;
	// Solo posición y giro: la escala la pone ATN_Buggy::FitTurtle y puede haber cambiado entretanto.
	if (RiderMesh)
	{
		RiderMesh->SetRelativeLocationAndRotation(BaseRelative.GetLocation(), BaseRelative.GetRotation());
	}
}

UChaosWheeledVehicleMovementComponent* UTN_BuggyRiderAnimComponent::GetVehicleMovement()
{
	if (!CachedMovement.IsValid() && GetOwner())
	{
		CachedMovement = GetOwner()->FindComponentByClass<UChaosWheeledVehicleMovementComponent>();
	}
	return CachedMovement.Get();
}

UTN_BuggyTurretComponent* UTN_BuggyRiderAnimComponent::GetTurret()
{
	if (!CachedTurret.IsValid() && GetOwner())
	{
		CachedTurret = GetOwner()->FindComponentByClass<UTN_BuggyTurretComponent>();
	}
	return CachedTurret.Get();
}
