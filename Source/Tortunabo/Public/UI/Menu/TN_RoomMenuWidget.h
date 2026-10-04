#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Multiplayer/TN_RoomTypes.h"
#include "UI/Pause/TN_PauseMenuWidget.h"
#include "TN_RoomMenuWidget.generated.h"

class UBorder;
class UScrollBox;
class UTextBlock;
class UTexture2D;
class UVerticalBox;
class UWidget;
class UWidgetSwitcher;
class UMP_GameInstance;
class UTN_ScoreShellSynthComponent;

/** Pantallas del menú de salas. */
enum class ETNRoomMenuPage : uint8
{
	/** Sin pantalla: solo el aviso de arriba, si hay (el menú del Blueprint se ve y se pulsa). */
	Closed,
	/** Crear partida: modo, pública o privada, plazas, nombre y código. */
	Create,
	/** Unirse: con un código o de la lista de salas públicas. */
	Join,
};

/**
 * @brief Campo del código de sala: cinco casillas con mayúsculas automáticas (Docs/Salas.md).
 *
 * Teclado: se escribe directamente (solo entran las letras y los números del alfabeto de TNRoomCode; las minúsculas pasan
 * a mayúsculas), Retroceso y Supr borran, ← → cambian de casilla, Ctrl+V o Mayús+Insert pegan (del texto pegado se saca el
 * código: «Código: K7M2P» → «K7M2P»), Ctrl+C copia e Intro entra. Ratón: clic en una casilla la elige, clic derecho pega y
 * la rueda cambia la letra. Mando: A empieza (y acaba) a escribir; escribiendo, ↑ ↓ cambian la letra, ← → la casilla, X la
 * borra y B termina; sin escribir, el mando se mueve por el menú como siempre.
 */
UCLASS()
class TORTUNABO_API UTN_RoomCodeField : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Lo escrito, en orden (puede estar incompleto). */
	FString GetCode() const;

	/** Rellena las casillas (normalizado) y pone el cursor detrás. */
	void SetCode(const FString& InCode);

	/** true si están las cinco casillas. */
	bool IsComplete() const;

	/** Pega el código del portapapeles (o avisa si no hay ninguno). */
	void PasteFromClipboard();

	/** Intro: entrar con el código. */
	TFunction<void()> OnSubmit;
	/** Al recibir el foco (ayuda de abajo). */
	TFunction<void(UTN_RoomCodeField*)> OnFocused;
	/** Sonidos de la interfaz, como las filas del menú de pausa. */
	TFunction<void(ETNPauseSound, float)> OnSound;
	/** Aviso corto (una letra que no vale, portapapeles sin código...). */
	TFunction<void(const FText&)> OnHint;

	/** Ayuda que enseña el menú con el foco aquí. */
	FText GetDescription() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyChar(const FGeometry& InGeometry, const FCharacterEvent& InCharEvent) override;
	virtual FNavigationReply NativeOnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent,
		const FNavigationReply& InDefaultReply) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent) override;
	virtual void NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent) override;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> Cells;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> CellTexts;

	/** Una letra por casilla (0: vacía). */
	TArray<TCHAR> Chars;
	/** Casilla del cursor. */
	int32 CaretIndex = 0;
	/** Escribiendo con el mando: ↑ ↓ cambian la letra y ← → la casilla (la navegación del menú se para). */
	bool bEditing = false;
	bool bFocused = false;
	float Clock = 0.f;

	void RefreshCells();
	void TypeChar(TCHAR Char);
	void Erase(bool bBackward);
	void MoveCaret(int32 Delta);
	void CycleChar(int32 Delta);
	void SetEditing(bool bInEditing);
	int32 CellAt(const FVector2D& ScreenPosition) const;
	void PlaySound(ETNPauseSound Sound, float Pitch = 0.f) const;

	/**
	 * Con mando y Steam (Steam Deck o Big Picture), A abre el teclado en pantalla de Steam (#354): el flotante escribe en
	 * las casillas como un teclado y el de pantalla completa deja el código al cerrarlo. false si no hay teclado de Steam
	 * (sin Steam, en el escritorio normal...): entonces A escribe con las casillas, como siempre.
	 */
	bool OpenSteamKeyboard();
	void HandleSteamText(bool bSubmitted, const FString& Text);
	/** El rectángulo de las casillas en la ventana del juego (px), para que el teclado flotante no lo tape. */
	FIntRect RectInWindow() const;
};

