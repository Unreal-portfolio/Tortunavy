#include "World/Beach/TN_BeachStun.h"
#include "World/Beach/TN_BeachStunComponent.h"
#include "Game/TN_CoopItemComponent.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachGullTuning.h"
#include "World/Beach/TN_BeachSandWorm.h"
#include "World/Beach/TN_BeachStorm.h"
#include "Core/TN_Log.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_DizzyBirdsComponent.h"
#include "Player/TN_HitFeedback.h"
#include "Player/TN_ShellBody.h"
#include "Player/TN_ShellComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PhysicsVolume.h"
#include "Net/UnrealNetwork.h"

namespace TNBeachStunDetail
{
	/** Reloj del servidor en esta máquina (en el servidor, su propio tiempo de mundo). */
	float ServerNow(const UWorld* World)
	{
		if (!World)
		{
			return 0.f;
		}
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			return static_cast<float>(GameState->GetServerWorldTimeSeconds());
		}
		return World->GetTimeSeconds();
	}

	bool IsCarried(const ATortugaCharacter* Turtle)
	{
		const UTN_CarryComponent* Carry = Turtle ? Turtle->GetCarryComponent() : nullptr;
		return Carry && Carry->GetCarrier() != nullptr;
	}

	/** Últimos segundos en los que el temblor se apaga poco a poco. */
	constexpr float TrembleFadeSeconds = 0.6f;

	/** Hora del mundo en el servidor (las reservas y la gracia se miden con ella). */
	double WorldNow(const UObject* Context)
	{
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		return World ? World->GetTimeSeconds() : 0.0;
	}

	/** Sitio de arena abierta: anillos cada tanto (cm) alrededor del punto pedido y la holgura de la cápsula. */
	constexpr float SpotRingStep = 250.f;
	constexpr float SpotCapsulePad = 15.f;
	/** El suelo, así de llano (normal Z). */
	constexpr float SpotMinNormalZ = 0.75f;

	/** Cerca del frente de la tormenta (a menos de esto por delante, cm, o detrás), nada lanza a la tortuga hacia atrás. */
	constexpr float StormNoBackReach = 2500.f;

	/**
	 * Lo que lanza a una tortuga que está cerca del frente de la tormenta (el pulpo hacia la salida, la gaviota al soltarla,
	 * el lagarto de lado...) pierde lo que la echaría hacia la tormenta: si no, cae detrás, la patean hacia delante y el
	 * enemigo la vuelve a lanzar hacia atrás, en cadena. Lo de lado y hacia arriba se queda.
	 */
	FVector StormSafeLaunch(const ACharacter& Turtle, const FVector& Launch)
	{
		if (Launch.IsNearlyZero())
		{
			return Launch;
		}
		const ATN_BeachStorm* Storm = ATN_BeachStorm::FindStorm(&Turtle);
		if (!Storm || !Storm->IsStormActive() || !Storm->IsBehindFront(Turtle.GetActorLocation(), -StormNoBackReach))
		{
			return Launch;
		}
		FVector Forward = Storm->GetActorForwardVector();
		Forward.Z = 0.0;
		if (!Forward.Normalize())
		{
			return Launch;
		}
		const double Back = FVector::DotProduct(Launch, Forward);
		return Back < 0.0 ? Launch - Forward * Back : Launch;
	}

	/** true si Where está dentro de un volumen de agua (pozas, balsas: APhysicsVolume con bWaterVolume). */
	bool IsWaterAt(UWorld& World, const FVector& Where)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TNBeachSpotWater), false);
		World.OverlapMultiByObjectType(Overlaps, Where, FQuat::Identity, FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllObjects),
			FCollisionShape::MakeSphere(40.f), Params);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			const APhysicsVolume* Volume = Cast<APhysicsVolume>(Overlap.GetActor());
			if (Volume && Volume->bWaterVolume)
			{
				return true;
			}
		}
		return false;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Funciones del contrato (TN_BeachStun.h)
// ─────────────────────────────────────────────────────────────────────────────

