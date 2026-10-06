#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "TN_PauseMenuWidget.generated.h"

class APlayerState;
class UBorder;
class UCanvasPanel;
class UHorizontalBox;
class UImage;
class UProgressBar;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UVerticalBox;
class UWidget;
class UWidgetSwitcher;
class UTN_CreditsWidget;
class UTN_GameSettingsSubsystem;
class UTN_ScoreShellSynthComponent;

/** Qué es una fila del menú de pausa. */
enum class ETNPauseRowKind : uint8
{
	/** Botón: Intro, Espacio, A del mando o clic. */
	Button,
	/** Deslizador: izquierda y derecha (A y D, cruceta, stick), clic o arrastre sobre la barra. */
	Slider,
	/** Lista de opciones: izquierda y derecha, Intro (la siguiente) o clic en las flechas. */
	Choice,
	/** Solo texto (la lista de controles): se recorre como las demás, no hace nada. */
	Info,
	/** Medidor en vivo (nivel del micrófono con la marca del umbral). */
	Meter,
	/**
	 * Tecla de una acción (teclado y ratón, mando): Intro, Espacio, A o clic la cambian («Pulsa una tecla...»); Supr, Y
	 * del mando o clic derecho la devuelven a la de serie.
	 */
	KeyBind,
	/**
	 * Entrada de una lista que se pulsa como un botón (salas, jugadores de la sala): el nombre, dos columnas de texto y,
	 * si hay, un icono al final (el «⋮» de las opciones de un jugador, el candado de una sala cerrada).
	 */
	Entry,
};

/** Aspecto de una fila. */
enum class ETNPauseRowStyle : uint8
{
	/** Fila de ajustes: panel azul marino a lo ancho, nombre a la izquierda y el control a la derecha. */
	List,
	/** Botón grande de la portada: etiqueta de arena con icono (como los del campeón de la carrera). */
	Big,
	/** Pestaña de los ajustes: píldora (dorada la activa). */
	Tab,
	/** Botón del cuadro de confirmación. */
	Dialog,
};

/** Sonidos de la interfaz (los «pom» y «plin» de las conchas, UTN_ScoreShellSynthComponent). */
enum class ETNPauseSound : uint8
{
	/** Pasar por encima o recorrer con el teclado o el mando. */
	Hover,
	/** Pulsar un botón. */
	Press,
	/** Cambiar un valor (el tono sube con el valor). */
	Tick,
};

/** Páginas del menú de pausa (el orden es el de las páginas en el conmutador). */
enum class ETNPausePage : uint8
{
	Home,
	Settings,
	Controls,
	/** La sala: nombre, código, cerrar y abrir, y quién está dentro (con el «⋮» para expulsar, el anfitrión). */
	Room,
	/** Créditos y licencias (UTN_CreditsWidget, Docs/Creditos.md). */
	Credits,
};

/** Pestañas de los ajustes. */
enum class ETNPauseTab : uint8
{
	Graphics,
	Sound,
	Voice,
	Controls,
	Game,
	Count,
};

/**
 * @brief Fila del menú de pausa hecha en código (botón, deslizador, lista de opciones, texto o medidor), con el estilo
 * del HUD Tortunavy. Se puede enfocar: el ratón la enfoca al pasar por encima, así que teclado, ratón y mando comparten
 * el mismo resaltado. Arriba y abajo los resuelve la navegación de Slate (y el UScrollBox que la contiene la desplaza);
 * izquierda y derecha cambian el valor.
 */
