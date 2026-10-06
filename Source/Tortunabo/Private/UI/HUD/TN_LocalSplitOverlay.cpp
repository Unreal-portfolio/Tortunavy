#include "UI/HUD/TN_LocalSplitOverlay.h"
#include "TN_HUDArt.h"
#include "TN_HUDStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del bloque.
namespace TNLocalSplitOverlayDetail
{
	UTextBlock* Label(UWidgetTree* Tree, const FText& Text, FName Weight, int32 Size, const FLinearColor& Color)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		TNHUDStyle::StyleText(Block, Weight, Size, Color);
		Block->SetText(Text);
		Block->SetJustification(ETextJustify::Center);
		return Block;
	}

	/** Pone W en el lienzo con ancla y alineación en el mismo punto, a su tamaño. */
	UCanvasPanelSlot* Pin(UCanvasPanel* Canvas, UWidget* W, const FVector2D& Anchor, const FVector2D& Offset)
	{
		UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(W);
		CanvasSlot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
		CanvasSlot->SetAlignment(Anchor);
		CanvasSlot->SetPosition(Offset);
		CanvasSlot->SetAutoSize(true);
		return CanvasSlot;
	}
}

void UTN_LocalSplitOverlay::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// La raíz antes de entrar en pantalla (Docs: un widget de C++ con la raíz puesta tarde se queda vacío en Slate).
	Build();
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UTN_LocalSplitOverlay::Build()
{
	using namespace TNLocalSplitOverlayDetail;
	if (!WidgetTree || Root)
	{
		return;
	}
	UWidgetTree* Tree = WidgetTree;
	Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	Tree->RootWidget = Root;

	// El cuadrante libre (tres jugadores): un cartel azul marino que lo tapa entero.
	EmptyCover = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
	EmptyCover->SetBrush(TNHUDStyle::Rounded(TNHUDArt::NavyDeep, 0.f));
	EmptyCover->SetHorizontalAlignment(HAlign_Center);
	EmptyCover->SetVerticalAlignment(VAlign_Center);
	UVerticalBox* EmptyColumn = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	EmptyTitle = Label(Tree, NSLOCTEXT("TNLocal", "EmptyTitle", "¿Uno más?"), TEXT("Black"), 34, TNHUDArt::Gold);
	EmptyText = Label(Tree, NSLOCTEXT("TNLocal", "EmptyJoin", "Pulsa Start en otro mando para unirte"), TEXT("Bold"), 20, TNHUDArt::Foam);
	EmptyText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* TitleSlot = EmptyColumn->AddChildToVerticalBox(EmptyTitle)) { TitleSlot->SetHorizontalAlignment(HAlign_Center); }
	if (UVerticalBoxSlot* TextSlot = EmptyColumn->AddChildToVerticalBox(EmptyText))
	{
		TextSlot->SetHorizontalAlignment(HAlign_Center);
		TextSlot->SetPadding(FMargin(24.f, 8.f, 24.f, 0.f));
	}
	EmptyCover->SetContent(EmptyColumn);
	Root->AddChildToCanvas(EmptyCover);
	EmptyCover->SetVisibility(ESlateVisibility::Collapsed);

	// Por vista: su zona (se coloca en su trozo), con la etiqueta del número arriba y el aviso de salir abajo.
	for (int32 i = 0; i < TNLocalPlay::MaxPlayers; ++i)
	{
		UCanvasPanel* Area = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		Root->AddChildToCanvas(Area);
		Area->SetVisibility(ESlateVisibility::Collapsed);

		UBorder* Tag = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		TNHUDStyle::StylePanel(Tag, TNHUDStyle::Panel, 14.f, FMargin(18.f, 6.f, 18.f, 8.f));
		UTextBlock* TagText = Label(Tree, FText::GetEmpty(), TEXT("Black"), 22, TNHUDArt::Gold);
		Tag->SetContent(TagText);
		Pin(Area, Tag, FVector2D(0.5f, 0.f), FVector2D(0.f, 18.f));
		Tag->SetVisibility(ESlateVisibility::Collapsed);

		UBorder* Leave = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		TNHUDStyle::StylePanel(Leave, TNHUDStyle::Panel, 14.f, FMargin(18.f, 8.f, 18.f, 10.f), TNHUDStyle::Coral);
		UVerticalBox* LeaveColumn = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		LeaveColumn->AddChildToVerticalBox(Label(Tree, NSLOCTEXT("TNLocal", "Leaving", "Dejando la partida… suelta B para quedarte"), TEXT("Bold"), 18,
			TNHUDArt::Cream));
		UProgressBar* Bar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
		Bar->SetWidgetStyle(TNHUDStyle::Bar(5.f));
		Bar->SetFillColorAndOpacity(TNHUDArt::CoralLight);
		Bar->SetPercent(0.f);
		USizeBox* BarBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		BarBox->SetHeightOverride(10.f);
		BarBox->SetContent(Bar);
		if (UVerticalBoxSlot* BarSlot = LeaveColumn->AddChildToVerticalBox(BarBox))
		{
			BarSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
			BarSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		Leave->SetContent(LeaveColumn);
		Pin(Area, Leave, FVector2D(0.5f, 0.5f), FVector2D::ZeroVector);
		Leave->SetVisibility(ESlateVisibility::Collapsed);

		ViewAreas.Add(Area);
		ViewTags.Add(Tag);
		ViewTagTexts.Add(TagText);
		LeavePanels.Add(Leave);
		LeaveBars.Add(Bar);
	}

	// Cómo se une otro mando (en el lobby, con sitio y sin cuadrante libre que ya lo diga).
	JoinPill = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
	TNHUDStyle::StylePanel(JoinPill, TNHUDStyle::Panel, 14.f, FMargin(16.f, 5.f, 16.f, 7.f));
	JoinPill->SetContent(Label(Tree, NSLOCTEXT("TNLocal", "JoinHint", "Pulsa Start en otro mando para unirte (hasta 4)"), TEXT("Bold"), 17, TNHUDArt::Foam));
	Pin(Root, JoinPill, FVector2D(0.5f, 1.f), FVector2D(0.f, -14.f));
	JoinPill->SetVisibility(ESlateVisibility::Collapsed);

	// Un aviso suelto (con gafas, por qué no entra un invitado): arriba en el centro, ajustado al ancho de la pantalla.
	NoticePill = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
	TNHUDStyle::StylePanel(NoticePill, TNHUDStyle::Panel, 14.f, FMargin(20.f, 8.f, 20.f, 10.f), TNHUDStyle::Coral);
	NoticeText = Label(Tree, FText::GetEmpty(), TEXT("Bold"), 20, TNHUDArt::Cream);
	NoticeText->SetAutoWrapText(true);
	USizeBox* NoticeBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	NoticeBox->SetMaxDesiredWidth(720.f);
	NoticeBox->SetContent(NoticeText);
	NoticePill->SetContent(NoticeBox);
	Pin(Root, NoticePill, FVector2D(0.5f, 0.f), FVector2D(0.f, 24.f));
	NoticePill->SetVisibility(ESlateVisibility::Collapsed);
}

