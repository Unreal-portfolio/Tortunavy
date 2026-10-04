#include "VR/TN_VRGrabComponent.h"
#include "VR/TN_VRMath.h"
#include "VR/TN_VRHandMath.h"
#include "Core/TN_Log.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_ShellBody.h"
#include "Player/TortugaCharacter.h"
#include "World/TN_PhysicsObjectActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
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

	/** Quién lleva cada componente en esta máquina y con qué manos (bit 0 izquierda, bit 1 derecha). */
	struct FHolder
	{
		TWeakObjectPtr<UTN_VRGrabComponent> Grabber;
		uint8 Hands = 0;
	};

	TMap<TWeakObjectPtr<const UPrimitiveComponent>, FHolder>& Holders()
	{
		static TMap<TWeakObjectPtr<const UPrimitiveComponent>, FHolder> Map;
		return Map;
	}

	/**
	 * Fuera las entradas de lo que ya no existe o de quien ya no existe: un objeto destruido mientras se llevaba (lo rompen,
	 * cae al agua) no pasa por RemoveHolder y su entrada se quedaba para siempre.
	 */
	void PruneHolders()
	{
		for (auto It = Holders().CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid() || !It.Value().Grabber.IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}

	void AddHolder(const UPrimitiveComponent* Component, UTN_VRGrabComponent* Grabber, int32 Hand)
	{
		PruneHolders();
		FHolder& Holder = Holders().FindOrAdd(Component);
		if (Holder.Grabber.Get() != Grabber)
		{
			Holder.Grabber = Grabber;
			Holder.Hands = 0;
		}
		Holder.Hands |= static_cast<uint8>(1 << Hand);
	}

	void RemoveHolder(const UPrimitiveComponent* Component, const UTN_VRGrabComponent* Grabber, int32 Hand)
	{
		FHolder* Holder = Holders().Find(Component);
		if (!Holder || Holder->Grabber.Get() != Grabber)
		{
			return;
		}
		Holder->Hands &= static_cast<uint8>(~(1 << Hand));
		if (Holder->Hands == 0)
		{
			Holders().Remove(Component);
		}
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

bool UTN_VRGrabComponent::CanOwnerGrab(const AActor* Owner)
{
	const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Owner);
	if (!Turtle)
	{
		return Owner != nullptr;
	}
	// Solo un jugador en VR (en el servidor, bVRPlayer le llega antes que el agarre por el mismo canal fiable).
	const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
	return Turtle->IsVRPlayer() && !Turtle->IsKnockedDown() && !Turtle->IsDead() && !Turtle->IsInShell()
		&& !(Carry && Carry->IsBeingCarried());

}

UTN_VRGrabComponent* UTN_VRGrabComponent::FindHolder(const UPrimitiveComponent* Component)
{
	TMap<TWeakObjectPtr<const UPrimitiveComponent>, TNVRGrabDetail::FHolder>& Map = TNVRGrabDetail::Holders();
	const TNVRGrabDetail::FHolder* Holder = Component ? Map.Find(Component) : nullptr;
	if (!Holder)
	{
		return nullptr;
	}
	UTN_VRGrabComponent* Grabber = Holder->Grabber.Get();
	if (!Grabber || (Grabber->GetHeld(0) != Component && Grabber->GetHeld(1) != Component))
	{
		// Quien lo llevaba ya no existe o ya no lo lleva (lo soltó sin pasar por aquí): está libre.
		Map.Remove(Component);
		return nullptr;
	}
	return Grabber;
}

int32 UTN_VRGrabComponent::NumHolderEntries()
{
	return TNVRGrabDetail::Holders().Num();
}

UPrimitiveComponent* UTN_VRGrabComponent::GetHeld(int32 Hand) const
{
	return TNVRGrabDetail::IsValidHand(Hand) ? Held[Hand].Get() : nullptr;
}

bool UTN_VRGrabComponent::IsOwnerLocal() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return !Pawn || Pawn->IsLocallyControlled();
}

