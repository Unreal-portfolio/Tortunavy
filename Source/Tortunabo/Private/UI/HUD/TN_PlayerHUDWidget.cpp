#include "UI/HUD/TN_PlayerHUDWidget.h"
#include "Player/TN_StaminaComponent.h"
#include "Core/TN_Log.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TortugaCharacter.h"
#include "Core/TN_InventoryTypes.h"
#include "Core/TN_CoopPlayerState.h"
#include "Components/ProgressBar.h"
#include "Components/Widget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "GameFramework/Pawn.h"

void UTN_PlayerHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (const APawn* Pawn = GetOwningPlayerPawn())
	{
		CachedStamina    = Pawn->FindComponentByClass<UTN_StaminaComponent>();
		CachedInventory  = Pawn->FindComponentByClass<UTN_InventoryComponent>();
	}

	// El selector siempre está sobre el slot equipado — mostrarlo desde el inicio.
	if (SlotEquippedSelector)
	{
		SlotEquippedSelector->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	RefreshStaminaWidgets();
	RefreshInventoryWidgets();
	BindToPlayerStateScore();
}

void UTN_PlayerHUDWidget::NativeDestruct()
{
	UnbindFromPlayerStateScore();
	Super::NativeDestruct();
}

void UTN_PlayerHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// PlayerState puede llegar tarde (post-travel). Reintentar bind si está pendiente.
	if (LastRaceScore < 0 && !BoundPlayerState.IsValid())
	{
		BindToPlayerStateScore();
	}

	// ── Stamina source: seguir el ViewTarget para que al espectear se muestre
	// la stamina del jugador observado, no la del pawn propio (muerto/nulo). ──────
	{
		UTN_StaminaComponent* DesiredStamina = nullptr;
		if (const APlayerController* PC = GetOwningPlayer())
		{
			// ViewTarget puede ser el pawn propio o el pawn espectado
			if (const APawn* ViewPawn = Cast<APawn>(PC->GetViewTarget()))
			{
				DesiredStamina = ViewPawn->FindComponentByClass<UTN_StaminaComponent>();
			}
		}
		// Fallback al pawn propio si el ViewTarget no tiene stamina
		if (!DesiredStamina)
		{
			if (const APawn* OwnPawn = GetOwningPlayerPawn())
			{
				DesiredStamina = OwnPawn->FindComponentByClass<UTN_StaminaComponent>();
			}
		}
		// Detectar cambio de fuente (propio → espectado o viceversa)
		if (DesiredStamina != CachedStamina.Get())
		{
			CachedStamina = DesiredStamina;
			LastStamina       = -1.f;
			LastWeightPenalty = -1.f;
			bLastExhausted    = false;
			UE_LOG(LogTortunabo, Verbose, TEXT("[PlayerHUD] Stamina source → %s"),
				*GetNameSafe(DesiredStamina ? DesiredStamina->GetOwner() : nullptr));
		}
	}

	// ── Inventario: como la estamina, el del ViewTarget (de espectador, la tortuga seguida; Docs/Fantasma_Espectador.md)
	// y, si no lleva, el del pawn propio (que cambia tras un viaje o una posesión). ──
	{
		UTN_InventoryComponent* DesiredInventory = nullptr;
		if (const APlayerController* PC = GetOwningPlayer())
		{
			if (const APawn* ViewPawn = Cast<APawn>(PC->GetViewTarget()))
			{
				DesiredInventory = ViewPawn->FindComponentByClass<UTN_InventoryComponent>();
			}
		}
		if (!DesiredInventory)
		{
			if (const APawn* Pawn = GetOwningPlayerPawn())
			{
				DesiredInventory = Pawn->FindComponentByClass<UTN_InventoryComponent>();
			}
		}
		if (DesiredInventory != CachedInventory.Get())
		{
			CachedInventory = DesiredInventory;
			LastEquippedId = NAME_None;
			LastStoredId   = NAME_None;
			// Otra tortuga (o ninguna): se repinta ya, aunque las dos no lleven nada.
			RefreshInventoryWidgets();
			UE_LOG(LogTortunabo, Verbose, TEXT("[PlayerHUD] Inventory source → %s"),
				*GetNameSafe(DesiredInventory ? DesiredInventory->GetOwner() : nullptr));
		}
	}

	// Throttle compartido: refrescar a ~20 fps
	RefreshAccumulator += InDeltaTime;
	if (RefreshAccumulator < kRefreshInterval)
	{
		return;
	}
	RefreshAccumulator = 0.f;

	// ── Stamina ──────────────────────────────────────────────────────────────
	if (CachedStamina.IsValid())
	{
		const float Current     = CachedStamina->GetCurrentStamina();
		const float WeightPen   = CachedStamina->GetWeightPenalty();
		const bool  bExhausted  = CachedStamina->IsExhausted();

		const bool bStaminaChanged = !FMath::IsNearlyEqual(Current, LastStamina, 0.5f) || bExhausted != bLastExhausted;
		const bool bWeightChanged  = !FMath::IsNearlyEqual(WeightPen, LastWeightPenalty, 0.5f);

		if (bStaminaChanged || bWeightChanged)
		{
			LastStamina       = Current;
			bLastExhausted    = bExhausted;
			LastWeightPenalty = WeightPen;
			RefreshStaminaWidgets();
		}
	}

	// ── Inventario ────────────────────────────────────────────────────────────
	if (CachedInventory.IsValid())
	{
		const FName EquippedId = CachedInventory->HasEquippedItem()
			? CachedInventory->GetEquippedItem().ItemId : NAME_None;
		const FName StoredId   = CachedInventory->HasStoredItem()
			? CachedInventory->GetStoredItem().ItemId   : NAME_None;

		if (EquippedId != LastEquippedId || StoredId != LastStoredId)
		{
			LastEquippedId = EquippedId;
			LastStoredId   = StoredId;
			RefreshInventoryWidgets();
		}
	}

	// ── DBNO / Revive ────────────────────────────────────────────────────────
	if (const APawn* Pawn = GetOwningPlayerPawn())
	{
		// Check DBNO via PlayerState
		if (const APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			if (const ATN_CoopPlayerState* PS = PC->GetPlayerState<ATN_CoopPlayerState>())
			{
				const bool bNowDBNO = PS->bIsDBNO;
				if (bNowDBNO != bLastDBNO)
				{
					bLastDBNO = bNowDBNO;
					OnDBNOStateChanged(bNowDBNO, PS->DBNOBleedoutTimeRemaining);
				}
				else if (bNowDBNO)
				{
					// Update bleedout remaining even if state didn't change
					OnDBNOStateChanged(true, PS->DBNOBleedoutTimeRemaining);
				}
			}
		}

		// Check revive channel via TortugaCharacter
		if (const ATortugaCharacter* TChar = Cast<ATortugaCharacter>(Pawn))
		{
			const bool bNowReviving = TChar->bIsReviving;
			const float NowProgress = TChar->ReviveProgress;
			if (bNowReviving != bLastReviving || !FMath::IsNearlyEqual(NowProgress, LastReviveProgress, 0.01f))
			{
				bLastReviving = bNowReviving;
				LastReviveProgress = NowProgress;
				OnReviveProgressUpdated(NowProgress, bNowReviving);
			}
		}
	}
}

