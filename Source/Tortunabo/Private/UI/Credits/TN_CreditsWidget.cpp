#include "UI/Credits/TN_CreditsWidget.h"
#include "UI/Credits/TN_CreditsData.h"
#include "UI/Pause/TN_PauseMenuWidget.h"
#include "../HUD/TN_HUDArt.h"
#include "../HUD/TN_HUDStyle.h"
#include "Core/TN_LocText.h"
#include "Core/TN_Log.h"
#include "UI/TN_ScreenHost.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Console.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#if !UE_BUILD_SHIPPING
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"
#endif

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del bloque.
namespace TNCreditsUI
{
	/** Medidas a 1080 p de referencia, como el menú de pausa (TNPauseUI). */
	constexpr float DesignWidth = 1920.f;
	constexpr float DesignHeight = 1080.f;
	constexpr float CardWidth = 1240.f;
	constexpr float PageListHeight = 540.f;
	constexpr float ScreenListHeight = 720.f;
	constexpr float ValueWidth = 380.f;
	constexpr float Value2Width = 300.f;
	/** Desplazamiento por pulsación, por página (fracción de la lista visible) y con el stick (unidades por segundo). */
	constexpr float LineStep = 90.f;
	constexpr float PageFraction = 0.85f;
	constexpr float StickSpeed = 1100.f;
	constexpr float StickDeadZone = 0.2f;
	/** Pantalla completa: encima del menú principal y de sus pantallas de salas (capa 10). */
	constexpr int32 ScreenZOrder = 20;

	const FMargin CardMargin(0.16f, 0.2f, 0.16f, 0.34f);
	const FMargin RibbonMargin(0.14f, 0.f, 0.14f, 0.f);
	const FLinearColor RowFill(0.012f, 0.045f, 0.08f, 0.55f);
	const FLinearColor RowEdge(1.f, 1.f, 1.f, 0.08f);

	template <typename T>
	T* Make(UWidgetTree* Tree)
	{
		return Tree->ConstructWidget<T>(T::StaticClass());
	}

	UTextBlock* Label(UWidgetTree* Tree, const FText& Content, FName Weight, int32 FontSize, const FLinearColor& Color)
	{
		UTextBlock* Out = Make<UTextBlock>(Tree);
		Out->SetText(Content);
		TNHUDStyle::StyleText(Out, Weight, FontSize, Color);
		return Out;
	}