UCLASS()
class TORTUNABO_API UTN_PauseRow : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetupButton(ETNPauseRowStyle InStyle, const FText& InLabel, TFunction<void()> InOnPressed, UTexture2D* InIcon = nullptr,
		const FText& InActionText = FText::GetEmpty());
	void SetupSlider(const FText& InLabel, float InMin, float InMax, float InStep, float InValue, TFunction<FText(float)> InFormat,
		TFunction<void(float)> InOnChanged);
	void SetupChoice(const FText& InLabel, const TArray<FText>& InOptions, int32 InIndex, TFunction<void(int32)> InOnChanged);
	void SetupInfo(const FText& InLabel, const FText& InValue, const FText& InValue2);
	/** Sampler: nivel (0..1), marca (0..1, negativa sin marca) y texto de la derecha; se llama cada fotograma. */
	void SetupMeter(const FText& InLabel, TFunction<void(float& OutLevel, float& OutMark, FText& OutText)> InSampler);
	/** Fila de tecla: InOnChange empieza a capturar la tecla nueva; InOnReset la devuelve a la de serie. */
	void SetupKeyBind(const FText& InLabel, const FString& InBindingId, TFunction<void()> InOnChange, TFunction<void()> InOnReset);
	/** Entrada: se pulsa como un botón; InValue e InValue2 van en dos columnas y, si hay, InIcon al final. */
	void SetupEntry(const FText& InLabel, const FText& InValue, const FText& InValue2, TFunction<void()> InOnPressed, UTexture2D* InIcon = nullptr);

	/** Colores de las dos columnas de texto (entradas y filas de texto). */
	void SetValueColors(const FLinearColor& InValue, const FLinearColor& InValue2);

	/** Ancho fijo (los botones de cuadro, para ponerlos en fila con otras cosas). */
	void SetWidthOverride(float InWidth);

	/** Teclas que enseña una fila de tecla (una columna sin cambio posible se ve apagada). */
	void SetKeyTexts(const FText& InKeyboard, const FText& InPad, bool bKeyboardEditable, bool bPadEditable);

	/** Esperando la tecla nueva: la fila lo dice y late. */
	void SetCapturing(bool bInCapturing);

	/** Fila de controles a la que corresponde (para volver a enfocarla al rehacer la lista). */
	const FString& GetBindingId() const { return BindingId; }

	/** Texto de ayuda que enseña el menú cuando la fila tiene el foco. */
	void SetDescription(const FText& InText) { Description = InText; }
	const FText& GetDescription() const { return Description; }

	/** Apagada: se ve atenuada y no cambia ni se pulsa (se puede seguir recorriendo). */
	void SetRowEnabled(bool bInEnabled);
	bool IsRowEnabled() const { return bEnabled; }

	/** Pestaña activa (píldora dorada). */
	void SetActive(bool bInActive);
	bool IsActive() const { return bActive; }

	void SetLabel(const FText& InLabel);

	/** Cambian el valor sin avisar (para refrescar la fila desde fuera). */
	void SetSliderValue(float InValue);
	/** Con OverrideText se enseña ese texto en vez de la opción (p. ej. «Personalizada», fuera de la lista). */
	void SetChoiceIndex(int32 InIndex, const FText& OverrideText = FText::GetEmpty());
	void SetChoiceOptions(const TArray<FText>& InOptions, int32 InIndex);

	int32 GetChoiceIndex() const { return Index; }
	float GetSliderValue() const { return Value; }
	ETNPauseRowKind GetKind() const { return Kind; }

	/** Pulsa el botón (o pasa a la siguiente opción) como si se hubiera pulsado Intro. */
	void Activate();

	/** El menú: aviso al recibir el foco (ayuda, desplazamiento) y sonidos. */
	TFunction<void(UTN_PauseRow*)> OnFocused;
	TFunction<void(ETNPauseSound, float)> OnSound;
	/** Pestañas: izquierda (-1) y derecha (1) de la cruceta, el stick, las flechas o A y D, en vez de mover el foco. */
	TFunction<void(int32 /*Direction*/)> OnSideStep;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FNavigationReply NativeOnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent,
		const FNavigationReply& InDefaultReply) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual void NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent) override;
	virtual void NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UBorder> Frame;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> Sizer;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ValueText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Value2Text;

	UPROPERTY(Transient)
	TObjectPtr<UImage> IconImage;

	UPROPERTY(Transient)
	TObjectPtr<UImage> LeftArrow;

	UPROPERTY(Transient)
	TObjectPtr<UImage> RightArrow;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> BarBox;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> Bar;

	UPROPERTY(Transient)
	TObjectPtr<UImage> MarkImage;

	ETNPauseRowKind Kind = ETNPauseRowKind::Info;
	ETNPauseRowStyle Style = ETNPauseRowStyle::List;
	FText Description;
	bool bEnabled = true;
	bool bActive = false;
	bool bFocused = false;
	bool bHovered = false;
	bool bPressed = false;
	bool bDragging = false;
	float Scale = 1.f;

	// Deslizador
	float Min = 0.f;
	float Max = 1.f;
	float Step = 0.05f;
	float Value = 0.f;
	TFunction<FText(float)> Format;
	TFunction<void(float)> OnValueChanged;

	// Lista de opciones
	TArray<FText> Options;
	int32 Index = 0;
	FText ChoiceOverride;
	TFunction<void(int32)> OnChoiceChanged;

	// Botón
	TFunction<void()> OnPressed;

	// Medidor
	TFunction<void(float&, float&, FText&)> Sampler;
	float MeterShown = 0.f;

	// Tecla
	FString BindingId;
	TFunction<void()> OnReset;
	bool bCapturing = false;
	bool bKeyEditable[2] = { true, true };
	/** Lo que enseña cada columna fuera de la captura. */
	FText KeyTexts[2];
	float CaptureClock = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> KeyCap;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> PadCap;

	/** Monta el árbol de la fila para su tipo y aspecto (lo llaman los Setup). */
	void Build();
	UWidget* BuildListContent();
	UWidget* BuildBigContent();
	void RefreshLook();
	void RefreshValue();
	void StepBy(int32 Direction);
	void SetValueFromScreen(const FVector2D& ScreenPosition);
	void PlaySound(ETNPauseSound Sound, float Pitch = 0.f) const;
};

