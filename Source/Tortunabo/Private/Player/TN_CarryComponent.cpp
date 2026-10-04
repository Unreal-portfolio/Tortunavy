#include "Player/TN_CarryComponent.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TN_ShellBody.h"
#include "Player/TN_StaminaComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Player/TN_TurtleActionSfx.h"

UTN_CarryComponent::UTN_CarryComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
	// Sonidos de serie de coger y lanzar a otra tortuga (#348); el Blueprint puede cambiarlos.
	GrabSound = TNTurtleActionSfx::FindDefaultSound(ETNTurtleActionSfx::Pickup);
	ThrowSound = TNTurtleActionSfx::FindDefaultSound(ETNTurtleActionSfx::Throw);
}

void UTN_CarryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTN_CarryComponent, CarriedTurtle);
	DOREPLIFETIME(UTN_CarryComponent, CarriedBy);
	DOREPLIFETIME_CONDITION(UTN_CarryComponent, bCarriedStruggling, COND_OwnerOnly);
	// El dueño empieza la toma de impulso al pulsar la E; los demás, con esto.
	DOREPLIFETIME_CONDITION(UTN_CarryComponent, ThrowWindupSerial, COND_SkipOwner);
}

ATortugaCharacter* UTN_CarryComponent::GetTurtle() const
{
	return Cast<ATortugaCharacter>(GetOwner());
}

bool UTN_CarryComponent::CanBeGrabbed(const ATortugaCharacter* Target) const
{
	const ATortugaCharacter* Self = GetTurtle();
	if (!Target || Target == Self || Target->IsDead())
	{
		return false;
	}
	if (!Target->IsInShell() && !Target->IsKnockedDown())
	{
		return false;
	}
	const UTN_CarryComponent* Other = Target->GetCarryComponent();
	return Other && !Other->IsBeingCarried() && !Other->IsCarrying();
}

// ─────────────────────────────────────────────────────────────────────────────
// Input
// ─────────────────────────────────────────────────────────────────────────────

bool UTN_CarryComponent::TryGrabNearest()
{
	ATortugaCharacter* Self = GetTurtle();
	if (!Self || Self->IsDead() || Self->IsKnockedDown() || Self->IsInShell() || IsCarrying() || IsBeingCarried() || !GetWorld())
	{
		return false;
	}

	ATortugaCharacter* Best = nullptr;
	float BestDist = GrabRange;
	const FVector Forward = Self->GetActorForwardVector();
	for (TActorIterator<ATortugaCharacter> It(GetWorld()); It; ++It)
	{
		ATortugaCharacter* Candidate = *It;
		if (!CanBeGrabbed(Candidate)) { continue; }
		const FVector To = Candidate->GetActorLocation() - Self->GetActorLocation();
		const float Dist = To.Size();
		const bool bInFront = FVector::DotProduct(Forward, To.GetSafeNormal2D()) > 0.2f || Dist < 120.f;
		if (Dist < BestDist && bInFront)
		{
			BestDist = Dist;
			Best = Candidate;
		}
	}
	if (!Best)
	{
		return false;
	}
	ServerGrab(Best);
	return true;
}

void UTN_CarryComponent::RequestThrow()
{
	if (!IsCarrying()) { return; }
	// Ya tomando impulso: el servidor tampoco aceptaría otro lanzamiento hasta soltarla.
	if (GetThrowWindupAlpha() >= 0.f) { return; }
	const ATortugaCharacter* Self = GetTurtle();
	// Hacia donde mira la cámara; en VR, hacia donde apunta la aleta derecha.
	const FRotator Aim = Self ? Self->GetTurtleAimRotation() : FRotator::ZeroRotator;
	// Las aletas se echan atrás al momento en esta máquina (el servidor la suelta al acabar la toma de impulso).
	BeginLocalThrowWindup();
	ServerThrow(Aim);
}