	FSlateBrush BoxBrush(UTexture2D* Tex, const FMargin& Margin)
	{
		FSlateBrush Brush;
		Brush.SetResourceObject(Tex);
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.Margin = Margin;
		if (Tex) { Brush.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY()); }
		return Brush;
	}

	USizeBox* Sized(UWidgetTree* Tree, UWidget* Content, float W, float H)
	{
		USizeBox* Out = Make<USizeBox>(Tree);
		if (W > 0.f) { Out->SetWidthOverride(W); }
		if (H > 0.f) { Out->SetHeightOverride(H); }
		if (Content) { Out->SetContent(Content); }
		return Out;
	}

	UCanvasPanelSlot* Pin(UCanvasPanel* Canvas, UWidget* W, const FVector2D& Anchor, const FVector2D& Offset)
	{
		UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(W);
		CanvasSlot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
		CanvasSlot->SetAlignment(Anchor);
		CanvasSlot->SetPosition(Offset);
		CanvasSlot->SetAutoSize(true);
		return CanvasSlot;
	}

	void Fill(UCanvasPanel* Canvas, UWidget* W)
	{
		UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(W);
		CanvasSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		CanvasSlot->SetOffsets(FMargin(0.f));
	}

	void AddV(UVerticalBox* Box, UWidget* W, const FMargin& Padding, EHorizontalAlignment H = HAlign_Fill)
	{
		UVerticalBoxSlot* VSlot = Box->AddChildToVerticalBox(W);
		VSlot->SetPadding(Padding);
		VSlot->SetHorizontalAlignment(H);
	}

	void AddH(UHorizontalBox* Box, UWidget* W, const FMargin& Padding, bool bFill = false)
	{
		UHorizontalBoxSlot* HSlot = Box->AddChildToHorizontalBox(W);
		HSlot->SetPadding(Padding);
		HSlot->SetVerticalAlignment(VAlign_Center);
		if (bFill) { HSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
	}

	/**
	 * Pantalla completa: velo a toda la pantalla y, encima, un lienzo de 1920 × 1080 que se encoge entero si no cabe (como el
	 * menú de pausa). Devuelve el lienzo.
	 */
	UCanvasPanel* BuildScreenCanvas(UWidgetTree* Tree)
	{
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree);
		Tree->RootWidget = Root;
		UImage* Veil = Make<UImage>(Tree);
		Veil->SetColorAndOpacity(TNHUDArt::Hex(0x0A1C38, 0.8f));
		Fill(Root, Veil);
		UCanvasPanel* Canvas = Make<UCanvasPanel>(Tree);
		UScaleBox* Fit = Make<UScaleBox>(Tree);
		Fit->SetStretch(EStretch::ScaleToFit);
		Fit->SetStretchDirection(EStretchDirection::DownOnly);
		Fit->SetContent(Sized(Tree, Canvas, DesignWidth, DesignHeight));
		Fill(Root, Fit);
		return Canvas;
	}

	/** Cinta coral con el título, como la de «PAUSA». */
	UBorder* Ribbon(UWidgetTree* Tree, const FText& Title)
	{
		UBorder* Out = Make<UBorder>(Tree);
		Out->SetBrush(BoxBrush(TNHUDArt::RibbonTexture(), RibbonMargin));
		Out->SetPadding(FMargin(76.f, 8.f, 76.f, 14.f));
		Out->SetHorizontalAlignment(HAlign_Center);
		Out->SetContent(Label(Tree, Title, TEXT("Black"), 34, FLinearColor::White));
		Out->SetRenderTransformAngle(-2.f);
		return Out;
	}

	bool IsKey(const FKey& Key, std::initializer_list<FKey> Keys)
	{
		for (const FKey& Option : Keys) { if (Key == Option) { return true; } }
		return false;
	}

	/** Colocación de un botón dentro de su caja (vertical u horizontal), para copiarla al botón nuevo. */
	struct FBoxSlotLayout
	{
		bool bValid = false;
		FMargin Padding;
		FSlateChildSize Size;
		EHorizontalAlignment Horizontal = HAlign_Fill;
		EVerticalAlignment Vertical = VAlign_Fill;
	};

	FBoxSlotLayout ReadLayout(const UPanelSlot* PanelSlot)
	{
		FBoxSlotLayout Layout;
		if (const UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(PanelSlot))
		{
			Layout = { true, VSlot->GetPadding(), VSlot->GetSize(), VSlot->GetHorizontalAlignment(), VSlot->GetVerticalAlignment() };
		}
		else if (const UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(PanelSlot))
		{
			Layout = { true, HSlot->GetPadding(), HSlot->GetSize(), HSlot->GetHorizontalAlignment(), HSlot->GetVerticalAlignment() };
		}
		return Layout;
	}

	void ApplyLayout(UPanelSlot* PanelSlot, const FBoxSlotLayout& Layout)
	{
		if (!Layout.bValid)
		{
			return;
		}
		if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(PanelSlot))
		{
			VSlot->SetPadding(Layout.Padding);
			VSlot->SetSize(Layout.Size);
			VSlot->SetHorizontalAlignment(Layout.Horizontal);
			VSlot->SetVerticalAlignment(Layout.Vertical);
		}
		else if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(PanelSlot))
		{
			HSlot->SetPadding(Layout.Padding);
			HSlot->SetSize(Layout.Size);
			HSlot->SetHorizontalAlignment(Layout.Horizontal);
			HSlot->SetVerticalAlignment(Layout.Vertical);
		}
	}

	/** Primer texto dentro de un botón del Blueprint (para copiar su estilo). */
	UTextBlock* FindLabel(UWidget* Widget)
	{
		if (UTextBlock* Text = Cast<UTextBlock>(Widget))
		{
			return Text;
		}
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
			{
				if (UTextBlock* Found = FindLabel(Panel->GetChildAt(ChildIndex))) { return Found; }
			}
		}
		return nullptr;
	}

	/** Mete Button delante de Before en su caja: UMG solo añade al final, así que lo que va desde Before se quita y se vuelve a poner. */
	bool InsertBefore(UButton* Button, UButton* Before, const FBoxSlotLayout& Layout)
	{
		UPanelWidget* Parent = Before ? Before->GetParent() : nullptr;
		if (!Parent || !(Parent->IsA<UVerticalBox>() || Parent->IsA<UHorizontalBox>()))
		{
			return false;
		}
		TArray<TPair<TWeakObjectPtr<UWidget>, FBoxSlotLayout>> Tail;
		for (int32 ChildIndex = Parent->GetChildIndex(Before); ChildIndex >= 0 && ChildIndex < Parent->GetChildrenCount(); ++ChildIndex)
		{
			UWidget* Child = Parent->GetChildAt(ChildIndex);
			Tail.Emplace(Child, ReadLayout(Child ? Child->Slot.Get() : nullptr));
		}
		for (const TPair<TWeakObjectPtr<UWidget>, FBoxSlotLayout>& Entry : Tail)
		{
			if (UWidget* Child = Entry.Key.Get()) { Parent->RemoveChild(Child); }
		}
		ApplyLayout(Parent->AddChild(Button), Layout);
		for (const TPair<TWeakObjectPtr<UWidget>, FBoxSlotLayout>& Entry : Tail)
		{
			if (UWidget* Child = Entry.Key.Get()) { ApplyLayout(Parent->AddChild(Child), Entry.Value); }
		}
		return true;
	}
}

