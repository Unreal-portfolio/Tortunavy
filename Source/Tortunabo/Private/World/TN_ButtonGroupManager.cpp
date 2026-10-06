#include "World/TN_ButtonGroupManager.h"
#include "World/TN_ButtonInteractable.h"
#include "Core/TN_Log.h"
#include "World/TN_PuzzleScoreSubsystem.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"

// ── FTN_TransformAction ───────────────────────────────────────────────────────

void FTN_TransformAction::ApplyAll(TArray<FTN_TransformAction>& Actions, bool bForward, UWorld* World, bool bOnlyClientMoved)
{
	for (FTN_TransformAction& Action : Actions)
	{
		// ── Lazy tag resolution ───────────────────────────────────────────────
		// Si TargetActor es null pero hay un tag configurado, hacemos un world scan
		// y cacheamos el resultado en TargetActor para que las llamadas siguientes
		// sean O(1). Útil para actores spawneados desde chunks en runtime.
		if (!Action.TargetActor && Action.TargetActorTag != NAME_None && World)
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->ActorHasTag(Action.TargetActorTag))
				{
					Action.TargetActor = *It;
					UE_LOG(LogTortunabo, Log, TEXT("[TransformAction] Resolved tag '%s' → '%s'"),
						*Action.TargetActorTag.ToString(), *GetNameSafe(*It));
					break;
				}
			}
			if (!Action.TargetActor)
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[TransformAction] No actor found with tag '%s'"),
					*Action.TargetActorTag.ToString());
			}
		}

		if (!Action.TargetActor) { continue; }

		AActor* Target = Action.TargetActor;
		if (bOnlyClientMoved && !TNButtonGroupRules::ClientMovesTarget(Target->GetIsReplicated(), Target->IsReplicatingMovement()))
		{
			continue;
		}
		const FVector  LocDelta = bForward ? Action.LocationOffset : -Action.LocationOffset;
		const FRotator RotDelta = bForward
			? Action.RotationOffset
			: FRotator(-Action.RotationOffset.Pitch, -Action.RotationOffset.Yaw, -Action.RotationOffset.Roll);

		Target->SetActorLocation(Target->GetActorLocation() + LocDelta);
		Target->SetActorRotation(Target->GetActorRotation() + RotDelta);
	}
}

ATN_ButtonGroupManager::ATN_ButtonGroupManager()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
}

void ATN_ButtonGroupManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_ButtonGroupManager, bGroupActive);
}

void ATN_ButtonGroupManager::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority()) { return; }

	// Cuenta para la eficiencia de puzle de la puntuación final del Coop (#789).
	UTN_PuzzleScoreSubsystem::Register(this);

	// Suscribirse al delegate de cada botón gestionado (eyedropper clásico)
	for (ATN_ButtonInteractable* Button : ManagedButtons)
	{
		if (!Button) { continue; }
		Button->OnActivationChanged.AddUObject(this, &ATN_ButtonGroupManager::OnButtonActivationChanged);
	}

	// Scan diferido: los botones en chunks spawneados en el mismo tick que el
	// manager aún no existen en BeginPlay → esperamos un tick.
	if (ManagedButtonTags.Num() > 0)
	{
		GetWorldTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &ATN_ButtonGroupManager::DeferredTagButtonScan));
	}
}

