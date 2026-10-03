// ─────────────────────────────────────────────────────────────────────────────
// TortugaCharacter — Sistema de revive (DBNO) y su audio.
//
// Definiciones extraídas de TortugaCharacter.cpp para mejorar la legibilidad.
// Misma clase ATortugaCharacter en otra unidad de traducción: sin cambios de
// lógica ni de replicación, solo organización.
// ─────────────────────────────────────────────────────────────────────────────

#include "Player/TortugaCharacter.h"
#include "Core/TN_Log.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Components/AudioComponent.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Core/TN_CoopPlayerState.h"
#include "Game/TN_RunGameMode.h"

// ─────────────────────────────────────────────────────────────────────────────
// REVIVE SYSTEM (DBNO)
// ─────────────────────────────────────────────────────────────────────────────

void ATortugaCharacter::TryStartReviveChannel()
{
	if (!HasAuthority()) { return; }
	if (bIsKnockedDown) { return; }  // Can't revive if you're knocked down yourself

	const FVector MyLoc = GetActorLocation();
	APlayerController* BestTargetPC = nullptr;
	float BestDistSq = ReviveRadiusCm * ReviveRadiusCm;

	// Buscar el jugador más cercano que esté en DBNO o noquedado (puffer-fish).
	// bIsDBNO: sistema de bleedout (actualmente desactivado).
	// bIsKnockedDown: knockdown por puffer-fish — también revivible por compañero.
	if (const UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* OtherPC = It->Get();
			if (!OtherPC || OtherPC == GetController()) { continue; }

			const ATN_CoopPlayerState* OtherPS = OtherPC->GetPlayerState<ATN_CoopPlayerState>();
			const APawn* OtherPawn = OtherPC->GetPawn();
			if (!OtherPawn) { continue; }

			const ATortugaCharacter* OtherTurtle = Cast<ATortugaCharacter>(OtherPawn);
			const bool bTargetNeedsRevive = (OtherPS && OtherPS->bIsDBNO)
				|| (OtherTurtle && OtherTurtle->IsKnockedDown());
			if (!bTargetNeedsRevive) { continue; }

			const float DistSq = FVector::DistSquared(MyLoc, OtherPawn->GetActorLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestTargetPC = OtherPC;
			}
		}
	}

	if (!BestTargetPC)
	{
		return;  // No DBNO player in range
	}

	// Start channeling
	ReviveTargetPC = BestTargetPC;
	ReviveChannelElapsed = 0.f;
	bIsReviving = true;
	ReviveProgress = 0.f;

	// ── Audio: canal de revive (spatialized, todos los cercanos lo oyen) ──
	PlayReviveChannelSound();

	GetWorldTimerManager().SetTimer(ReviveChannelTimerHandle, this,
		&ATortugaCharacter::TickReviveChannel, ReviveChannelTickInterval, true);

	UE_LOG(LogTortunabo, Log, TEXT("[Revive] %s started reviving %s (%.0fcm)"),
		*GetNameSafe(this), *GetNameSafe(BestTargetPC),
		FMath::Sqrt(BestDistSq));
}

void ATortugaCharacter::CancelReviveChannel()
{
	if (!bIsReviving) { return; }

	bIsReviving = false;
	ReviveProgress = 0.f;
	ReviveTargetPC.Reset();
	ReviveChannelElapsed = 0.f;
	GetWorldTimerManager().ClearTimer(ReviveChannelTimerHandle);

	// ── Parar audio del canal de revive ──
	StopReviveChannelSound();

	UE_LOG(LogTortunabo, Log, TEXT("[Revive] %s cancelled revive channel"), *GetNameSafe(this));
}

void ATortugaCharacter::OnRep_IsReviving()
{
	// Fired on remote clients (incluido el propio reviver): el servidor ya reprodujo/paró
	// el sonido localmente en TryStartReviveChannel/CancelReviveChannel.
	if (bIsReviving)
	{
		PlayReviveChannelSound();
	}
	else
	{
		StopReviveChannelSound();
	}
}

