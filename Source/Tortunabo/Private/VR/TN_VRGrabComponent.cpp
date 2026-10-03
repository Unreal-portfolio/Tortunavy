#include "VR/TN_VRGrabComponent.h"
#include "VR/TN_VRMath.h"
#include "Core/TN_Log.h"
#include "Player/TN_ShellBody.h"
#include "World/TN_PhysicsObjectActor.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"

namespace TNVRGrabDetail
{
	/** Veces por segundo que el dueño manda la mano al servidor mientras lleva algo. */
	constexpr double SendRate = 30.0;
	/** Cuánto se tolera de más en la distancia de la mano al objeto al aceptarlo en el servidor (latencia). */
	constexpr float ServerGrabSlack = 60.f;

	/** Etiqueta de actor: se coge con la mano aunque en el cliente no simule física (la simula el servidor). */
	const FName GrabTag(TEXT("VRGrab"));
	/** Etiqueta de actor: nunca se coge con la mano. */
	const FName NoGrabTag(TEXT("NoVRGrab"));

	bool IsValidHand(int32 Hand)
	{
		return Hand == 0 || Hand == 1;
	}

	/**
	 * Lo que la mano nunca mueve: tortugas, enemigos y caparazones (tienen sus reglas: coger compañeros, derribos), lo que
	 * lleva NoVRGrab y un actor replicado que no replica su movimiento (los demás no lo verían moverse).
	 */
	bool IsExcludedOwner(const AActor* Owner, const AActor* ByActor)
	{
		return !Owner || Owner == ByActor || Owner->IsA<APawn>() || Owner->IsA<ATN_ShellBody>() || Owner->ActorHasTag(NoGrabTag)
			|| (Owner->GetIsReplicated() && !Owner->IsReplicatingMovement());
	}
}

UTN_VRGrabComponent::UTN_VRGrabComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UTN_VRGrabComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		ReleaseHere(Hand, FVector::ZeroVector);
	}
	Super::EndPlay(EndPlayReason);
}

bool UTN_VRGrabComponent::IsGrabbable(const UPrimitiveComponent* Component, const AActor* ByActor, float MaxMassKg)
{
	if (!Component || !Component->IsSimulatingPhysics() || Component->Mobility != EComponentMobility::Movable)
	{
		return false;
	}
	if (TNVRGrabDetail::IsExcludedOwner(Component->GetOwner(), ByActor))
	{
		return false;
	}
	return Component->GetMass() <= MaxMassKg;
}

bool UTN_VRGrabComponent::IsGrabbableFromClient(UPrimitiveComponent* Component, const AActor* ByActor, float MaxMassKg)
{
	if (!Component || Component->Mobility != EComponentMobility::Movable)
	{
		return false;
	}
	const AActor* Owner = Component->GetOwner();
	if (TNVRGrabDetail::IsExcludedOwner(Owner, ByActor) || !Owner->GetIsReplicated())
	{
		return false;
	}
	bool bCandidate = false;
	if (const ATN_PhysicsObjectActor* Physics = Cast<ATN_PhysicsObjectActor>(Owner))
	{
		// Los objetos con física del juego solo simulan en el servidor: su malla, salvo los cubos de empujar (sin física).
		bCandidate = !Physics->UsesKinematicPush() && Component == Physics->GetPhysicsMesh();
	}
	else
	{
		bCandidate = Component->IsSimulatingPhysics() || Owner->ActorHasTag(TNVRGrabDetail::GrabTag);
	}
	// Sin simular, la masa sale de su forma (la de verdad la mira el servidor al aceptarlo).
	return bCandidate && Component->CalculateMass() <= MaxMassKg;
}

bool UTN_VRGrabComponent::IsGrabbableHere(UPrimitiveComponent* Component) const
{
	const AActor* Target = Component ? Component->GetOwner() : nullptr;
	// Lo que mueve el servidor (actor replicado y esta máquina no es el servidor): por clase o etiqueta; el servidor decide.
	if (Target && Target->GetIsReplicated() && !Target->HasAuthority())
	{
		return IsGrabbableFromClient(Component, GetOwner(), MaxMass);
	}
	return IsGrabbable(Component, GetOwner(), MaxMass);
}

