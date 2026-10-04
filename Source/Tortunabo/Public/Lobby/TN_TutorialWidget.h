#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/TN_InputGlyphs.h"
#include "TN_TutorialWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UImage;
class UTextBlock;
class UTN_ButtonGlyphWidget;
class UVerticalBox;
class UWidget;

/** Una tarea del cartel: la tecla (o el botón) que se usa, qué hay que hacer y si ya está. */
struct FTNTutorialTaskView
{
	FText Key;
	/** Con mando, el botón que se dibuja en lugar del texto de la tecla (vacío: se enseña Key). */
	FKey PadKey;
	ETNPadFamily PadFamily = ETNPadFamily::Xbox;
	FText Text;
	bool bDone = false;
};

/** Lo que enseña el cartel del tutorial. */
struct FTNTutorialView
{
	int32 StationIndex = 0;
	int32 NumStations = 1;
	FText Title;
	FText Tip;
	TArray<FTNTutorialTaskView> Tasks;
	/** Cómo saltarlo (con la tecla del menú de pausa del jugador). */
	FText SkipHint;
};

/**
 * @brief Cartel del tutorial en el HUD (estilo Tortunavy, a la derecha): «TUTORIAL · 5 / 19», el nombre de la estación,
 * una línea por tarea con la tecla o el botón del jugador en una tecla dibujada y una casilla que se rellena al hacerla, un
 * consejo y cómo saltarlo. Al aprender una estación el cartel da un saltito y sale «¡Bien!» encima; al acabar, un mensaje
 * grande de despedida. No coge ni ratón ni teclado. Todo en código (raíz en NativeOnInitialized).
 */
UCLASS()
class TORTUNABO_API UTN_TutorialWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetView(const FTNTutorialView& InView);

	/** Saltito del cartel y un «¡Bien!» (o el texto que se pase) encima. */
	void Celebrate(const FText& Text);

	/** Mensaje final (tras la cascada o al saltarlo): el cartel se va y queda el mensaje unos segundos. */
	void ShowFarewell(const FText& Title, const FText& Subtitle);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Canvas;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Card;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HeaderText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> TaskBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TipText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SkipText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CheerText;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Farewell;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FarewellTitle;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FarewellText;

	/** Filas de tareas (una por tarea posible): casilla, tecla y texto. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> TaskChecks;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> TaskKeys;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> TaskKeyTexts;

	/** El botón del mando de cada tarea (#347): con mando sustituye a la tecla dibujada. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTN_ButtonGlyphWidget>> TaskKeyGlyphs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> TaskTexts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> TaskRows;

	FTNTutorialView View;
	TArray<bool> ShownDone;
	float Clock = 0.f;
	float PopAt = -10.f;
	float CheerAt = -10.f;
	float FarewellAt = -10.f;
	int32 ShownStation = INDEX_NONE;

	void BuildTree();
	void ApplyTaskStyle(int32 Index, bool bDone);
};