void ATortugaCharacter::TickReviveChannel()
{
	if (!HasAuthority())
	{
		CancelReviveChannel();
		return;
	}

	// Validate: reviver is still emoting
	if (ReplicatedEmoteIndex < 0)
	{
		CancelReviveChannel();
		return;
	}

	// Validate: reviver is not knocked down
	if (bIsKnockedDown)
	{
		CancelReviveChannel();
		return;
	}

	// Validate: reviver hasn't finished the race or died
	if (APlayerController* MyPC = Cast<APlayerController>(GetController()))
	{
		if (const ATN_CoopPlayerState* MyPS = MyPC->GetPlayerState<ATN_CoopPlayerState>())
		{
			if (!MyPS->bIsAlive || MyPS->bHasFinishedRun)
			{
				CancelReviveChannel();
				return;
			}
		}
	}

	// Validate: target still valid and in DBNO
	APlayerController* TargetPC = ReviveTargetPC.Get();
	if (!TargetPC)
	{
		CancelReviveChannel();
		return;
	}

	const ATN_CoopPlayerState* TargetPS = TargetPC->GetPlayerState<ATN_CoopPlayerState>();

	// Validate: still in range
	const APawn* TargetPawn = TargetPC->GetPawn();
	if (!TargetPawn)
	{
		CancelReviveChannel();
		return;
	}

	// Validate: target still needs reviving (DBNO o noquedado por puffer-fish)
	const ATortugaCharacter* TargetTurtle = Cast<ATortugaCharacter>(TargetPawn);
	const bool bTargetStillRevivable = (TargetPS && TargetPS->bIsDBNO)
		|| (TargetTurtle && TargetTurtle->IsKnockedDown());
	if (!bTargetStillRevivable)
	{
		// Target ya se recuperó (auto-recover) — cancelar canal limpiamente
		CancelReviveChannel();
		return;
	}

	const float DistSq = FVector::DistSquared(GetActorLocation(), TargetPawn->GetActorLocation());
	if (DistSq > ReviveRadiusCm * ReviveRadiusCm)
	{
		CancelReviveChannel();
		return;
	}

	// Advance channel
	ReviveChannelElapsed += ReviveChannelTickInterval;
	ReviveProgress = FMath::Clamp(ReviveChannelElapsed / ReviveDurationSeconds, 0.f, 1.f);

	// Check if complete
	if (ReviveChannelElapsed >= ReviveDurationSeconds)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Revive] %s successfully revived %s!"),
			*GetNameSafe(this), *GetNameSafe(TargetPC));

		// Complete the revive via GameMode
		if (ATN_RunGameMode* RunGM = GetWorld()->GetAuthGameMode<ATN_RunGameMode>())
		{
			RunGM->RevivePlayer(TargetPC);
		}

		// Clean up channel state
		CancelReviveChannel();

		// Cancel the reviver's emote (revive is done)
		if (IsLocallyControlled())
		{
			CancelEmote();
		}
		else
		{
			// Server-controlled pawn: force emote cancel
			ReplicatedEmoteIndex = -1;
			if (ActiveEmoteIndex >= 0 || bEmoteBlendingOut)
			{
				CancelEmote();
			}
		}
	}
}


// ─────────────────────────────────────────────────────────────────────────────
// DBNO / REVIVE AUDIO
// ─────────────────────────────────────────────────────────────────────────────

UAudioComponent* ATortugaCharacter::EnsureReviveAudioComponent()
{
	if (ReviveAudioComponent)
	{
		return ReviveAudioComponent;
	}

	ReviveAudioComponent = CreateProximityAudioComponent(TEXT("ReviveAudio"), ReviveAudioInnerRadius, ReviveAudioOuterRadius);
	if (!ReviveAudioComponent)
	{
		return nullptr;
	}

	// Auto-restart loop while revive is channeling.
	ReviveAudioComponent->OnAudioFinished.AddDynamic(this, &ATortugaCharacter::OnReviveAudioFinished);

	ReviveAudioComponent->RegisterComponent();
	return ReviveAudioComponent;
}

