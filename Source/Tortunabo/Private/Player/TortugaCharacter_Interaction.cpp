// ─────────────────────────────────────────────────────────────────────────────
// TortugaCharacter — Interacción y uso de ítems.
//
// Definiciones extraídas de TortugaCharacter.cpp para mejorar la legibilidad:
// escaneo de interactuables (UpdateFocusedInteractable), Server RPCs de
// interacción/uso/drop de ítems y helpers de spawn. Misma clase ATortugaCharacter
// en otra unidad de traducción: sin cambios de lógica ni de replicación.
// ─────────────────────────────────────────────────────────────────────────────

#include "Player/TortugaCharacter.h"
#include "Camera/CameraComponent.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_ThrowArc.h"
#include "Core/TN_Log.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TN_StaminaComponent.h"
#include "Player/TN_TurtleAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "World/TN_InteractableBase.h"
#include "World/TN_PickupInteractableBase.h"
#include "World/TN_ThrowableItemActor.h"
#include "World/TN_ConchPickup.h"
#include "World/TN_InkProjectile.h"
#include "Game/TN_TctItems.h"
#include "Game/TN_CoopItems.h"
#include "World/Beach/TN_RaceItems.h"
#include "Game/TN_BeachRaceGameState.h"
#include "Core/TN_CoopPlayerState.h"
#include "Game/TN_RunGameMode.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "Net/UnrealNetwork.h"
#include "Engine/OverlapResult.h"
#include "DrawDebugHelpers.h"

// El CVar de debug se define (con linkage externo) en TortugaCharacter.cpp
extern TAutoConsoleVariable<int32> CVarDebugInteraction;

void ATortugaCharacter::UpdateFocusedInteractable()
{
	// Si el pawn ya no tiene controlador (terminó la carrera, murió, espectador)
	// limpiar el foco y no hacer overlap queries sobre el pawn oculto.
	if (!GetWorld() || !GetController()) { FocusedInteractable = nullptr; return; }

	const bool bDebug = CVarDebugInteraction.GetValueOnGameThread() != 0;

	// ── Detección por proximidad: esfera alrededor del personaje ─────────────
	// No usa raycast ni cámara — el jugador solo tiene que acercarse al objeto.
	// Busca todos los actores WorldDynamic en el radio y escoge el más cercano
	// que sea un ATN_InteractableBase válido.
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TN_InteractionProximity), false);
	QueryParams.AddIgnoredActor(this);

	GetWorld()->OverlapMultiByObjectType(
		Overlaps,
		GetActorLocation(),
		FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldDynamic),
		FCollisionShape::MakeSphere(MaxInteractionDistance),
		QueryParams);

	ATN_InteractableBase* BestCandidate = nullptr;
	float BestDistSq = FLT_MAX;

	for (const FOverlapResult& Result : Overlaps)
	{
		ATN_InteractableBase* Interactable = Cast<ATN_InteractableBase>(Result.GetActor());
		if (!Interactable || !Interactable->CanInteract(this)) { continue; }

		// Misma medida que la validación del servidor (ServerTryInteract): el aviso solo sale cuando pulsar funciona.
		// El solapamiento encuentra cualquier colisión del actor (paredes del probador, mostrador), que puede estar
		// mucho más cerca que su punto de interacción.
		const float DistSq = FVector::DistSquared(GetActorLocation(), Interactable->GetInteractionPointFor(this));
		if (DistSq > FMath::Square(MaxInteractionDistance)) { continue; }
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestCandidate = Interactable;
		}
	}

	if (FocusedInteractable.Get() != BestCandidate)
	{
		FocusedInteractable = BestCandidate;
		if (bDebug)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[Interact:DEBUG] Focus → %s  (dist=%.0f)"),
				BestCandidate ? *BestCandidate->GetName() : TEXT("(none)"),
				BestCandidate ? FVector::Dist(GetActorLocation(), BestCandidate->GetInteractionPointFor(this)) : 0.f);
		}
	}

	if (bDebug)
	{
		// Mostrar la esfera de detección
		DrawDebugSphere(GetWorld(), GetActorLocation(), MaxInteractionDistance,
			16, BestCandidate ? FColor::Green : FColor::Silver,
			false, InteractionScanInterval * 1.5f, 0, 0.8f);

		if (BestCandidate)
		{
			DrawDebugLine(GetWorld(), GetActorLocation(), BestCandidate->GetInteractionPointFor(this),
				FColor::Cyan, false, InteractionScanInterval * 1.5f, 0, 2.f);
		}
	}
}

