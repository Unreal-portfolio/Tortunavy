#include "World/Beach/TN_BeachTrapStatusComponent.h"

#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_StaminaComponent.h"
#include "Player/TortugaCharacter.h"
#include "TimerManager.h"
#include "TN_BeachRideKit.h"
#include "World/Beach/TN_BeachEnemy.h"

namespace TNBeachTrapStatusDetail
{
	/** Tope de seguridad de una atrapada (s): si quien la atrapó no la suelta, la suelta el componente. */
	constexpr float MaxTrappedSeconds = 8.f;
	/** Pulsaciones de forcejeo que el dueño manda como mucho por segundo (el servidor aplica el mismo límite). */
	constexpr double MinPressGap = TNBeachCreatureRules::EscapeMinPressGap;
}

UTN_BeachTrapStatusComponent::UTN_BeachTrapStatusComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UTN_BeachTrapStatusComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTN_BeachTrapStatusComponent, SlowState);
	DOREPLIFETIME(UTN_BeachTrapStatusComponent, TrapState);
	DOREPLIFETIME_CONDITION(UTN_BeachTrapStatusComponent, bEscapeArmed, COND_OwnerOnly);
}

void UTN_BeachTrapStatusComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SlowTimer);
		World->GetTimerManager().ClearTimer(DizzyTimer);
		World->GetTimerManager().ClearTimer(WatchdogTimer);
		World->GetTimerManager().ClearTimer(EscapeWatchTimer);
	}
	if (APlayerController* PC = IgnoringController.Get())
	{
		PC->SetIgnoreMoveInput(false);
	}
	IgnoringController.Reset();
	Super::EndPlay(EndPlayReason);
}

UTN_BeachTrapStatusComponent* UTN_BeachTrapStatusComponent::FindOn(const AActor* Turtle)
{
	return Turtle ? Turtle->FindComponentByClass<UTN_BeachTrapStatusComponent>() : nullptr;
}

UTN_BeachTrapStatusComponent* UTN_BeachTrapStatusComponent::FindOrAddTo(ACharacter* Turtle)
{
	if (!IsValid(Turtle))
	{
		return nullptr;
	}
	if (UTN_BeachTrapStatusComponent* Existing = FindOn(Turtle))
	{
		return Existing;
	}
	if (!Turtle->HasAuthority())
	{
		return nullptr;
	}
	UTN_BeachTrapStatusComponent* Comp = NewObject<UTN_BeachTrapStatusComponent>(Turtle, TEXT("BeachTrapStatus"));
	Turtle->AddInstanceComponent(Comp);
	Comp->RegisterComponent();
	return Comp;
}

FName UTN_BeachTrapStatusComponent::SlowSource()
{
	static const FName Name(TEXT("BeachCreatureSlow"));
	return Name;
}

bool UTN_BeachTrapStatusComponent::SimulatesOwner() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn && (Pawn->HasAuthority() || Pawn->IsLocallyControlled());
}

double UTN_BeachTrapStatusComponent::ServerNowSeconds() const
{
	return ATN_BeachEnemy::ServerNow(this);
}

// ─────────────────────────────────────────────────────────────────────────────
// Ralentización
// ─────────────────────────────────────────────────────────────────────────────

void UTN_BeachTrapStatusComponent::ServerSlow(float SpeedFactor, float Seconds)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Seconds <= 0.f)
	{
		return;
	}
	const double Now = ServerNowSeconds();
	const float NewEnd = static_cast<float>(Now + Seconds);
	const bool bActive = SlowState.EndServerTime > Now;
	SlowState.SpeedFactor = bActive ? FMath::Min(SlowState.SpeedFactor, SpeedFactor) : FMath::Clamp(SpeedFactor, 0.05f, 1.f);
	SlowState.EndServerTime = bActive ? FMath::Max(SlowState.EndServerTime, NewEnd) : NewEnd;
	++SlowState.Serial;
	ApplySlowLocal();
	GetOwner()->ForceNetUpdate();
}

bool UTN_BeachTrapStatusComponent::IsSlowed() const
{
	return SlowState.EndServerTime > ServerNowSeconds();
}

void UTN_BeachTrapStatusComponent::OnRep_Slow()
{
	ApplySlowLocal();
}

