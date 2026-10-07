#include "Player/TN_VitalsComponent.h"

#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Game/TN_RunGameMode.h"
#include "Net/UnrealNetwork.h"
#include "Player/TortugaCharacter.h"

UTN_VitalsComponent::UTN_VitalsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

UTN_VitalsComponent* UTN_VitalsComponent::FindOn(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UTN_VitalsComponent>() : nullptr;
}

void UTN_VitalsComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!IsServer())
	{
		return;
	}
	const TNVitals::FState Full = TNVitals::Full(MakeParams());
	Health = Full.Health;
	Hydration = Full.Hydration;
	// Solo el servidor avanza los vitales; los clientes los reciben por replicación.
	SetComponentTickInterval(ServerStepSeconds);
	SetComponentTickEnabled(true);
}

void UTN_VitalsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTN_VitalsComponent, Health);
	DOREPLIFETIME(UTN_VitalsComponent, Hydration);
	DOREPLIFETIME(UTN_VitalsComponent, Poison);
}

void UTN_VitalsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AdvanceVitals(DeltaTime);
}

void UTN_VitalsComponent::AdvanceVitals(float DeltaSeconds)
{
	if (!IsServer() || !CanBeHurt())
	{
		return;
	}
	const TNVitals::FStepResult Result = TNVitals::Step(MakeState(), MakeParams(), DeltaSeconds);
	const ETNDeathCause Cause = Result.DepletedBy == TNVitals::EDepletedBy::Poison ? ETNDeathCause::Poison : ETNDeathCause::Dehydration;
	CommitState(Result.State, Cause);
}

float UTN_VitalsComponent::ApplyDamage(float Amount, AActor* DamageCauser, ETNDeathCause Cause)
{
	if (!IsServer() || Amount <= 0.f || !CanBeHurt())
	{
		return 0.f;
	}
	const float Before = Health;
	const ETNDeathCause Resolved = Cause != ETNDeathCause::Unknown ? Cause : TNDeathCause::FromInstigator(DamageCauser, GetOwner());
	CommitState(TNVitals::Damage(MakeState(), Amount), Resolved);
	UE_LOG(LogTortunabo, Log, TEXT("[Vitals] %s recibe %.1f de daño de %s: vida %.1f → %.1f"),
		*GetNameSafe(GetOwner()), Amount, *GetNameSafe(DamageCauser), Before, Health);
	return Before - Health;
}

float UTN_VitalsComponent::Heal(float Amount)
{
	if (!IsServer())
	{
		return 0.f;
	}
	const float Before = Health;
	CommitState(TNVitals::Heal(MakeState(), MakeParams(), Amount), ETNDeathCause::Unknown);
	return Health - Before;
}

void UTN_VitalsComponent::ApplyPoison(float DamagePerSecond, float Seconds)
{
	if (!IsServer() || !CanBeHurt())
	{
		return;
	}
	CommitState(TNVitals::Poison(MakeState(), DamagePerSecond, Seconds), ETNDeathCause::Poison);
}

void UTN_VitalsComponent::CurePoison()
{
	if (IsServer())
	{
		CommitState(TNVitals::CurePoison(MakeState()), ETNDeathCause::Unknown);
	}
}

float UTN_VitalsComponent::Hydrate(float Amount)
{
	if (!IsServer())
	{
		return 0.f;
	}
	const float Before = Hydration;
	CommitState(TNVitals::Hydrate(MakeState(), MakeParams(), Amount), ETNDeathCause::Unknown);
	return Hydration - Before;
}

void UTN_VitalsComponent::RestoreAll()
{
	if (IsServer())
	{
		CommitState(TNVitals::Full(MakeParams()), ETNDeathCause::Unknown);
	}
}

float UTN_VitalsComponent::GetPoisonSecondsLeft() const
{
	if (!IsPoisoned())
	{
		return 0.f;
	}
	return IsServer() ? PoisonSecondsLeft : FMath::Max(0.f, Poison.EndsAtServerTime - ServerTimeSeconds());
}