void UTN_VRGrabComponent::SetOwnerIgnores(UPrimitiveComponent* Target, bool bIgnore) const
{
	// La cápsula (la raíz) barre con su movimiento: sin esto la tortuga se sube a lo que lleva en la mano y se eleva con
	// ello, o lo empuja al andar. Lo hace cada máquina que mueve la cápsula (el dueño y el servidor) para que no se corrijan.
	UPrimitiveComponent* Body = GetOwner() ? Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()) : nullptr;
	if (Body && Target)
	{
		Body->IgnoreComponentWhenMoving(Target, bIgnore);
	}
}

void UTN_VRGrabComponent::RememberGrabPoint(int32 Hand, UPrimitiveComponent* Target, const FTransform& HandWorld)
{
	// Se coge por el punto más cercano a la mano, con el giro que tiene: no salta a la palma.
	FVector GrabPoint;
	if (Target->GetClosestPointOnCollision(HandWorld.GetLocation(), GrabPoint) < 0.f)
	{
		GrabPoint = Target->GetComponentLocation();
	}
	const FTransform GrabWorld(Target->GetComponentQuat(), GrabPoint);
	HeldFromHand[Hand] = GrabWorld.GetRelativeTransform(HandWorld);
	GrabPointLocal[Hand] = Target->GetComponentTransform().InverseTransformPosition(GrabPoint);
	StrainSince[Hand] = -1.0;
}

float UTN_VRGrabComponent::SeparationFromHand(int32 Hand, const FTransform& HandWorld) const
{
	const UPrimitiveComponent* Target = GetHeld(Hand);
	if (!Target)
	{
		return 0.f;
	}
	const FVector Now = Target->GetComponentTransform().TransformPosition(GrabPointLocal[Hand]);
	const FVector Goal = (HeldFromHand[Hand] * HandWorld).GetLocation();
	return static_cast<float>(FVector::Dist(Now, Goal));
}

float UTN_VRGrabComponent::GetStrain(int32 Hand, const FTransform& HandWorld) const
{
	if (!IsGrabbing(Hand) || bHeldByServer[Hand])
	{
		return 0.f;
	}
	return SeparationFromHand(Hand, HandWorld);
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

bool UTN_VRGrabComponent::HasClearReach(const UWorld* World, const FVector& Eyes, const FVector& Point, const FCollisionQueryParams& Params,
	bool bPenetratingBlocks)
{
	const double Length = FVector::Dist(Eyes, Point);
	if (!World || Length <= ReachTolerance)
	{
		return true;
	}
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Eyes, Point, ECC_Camera, Params))
	{
		return true;
	}
	return Hit.bStartPenetrating ? !bPenetratingBlocks : Hit.Distance >= Length - ReachTolerance;
}

FVector UTN_VRGrabComponent::GetReachEyes() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}
	if (const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Owner))
	{
		if (const UCameraComponent* Eyes = Turtle->GetVRCamera())
		{
			return Eyes->GetComponentLocation();
		}
	}
	FVector Location;
	FRotator Rotation;
	Owner->GetActorEyesViewPoint(Location, Rotation);
	return Location;
}

FCollisionQueryParams UTN_VRGrabComponent::MakeReachParams(const AActor* Target) const
{
	// Como al parar las manos: solo el escenario, ni la tortuga ni lo que lleva encima o en las manos.
	const AActor* Owner = GetOwner();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNVRReach), false, Owner);
	if (Owner)
	{
		TArray<AActor*> Attached;
		Owner->GetAttachedActors(Attached);
		Params.AddIgnoredActors(Attached);
	}
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		if (const UPrimitiveComponent* HeldNow = GetHeld(Hand))
		{
			Params.AddIgnoredComponent(HeldNow);
		}
	}
	if (Target)
	{
		Params.AddIgnoredActor(Target);
	}
	return Params;
}