/**
 * @brief Pantallas de salas del menú principal (Docs/Salas.md), montadas en código con el estilo del menú de pausa: «Crear
 *        partida» (modo, pública o privada, 4, 6 u 8 plazas, nombre al azar con «otro nombre» y el código de la privada, que
 *        se puede copiar) y «Unirse» (código de sala y lista de salas públicas con su nombre en tu idioma, modo, «3/4» y si
 *        está cerrada; actualizar y entrar).
 *
 * La crea y la enseña UMP_MainMenuWidget (sus botones «Crear partida» y «Unirse» abren cada pantalla y, mientras está
 * abierta, el menú del Blueprint se esconde). Cerrada, solo enseña arriba el aviso que dejó la GameInstance al volver al
 * menú (expulsado, sala cerrada o llena, el anfitrión se fue...). Teclado, ratón y mando como en el menú de pausa (filas
 * UTN_PauseRow); Esc o B vuelven, F5 o Y actualizan la lista.
 */
UCLASS()
class TORTUNABO_API UTN_RoomMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Open(ETNRoomMenuPage NewPage);
	void Close();
	bool IsOpen() const { return Page != ETNRoomMenuPage::Closed; }

	/** Aviso: dentro de la pantalla si está abierta; si no, arriba, sobre el menú. */
	void ShowNotice(const FText& Text, bool bError, float Seconds = 7.f);

	/** Se abre o se cierra (el menú principal esconde o enseña sus botones). */
	TFunction<void(bool /*bOpen*/)> OnOpenChanged;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	// ── Árbol ────────────────────────────────────────────────────────────────

	/** Velo y pantallas (escondido con la pantalla cerrada). */
	UPROPERTY(Transient)
	TObjectPtr<UWidget> ScreenLayer;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetSwitcher> Pages;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HelpText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HintText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NoticeText;

	/** Aviso de arriba con la pantalla cerrada. */
	UPROPERTY(Transient)
	TObjectPtr<UWidget> Toast;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ToastText;

	// Crear partida
	UPROPERTY(Transient)
	TObjectPtr<UTN_PauseRow> ModeRow;

	/** Rally (#632): circuito, solo con el Rally elegido, y tortugas por buggy, con el Rally o Karts. */
	UPROPERTY(Transient)
	TObjectPtr<UTN_PauseRow> RallyMapRow;

	UPROPERTY(Transient)
	TObjectPtr<UTN_PauseRow> RallySeatsRow;

	/** Todos contra Todos (#651): arena, solo con el modo elegido. */
	UPROPERTY(Transient)
	TObjectPtr<UTN_PauseRow> TctArenaRow;

	UPROPERTY(Transient)
	TObjectPtr<UTN_PauseRow> VisibilityRow;

	UPROPERTY(Transient)
	TObjectPtr<UTN_PauseRow> SizeRow;

	UPROPERTY(Transient)
	TObjectPtr<UTN_PauseRow> NameRow;

	UPROPERTY(Transient)
	TObjectPtr<UTN_PauseRow> CodeRow;

	UPROPERTY(Transient)
	TObjectPtr<UTN_PauseRow> CreateButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CreateSummary;

	// Unirse
	UPROPERTY(Transient)
	TObjectPtr<UTN_RoomCodeField> CodeField;

	UPROPERTY(Transient)
	TObjectPtr<UTN_PauseRow> CodeJoinButton;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> RoomScroll;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ListStatus;

	// ── Estado ───────────────────────────────────────────────────────────────

	ETNRoomMenuPage Page = ETNRoomMenuPage::Closed;
	/** Lo elegido en «Crear partida». */
	FTNRoomConfig Draft;
	TArray<int32> SizeOptions;
	TWeakObjectPtr<UWidget> LastFocused;
	TWeakObjectPtr<UTN_ScoreShellSynthComponent> Synth;
	double LastSoundTime = 0.0;
	float NoticeTime = 0.f;
	float ToastTime = 0.f;
	/** Segundos para volver a buscar salas solo (con la pantalla «Unirse» abierta). */
	float AutoRefreshTime = 0.f;
	float Clock = 0.f;
	FDelegateHandle NoticeHandle;
	FDelegateHandle ListHandle;

	// ── Montaje ──────────────────────────────────────────────────────────────

	void BuildTree();
	UWidget* BuildCreatePage();
	UWidget* BuildJoinPage();
	UTN_PauseRow* NewRow();

	// ── Crear partida ────────────────────────────────────────────────────────

	void RefreshCreateRows();
	void RerollName();
	void HostRoom();

	// ── Unirse ───────────────────────────────────────────────────────────────

	void RebuildRoomRows();
	void RefreshListStatus();
	void RefreshRooms();
	void JoinByCode();

	// ── Entrada, foco, avisos y sonido ───────────────────────────────────────

	void GoBack();
	void FocusWidget(UWidget* Target);
	void FocusFirst();
	void HandleFocused(UWidget* Focused, const FText& Description);
	void RefreshHint();
	void PlayUISound(ETNPauseSound Sound, float Pitch);
	void HandleRoomNotice(const FText& Message, bool bError);
	void HandleRoomListChanged();
	UMP_GameInstance* GetRoomGameInstance() const;
};