FVector ATortugaCharacter::FindGroundBelow(const FVector& WorldLocation) const
{
	if (!GetWorld()) { return WorldLocation; }

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TN_GroundTrace), false, this);
	const FVector Start = WorldLocation + FVector(0.f, 0.f, 30.f);
	const FVector End   = WorldLocation - FVector(0.f, 0.f, 1500.f);

	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		// Offset por la mitad de la extensión vertical del mesh para que el
		// borde inferior del objeto quede apoyado en el suelo (no el centro).
		// El valor por defecto de 15cm cubre la mayoría de items pequeños.
		return Hit.ImpactPoint + FVector(0.f, 0.f, 15.f);
	}
	return WorldLocation;
}


void ATortugaCharacter::ServerTryInteract_Implementation(ATN_InteractableBase* Interactable)
{
	const bool bDebug = CVarDebugInteraction.GetValueOnGameThread() != 0;

	if (!Interactable)
	{
		if (bDebug) { UE_LOG(LogTortunabo, Warning, TEXT("[Interact:SERVER] Interactable is NULL — client sent invalid reference")); }
		return;
	}

	if (!Interactable->CanInteract(this))
	{
		// Solo si lo que impide cogerlo es el inventario lleno se usa/lanza el ítem de la mano, sin desperdiciar el input.
		// Cogido por otra hace un instante (bTaken aún no había llegado a este cliente), desactivado o sin ítem: no se
		// gasta nada (#570).
		const ATN_PickupInteractableBase* Pickup = Cast<ATN_PickupInteractableBase>(Interactable);
		if (Pickup && Pickup->IsBlockedOnlyByFullInventory(this)
			&& InventoryComponent && InventoryComponent->HasEquippedItem())
		{
			if (bIsKnockedDown || bIsDead)
			{
				return;
			}
			if (bDebug)
			{
				UE_LOG(LogTortunabo, Log, TEXT("[Interact:SERVER] Pickup '%s' no recogible + inventario lleno → usando ítem equipado."),
					*Interactable->GetName());
			}
			ServerUseEquippedItem_Implementation();
		}
		else if (bDebug)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Interact:SERVER] CanInteract=FALSE para '%s' — tomado, desactivado o sin espacio."), *Interactable->GetName());
		}
		return;
	}

	const float MaxDistance = FMath::Max(MaxInteractionDistance, Interactable->GetInteractionDistance());
	float PingDistanceAllowance = 0.f;
	if (const APlayerState* PS = GetPlayerState())
	{
		PingDistanceAllowance = FMath::Clamp(PS->ExactPing * 0.25f, 0.f, MaxLagCompensationDistance);
	}

	const float TotalAllowed = MaxDistance + 100.f + PingDistanceAllowance;
	const float ActualDist = FVector::Dist(GetActorLocation(), Interactable->GetInteractionPointFor(this));

	if (ActualDist > TotalAllowed)
	{
		if (bDebug) { UE_LOG(LogTortunabo, Warning, TEXT("[Interact:SERVER] TOO FAR — dist=%.1f  allowed=%.1f  (MaxDist=%.1f + 100 + ping=%.1f)"), ActualDist, TotalAllowed, MaxDistance, PingDistanceAllowance); }
		return;
	}

	if (bDebug) { UE_LOG(LogTortunabo, Log, TEXT("[Interact:SERVER] ✓ Calling Interact on '%s' — dist=%.1f"), *Interactable->GetName(), ActualDist); }

	Interactable->Interact(this);
}

// ── Interacción de mantener la tecla (rebuscar un decorado, ATN_ProcSearchSpot) ──────────────────────────────────
// El cliente solo avisa de que empieza y de que suelta; el tiempo lo cuenta el interactuable en el servidor, que
// también vigila que la tortuga siga cerca y en condiciones mientras dura.

