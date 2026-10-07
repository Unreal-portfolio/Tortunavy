// ─────────────────────────────────────────────────────────────────────────────
// TortugaCharacter — Ragdoll del derribo en red (#153, Docs/Ragdoll_Red.md).
//
// El servidor simula el ragdoll que manda: lo empujan las demás tortugas, decide cuándo se asienta y replica la pose del
// cuerpo raíz (KnockdownRootPose). Los clientes simulan el suyo y lo corrigen hacia esa pose. Las decisiones son puras
// (TN_RagdollNet.h) y las prueba Tortunabo.RagdollNet.
// ─────────────────────────────────────────────────────────────────────────────

#include "Player/TortugaCharacter.h"
#include "Player/TN_RagdollNet.h"
#include "Core/TN_Log.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "PhysicsEngine/BodyInstance.h"

namespace TNRagdollNetDetail
{
	/** Cuerpo raíz del ragdoll del derribo si está simulando; nulo si no. */
	FBodyInstance* SimulatingRootBody(USkeletalMeshComponent* SkelMesh)
	{
		FBodyInstance* RootBody = SkelMesh ? SkelMesh->GetBodyInstance() : nullptr;
		if (!RootBody || !RootBody->IsValidBodyInstance() || !SkelMesh->IsSimulatingPhysics())
		{
			return nullptr;
		}
		return RootBody;
	}
}

FTNRagdollRootPose ATortugaCharacter::SampleKnockdownRootPose(bool bActive, bool bSettled) const
{
	FTNRagdollRootPose Pose;
	Pose.bActive = bActive;
	Pose.bSettled = bSettled;
	const USkeletalMeshComponent* SkelMesh = GetMesh();
	const FBodyInstance* RootBody = SkelMesh ? SkelMesh->GetBodyInstance() : nullptr;
	if (!RootBody || !RootBody->IsValidBodyInstance())
	{
		Pose.Location = TNRagdollNet::QuantizeLocation(GetActorLocation());
		Pose.Rotation = GetActorRotation();
		return Pose;
	}
	const FTransform RootXf = RootBody->GetUnrealWorldTransform();
	// Con la misma precisión con la que viaja: el servidor y los clientes persiguen exactamente el mismo punto.
	Pose.Location = TNRagdollNet::QuantizeLocation(RootXf.GetLocation());
	Pose.Rotation = RootXf.Rotator();
	Pose.Velocity = bSettled ? FVector::ZeroVector : RootBody->GetUnrealWorldVelocity();
	return Pose;
}

void ATortugaCharacter::OnRep_KnockdownRootPose()
{
	if (const UWorld* World = GetWorld())
	{
		RootPoseReceivedAt = World->GetTimeSeconds();
	}
}

FVector ATortugaCharacter::TickKnockdownRagdollNet(float DeltaTime)
{
	if (HasAuthority())
	{
		ServerTickKnockdownRagdollNet(DeltaTime);
		return FVector::ZeroVector;
	}
	return ClientCorrectKnockdownRagdoll(DeltaTime);
}

