#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/TN_MatchFlowTypes.h"
#include "TN_CoopFlowHUDWidget.generated.h"

class UBorder;
class UHorizontalBox;
class UTextBlock;
class UTexture2D;
class UVerticalBox;
class UWidget;
class ATN_CoopGameState;
class ATN_CoopPlayerState;

/**
 * @brief HUD del flujo de partida cooperativa: status strip, panel de Resultados, scoreboard global y feed de Quick Chat.
 *
 * Responsabilidades:
 *  - Status strip (PrimaryText/SecondaryText) con el estado actual: "Esperando jugadores", countdown, "1º / 2º / 3º", etc.
 *  - Panel de Resultados al final de la run con scoreboard de hasta 8 filas (Row1..Row8 RankNameTimeScore): las cuatro de
 *    siempre y, con más de cuatro jugadores, una por jugador.
 *  - Hint de espectador mientras el jugador está observando a otros.
 *  - Feed de Quick Chat con fade-out automático tras inactividad.
 *  - Bindings dobles: polling cada RefreshInterval + delegate OnMatchFlowStateChanged como backup.
 */
UCLASS()
class TORTUNABO_API UTN_CoopFlowHUDWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ── Lobby / In-progress status strip ──────────────────────────────────────
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> RootContainer;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PrimaryText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SecondaryText;

	// ── Results panel widgets — name them EXACTLY as declared here ─────────────
	// Any container widget (Overlay, Border, CanvasPanel…) named "ResultsOverlay"
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> ResultsOverlay;

	// "¡PRIMER LUGAR!" / "¡ELIMINADO!" etc.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultsTitle;

	// "Puesto: #1" / "Eliminado"
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultsRankText;

	// "Tiempo: 12.3s"  — empty if player was eliminated
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultsTimeText;

	// "Volviendo al lobby en: 8"  — updated every 0.1 s
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultsCountdown;

	// Puntuación final del Coop (#789): desglose por término y total. Oculto si la partida no la tiene.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CoopScoreText;

	// Títulos de fin de partida (#798): «Saltarín: nombre (N saltos)». Oculto si nadie se lo lleva.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EndTitleText;

	// "Scroll para cambiar de jugador"  — shown only while spectating
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SpectatorHint;

	// ── Scoreboard global (Results) ─────────────────────────────────────────────
	// Hasta 8 filas (los jugadores que caben en la sesión). En el BP nómbralas exactamente:
	//   Row1RankText, Row1NameText, Row1TimeText, Row1ScoreText
	//   Row2... Row3... Row4... Row5... Row6... Row7... Row8...
	// Las filas 5 a 8 son opcionales: un BP con solo cuatro sigue valiendo (los resultados de más de cuatro jugadores no
	// se enseñan).
	// Si una fila no tiene datos (ej: solo 3 jugadores), sus textos se vacían.
	// El número del rank: "1º"/"2º"/... o "✗" si eliminado.

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row1RankText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row1NameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row1TimeText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row1ScoreText;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row2RankText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row2NameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row2TimeText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row2ScoreText;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row3RankText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row3NameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row3TimeText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row3ScoreText;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row4RankText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row4NameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row4TimeText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row4ScoreText;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row5RankText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row5NameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row5TimeText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row5ScoreText;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row6RankText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row6NameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row6TimeText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row6ScoreText;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row7RankText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row7NameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row7TimeText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row7ScoreText;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row8RankText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row8NameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row8TimeText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Row8ScoreText;

	/** Filas del marcador: los ocho jugadores que caben en la sesión. */
	static constexpr int32 MaxScoreboardRows = 8;

	/**
	 * Fondo de cada fila del marcador, en orden (opcional: lo rellena el HUD que construye las filas en código, como
	 * UTN_RunFlowHUDWidget; un Blueprint dibujado a mano no lo necesita). Con cuatro resultados o menos se ven las cuatro
	 * filas de siempre; con más, una fila por resultado y algo más compactas para que quepan ocho.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> ScoreboardRowPanels;

	// Optional Blueprint hook — called when the flow state changes.
	// Override in BP if you need extra visual effects; all logic is already in C++.
	UFUNCTION(BlueprintImplementableEvent, Category = "Flow")
	void OnFlowStateChanged(ETNMatchFlowState NewState);

	/**
	 * Pinta una línea nueva en el feed de quick chat.
	 * La implementación C++ escribe automáticamente en ChatHistoryBox.
	 * Blueprint puede sobreescribir para añadir efectos extra; si lo hace,
	 * llamar a Super para que el C++ también se ejecute.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "QuickChat")
	void OnQuickChatEntryReceived(int32 Sequence, const FText& SenderName, const FText& MessageText, UTexture2D* Icon, float ServerTimeSeconds);

	/**
	 * VerticalBox donde se añaden los mensajes de quick chat.
	 * Añadir en el Designer del Blueprint con el nombre exacto "ChatHistoryBox".
	 * Sin nodos en Event Graph necesarios: la implementación C++ lo rellena.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> ChatHistoryBox;

	/** Máximo de líneas visibles en el feed de chat (las más antiguas se eliminan). */
	UPROPERTY(EditDefaultsOnly, Category = "QuickChat")
	int32 MaxChatLines = 8;

	/** Color del texto de los mensajes de chat. */
	UPROPERTY(EditDefaultsOnly, Category = "QuickChat")
	FLinearColor ChatTextColor = FLinearColor::White;

	/** Segundos de inactividad antes de que el feed de chat empiece a desaparecer. */
	UPROPERTY(EditDefaultsOnly, Category = "QuickChat")
	float ChatInactivitySeconds = 3.f;

	/** Duración del fade-out del feed de chat (segundos). */
	UPROPERTY(EditDefaultsOnly, Category = "QuickChat")
	float ChatFadeOutSeconds = 0.5f;