UTN_CreditsWidget* UTN_CreditsWidget::CreatePage(UUserWidget* OwnerMenu)
{
	if (!OwnerMenu)
	{
		return nullptr;
	}
	UTN_CreditsWidget* Page = CreateWidget<UTN_CreditsWidget>(OwnerMenu, UTN_CreditsWidget::StaticClass());
	if (Page)
	{
		Page->BuildTree(false);
	}
	return Page;
}

UTN_CreditsWidget* UTN_CreditsWidget::OpenOver(APlayerController* PC, UUserWidget* Menu, UWidget* FocusBack)
{
	if (!PC || !PC->IsLocalController())
	{
		return nullptr;
	}
	UTN_CreditsWidget* Screen = CreateWidget<UTN_CreditsWidget>(PC, UTN_CreditsWidget::StaticClass());
	if (!Screen)
	{
		return nullptr;
	}
	Screen->BuildTree(true);
	Screen->FocusBackTarget = FocusBack;
	if (Menu)
	{
		// Escondido mientras tanto (como con las pantallas de salas): el mando no se escapa a sus botones.
		Screen->HiddenMenu = Menu;
		if (Menu->GetVisibility() != ESlateVisibility::Collapsed) { Screen->HiddenMenuVisibility = Menu->GetVisibility(); }
		Menu->SetVisibility(ESlateVisibility::Collapsed);
	}
	TNScreen::AddToScreen(Screen, TNCreditsUI::ScreenZOrder);
	Screen->FocusList();
	return Screen;
}

UButton* UTN_CreditsWidget::AddMenuButton(UWidgetTree* Tree, UButton* StyleSource, UButton* Before)
{
	using namespace TNCreditsUI;
	if (!Tree || !StyleSource || !Before)
	{
		return nullptr;
	}
	UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CreditsButton"));
	if (!Button)
	{
		return nullptr;
	}
	Button->SetStyle(StyleSource->GetStyle());
	Button->SetColorAndOpacity(StyleSource->GetColorAndOpacity());
	Button->SetBackgroundColor(StyleSource->GetBackgroundColor());
	Button->SetClickMethod(StyleSource->GetClickMethod());
	Button->SetTouchMethod(StyleSource->GetTouchMethod());
	Button->SetPressMethod(StyleSource->GetPressMethod());
	UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	if (const UTextBlock* Source = FindLabel(StyleSource))
	{
		Text->SetFont(Source->GetFont());
		Text->SetColorAndOpacity(Source->GetColorAndOpacity());
		Text->SetShadowOffset(Source->GetShadowOffset());
		Text->SetShadowColorAndOpacity(Source->GetShadowColorAndOpacity());
		Text->SetMinDesiredWidth(Source->GetMinDesiredWidth());
	}
	Text->SetText(NSLOCTEXT("TNCredits", "Button", "Créditos"));
	Button->SetContent(Text);

	if (!InsertBefore(Button, Before, ReadLayout(StyleSource->Slot)))
	{
		// Los botones no están en una caja: abajo en el centro, debajo de «Ajustes» (que va a -90).
		UCanvasPanel* Root = Cast<UCanvasPanel>(Tree->RootWidget);
		if (!Root)
		{
			return nullptr;
		}
		Pin(Root, Button, FVector2D(0.5f, 1.f), FVector2D(0.0, -30.0));
	}
	return Button;
}