/** Contador de fotogramas por segundo (ajuste «Mostrar FPS»): abajo a la derecha, sin tapar clics. */
UCLASS()
class TORTUNABO_API UTN_FpsCounterWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FpsText;

	float Accumulated = 0.f;
	int32 Frames = 0;
	float WorstFrame = 0.f;
};

/**
 * Quién habla (ajuste «Quién habla», accesibilidad): los jugadores que se oyen hablar por voz ahora mismo, con su nombre,
 * a la derecha de la pantalla. Para jugar sin sonido o con dificultades de oído. Sin tapar clics.
 */
UCLASS()
class TORTUNABO_API UTN_TalkersWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> List;

	/** Quién salía la última vez (para rehacer la lista solo si cambia) y cuánto falta para volver a mirar. */
	TArray<FString> Shown;
	float Timer = 0.f;
};

/**
 * @brief Menú de pausa de Tortunavy (todos los modos). No pausa el juego: la partida es en red y sigue.
 *
 * Arriba, el mapa o modo, la sala (nombre, código si es privada, «3/4» y si está cerrada) y los jugadores conectados con
 * su icono de voz (hablando, silenciado). Portada: Continuar, Ajustes, Controles, Sala (en red: código, cerrar y abrir,
 * y expulsar con el «⋮» de cada jugador, el anfitrión; Docs/Salas.md), Volver al lobby (el anfitrión lleva a todos; un invitado sale él solo al menú
 * principal), Salir de la partida o Menú principal y Salir al escritorio; lo que corta la partida pide confirmación. Ajustes en cinco pestañas
 * (Gráficos, Sonido, Voz, Controles, Juego) que se aplican al momento (UTN_GameSettingsSubsystem y UGameUserSettings);
 * Controles: las teclas y los botones del juego (de IMC_Player, más hablar y el menú), que se cambian aquí mismo
 * («Pulsa una tecla...»), y los que no se cambian (cámara, espectador, menús).
 *
 * Mientras está abierto: modo de entrada interfaz y juego, cursor a la vista, la tortuga quieta (sin mover ni girar la
 * cámara) y las teclas se quedan en el menú (salvo la consola y la de pulsar para hablar). Escape o B vuelven atrás (en la
 * portada, cierran); Tabulador, Start o la tecla elegida para el menú cierran del todo; Q y E o LB y RB cambian de
 * pestaña. Todo va dentro de un lienzo de 1920 × 1080 que se encoge si no cabe (tamaño de la interfaz grande).
 *
 * Lo abre y lo cierra UTN_GameSettingsSubsystem (Escape; la tecla y el botón elegidos, Start de serie; Tabulador en el
 * editor).
 *
 * Partida local (#311): lo abre cualquiera, a toda la pantalla, y la partida se para para todos. Solo lo maneja quien lo
 * abrió (su foco; el ratón, que es del jugador 1, no toca el de un invitado) y lo que cambia es suyo: un invitado ve
 * Controles y Juego con sus ajustes de jugador (cámara, teclas y botones), que no se guardan, y puede dejar de jugar en el
 * lobby; el jugador 1 ve todo y cierra la partida. Sin página «Sala» ni pestaña de voz; en el lobby, «Hacer el tutorial».
 */
UCLASS()
class TORTUNABO_API UTN_PauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Toma la entrada del jugador (modo interfaz y juego, cursor, tortuga quieta) y enfoca la primera opción. */
	void TakeInput();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent) override;
	virtual FNavigationReply NativeOnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent,
		const FNavigationReply& InDefaultReply) override;

