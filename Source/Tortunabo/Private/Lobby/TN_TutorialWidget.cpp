#include "Lobby/TN_TutorialWidget.h"
#include "../UI/HUD/TN_HUDStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UI/HUD/TN_ButtonGlyphWidget.h"

// Con nombre (no anónimo): en la compilación por bloques los nombres de un espacio anónimo se ven en el resto del bloque.
namespace TNTutorialWidgetDetail
{
	/** Ancho del cartel (px a 1080 p) y hueco con el borde derecho de la pantalla. */
	constexpr float CardWidth = 470.f;
	constexpr float RightMargin = 28.f;
	/** Tareas que caben en el cartel (las estaciones tienen una o dos). */
	constexpr int32 MaxRows = 3;
	/** Alto del botón del mando dibujado en cada tarea (px), como la tecla dibujada. */
	constexpr float TaskGlyphHeight = 32.f;
	/** Saltito del cartel al aprender algo, y cuánto se ve el «¡Bien!». */
	constexpr float PopSeconds = 0.45f;
	constexpr float CheerSeconds = 1.6f;
	/** Cuánto se queda el mensaje final. */
	constexpr float FarewellSeconds = 6.f;

	/** Crea un widget del árbol (la raíz ya está puesta: no se queda vacío en Slate). */
	template <typename T>
	T* Make(UWidgetTree* Tree)
	{
		return Tree->ConstructWidget<T>(T::StaticClass());
	}

	UTextBlock* MakeText(UWidgetTree* Tree, FName Weight, int32 Size, const FLinearColor& Color, bool bOutline = true)
	{
		UTextBlock* T = Make<UTextBlock>(Tree);
		TNHUDStyle::StyleText(T, Weight, Size, Color, bOutline);
		return T;
	}

	/** Tecla dibujada: crema con el filo azul marino y la letra oscura. */
	FSlateBrush KeyBrush()
	{
		return TNHUDStyle::Rounded(FLinearColor(1.f, 0.985f, 0.94f, 0.96f), 8.f, FLinearColor(0.07f, 0.19f, 0.35f, 1.f), 2.f);
	}

	FSlateBrush CheckBrush(bool bDone)
	{
		return bDone ? TNHUDStyle::Rounded(TNHUDStyle::Accent, 7.f, FLinearColor(1.f, 1.f, 1.f, 0.9f), 2.f)
			: TNHUDStyle::Rounded(FLinearColor(0.f, 0.f, 0.f, 0.15f), 7.f, FLinearColor(1.f, 1.f, 1.f, 0.7f), 2.f);
	}

	/** Rebote de 0 a 1 (sube y vuelve) para el saltito. */
	float Pop(float T)
	{
		if (T < 0.f || T > 1.f) { return 0.f; }
		return FMath::Sin(T * UE_PI) * (1.f - T * 0.4f);
	}
}

void UTN_TutorialWidget::NativeOnInitialized()
{
	BuildTree();
	Super::NativeOnInitialized();
}

