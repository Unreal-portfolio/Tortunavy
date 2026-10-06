// Guantazo con la aleta (#832, #709): el dueño lo da al momento y el servidor decide a quién da. Ver TN_FlipperSlapComponent.h.

#include "Player/TN_FlipperSlapComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_FlipperSlapRules.h"
#include "Player/TN_HitFeedback.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "Player/TortugaCharacter.h"
#include "TimerManager.h"
#include "World/Beach/TN_RaceBurstFX.h"
#include "World/Beach/TN_RaceItemSynth.h"
#include "Game/TN_ItemRuntime.h"

namespace TNFlipperSlapDetail
{
	/** Altura del pecho sobre los pies (cm): de donde sale la línea de vista y donde brilla el golpe. */
	constexpr float ChestHeight = 35.f;

	/** El pecho de una tortuga: sus pies (centro de la cápsula menos la media altura) y ChestHeight encima. */
	FVector ChestOf(const ACharacter& Turtle)
	{
		const UCapsuleComponent* Capsule = Turtle.GetCapsuleComponent();
		const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.f;
		return Turtle.GetActorLocation() + FVector(0.0, 0.0, ChestHeight - HalfHeight);
	}

	/** Hasta dónde llega el destello desde el centro de la golpeada, hacia quien golpea (cm). */
	constexpr float ImpactInset = 40.f;

	/** El soplido de la aleta (más agudo y corto que el de lanzar) y el golpe seco. */
	constexpr float WhooshPitch = 1.8f;
	constexpr float WhooshVolume = 0.6f;
	constexpr float SmackPitch = 1.25f;

	/** Tamaño del estallido de estrellitas (1 = el de una caja de objetos). */
	constexpr float BurstScale = 0.45f;

	/** Sin escenario entre From y To. */
	bool HasLineOfSight(const UWorld* World, const FVector& From, const FVector& To, const AActor* A, const AActor* B)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FlipperSlapSight), false, A);
		Params.AddIgnoredActor(B);
		FHitResult Hit;
		return World && !World->LineTraceSingleByObjectType(Hit, From, To, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
	}
}

UTN_FlipperSlapComponent::UTN_FlipperSlapComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UTN_FlipperSlapComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ImpactTimer);
	}
	Super::EndPlay(EndPlayReason);
}

ATortugaCharacter* UTN_FlipperSlapComponent::GetTurtle() const
{
	return Cast<ATortugaCharacter>(GetOwner());
}

bool UTN_FlipperSlapComponent::CanSlap(const ATortugaCharacter* Turtle)
{
	if (!IsValid(Turtle) || Turtle->IsDead() || Turtle->IsKnockedDown() || Turtle->IsInShell() || Turtle->IsDiving()
		|| Turtle->IsBellyPoseActive() || Turtle->GetActiveEmoteIndex() >= 0)
	{
		return false;
	}
	const UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
	if (Move && Move->IsSwimming())
	{
		return false;
	}
	// Lo demás que impide actuar (la sujeta un enemigo o un gusano, la recolocan, lleva o la llevan) es lo mismo que para
	// usar un objeto.
	return TNItemRuntime::CanUseNow(Turtle);
}

bool UTN_FlipperSlapComponent::CanBeSlapped(const ATortugaCharacter* Turtle)
{
	if (!IsValid(Turtle) || Turtle->IsDead() || Turtle->IsKnockedDown() || Turtle->IsInShell() || !TNItemRuntime::CanBeHurt(Turtle))
	{
		return false;
	}
	const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
	return !(Carry && Carry->IsBeingCarried());
}