void TNBeach::StunTurtle(ACharacter* Turtle, float Seconds, const FVector& Launch)
{
	// En la boca de un gusano de arena (al acabar la cuenta atrás) ya no le pasa nada más hasta la ronda siguiente.
	if (!Turtle || Seconds <= 0.f || !Turtle->HasAuthority() || Turtle->IsActorBeingDestroyed() || ATN_BeachSandWorm::IsBeingEaten(Turtle))
	{
		return;
	}
	if (const ATortugaCharacter* TurtleCharacter = Cast<ATortugaCharacter>(Turtle))
	{
		if (TurtleCharacter->IsDead())
		{
			return;
		}
	}
	// Protegida por el pez globo: nada la aturde.
	if (UTN_CoopItemComponent::IsTurtleProtected(Turtle))
	{
		return;
	}
	// La patada de la tormenta o la red de seguridad la están recolocando, o la sujeta un enemigo: nada la relanza hasta que
	// acaben (ellas se reservan la tortuga después de su propia llamada; el enemigo la suelta antes de aturdirla él). Una
	// bola que nace mientras un enemigo la sigue colocando en su pico o en su boca tiene dos que la mueven a la vez.
	const ETNBeachMover Mover = GetTurtleMover(Turtle);
	if (!CanStunOver(Mover))
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] %s: no se aturde, la mueve %s."), *GetNameSafe(Turtle), GetMoverName(Mover));
		return;
	}
	if (UTN_BeachStunComponent* Stun = UTN_BeachStunComponent::FindOrAddOn(Turtle))
	{
		Stun->StartStun(Seconds, TNBeachStunDetail::StormSafeLaunch(*Turtle, Launch));
	}
}

void TNBeach::KnockDownTurtle(ACharacter* Turtle, float Seconds, const FVector& Impulse)
{
	if (!Turtle || Seconds <= 0.f || !Turtle->HasAuthority() || Turtle->IsActorBeingDestroyed() || ATN_BeachSandWorm::IsBeingEaten(Turtle))
	{
		return;
	}
	ATortugaCharacter* TurtleCharacter = Cast<ATortugaCharacter>(Turtle);
	if (!TurtleCharacter || TurtleCharacter->IsDead() || !CanStunOver(GetTurtleMover(Turtle)) || UTN_CoopItemComponent::IsTurtleProtected(Turtle))
	{
		return;
	}
	// El derribo de la piel de plátano: ragdoll, mareo y levantarse al acabar.
	TurtleCharacter->ApplyKnockdown(Seconds, Impulse);
}

bool TNBeach::IsTurtleStunned(const ACharacter* Turtle)
{
	const UTN_BeachStunComponent* Stun = UTN_BeachStunComponent::FindOn(Turtle);
	return Stun && Stun->IsStunned();
}

bool TNBeach::SlipFromHolder(ACharacter* Turtle)
{
	ATortugaCharacter* TurtleCharacter = Cast<ATortugaCharacter>(Turtle);
	return TurtleCharacter && TurtleCharacter->HasAuthority() && ATN_BeachEnemy::ServerSlipHeldTurtle(TurtleCharacter);
}

