#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "UI/Race/TN_RaceTallyWidget.h"
#include "TN_RaceChampionWidget.generated.h"

class ATN_RacePodiumStage;
class UBorder;
class UButton;
class UCanvasPanel;
class UCanvasPanelSlot;
class UImage;
class UMaterialInstanceDynamic;
class UTextBlock;
class UWidget;
class UTN_ScoreShellSynthComponent;

/** Lo que enseña la pantalla del campeón. */
struct FTNRaceChampionSetup
{
	/** Podio: la primera (campeona), la segunda y la tercera; puede haber menos. */
	TArray<FTNRaceTallyRow> Podium;
	/** Conchas para ganar (las que luce la campeona). */
	int32 Target = 3;
	/** La partida se decidió en el sprint final de desempate (lo dice el cartel de la campeona). */
	bool bSprintWin = false;
	/** Vista previa por consola (TN.Race.Podium): cualquier botón la cierra. */
	bool bPreview = false;
};

/** Papelito de confeti que cae sobre el podio (se pinta en NativePaint). */
struct FTNConfetti
{
	FVector2D Pos = FVector2D::ZeroVector;
	FVector2D Vel = FVector2D::ZeroVector;
	FVector2D Size = FVector2D(10.f, 16.f);
	float Angle = 0.f;
	float Spin = 0.f;
	float Sway = 0.f;
	FLinearColor Tint = FLinearColor::White;
};

/**
 * Foco con mando y teclado de la pantalla del campeón (#557), como lógica pura: la usa UTN_RaceChampionWidget y la
 * prueban los tests de Tortunabo.UI.RaceChampion.
 */
namespace TNRaceChampionFocus
{
	/** Botones en el orden en que se ven (de arriba abajo). */
	enum EButton : int32 { PlayAgain = 0, ChangeMode = 1, Quit = 2, ButtonCount = 3 };

	/** Qué hace la tecla de atrás (B, Escape). */
	enum class EBackAction : uint8 { None, FocusQuit, Quit };

	/** Volver a jugar y Cambiar de modo, solo para quien elige; Salir, para todos; ninguno una vez elegido. */
	bool IsEnabled(int32 Button, bool bCanChoose, bool bChoiceMade);

	/**
	 * Botón que debe tener el foco: el actual si sigue activo; si no (o si no hay ninguno), el siguiente activo dando la
	 * vuelta, empezando por arriba. INDEX_NONE si no queda ninguno activo.
	 */
	int32 PickFocus(int32 Current, bool bCanChoose, bool bChoiceMade);

	/** Atrás: con el foco fuera de Salir lo lleva a Salir (así el anfitrión no se va de un toque); en Salir, sale. */
	EBackAction OnBack(int32 Current, bool bChoiceMade);

	/** B del mando, Escape y el atrás genérico. */
	bool IsBackKey(const FKey& Key);
}

/**
 * @brief Pantalla del campeón del modo carrera (ETNBeachRacePhase::Champion), con el estilo del HUD Tortunavy y hecha
 * en código.
 *
 * A la izquierda, sobre un panel azul marino que se funde con el fondo: la cinta «¡Campeona de la playa!», el cartel de
 * la campeona (su cara con su piel, corona, su nombre y sus conchas) y los botones Volver a jugar, Cambiar de modo y
 * Salir. Solo el anfitrión elige volver a jugar o cambiar de modo (ATN_BeachRaceGameMode::CanLocalPlayerChoose); los
 * demás ven esos botones apagados y pueden salir. Los botones llaman a ATN_BeachRaceGameMode::RequestChampionChoice:
 * la interfaz no decide nada del flujo. Con mando o teclado el foco empieza en el primer botón activo (Salir en un
 * cliente), la cruceta o el stick lo mueven, la A pulsa y la B lleva a Salir (y, desde Salir, sale).
 *
 * A la derecha, de fondo animado, el podio de verdad (ATN_RacePodiumStage, capturado a una textura y pintado con
 * M_UI_Preview) sobre un cielo pintado (degradado, sol y nubes que pasan), con el nombre de cada tortuga encima de su
 * cabeza y confeti cayendo. Tapa toda la pantalla (también el panel de resultados del HUD de siempre).
 *
 * Lo crea UTN_RaceScreensSubsystem; también la vista previa TN.Race.Podium.
 */
