#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TN_PlayerHUDWidget.generated.h"

class UProgressBar;
class UWidget;
class UTextBlock;
class UImage;
class UTN_StaminaComponent;
class UTN_InventoryComponent;
class UTexture2D;
class ATN_CoopPlayerState;

/**
 * @brief HUD principal con barra de stamina, slots de inventario, RaceScore y hooks DBNO/Revive.
 *
 * Widgets opcionales — nómbralos EXACTAMENTE igual en el BP Designer:
 *
 *   STAMINA:
 *   - StaminaBar       (UProgressBar)     → relleno proporcional a stamina actual/max
 *   - WeightPenaltyBar (UProgressBar)     → zona bloqueada por peso (superponer sobre StaminaBar,
 *                                           alineada a la derecha, color distinto ej. marrón oscuro)
 *                                           Percent = WeightPenalty / MaxStamina
 *   - ExhaustedRoot    (cualquier widget) → visible solo durante penalización por agotamiento
 *   - StaminaText      (UTextBlock)       → opcional, muestra "120 / 200"
 *
 *   INVENTARIO:
 *   - SlotEquippedImage    (UImage)           → icono del ítem equipado (slot activo)
 *   - SlotStoredImage      (UImage)           → icono del ítem guardado (slot pasivo)
 *   - SlotEquippedSelector (cualquier widget) → borde/resaltado del slot activo (siempre visible)
 */
UCLASS()
class TORTUNABO_API UTN_PlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ── Stamina bar ───────────────────────────────────────────────────────────

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> StaminaBar;

	/**
	 * Barra que muestra la zona de stamina bloqueada por el peso de los ítems.
	 * Superponer sobre StaminaBar con alineación a la derecha y distinto color.
	 * Percent = WeightPenalty / MaxStamina.
	 * Si no hay peso, el porcentaje será 0 (invisible si el color tiene alpha 0).
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> WeightPenaltyBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> ExhaustedRoot;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StaminaText;

	// ── Score (RaceScore live) ────────────────────────────────────────────────

	/**
	 * TextBlock que muestra el RaceScore del jugador local en tiempo real.
	 * Nómbralo exactamente "ScoreText" en el BP Designer. Si no existe → no se muestra.
	 * Se actualiza vía OnRaceScoreChanged delegate del PlayerState (ScorePickup/MarkPlayerFinished).
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ScoreText;

	// ── Inventory slots ───────────────────────────────────────────────────────

	/**
	 * Imagen del slot equipado (activo). Muestra ItemIcon del ítem en mano.
	 * Nómbralo exactamente "SlotEquippedImage" en el BP Designer.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> SlotEquippedImage;

	/**
	 * Imagen del slot guardado (pasivo). Muestra ItemIcon del ítem en reserva.
	 * Nómbralo exactamente "SlotStoredImage" en el BP Designer.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> SlotStoredImage;

	/**
	 * Indicador visual del slot activo (borde, brillo, etc.).
	 * Siempre posicionado sobre el slot equipado. Puedes animarlo desde BP.
	 * Nómbralo exactamente "SlotEquippedSelector" en el BP Designer.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> SlotEquippedSelector;

	// ── Blueprint hooks ───────────────────────────────────────────────────────

	UFUNCTION(BlueprintImplementableEvent, Category = "Stamina")
	void OnStaminaUpdated(float CurrentStamina, float MaxStamina, bool bExhausted);

	/**
	 * Llamado cuando cambia el peso total cargado (o la stamina por peso).
	 * Úsalo en BP para animar la zona oscura de la barra o mostrar el icono de peso.
	 * @param WeightPenalty     Stamina "bloqueada" por el peso actual (0 = sin peso).
	 * @param MaxStamina        Stamina máxima base (sin penalización).
	 * @param EffectiveMaxStamina  Stamina máxima real = MaxStamina - WeightPenalty.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Stamina|Weight")
	void OnWeightUpdated(float WeightPenalty, float MaxStamina, float EffectiveMaxStamina);

	/**
	 * Llamado cuando el inventario cambia. Úsalo en BP para animaciones de slot.
	 * @param EquippedIcon    Icono del ítem equipado (puede ser null si no hay ítem)
	 * @param bHasEquipped    True si hay ítem en el slot equipado
	 * @param StoredIcon      Icono del ítem guardado (puede ser null si no hay ítem)
	 * @param bHasStored      True si hay ítem en el slot guardado
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory")
	void OnInventoryUpdated(UTexture2D* EquippedIcon, bool bHasEquipped,
	                        UTexture2D* StoredIcon,   bool bHasStored);

	/**
	 * Llamado cuando cambia el estado DBNO del jugador local.
	 * @param bIsDBNO             True si está en estado Down But Not Out.
	 * @param BleedoutRemaining   Segundos restantes antes de morir (−1 si no DBNO).
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DBNO")
	void OnDBNOStateChanged(bool bIsDBNO, float BleedoutRemaining);

	/**
	 * Llamado cuando el jugador local está canalizando un revive a un compañero.
	 * @param Progress01   Progreso de 0 a 1 (1 = revive completo).
	 * @param bIsReviving  True si está canalizando activamente.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DBNO")
	void OnReviveProgressUpdated(float Progress01, bool bIsReviving);

	/** Hook BP para animar incrementos de score (popup, scale tween, etc). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Score")
	void OnRaceScoreUpdated(int32 NewScore, int32 Delta);

private:
	void RefreshStaminaWidgets();
	void RefreshInventoryWidgets();
	void BindToPlayerStateScore();
	void UnbindFromPlayerStateScore();

	UFUNCTION()
	void HandleRaceScoreChanged(int32 NewScore);

	TWeakObjectPtr<ATN_CoopPlayerState> BoundPlayerState;
	int32 LastRaceScore = 0;

	TWeakObjectPtr<UTN_StaminaComponent>    CachedStamina;
	TWeakObjectPtr<UTN_InventoryComponent>  CachedInventory;

	// Stamina throttle
	float LastStamina       = -1.f;
	float LastWeightPenalty = -1.f;
	bool  bLastExhausted    = false;

	// Inventory change detection (compare by ItemId to avoid redundant refreshes)
	FName LastEquippedId = NAME_None;
	FName LastStoredId   = NAME_None;

	// DBNO state change detection
	bool  bLastDBNO       = false;
	bool  bLastReviving   = false;
	float LastReviveProgress = -1.f;

	static constexpr float kRefreshInterval = 0.05f;
	float RefreshAccumulator = 0.f;
};