void ATN_ButtonGroupManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (ATN_ButtonInteractable* Button : ManagedButtons)
	{
		if (Button)
		{
			Button->OnActivationChanged.RemoveAll(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

// ── Lógica ─────────────────────────────────────────────────────────────────────

void ATN_ButtonGroupManager::OnButtonActivationChanged(ATN_ButtonInteractable* /*Button*/, bool bActivated)
{
	if (!HasAuthority()) { return; }

	// Si ya se disparó y es one-shot, no procesar más cambios
	if (bTriggered) { return; }

	// El primer botón pulsado pone en marcha el reloj de la eficiencia de puzle (#789).
	if (bActivated) { UTN_PuzzleScoreSubsystem::NotifyProgress(this); }

	CheckAndTrigger();
}

void ATN_ButtonGroupManager::CheckAndTrigger()
{
	// Contar cuántos botones están activados actualmente
	int32 ActiveCount = 0;
	for (const ATN_ButtonInteractable* Button : ManagedButtons)
	{
		if (Button && Button->IsActivated()) { ++ActiveCount; }
	}

	switch (TNButtonGroupRules::Decide(ActiveCount, GetEffectiveThreshold(), bGroupActive, bOneShot))
	{
	case TNButtonGroupRules::EGroupChange::Activate:
		// Umbral alcanzado: disparar. El estado replica solo (también a quien entre tarde); el efecto, al momento.
		bGroupActive = true;
		bTriggered = bOneShot;
		UTN_PuzzleScoreSubsystem::NotifySolved(this);
		ApplyTriggerActions(true);
		MulticastPlayGroupEffect(true);
		ForceNetUpdate();
		break;
	case TNButtonGroupRules::EGroupChange::Deactivate:
		// Bajamos de umbral y no es one-shot: revertir.
		bGroupActive = false;
		ApplyTriggerActions(false);
		MulticastPlayGroupEffect(false);
		ForceNetUpdate();
		break;
	default:
		break;
	}
}

void ATN_ButtonGroupManager::OnRep_GroupActive()
{
	// Clientes: los objetivos cuyo movimiento no replica el servidor los mueve cada cliente, una vez por cambio. Llega
	// también a quien entra tarde con el puzzle ya resuelto (antes solo había un multicast que no le llegaba).
	if (bGroupActive != bClientActionsApplied)
	{
		FTN_TransformAction::ApplyAll(TriggerActions, bGroupActive, GetWorld(), true);
		bClientActionsApplied = bGroupActive;
	}
}

int32 ATN_ButtonGroupManager::GetEffectiveThreshold() const
{
	const int32 N = ManagedButtons.Num();
	if (N <= 0) { return 1; }
	if (TriggerThreshold <= 0) { return N; }
	return FMath::Clamp(TriggerThreshold, 1, N);
}

void ATN_ButtonGroupManager::ApplyTriggerActions(bool bForward)
{
	FTN_TransformAction::ApplyAll(TriggerActions, bForward, GetWorld());
}

void ATN_ButtonGroupManager::RegisterButton(ATN_ButtonInteractable* Button)
{
	if (!Button || !HasAuthority()) { return; }

	// Idempotente: no añadir duplicados
	for (const TObjectPtr<ATN_ButtonInteractable>& Existing : ManagedButtons)
	{
		if (Existing == Button) { return; }
	}

	ManagedButtons.Add(Button);
	Button->OnActivationChanged.AddUObject(this, &ATN_ButtonGroupManager::OnButtonActivationChanged);

	UE_LOG(LogTortunabo, Log, TEXT("[ButtonGroupManager] '%s' registró botón '%s' en runtime."),
		*GetName(), *GetNameSafe(Button));

	// Re-evaluar inmediatamente por si el botón ya estaba activado
	if (!bTriggered) { CheckAndTrigger(); }
}

void ATN_ButtonGroupManager::DeferredTagButtonScan()
{
	if (!HasAuthority()) { return; }

	for (TActorIterator<ATN_ButtonInteractable> It(GetWorld()); It; ++It)
	{
		ATN_ButtonInteractable* Button = *It;
		if (!Button) { continue; }
		for (const FName& Tag : ManagedButtonTags)
		{
			if (Tag != NAME_None && Button->ActorHasTag(Tag))
			{
				RegisterButton(Button);
				break;  // un botón puede tener varios tags, pero solo registrar una vez
			}
		}
	}
}

void ATN_ButtonGroupManager::MulticastPlayGroupEffect_Implementation(bool bActivated)
{
	PlayGroupEffect(bActivated);
}

void ATN_ButtonGroupManager::PlayGroupEffect(bool bActivated) const
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (USoundBase* Sound = bActivated ? ActivatedSound.Get() : DeactivatedSound.Get())
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation());
	}
	if (bActivated && ActivatedVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ActivatedVFX, GetActorLocation());
	}
}