UTexture2D* UTN_CreditsWidget::MenuIcon()
{
	return TNHUDArt::Cached(TEXT("PauseIconCredits"), []
	{
		// Estrella de tinta, como los demás iconos de la portada (TNPauseArt::MenuIcon).
		TNHUDArt::FPainter P(64, 64);
		const TArray<FVector2f> Star = TNHUDArt::StarPoints(32.f, 34.f, 24.f, 0.45f);
		P.Fill([&Star](float x, float y) { return TNHUDArt::Polygon(x, y, Star) - 1.5f; }, TNHUDArt::Ink);
		return P.ToTexture(TEXT("TN_PauseIconCredits"));
	});
}

void UTN_CreditsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// El foco se queda en este widget (la lista no tiene filas enfocables): así recibe las flechas y el mando.
	SetIsFocusable(true);
}

void UTN_CreditsWidget::BuildTree(bool bInStandalone)
{
	using namespace TNCreditsUI;
	if (bBuilt || !WidgetTree)
	{
		return;
	}
	bBuilt = true;
	bStandalone = bInStandalone;
	UWidgetTree* Tree = WidgetTree;
	if (!bStandalone)
	{
		Tree->RootWidget = BuildCard(PageListHeight);
		FillList();
		return;
	}

	UCanvasPanel* Canvas = BuildScreenCanvas(Tree);
	Pin(Canvas, Ribbon(Tree, NSLOCTEXT("TNCredits", "Title", "CRÉDITOS")), FVector2D(0.5f, 0.f), FVector2D(0.f, 36.f));
	Pin(Canvas, BuildCard(ScreenListHeight), FVector2D(0.5f, 0.f), FVector2D(0.f, 128.f));

	BackRow = CreateWidget<UTN_PauseRow>(this, UTN_PauseRow::StaticClass());
	if (BackRow)
	{
		TWeakObjectPtr<UTN_CreditsWidget> WeakThis(this);
		BackRow->SetupButton(ETNPauseRowStyle::Dialog, NSLOCTEXT("TNCredits", "Back", "Volver"),
			[WeakThis]() { if (UTN_CreditsWidget* Self = WeakThis.Get()) { Self->Close(); } });
		Pin(Canvas, BackRow, FVector2D(0.5f, 1.f), FVector2D(0.f, -78.f));
	}
	UTextBlock* Hint = Label(Tree, NSLOCTEXT("TNCredits", "HintScreen", "↑ ↓ · Rueda  Desplazar      Esc · B  Volver"), TEXT("Bold"), 16, TNHUDStyle::TextDim);
	Pin(Canvas, Hint, FVector2D(0.5f, 1.f), FVector2D(0.f, -24.f));

	FillList();
}

UWidget* UTN_CreditsWidget::BuildCard(float ListHeight)
{
	using namespace TNCreditsUI;
	UWidgetTree* Tree = WidgetTree;
	UVerticalBox* Column = Make<UVerticalBox>(Tree);
	if (!bStandalone)
	{
		// En la página, el título va en la tarjeta (la cinta de arriba es la de «PAUSA»).
		AddV(Column, Label(Tree, NSLOCTEXT("TNCredits", "Title", "CRÉDITOS"), TEXT("Black"), 28, TNHUDArt::Gold), FMargin(0.f, 0.f, 0.f, 10.f), HAlign_Center);
	}
	List = Make<UScrollBox>(Tree);
	FScrollBarStyle BarStyle = List->GetWidgetBarStyle();
	BarStyle.SetVerticalBackgroundImage(TNHUDStyle::Rounded(FLinearColor(0.f, 0.02f, 0.04f, 0.5f), 4.f));
	BarStyle.SetNormalThumbImage(TNHUDStyle::Rounded(TNHUDArt::Hex(0x62D2EA, 0.55f), 4.f));
	BarStyle.SetHoveredThumbImage(TNHUDStyle::Rounded(TNHUDArt::Hex(0x62D2EA, 0.85f), 4.f));
	BarStyle.SetDraggedThumbImage(TNHUDStyle::Rounded(TNHUDArt::Gold, 4.f));
	List->SetWidgetBarStyle(BarStyle);
	List->SetScrollbarThickness(FVector2D(8.f, 8.f));
	List->SetScrollbarPadding(FMargin(8.f, 0.f, 0.f, 0.f));
	List->SetAlwaysShowScrollbar(true);
	AddV(Column, Sized(Tree, List, 0.f, ListHeight), FMargin(0.f));

	UBorder* Card = Make<UBorder>(Tree);
	Card->SetBrush(BoxBrush(TNHUDArt::CardTexture(), CardMargin));
	Card->SetPadding(FMargin(34.f, 22.f, 34.f, 48.f));
	Card->SetContent(Column);
	return Sized(Tree, Card, CardWidth, 0.f);
}