UCLASS()
class TORTUNABO_API UTN_RaceChampionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Setup(const FTNRaceChampionSetup& InSetup);

	/** Segundos que quedan de la fase (0 = sin cuenta: se espera al anfitrión). */
	void SetSecondsLeft(float InSeconds);

	/** Vista previa: se llama al pulsar cualquier botón (cierra la vista previa). */
	TFunction<void()> OnPreviewClosed;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InFocusEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void BuildTree();
	UButton* MakeButton(const FText& Label, uint8 Icon);
	void RefreshButtons();
	/** Botón por índice (TNRaceChampionFocus::EButton). */
	UButton* GetButton(int32 Index) const;
	/** Índice del botón que tiene el foco del jugador dueño, o INDEX_NONE. */
	int32 GetFocusedButton() const;
	/**
	 * Pone el foco en el botón que toca (TNRaceChampionFocus::PickFocus). Sin bForce solo lo mueve si ya estaba en uno de
	 * los botones (no se lo quita a otro menú que se haya abierto encima).
	 */
	void UpdateFocus(bool bForce);
	void FocusButton(int32 Index);
	void TickPodiumImage();
	void TickConfetti(float DeltaTime);
	/** Elección de la pantalla (ETNBeachChampionChoice como número). */
	void Choose(uint8 Choice);
	void PlaySound(bool bPlin, uint8 Tier, float Semitones, float Volume);

	UFUNCTION() void HandlePlayAgain();
	UFUNCTION() void HandleChangeMode();
	UFUNCTION() void HandleQuit();
	UFUNCTION() void HandleHovered();

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Canvas;
	UPROPERTY(Transient) TObjectPtr<UImage> SunImage;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> Clouds;
	UPROPERTY(Transient) TObjectPtr<UImage> PodiumImage;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PodiumMID;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> NameTags;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> NameTagTexts;
	UPROPERTY(Transient) TObjectPtr<UWidget> LeftColumn;
	UPROPERTY(Transient) TObjectPtr<UImage> ChampionFace;
	UPROPERTY(Transient) TObjectPtr<UImage> ChampionCrown;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ChampionName;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> ChampionShells;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SubtitleText;
	UPROPERTY(Transient) TObjectPtr<UButton> PlayAgainButton;
	UPROPERTY(Transient) TObjectPtr<UButton> ChangeModeButton;
	UPROPERTY(Transient) TObjectPtr<UButton> QuitButton;
	UPROPERTY(Transient) TObjectPtr<UWidget> HostNote;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(Transient) TObjectPtr<UWidget> PreviewTag;

	TWeakObjectPtr<ATN_RacePodiumStage> Stage;
	TWeakObjectPtr<UTN_ScoreShellSynthComponent> Synth;
	FTNRaceChampionSetup ChampionSetup;
	TArray<FTNConfetti> Confetti;
	FSlateBrush ConfettiBrush;

	float Time = 0.f;
	float SecondsLeft = 0.f;
	float ConfettiDebt = 0.f;
	/** Tamaño de este widget (espacio local) del último fotograma. */
	FVector2D LocalSize = FVector2D(1920.f, 1080.f);
	/** El jugador local puede elegir (anfitrión) y ya ha elegido. */
	bool bCanChoose = false;
	bool bChoiceMade = false;
	/** El modo de entrada que se puso al abrir (para devolverlo al cerrar). */
	bool bInputTaken = false;
	/** Botón con el foco en el fotograma anterior (para el «pom» al moverlo con el mando). */
	int32 LastFocusedButton = INDEX_NONE;
	/** El próximo cambio de foco lo hace el código (al abrir o al apagarse un botón): sin «pom». */
	bool bQuietFocusChange = false;
};