bool TNBeach::IsDodgingByBellyDive(const ACharacter* Turtle)
{
	const ATortugaCharacter* TurtleCharacter = Cast<ATortugaCharacter>(Turtle);
	if (!TurtleCharacter || !TurtleCharacter->IsBellyPoseActive())
	{
		return false;
	}
	const UCharacterMovementComponent* Move = TurtleCharacter->GetCharacterMovement();
	const bool bAirborne = Move && Move->IsFalling();
	return TNBeachGullTuning::DodgesByBellyDive(true, bAirborne, static_cast<float>(TurtleCharacter->GetVelocity().Size2D()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Quién mueve a la tortuga (árbitro, servidor)
// ─────────────────────────────────────────────────────────────────────────────

TNBeach::ETNBeachMover TNBeach::GetTurtleMover(const ACharacter* Turtle)
{
	const ATortugaCharacter* TurtleCharacter = Cast<ATortugaCharacter>(Turtle);
	if (!TurtleCharacter)
	{
		return ETNBeachMover::None;
	}
	FTNMoverView View;
	View.bEaten = ATN_BeachSandWorm::IsBeingEaten(TurtleCharacter);
	View.Claim = GetTurtleClaim(TurtleCharacter);
	View.bHeld = ATN_BeachEnemy::IsTurtleHeld(TurtleCharacter);
	View.bCarried = TNBeachStunDetail::IsCarried(TurtleCharacter);
	View.bKnockedDown = TurtleCharacter->IsKnockedDown();
	View.bInShell = TurtleCharacter->IsInShell();
	View.bFallImmune = TurtleCharacter->IsFallImmune();
	return ResolveMover(View);
}

const TCHAR* TNBeach::GetMoverName(ETNBeachMover Mover)
{
	switch (Mover)
	{
	case ETNBeachMover::Launch: return TEXT("un lanzamiento");
	case ETNBeachMover::Ball: return TEXT("su bola del caparazón");
	case ETNBeachMover::Knockdown: return TEXT("el derribo (ragdoll)");
	case ETNBeachMover::Carried: return TEXT("otra tortuga (en brazos)");
	case ETNBeachMover::Held: return TEXT("un enemigo (sujeta)");
	case ETNBeachMover::StormKick: return TEXT("la patada de la tormenta");
	case ETNBeachMover::SafetyNet: return TEXT("la red de seguridad");
	case ETNBeachMover::Eaten: return TEXT("un gusano de arena");
	default: return TEXT("su propio movimiento");
	}
}

void TNBeach::ClaimTurtle(ACharacter* Turtle, ETNBeachMover Mover, float Seconds)
{
	if (!Turtle || !Turtle->HasAuthority() || Mover == ETNBeachMover::None || Seconds <= 0.f)
	{
		return;
	}
	if (UTN_BeachStunComponent* Stun = UTN_BeachStunComponent::FindOrAddOn(Turtle))
	{
		Stun->ClaimMover = static_cast<uint8>(Mover);
		Stun->ClaimUntil = TNBeachStunDetail::WorldNow(Turtle) + Seconds;
	}
}

void TNBeach::ReleaseTurtle(ACharacter* Turtle, ETNBeachMover Mover)
{
	UTN_BeachStunComponent* Stun = UTN_BeachStunComponent::FindOn(Turtle);
	if (Stun && Stun->ClaimMover == static_cast<uint8>(Mover))
	{
		Stun->ClaimMover = static_cast<uint8>(ETNBeachMover::None);
		Stun->ClaimUntil = 0.0;
	}
}

TNBeach::ETNBeachMover TNBeach::GetTurtleClaim(const ACharacter* Turtle)
{
	const UTN_BeachStunComponent* Stun = UTN_BeachStunComponent::FindOn(Turtle);
	if (!Stun || Stun->ClaimMover == static_cast<uint8>(ETNBeachMover::None) || TNBeachStunDetail::WorldNow(Turtle) >= Stun->ClaimUntil)
	{
		return ETNBeachMover::None;
	}
	return static_cast<ETNBeachMover>(Stun->ClaimMover);
}

bool TNBeach::IsTurtleRelocating(const ACharacter* Turtle)
{
	const ETNBeachMover Claim = GetTurtleClaim(Turtle);
	return Claim == ETNBeachMover::StormKick || Claim == ETNBeachMover::SafetyNet;
}

void TNBeach::GrantStormGrace(ACharacter* Turtle, float Seconds)
{
	if (!Turtle || !Turtle->HasAuthority() || Seconds <= 0.f)
	{
		return;
	}
	if (UTN_BeachStunComponent* Stun = UTN_BeachStunComponent::FindOrAddOn(Turtle))
	{
		Stun->StormGraceUntil = FMath::Max(Stun->StormGraceUntil, TNBeachStunDetail::WorldNow(Turtle) + Seconds);
	}
}

bool TNBeach::HasStormGrace(const ACharacter* Turtle)
{
	const UTN_BeachStunComponent* Stun = UTN_BeachStunComponent::FindOn(Turtle);
	return Stun && TNBeachStunDetail::WorldNow(Turtle) < Stun->StormGraceUntil;
}

void TNBeach::RelocateTurtle(ACharacter* Turtle, const FTransform& Where)
{
	ATortugaCharacter* TurtleCharacter = Cast<ATortugaCharacter>(Turtle);
	if (!TurtleCharacter || !TurtleCharacter->HasAuthority())
	{
		return;
	}
	// El enemigo que la tenga en la boca o el pico la suelta y deja el ataque (si no, la volvería a colocar en su boca).
	ATN_BeachEnemy::ServerReleaseHeldTurtle(TurtleCharacter, TEXT("recolocada"));
	// Lo que lleve y quien la lleve en brazos.
	if (UTN_CarryComponent* Carry = TurtleCharacter->GetCarryComponent())
	{
		if (Carry->IsCarrying())
		{
			Carry->ForceRelease(false);
		}
		if (ATortugaCharacter* Carrier = Carry->GetCarrier())
		{
			if (UTN_CarryComponent* CarrierCarry = Carrier->GetCarryComponent())
			{
				CarrierCarry->ForceRelease(false);
			}
		}
	}
	// Del derribo se levanta (vuelven la colisión de la cápsula, el movimiento, su suavizado y su réplica).
	if (TurtleCharacter->IsKnockedDown())
	{
		TurtleCharacter->RecoverFromKnockdown();
	}
	// Aturdida y en bola: fuera del todo, en el acto (sin caja que siga cayendo donde estaba).
	if (UTN_BeachStunComponent* Stun = UTN_BeachStunComponent::FindOn(TurtleCharacter))
	{
		if (Stun->IsStunned())
		{
			Stun->EndStun(true);
		}
	}
	if (UTN_ShellComponent* Shell = TurtleCharacter->GetShellComponent())
	{
		Shell->SetExitLocked(false);
		Shell->ForceExitShell();
	}
	UCharacterMovementComponent* Move = TurtleCharacter->GetCharacterMovement();
	if (Move)
	{
		Move->StopMovementImmediately();
		Move->PendingLaunchVelocity = FVector::ZeroVector;
	}
	TurtleCharacter->SetActorLocationAndRotation(Where.GetLocation(), Where.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	if (Move)
	{
		if (!Move->IsComponentTickEnabled())
		{
			Move->SetComponentTickEnabled(true);
		}
		// El teletransporte no es una caída: la caída se empieza a contar aquí. Si ya estaba cayendo (al salir del caparazón o
		// del derribo de arriba se le pone la caída donde estaba), pedir otra vez MOVE_Falling no hace nada y la tortuga se
		// quedaba con la altura de antes (la caja de la bola en lo alto de un castillo, el vuelo de una patada, el pico de
		// una gaviota): a los 5 m de «caída» se metía sola en una bola nada más ponerla de pie (ATortugaCharacter::
		// TickFallRules) y el aterrizaje contaba una caída mortal. Pasando por MOVE_None, la caída vuelve a empezar aquí.
		if (Move->MovementMode == MOVE_Falling)
		{
			Move->SetMovementMode(MOVE_None);
		}
		Move->SetMovementMode(MOVE_Falling);
	}
	TurtleCharacter->ForceNetUpdate();
}

bool TNBeach::FindOpenSandSpot(const ACharacter* Turtle, const FVector& Desired, float SearchRadius, FTransform& OutTransform,
	const TArray<FVector>* Avoid, float AvoidRadius)
{
	using namespace TNBeachStunDetail;
	UWorld* World = Turtle ? Turtle->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	// De pie: la cápsula de la clase (en el panzazo, la de la tortuga es más baja).
	const ACharacter* Defaults = Turtle->GetClass()->GetDefaultObject<ACharacter>();
	const UCapsuleComponent* DefaultCapsule = Defaults ? Defaults->GetCapsuleComponent() : nullptr;
	const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
	const float HalfHeight = DefaultCapsule ? DefaultCapsule->GetScaledCapsuleHalfHeight() : (Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f);
	const float Radius = DefaultCapsule ? DefaultCapsule->GetScaledCapsuleRadius() : (Capsule ? Capsule->GetScaledCapsuleRadius() : 34.f);

	FCollisionQueryParams Query(SCENE_QUERY_STAT(TNBeachOpenSand), false, Turtle);
	if (const ATortugaCharacter* TurtleCharacter = Cast<ATortugaCharacter>(Turtle))
	{
		if (const UTN_ShellComponent* Shell = TurtleCharacter->GetShellComponent())
		{
			Query.AddIgnoredActor(Shell->GetBody());
		}
	}
	const double AvoidSq = FMath::Square(static_cast<double>(AvoidRadius));
	const int32 Rings = SearchRadius > 0.f ? FMath::Max(1, FMath::CeilToInt32(SearchRadius / SpotRingStep)) : 0;
	for (int32 Ring = 0; Ring <= Rings; ++Ring)
	{
		const int32 Count = Ring == 0 ? 1 : FMath::Min(8 + 4 * (Ring - 1), 24);
		for (int32 k = 0; k < Count; ++k)
		{
			const double Angle = (Ring % 2 == 0 ? 0.0 : 0.5) * UE_DOUBLE_TWO_PI / Count + UE_DOUBLE_TWO_PI * k / Count;
			const FVector Point = Desired + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * (Ring * SpotRingStep);
			if (Avoid && AvoidSq > 0.0 && Avoid->ContainsByPredicate([&Point, AvoidSq](const FVector& Bad) { return FVector::DistSquared2D(Point, Bad) < AvoidSq; }))
			{
				continue;
			}
			// Lo primero que para a una tortuga desde arriba, llano (nada encima).
			float Ground = 0.f;
			if (!ATN_BeachEnemy::TraceGround(Turtle, Point, Ground))
			{
				continue;
			}
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, FVector(Point.X, Point.Y, Ground + 400.0), FVector(Point.X, Point.Y, Ground - 250.0), ECC_Pawn, Query)
				|| Hit.bStartPenetrating || Hit.ImpactNormal.Z < SpotMinNormalZ)
			{
				continue;
			}
			// La tortuga de pie cabe (ni una roca, ni una muralla, ni otra tortuga) y no está en el agua.
			const FVector Stand = Hit.ImpactPoint + FVector(0.0, 0.0, HalfHeight + 5.0);
			if (World->OverlapBlockingTestByChannel(Stand, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius + SpotCapsulePad, HalfHeight), Query)
				|| IsWaterAt(*World, Stand))
			{
				continue;
			}
			OutTransform = FTransform(FRotator(0.f, Turtle->GetActorRotation().Yaw, 0.f), Stand);
			return true;
		}
	}
	return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// UTN_BeachStunComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_BeachStunComponent::UTN_BeachStunComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// Después de la física: la caja del caparazón coloca la malla en TG_PostPhysics y el temblor va encima.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(true);
}

void UTN_BeachStunComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTN_BeachStunComponent, bStunned);
	DOREPLIFETIME(UTN_BeachStunComponent, StunEndServerTime);
}