void UTN_CreditsWidget::FillList()
{
	if (!List)
	{
		return;
	}
	List->ClearChildren();
	SectionCount = 0;

	FTNCreditsData Data;
	FString Error;
	const FString Path = TNCredits::DefaultPath();
	if (!TNCredits::LoadFile(Path, Data, Error))
	{
		UE_LOG(LogTortunabo, Error, TEXT("[Créditos] No se han podido cargar %s: %s"), *Path, *Error);
		AddNote(NSLOCTEXT("TNCredits", "Missing", "No se han podido cargar los créditos."), 18);
		return;
	}
	for (const FTNCreditsSection& Section : Data.Sections)
	{
		AddHeader(TNCredits::SectionTitle(Section.Id));
		for (const FTNCreditsEntry& Entry : Section.Entries)
		{
			TArray<FText> Roles;
			for (const FString& Role : Entry.Roles) { Roles.Add(TNCredits::RoleName(Role)); }
			// Con papeles (unidos con « · », que vale en todos los idiomas): papel y origen o licencia; sin ellos (fuentes, motor): origen y
			// licencia. Los datos no se traducen.
			const FText Value = Roles.Num() > 0 ? FText::Join(INVTEXT(" · "), Roles) : TNLocText::Literal(Entry.Source);
			const FString Value2 = !Entry.License.IsEmpty() ? Entry.License : (Roles.Num() > 0 ? Entry.Source : FString());
			AddEntryRow(TNLocText::Literal(Entry.Name), Value, TNLocText::Literal(Value2));
		}
		for (const FString& Note : Section.Notes)
		{
			AddNote(TNLocText::Literal(Note), 17);
		}
		if (!Section.LicenseText.IsEmpty())
		{
			AddNote(TNLocText::Literal(Section.LicenseText), 14);
		}
		++SectionCount;
	}
	// Margen al final: la ola del borde de la tarjeta no tapa la última línea.
	List->AddChild(TNCreditsUI::Sized(WidgetTree, nullptr, 0.f, 36.f));
}

void UTN_CreditsWidget::AddHeader(const FText& Title)
{
	UTextBlock* Header = TNCreditsUI::Label(WidgetTree, Title, TEXT("Black"), 20, TNHUDArt::Gold);
	if (UScrollBoxSlot* HeaderSlot = Cast<UScrollBoxSlot>(List->AddChild(Header)))
	{
		HeaderSlot->SetPadding(FMargin(8.f, List->GetChildrenCount() > 1 ? 18.f : 0.f, 0.f, 6.f));
	}
}

void UTN_CreditsWidget::AddEntryRow(const FText& Name, const FText& Value, const FText& Value2)
{
	using namespace TNCreditsUI;
	UWidgetTree* Tree = WidgetTree;
	UHorizontalBox* Line = Make<UHorizontalBox>(Tree);
	UTextBlock* NameText = Label(Tree, Name, TEXT("Bold"), 20, TNHUDStyle::Text);
	NameText->SetAutoWrapText(true);
	AddH(Line, NameText, FMargin(0.f, 0.f, 12.f, 0.f), true);
	UTextBlock* ValueText = Label(Tree, Value, TEXT("Bold"), 18, TNHUDArt::SandC);
	ValueText->SetAutoWrapText(true);
	AddH(Line, Sized(Tree, ValueText, ValueWidth, 0.f), FMargin(0.f, 0.f, 12.f, 0.f));
	UTextBlock* Value2Text = Label(Tree, Value2, TEXT("Bold"), 18, TNHUDArt::SeaLight);
	Value2Text->SetAutoWrapText(true);
	AddH(Line, Sized(Tree, Value2Text, Value2Width, 0.f), FMargin(0.f));

	UBorder* Row = Make<UBorder>(Tree);
	Row->SetBrush(TNHUDStyle::Rounded(RowFill, 10.f, RowEdge, 1.f));
	Row->SetPadding(FMargin(20.f, 12.f, 16.f, 12.f));
	Row->SetVerticalAlignment(VAlign_Center);
	Row->SetContent(Line);
	if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(List->AddChild(Row)))
	{
		RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		RowSlot->SetHorizontalAlignment(HAlign_Fill);
	}
}