// ── Stamina ───────────────────────────────────────────────────────────────────

void UTN_PlayerHUDWidget::RefreshStaminaWidgets()
{
	if (!CachedStamina.IsValid())
	{
		if (StaminaBar)      { StaminaBar->SetVisibility(ESlateVisibility::Hidden); }
		if (WeightPenaltyBar){ WeightPenaltyBar->SetVisibility(ESlateVisibility::Hidden); }
		if (ExhaustedRoot)   { ExhaustedRoot->SetVisibility(ESlateVisibility::Hidden); }
		if (StaminaText)     { StaminaText->SetVisibility(ESlateVisibility::Hidden); }
		return;
	}

	const float Current    = CachedStamina->GetCurrentStamina();
	const float MaxStam    = CachedStamina->GetMaxStamina();
	const float EffMax     = CachedStamina->GetEffectiveMaxStamina();
	const float WeightPen  = CachedStamina->GetWeightPenalty();
	const bool  bExhaust   = CachedStamina->IsExhausted();

	// Ratio de stamina actual vs el máximo BASE (para que la barra de stamina
	// y la de peso compartan la misma escala visual).
	const float StaminaRatio = (MaxStam > 0.f) ? FMath::Clamp(Current / MaxStam, 0.f, 1.f) : 0.f;
	// Ratio que ocupa la penalización de peso (zona oscura a la derecha).
	const float WeightRatio  = (MaxStam > 0.f) ? FMath::Clamp(WeightPen / MaxStam, 0.f, 1.f) : 0.f;

	if (StaminaBar)
	{
		StaminaBar->SetVisibility(ESlateVisibility::HitTestInvisible);
		StaminaBar->SetPercent(StaminaRatio);
	}

	// WeightPenaltyBar: superponer sobre StaminaBar, alineada a la derecha.
	// En el Widget Designer: mismo tamaño que StaminaBar, mismo anchor,
	// Fill Direction = Right to Left, color distinto (ej. marrón oscuro #5C3317).
	if (WeightPenaltyBar)
	{
		WeightPenaltyBar->SetVisibility(ESlateVisibility::HitTestInvisible);
		WeightPenaltyBar->SetPercent(WeightRatio);
	}

	if (ExhaustedRoot)
	{
		ExhaustedRoot->SetVisibility(bExhaust
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Hidden);
	}

	// La barra no lleva número: basta con ver cuánto queda.
	if (StaminaText)
	{
		StaminaText->SetVisibility(ESlateVisibility::Collapsed);
	}

	OnStaminaUpdated(Current, MaxStam, bExhaust);
	OnWeightUpdated(WeightPen, MaxStam, EffMax);
}