UTN_BeachStunComponent* UTN_BeachStunComponent::FindOn(const AActor* Turtle)
{
	return Turtle ? Turtle->FindComponentByClass<UTN_BeachStunComponent>() : nullptr;
}

UTN_BeachStunComponent* UTN_BeachStunComponent::FindOrAddOn(ACharacter* Turtle)
{
	if (UTN_BeachStunComponent* Existing = FindOn(Turtle))
	{
		return Existing;
	}
	if (!Turtle || !Turtle->HasAuthority())
	{
		return nullptr;
	}
	// Componente dinámico replicado: el servidor lo crea y cada cliente recibe el suyo (OnCreatedFromReplication).
	UTN_BeachStunComponent* Stun = NewObject<UTN_BeachStunComponent>(Turtle, UTN_BeachStunComponent::StaticClass(), TEXT("BeachStun"));
	if (!Stun)
	{
		return nullptr;
	}
	Turtle->AddInstanceComponent(Stun);
	Stun->RegisterComponent();
	return Stun;
}

void UTN_BeachStunComponent::StartStun(float Seconds, const FVector& Launch)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World || !Owner->HasAuthority() || Seconds <= 0.f)
	{
		return;
	}

	const float NewEnd = World->GetTimeSeconds() + Seconds;
	StunEndServerTime = bStunned ? FMath::Max(StunEndServerTime, NewEnd) : NewEnd;

	if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Owner))
	{
		// El golpe que la mete a la fuerza en el caparazón: sacudida y vibración en su máquina (#350).
		Turtle->NotifyHitFeedback(TNHitFeedback::StrengthFromImpulse(Launch.Size()));
		UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
		const bool bCarried = TNBeachStunDetail::IsCarried(Turtle);
		// Suelta a quien lleve; si la lleva otra, sigue en sus brazos (ya va metida en el caparazón).
		if (Carry && Carry->IsCarrying())
		{
			Carry->ForceRelease(false);
		}
		if (Turtle->IsKnockedDown())
		{
			Turtle->RecoverFromKnockdown();
		}
		if (UTN_ShellComponent* Shell = Turtle->GetShellComponent())
		{
			if (!Shell->IsInShell())
			{
				// Sin cuerpo aquí: la caja se suelta abajo como lanzada (tumbada donde está o con la velocidad pedida).
				Shell->ForceEnterShell(false, false);
			}
			if (!bCarried)
			{
				if (ATN_ShellBody* Body = Shell->GetBody())
				{
					// Ya rodaba (se metió a mano o la habían aturdido): que no salga al pararse y, si hay golpe, otro empujón.
					Body->InitBody(Turtle, false);
					UBoxComponent* Box = Body->GetBox();
					if (Box && !Launch.IsNearlyZero())
					{
						Box->SetPhysicsLinearVelocity(Launch);
					}
				}
				else
				{
					Shell->StartBody(Launch, true, false);
				}
			}
			Shell->SetExitLocked(true);
		}
	}

	const bool bWasStunned = bStunned;
	bStunned = true;
	if (!bWasStunned)
	{
		// En un servidor escucha OnRep no corre en el anfitrión.
		ApplyLocalVisuals(true);
	}
	SetComponentTickEnabled(true);
	Owner->ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] %s aturdida %.1f s%s."), *GetNameSafe(Owner), StunEndServerTime - World->GetTimeSeconds(),
		bWasStunned ? TEXT(" (alargado)") : TEXT(""));
}