UPrimitiveComponent* UTN_VRGrabComponent::FindGrabbable(const FVector& At) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNVRGrab), false, GetOwner());
	World->OverlapMultiByObjectType(Overlaps, At, FQuat::Identity, Objects, FCollisionShape::MakeSphere(GrabRadius), Params);
	UPrimitiveComponent* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Candidate = Overlap.GetComponent();
		if (!IsGrabbableHere(Candidate))
		{
			continue;
		}
		FVector Closest;
		const float Distance = Candidate->GetClosestPointOnCollision(At, Closest);
		// Dentro del objeto (0) o sin colisión simple (-1): cuenta como tocándolo.
		const float Score = Distance < 0.f ? static_cast<float>(FVector::Dist(At, Candidate->GetComponentLocation())) : Distance;
		if (Score < BestDistance)
		{
			BestDistance = Score;
			Best = Candidate;
		}
	}
	return Best;
}

UPhysicsHandleComponent* UTN_VRGrabComponent::GetHandle(int32 Hand)
{
	TObjectPtr<UPhysicsHandleComponent>& Handle = Hand == 0 ? LeftHandle : RightHandle;
	if (!Handle && GetOwner())
	{
		Handle = NewObject<UPhysicsHandleComponent>(GetOwner(), Hand == 0 ? TEXT("VRGrabHandleLeft") : TEXT("VRGrabHandleRight"), RF_Transient);
		// Firme pero con algo de muelle: lo cogido no atraviesa las paredes y pesa un poco en la mano.
		Handle->RegisterComponent();
		Handle->SetLinearStiffness(1500.f);
		Handle->SetLinearDamping(120.f);
		Handle->SetAngularStiffness(1200.f);
		Handle->SetAngularDamping(300.f);
		Handle->SetInterpolationSpeed(40.f);
	}
	return Handle;
}

bool UTN_VRGrabComponent::IsGrabbing(int32 Hand) const
{
	return TNVRGrabDetail::IsValidHand(Hand) && Held[Hand].IsValid();
}

bool UTN_VRGrabComponent::TryGrab(int32 Hand, const FTransform& HandWorld)
{
	if (!TNVRGrabDetail::IsValidHand(Hand) || IsGrabbing(Hand))
	{
		return false;
	}
	UPrimitiveComponent* Target = FindGrabbable(HandWorld.GetLocation());
	if (!Target)
	{
		return false;
	}
	const AActor* TargetActor = Target->GetOwner();
	// Replicado y no somos el servidor: lo coge y lo mueve el servidor (que también lo valida).
	const bool bViaServer = TargetActor && TargetActor->GetIsReplicated() && !TargetActor->HasAuthority();
	if (bViaServer)
	{
		// Lo mueve el servidor; aquí solo se recuerda para mandarle la mano y soltarlo.
		Held[Hand] = Target;
		bHeldByServer[Hand] = true;
		LastMoveSent[Hand] = -1.0;
		ServerGrab(static_cast<uint8>(Hand), Target, HandWorld.GetLocation(), HandWorld.Rotator());
		return true;
	}
	return GrabHere(Hand, Target, HandWorld);
}