void UTN_CreditsWidget::AddNote(const FText& Note, int32 FontSize)
{
	UTextBlock* Text = TNCreditsUI::Label(WidgetTree, Note, TEXT("Regular"), FontSize, TNHUDStyle::TextDim);
	Text->SetAutoWrapText(true);
	if (UScrollBoxSlot* NoteSlot = Cast<UScrollBoxSlot>(List->AddChild(Text)))
	{
		NoteSlot->SetPadding(FMargin(10.f, 4.f, 10.f, 8.f));
	}
}

void UTN_CreditsWidget::FocusList()
{
	SetKeyboardFocus();
}

void UTN_CreditsWidget::Close()
{
	if (!bStandalone || bClosing)
	{
		return;
	}
	bClosing = true;
	StickScroll = 0.f;
	if (UUserWidget* Menu = HiddenMenu.Get()) { Menu->SetVisibility(HiddenMenuVisibility); }
	UWidget* Back = FocusBackTarget.Get();
	RemoveFromParent();
	if (Back) { Back->SetKeyboardFocus(); }
}

void UTN_CreditsWidget::ScrollBy(float Delta)
{
	if (!List)
	{
		return;
	}
	const float End = List->GetScrollOffsetOfEnd();
	float Target = FMath::Max(0.f, List->GetScrollOffset() + Delta);
	// Antes de la primera maquetación el final aún vale 0: sin tope por arriba hasta entonces.
	if (End > 0.f) { Target = FMath::Min(Target, End); }
	List->SetScrollOffset(Target);
}

void UTN_CreditsWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (StickScroll != 0.f)
	{
		// Stick hacia arriba (positivo): la lista sube.
		ScrollBy(-StickScroll * TNCreditsUI::StickSpeed * InDeltaTime);
	}
	if (!bStandalone || bClosing || HasAnyUserFocus() || HasFocusedDescendants())
	{
		return;
	}
	// Pantalla completa sin el foco (clic fuera de la ventana, otra cosa que lo pidió): vuelve aquí, salvo con la consola abierta.
	const UWorld* World = GetWorld();
	const UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	const bool bConsoleOpen = Viewport && Viewport->ViewportConsole && Viewport->ViewportConsole->ConsoleActive();
	if (!bConsoleOpen)
	{
		FocusList();
	}
}

bool UTN_CreditsWidget::HandleScrollKey(const FKey& Key)
{
	using TNCreditsUI::IsKey;
	const float Page = List ? FMath::Max(TNCreditsUI::LineStep, List->GetCachedGeometry().GetLocalSize().Y * TNCreditsUI::PageFraction)
		: TNCreditsUI::LineStep;
	if (IsKey(Key, { EKeys::Up, EKeys::W, EKeys::Gamepad_DPad_Up })) { ScrollBy(-TNCreditsUI::LineStep); return true; }
	if (IsKey(Key, { EKeys::Down, EKeys::S, EKeys::Gamepad_DPad_Down })) { ScrollBy(TNCreditsUI::LineStep); return true; }
	if (IsKey(Key, { EKeys::PageUp, EKeys::Gamepad_LeftShoulder })) { ScrollBy(-Page); return true; }
	if (IsKey(Key, { EKeys::PageDown, EKeys::Gamepad_RightShoulder })) { ScrollBy(Page); return true; }
	if (Key == EKeys::Home && List) { List->ScrollToStart(); return true; }
	if (Key == EKeys::End && List) { List->ScrollToEnd(); return true; }
	// Los sticks desplazan con su inclinación (NativeOnAnalogValueChanged); izquierda y derecha no hacen nada aquí.
	return IsKey(Key, { EKeys::Gamepad_LeftStick_Up, EKeys::Gamepad_LeftStick_Down, EKeys::Gamepad_RightStick_Up, EKeys::Gamepad_RightStick_Down,
		EKeys::Gamepad_LeftStick_Left, EKeys::Gamepad_LeftStick_Right, EKeys::Gamepad_RightStick_Left, EKeys::Gamepad_RightStick_Right,
		EKeys::Left, EKeys::Right, EKeys::A, EKeys::D, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right });
}