void UTN_BeachTrapStatusComponent::ApplySlowLocal()
{
	UWorld* World = GetWorld();
	if (!World || !SimulatesOwner())
	{
		return;
	}
	const double Left = SlowState.EndServerTime - ServerNowSeconds();
	if (Left <= 0.0)
	{
		ClearSlowLocal();
		return;
	}
	if (UTN_StaminaComponent* Stamina = GetOwner()->FindComponentByClass<UTN_StaminaComponent>())
	{
		Stamina->SetSpeedCap(SlowSource(), Stamina->GetWalkSpeed() * SlowState.SpeedFactor);
	}
	World->GetTimerManager().SetTimer(SlowTimer, FTimerDelegate::CreateUObject(this, &UTN_BeachTrapStatusComponent::ClearSlowLocal),
		static_cast<float>(Left), false);
}

void UTN_BeachTrapStatusComponent::ClearSlowLocal()
{
	if (UTN_StaminaComponent* Stamina = GetOwner() ? GetOwner()->FindComponentByClass<UTN_StaminaComponent>() : nullptr)
	{
		Stamina->ClearSpeedCap(SlowSource());
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Atrapada
// ─────────────────────────────────────────────────────────────────────────────

void UTN_BeachTrapStatusComponent::ServerTrap(AActor* By, const FVector& Anchor)
{
	UWorld* World = GetWorld();
	if (!World || !GetOwner() || !GetOwner()->HasAuthority() || TrapState.bTrapped)
	{
		return;
	}
	TrappedBy = By;
	TrappedAt = World->GetTimeSeconds();
	TrapState.bTrapped = true;
	TrapState.Anchor = Anchor;
	TrapState.HopVelocity = FVector::ZeroVector;
	TrapState.DizzySeconds = 0.f;
	++TrapState.Serial;
	ServerArmEscape(true);
	OnRep_Trap();
	World->GetTimerManager().SetTimer(WatchdogTimer, FTimerDelegate::CreateUObject(this, &UTN_BeachTrapStatusComponent::ServerTrapWatchdog), 0.5f, true);
	GetOwner()->ForceNetUpdate();
}

void UTN_BeachTrapStatusComponent::ServerRelease(const FVector& HopVelocity, float DizzySeconds)
{
	UWorld* World = GetWorld();
	if (!World || !GetOwner() || !GetOwner()->HasAuthority() || !TrapState.bTrapped)
	{
		return;
	}
	TrapState.bTrapped = false;
	TrapState.HopVelocity = HopVelocity;
	TrapState.DizzySeconds = DizzySeconds;
	++TrapState.Serial;
	TrappedBy.Reset();
	ServerArmEscape(false);
	World->GetTimerManager().ClearTimer(WatchdogTimer);
	OnRep_Trap();
	if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner()); Turtle && DizzySeconds > 0.f)
	{
		Turtle->ApplyMareoEffect(DizzySeconds);
	}
	GetOwner()->ForceNetUpdate();
}

void UTN_BeachTrapStatusComponent::ServerTrapWatchdog()
{
	const UWorld* World = GetWorld();
	if (!World || !TrapState.bTrapped)
	{
		return;
	}
	const bool bLost = !TrappedBy.IsValid();
	const bool bTooLong = World->GetTimeSeconds() - TrappedAt > TNBeachTrapStatusDetail::MaxTrappedSeconds;
	const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner());
	if (bLost || bTooLong || !Turtle || Turtle->IsDead())
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Playa] %s: se suelta sola de una trampa (%s)."), *GetNameSafe(GetOwner()),
			bLost ? TEXT("la trampa ya no está") : TEXT("tope de tiempo"));
		ServerRelease(FVector(0.0, 0.0, 400.0), 0.f);
	}
}

void UTN_BeachTrapStatusComponent::OnRep_Trap()
{
	if (TrapState.Serial == AppliedTrapSerial && TrapState.bTrapped == bTrapApplied)
	{
		return;
	}
	AppliedTrapSerial = TrapState.Serial;
	if (TrapState.bTrapped)
	{
		ApplyTrapLocal();
	}
	else
	{
		ReleaseTrapLocal();
	}
}

