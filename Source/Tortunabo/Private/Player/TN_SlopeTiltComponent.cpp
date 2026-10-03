#include "Player/TN_SlopeTiltComponent.h"

#include "Player/TN_SlopeTiltDecisions.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_CarryComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace TNSlopeTiltPrivate
{
	TAutoConsoleVariable<int32> CVarSlopeTiltEnable(
		TEXT("TN.SlopeTilt.Enable"), 1,
		TEXT("Inclinación visual de la tortuga con la pendiente (#586): 1 = activa, 0 = el modelo siempre recto."),
		ECVF_Default);

	/** Giro relativo igual al escrito (con margen por la conversión cuaternión ↔ rotador). */
	constexpr float SameRotationTolerance = 1.e-2f;

	/** Los proxies parados reutilizan la traza: se repite al moverse más de esto (cm) o pasado RetraceSeconds. */
	constexpr float RetraceDistanceSq = 1.f;
	constexpr float RetraceSeconds = 0.25f;
}

UTN_SlopeTiltComponent::UTN_SlopeTiltComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(false);
}

void UTN_SlopeTiltComponent::BeginPlay()
{
	Super::BeginPlay();

	// Sin pantalla no hay nada que inclinar.
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
		return;
	}

	// Después del Tick del actor (panzazo, emotes) y del movimiento (suelo y suavizado de red de la malla): así lo que
	// escriben ellos este fotograma es la base sobre la que se inclina.
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	PrimaryComponentTick.AddPrerequisite(Owner, Owner->PrimaryActorTick);
	if (const ACharacter* Character = Cast<ACharacter>(Owner))
	{
		if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
		{
			PrimaryComponentTick.AddPrerequisite(Move, Move->PrimaryComponentTick);
		}
	}
}

void UTN_SlopeTiltComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (Mesh && !Mesh->IsSimulatingPhysics())
	{
		DropTilt(*Mesh);
	}
	Super::EndPlay(EndPlayReason);
}

void UTN_SlopeTiltComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh)
	{
		return;
	}

	// En ragdoll la malla la mueve la física: no se toca. El derribo guardó su giro relativo (con la inclinación) para
	// devolverlo al levantarse: si lo devuelve tal cual, se le quita la inclinación.
	if (Mesh->IsSimulatingPhysics())
	{
		bRestoreAfterRagdoll |= bTiltApplied;
		bTiltApplied = false;
		CurrentTilt = TargetTilt = FRotator::ZeroRotator;
		return;
	}
	if (bRestoreAfterRagdoll)
	{
		bRestoreAfterRagdoll = false;
		if (Mesh->GetRelativeRotation().Equals(LastWrittenRelative, TNSlopeTiltPrivate::SameRotationTolerance))
		{
			Mesh->SetRelativeRotation(BaseRelative);
		}
	}

	const TNSlopeTilt::FTiltGate Gate = ReadGate(*Character);
	if (TNSlopeTilt::IsTakenOver(Gate))
	{
		DropTilt(*Mesh);
		return;
	}

	TargetTilt = FRotator::ZeroRotator;
	FVector FloorNormal = FVector::UpVector;
	if (TNSlopeTiltPrivate::CVarSlopeTiltEnable.GetValueOnGameThread() != 0
		&& TNSlopeTilt::ShouldTilt(Gate)
		&& ReadFloorNormal(*Character, FloorNormal))
	{
		const float Yaw = Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetComponentRotation().Yaw : Character->GetActorRotation().Yaw;
		TargetTilt = TNSlopeTilt::ComputeTilt(FloorNormal, Yaw, MaxTiltDeg, MinTiltDeg);
	}

	// Recto y sin nada aplicado: no se escribe en la malla (el caso normal en llano no cuesta nada más).
	if (!bTiltApplied && TargetTilt.IsZero() && CurrentTilt.IsZero())
	{
		return;
	}

	CurrentTilt = TNSlopeTilt::StepTilt(CurrentTilt, TargetTilt, DeltaTime, TiltInterpSpeed);
	ApplyToVisual(*Mesh);
}