FReply UTN_CreditsWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (TNCreditsUI::IsKey(Key, { EKeys::Escape, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_Special_Left, EKeys::Virtual_Back,
		EKeys::BackSpace, EKeys::Tab, EKeys::Gamepad_Special_Right }))
	{
		// En la página, volver y cerrar los atiende el menú de pausa.
		if (!bStandalone)
		{
			return FReply::Unhandled();
		}
		if (!InKeyEvent.IsRepeat()) { Close(); }
		return FReply::Handled();
	}
	if (HandleScrollKey(Key))
	{
		return FReply::Handled();
	}
	if (!bStandalone || GetDefault<UInputSettings>()->ConsoleKeys.Contains(Key))
	{
		return FReply::Unhandled();
	}
	// Pantalla completa: lo demás se queda aquí (Intro y A ya los atiende «Volver» si tiene el foco).
	return FReply::Handled();
}

FReply UTN_CreditsWidget::NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent)
{
	const FKey Key = InAnalogEvent.GetKey();
	if (Key == EKeys::Gamepad_LeftY || Key == EKeys::Gamepad_RightY)
	{
		const float Value = InAnalogEvent.GetAnalogValue();
		StickScroll = FMath::Abs(Value) > TNCreditsUI::StickDeadZone ? Value : 0.f;
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_LeftX || Key == EKeys::Gamepad_RightX)
	{
		return FReply::Handled();
	}
	return bStandalone ? FReply::Handled() : FReply::Unhandled();
}

FReply UTN_CreditsWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Un clic en la lista (no en su barra ni en «Volver», que lo atienden ellos) devuelve el foco aquí.
	return FReply::Handled().SetUserFocus(TakeWidget(), EFocusCause::Mouse);
}

void UTN_CreditsWidget::NativeOnFocusLost(const FFocusEvent& InFocusEvent)
{
	Super::NativeOnFocusLost(InFocusEvent);
	StickScroll = 0.f;
}

#if !UE_BUILD_SHIPPING
namespace TNCreditsDebug
{
	/**
	 * TN.Credits.Open [Desplazamiento] [Captura]: abre los créditos como el botón del menú principal (si lo hay; si no, encima de lo
	 * que haya), los desplaza y, con una ruta, guarda una captura de la ventana con la interfaz (para revisar el aspecto).
	 */
	/** El mismo camino que un clic: pulsa el botón «CreditsButton» de un menú en pantalla. false si no hay ninguno. */
	bool ClickMenuButton(UWorld* World)
	{
		TArray<UUserWidget*> Menus;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Menus, UUserWidget::StaticClass(), true);
		for (UUserWidget* Menu : Menus)
		{
			if (UButton* Button = Menu && Menu->WidgetTree ? Cast<UButton>(Menu->WidgetTree->FindWidget(TEXT("CreditsButton"))) : nullptr)
			{
				Button->OnClicked.Broadcast();
				return true;
			}
		}
		return false;
	}

	void Open(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!PC)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Créditos] TN.Credits.Open: no hay jugador local."));
			return;
		}
		if (!ClickMenuButton(World))
		{
			UTN_CreditsWidget::OpenOver(PC, nullptr, nullptr);
		}
		TArray<UUserWidget*> Opened;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Opened, UTN_CreditsWidget::StaticClass(), false);
		TWeakObjectPtr<UTN_CreditsWidget> Screen = Opened.Num() > 0 ? Cast<UTN_CreditsWidget>(Opened.Last()) : nullptr;
		const float Scroll = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.f;
		const FString ShotPath = Args.Num() > 1 ? Args[1] : FString();
		// Se espera a que la lista esté maquetada para desplazarla y, para la captura, a que acabe la pantalla de carga («¡PUM!»).
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Screen, Scroll](float)
		{
			if (UTN_CreditsWidget* Credits = Screen.Get()) { Credits->ScrollBy(Scroll); }
			return false;
		}), 1.f);
		if (!ShotPath.IsEmpty())
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([ShotPath](float)
			{
				FScreenshotRequest::RequestScreenshot(ShotPath, true, false);
				UE_LOG(LogTortunabo, Log, TEXT("[Créditos] Captura pedida: %s"), *ShotPath);
				return false;
			}), 6.f);
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs OpenCommand(
		TEXT("TN.Credits.Open"),
		TEXT("Abre la pantalla de créditos. Uso: TN.Credits.Open [Desplazamiento en unidades] [Ruta de la captura]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Open));
}
#endif