UAudioComponent* ATortugaCharacter::EnsureDBNOAudioComponent()
{
	if (DBNOAudioComponent)
	{
		return DBNOAudioComponent;
	}

	DBNOAudioComponent = NewObject<UAudioComponent>(this, TEXT("DBNOAudio"));
	if (!DBNOAudioComponent)
	{
		return nullptr;
	}

	DBNOAudioComponent->SetupAttachment(GetRootComponent());
	DBNOAudioComponent->bAutoActivate = false;
	DBNOAudioComponent->bAlwaysPlay = false;

	// Non-spatialized: only the local DBNO player hears the heartbeat.
	DBNOAudioComponent->bAllowSpatialization = false;
	DBNOAudioComponent->bIsUISound = true;  // Bypass distance culling — always audible for the local player.

	// Auto-restart loop while DBNO.
	DBNOAudioComponent->OnAudioFinished.AddDynamic(this, &ATortugaCharacter::OnDBNOAudioFinished);

	DBNOAudioComponent->RegisterComponent();
	return DBNOAudioComponent;
}

void ATortugaCharacter::PlayReviveChannelSound()
{
	if (!ReviveChannelSound)
	{
		return;
	}

	UAudioComponent* AC = EnsureReviveAudioComponent();
	if (!AC)
	{
		return;
	}

	AC->SetSound(ReviveChannelSound);
	AC->Play();
}

void ATortugaCharacter::StopReviveChannelSound()
{
	if (ReviveAudioComponent && ReviveAudioComponent->IsPlaying())
	{
		ReviveAudioComponent->Stop();
	}
}

void ATortugaCharacter::PlayReviveSuccessSound()
{
	if (!ReviveSuccessSound)
	{
		return;
	}

	// Success sound: spatialized one-shot (reuse the ReviveAudioComponent).
	// If channel sound was playing, Stop first so OnFinished doesn't re-loop.
	StopReviveChannelSound();

	UAudioComponent* AC = EnsureReviveAudioComponent();
	if (!AC)
	{
		return;
	}

	AC->SetSound(ReviveSuccessSound);
	AC->Play();
	// Note: OnReviveAudioFinished will NOT re-loop because bIsReviving == false at this point.
}

void ATortugaCharacter::MulticastPlayReviveSuccessSound_Implementation()
{
	PlayReviveSuccessSound();
}

void ATortugaCharacter::PlayDBNOHeartbeatSound()
{
	if (!DBNOHeartbeatSound)
	{
		return;
	}

	UAudioComponent* AC = EnsureDBNOAudioComponent();
	if (!AC)
	{
		return;
	}

	AC->SetSound(DBNOHeartbeatSound);
	AC->Play();
}

void ATortugaCharacter::StopDBNOHeartbeatSound()
{
	if (DBNOAudioComponent && DBNOAudioComponent->IsPlaying())
	{
		DBNOAudioComponent->Stop();
	}
}

void ATortugaCharacter::OnReviveAudioFinished()
{
	// Re-loop revive channel sound while actively channeling.
	if (bIsReviving && ReviveAudioComponent && ReviveAudioComponent->Sound)
	{
		ReviveAudioComponent->Play();
	}
	// If not reviving (e.g., success sound just finished), do nothing — one-shot.
}

void ATortugaCharacter::OnDBNOAudioFinished()
{
	// Re-loop heartbeat while knocked down (DBNO).
	if (bIsKnockedDown && IsLocallyControlled() && DBNOAudioComponent && DBNOAudioComponent->Sound)
	{
		DBNOAudioComponent->Play();
	}
}