void ATortugaCharacter::ServerTickKnockdownRagdollNet(float DeltaTime)
{
	USkeletalMeshComponent* SkelMesh = GetMesh();
	FBodyInstance* RootBody = TNRagdollNetDetail::SimulatingRootBody(SkelMesh);
	const UWorld* World = GetWorld();
	if (!RootBody || !World)
	{
		return;
	}

	const bool bPushed = ServerPushKnockdownRagdoll(DeltaTime);
	const float LinearSpeed = RootBody->GetUnrealWorldVelocity().Size();
	const float AngularSpeedDeg = FMath::RadiansToDegrees(RootBody->GetUnrealWorldAngularVelocityInRadians().Size());
	RagdollSettleTimer = bPushed
		? 0.f
		: TNRagdollNet::AdvanceSettleTimer(RagdollSettleTimer, LinearSpeed, AngularSpeedDeg, DeltaTime, RagdollNetTuning);
	const bool bSettled = TNRagdollNet::IsSettled(RagdollSettleTimer, RagdollNetTuning);
	const bool bWasSettled = LastSentRootPose.bSettled;

	// Asentado: quieto y dormido, como el cadáver congelado (PutAllRigidBodiesToSleep, sin dejar de simular: así no vuelve
	// a la pose de referencia y un empujón o un golpe lo despiertan).
	if (bSettled && !bWasSettled)
	{
		SkelMesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
		SkelMesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		SkelMesh->PutAllRigidBodiesToSleep();
	}

	const FTNRagdollRootPose Current = SampleKnockdownRootPose(/*bActive=*/true, bSettled);
	const float Now = World->GetTimeSeconds();
	if (!TNRagdollNet::ShouldSendPose(Now - LastRootPoseSentAt, LastSentRootPose, Current, RagdollNetTuning))
	{
		return;
	}
	KnockdownRootPose = Current;
	LastSentRootPose = Current;
	LastRootPoseSentAt = Now;
	if (bSettled != bWasSettled)
	{
		// El cambio de asentado no espera al siguiente pase de red.
		ForceNetUpdate();
	}
}

bool ATortugaCharacter::ServerPushKnockdownRagdoll(float DeltaTime)
{
	USkeletalMeshComponent* SkelMesh = GetMesh();
	FBodyInstance* RootBody = TNRagdollNetDetail::SimulatingRootBody(SkelMesh);
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!RootBody || !GameState)
	{
		return false;
	}

	// El perfil Ragdoll no choca con cápsulas: el empuje lo decide el servidor y llega a los clientes con la pose.
	const FVector BodyLocation = RootBody->GetUnrealWorldTransform().GetLocation();
	const FVector BodyVelocity = RootBody->GetUnrealWorldVelocity();
	FVector TotalChange = FVector::ZeroVector;
	for (const APlayerState* OtherState : GameState->PlayerArray)
	{
		const ATortugaCharacter* Other = OtherState ? Cast<ATortugaCharacter>(OtherState->GetPawn()) : nullptr;
		if (!Other || Other == this || Other->bIsKnockedDown || Other->bIsDead)
		{
			continue;
		}
		TotalChange += TNRagdollNet::ComputePushVelocityChange(Other->GetActorLocation(), Other->GetVelocity(), BodyLocation,
			BodyVelocity + TotalChange, DeltaTime, RagdollNetTuning);
	}
	if (TotalChange.IsNearlyZero())
	{
		return false;
	}
	// A todos los cuerpos a la vez: el ragdoll se desplaza entero, sin estirarse.
	SkelMesh->WakeAllRigidBodies();
	SkelMesh->SetAllPhysicsLinearVelocity(TotalChange, /*bAddToCurrent=*/true);
	return true;
}