void UTN_VitalsComponent::CommitState(const TNVitals::FState& NewState, ETNDeathCause CauseIfDepleted)
{
	const bool bWasDepleted = IsDepleted();
	const float OldHealth = Health;
	const float OldHydration = Hydration;
	const FTNPoisonNet OldPoison = Poison;

	Health = NewState.Health;
	Hydration = NewState.Hydration;
	PoisonSecondsLeft = NewState.IsPoisoned() ? NewState.PoisonSecondsLeft : 0.f;
	Poison.DamagePerSecond = NewState.IsPoisoned() ? NewState.PoisonDamagePerSecond : 0.f;
	// La hora de fin solo cambia al poner o alargar el veneno: así no se replica en cada paso.
	if (!NewState.IsPoisoned())
	{
		Poison.EndsAtServerTime = 0.f;
	}
	else if (!FMath::IsNearlyEqual(Poison.EndsAtServerTime - ServerTimeSeconds(), PoisonSecondsLeft, ServerStepSeconds))
	{
		Poison.EndsAtServerTime = ServerTimeSeconds() + PoisonSecondsLeft;
	}

	if (Health != OldHealth)
	{
		OnHealthChanged.Broadcast(Health, OldHealth);
	}
	if (Hydration != OldHydration)
	{
		OnHydrationChanged.Broadcast(Hydration, OldHydration);
	}
	if (Poison.DamagePerSecond != OldPoison.DamagePerSecond)
	{
		OnPoisonChanged.Broadcast(IsPoisoned(), Poison.DamagePerSecond);
	}

	if (bWasDepleted || !IsDepleted())
	{
		return;
	}
	DepletionCause = CauseIfDepleted;
	UE_LOG(LogTortunabo, Log, TEXT("[Vitals] %s se queda sin vida (causa %s): pide la muerte"),
		*GetNameSafe(GetOwner()), *UEnum::GetValueAsString(CauseIfDepleted));
	if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner()))
	{
		Turtle->RequestKillBy(CauseIfDepleted);
	}
}

void UTN_VitalsComponent::OnRep_Health(float OldHealth)
{
	OnHealthChanged.Broadcast(Health, OldHealth);
}

void UTN_VitalsComponent::OnRep_Hydration(float OldHydration)
{
	OnHydrationChanged.Broadcast(Hydration, OldHydration);
}

void UTN_VitalsComponent::OnRep_Poison(const FTNPoisonNet& OldPoison)
{
	if (Poison.DamagePerSecond != OldPoison.DamagePerSecond)
	{
		OnPoisonChanged.Broadcast(IsPoisoned(), Poison.DamagePerSecond);
	}
}

bool UTN_VitalsComponent::IsServer() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

bool UTN_VitalsComponent::CanBeHurt() const
{
	const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner());
	if (!Turtle || Turtle->IsDead())
	{
		return false;
	}
	const UWorld* World = GetWorld();
	const ATN_RunGameMode* GameMode = World ? World->GetAuthGameMode<ATN_RunGameMode>() : nullptr;
	APlayerController* PlayerController = Cast<APlayerController>(Turtle->GetController());
	return !(GameMode && PlayerController && GameMode->IsPlayerReviveImmune(PlayerController));
}

TNVitals::FParams UTN_VitalsComponent::MakeParams() const
{
	TNVitals::FParams Params;
	Params.MaxHealth = MaxHealth;
	Params.MaxHydration = MaxHydration;
	Params.HydrationDrainPerSecond = HydrationDrainPerSecond;
	Params.DehydratedHealthDrainPerSecond = DehydratedHealthDrainPerSecond;
	return Params;
}

TNVitals::FState UTN_VitalsComponent::MakeState() const
{
	TNVitals::FState State;
	State.Health = Health;
	State.Hydration = Hydration;
	State.PoisonDamagePerSecond = Poison.DamagePerSecond;
	State.PoisonSecondsLeft = PoisonSecondsLeft;
	return State;
}

float UTN_VitalsComponent::ServerTimeSeconds() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.f;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? static_cast<float>(GameState->GetServerWorldTimeSeconds()) : World->GetTimeSeconds();
}