void ATortugaCharacter::ReleaseInteract()
{
	if (ATN_InteractableBase* Held = HoldInteractable.Get())
	{
		ServerEndHoldInteract(Held);
	}
	HoldInteractable.Reset();
}

void ATortugaCharacter::ServerBeginHoldInteract_Implementation(ATN_InteractableBase* Interactable)
{
	const bool bDebug = CVarDebugInteraction.GetValueOnGameThread() != 0;
	if (!Interactable || bIsKnockedDown || bIsDead || IsInShell() || Interactable->GetHoldDuration() <= 0.f)
	{
		return;
	}
	if (!Interactable->CanInteract(this))
	{
		if (bDebug) { UE_LOG(LogTortunabo, Warning, TEXT("[Interact:SERVER] Mantener: CanInteract=FALSE para '%s' (ya buscado u ocupado)."), *Interactable->GetName()); }
		return;
	}

	// Misma medida y holgura que ServerTryInteract.
	const float MaxDistance = FMath::Max(MaxInteractionDistance, Interactable->GetInteractionDistance());
	const APlayerState* HoldPS = GetPlayerState();
	const float PingDistanceAllowance = HoldPS ? FMath::Clamp(HoldPS->ExactPing * 0.25f, 0.f, MaxLagCompensationDistance) : 0.f;
	const float ActualDist = FVector::Dist(GetActorLocation(), Interactable->GetInteractionPointFor(this));
	if (ActualDist > MaxDistance + 100.f + PingDistanceAllowance)
	{
		if (bDebug) { UE_LOG(LogTortunabo, Warning, TEXT("[Interact:SERVER] Mantener: demasiado lejos de '%s' (%.0f cm)."), *Interactable->GetName(), ActualDist); }
		return;
	}

	if (bDebug) { UE_LOG(LogTortunabo, Log, TEXT("[Interact:SERVER] ✓ Empieza a mantener '%s' — dist=%.1f"), *Interactable->GetName(), ActualDist); }
	Interactable->BeginHoldInteract(this);
}

void ATortugaCharacter::ServerEndHoldInteract_Implementation(ATN_InteractableBase* Interactable)
{
	if (Interactable)
	{
		Interactable->EndHoldInteract(this);
	}
}

void ATortugaCharacter::ServerUseEquippedItem_Implementation()
{
	if (bIsKnockedDown || bIsDead)
	{
		return;
	}
	if (!InventoryComponent || !StaminaComponent)
	{
		return;
	}

	if (!InventoryComponent->HasEquippedItem())
	{
		return;
	}

	const FTN_InventoryItem EquippedItem = InventoryComponent->GetEquippedItem();
	if (!EquippedItem.IsValid())
	{
		return;
	}

	// En la carrera, los objetos de DT_Items siguen la regla de los de carrera (que ya la aplica TNRaceItems::ServerUse
	// con su «nop»): nada desde el pelícano, el pico de la gaviota, el gusano, el aturdimiento o con la carrera parada.
	if (EquippedItem.UseType != ETN_ItemUseType::RaceItem && GetWorld()
		&& GetWorld()->GetGameState<ATN_BeachRaceGameState>() && !TNRaceItems::CanUseNow(this))
	{
		return;
	}

	if (EquippedItem.UseType == ETN_ItemUseType::SelfStaminaBoost)
	{
		HandleUseSelfStaminaBoost(EquippedItem);
		return;
	}

	// #3 — Barrita Energética: recuperación instantánea al máximo, sin boost de duración ni penalización.
	if (EquippedItem.UseType == ETN_ItemUseType::SelfStaminaFull)
	{
		HandleUseSelfStaminaFull(EquippedItem);
		return;
	}

	if (EquippedItem.UseType == ETN_ItemUseType::BigHead)
	{
		HandleUseBigHead(EquippedItem);
		return;
	}

	if ((EquippedItem.UseType == ETN_ItemUseType::Throwable)
		&& EquippedItem.ThrowableData.ActorClass)
	{
		HandleUseThrowable(EquippedItem);
		return;
	}

	// ── #22 Concha trampa ────────────────────────────────────────────────────────
	if ((EquippedItem.UseType == ETN_ItemUseType::Conch)
		&& EquippedItem.ConchData.ActorClass)
	{
		HandleUseConch(EquippedItem);
		return;
	}

	// ── #13 Tinta de calamar ─────────────────────────────────────────────────────
	if ((EquippedItem.UseType == ETN_ItemUseType::InkThrower)
		&& EquippedItem.InkData.ProjectileClass)
	{
		HandleUseInkThrower(EquippedItem);
		return;
	}

	// ── #5 Tótem — uso manual: revivir a un jugador muerto aleatorio ──────────
	if (EquippedItem.UseType == ETN_ItemUseType::Totem)
	{
		HandleUseTotem(EquippedItem);
		return;
	}

	// ── Objetos de la carrera de la playa (turbo, pelícano taxi, protector solar...): World/Beach/TN_RaceItems.h ──
	if (EquippedItem.UseType == ETN_ItemUseType::RaceItem)
	{
		TNRaceItems::ServerUse(this, EquippedItem);
		return;
	}

	// ── Objetos de combate de Todos contra Todos (pistola de noqueo, garfio, pala...): Game/TN_TctItems.h ──
	if (EquippedItem.UseType == ETN_ItemUseType::TctItem)
	{
		TNTctItems::ServerUse(this, EquippedItem);
		return;
	}

	// ── Objetos del cooperativo (charco de pesca, pez globo, arpón...): Game/TN_CoopItems.h ──
	if (EquippedItem.UseType == ETN_ItemUseType::CoopItem)
	{
		TNCoopItems::ServerUse(this, EquippedItem);
		return;
	}
}