FVector ATortugaCharacter::ClientCorrectKnockdownRagdoll(float DeltaTime)
{
	const FTNRagdollRootPose& Pose = KnockdownRootPose;
	USkeletalMeshComponent* SkelMesh = GetMesh();
	FBodyInstance* RootBody = TNRagdollNetDetail::SimulatingRootBody(SkelMesh);
	const UWorld* World = GetWorld();
	if (!Pose.bActive || RootPoseReceivedAt < 0.f || !RootBody || !World)
	{
		return FVector::ZeroVector;
	}

	// El servidor lo ha vuelto a mover (empujón, golpe): se despierta y vuelve a seguirlo.
	if (bLocalRagdollSettled && !Pose.bSettled)
	{
		bLocalRagdollSettled = false;
		SkelMesh->WakeAllRigidBodies();
	}
	if (bLocalRagdollSettled)
	{
		return FVector::ZeroVector;
	}

	const FTransform LocalXf = RootBody->GetUnrealWorldTransform();
	const FVector LocalVelocity = RootBody->GetUnrealWorldVelocity();
	const float Age = World->GetTimeSeconds() - RootPoseReceivedAt;
	const FVector Target = TNRagdollNet::ExtrapolateTarget(Pose, Age, GetOneWayLatencySeconds(), RagdollNetTuning);
	const TNRagdollNet::FCorrection Correction = TNRagdollNet::ComputeCorrection(LocalXf.GetLocation(), LocalVelocity, Target,
		Pose.Velocity, Pose.bSettled, DeltaTime, RagdollNetTuning);

	switch (Correction.Mode)
	{
	case TNRagdollNet::ECorrectionMode::Nudge:
		SkelMesh->SetAllPhysicsLinearVelocity(Correction.DeltaVelocity, /*bAddToCurrent=*/true);
		return FVector::ZeroVector;

	case TNRagdollNet::ECorrectionMode::Snap:
	{
		// Muy lejos (entrar tarde, una pérdida larga): a la pose del servidor de golpe, también el giro del cuerpo raíz.
		const FQuat DeltaRotation = Pose.Rotation.Quaternion() * LocalXf.GetRotation().Inverse();
		TeleportKnockdownRagdoll(Correction.Translation, DeltaRotation);
		SkelMesh->SetAllPhysicsLinearVelocity(Correction.DeltaVelocity, /*bAddToCurrent=*/true);
		UE_LOG(LogTortunabo, Log, TEXT("[Ragdoll][Red] %s: a la pose del servidor de golpe (%.0f cm)."),
			*GetName(), Correction.Translation.Size());
		return Correction.Translation;
	}

	case TNRagdollNet::ECorrectionMode::SnapAndSleep:
	case TNRagdollNet::ECorrectionMode::None:
	default:
		break;
	}

	if (!Pose.bSettled)
	{
		return FVector::ZeroVector;
	}
	// Asentado en el servidor: el cuerpo raíz en su mismo punto (sin girar, para no meter las patas en el suelo) y dormido.
	TeleportKnockdownRagdoll(Correction.Translation, FQuat::Identity);
	SkelMesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
	SkelMesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	SkelMesh->PutAllRigidBodiesToSleep();
	bLocalRagdollSettled = true;
	// El desfase con el servidor justo antes de asentarse (criterio de #153: menos de 50 cm).
	UE_LOG(LogTortunabo, Log, TEXT("[Ragdoll][Red] %s asentado: desfase con el servidor %.1f cm."),
		*GetName(), Correction.Translation.Size());
	return Correction.Translation;
}

void ATortugaCharacter::TeleportKnockdownRagdoll(const FVector& Translation, const FQuat& RotationAroundRoot)
{
	USkeletalMeshComponent* SkelMesh = GetMesh();
	const FBodyInstance* RootBody = TNRagdollNetDetail::SimulatingRootBody(SkelMesh);
	if (!RootBody || (Translation.IsNearlyZero(0.01) && RotationAroundRoot.Equals(FQuat::Identity)))
	{
		return;
	}
	const FVector Pivot = RootBody->GetUnrealWorldTransform().GetLocation();
	for (FBodyInstance* Body : SkelMesh->Bodies)
	{
		if (!Body || !Body->IsValidBodyInstance())
		{
			continue;
		}
		FTransform BodyXf = Body->GetUnrealWorldTransform();
		BodyXf.SetLocation(Pivot + Translation + RotationAroundRoot.RotateVector(BodyXf.GetLocation() - Pivot));
		BodyXf.SetRotation(RotationAroundRoot * BodyXf.GetRotation());
		Body->SetBodyTransform(BodyXf, ETeleportType::TeleportPhysics);
	}
}

float ATortugaCharacter::GetOneWayLatencySeconds() const
{
	const UWorld* World = GetWorld();
	const APlayerController* LocalController = World ? World->GetFirstPlayerController() : nullptr;
	const APlayerState* LocalState = LocalController ? LocalController->PlayerState : nullptr;
	// GetPingInMilliseconds es la ida y vuelta.
	return LocalState ? LocalState->GetPingInMilliseconds() * 0.0005f : 0.f;
}
