#include "World/TN_PressurePlate.h"
#include "World/TN_ButtonGroupManager.h"   // FTN_TransformAction
#include "Player/TortugaCharacter.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_Log.h"
#include "World/TN_PuzzleScoreSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

// ────────────────────────────────────────────────────────────────────────────
// ATN_PressurePlate
// ────────────────────────────────────────────────────────────────────────────

ATN_PressurePlate::ATN_PressurePlate()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);

	PlateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlateMesh"));
	SetRootComponent(PlateMesh);
	PlateMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PlateMesh->SetCollisionResponseToAllChannels(ECR_Block);

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->SetupAttachment(PlateMesh);
	TriggerBox->SetBoxExtent(FVector(75.f, 75.f, 30.f));
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionObjectType(ECC_WorldDynamic);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);
}

void ATN_PressurePlate::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ATN_PressurePlate::OnTriggerBeginOverlap);
		TriggerBox->OnComponentEndOverlap.AddDynamic(this, &ATN_PressurePlate::OnTriggerEndOverlap);

		// Auto-registro diferido: el manager puede no existir aún en este tick
		if (ManagerTag != NAME_None)
		{
			GetWorldTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(this, &ATN_PressurePlate::DeferredSelfRegisterWithManager));
		}
	}
}

void ATN_PressurePlate::DeferredSelfRegisterWithManager()
{
	if (!HasAuthority() || ManagerTag == NAME_None) { return; }

	for (TActorIterator<ATN_PressurePlateGroupManager> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(ManagerTag))
		{
			It->RegisterPlate(this);
			break;
		}
	}
}

void ATN_PressurePlate::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OccupyingCharacters.Empty();
	Super::EndPlay(EndPlayReason);
}

void ATN_PressurePlate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_PressurePlate, bOccupied);
}

// ── Overlaps ───────────────────────────────────────────────────────────────────

void ATN_PressurePlate::OnTriggerBeginOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (!HasAuthority()) { return; }
	if (ATortugaCharacter* C = Cast<ATortugaCharacter>(OtherActor))
	{
		OccupyingCharacters.Add(C);
		RefreshOccupancy();
	}
}

void ATN_PressurePlate::OnTriggerEndOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/)
{
	if (!HasAuthority()) { return; }
	if (ATortugaCharacter* C = Cast<ATortugaCharacter>(OtherActor))
	{
		OccupyingCharacters.Remove(C);
		RefreshOccupancy();
	}
}

void ATN_PressurePlate::RefreshOccupancy()
{
	// No-op real: solo se añaden punteros válidos en OnTriggerBeginOverlap, así que
	// nunca hay una entrada literalmente nula que este Remove encuentre (un
	// TWeakObjectPtr a un pawn destruido no se vuelve == nullptr). La limpieza
	// efectiva de referencias muertas la hace el Weak.Get() null-check del bucle
	// de abajo y de HasLiveOccupant/ResetLatch.
	OccupyingCharacters.Remove(nullptr);

	// Comprobar si alguno de los personajes encima está vivo
	bool bAnyAliveOccupying = false;
	for (const TWeakObjectPtr<ATortugaCharacter>& Weak : OccupyingCharacters)
	{
		if (ATortugaCharacter* C = Weak.Get())
		{
			if (ATN_CoopPlayerState* PS = C->GetPlayerState<ATN_CoopPlayerState>())
			{
				if (PS->IsAliveAndPlaying())
				{
					bAnyAliveOccupying = true;
					break;
				}
			}
		}
	}

	// Modo Latched: una vez activada, ignoramos transiciones a false mientras no
	// se llame a ResetLatch(). Las transiciones a true (primera activación) sí pasan.
	if (Mode == EPressurePlateMode::Latched && bOccupied && !bAnyAliveOccupying)
	{
		return;
	}

	if (bAnyAliveOccupying != bOccupied)
	{
		bOccupied = bAnyAliveOccupying;
		MulticastOnOccupancyChanged(bOccupied);
		OnOccupancyChanged.Broadcast(this, bOccupied);
	}
}