void ATortugaCharacter::HandleUseSelfStaminaBoost(const FTN_InventoryItem& EquippedItem)
{
	FTN_InventoryItem ConsumedItem;
	if (!InventoryComponent->TryConsumeEquippedItem(ConsumedItem))
	{
		return;
	}

	// Aplicar penalización post-boost específica del ítem (sobreescribe el valor global del componente).
	StaminaComponent->SetPostBoostExhaustionSeconds(EquippedItem.StaminaBoostData.PostBoostExhaustionSeconds);
	GrantInfiniteStamina(EquippedItem.StaminaBoostData.DurationSeconds);
}

void ATortugaCharacter::HandleUseSelfStaminaFull(const FTN_InventoryItem& EquippedItem)
{
	FTN_InventoryItem ConsumedItem;
	if (!InventoryComponent->TryConsumeEquippedItem(ConsumedItem))
	{
		return;
	}

	// Resetear penalización post-boost heredada antes de restaurar,
	// para que no se aplique agotamiento si el jugador usó un boost antes.
	StaminaComponent->SetPostBoostExhaustionSeconds(0.f);
	StaminaComponent->RestoreStaminaToFull();
}

void ATortugaCharacter::HandleUseBigHead(const FTN_InventoryItem& EquippedItem)
{
	FTN_InventoryItem ConsumedItem;
	if (!InventoryComponent->TryConsumeEquippedItem(ConsumedItem))
	{
		return;
	}

	bBigHead = true;
	ApplyBigHeadVisual(true);

	// Timer para restablecer al tamaño original + efecto de mareo (#2).
	// CreateUObject en lugar de lambda: el binding es weak, así que si el objeto
	// ya se destruyó el timer no ejecuta nada (no depende de un clear en EndPlay).
	FTimerDelegate BigHeadDel = FTimerDelegate::CreateUObject(this, &ATortugaCharacter::RemoveBigHeadEffect);
	GetWorldTimerManager().SetTimer(BigHeadTimerHandle, BigHeadDel, BigHeadDurationSeconds, false);
}

FVector ATortugaCharacter::GetThrowDirection(const FRotator& AimRotation) const
{
	// La cámara mira con el giro del mando más su propio cabeceo (CameraAimPitchOffset, hacia abajo). Con ella a nivel, el
	// lanzamiento sale a ThrowBasePitchDeg; mirar arriba o abajo solo lo cambia en parte, para que no salga por las nubes.
	const float CameraPitch = FRotator::NormalizeAxis(AimRotation.Pitch) + CameraAimPitchOffset;
	const float MinPitch = FMath::Min(ThrowMinPitchDeg, ThrowMaxPitchDeg);
	const float Pitch = FMath::Clamp(ThrowBasePitchDeg + ThrowAimPitchFactor * CameraPitch, MinPitch, ThrowMaxPitchDeg);
	return FRotator(Pitch, AimRotation.Yaw, 0.f).Vector();
}