float UTN_CarryComponent::GetThrowWindupAlpha() const
{
	const UWorld* World = GetWorld();
	if (LocalWindupStart < 0.0 || !World || !IsCarrying())
	{
		return -1.f;
	}
	const double Elapsed = World->GetTimeSeconds() - LocalWindupStart;
	// Si pasado un rato no la ha soltado (el servidor no llegó a lanzarla), deja de tomar impulso.
	if (Elapsed > static_cast<double>(ThrowWindupSeconds) + 0.6)
	{
		return -1.f;
	}
	return ThrowWindupSeconds > 0.01f ? FMath::Clamp(static_cast<float>(Elapsed) / ThrowWindupSeconds, 0.f, 1.f) : 1.f;
}

void UTN_CarryComponent::BeginLocalThrowWindup()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	// Ya la estaba tomando (el anfitrión que lanza la empieza al pulsar y otra vez al recibir su propio lanzamiento).
	if (LocalWindupStart >= 0.0 && Now - LocalWindupStart < static_cast<double>(ThrowWindupSeconds) + 0.6)
	{
		return;
	}
	LocalWindupStart = Now;
}

void UTN_CarryComponent::OnRep_ThrowWindupSerial()
{
	if (IsCarrying())
	{
		BeginLocalThrowWindup();
	}
}

void UTN_CarryComponent::RequestDrop()
{
	if (IsCarrying())
	{
		ServerDrop();
	}
}

void UTN_CarryComponent::SetStruggleInput(bool bInStruggling)
{
	const bool bWant = bInStruggling && IsBeingCarried();
	if (bWant != bLocalStruggleSent)
	{
		bLocalStruggleSent = bWant;
		ServerSetStruggling(bWant);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

void UTN_CarryComponent::ServerGrab_Implementation(ATortugaCharacter* Target)
{
	ATortugaCharacter* Self = GetTurtle();
	if (!Self || Self->IsDead() || Self->IsKnockedDown() || Self->IsInShell() || IsCarrying() || IsBeingCarried())
	{
		return;
	}
	if (!CanBeGrabbed(Target) || FVector::Dist(Self->GetActorLocation(), Target->GetActorLocation()) > GrabRange + 80.f)
	{
		return;
	}

	// Aturdida: pasa a ser una bola de caparazón mientras la llevan.
	if (Target->IsKnockedDown())
	{
		Target->RecoverFromKnockdown();
	}
	if (UTN_ShellComponent* Shell = Target->GetShellComponent())
	{
		// Sin cuerpo físico mientras la llevan: si estaba suelta como caja, se quita antes de engancharla.
		Shell->ForceEnterShell(false);
		Shell->StopBody();
		Shell->SetExitLocked(true);
	}

	UTN_CarryComponent* Other = Target->GetCarryComponent();
	CarriedTurtle = Target;
	bCarriedStruggling = false;
	Other->CarriedBy = Self;
	Other->bStruggling = false;
	Other->StruggleTime = 0.f;
	Other->bAwaitingBounce = false;
	ApplyCarrierLocalState(true);
	Other->ApplyCarriedLocalState(Self);

	if (GrabSound)
	{
		Self->MulticastPlaySfx(GrabSound);
	}
}

void UTN_CarryComponent::ServerThrow_Implementation(FRotator AimRotation)
{
	const ATortugaCharacter* Self = GetTurtle();
	UWorld* World = GetWorld();
	// Puntería del cliente sin sanear: un NaN acabaría en la velocidad del lanzamiento en el host.
	if (!Self || !CarriedTurtle || bThrowWindupPending || AimRotation.ContainsNaN())
	{
		return;
	}
	if (ThrowWindupSeconds <= 0.01f || !World)
	{
		ThrowCarried(AimRotation);
		return;
	}

	// Saque de banda: primero las aletas detrás de la cabeza (todas las máquinas lo ven con ThrowWindupSerial); al acabar,
	// la suelta hacia donde apuntaba al pulsar.
	bThrowWindupPending = true;
	PendingThrowAim = AimRotation;
	ThrowWindupSerial = ThrowWindupSerial >= 255 ? static_cast<uint8>(1) : static_cast<uint8>(ThrowWindupSerial + 1);
	BeginLocalThrowWindup();
	World->GetTimerManager().SetTimer(ThrowWindupTimer,
		FTimerDelegate::CreateUObject(this, &UTN_CarryComponent::FinishThrowWindup), ThrowWindupSeconds, false);
}

void UTN_CarryComponent::FinishThrowWindup()
{
	bThrowWindupPending = false;
	if (CarriedTurtle)
	{
		ThrowCarried(PendingThrowAim);
	}
}

void UTN_CarryComponent::CancelThrowWindup()
{
	if (!bThrowWindupPending)
	{
		return;
	}
	bThrowWindupPending = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ThrowWindupTimer);
	}
}