bool ATN_PressurePlate::HasLiveOccupant() const
{
	// Recalcula en el acto: un jugador que muere encima de la placa puede no generar
	// EndOverlap (el ragdoll sigue solapando), dejando bOccupied pegado en true. La
	// condición de grupo debe mirar la ocupación VIVA real, no el flag replicado.
	for (const TWeakObjectPtr<ATortugaCharacter>& Weak : OccupyingCharacters)
	{
		if (const ATortugaCharacter* C = Weak.Get())
		{
			if (const ATN_CoopPlayerState* PS = C->GetPlayerState<ATN_CoopPlayerState>())
			{
				if (PS->IsAliveAndPlaying())
				{
					return true;
				}
			}
		}
	}
	return false;
}

void ATN_PressurePlate::ResetLatch()
{
	if (!HasAuthority()) { return; }
	if (Mode != EPressurePlateMode::Latched || !bOccupied) { return; }

	// Si hay alguien vivo encima todavía, la placa seguirá activa en el próximo
	// RefreshOccupancy — así que sólo forzamos el reset cuando esté vacía.
	OccupyingCharacters.Remove(nullptr);
	for (const TWeakObjectPtr<ATortugaCharacter>& Weak : OccupyingCharacters)
	{
		if (ATortugaCharacter* C = Weak.Get())
		{
			if (ATN_CoopPlayerState* PS = C->GetPlayerState<ATN_CoopPlayerState>())
			{
				if (PS->IsAliveAndPlaying()) { return; }
			}
		}
	}

	bOccupied = false;
	MulticastOnOccupancyChanged(false);
	OnOccupancyChanged.Broadcast(this, false);
}

void ATN_PressurePlate::OnRep_bOccupied()
{
	// Visual feedback local (sin lógica de juego en clientes)
}

void ATN_PressurePlate::MulticastOnOccupancyChanged_Implementation(bool bNewOccupied)
{
	USoundBase* Sound = bNewOccupied ? PressSound : ReleaseSound;
	if (Sound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, Sound, GetActorLocation());
	}
}

// ────────────────────────────────────────────────────────────────────────────
// ATN_PressurePlateGroupManager
// ────────────────────────────────────────────────────────────────────────────

ATN_PressurePlateGroupManager::ATN_PressurePlateGroupManager()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
}

void ATN_PressurePlateGroupManager::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority()) { return; }

	// Cuenta para la eficiencia de puzle de la puntuación final del Coop (#789).
	UTN_PuzzleScoreSubsystem::Register(this);

	for (ATN_PressurePlate* Plate : ManagedPlates)
	{
		if (Plate)
		{
			Plate->OnOccupancyChanged.AddUObject(this, &ATN_PressurePlateGroupManager::OnOccupancyChanged);
		}
	}

	// Scan diferido por tags — placas en chunks que aún no existen en BeginPlay
	if (ManagedPlateTags.Num() > 0)
	{
		GetWorldTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &ATN_PressurePlateGroupManager::DeferredTagPlateScan));
	}
}

void ATN_PressurePlateGroupManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(HoldTimerHandle);
	for (ATN_PressurePlate* Plate : ManagedPlates)
	{
		if (Plate) { Plate->OnOccupancyChanged.RemoveAll(this); }
	}
	Super::EndPlay(EndPlayReason);
}

void ATN_PressurePlateGroupManager::OnOccupancyChanged(ATN_PressurePlate* /*Plate*/, bool bOccupied)
{
	if (!HasAuthority() || bTriggered) { return; }
	// La primera placa pisada pone en marcha el reloj de la eficiencia de puzle (#789).
	if (bOccupied) { UTN_PuzzleScoreSubsystem::NotifyProgress(this); }
	EvaluateAndHandleCondition();
}

void ATN_PressurePlateGroupManager::EvaluateAndHandleCondition()
{
	const bool bNowMet = EvaluateCondition();

	if (bNowMet && !bConditionMet)
	{
		// Condición recién cumplida — arrancar hold timer
		bConditionMet = true;

		if (HoldDurationRequired <= 0.f)
		{
			OnHoldTimerExpired();
		}
		else
		{
			FTimerDelegate Del = FTimerDelegate::CreateUObject(this, &ATN_PressurePlateGroupManager::OnHoldTimerExpired);
			GetWorldTimerManager().SetTimer(HoldTimerHandle, Del, HoldDurationRequired, false);
		}
	}
	else if (!bNowMet && bConditionMet)
	{
		// Condición perdida — cancelar hold
		bConditionMet = false;
		GetWorldTimerManager().ClearTimer(HoldTimerHandle);
		MulticastNotifyConditionChange(false);
	}
}