void UTN_BeachTrapStatusComponent::ApplyTrapLocal()
{
	ACharacter* Turtle = Cast<ACharacter>(GetOwner());
	if (!Turtle)
	{
		return;
	}
	bTrapApplied = true;
	if (SimulatesOwner())
	{
		if (UCharacterMovementComponent* Move = Turtle->GetCharacterMovement())
		{
			Move->StopMovementImmediately();
			Move->DisableMovement();
		}
		Turtle->SetActorLocation(FVector(TrapState.Anchor), false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (Turtle->IsLocallyControlled())
	{
		APlayerController* PC = Cast<APlayerController>(Turtle->GetController());
		if (PC && PC->IsLocalController() && !IgnoringController.IsValid())
		{
			PC->SetIgnoreMoveInput(true);
			IgnoringController = PC;
		}
	}
	TNBeachRideKit::SetDizzyBirds(Turtle, false);
}

void UTN_BeachTrapStatusComponent::ReleaseTrapLocal()
{
	ACharacter* Turtle = Cast<ACharacter>(GetOwner());
	const bool bWasApplied = bTrapApplied;
	bTrapApplied = false;
	if (APlayerController* PC = IgnoringController.Get())
	{
		PC->SetIgnoreMoveInput(false);
	}
	IgnoringController.Reset();
	if (!Turtle || !bWasApplied)
	{
		return;
	}
	if (SimulatesOwner())
	{
		UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
		if (Move && Move->MovementMode == MOVE_None)
		{
			Move->SetMovementMode(MOVE_Falling);
			const FVector Hop(TrapState.HopVelocity);
			if (!Hop.IsNearlyZero())
			{
				Turtle->LaunchCharacter(Hop, true, true);
			}
		}
	}
	if (TrapState.DizzySeconds > 0.f && GetWorld())
	{
		TNBeachRideKit::SetDizzyBirds(Turtle, true);
		GetWorld()->GetTimerManager().SetTimer(DizzyTimer, FTimerDelegate::CreateUObject(this, &UTN_BeachTrapStatusComponent::EndDizzyLocal),
			TrapState.DizzySeconds, false);
	}
}

void UTN_BeachTrapStatusComponent::EndDizzyLocal()
{
	TNBeachRideKit::SetDizzyBirds(Cast<ACharacter>(GetOwner()), false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Forcejeo
// ─────────────────────────────────────────────────────────────────────────────

void UTN_BeachTrapStatusComponent::ServerArmEscape(bool bArmed)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (bArmed && !bEscapeArmed)
	{
		Mash.Reset();
	}
	if (bEscapeArmed != bArmed)
	{
		bEscapeArmed = bArmed;
		GetOwner()->ForceNetUpdate();
	}
	OrphanEscapeLooks = 0;
	if (UWorld* World = GetWorld())
	{
		FTimerManager& Timers = World->GetTimerManager();
		if (!bArmed)
		{
			Timers.ClearTimer(EscapeWatchTimer);
		}
		else if (!Timers.IsTimerActive(EscapeWatchTimer))
		{
			Timers.SetTimer(EscapeWatchTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { ServerCheckOrphanEscape(); }),
				EscapeWatchSeconds, true);
		}
	}
}

bool UTN_BeachTrapStatusComponent::ServerCheckOrphanEscape()
{
	const AActor* Owner = GetOwner();
	if (!bEscapeArmed || !Owner || !Owner->HasAuthority())
	{
		OrphanEscapeLooks = 0;
		return false;
	}
	if (TrapState.bTrapped || ATN_BeachEnemy::IsTurtleHeld(Cast<ATortugaCharacter>(Owner)))
	{
		OrphanEscapeLooks = 0;
		return false;
	}
	// Una mirada suelta no cuenta: entre el agarre y la primera sujeción puede pasar un fotograma.
	if (++OrphanEscapeLooks < 2)
	{
		return false;
	}
	UE_LOG(LogTortunabo, Warning, TEXT("[Playa] %s: forcejeo armado sin nadie que la sujete; se desarma."), *GetNameSafe(Owner));
	ServerArmEscape(false);
	return true;
}

void UTN_BeachTrapStatusComponent::PressEscape()
{
	if (!bEscapeArmed || !GetOwner())
	{
		return;
	}
	if (GetOwner()->HasAuthority())
	{
		Mash.Press(ServerNowSeconds(), TNBeachCreatureRules::EscapeDecay);
		return;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetRealTimeSeconds() : 0.0;
	if (LastPressSent >= 0.0 && Now - LastPressSent < TNBeachTrapStatusDetail::MinPressGap)
	{
		return;
	}
	LastPressSent = Now;
	ServerEscapePress();
}

void UTN_BeachTrapStatusComponent::ServerEscapePress_Implementation()
{
	// El cliente ya limita la frecuencia, pero uno modificado se soltaría al instante: el servidor ignora las seguidas (#896).
	if (bEscapeArmed)
	{
		Mash.PressAtMostEvery(ServerNowSeconds(), TNBeachCreatureRules::EscapeDecay, TNBeachTrapStatusDetail::MinPressGap);
	}
}

bool UTN_BeachTrapStatusComponent::HasEscaped() const
{
	return bEscapeArmed && TNBeachCreatureRules::HasEscaped(Mash, ServerNowSeconds());
}