void UTN_LocalSplitOverlay::PlaceInRect(UWidget* Widget, const TNLocalPlay::FViewRect& Rect)
{
	if (UCanvasPanelSlot* CanvasSlot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr)
	{
		CanvasSlot->SetAnchors(FAnchors(Rect.X, Rect.Y, Rect.X + Rect.W, Rect.Y + Rect.H));
		CanvasSlot->SetOffsets(FMargin(0.f));
		CanvasSlot->SetAlignment(FVector2D::ZeroVector);
	}
}

void UTN_LocalSplitOverlay::Refresh(const FTNSplitOverlayState& State)
{
	if (!Root)
	{
		Build();
	}
	if (!Root)
	{
		return;
	}

	// El cuadrante libre.
	const int32 WantEmpty = State.bHasEmptyRect ? (State.bCanJoin ? 2 : 1) : 0;
	if (WantEmpty != ShownEmpty)
	{
		ShownEmpty = WantEmpty;
		EmptyCover->SetVisibility(WantEmpty > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (WantEmpty > 0)
		{
			PlaceInRect(EmptyCover, State.EmptyRect);
			EmptyTitle->SetText(WantEmpty == 2 ? NSLOCTEXT("TNLocal", "EmptyTitle", "¿Uno más?") : NSLOCTEXT("TNLocal", "EmptyGame", "Tortunavy"));
			EmptyText->SetVisibility(WantEmpty == 2 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	}

	// Cómo se une otro mando: con uno abajo en el centro; con dos, sobre la raya que los separa; con tres lo dice el cuadrante.
	const int32 NumViews = State.Views.Num();
	const int32 WantJoin = State.bCanJoin && !State.bHasEmptyRect && NumViews < TNLocalPlay::MaxPlayers ? NumViews : 0;
	if (WantJoin != ShownJoin)
	{
		ShownJoin = WantJoin;
		JoinPill->SetVisibility(WantJoin > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* PillSlot = Cast<UCanvasPanelSlot>(JoinPill->Slot))
		{
			const bool bOnSplit = WantJoin == 2;
			PillSlot->SetAnchors(FAnchors(0.5f, bOnSplit ? 0.5f : 1.f));
			PillSlot->SetAlignment(FVector2D(0.5f, bOnSplit ? 0.5f : 1.f));
			PillSlot->SetPosition(FVector2D(0.f, bOnSplit ? 0.f : -14.f));
		}
	}

	// El aviso suelto.
	const FString WantNotice = State.Notice.ToString();
	if (WantNotice != ShownNotice)
	{
		ShownNotice = WantNotice;
		NoticeText->SetText(State.Notice);
		NoticePill->SetVisibility(WantNotice.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	// Cada vista: su zona, su número y quién está saliendo.
	ShownRects.SetNum(TNLocalPlay::MaxPlayers);
	ShownTags.SetNum(TNLocalPlay::MaxPlayers);
	for (int32 i = 0; i < ViewAreas.Num(); ++i)
	{
		UCanvasPanel* Area = ViewAreas[i];
		const FTNSplitOverlayView* View = State.Views.IsValidIndex(i) ? &State.Views[i] : nullptr;
		const bool bShowTag = View && View->bShowTag;
		const bool bLeaving = View && View->LeaveProgress >= 0.f;
		if (!View || (!bShowTag && !bLeaving))
		{
			if (Area->GetVisibility() != ESlateVisibility::Collapsed) { Area->SetVisibility(ESlateVisibility::Collapsed); }
			continue;
		}
		if (Area->GetVisibility() != ESlateVisibility::HitTestInvisible) { Area->SetVisibility(ESlateVisibility::HitTestInvisible); }
		if (!(ShownRects[i] == View->Rect))
		{
			ShownRects[i] = View->Rect;
			PlaceInRect(Area, View->Rect);
		}
		const FText Tag = View->bCanLeave
			? FText::Format(NSLOCTEXT("TNLocal", "TagGuest", "Jugador {0} · mantén B para salir"), FText::AsNumber(View->PlayerNumber))
			: FText::Format(NSLOCTEXT("TNLocal", "Tag", "Jugador {0}"), FText::AsNumber(View->PlayerNumber));
		const FString TagKey = bShowTag ? Tag.ToString() : FString();
		if (TagKey != ShownTags[i])
		{
			ShownTags[i] = TagKey;
			ViewTagTexts[i]->SetText(Tag);
			ViewTags[i]->SetVisibility(bShowTag ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
		LeavePanels[i]->SetVisibility(bLeaving ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bLeaving) { LeaveBars[i]->SetPercent(FMath::Clamp(View->LeaveProgress, 0.f, 1.f)); }
	}
}