void UTN_BeachStunComponent::EndStun(bool bExitShellNow)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !bStunned)
	{
		return;
	}
	bStunned = false;
	StunEndServerTime = 0.f;

	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Owner);
	UTN_ShellComponent* Shell = Turtle ? Turtle->GetShellComponent() : nullptr;
	if (Shell)
	{
		Shell->SetExitLocked(false);
		// Si la lleva otra tortuga, sale cuando la suelte (lo de siempre del caparazón).
		if (Shell->IsInShell() && !TNBeachStunDetail::IsCarried(Turtle))
		{
			ATN_ShellBody* Body = Shell->GetBody();
			if (Body && !bExitShellNow)
			{
				// Sale en cuanto la bola se para (si ya está quieta, en un momento).
				Body->InitBody(Turtle, true);
			}
			else
			{
				Shell->ForceExitShell();
			}
		}
	}

	ApplyLocalVisuals(false);
	Owner->ForceNetUpdate();
}

float UTN_BeachStunComponent::GetSecondsLeft() const
{
	if (!bStunned)
	{
		return 0.f;
	}
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	const float Now = (Owner && Owner->HasAuthority() && World) ? World->GetTimeSeconds() : TNBeachStunDetail::ServerNow(World);
	return FMath::Max(0.f, StunEndServerTime - Now);
}