// ── Inventory ─────────────────────────────────────────────────────────────────

void UTN_PlayerHUDWidget::RefreshInventoryWidgets()
{
	const bool bHasEquipped = CachedInventory.IsValid() && CachedInventory->HasEquippedItem();
	const bool bHasStored   = CachedInventory.IsValid() && CachedInventory->HasStoredItem();

	UTexture2D* EquippedIcon = bHasEquipped ? CachedInventory->GetEquippedItem().ItemIcon.Get() : nullptr;
	UTexture2D* StoredIcon   = bHasStored   ? CachedInventory->GetStoredItem().ItemIcon.Get()   : nullptr;

	// Slot equipado
	if (SlotEquippedImage)
	{
		if (EquippedIcon)
		{
			SlotEquippedImage->SetBrushFromTexture(EquippedIcon, /*bMatchSize=*/false);
			SlotEquippedImage->SetColorAndOpacity(FLinearColor::White);
		}
		else
		{
			// Sin ítem: mostrar el slot vacío (imagen transparente)
			SlotEquippedImage->SetColorAndOpacity(FLinearColor::Transparent);
		}
	}

	// Slot guardado
	if (SlotStoredImage)
	{
		if (StoredIcon)
		{
			SlotStoredImage->SetBrushFromTexture(StoredIcon, /*bMatchSize=*/false);
			SlotStoredImage->SetColorAndOpacity(FLinearColor::White);
		}
		else
		{
			SlotStoredImage->SetColorAndOpacity(FLinearColor::Transparent);
		}
	}

	// Selector: siempre sobre el slot equipado
	if (SlotEquippedSelector)
	{
		SlotEquippedSelector->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	OnInventoryUpdated(EquippedIcon, bHasEquipped, StoredIcon, bHasStored);
}

// ── Score live ───────────────────────────────────────────────────────────────

void UTN_PlayerHUDWidget::BindToPlayerStateScore()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC) { return; }
	ATN_CoopPlayerState* PS = PC->GetPlayerState<ATN_CoopPlayerState>();
	if (!PS)
	{
		// Race condition: PS aún no asignado al PC en BeginPlay del widget.
		// Reintentar en el siguiente tick. Como el HUD ya tiene NativeTick activo,
		// chequea allí cada vez que LastRaceScore < 0 (sentinel).
		LastRaceScore = -1;
		return;
	}
	BoundPlayerState = PS;
	PS->OnRaceScoreChanged.AddDynamic(this, &UTN_PlayerHUDWidget::HandleRaceScoreChanged);
	LastRaceScore = PS->RaceScore;
	HandleRaceScoreChanged(PS->RaceScore);
}

void UTN_PlayerHUDWidget::UnbindFromPlayerStateScore()
{
	if (ATN_CoopPlayerState* PS = BoundPlayerState.Get())
	{
		PS->OnRaceScoreChanged.RemoveDynamic(this, &UTN_PlayerHUDWidget::HandleRaceScoreChanged);
	}
	BoundPlayerState.Reset();
}

void UTN_PlayerHUDWidget::HandleRaceScoreChanged(int32 NewScore)
{
	const int32 Delta = NewScore - LastRaceScore;
	LastRaceScore = NewScore;

	if (ScoreText)
	{
		ScoreText->SetText(FText::AsNumber(NewScore));
	}
	OnRaceScoreUpdated(NewScore, FMath::Max(0, Delta));
}