void UTN_CarryComponent::ThrowCarried(const FRotator& AimRotation)
{
	ATortugaCharacter* Self = GetTurtle();
	ATortugaCharacter* Carried = CarriedTurtle;
	if (!Self || !Carried)
	{
		return;
	}

	// Hacia donde mira la cámara, con el arco bajo de todos los lanzamientos (~25° con la cámara a nivel).
	const float Speed = ThrowSpeed * (bCarriedStruggling ? StruggleThrowMultiplier : 1.f);
	const FVector Flat = FRotator(0.f, AimRotation.Yaw, 0.f).Vector();
	const FVector Start = Self->GetActorLocation() + Flat * 70.f + FVector(0.f, 0.f, CarryHeight + 20.f);
	// Al punto del centro de la pantalla (en VR, hacia la aleta).
	// Dentro del caparazón la caja frena un poco en el aire: se compensa para que llegue al punto.
	const UTN_ShellComponent* CarriedShell = Carried->GetShellComponent();
	const float Damping = CarriedShell && CarriedShell->IsInShell() ? ATN_ShellBody::BoxLinearDamping : 0.f;
	const FVector Dir = Self->GetThrowDirectionToCrosshair(Start, AimRotation, Speed, 0.f, Damping);

	Release(Carried, Start, Dir * Speed, true, true);
	if (ThrowSound)
	{
		Self->MulticastPlaySfx(ThrowSound);
	}
}

void UTN_CarryComponent::ThrowWithDive(const FVector& DiveDir, const FVector& DiveVelocity, const FVector& CarrierVelocity)
{
	ATortugaCharacter* Self = GetTurtle();
	ATortugaCharacter* Carried = CarriedTurtle;
	if (!Self || !Carried || !Self->HasAuthority())
	{
		return;
	}
	CancelThrowWindup();

	// El lanzamiento de siempre hacia donde se tira de panzazo, más el impulso del panzazo (en horizontal, con la carrera
	// dentro) y el del salto (solo hacia arriba).
	FVector Flat(DiveDir.X, DiveDir.Y, 0.0);
	if (!Flat.Normalize())
	{
		Flat = Self->GetActorForwardVector();
	}
	const FRotator Aim(Self->GetControlRotation().Pitch, Flat.Rotation().Yaw, 0.f);
	const float Speed = ThrowSpeed * (bCarriedStruggling ? StruggleThrowMultiplier : 1.f);
	FVector Velocity = Self->GetThrowDirection(Aim) * Speed
		+ FVector(DiveVelocity.X, DiveVelocity.Y, 0.0) * DiveThrowCarryFactor
		+ FVector(0.0, 0.0, FMath::Max(0.0, CarrierVelocity.Z)) * DiveThrowJumpFactor;
	Velocity = Velocity.GetClampedToMaxSize(DiveThrowMaxSpeed);
	const FVector Start = Self->GetActorLocation() + Flat * 70.0 + FVector(0.0, 0.0, CarryHeight + 20.0);

	Release(Carried, Start, Velocity, true, true);
	if (ThrowSound)
	{
		Self->MulticastPlaySfx(ThrowSound);
	}
}

void UTN_CarryComponent::ServerDrop_Implementation()
{
	ATortugaCharacter* Self = GetTurtle();
	ATortugaCharacter* Carried = CarriedTurtle;
	if (!Self || !Carried)
	{
		return;
	}
	const FVector Start = Self->GetActorLocation() + Self->GetActorForwardVector() * 130.f + FVector(0.f, 0.f, 30.f);
	Release(Carried, Start, FVector::ZeroVector, false, false);
}

