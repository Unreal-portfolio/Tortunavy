#include "UI/HUD/TN_PlayerHUDWidget.h"
#include "Core/TN_Log.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TortugaCharacter.h"
#include "Core/TN_InventoryTypes.h"
#include "Core/TN_CoopPlayerState.h"
#include "Components/Widget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "GameFramework/Pawn.h"

void UTN_PlayerHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (const APawn* Pawn = GetOwningPlayerPawn())
	{
		CachedInventory  = Pawn->FindComponentByClass<UTN_InventoryComponent>();
	}

	// El selector siempre está sobre el slot equipado — mostrarlo desde el inicio.
	if (SlotEquippedSelector)
	{
		SlotEquippedSelector->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

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

	// ── Inventario: el del ViewTarget (de espectador, la tortuga seguida; Docs/Fantasma_Espectador.md)
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