FRotator ATortugaCharacter::GetTurtleAimRotation() const
{
	return Controller ? Controller->GetControlRotation() : GetActorRotation();
}

bool ATortugaCharacter::UsesCameraThrowAim() const
{
	return FollowCamera != nullptr;
}

bool ATortugaCharacter::GetCrosshairPoint(FVector& OutPoint) const
{
	const UWorld* World = GetWorld();
	if (!UsesCameraThrowAim() || !World)
	{
		return false;
	}

	// Rayo por el centro de la pantalla: sale de la cámara hacia delante. Su primer choque (menos la propia tortuga y lo que
	// lleva) es el punto de mira; sin choque, un punto lejano en el mismo rayo.
	constexpr float AimRange = 8000.f;
	const FVector CamLoc = FollowCamera->GetComponentLocation();
	const FVector CamDir = FollowCamera->GetForwardVector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ThrowCrosshair), false, this);
	if (const UTN_CarryComponent* Carry = CarryComponent)
	{
		if (const AActor* Carried = Carry->GetCarriedTurtle())
		{
			Params.AddIgnoredActor(Carried);
		}
	}
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, CamLoc, CamLoc + CamDir * AimRange, ECC_Visibility, Params);
	OutPoint = bHit ? FVector(Hit.ImpactPoint) : CamLoc + CamDir * AimRange;
	return true;
}

FVector ATortugaCharacter::GetThrowDirectionToCrosshair(const FVector& Origin, const FRotator& AimRotation, float Speed, float GravityCmS2, float LinearDamping) const
{
	const UWorld* World = GetWorld();
	FVector Target;
	if (!World || Speed < 1.f || !GetCrosshairPoint(Target))
	{
		return GetThrowDirection(AimRotation);
	}

	// Tiro parabólico (la gravedad del mundo, con ProjectileGravityScale 1): el ángulo bajo que llega justo al punto.
	const FVector Delta = Target - Origin;
	const FVector Flat(Delta.X, Delta.Y, 0.0);
	const double D = Flat.Size();
	if (D < 1.0)
	{
		return Delta.GetSafeNormal();
	}
	const double G = GravityCmS2 > 1.f ? static_cast<double>(GravityCmS2) : FMath::Max(1.0, -static_cast<double>(World->GetGravityZ()));
	// Sin alcance (punto demasiado lejos): el ángulo de máximo alcance.
	const double Theta = TNThrowArc::LaunchPitch(D, Delta.Z, static_cast<double>(Speed), G, static_cast<double>(LinearDamping));
	return (Flat / D * FMath::Cos(Theta) + FVector(0.0, 0.0, FMath::Sin(Theta))).GetSafeNormal();
}

void ATortugaCharacter::MulticastItemThrowAnim_Implementation()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (UTN_TurtleAnimInstance* TurtleAnim = SkelMesh ? Cast<UTN_TurtleAnimInstance>(SkelMesh->GetAnimInstance()) : nullptr)
	{
		TurtleAnim->PlayThrow(false);
	}
}