void UTN_CarryComponent::ServerSetStruggling_Implementation(bool bInStruggling)
{
	bStruggling = bInStruggling && IsBeingCarried();
	if (!bStruggling)
	{
		StruggleTime = 0.f;
	}
	if (ATortugaCharacter* Carrier = CarriedBy)
	{
		if (UTN_CarryComponent* CarrierComp = Carrier->GetCarryComponent())
		{
			CarrierComp->bCarriedStruggling = bStruggling;
		}
	}
}

void UTN_CarryComponent::ForceRelease(bool bEscapeHop)
{
	ATortugaCharacter* Self = GetTurtle();
	ATortugaCharacter* Carried = CarriedTurtle;
	if (!Self || !Carried || !Self->HasAuthority())
	{
		return;
	}
	const FVector Side = Self->GetActorRightVector() * (FMath::RandBool() ? 1.f : -1.f);
	const FVector Start = Self->GetActorLocation() + Side * 90.f + FVector(0.f, 0.f, CarryHeight);
	const FVector Hop = bEscapeHop ? Side * 350.f + FVector(0.f, 0.f, 450.f) : FVector::ZeroVector;
	// Escapada a base de forcejear: sale disparada como caparazón y sale en cuanto se para.
	Release(Carried, Start, Hop, false, bEscapeHop);
}

void UTN_CarryComponent::Release(ATortugaCharacter* Carried, const FVector& Location, const FVector& Velocity, bool bThrown, bool bExitOnRest)
{
	// Se suelta por lo que sea (lanzada, dejada, escapada, derribo): la toma de impulso pendiente ya no vale.
	CancelThrowWindup();
	UTN_CarryComponent* Other = Carried ? Carried->GetCarryComponent() : nullptr;
	CarriedTurtle = nullptr;
	bCarriedStruggling = false;
	ApplyCarrierLocalState(false);
	if (!Other)
	{
		return;
	}

	Other->CarriedBy = nullptr;
	Other->bStruggling = false;
	Other->StruggleTime = 0.f;
	Other->ApplyCarriedLocalState(nullptr);

	// Hasta donde llegue sin atravesar nada; el cliente dueño se pone en el mismo sitio (ClientApplyThrow).
	const FVector Placed = SweepReleaseLocation(Carried, Location);
	Carried->SetActorLocation(Placed, false, nullptr, ETeleportType::TeleportPhysics);
	UTN_ShellComponent* Shell = Carried->GetShellComponent();
	const bool bAsShell = Shell && Shell->IsInShell();
	// El rebote viejo (NotifyLanded) solo queda para quien no va en el caparazón.
	Other->bAwaitingBounce = bThrown && !bAsShell;
	if (Shell)
	{
		// Lanzada: no sale del caparazón hasta que se para. Soltada: puede salir ya.
		Shell->SetExitLocked(bThrown);
	}
	if (bAsShell)
	{
		// Sale como caparazón con física: vuela, da volteretas, rebota y rueda (la caja se replica sola).
		Shell->StartBody(Velocity, true, bExitOnRest);
	}
	else if (!Velocity.IsNearlyZero())
	{
		Carried->LaunchCharacter(Velocity, true, true);
	}
	Other->ClientApplyThrow(Placed, Velocity, bThrown && !bAsShell);
}

FVector UTN_CarryComponent::SweepReleaseLocation(const ATortugaCharacter* Carried, const FVector& Target) const
{
	const UCapsuleComponent* Capsule = Carried ? Carried->GetCapsuleComponent() : nullptr;
	UWorld* World = GetWorld();
	if (!Capsule || !World)
	{
		return Target;
	}
	// Sin la propia llevada ni quien la lleva (van pegadas y se solapan); lo demás que pararía a su cápsula al andar.
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TNCarryRelease), false, Carried);
	Query.AddIgnoredActor(GetOwner());
	FCollisionResponseParams Response;
	Capsule->InitSweepCollisionParams(Query, Response);
	FHitResult Hit;
	const bool bHit = World->SweepSingleByChannel(Hit, Carried->GetActorLocation(), Target, Capsule->GetComponentQuat(),
		Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(), Query, Response);
	// Si ya empieza metida en algo (poco probable: va encima de quien la lleva), el sitio de siempre.
	return bHit && !Hit.bStartPenetrating ? Hit.Location : Target;
}