void ATN_PressurePlateGroupManager::OnHoldTimerExpired()
{
	if (!bConditionMet || bTriggered) { return; }

	// Re-verificar por si alguien salió entre el último callback y el timer
	if (!EvaluateCondition())
	{
		bConditionMet = false;
		MulticastNotifyConditionChange(false);
		return;
	}

	bTriggered = bOneShot;
	UTN_PuzzleScoreSubsystem::NotifySolved(this);
	ApplyTriggerActions();
	MulticastNotifyConditionChange(true);
}

bool ATN_PressurePlateGroupManager::EvaluateCondition() const
{
	if (ManagedPlates.Num() == 0) { return false; }

	// Contar jugadores vivos
	int32 AlivePlayers = 0;
	if (UWorld* World = GetWorld())
	{
		if (AGameStateBase* GS = World->GetGameState())
		{
			for (APlayerState* PS : GS->PlayerArray)
			{
				if (ATN_CoopPlayerState* TNPS = Cast<ATN_CoopPlayerState>(PS))
				{
					if (TNPS->IsAliveAndPlaying()) { ++AlivePlayers; }
				}
			}
		}
	}

	if (AlivePlayers == 0) { return false; }

	// Contar cuántas placas tienen un jugador VIVO encima AHORA (no el bOccupied
	// replicado, que puede quedar pegado si alguien muere sobre la placa). Evita el
	// exploit de "morir sobre placas para mantenerlas pulsadas".
	int32 PlatesOccupied = 0;
	for (const ATN_PressurePlate* Plate : ManagedPlates)
	{
		if (Plate && Plate->HasLiveOccupant()) { ++PlatesOccupied; }
	}

	return PlatesOccupied >= GetEffectiveThreshold(AlivePlayers);
}

int32 ATN_PressurePlateGroupManager::GetEffectiveThreshold(int32 AlivePlayers) const
{
	const int32 N = ManagedPlates.Num();
	if (N <= 0) { return 1; }
	if (TriggerThreshold <= 0)
	{
		// Comportamiento original: tantas placas ocupadas como jugadores vivos
		return FMath::Clamp(AlivePlayers, 1, N);
	}
	return FMath::Clamp(TriggerThreshold, 1, N);
}

void ATN_PressurePlateGroupManager::ApplyTriggerActions()
{
	FTN_TransformAction::ApplyAll(TriggerActions, true, GetWorld());
}

void ATN_PressurePlateGroupManager::RegisterPlate(ATN_PressurePlate* Plate)
{
	if (!Plate || !HasAuthority()) { return; }

	// Idempotente
	for (const TObjectPtr<ATN_PressurePlate>& Existing : ManagedPlates)
	{
		if (Existing == Plate) { return; }
	}

	ManagedPlates.Add(Plate);
	Plate->OnOccupancyChanged.AddUObject(this, &ATN_PressurePlateGroupManager::OnOccupancyChanged);

	UE_LOG(LogTortunabo, Log, TEXT("[PressurePlateGroupManager] '%s' registró placa '%s' en runtime."),
		*GetName(), *GetNameSafe(Plate));

	// Re-evaluar por si la placa ya estaba activa
	if (!bTriggered) { EvaluateAndHandleCondition(); }
}

void ATN_PressurePlateGroupManager::DeferredTagPlateScan()
{
	if (!HasAuthority()) { return; }

	for (TActorIterator<ATN_PressurePlate> It(GetWorld()); It; ++It)
	{
		ATN_PressurePlate* Plate = *It;
		if (!Plate) { continue; }
		for (const FName& Tag : ManagedPlateTags)
		{
			if (Tag != NAME_None && Plate->ActorHasTag(Tag))
			{
				RegisterPlate(Plate);
				break;
			}
		}
	}
}

void ATN_PressurePlateGroupManager::MulticastNotifyConditionChange_Implementation(bool bMet)
{
	USoundBase* Sound = bMet ? ConditionMetSound : ConditionLostSound;
	if (Sound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, Sound, GetActorLocation());
	}
}