void ATortugaCharacter::HandleUseThrowable(const FTN_InventoryItem& EquippedItem)
{
	const FVector SpawnLocation = GetItemSpawnLocation();

	// ── Dirección de lanzamiento: hacia donde mira la cámara, con el arco bajo de todos los lanzamientos ──
	// y al punto del centro de la pantalla.
	const float ThrowSpeedCmS = FMath::Max(EquippedItem.ThrowableData.ThrowSpeed, 0.0f);
	const FVector ArcedDirection = GetThrowDirectionToCrosshair(SpawnLocation, GetTurtleAimRotation(), ThrowSpeedCmS);

	const FVector LaunchVelocity = ArcedDirection * ThrowSpeedCmS;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	FTN_InventoryItem ConsumedItem;
	if (!InventoryComponent->TryConsumeEquippedItem(ConsumedItem))
	{
		return;
	}

	if (ATN_ThrowableItemActor* ThrowableActor = GetWorld()->SpawnActor<ATN_ThrowableItemActor>(EquippedItem.ThrowableData.ActorClass, SpawnLocation, ArcedDirection.Rotation(), SpawnParams))
	{
		// SourceItem lleva PickupActorClass para que el throwable sepa
		// qué pickup spawnear cuando aterrice o impacte (se convierte en recogible)
		ThrowableActor->SetSourceItem(ConsumedItem);
		ThrowableActor->InitializeThrow(SpawnLocation, LaunchVelocity);

		if (ThrowSound) { MulticastPlaySfx(ThrowSound); }
		MulticastItemThrowAnim();
	}
	else
	{
		InventoryComponent->TryAddOrReplaceEquipped(ConsumedItem, true);
	}
}

void ATortugaCharacter::HandleUseConch(const FTN_InventoryItem& EquippedItem)
{
	FTN_InventoryItem ConsumedItem;
	if (!InventoryComponent->TryConsumeEquippedItem(ConsumedItem)) { return; }

	// Colocar la concha en el suelo justo debajo del jugador
	const FVector PlaceLoc = FindGroundBelow(GetActorLocation());

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner     = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (ATN_ConchPickup* Conch = GetWorld()->SpawnActor<ATN_ConchPickup>(
		ConsumedItem.ConchData.ActorClass, PlaceLoc, FRotator::ZeroRotator, SpawnParams))
	{
		// Al gastarse vuelve al suelo como pickup de este mismo ítem (#568).
		Conch->SetRecycledItem(ConsumedItem);
		Conch->PlaceAsTrap(PlaceLoc);
	}
}

void ATortugaCharacter::HandleUseInkThrower(const FTN_InventoryItem& EquippedItem)
{
	FTN_InventoryItem ConsumedItem;
	if (!InventoryComponent->TryConsumeEquippedItem(ConsumedItem)) { return; }

	// Con el mismo arco bajo que el resto de lanzamientos (la tinta también cae con la gravedad).
	const FVector Origin    = GetItemSpawnLocation();
	const FVector Direction = GetThrowDirectionToCrosshair(Origin, GetTurtleAimRotation(), ConsumedItem.InkData.ThrowSpeed);
	ATN_InkProjectile::Spawn(this, ConsumedItem.InkData.ProjectileClass,
		Origin, Direction, ConsumedItem.InkData.ThrowSpeed);
	MulticastItemThrowAnim();
}

void ATortugaCharacter::HandleUseTotem(const FTN_InventoryItem& EquippedItem)
{
	// Buscar jugadores eliminados
	TArray<APlayerController*> DeadPlayers;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || PC == GetController()) { continue; }
		ATN_CoopPlayerState* PS = PC->GetPlayerState<ATN_CoopPlayerState>();
		if (PS && PS->bIsEliminated)
		{
			DeadPlayers.Add(PC);
		}
	}

	if (DeadPlayers.Num() == 0)
	{
		// Nadie a quien revivir — no consumir el ítem
		return;
	}

	FTN_InventoryItem ConsumedItem;
	if (!InventoryComponent->TryConsumeEquippedItem(ConsumedItem)) { return; }

	// Seleccionar y revivir
	const int32 Idx = FMath::RandRange(0, DeadPlayers.Num() - 1);
	APlayerController* TargetPC = DeadPlayers[Idx];

	if (ATN_RunGameMode* GM = GetWorld()->GetAuthGameMode<ATN_RunGameMode>())
	{
		GM->RevivePlayer(TargetPC);

		APawn* RevivedPawn = TargetPC->GetPawn();
		if (RevivedPawn)
		{
			const FVector RightOffset = GetActorRightVector() * 150.f;
			RevivedPawn->TeleportTo(GetActorLocation() + RightOffset, GetActorRotation());
		}
	}
}