void UTN_CarryComponent::ClientApplyThrow_Implementation(FVector StartLocation, FVector Velocity, bool bBounce)
{
	ATortugaCharacter* Self = GetTurtle();
	if (!Self || Self->HasAuthority())
	{
		// En el servidor (o el host) el impulso ya se aplicó en Release.
		return;
	}
	ApplyCarriedLocalState(nullptr);
	// En el caparazón la lleva su caja física, que ya llega replicada: nada de impulso con el movimiento.
	const UTN_ShellComponent* Shell = Self->GetShellComponent();
	if (Shell && (Shell->IsInShell() || Shell->HasLocalBody()))
	{
		bAwaitingBounce = false;
		return;
	}
	Self->SetActorLocation(StartLocation, false, nullptr, ETeleportType::TeleportPhysics);
	bAwaitingBounce = bBounce;
	if (!Velocity.IsNearlyZero())
	{
		Self->LaunchCharacter(Velocity, true, true);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Estado local (todas las máquinas)
// ─────────────────────────────────────────────────────────────────────────────

void UTN_CarryComponent::OnRep_CarriedTurtle()
{
	ApplyCarrierLocalState(CarriedTurtle != nullptr);
}

void UTN_CarryComponent::OnRep_CarriedBy()
{
	ApplyCarriedLocalState(CarriedBy);
}

void UTN_CarryComponent::ApplyCarrierLocalState(bool bCarrying)
{
	ATortugaCharacter* Self = GetTurtle();
	if (!Self || bCarrierStateApplied == bCarrying)
	{
		return;
	}
	bCarrierStateApplied = bCarrying;
	// Al coger o al soltar, sin toma de impulso a medias (la de soltar ya ha acabado).
	LocalWindupStart = -1.0;
	if (UTN_StaminaComponent* Stamina = Self->GetStaminaComponent())
	{
		if (bCarrying) { Stamina->SetSpeedCap(TNMovementLimits::CarrySource(), CarrySpeedCap); }
		else { Stamina->ClearSpeedCap(TNMovementLimits::CarrySource()); }
	}
	if (!bCarrying)
	{
		Self->SetCarryShake(FRotator::ZeroRotator);
	}
}

void UTN_CarryComponent::ApplyCarriedLocalState(ATortugaCharacter* Carrier)
{
	ATortugaCharacter* Self = GetTurtle();
	if (!Self)
	{
		return;
	}
	UCharacterMovementComponent* Move = Self->GetCharacterMovement();
	UTN_ShellComponent* Shell = Self->GetShellComponent();

	if (Carrier)
	{
		// Si en esta máquina seguía enganchada a su caja física, se suelta ya (llega antes que su destrucción).
		if (Shell)
		{
			Shell->DropLocalBody();
		}
		if (Move)
		{
			Move->StopMovementImmediately();
			Move->DisableMovement();
		}
		Self->GetCapsuleComponent()->IgnoreActorWhenMoving(Carrier, true);
		Carrier->GetCapsuleComponent()->IgnoreActorWhenMoving(Self, true);
		Self->AttachToActor(Carrier, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		Self->SetActorRelativeLocation(FVector(0.f, 0.f, CarryHeight));
		Self->SetActorRelativeRotation(FRotator::ZeroRotator);
		LastCarrier = Carrier;
		bCarriedStateApplied = true;
		return;
	}

	if (!bCarriedStateApplied)
	{
		return;
	}
	bCarriedStateApplied = false;
	Self->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Self->SetActorRotation(FRotator(0.f, Self->GetActorRotation().Yaw, 0.f));
	// Si ya va como caparazón con física, el movimiento sigue apagado hasta que salga.
	if (Move && !(Shell && Shell->HasLocalBody()))
	{
		Move->SetMovementMode(MOVE_Falling);
	}

	// La colisión con quien lo llevaba vuelve cuando ya se ha separado.
	TWeakObjectPtr<UTN_CarryComponent> WeakThis(this);
	TWeakObjectPtr<ATortugaCharacter> WeakCarrier = LastCarrier;
	FTimerHandle Handle;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(Handle, [WeakThis, WeakCarrier]()
		{
			if (WeakThis.IsValid() && WeakCarrier.IsValid())
			{
				WeakThis->RestoreCollisionWith(WeakCarrier.Get());
			}
		}, 0.45f, false);
	}
}

void UTN_CarryComponent::RestoreCollisionWith(ATortugaCharacter* Other)
{
	ATortugaCharacter* Self = GetTurtle();
	if (!Self || !Other || IsBeingCarried())
	{
		return;
	}
	Self->GetCapsuleComponent()->IgnoreActorWhenMoving(Other, false);
	Other->GetCapsuleComponent()->IgnoreActorWhenMoving(Self, false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Aterrizaje del lanzado
// ─────────────────────────────────────────────────────────────────────────────

void UTN_CarryComponent::NotifyLanded()
{
	if (!bAwaitingBounce)
	{
		return;
	}
	bAwaitingBounce = false;
	ATortugaCharacter* Self = GetTurtle();
	if (!Self)
	{
		return;
	}
	// Rebote siempre vertical donde cae: se estira en el aire y aterriza de pie.
	Self->LaunchCharacter(FVector(0.f, 0.f, BounceVelocity), true, true);
	if (Self->HasAuthority())
	{
		if (UTN_ShellComponent* Shell = Self->GetShellComponent())
		{
			Shell->SetExitLocked(false);
			Shell->ForceExitShell();
		}
	}
}

void UTN_CarryComponent::NotifyEnteredWater()
{
	if (!bAwaitingBounce)
	{
		return;
	}
	bAwaitingBounce = false;
	ATortugaCharacter* Self = GetTurtle();
	if (Self && Self->HasAuthority())
	{
		if (UTN_ShellComponent* Shell = Self->GetShellComponent())
		{
			Shell->SetExitLocked(false);
			Shell->ForceExitShell();
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void UTN_CarryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Portador o llevada que desaparece (se desconecta, reaparece, la destruyen): la otra no puede quedarse enganchada
	// con el movimiento apagado y el caparazón bloqueado. Solo al destruirse; en un viaje de mapa cae todo el mundo.
	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority()
		&& (EndPlayReason == EEndPlayReason::Destroyed || EndPlayReason == EEndPlayReason::RemovedFromWorld))
	{
		if (IsCarrying())
		{
			ForceRelease(false);
		}
		ATortugaCharacter* Carrier = CarriedBy;
		if (IsValid(Carrier))
		{
			if (UTN_CarryComponent* CarrierComp = Carrier->GetCarryComponent())
			{
				CarrierComp->ForceRelease(false);
			}
		}
	}
	CancelThrowWindup();
	Super::EndPlay(EndPlayReason);
}

void UTN_CarryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ATortugaCharacter* Self = GetTurtle();
	if (!Self)
	{
		return;
	}

	if (Self->HasAuthority())
	{
		// Llevado: forcejeo continuo → se libera.
		if (IsBeingCarried())
		{
			StruggleTime = bStruggling ? StruggleTime + DeltaTime : 0.f;
			if (StruggleTime >= SecondsToEscape)
			{
				if (UTN_CarryComponent* CarrierComp = CarriedBy->GetCarryComponent())
				{
					CarrierComp->ForceRelease(true);
				}
			}
		}

		// Portador: si él o su carga dejan de poder seguir así, se suelta.
		if (IsCarrying())
		{
			ATortugaCharacter* Carried = CarriedTurtle;
			if (!IsValid(Carried) || Carried->IsDead() || Self->IsDead() || Self->IsKnockedDown())
			{
				ForceRelease(false);
			}
		}
	}

	// Temblor de cámara del portador mientras su carga forcejea (solo local).
	if (Self->IsLocallyControlled() && IsCarrying())
	{
		if (bCarriedStruggling)
		{
			ShakeTime += DeltaTime;
			Self->SetCarryShake(FRotator(FMath::Sin(ShakeTime * 41.f) * StruggleShakeDegrees,
				FMath::Sin(ShakeTime * 33.f + 1.3f) * StruggleShakeDegrees, 0.f));
		}
		else
		{
			Self->SetCarryShake(FRotator::ZeroRotator);
		}
	}
}