void UTN_TutorialWidget::BuildTree()
{
	using namespace TNTutorialWidgetDetail;
	if (!WidgetTree || Canvas)
	{
		return;
	}
	UWidgetTree* Tree = WidgetTree;
	// La raíz, lo primero: si el widget entra en un panel antes de tenerla, se queda vacío (Docs/Menu_Pausa.md).
	Canvas = Make<UCanvasPanel>(Tree);
	Tree->RootWidget = Canvas;
	SetVisibility(ESlateVisibility::HitTestInvisible);

	// ── Cartel de la derecha ──
	UVerticalBox* Column = Make<UVerticalBox>(Tree);
	HeaderText = MakeText(Tree, TEXT("Bold"), 14, TNHUDStyle::Sand);
	Column->AddChildToVerticalBox(HeaderText);
	TitleText = MakeText(Tree, TEXT("Black"), 30, TNHUDStyle::Text);
	TitleText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(TitleText)) { S->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f)); }

	TaskBox = Make<UVerticalBox>(Tree);
	for (int32 i = 0; i < MaxRows; ++i)
	{
		UHorizontalBox* Row = Make<UHorizontalBox>(Tree);
		// Casilla.
		USizeBox* CheckSize = Make<USizeBox>(Tree);
		CheckSize->SetWidthOverride(24.f);
		CheckSize->SetHeightOverride(24.f);
		UBorder* Check = Make<UBorder>(Tree);
		Check->SetBrush(CheckBrush(false));
		CheckSize->SetContent(Check);
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(CheckSize))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
		}
		// Tecla.
		UBorder* Key = Make<UBorder>(Tree);
		Key->SetBrush(KeyBrush());
		Key->SetPadding(FMargin(9.f, 3.f, 9.f, 4.f));
		UTextBlock* KeyText = MakeText(Tree, TEXT("Bold"), 16, FLinearColor(0.05f, 0.12f, 0.24f, 1.f), false);
		KeyText->SetShadowOffset(FVector2D::ZeroVector);
		Key->SetContent(KeyText);
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Key))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
		}
		// Con mando, el botón dibujado en lugar de la tecla (SetView elige cuál se ve).
		UTN_ButtonGlyphWidget* Glyph = Make<UTN_ButtonGlyphWidget>(Tree);
		Glyph->SetGlyphHeight(TaskGlyphHeight);
		Glyph->SetVisibility(ESlateVisibility::Collapsed);
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Glyph))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
		}
		// Qué hay que hacer.
		UTextBlock* Text = MakeText(Tree, TEXT("Bold"), 18, TNHUDStyle::Text);
		Text->SetAutoWrapText(true);
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Text))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		if (UVerticalBoxSlot* S = TaskBox->AddChildToVerticalBox(Row)) { S->SetPadding(FMargin(0.f, 3.f, 0.f, 5.f)); }
		TaskRows.Add(Row);
		TaskChecks.Add(Check);
		TaskKeys.Add(Key);
		TaskKeyTexts.Add(KeyText);
		TaskKeyGlyphs.Add(Glyph);
		TaskTexts.Add(Text);
		ShownDone.Add(false);
	}
	Column->AddChildToVerticalBox(TaskBox);

	TipText = MakeText(Tree, TEXT("Regular"), 16, TNHUDStyle::TextDim);
	TipText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(TipText)) { S->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f)); }
	SkipText = MakeText(Tree, TEXT("Regular"), 13, FLinearColor(0.62f, 0.72f, 0.8f, 0.9f));
	SkipText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(SkipText)) { S->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f)); }

	USizeBox* Width = Make<USizeBox>(Tree);
	Width->SetWidthOverride(CardWidth);
	Width->SetContent(Column);
	Card = Make<UBorder>(Tree);
	TNHUDStyle::StylePanel(Card, TNHUDStyle::Panel, 18.f, FMargin(22.f, 16.f, 22.f, 18.f), TNHUDStyle::Edge, 2.f);
	Card->SetContent(Width);
	Card->SetRenderTransformPivot(FVector2D(1.f, 0.5f));
	if (UCanvasPanelSlot* S = Canvas->AddChildToCanvas(Card))
	{
		S->SetAnchors(FAnchors(1.f, 0.5f));
		S->SetAlignment(FVector2D(1.f, 0.5f));
		S->SetPosition(FVector2D(-RightMargin, 30.f));
		S->SetAutoSize(true);
	}

	// ── «¡Bien!» encima del cartel ──
	CheerText = MakeText(Tree, TEXT("Black"), 34, TNHUDStyle::Sand);
	CheerText->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	CheerText->SetRenderOpacity(0.f);
	if (UCanvasPanelSlot* S = Canvas->AddChildToCanvas(CheerText))
	{
		S->SetAnchors(FAnchors(1.f, 0.5f));
		S->SetAlignment(FVector2D(0.5f, 1.f));
		S->SetPosition(FVector2D(-RightMargin - CardWidth * 0.5f - 22.f, -190.f));
		S->SetAutoSize(true);
	}

	// ── Mensaje final, arriba en el centro ──
	UVerticalBox* FarewellColumn = Make<UVerticalBox>(Tree);
	FarewellTitle = MakeText(Tree, TEXT("Black"), 44, TNHUDStyle::Sand);
	FarewellTitle->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* S = FarewellColumn->AddChildToVerticalBox(FarewellTitle)) { S->SetHorizontalAlignment(HAlign_Center); }
	FarewellText = MakeText(Tree, TEXT("Bold"), 21, TNHUDStyle::Text);
	FarewellText->SetJustification(ETextJustify::Center);
	FarewellText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* S = FarewellColumn->AddChildToVerticalBox(FarewellText))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	}
	USizeBox* FarewellWidth = Make<USizeBox>(Tree);
	FarewellWidth->SetWidthOverride(760.f);
	FarewellWidth->SetContent(FarewellColumn);
	Farewell = Make<UBorder>(Tree);
	TNHUDStyle::StylePanel(Farewell, TNHUDStyle::Panel, 24.f, FMargin(34.f, 20.f, 34.f, 24.f), TNHUDStyle::Sand, 2.5f);
	Farewell->SetContent(FarewellWidth);
	Farewell->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	Farewell->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* S = Canvas->AddChildToCanvas(Farewell))
	{
		S->SetAnchors(FAnchors(0.5f, 0.f));
		S->SetAlignment(FVector2D(0.5f, 0.f));
		S->SetPosition(FVector2D(0.f, 150.f));
		S->SetAutoSize(true);
	}
}