void UTN_BeachStunComponent::OnRep_Stunned()
{
	ApplyLocalVisuals(bStunned);
}

void UTN_BeachStunComponent::ApplyLocalVisuals(bool bOn)
{
	if (const AActor* Owner = GetOwner())
	{
		if (UTN_DizzyBirdsComponent* Birds = Owner->FindComponentByClass<UTN_DizzyBirdsComponent>())
		{
			// Al acabar, los pájaros siguen si la tortuga está derribada (son del derribo).
			const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Owner);
			Birds->SetDizzy(bOn || (Turtle && Turtle->IsKnockedDown()));
		}
	}
	TrembleTime = 0.f;
	if (!bOn)
	{
		if (AActor* After = TrembleAfter.Get())
		{
			PrimaryComponentTick.RemovePrerequisite(After, After->PrimaryActorTick);
		}
		TrembleAfter.Reset();
	}
	// El servidor necesita el tick para acabar el aturdimiento; los demás, para el temblor.
	SetComponentTickEnabled(bOn);
}

void UTN_BeachStunComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World || !bStunned)
	{
		return;
	}

	if (Owner->HasAuthority())
	{
		if (World->GetTimeSeconds() >= StunEndServerTime)
		{
			EndStun(false);
			return;
		}
		// Lanzada y rebotando (la suelta quien la llevaba), el caparazón desbloquea la salida: aturdida no se sale.
		if (const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Owner))
		{
			UTN_ShellComponent* Shell = Turtle->GetShellComponent();
			if (Shell && Shell->IsInShell() && !Shell->IsExitLocked())
			{
				Shell->SetExitLocked(true);
			}
		}
	}

	if (GetNetMode() != NM_DedicatedServer)
	{
		// Los pájaros son también del derribo: si el derribo acaba mientras sigue aturdida, se vuelven a encender.
		if (UTN_DizzyBirdsComponent* Birds = Owner->FindComponentByClass<UTN_DizzyBirdsComponent>())
		{
			if (!Birds->IsDizzy())
			{
				Birds->SetDizzy(true);
			}
		}
		TickTremble(DeltaTime);
	}
}