TNSlopeTilt::FTiltGate UTN_SlopeTiltComponent::ReadGate(const ACharacter& Character) const
{
	const UCharacterMovementComponent* Move = Character.GetCharacterMovement();
	TNSlopeTilt::FTiltGate Gate;
	Gate.bOnGround = Move && Move->IsMovingOnGround() && !Character.GetAttachParentActor();
	if (const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(&Character))
	{
		Gate.bInShell = Turtle->IsInShell();
		Gate.bKnockedDown = Turtle->IsKnockedDown();
		Gate.bDead = Turtle->IsDead();
		const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
		Gate.bCarried = Carry && Carry->IsBeingCarried();
	}
	return Gate;
}

bool UTN_SlopeTiltComponent::ReadFloorNormal(const ACharacter& Character, FVector& OutNormal)
{
	// El dueño y el servidor tienen el suelo del movimiento al día; los proxies simulados, no siempre.
	const UCharacterMovementComponent* Move = Character.GetCharacterMovement();
	if (Character.GetLocalRole() == ROLE_SimulatedProxy || !Move)
	{
		return TraceFloorNormal(Character, OutNormal);
	}

	const FFindFloorResult& Floor = Move->CurrentFloor;
	if (!Floor.IsWalkableFloor())
	{
		return false;
	}
	// La normal de la cara que toca (ImpactNormal) y no la del barrido de la cápsula, que se redondea en los bordes.
	const FHitResult& Hit = Floor.HitResult;
	OutNormal = Hit.ImpactNormal.Z >= Move->GetWalkableFloorZ() ? Hit.ImpactNormal : Hit.Normal;
	return true;
}

bool UTN_SlopeTiltComponent::TraceFloorNormal(const ACharacter& Character, FVector& OutNormal)
{
	const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	UWorld* World = GetWorld();
	if (!Capsule || !World)
	{
		return false;
	}

	const FVector Location = Capsule->GetComponentLocation();
	TraceAge += World->GetDeltaSeconds();
	if (FVector::DistSquared(Location, LastTraceLocation) < TNSlopeTiltPrivate::RetraceDistanceSq
		&& TraceAge < TNSlopeTiltPrivate::RetraceSeconds)
	{
		OutNormal = CachedTraceNormal;
		return bCachedTraceHit;
	}
	LastTraceLocation = Location;
	TraceAge = 0.f;

	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector End = Location - FVector(0.f, 0.f, HalfHeight + ProxyTraceDistance);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNSlopeTiltTrace), false, &Character);
	FCollisionResponseParams Response;
	Capsule->InitSweepCollisionParams(Params, Response);
	FHitResult Hit;
	bCachedTraceHit = World->LineTraceSingleByChannel(Hit, Location, End, Capsule->GetCollisionObjectType(), Params, Response);
	const UCharacterMovementComponent* Move = Character.GetCharacterMovement();
	if (bCachedTraceHit && Move && !Move->IsWalkable(Hit))
	{
		bCachedTraceHit = false;
	}
	CachedTraceNormal = bCachedTraceHit ? Hit.ImpactNormal : FVector::UpVector;
	OutNormal = CachedTraceNormal;
	return bCachedTraceHit;
}

void UTN_SlopeTiltComponent::ApplyToVisual(USceneComponent& Visual)
{
	// Otro sistema ha escrito el giro desde la última vez (o es la primera): ese es el nuevo giro sin inclinar.
	const FRotator Now = Visual.GetRelativeRotation();
	if (!bTiltApplied || !Now.Equals(LastWrittenRelative, TNSlopeTiltPrivate::SameRotationTolerance))
	{
		BaseRelative = Now.Quaternion();
	}

	if (CurrentTilt.IsZero())
	{
		// Llegada a recto: se deja la base tal cual y se deja de escribir.
		Visual.SetRelativeRotation(BaseRelative);
		bTiltApplied = false;
		return;
	}

	Visual.SetRelativeRotation(TNSlopeTilt::ComposeTilt(BaseRelative, CurrentTilt));
	LastWrittenRelative = Visual.GetRelativeRotation();
	bTiltApplied = true;
}

void UTN_SlopeTiltComponent::DropTilt(USceneComponent& Visual)
{
	// Si la malla sigue con lo que escribimos, vuelve a su base; si otro sistema ya la ha colocado, se deja como está.
	if (bTiltApplied && Visual.GetRelativeRotation().Equals(LastWrittenRelative, TNSlopeTiltPrivate::SameRotationTolerance))
	{
		Visual.SetRelativeRotation(BaseRelative);
	}
	bTiltApplied = false;
	CurrentTilt = TargetTilt = FRotator::ZeroRotator;
}