bool UTN_VRGrabComponent::CanReach(const AActor* Target, const FVector& Point, bool bPenetratingBlocks) const
{
	if (!GetOwner())
	{
		return true;
	}
	return HasClearReach(GetWorld(), GetReachEyes(), Point, MakeReachParams(Target), bPenetratingBlocks);
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
		// Lo que lleva otra tortuga (en esta máquina; lo que mueve el servidor lo comprueba él) no se le quita de la mano.
		const UTN_VRGrabComponent* Holder = FindHolder(Candidate);
		if (!IsGrabbableHere(Candidate) || (Holder && Holder != this))
		{
			continue;
		}
		FVector Closest;
		const float Distance = Candidate->GetClosestPointOnCollision(At, Closest);
		// Dentro del objeto (0) o sin colisión simple (-1): cuenta como tocándolo.
		const float Score = Distance < 0.f ? static_cast<float>(FVector::Dist(At, Candidate->GetComponentLocation())) : Distance;
		// La punta se para en la pared, pero el radio de búsqueda la pasa: lo que queda al otro lado no se coge.
		if (Score >= BestDistance || !CanReach(Candidate->GetOwner(), Distance < 0.f ? Candidate->GetComponentLocation() : Closest))
		{
			continue;
		}
		BestDistance = Score;
		Best = Candidate;
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
	if (!TNVRGrabDetail::IsValidHand(Hand) || IsGrabbing(Hand) || !CanOwnerGrab(GetOwner()))
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
		RememberGrabPoint(Hand, Target, HandWorld);
		SetOwnerIgnores(Target, true);
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
	if (!TNVRGrabDetail::IsValidHand(Hand))
	{
		return;
	}
	if (!Held[Hand].IsValid())
	{
		// Ya no existe (destruido mientras se llevaba) o ya se soltó: se recoge lo que quede en esta máquina (el agarre, el
		// registro). Lo que movía el servidor ya lo ha soltado él.
		if (bHeldByServer[Hand])
		{
			Held[Hand].Reset();
			bHeldByServer[Hand] = false;
		}
		else
		{
			ReleaseHere(Hand, FVector::ZeroVector);
		}
		return;
	}
	const FVector Velocity = TNVRMath::ThrowVelocity(HandVelocity);
	if (bHeldByServer[Hand])
	{
		SetOwnerIgnores(Held[Hand].Get(), false);
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
	RememberGrabPoint(Hand, Target, HandWorld);
	const FVector GrabPoint = Target->GetComponentTransform().TransformPosition(GrabPointLocal[Hand]);
	Target->WakeAllRigidBodies();
	// Lo que va en la mano puede ir deprisa (y salir lanzado): con CCD no atraviesa paredes finas. Se queda puesto.
	Target->SetUseCCD(true);
	Handle->GrabComponentAtLocationWithRotation(Target, NAME_None, GrabPoint, Target->GetComponentRotation());
	Held[Hand] = Target;
	bHeldByServer[Hand] = false;
	TNVRGrabDetail::AddHolder(Target, this, Hand);
	SetOwnerIgnores(Target, true);
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
		DropOnServer(Hand);
		return;
	}
	// Enganchado lejos de la mano (detrás de una pared, sujeto por algo): se suelta en vez de seguir tirando de él.
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (TNVRHands::ShouldBreakGrab(SeparationFromHand(Hand, HandWorld), Now, StrainSince[Hand]))
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[VR] %s: %s se ha enganchado y se suelta."), *GetNameSafe(GetOwner()), *GetNameSafe(Target->GetOwner()));
		DropOnServer(Hand);
		return;
	}
	const FTransform Goal = HeldFromHand[Hand] * HandWorld;
	Handle->SetTargetLocationAndRotation(Goal.GetLocation(), Goal.Rotator());
}