private:
	// ── Árbol ────────────────────────────────────────────────────────────────

	/** Lienzo de 1920 × 1080 dentro de una caja que lo encoge si no cabe (interfaz grande o pantalla pequeña). */
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Canvas;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetSwitcher> Pages;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ModeText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SessionText;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> PlayersBox;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> HomeColumn;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HomeNote;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> SettingsList;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> ControlsList;

	/** Página «Sala»: su lista y el botón de la portada que lleva a ella (para volver a enfocarlo). */
	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> RoomList;

	UPROPERTY(Transient)
	TObjectPtr<UTN_PauseRow> RoomHomeRow;

	/** Página «Créditos» (su botón es el cuarto de la portada, HomeRows[3]). */
	UPROPERTY(Transient)
	TObjectPtr<UTN_CreditsWidget> CreditsPage;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HelpText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HintText;

	/** Aviso de un momento (qué tecla se ha cambiado, a quién se le ha quitado...), encima de la ayuda. */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NoticeText;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTN_PauseRow>> TabRows;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTN_PauseRow>> HomeRows;

	/** Iconos de voz de la lista de jugadores (uno por jugador, en el orden de ChipPlayers). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> ChipVoiceIcons;

	TArray<TWeakObjectPtr<APlayerState>> ChipPlayers;

	// ── Confirmación ─────────────────────────────────────────────────────────

	UPROPERTY(Transient)
	TObjectPtr<UWidget> ConfirmLayer;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ConfirmTitle;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ConfirmText;

	UPROPERTY(Transient)
	TObjectPtr<UTN_PauseRow> ConfirmYes;

	UPROPERTY(Transient)
	TObjectPtr<UTN_PauseRow> ConfirmNo;

	TFunction<void()> ConfirmAction;
	TFunction<void()> CancelAction;
	/** Cuenta atrás del cuadro (resolución nueva: se deshace sola al llegar a cero); negativa sin cuenta. */
	float ConfirmCountdown = -1.f;
	FText ConfirmBaseText;
	TWeakObjectPtr<UTN_PauseRow> FocusBeforeConfirm;

	// ── Estado ───────────────────────────────────────────────────────────────

	ETNPausePage Page = ETNPausePage::Home;
	ETNPauseTab Tab = ETNPauseTab::Graphics;
	TWeakObjectPtr<UTN_PauseRow> LastFocused;
	TWeakObjectPtr<UTN_ScoreShellSynthComponent> Synth;
	double LastSoundTime = 0.0;
	float InfoTimer = 0.f;
	float Clock = 0.f;
	bool bInputTaken = false;
	bool bLeaving = false;
	int32 IgnoreMoveApplied = 0;
	int32 IgnoreLookApplied = 0;
	/** Menús que ya estaban en pantalla al abrir (para saber al cerrar si ha salido otro que quiere el cursor). */
	TArray<TWeakObjectPtr<UUserWidget>> WidgetsAtOpen;
	/** Jugadores con fila propia en la pestaña de voz (para rehacerla si alguien entra o sale). */
	TArray<TWeakObjectPtr<APlayerState>> VoiceTabPlayers;

	/** Lo que enseña la página «Sala» (jugadores, cerrada, plazas...): se rehace si cambia. */
	FString RoomListSignature;

	/** Aviso en pantalla: segundos que le quedan. */
	float NoticeTime = 0.f;

	/** Captura de tecla: fila y fila de controles que esperan la tecla nueva y cuánto llevan esperando. */
	TWeakObjectPtr<UTN_PauseRow> CaptureRow;
	FString CaptureId;
	float CaptureElapsed = 0.f;

	/** Filas de gráficos que se refrescan entre sí (calidad general y por partes, escala de resolución, ventana). */
	TWeakObjectPtr<UTN_PauseRow> OverallRow;
	TWeakObjectPtr<UTN_PauseRow> ResolutionRow;
	TWeakObjectPtr<UTN_PauseRow> ResScaleRow;
	TArray<TWeakObjectPtr<UTN_PauseRow>> QualityRows;
	TArray<FIntPoint> ResolutionChoices;

	// ── Montaje ──────────────────────────────────────────────────────────────

	void BuildTree();
	UWidget* BuildHeader();
	UWidget* BuildHomePage();
	UWidget* BuildSettingsPage();
	UWidget* BuildControlsPage();
	UWidget* BuildRoomPage();
	UWidget* BuildConfirmLayer();
	void BuildHomeButtons();
	void RefreshHeader();
	void RebuildPlayers();
	void UpdateVoiceIcons();

	void ShowPage(ETNPausePage NewPage);
	/** Abre la pestaña NewTab; el foco, a su lista o (bFocusList = false, al cambiarla desde la barra) a su pestaña. */
	void ShowTab(ETNPauseTab NewTab, bool bFocusList = true);
	void FillTab();
	void FillGraphicsTab();
	void FillSoundTab();
	void FillVoiceTab();
	void FillControlsTab();
	void FillGameTab();
	void FillControlsList();
	/** Página «Sala»: la sala y sus jugadores (la vuelve a hacer si cambia algo, sin perder la fila enfocada). */
	void FillRoomList();
	FString BuildRoomSignature() const;
	/** El «⋮» de un jugador (anfitrión): sus opciones, y expulsar con confirmación. */
	void OpenPlayerOptions(APlayerState* Target);
	void RefreshGraphicsRows();
	void RefreshHint();

	UTN_PauseRow* NewRow();
	UTN_PauseRow* AddListRow(UScrollBox* List);
	void AddListHeader(UScrollBox* List, const FText& Title);
	void AddListNote(UScrollBox* List, const FText& Note);
	UTN_PauseRow* AddVolumeRow(const FText& Label, const FText& Description, float Value, TFunction<void(float)> OnChanged,
		float MaxValue = 1.f);
	UTN_PauseRow* AddToggleRow(const FText& Label, const FText& Description, bool bValue, TFunction<void(bool)> OnChanged);
	UTN_PauseRow* AddQualityRow(const FText& Label, const FText& Description, int32 Value, TFunction<void(int32)> OnChanged);
	/** Fila de tecla de la fila de controles Id (de UTN_GameSettingsSubsystem::GetKeyBindings). */
	UTN_PauseRow* AddKeyBindRow(UScrollBox* List, const FString& Id, const FText& Description);

	// ── Acciones ─────────────────────────────────────────────────────────────

	void GoBack();
	void CloseMenu();
	void ReturnToLobby();
	void LeaveToMenu();
	void QuitToDesktop();
	void AskConfirm(const FText& Title, const FText& Text, const FText& YesLabel, TFunction<void()> OnYes,
		TFunction<void()> OnNo = nullptr, float CountdownSeconds = -1.f);
	void CloseConfirm(bool bAccepted);
	bool IsConfirmOpen() const;
	void OnVideoModeChanged();

	// ── Teclas ───────────────────────────────────────────────────────────────

	void StartKeyCapture(UTN_PauseRow* Row, const FString& Id);
	void FinishKeyCapture(const FKey& Key);
	void CancelKeyCapture(bool bSilent);
	bool IsCapturingKey() const { return !CaptureId.IsEmpty(); }
	void ResetKeyRow(const FString& Id);
	/** Rehace la lista donde están las filas de tecla (controles o voz) y vuelve a enfocar la de Id. */
	void RefreshKeyRows(const FString& FocusId);
	void ShowNotice(const FText& Text, float Seconds = 5.f);

	// ── Entrada y foco ───────────────────────────────────────────────────────

	void ApplyMenuInputMode();
	void ReleaseInput();
	void FocusRow(UTN_PauseRow* Row);
	void FocusFirstOfPage();
	void HandleRowFocused(UTN_PauseRow* Row);
	/** El jugador ha cambiado el idioma (fila «Idioma»): refresca lo que el menú guarda como texto compuesto (cabecera, pie y ayuda). */
	void OnLanguageChanged();
	void PlayUISound(ETNPauseSound Sound, float Pitch);

	// ── Contexto ─────────────────────────────────────────────────────────────

	UTN_GameSettingsSubsystem* GetSettings() const;
	bool IsHost() const;
	bool IsInLobby() const;
	bool CanReturnToLobby() const;
	/** Partida en red (anfitrión o invitado): hay página «Sala». */
	bool HasRoomPage() const;

	// ── Partida local (#311) ─────────────────────────────────────────────────

	/** true si lo ha abierto un invitado de la partida local (solo sus ajustes de jugador; el ratón no lo toca). */
	bool IsGuestMenu() const;
	/** true en una partida local. */
	bool IsLocalGame() const;
	/** ¿Se ve esta pestaña? (Un invitado, solo Controles y Juego; en la partida local, sin Voz.) */
	bool IsTabAvailable(ETNPauseTab InTab) const;
	/** La siguiente pestaña que se ve en esa dirección (Q y E, LB y RB), desde la abierta o desde From. */
	ETNPauseTab StepTab(int32 Direction) const { return StepTab(Tab, Direction); }
	ETNPauseTab StepTab(ETNPauseTab From, int32 Direction) const;
	/** Enfoca para el jugador que maneja el menú (con la pantalla partida, su foco; si no, el del teclado). */
	void FocusForOwner(UWidget* Widget);
};