void UTN_TutorialWidget::ApplyTaskStyle(int32 Index, bool bDone)
{
	if (!TaskChecks.IsValidIndex(Index))
	{
		return;
	}
	TaskChecks[Index]->SetBrush(TNTutorialWidgetDetail::CheckBrush(bDone));
	TaskTexts[Index]->SetColorAndOpacity(FSlateColor(bDone ? TNHUDStyle::TextDim : TNHUDStyle::Text));
	TaskKeys[Index]->SetRenderOpacity(bDone ? 0.55f : 1.f);
	if (TaskKeyGlyphs.IsValidIndex(Index)) { TaskKeyGlyphs[Index]->SetRenderOpacity(bDone ? 0.55f : 1.f); }
}

void UTN_TutorialWidget::SetView(const FTNTutorialView& InView)
{
	BuildTree();
	View = InView;
	if (!HeaderText)
	{
		return;
	}
	HeaderText->SetText(FText::Format(NSLOCTEXT("TNTutorial", "Header", "TUTORIAL · {0} / {1}"),
		FText::AsNumber(View.StationIndex + 1), FText::AsNumber(View.NumStations)));
	TitleText->SetText(View.Title);
	TipText->SetText(View.Tip);
	TipText->SetVisibility(View.Tip.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	SkipText->SetText(View.SkipHint);
	const bool bNewStation = ShownStation != View.StationIndex;
	ShownStation = View.StationIndex;
	for (int32 i = 0; i < TaskRows.Num(); ++i)
	{
		const bool bShow = View.Tasks.IsValidIndex(i);
		TaskRows[i]->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (!bShow)
		{
			continue;
		}
		const FTNTutorialTaskView& Task = View.Tasks[i];
		TaskTexts[i]->SetText(Task.Text);
		TaskKeyTexts[i]->SetText(Task.Key);
		const bool bGlyph = Task.PadKey.IsValid() && TaskKeyGlyphs[i]->SetKey(Task.PadKey, Task.PadFamily);
		TaskKeyGlyphs[i]->SetVisibility(bGlyph ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		TaskKeys[i]->SetVisibility(bGlyph || Task.Key.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		if (bNewStation || ShownDone[i] != Task.bDone)
		{
			ShownDone[i] = Task.bDone;
			ApplyTaskStyle(i, Task.bDone);
		}
	}
	if (bNewStation)
	{
		// Estación nueva: el cartel entra con un saltito.
		PopAt = Clock;
	}
	if (Card && Farewell && Farewell->GetVisibility() == ESlateVisibility::Collapsed)
	{
		Card->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UTN_TutorialWidget::Celebrate(const FText& Text)
{
	BuildTree();
	PopAt = Clock;
	CheerAt = Clock;
	if (CheerText)
	{
		CheerText->SetText(Text);
	}
}

void UTN_TutorialWidget::ShowFarewell(const FText& Title, const FText& Subtitle)
{
	BuildTree();
	if (!Farewell)
	{
		return;
	}
	FarewellTitle->SetText(Title);
	FarewellText->SetText(Subtitle);
	Farewell->SetVisibility(ESlateVisibility::HitTestInvisible);
	Card->SetVisibility(ESlateVisibility::Collapsed);
	FarewellAt = Clock;
}

void UTN_TutorialWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	using namespace TNTutorialWidgetDetail;
	Super::NativeTick(MyGeometry, InDeltaTime);
	Clock += InDeltaTime;
	if (Card)
	{
		const float P = Pop((Clock - PopAt) / PopSeconds);
		Card->SetRenderScale(FVector2D(1.f + 0.05f * P, 1.f + 0.05f * P));
	}
	if (CheerText)
	{
		const float T = (Clock - CheerAt) / CheerSeconds;
		if (T >= 0.f && T <= 1.f)
		{
			const float In = FMath::Clamp(T / 0.15f, 0.f, 1.f);
			const float Out = 1.f - FMath::Clamp((T - 0.7f) / 0.3f, 0.f, 1.f);
			CheerText->SetRenderOpacity(In * Out);
			const float S = 0.7f + 0.3f * In + 0.08f * Pop(FMath::Clamp(T / 0.35f, 0.f, 1.f));
			CheerText->SetRenderScale(FVector2D(S, S));
			CheerText->SetRenderTranslation(FVector2D(0.f, -24.f * T));
		}
		else if (CheerText->GetRenderOpacity() > 0.f)
		{
			CheerText->SetRenderOpacity(0.f);
		}
	}
	if (Farewell && Farewell->GetVisibility() != ESlateVisibility::Collapsed)
	{
		const float T = Clock - FarewellAt;
		const float In = FMath::Clamp(T / 0.35f, 0.f, 1.f);
		const float Out = 1.f - FMath::Clamp((T - (FarewellSeconds - 0.6f)) / 0.6f, 0.f, 1.f);
		Farewell->SetRenderOpacity(In * Out);
		const float S = 0.85f + 0.15f * In + 0.06f * Pop(FMath::Clamp(T / 0.5f, 0.f, 1.f));
		Farewell->SetRenderScale(FVector2D(S, S));
		if (T > FarewellSeconds)
		{
			Farewell->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}