float UTN_FlipperSlapComponent::GetSwingPhase() const
{
	const UWorld* World = GetWorld();
	return World ? TNFlipperSlap::SwingPhase(World->GetTimeSeconds(), SwingStart) : -1.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Quien golpea
// ─────────────────────────────────────────────────────────────────────────────

bool UTN_FlipperSlapComponent::TrySlap()
{
	ATortugaCharacter* Turtle = GetTurtle();
	const UWorld* World = GetWorld();
	if (!Turtle || !World || !Turtle->IsLocallyControlled() || !CanSlap(Turtle))
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	if (!TNFlipperSlap::IsCooledDown(Now, SwingStart))
	{
		return false;
	}
	// La aleta y el soplido, al momento; a quién da, lo decide el servidor.
	BeginSwing(Now);
	if (Turtle->HasAuthority())
	{
		ResolveOnServer(Now);
	}
	else
	{
		ServerSlap();
	}
	return true;
}

void UTN_FlipperSlapComponent::BeginSwing(double Now)
{
	SwingStart = Now;
	if (UTN_RaceItemSynthComponent* Sfx = GetSynth())
	{
		Sfx->Play(ETNRaceSound::Throw, TNFlipperSlapDetail::WhooshPitch, TNFlipperSlapDetail::WhooshVolume);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

void UTN_FlipperSlapComponent::ServerSlap_Implementation()
{
	const ATortugaCharacter* Turtle = GetTurtle();
	const UWorld* World = GetWorld();
	if (!Turtle || !World || !CanSlap(Turtle))
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (!TNFlipperSlap::IsCooledDown(Now, LastServerSlap, TNFlipperSlap::ServerCooldownTolerance))
	{
		return;
	}
	ResolveOnServer(Now);
}

void UTN_FlipperSlapComponent::ResolveOnServer(double Now)
{
	using namespace TNFlipperSlapDetail;
	ATortugaCharacter* Turtle = GetTurtle();
	UWorld* World = GetWorld();
	if (!Turtle || !World || !Turtle->HasAuthority())
	{
		return;
	}
	LastServerSlap = Now;

	// Hacia donde mira la cámara, en horizontal.
	FRotator Aim = Turtle->GetTurtleAimRotation();
	Aim.Pitch = 0.f;
	Aim.Roll = 0.f;
	const FVector Forward = Aim.Vector();
	const FVector Origin = Turtle->GetActorLocation();
	const FVector Chest = ChestOf(*Turtle);

	TArray<ATortugaCharacter*> Candidates;
	TArray<FVector> Points;
	for (TActorIterator<ATortugaCharacter> It(World); It; ++It)
	{
		ATortugaCharacter* Other = *It;
		float Score = 0.f;
		if (Other == Turtle || !CanBeSlapped(Other) || !TNFlipperSlap::InArc(Origin, Forward, Other->GetActorLocation(), Score)
			|| !HasLineOfSight(World, Chest, ChestOf(*Other), Turtle, Other))
		{
			continue;
		}
		Candidates.Add(Other);
		Points.Add(Other->GetActorLocation());
	}

	const int32 Best = TNFlipperSlap::PickTarget(Origin, Forward, Points);
	ATortugaCharacter* Victim = Candidates.IsValidIndex(Best) ? Candidates[Best] : nullptr;
	FVector ImpactPoint = Origin + Forward * TNFlipperSlap::Reach * 0.5f;
	if (Victim)
	{
		const FVector At = Victim->GetActorLocation();
		ImpactPoint = ChestOf(*Victim) + (Origin - At).GetSafeNormal2D() * ImpactInset;
		// Mareo de siempre (lento un momento), un empujoncito y la sacudida mínima de cámara; sin derribo.
		Victim->ApplyMareoEffect(TNFlipperSlap::DizzySeconds);
		UTN_TurtleMovementComponent::LaunchFromServer(Victim, TNFlipperSlap::PushVelocity(Origin, Forward, At));
		Victim->NotifyHitFeedback(TNHitFeedback::MinStrength);
		UE_LOG(LogTortunabo, Log, TEXT("[Guantazo] %s da un guantazo a %s."), *GetNameSafe(Turtle), *GetNameSafe(Victim));
	}
	MulticastSlap(Victim != nullptr, ImpactPoint);
}

// ─────────────────────────────────────────────────────────────────────────────
// Todas las máquinas con pantalla
// ─────────────────────────────────────────────────────────────────────────────

void UTN_FlipperSlapComponent::MulticastSlap_Implementation(bool bHit, FVector_NetQuantize10 ImpactPoint)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const ATortugaCharacter* Turtle = GetTurtle();
	const UWorld* World = GetWorld();
	if (!Turtle || !World)
	{
		return;
	}
	// Quien golpea ya ha movido la aleta y sonado al pulsar; los demás empiezan ahora.
	if (!Turtle->IsLocallyControlled())
	{
		BeginSwing(World->GetTimeSeconds());
	}
	if (bHit)
	{
		ScheduleImpact(FVector(ImpactPoint));
	}
}

void UTN_FlipperSlapComponent::ScheduleImpact(const FVector& Point)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Delay = TNFlipperSlap::ImpactDelay(World->GetTimeSeconds(), SwingStart);
	if (Delay <= UE_KINDA_SMALL_NUMBER)
	{
		PlayImpact(Point);
		return;
	}
	World->GetTimerManager().SetTimer(ImpactTimer, FTimerDelegate::CreateUObject(this, &UTN_FlipperSlapComponent::PlayImpact, Point), Delay, false);
}

void UTN_FlipperSlapComponent::PlayImpact(FVector Point)
{
	if (UTN_RaceItemSynthComponent* Sfx = GetSynth())
	{
		Sfx->Play(ETNRaceSound::Catch, TNFlipperSlapDetail::SmackPitch, 1.f);
	}
	ATN_RaceBurstFX::SpawnLocal(GetWorld(), ETNRaceBurst::StarPop, Point, TNFlipperSlapDetail::BurstScale);
}

UTN_RaceItemSynthComponent* UTN_FlipperSlapComponent::GetSynth()
{
	AActor* Owner = GetOwner();
	if (!Owner || Owner->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	if (!Synth)
	{
		Synth = UTN_RaceItemSynthComponent::AttachTo(Owner, Owner->GetActorLocation(), 1500.f, 9000.f);
	}
	return Synth;
}