void UTN_BeachStunComponent::TickTremble(float DeltaTime)
{
	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner());
	UTN_ShellComponent* Shell = Turtle ? Turtle->GetShellComponent() : nullptr;
	ATN_ShellBody* Body = Shell ? Shell->GetBody() : nullptr;
	USkeletalMeshComponent* SkelMesh = Turtle ? Turtle->GetMesh() : nullptr;
	if (!Body || !SkelMesh || !Shell->HasLocalBody())
	{
		return;
	}
	// La caja coloca la malla cada fotograma (FollowBody): el temblor va después y nunca se acumula.
	if (TrembleAfter.Get() != Body)
	{
		if (AActor* Old = TrembleAfter.Get())
		{
			PrimaryComponentTick.RemovePrerequisite(Old, Old->PrimaryActorTick);
		}
		PrimaryComponentTick.AddPrerequisite(Body, Body->PrimaryActorTick);
		TrembleAfter = Body;
	}

	TrembleTime += DeltaTime;
	const float T = TrembleTime;
	const float Strength = FMath::Clamp(GetSecondsLeft() / TNBeachStunDetail::TrembleFadeSeconds, 0.f, 1.f);
	if (Strength <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector Offset(
		FMath::Sin(T * 57.f) * 0.6f + FMath::Sin(T * 83.f + 1.1f) * 0.4f,
		FMath::Sin(T * 61.f + 2.f) * 0.6f + FMath::Sin(T * 97.f + 0.3f) * 0.4f,
		FMath::Abs(FMath::Sin(T * 44.f)) * 0.5f);
	const FRotator Wobble(
		FMath::Sin(T * 49.f + 0.5f) * TrembleDegrees,
		FMath::Sin(T * 38.f) * TrembleDegrees * 0.5f,
		FMath::Sin(T * 53.f + 1.7f) * TrembleDegrees);
	SkelMesh->AddWorldOffset(Offset * (TrembleAmplitude * Strength), false, nullptr, ETeleportType::TeleportPhysics);
	SkelMesh->AddLocalRotation(Wobble * Strength, false, nullptr, ETeleportType::TeleportPhysics);
}

void UTN_BeachStunComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AActor* After = TrembleAfter.Get())
	{
		PrimaryComponentTick.RemovePrerequisite(After, After->PrimaryActorTick);
	}
	TrembleAfter.Reset();
	Super::EndPlay(EndPlayReason);
}