void UTN_VRGrabComponent::DropOnServer(int32 Hand)
{
	UPrimitiveComponent* Target = GetHeld(Hand);
	ReleaseHere(Hand, FVector::ZeroVector);
	// El dueño es un cliente: lo cree cogido y sigue mandando la mano; se le dice que ya no.
	if (Target && !IsOwnerLocal())
	{
		ClientGrabLost(static_cast<uint8>(Hand), Target);
	}
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
	if (Target)
	{
		TNVRGrabDetail::RemoveHolder(Target, this, Hand);
		SetOwnerIgnores(Target, false);
	}
	else if (!Held[Hand].IsExplicitlyNull())
	{
		// Se ha destruido mientras se llevaba: su entrada del registro ya no se encuentra por el objeto.
		TNVRGrabDetail::PruneHolders();
	}
	// Servidor: el objeto con física vuelve a dormirse en red cuando se pare (su temporizador de siempre).
	if (ATN_PhysicsObjectActor* Physics = Cast<ATN_PhysicsObjectActor>(AwakeActor[Hand].Get()))
	{
		Physics->SetExternallyHeld(false);
	}
	AwakeActor[Hand].Reset();
	Held[Hand].Reset();
	bHeldByServer[Hand] = false;
	StrainSince[Hand] = -1.0;
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
	// Aquí sí simula (el servidor): con física de verdad, móvil, con su movimiento replicado y de hasta MaxMass kg; y la
	// tortuga puede usar las manos (ni derribada, ni muerta, ni en el caparazón, ni llevada).
	if (!Owner || !CanOwnerGrab(Owner) || !IsGrabbable(Target, Owner, MaxMass))
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[VR] %s: agarre de %s rechazado (no se puede coger)."), *GetNameSafe(Owner), *GetNameSafe(Target ? Target->GetOwner() : nullptr));
		ClientGrabLost(Hand, Target);
		return;
	}
	// Lo lleva otra tortuga: dos manos de dos jugadores tirando del mismo objeto lo harían temblar o salir disparado.
	const UTN_VRGrabComponent* Holder = FindHolder(Target);
	if (Holder && Holder != this)
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[VR] %s: agarre de %s rechazado (lo lleva %s)."), *GetNameSafe(Owner), *GetNameSafe(Target->GetOwner()),
			*GetNameSafe(Holder->GetOwner()));
		ClientGrabLost(Hand, Target);
		return;
	}
	// Al alcance de la tortuga y de su mano (con margen por la latencia).
	FVector Closest;
	float HandDistance = Target->GetClosestPointOnCollision(HandLocation, Closest);
	if (HandDistance < 0.f)
	{
		Closest = Target->GetComponentLocation();
		HandDistance = static_cast<float>(FVector::Dist(HandLocation, Closest));
	}
	if (FVector::Dist(Owner->GetActorLocation(), HandLocation) > MaxServerReach
		|| HandDistance > GrabRadius + TNVRGrabDetail::ServerGrabSlack)
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[VR] %s: agarre de %s rechazado (lejos)."), *GetNameSafe(Owner), *GetNameSafe(Target->GetOwner()));
		ClientGrabLost(Hand, Target);
		return;
	}
	// Que se vea, como en el dueño (un cliente no coge a través de una pared): desde los ojos del peón o, como los de verdad
	// pueden estar algo apartados de la cápsula, pasando por la mano que manda (los ojos ven la mano y la mano ve el objeto).
	// Aquí un trazo que empieza dentro de algo cuenta como tapado (cabeza o mano metidas en una pared); entonces vale que lo
	// vea el centro de la cápsula, que el movimiento no deja dentro del escenario.
	const AActor* TargetActor = Target->GetOwner();
	const FCollisionQueryParams ReachParams = MakeReachParams(TargetActor);
	const bool bSeen = CanReach(TargetActor, Closest, true)
		|| (CanReach(TargetActor, HandLocation, true) && HasClearReach(GetWorld(), HandLocation, Closest, ReachParams, true))
		|| HasClearReach(GetWorld(), Owner->GetActorLocation(), Closest, ReachParams, true);
	if (!bSeen)
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[VR] %s: agarre de %s rechazado (algo en medio)."), *GetNameSafe(Owner), *GetNameSafe(TargetActor));
		ClientGrabLost(Hand, Target);
		return;
	}
	if (!GrabHere(Hand, Target, FTransform(HandRotation, HandLocation)))
	{
		ClientGrabLost(Hand, Target);
	}
}

void UTN_VRGrabComponent::ClientGrabLost_Implementation(uint8 Hand, UPrimitiveComponent* Target)
{
	// El dueño deja de mandar la mano (ATN_VRRig ve que ya no coge nada, suelta el agarre y vibra).
	if (TNVRGrabDetail::IsValidHand(Hand) && bHeldByServer[Hand] && Held[Hand].Get() == Target)
	{
		SetOwnerIgnores(Target, false);
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
	// Derribada, muerta, en el caparazón o llevada mientras lo llevaba: se le cae.
	if (!CanOwnerGrab(Owner))
	{
		DropOnServer(Hand);
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