private:
	void EnsureRuntimeWidgets();
	void RefreshTexts();
	void BindQuickChat(ATN_CoopGameState* GameState);
	void UnbindQuickChat();
	void ReplayQuickChatHistory(const ATN_CoopGameState* GameState);

	UFUNCTION()
	void HandleQuickChatReceived(const FTN_QuickChatEntry& Entry);

	// ── Status strip helpers ───────────────────────────────────────────────────
	FText BuildPrimaryText(const ATN_CoopGameState* GameState) const;
	FText BuildSecondaryText(const ATN_CoopGameState* GameState) const;
	bool  ShouldBeVisible(ETNMatchFlowState State) const;

	// ── Results panel helpers ──────────────────────────────────────────────────
	void HandleFlowStateChange(ETNMatchFlowState NewState, const ATN_CoopGameState* GameState);
	void ShowResultsPanel(const ATN_CoopGameState* GameState);
	void HideResultsPanel();
	void RefreshResultsCountdown(const ATN_CoopGameState* GameState);
	/** Lo de Results que puede llegar más tarde que el estado (puntuación final del Coop): se repasa en cada refresco. */
	void RefreshResultsExtras(const ATN_CoopGameState* GameState);
	FText BuildRankTitle(int32 FinishRank, bool bEliminated) const;

	void RefreshScoreboard(const ATN_CoopGameState* GameState);
	void FillScoreboardRow(int32 RowIndex, const struct FTN_RaceResultEntry* Entry);

	/** Los cuatro textos de una fila del marcador (puesto, nombre, tiempo, puntos); nulos los que el diseño no trae. */
	void GetScoreboardRowTexts(int32 RowIndex, UTextBlock* (&OutTexts)[4]) const;

	/** Enseña las filas que hacen falta (ScoreboardRowPanels) y las compacta si son más de cuatro. */
	void ApplyScoreboardDensity(int32 NumEntries);

	/** Tamaño de letra de las filas antes de compactarlas (se lee la primera vez). */
	float ScoreboardBaseFontSize = 0.f;

	UFUNCTION()
	void HandleRaceResultsUpdated();

	void StartChatFade();
	UHorizontalBox* BuildChatRow(const FText& SenderName, const FText& MessageText, UTexture2D* Icon);

	// ── State tracking ─────────────────────────────────────────────────────────
	float RefreshAccumulator  = 0.f;
	float RefreshInterval     = 0.1f;

	ETNMatchFlowState LastKnownFlowState = ETNMatchFlowState::WaitingForPlayers;
	bool bFlowStateInitialized = false;
	bool bResultsVisible       = false;
	int32 LastQuickChatSequenceSeen = 0;
	bool bQuickChatHistoryReplayed = false;

	UPROPERTY(Transient)
	TObjectPtr<ATN_CoopGameState> BoundQuickChatGameState;

	// ── Chat fade state ────────────────────────────────────────────────────────
	FTimerHandle ChatFadeTimer;
	bool  bChatFadingOut     = false;
	float ChatCurrentOpacity = 1.f;

	// ── OnMatchFlowStateChanged binding — backup channel besides polling ──────
	// Binding allows immediate response when state changes, covering the case
	// where the widget is recreated (e.g. after ClientRestart on revive) and
	// the polling misses the transition because the timer hasn't fired yet.
	UFUNCTION()
	void OnMatchFlowStateChangedHandler(ETNMatchFlowState NewState);

	UPROPERTY(Transient)
	TObjectPtr<ATN_CoopGameState> BoundFlowGameState;
};