void ATortugaCharacter::ServerDropEquippedItem_Implementation()
{
	if (bIsKnockedDown || bIsDead) { return; }
	// En la carrera no se suelta nada en el aire (pelícano, gaviota...): el pickup quedaría fuera del alcance.
	if (GetWorld() && GetWorld()->GetGameState<ATN_BeachRaceGameState>() && !TNRaceItems::CanUseNow(this)) { return; }
	if (!InventoryComponent || !InventoryComponent->HasEquippedItem()) { return; }

	// Validar ANTES de consumir. La versión anterior extraía el ítem del inventario
	// primero y solo después comprobaba PickupActorClass / el spawn: si la clase era
	// null o SpawnActor fallaba, el ítem quedaba consumido pero sin pickup en el mundo
	// (item lost). Ahora spawnamos primero y solo consumimos si el pickup existe.
	const FTN_InventoryItem& Equipped = InventoryComponent->GetEquippedItem();
	if (!Equipped.IsValid() || !Equipped.PickupActorClass)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// Siempre spawnear en el suelo aunque el personaje esté en el aire
	const FVector DropPoint = FindGroundBelow(GetItemSpawnLocation());

	ATN_PickupInteractableBase* PickupActor = GetWorld()->SpawnActor<ATN_PickupInteractableBase>(
		Equipped.PickupActorClass, DropPoint, FRotator::ZeroRotator, SpawnParams);
	if (!PickupActor)
	{
		return; // Spawn falló → NO consumir el ítem (evita pérdida)
	}

	FTN_InventoryItem DroppedItem;
	if (!InventoryComponent->TryExtractEquippedItem(DroppedItem))
	{
		PickupActor->Destroy();
		return;
	}

	PickupActor->InitializeFromInventoryItem(DroppedItem);
}

FVector ATortugaCharacter::GetItemSpawnLocation() const
{
	// Delante y algo por encima del centro de la cápsula: así la bola, la tinta y lo que se suelta no nacen en el cuerpo.
	constexpr float ForwardCm = 120.f;
	constexpr float UpCm = 40.f;
	// Entre el centro y ese punto no puede haber nada (#571): pegada a un muro, puerta o valla de menos de ~80 cm el punto
	// caía al otro lado y la bola o la tinta salían desde allí. Se barre una esfera del tamaño de una bola y, si choca, el
	// punto se queda en este lado, con margen.
	constexpr float SweepRadiusCm = 20.f;
	constexpr float WallMarginCm = 25.f;

	const FVector Start = GetActorLocation();
	const FVector Desired = Start + GetActorForwardVector() * ForwardCm + FVector(0.f, 0.f, UpCm);
	const UWorld* World = GetWorld();
	if (!World)
	{
		return Desired;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(TN_ItemSpawnClearance), false, this);
	if (const AActor* Carried = CarryComponent ? CarryComponent->GetCarriedTurtle() : nullptr)
	{
		Params.AddIgnoredActor(Carried);
	}
	// Ni el vehículo en el que va ni lo que lleva enganchado (casco, objetos a la espalda).
	if (const AActor* Mount = GetAttachParentActor())
	{
		Params.AddIgnoredActor(Mount);
	}
	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	Params.AddIgnoredActors(AttachedActors);
	// Solo cuenta lo que hace de pared (el mismo canal que FindGroundBelow): ni otras tortugas ni cuerpos sueltos.
	FCollisionResponseParams Response;
	Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	Response.CollisionResponse.SetResponse(ECC_PhysicsBody, ECR_Ignore);

	FHitResult Hit;
	if (!World->SweepSingleByChannel(Hit, Start, Desired, FQuat::Identity, ECC_WorldStatic,
		FCollisionShape::MakeSphere(SweepRadiusCm), Params, Response))
	{
		return Desired;
	}
	if (Hit.bStartPenetrating)
	{
		return Start;
	}
	const FVector Dir = (Desired - Start).GetSafeNormal();
	const double Clear = FMath::Max(0.0, FVector::Dist(Start, Hit.Location) - WallMarginCm);
	return Start + Dir * Clear;
}

FVector ATortugaCharacter::GetItemForwardDirection() const
{
	if (Controller)
	{
		const FRotator ViewRotation = Controller->GetControlRotation();
		return ViewRotation.Vector().GetSafeNormal();
	}

	return GetActorForwardVector();
}