void UTN_VRGrabComponent::UpdateGrab(int32 Hand, const FTransform& HandWorld)
{
	if (!IsGrabbing(Hand))
	{
		return;
	}
	if (!bHeldByServer[Hand])
	{
		MoveHere(Hand, HandWorld);
		return;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetRealTimeSeconds() : 0.0;
	if (LastMoveSent[Hand] < 0.0 || Now - LastMoveSent[Hand] >= 1.0 / TNVRGrabDetail::SendRate)
	{
		LastMoveSent[Hand] = Now;
		ServerMoveGrab(static_cast<uint8>(Hand), HandWorld.GetLocation(), HandWorld.Rotator());
	}
}

void UTN_VRGrabComponent::Release(int32 Hand, const FVector& HandVelocity)
{
	if (!TNVRGrabDetail::IsValidHand(Hand) || !Held[Hand].IsValid())
	{
		if (TNVRGrabDetail::IsValidHand(Hand))
		{
			Held[Hand].Reset();
			bHeldByServer[Hand] = false;
		}
		return;
	}
	const FVector Velocity = TNVRMath::ThrowVelocity(HandVelocity);
	if (bHeldByServer[Hand])
	{
		Held[Hand].Reset();
		bHeldByServer[Hand] = false;
		ServerRelease(static_cast<uint8>(Hand), Velocity);
		return;
	}
	ReleaseHere(Hand, Velocity);
}

bool UTN_VRGrabComponent::GrabHere(int32 Hand, UPrimitiveComponent* Target, const FTransform& HandWorld)
{
	UPhysicsHandleComponent* Handle = GetHandle(Hand);
	if (!Handle || !Target)
	{
		return false;
	}
	// Lo que llevara ya esa mano, fuera (con todo lo suyo: el agarre y, en el servidor, que vuelva a dormirse en red).
	if (Held[Hand].IsValid() || Handle->GetGrabbedComponent())
	{
		ReleaseHere(Hand, FVector::ZeroVector);
	}
	// Se coge por el punto más cercano a la mano, con el giro que tiene: no salta a la palma.
	FVector GrabPoint;
	if (Target->GetClosestPointOnCollision(HandWorld.GetLocation(), GrabPoint) < 0.f)
	{
		GrabPoint = Target->GetComponentLocation();
	}
	const FTransform GrabWorld(Target->GetComponentQuat(), GrabPoint);
	HeldFromHand[Hand] = GrabWorld.GetRelativeTransform(HandWorld);
	Target->WakeAllRigidBodies();
	Handle->GrabComponentAtLocationWithRotation(Target, NAME_None, GrabPoint, Target->GetComponentRotation());
	Held[Hand] = Target;
	bHeldByServer[Hand] = false;
	KeepAwakeWhileHeld(Hand, Target->GetOwner());
	UE_LOG(LogTortunabo, Verbose, TEXT("[VR] %s coge %s con la aleta %s."), *GetNameSafe(GetOwner()), *GetNameSafe(Target->GetOwner()),
		Hand == 0 ? TEXT("izquierda") : TEXT("derecha"));
	return true;
}

void UTN_VRGrabComponent::MoveHere(int32 Hand, const FTransform& HandWorld)
{
	UPhysicsHandleComponent* Handle = Hand == 0 ? LeftHandle.Get() : RightHandle.Get();
	UPrimitiveComponent* Target = Held[Hand].Get();
	if (!Handle || !Target || Handle->GetGrabbedComponent() != Target || !Target->IsSimulatingPhysics())
	{
		// Se ha roto, lo ha cogido otro sistema o ya no tiene física: se suelta.
		ReleaseHere(Hand, FVector::ZeroVector);
		return;
	}
	const FTransform Goal = HeldFromHand[Hand] * HandWorld;
	Handle->SetTargetLocationAndRotation(Goal.GetLocation(), Goal.Rotator());
}

void UTN_VRGrabComponent::ReleaseHere(int32 Hand, const FVector& Velocity)
{
	if (!TNVRGrabDetail::IsValidHand(Hand))
	{
		return;
	}
	UPhysicsHandleComponent* Handle = Hand == 0 ? LeftHandle.Get() : RightHandle.Get();
	UPrimitiveComponent* Target = Held[Hand].Get();
	if (Handle && Handle->GetGrabbedComponent())
	{
		Handle->ReleaseComponent();
	}
	if (Target && Target->IsSimulatingPhysics() && !Velocity.IsNearlyZero())
	{
		Target->SetPhysicsLinearVelocity(Velocity);
	}
	// Servidor: el objeto con física vuelve a dormirse en red cuando se pare (su temporizador de siempre).
	if (ATN_PhysicsObjectActor* Physics = Cast<ATN_PhysicsObjectActor>(AwakeActor[Hand].Get()))
	{
		Physics->SetExternallyHeld(false);
	}
	AwakeActor[Hand].Reset();
	Held[Hand].Reset();
	bHeldByServer[Hand] = false;
}

void UTN_VRGrabComponent::KeepAwakeWhileHeld(int32 Hand, AActor* Target)
{
	AwakeActor[Hand].Reset();
	// Solo el servidor de un actor replicado: los clientes lo ven moverse por su réplica, que no sale si está dormido en red
	// (ATN_PhysicsObjectActor duerme con DORM_DormantAll hasta que algo lo golpea).
	if (!Target || !Target->GetIsReplicated() || !Target->HasAuthority())
	{
		return;
	}
	if (ATN_PhysicsObjectActor* Physics = Cast<ATN_PhysicsObjectActor>(Target))
	{
		Physics->SetExternallyHeld(true);
		AwakeActor[Hand] = Target;
	}
	else if (Target->NetDormancy > DORM_Awake)
	{
		// Otro actor replicado (etiqueta VRGrab): despierto desde ahora (no tiene temporizador para volver a dormirse).
		Target->FlushNetDormancy();
		Target->SetNetDormancy(DORM_Awake);
	}
}

void UTN_VRGrabComponent::ServerGrab_Implementation(uint8 Hand, UPrimitiveComponent* Target, FVector_NetQuantize10 HandLocation, FRotator HandRotation)
{
	const AActor* Owner = GetOwner();
	if (!TNVRGrabDetail::IsValidHand(Hand))
	{
		return;
	}
	// Aquí sí simula (el servidor): con física de verdad, móvil, con su movimiento replicado y de hasta MaxMass kg.
	if (!Owner || !IsGrabbable(Target, Owner, MaxMass))
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[VR] %s: agarre de %s rechazado (no se puede coger)."), *GetNameSafe(Owner), *GetNameSafe(Target ? Target->GetOwner() : nullptr));
		ClientGrabRejected(Hand, Target);
		return;
	}
	// Al alcance de la tortuga y de su mano (con margen por la latencia).
	FVector Closest;
	float HandDistance = Target->GetClosestPointOnCollision(HandLocation, Closest);
	if (HandDistance < 0.f)
	{
		HandDistance = static_cast<float>(FVector::Dist(HandLocation, Target->GetComponentLocation()));
	}
	if (FVector::Dist(Owner->GetActorLocation(), HandLocation) > MaxServerReach
		|| HandDistance > GrabRadius + TNVRGrabDetail::ServerGrabSlack)
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[VR] %s: agarre de %s rechazado (lejos)."), *GetNameSafe(Owner), *GetNameSafe(Target->GetOwner()));
		ClientGrabRejected(Hand, Target);
		return;
	}
	if (!GrabHere(Hand, Target, FTransform(HandRotation, HandLocation)))
	{
		ClientGrabRejected(Hand, Target);
	}
}

