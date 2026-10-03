#include "World/TN_TotemInteractable.h"
#include "Core/TN_Log.h"
#include "Player/TortugaCharacter.h"
#include "Core/TN_CoopPlayerState.h"
#include "Game/TN_RunGameMode.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

ATN_TotemInteractable::ATN_TotemInteractable()
{
	PromptText           = NSLOCTEXT("Tortunabo", "TotemPrompt", "Activar Tótem");
	InteractionDistance  = 250.f;
	CooldownSeconds      = 5.f;
}

void ATN_TotemInteractable::Interact(APawn* Interactor)
{
	if (!HasAuthority() || !Interactor || !CanInteract(Interactor)) { return; }

	// ── Buscar jugadores eliminados ───────────────────────────────────────────
	TArray<APlayerController*> DeadPlayers;
	for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
	{
		APlayerController* PC = *It;
		if (!PC) { continue; }
		ATN_CoopPlayerState* PS = PC->GetPlayerState<ATN_CoopPlayerState>();
		if (PS && PS->bIsEliminated)
		{
			DeadPlayers.Add(PC);
		}
	}

	if (DeadPlayers.Num() == 0)
	{
		MulticastOnNoTarget();
		return;
	}

	// ── Seleccionar aleatoriamente ────────────────────────────────────────────
	const int32 Idx = FMath::RandRange(0, DeadPlayers.Num() - 1);
	APlayerController* TargetPC = DeadPlayers[Idx];

	// ── Revivir via RunGameMode ────────────────────────────────────────────────
	ATN_RunGameMode* GM = Cast<ATN_RunGameMode>(GetWorld()->GetAuthGameMode());
	if (!GM)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Totem] No ATN_RunGameMode found — cannot revive."));
		return;
	}

	GM->RevivePlayer(TargetPC);

	// ── Teleportar al revivido cerca del activador ────────────────────────────
	// GetPawn() puede ser null si el spawn falló (no había spawn point, etc.)
	APawn* RevivedPawn = TargetPC->GetPawn();
	if (!RevivedPawn)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Totem] RevivePlayer succeeded but GetPawn() returned null for '%s'"),
			*GetNameSafe(TargetPC));
		MulticastOnNoTarget();
		return;
	}

	{
		const FVector ActivatorLoc = Interactor->GetActorLocation();
		const FVector RightOffset  = Interactor->GetActorRightVector() * ReviveSpawnOffset;
		const FVector SpawnLoc     = ActivatorLoc + RightOffset;
		RevivedPawn->TeleportTo(SpawnLoc, Interactor->GetActorRotation());
	}

	// ── Feedback visual ────────────────────────────────────────────────────────
	// Viaja el PlayerState (siempre resuelto en clientes), no el pawn recién
	// re-poseído (canal posiblemente sin abrir → llegaría null).
	APlayerState* RevivedPS = TargetPC->PlayerState;
	ATortugaCharacter* ActivatorChar = Cast<ATortugaCharacter>(Interactor);

	// ── Consumir o cooldown ───────────────────────────────────────────────────
	if (bDestroyAfterUse)
	{
		// NO Destroy() inmediato tras el multicast: el canal puede cerrarse antes
		// de enviar el RPC y los clientes no verían la activación. Patrón del
		// proyecto: consumir (invisible + sin interacción) + SetLifeSpan.
		// HideInteractableMesh hace FlushNetDormancy, despertando el canal para
		// que el multicast salga en el próximo paquete.
		HideInteractableMesh();
		SetInteractionEnabled(false);
		SetLifeSpan(1.5f);
		MulticastOnActivated(RevivedPS, ActivatorChar);
		return;
	}

	MulticastOnActivated(RevivedPS, ActivatorChar);
	// Delegar a la base para gestionar cooldown
	Super::Interact(Interactor);
}

void ATN_TotemInteractable::MulticastOnActivated_Implementation(APlayerState* RevivedPS,
	ATortugaCharacter* Activator)
{
	if (SoundActivate)
	{
		UGameplayStatics::PlaySoundAtLocation(this, SoundActivate, GetActorLocation());
	}
	if (VFXActivate)
	{
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), VFXActivate, GetActorLocation());
	}
	NotifyActivatedWhenResolved(RevivedPS, Activator, /*AttemptsLeft=*/5);
}

void ATN_TotemInteractable::NotifyActivatedWhenResolved(APlayerState* RevivedPS,
	ATortugaCharacter* Activator, int32 AttemptsLeft)
{
	ATortugaCharacter* Revived = RevivedPS ? Cast<ATortugaCharacter>(RevivedPS->GetPawn()) : nullptr;
	if (Revived || !RevivedPS || AttemptsLeft <= 0)
	{
		OnTotemActivated(Revived, Activator);
		return;
	}

	// El pawn del revivido aún no replicó en esta máquina — reintentar en breve.
	// Weak captures: si el PS o el activador desaparecen mientras tanto, se
	// dispara igualmente con lo que quede válido.
	TWeakObjectPtr<APlayerState> WeakPS(RevivedPS);
	TWeakObjectPtr<ATortugaCharacter> WeakActivator(Activator);
	FTimerHandle RetryHandle;
	GetWorldTimerManager().SetTimer(RetryHandle,
		FTimerDelegate::CreateWeakLambda(this, [this, WeakPS, WeakActivator, AttemptsLeft]()
		{
			NotifyActivatedWhenResolved(WeakPS.Get(), WeakActivator.Get(), AttemptsLeft - 1);
		}),
		0.2f, false);
}

void ATN_TotemInteractable::MulticastOnNoTarget_Implementation()
{
	if (SoundNoTarget)
	{
		UGameplayStatics::PlaySoundAtLocation(this, SoundNoTarget, GetActorLocation());
	}
	OnTotemNoTarget();
}