void UTN_VRGrabComponent::ClientGrabRejected_Implementation(uint8 Hand, UPrimitiveComponent* Target)
{
	// El dueño deja de mandar la mano (ATN_VRRig ve que ya no coge nada y suelta el agarre).
	if (TNVRGrabDetail::IsValidHand(Hand) && bHeldByServer[Hand] && Held[Hand].Get() == Target)
	{
		Held[Hand].Reset();
		bHeldByServer[Hand] = false;
	}
}

void UTN_VRGrabComponent::ServerMoveGrab_Implementation(uint8 Hand, FVector_NetQuantize10 HandLocation, FRotator HandRotation)
{
	const AActor* Owner = GetOwner();
	if (!TNVRGrabDetail::IsValidHand(Hand) || !Owner || !Held[Hand].IsValid())
	{
		return;
	}
	// La mano no se aleja de la tortuga más de lo que llega (un cliente no arrastra cosas por el mapa).
	FVector Location = HandLocation;
	const FVector FromOwner = Location - Owner->GetActorLocation();
	if (FromOwner.SizeSquared() > FMath::Square(MaxServerReach))
	{
		Location = Owner->GetActorLocation() + FromOwner.GetSafeNormal() * MaxServerReach;
	}
	MoveHere(Hand, FTransform(HandRotation, Location));
}

void UTN_VRGrabComponent::ServerRelease_Implementation(uint8 Hand, FVector_NetQuantize10 Velocity)
{
	ReleaseHere(Hand, TNVRMath::ThrowVelocity(Velocity));
}
