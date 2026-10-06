#include "UI/Menu/TN_RoomMenuWidget.h"
#include "TN_RoomArt.h"
#include "../HUD/TN_HUDArt.h"
#include "../HUD/TN_HUDStyle.h"
#include "Audio/TN_ScoreShellSynthComponent.h"
#include "Core/TN_LocText.h"
#include "Lobby/TN_LobbyMission.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Multiplayer/TN_RoomNames.h"
#include "Multiplayer/TN_SteamGamepadInput.h"
#include "VR/TN_VRMenuClaim.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Engine/Console.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputCoreTypes.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SWindow.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto
// del bloque. Las mismas piezas que el menú de pausa (TNPauseUI), con las mismas medidas.
namespace TNRoomUI
{
	/** Lienzo de diseño: todo cabe aquí y se encoge entero si la pantalla (en unidades de interfaz) es menor. */
	constexpr float DesignWidth = 1920.f;
	constexpr float DesignHeight = 1080.f;
	constexpr float CreateCardWidth = 1120.f;
	constexpr float JoinCardWidth = 1240.f;
	constexpr float RoomListHeight = 390.f;
	/** Ancho de la franja de abajo (aviso y ayuda): sus textos se parten en líneas a este ancho. */
	constexpr float FooterWidth = 1180.f;
	constexpr float CellWidth = 58.f;
	constexpr float CellHeight = 70.f;
	constexpr float CellGap = 8.f;
	/** Segundos entre búsquedas automáticas de la lista con «Unirse» abierta. */
	constexpr float AutoRefreshSeconds = 20.f;

	const FMargin CardMargin(0.16f, 0.2f, 0.16f, 0.34f);
	const FMargin RibbonMargin(0.14f, 0.f, 0.14f, 0.f);

	template <typename T>
	T* Make(UWidgetTree* Tree)
	{
		return Tree->ConstructWidget<T>(T::StaticClass());
	}

	UTextBlock* Label(UWidgetTree* Tree, const FText& Content, FName Weight, int32 FontSize, const FLinearColor& Color, bool bOutline = true)
	{
		UTextBlock* Out = Make<UTextBlock>(Tree);
		Out->SetText(Content);
		TNHUDStyle::StyleText(Out, Weight, FontSize, Color, bOutline);
		if (!bOutline) { Out->SetShadowColorAndOpacity(FLinearColor::Transparent); }
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

	/**
	 * Texto centrado que se parte en líneas a lo ancho de la franja de abajo. Con el ancho fijo (WrapTextAt), su alto es el
	 * de todas sus líneas desde el primer fotograma, sin esperar a que lo coloquen.
	 */
	UTextBlock* WrappedLabel(UWidgetTree* Tree, FName Weight, int32 FontSize, const FLinearColor& Color)
	{
		UTextBlock* Out = Label(Tree, FText::GetEmpty(), Weight, FontSize, Color);
		Out->SetJustification(ETextJustify::Center);
		Out->SetAutoWrapText(true);
		Out->SetWrapTextAt(FooterWidth);
		return Out;
	}

	USizeBox* Sized(UWidgetTree* Tree, UWidget* Content, float W, float H)
	{
		USizeBox* Out = Make<USizeBox>(Tree);
		if (W > 0.f) { Out->SetWidthOverride(W); }
		if (H > 0.f) { Out->SetHeightOverride(H); }
		if (Content) { Out->SetContent(Content); }
		return Out;
	}

	UBorder* Card(UWidgetTree* Tree, UWidget* Content, const FMargin& Padding)
	{
		UBorder* Out = Make<UBorder>(Tree);
		Out->SetBrush(BoxBrush(TNHUDArt::CardTexture(), CardMargin));
		Out->SetPadding(Padding);
		Out->SetHorizontalAlignment(HAlign_Fill);
		Out->SetVerticalAlignment(VAlign_Fill);
		if (Content) { Out->SetContent(Content); }
		return Out;
	}

	/** Caja de 1920 × 1080 que se encoge entera si no cabe (interfaz grande o ventana pequeña). */
	UScaleBox* Fit(UWidgetTree* Tree, UWidget* Content)
	{
		UScaleBox* Box = Make<UScaleBox>(Tree);
		Box->SetStretch(EStretch::ScaleToFit);
		Box->SetStretchDirection(EStretchDirection::DownOnly);
		Box->SetContent(Sized(Tree, Content, DesignWidth, DesignHeight));
		return Box;
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

	UCanvasPanelSlot* Fill(UCanvasPanel* Canvas, UWidget* W)
	{
		UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(W);
		CanvasSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		CanvasSlot->SetOffsets(FMargin(0.f));
		return CanvasSlot;
	}

	UHorizontalBoxSlot* AddH(UHorizontalBox* Box, UWidget* W, const FMargin& Padding = FMargin(0.f), bool bFill = false)
	{
		UHorizontalBoxSlot* HSlot = Box->AddChildToHorizontalBox(W);
		HSlot->SetPadding(Padding);
		HSlot->SetVerticalAlignment(VAlign_Center);
		if (bFill) { HSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
		return HSlot;
	}

	UVerticalBoxSlot* AddV(UVerticalBox* Box, UWidget* W, const FMargin& Padding = FMargin(0.f), EHorizontalAlignment H = HAlign_Fill)
	{
		UVerticalBoxSlot* VSlot = Box->AddChildToVerticalBox(W);
		VSlot->SetPadding(Padding);
		VSlot->SetHorizontalAlignment(H);
		return VSlot;
	}

	UOverlaySlot* AddO(UOverlay* Stack, UWidget* W, EHorizontalAlignment H, EVerticalAlignment V, const FMargin& Padding = FMargin(0.f))
	{
		UOverlaySlot* OSlot = Stack->AddChildToOverlay(W);
		OSlot->SetHorizontalAlignment(H);
		OSlot->SetVerticalAlignment(V);
		OSlot->SetPadding(Padding);
		return OSlot;
	}

	bool IsKey(const FKey& Key, std::initializer_list<FKey> Keys)
	{
		for (const FKey& Option : Keys) { if (Key == Option) { return true; } }
		return false;
	}

	/** Cartel azul marino con la cinta coral del título encima de su borde, como el de «PAUSA». */
	UWidget* Ribbon(UWidgetTree* Tree, UTextBlock*& OutTitle)
	{
		OutTitle = Label(Tree, FText::GetEmpty(), TEXT("Black"), 34, FLinearColor::White);
		UBorder* Band = Make<UBorder>(Tree);
		Band->SetBrush(BoxBrush(TNHUDArt::RibbonTexture(), RibbonMargin));
		Band->SetPadding(FMargin(76.f, 8.f, 76.f, 14.f));
		Band->SetHorizontalAlignment(HAlign_Center);
		Band->SetContent(OutTitle);
		Band->SetRenderTransformAngle(-2.f);
		return Band;
	}

	/** Barra de desplazamiento con el estilo del HUD (la misma que la de los ajustes del menú de pausa). */
	void StyleScroll(UScrollBox* List)
	{
		FScrollBarStyle BarStyle = List->GetWidgetBarStyle();
		BarStyle.SetVerticalBackgroundImage(TNHUDStyle::Rounded(FLinearColor(0.f, 0.02f, 0.04f, 0.5f), 4.f));
		BarStyle.SetNormalThumbImage(TNHUDStyle::Rounded(TNHUDArt::Hex(0x62D2EA, 0.55f), 4.f));
		BarStyle.SetHoveredThumbImage(TNHUDStyle::Rounded(TNHUDArt::Hex(0x62D2EA, 0.85f), 4.f));
		BarStyle.SetDraggedThumbImage(TNHUDStyle::Rounded(TNHUDArt::Gold, 4.f));
		List->SetWidgetBarStyle(BarStyle);
		List->SetScrollbarThickness(FVector2D(8.f, 8.f));
		List->SetScrollbarPadding(FMargin(8.f, 0.f, 0.f, 0.f));
	}

	FText SizeText(int32 Size)
	{
		return FText::Format(NSLOCTEXT("TNRooms", "SizeFmt", "{0} {0}|plural(one=tortuga,other=tortugas)"), Size);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Campo del código
// ─────────────────────────────────────────────────────────────────────────────

void UTN_RoomCodeField::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// Se enfoca (teclado y mando) y la raíz existe desde ya: el campo entra en un panel que ya está en pantalla.
	SetIsFocusable(true);
	// Visible, no SelfHitTestInvisible (el de serie de un UUserWidget): la navegación de Slate solo llega a lo que se puede
	// tocar (como las filas, UTN_PauseRow).
	SetVisibility(ESlateVisibility::Visible);
	Chars.Init(TCHAR(0), TNRoomCode::Length);
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}
	UWidgetTree* Tree = WidgetTree;
	UHorizontalBox* Line = TNRoomUI::Make<UHorizontalBox>(Tree);
	Tree->RootWidget = Line;
	for (int32 i = 0; i < TNRoomCode::Length; ++i)
	{
		UTextBlock* Letter = TNRoomUI::Label(Tree, FText::GetEmpty(), TEXT("Black"), 34, TNHUDArt::Cream);
		Letter->SetJustification(ETextJustify::Center);
		UBorder* Cell = TNRoomUI::Make<UBorder>(Tree);
		Cell->SetHorizontalAlignment(HAlign_Center);
		Cell->SetVerticalAlignment(VAlign_Center);
		Cell->SetContent(Letter);
		TNRoomUI::AddH(Line, TNRoomUI::Sized(Tree, Cell, TNRoomUI::CellWidth, TNRoomUI::CellHeight), FMargin(i > 0 ? TNRoomUI::CellGap : 0.f, 0.f, 0.f, 0.f));
		Cells.Add(Cell);
		CellTexts.Add(Letter);
	}
	RefreshCells();
}

FString UTN_RoomCodeField::GetCode() const
{
	FString Out;
	for (const TCHAR Char : Chars)
	{
		if (Char != TCHAR(0)) { Out.AppendChar(Char); }
	}
	return Out;
}

void UTN_RoomCodeField::SetCode(const FString& InCode)
{
	const FString Clean = TNRoomCode::Normalize(InCode);
	for (int32 i = 0; i < Chars.Num(); ++i)
	{
		Chars[i] = i < Clean.Len() ? Clean[i] : TCHAR(0);
	}
	CaretIndex = FMath::Clamp(Clean.Len(), 0, TNRoomCode::Length - 1);
	RefreshCells();
}

bool UTN_RoomCodeField::IsComplete() const
{
	return TNRoomCode::IsComplete(GetCode());
}

void UTN_RoomCodeField::PasteFromClipboard()
{
	FString Pasted;
	FPlatformApplicationMisc::ClipboardPaste(Pasted);
	const FString Code = TNRoomCode::FromPasted(Pasted);
	if (Code.IsEmpty())
	{
		if (OnHint) { OnHint(NSLOCTEXT("TNRooms", "PasteEmpty", "En el portapapeles no hay ningún código de sala.")); }
		PlaySound(ETNPauseSound::Hover);
		return;
	}
	SetCode(Code);
	PlaySound(ETNPauseSound::Press);
}

bool UTN_RoomCodeField::OpenSteamKeyboard()
{
	FTNSteamKeyboardRequest Request;
	Request.Description = NSLOCTEXT("TNRooms", "CodeRow", "Código de la sala");
	Request.ExistingText = GetCode();
	Request.MaxChars = TNRoomCode::Length;
	Request.FieldRect = RectInWindow();
	TWeakObjectPtr<UTN_RoomCodeField> WeakThis(this);
	const TOptional<ETNSteamKeyboard> Opened = TNSteamGamepadInput::OpenKeyboard(Request, true,
		[WeakThis](bool bSubmitted, const FString& Text)
		{
			if (UTN_RoomCodeField* Field = WeakThis.Get()) { Field->HandleSteamText(bSubmitted, Text); }
		});
	if (!Opened.IsSet())
	{
		return false;
	}
	// El flotante escribe como un teclado: desde la primera casilla vacía.
	if (Opened.GetValue() == ETNSteamKeyboard::Floating)
	{
		CaretIndex = FMath::Clamp(GetCode().Len(), 0, TNRoomCode::Length - 1);
		RefreshCells();
	}
	return true;
}

void UTN_RoomCodeField::HandleSteamText(bool bSubmitted, const FString& Text)
{
	if (!bSubmitted)
	{
		return;
	}
	const FString Code = TNRoomCode::FromPasted(Text);
	if (Code.IsEmpty())
	{
		PlaySound(ETNPauseSound::Hover);
		if (!Text.TrimStartAndEnd().IsEmpty() && OnHint)
		{
			OnHint(NSLOCTEXT("TNRooms", "CodeBadChar", "Los códigos nunca llevan O, 0, I, 1 ni L: fíjate bien en el código."));
		}
		return;
	}
	SetCode(Code);
	PlaySound(ETNPauseSound::Press);
	// «Hecho» en el teclado de Steam es como Intro: con el código entero, se entra.
	if (IsComplete() && OnSubmit)
	{
		OnSubmit();
	}
}

FIntRect UTN_RoomCodeField::RectInWindow() const
{
	const FGeometry& Geometry = GetCachedGeometry();
	FVector2D Position = Geometry.GetAbsolutePosition();
	const FVector2D Size = Geometry.GetAbsoluteSize();
	const TSharedPtr<SWidget> Cached = GetCachedWidget();
	if (Cached.IsValid() && FSlateApplication::IsInitialized())
	{
		if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(Cached.ToSharedRef()))
		{
			Position -= Window->GetPositionInScreen();
		}
	}
	const FIntPoint Min(FMath::RoundToInt(Position.X), FMath::RoundToInt(Position.Y));
	return FIntRect(Min, Min + FIntPoint(FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y)));
}

FText UTN_RoomCodeField::GetDescription() const
{
	return bEditing
		? NSLOCTEXT("TNRooms", "CodeFieldEditing", "↑ ↓ cambian la letra, ← → la casilla, X la borra. A o B: terminar. Intro: entrar.")
		: NSLOCTEXT("TNRooms", "CodeFieldDesc", "Escribe o pega (Ctrl+V, clic derecho) el código de 5 letras y números. Con mando: A para escribirlo. Intro: entrar.");
}

void UTN_RoomCodeField::RefreshCells()
{
	for (int32 i = 0; i < Cells.Num() && i < CellTexts.Num() && i < Chars.Num(); ++i)
	{
		const bool bCaret = bFocused && i == CaretIndex;
		const bool bFilled = Chars[i] != TCHAR(0);
		const FLinearColor FillColor = bCaret && bEditing ? TNHUDArt::Hex(0x5A3F0C, 0.95f) : TNHUDArt::Hex(0x0B2A4A, 0.92f);
		const FLinearColor Edge = bCaret ? TNHUDArt::Gold : FLinearColor(1.f, 1.f, 1.f, bFocused ? 0.35f : 0.16f);
		Cells[i]->SetBrush(TNHUDStyle::Rounded(FillColor, 10.f, Edge, bCaret ? 2.5f : 1.f));
		CellTexts[i]->SetText(bFilled ? TNLocText::Literal(FString::Chr(Chars[i])) : INVTEXT("·"));
		CellTexts[i]->SetColorAndOpacity(FSlateColor(bFilled ? TNHUDArt::Cream : TNHUDStyle::TextDim));
		Cells[i]->SetRenderOpacity(1.f);
	}
}

void UTN_RoomCodeField::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Clock += InDeltaTime;
	// Escribiendo con el mando, la casilla del cursor late.
	if (bEditing && Cells.IsValidIndex(CaretIndex))
	{
		Cells[CaretIndex]->SetRenderOpacity(0.6f + 0.4f * FMath::Abs(FMath::Cos(Clock * 4.f)));
	}
}

void UTN_RoomCodeField::TypeChar(TCHAR Char)
{
	if (!Chars.IsValidIndex(CaretIndex))
	{
		return;
	}
	Chars[CaretIndex] = Char;
	const float Pitch = static_cast<float>(CaretIndex) / FMath::Max(1, TNRoomCode::Length - 1);
	CaretIndex = FMath::Min(CaretIndex + 1, TNRoomCode::Length - 1);
	RefreshCells();
	PlaySound(ETNPauseSound::Tick, Pitch);
}

void UTN_RoomCodeField::Erase(bool bBackward)
{
	if (!Chars.IsValidIndex(CaretIndex))
	{
		return;
	}
	// Retroceso: borra la casilla del cursor si tiene letra; si no, la de antes (y se queda en ella).
	if (bBackward && Chars[CaretIndex] == TCHAR(0) && CaretIndex > 0)
	{
		--CaretIndex;
	}
	Chars[CaretIndex] = TCHAR(0);
	RefreshCells();
	PlaySound(ETNPauseSound::Hover);
}

void UTN_RoomCodeField::MoveCaret(int32 Delta)
{
	const int32 Next = FMath::Clamp(CaretIndex + Delta, 0, TNRoomCode::Length - 1);
	if (Next != CaretIndex)
	{
		CaretIndex = Next;
		RefreshCells();
		PlaySound(ETNPauseSound::Hover);
	}
}

void UTN_RoomCodeField::CycleChar(int32 Delta)
{
	if (!Chars.IsValidIndex(CaretIndex) || Delta == 0)
	{
		return;
	}
	const FString Alphabet(TNRoomCode::Alphabet());
	int32 Current = INDEX_NONE;
	if (Chars[CaretIndex] != TCHAR(0))
	{
		Alphabet.FindChar(Chars[CaretIndex], Current);
	}
	const int32 Count = Alphabet.Len();
	const int32 Next = Current == INDEX_NONE ? (Delta > 0 ? 0 : Count - 1) : (Current + Delta + Count) % Count;
	Chars[CaretIndex] = Alphabet[Next];
	RefreshCells();
	PlaySound(ETNPauseSound::Tick, static_cast<float>(Next) / FMath::Max(1, Count - 1));
}

void UTN_RoomCodeField::SetEditing(bool bInEditing)
{
	if (bEditing == bInEditing)
	{
		return;
	}
	bEditing = bInEditing;
	RefreshCells();
	// La ayuda de abajo cambia con el modo.
	if (bFocused && OnFocused) { OnFocused(this); }
}

int32 UTN_RoomCodeField::CellAt(const FVector2D& ScreenPosition) const
{
	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		if (Cells[i] && Cells[i]->GetCachedGeometry().IsUnderLocation(ScreenPosition))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void UTN_RoomCodeField::PlaySound(ETNPauseSound Sound, float Pitch) const
{
	if (OnSound) { OnSound(Sound, Pitch); }
}

FReply UTN_RoomCodeField::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	using TNRoomUI::IsKey;
	const FKey Key = InKeyEvent.GetKey();
	const bool bCtrl = InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown();

	// Pegar y copiar.
	if ((bCtrl && Key == EKeys::V) || (InKeyEvent.IsShiftDown() && Key == EKeys::Insert))
	{
		if (!InKeyEvent.IsRepeat()) { PasteFromClipboard(); }
		return FReply::Handled();
	}
	if (bCtrl && Key == EKeys::C)
	{
		const FString Code = GetCode();
		if (!InKeyEvent.IsRepeat() && !Code.IsEmpty()) { FPlatformApplicationMisc::ClipboardCopy(*Code); }
		return FReply::Handled();
	}
	if (Key == EKeys::BackSpace)
	{
		Erase(true);
		return FReply::Handled();
	}
	if (IsKey(Key, { EKeys::Delete, EKeys::Gamepad_FaceButton_Left }))
	{
		// Con gafas, la X de los Touch llega aquí antes de aceptar (#648).
		TNVRMenuClaim::Claim();
		Erase(false);
		return FReply::Handled();
	}
	if (Key == EKeys::Home || Key == EKeys::End)
	{
		MoveCaret(Key == EKeys::Home ? -TNRoomCode::Length : TNRoomCode::Length);
		return FReply::Handled();
	}
	// Intro: entrar. A del mando: empezar o acabar de escribir.
	if (Key == EKeys::Enter)
	{
		if (!InKeyEvent.IsRepeat())
		{
			SetEditing(false);
			if (OnSubmit) { OnSubmit(); }
		}
		return FReply::Handled();
	}
	if (IsKey(Key, { EKeys::Gamepad_FaceButton_Bottom, EKeys::Virtual_Accept }))
	{
		if (!InKeyEvent.IsRepeat())
		{
			// Con Steam, el teclado en pantalla; sin él, se escribe con las casillas.
			if (bEditing || !OpenSteamKeyboard())
			{
				SetEditing(!bEditing);
			}
			PlaySound(ETNPauseSound::Press);
		}
		return FReply::Handled();
	}
	// B o Esc: escribiendo, terminan; si no, siguen hacia el menú (volver).
	if (IsKey(Key, { EKeys::Gamepad_FaceButton_Right, EKeys::Escape, EKeys::Virtual_Back }))
	{
		if (bEditing)
		{
			SetEditing(false);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	const bool bUp = IsKey(Key, { EKeys::Up, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_LeftStick_Up });
	const bool bDown = IsKey(Key, { EKeys::Down, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_LeftStick_Down });
	if (bUp || bDown)
	{
		if (bEditing)
		{
			CycleChar(bUp ? 1 : -1);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
	const bool bLeft = IsKey(Key, { EKeys::Left, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_LeftStick_Left });
	const bool bRight = IsKey(Key, { EKeys::Right, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_LeftStick_Right });
	if (bLeft || bRight)
	{
		// Las flechas del teclado recorren las casillas y, en el borde, salen del campo; el mando solo escribiendo.
		const int32 Delta = bLeft ? -1 : 1;
		const bool bKeyboard = Key == EKeys::Left || Key == EKeys::Right;
		const bool bAtEdge = CaretIndex + Delta < 0 || CaretIndex + Delta >= TNRoomCode::Length;
		if (bEditing || (bKeyboard && !bAtEdge))
		{
			MoveCaret(Delta);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	// Letras y números: los escribe NativeOnKeyChar; aquí solo que no sigan hacia el menú.
	const uint32 Char = InKeyEvent.GetCharacter();
	if (Char > 0 && Char < 128 && FChar::IsAlnum(static_cast<TCHAR>(Char)))
	{
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UTN_RoomCodeField::NativeOnKeyChar(const FGeometry& InGeometry, const FCharacterEvent& InCharEvent)
{
	const TCHAR Char = FChar::ToUpper(InCharEvent.GetCharacter());
	if (!FChar::IsAlnum(Char))
	{
		return Super::NativeOnKeyChar(InGeometry, InCharEvent);
	}
	if (TNRoomCode::IsAllowedChar(Char))
	{
		TypeChar(Char);
	}
	else
	{
		PlaySound(ETNPauseSound::Hover);
		if (OnHint) { OnHint(NSLOCTEXT("TNRooms", "CodeBadChar", "Los códigos nunca llevan O, 0, I, 1 ni L: fíjate bien en el código.")); }
	}
	return FReply::Handled();
}

FNavigationReply UTN_RoomCodeField::NativeOnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent, const FNavigationReply& InDefaultReply)
{
	// Escribiendo con el mando, el stick y la cruceta cambian las letras: no se sale del campo.
	if (bEditing)
	{
		return FNavigationReply::Stop();
	}
	return Super::NativeOnNavigation(MyGeometry, InNavigationEvent, InDefaultReply);
}

FReply UTN_RoomCodeField::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	SetKeyboardFocus();
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		PasteFromClipboard();
		return FReply::Handled();
	}
	const int32 Cell = CellAt(InMouseEvent.GetScreenSpacePosition());
	if (Cell != INDEX_NONE && Cell != CaretIndex)
	{
		CaretIndex = Cell;
		RefreshCells();
		PlaySound(ETNPauseSound::Hover);
	}
	return FReply::Handled();
}

FReply UTN_RoomCodeField::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const int32 Cell = CellAt(InMouseEvent.GetScreenSpacePosition());
	if (Cell != INDEX_NONE)
	{
		CaretIndex = Cell;
	}
	CycleChar(InMouseEvent.GetWheelDelta() > 0.f ? 1 : -1);
	return FReply::Handled();
}

void UTN_RoomCodeField::NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent)
{
	Super::NativeOnAddedToFocusPath(InFocusEvent);
	if (!bFocused)
	{
		bFocused = true;
		RefreshCells();
		PlaySound(ETNPauseSound::Hover);
		if (OnFocused) { OnFocused(this); }
	}
}

void UTN_RoomCodeField::NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent)
{
	Super::NativeOnRemovedFromFocusPath(InFocusEvent);
	bFocused = false;
	bEditing = false;
	RefreshCells();
}

// ─────────────────────────────────────────────────────────────────────────────
// Menú de salas: montaje
// ─────────────────────────────────────────────────────────────────────────────

void UTN_RoomMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// Se enfoca al hacer clic fuera de las opciones (así las teclas no se pierden).
	SetIsFocusable(true);
	BuildTree();
	if (UMP_GameInstance* GameInstance = GetRoomGameInstance())
	{
		NoticeHandle = GameInstance->OnRoomNotice.AddUObject(this, &UTN_RoomMenuWidget::HandleRoomNotice);
		ListHandle = GameInstance->OnRoomListChanged.AddUObject(this, &UTN_RoomMenuWidget::HandleRoomListChanged);
	}
}

void UTN_RoomMenuWidget::NativeDestruct()
{
	if (UMP_GameInstance* GameInstance = GetRoomGameInstance())
	{
		GameInstance->OnRoomNotice.Remove(NoticeHandle);
		GameInstance->OnRoomListChanged.Remove(ListHandle);
	}
	NoticeHandle.Reset();
	ListHandle.Reset();
	Super::NativeDestruct();
}

void UTN_RoomMenuWidget::BuildTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}
	UWidgetTree* Tree = WidgetTree;
	// La raíz, antes que nada: el widget se mete en la pantalla ya montado (ver UTN_PauseRow::NativeOnInitialized).
	UCanvasPanel* Root = TNRoomUI::Make<UCanvasPanel>(Tree);
	Tree->RootWidget = Root;

	// Pantallas: velo azul marino y el lienzo de 1920 × 1080 que se encoge si no cabe.
	UOverlay* Screen = TNRoomUI::Make<UOverlay>(Tree);
	UImage* Veil = TNRoomUI::Make<UImage>(Tree);
	Veil->SetColorAndOpacity(TNHUDArt::Hex(0x0A1C38, 0.8f));
	TNRoomUI::AddO(Screen, Veil, HAlign_Fill, VAlign_Fill);
	UCanvasPanel* Canvas = TNRoomUI::Make<UCanvasPanel>(Tree);
	TNRoomUI::AddO(Screen, TNRoomUI::Fit(Tree, Canvas), HAlign_Fill, VAlign_Fill);
	TNRoomUI::Fill(Root, Screen);
	ScreenLayer = Screen;

	UTextBlock* Title = nullptr;
	TNRoomUI::Pin(Canvas, TNRoomUI::Ribbon(Tree, Title), FVector2D(0.5f, 0.f), FVector2D(0.f, 40.f));
	TitleText = Title;

	Pages = TNRoomUI::Make<UWidgetSwitcher>(Tree);
	for (UWidget* PageWidget : { BuildCreatePage(), BuildJoinPage() })
	{
		if (UWidgetSwitcherSlot* PageSlot = Cast<UWidgetSwitcherSlot>(Pages->AddChild(PageWidget)))
		{
			PageSlot->SetHorizontalAlignment(HAlign_Center);
			PageSlot->SetVerticalAlignment(VAlign_Top);
		}
	}
	TNRoomUI::Pin(Canvas, Pages, FVector2D(0.5f, 0.f), FVector2D(0.f, 140.f));

	// Abajo, apilados de arriba abajo (como en el menú de pausa): el aviso, la ayuda de la opción enfocada y los atajos.
	// Cada uno ocupa las líneas que necesite y sube al de encima: un aviso o una ayuda de dos líneas no pisan al vecino
	// (#245). La ayuda tiene su hueco fijo sobre los atajos, salga aviso o no.
	UVerticalBox* Footer = TNRoomUI::Make<UVerticalBox>(Tree);
	NoticeText = TNRoomUI::WrappedLabel(Tree, TEXT("Bold"), 20, TNHUDArt::Gold);
	NoticeText->SetVisibility(ESlateVisibility::Collapsed);
	TNRoomUI::AddV(Footer, TNRoomUI::Sized(Tree, NoticeText, TNRoomUI::FooterWidth, 0.f), FMargin(0.f), HAlign_Center);
	HelpText = TNRoomUI::WrappedLabel(Tree, TEXT("Regular"), 18, TNHUDArt::Foam);
	TNRoomUI::AddV(Footer, TNRoomUI::Sized(Tree, HelpText, TNRoomUI::FooterWidth, 0.f), FMargin(0.f, 10.f, 0.f, 4.f), HAlign_Center);
	HintText = TNRoomUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 16, TNHUDStyle::TextDim);
	TNRoomUI::AddV(Footer, HintText, FMargin(0.f), HAlign_Center);
	TNRoomUI::Pin(Canvas, Footer, FVector2D(0.5f, 1.f), FVector2D(0.f, -20.f));

	// Aviso de arriba con la pantalla cerrada (sobre el menú del Blueprint, sin tapar sus clics).
	UCanvasPanel* ToastCanvas = TNRoomUI::Make<UCanvasPanel>(Tree);
	ToastText = TNRoomUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 21, TNHUDArt::Cream);
	ToastText->SetJustification(ETextJustify::Center);
	ToastText->SetAutoWrapText(true);
	UWidget* ToastCard = TNRoomUI::Sized(Tree, TNRoomUI::Card(Tree, ToastText, FMargin(40.f, 24.f, 40.f, 40.f)), 980.f, 0.f);
	TNRoomUI::Pin(ToastCanvas, ToastCard, FVector2D(0.5f, 0.f), FVector2D(0.f, 30.f));
	UWidget* ToastLayer = TNRoomUI::Fit(Tree, ToastCanvas);
	TNRoomUI::Fill(Root, ToastLayer);
	ToastLayer->SetVisibility(ESlateVisibility::HitTestInvisible);
	Toast = ToastCard;
	Toast->SetVisibility(ESlateVisibility::Collapsed);

	// Cerrada hasta que el menú la abra: no tapa los clics del Blueprint.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	ScreenLayer->SetVisibility(ESlateVisibility::Collapsed);
	RefreshHint();
}

UWidget* UTN_RoomMenuWidget::BuildCreatePage()
{
	UWidgetTree* Tree = WidgetTree;
	TWeakObjectPtr<UTN_RoomMenuWidget> WeakThis(this);
	UVerticalBox* Column = TNRoomUI::Make<UVerticalBox>(Tree);
	auto AddRow = [this, Column]() -> UTN_PauseRow*
	{
		UTN_PauseRow* Row = NewRow();
		if (Row) { TNRoomUI::AddV(Column, Row, FMargin(0.f, 0.f, 0.f, 6.f)); }
		return Row;
	};

	ModeRow = AddRow();
	if (ModeRow)
	{
		TArray<FText> Modes;
		for (const ETNProcGameMode Mode : TNLobbyMission::GetMenuModes()) { Modes.Add(TNLobbyMission::ModeName(Mode)); }
		ModeRow->SetupChoice(NSLOCTEXT("TNRooms", "ModeRow", "Modo"), Modes, 0, [WeakThis](int32 Choice)
		{
			UTN_RoomMenuWidget* Menu = WeakThis.Get();
			if (!Menu) { return; }
			const TArray<ETNProcGameMode> MenuModes = TNLobbyMission::GetMenuModes();
			Menu->Draft.Mode = MenuModes[FMath::Clamp(Choice, 0, MenuModes.Num() - 1)];
			Menu->RefreshCreateRows();
		});
	}
	TctArenaRow = AddRow();
	if (TctArenaRow)
	{
		TArray<FText> Arenas;
		for (const FName Arena : TNLobbyMission::TctArenaOptions()) { Arenas.Add(TNLobbyMission::TctArenaName(Arena)); }
		TctArenaRow->SetupChoice(NSLOCTEXT("TNRooms", "TctArenaRow", "Arena"), Arenas, 0, [WeakThis](int32 Choice)
		{
			UTN_RoomMenuWidget* Menu = WeakThis.Get();
			const TArray<FName>& Options = TNLobbyMission::TctArenaOptions();
			if (!Menu || !Options.IsValidIndex(Choice)) { return; }
			Menu->Draft.TctArena = Options[Choice];
			Menu->RefreshCreateRows();
		});
		TctArenaRow->SetDescription(NSLOCTEXT("TNRooms", "TctArenaDesc",
			"La arena de Todos contra Todos: se inunda ronda a ronda y caer al agua es la muerte."));
	}
	VisibilityRow = AddRow();
	if (VisibilityRow)
	{
		const TArray<FText> Kinds = { TNRoomText::Visibility(false), TNRoomText::Visibility(true) };
		VisibilityRow->SetupChoice(NSLOCTEXT("TNRooms", "VisibilityRow", "Sala"), Kinds, 0, [WeakThis](int32 Choice)
		{
			UTN_RoomMenuWidget* Menu = WeakThis.Get();
			if (!Menu) { return; }
			Menu->Draft.bPrivate = Choice == 1;
			Menu->RefreshCreateRows();
		});
		VisibilityRow->SetDescription(NSLOCTEXT("TNRooms", "VisibilityDesc",
			"Pública: sale en la lista de «Unirse» y entra quien quiera. Privada: no sale en la lista; se entra con su código o por invitación de Steam."));
	}
	SizeRow = AddRow();
	if (SizeRow)
	{
		SizeRow->SetupChoice(NSLOCTEXT("TNRooms", "SizeRow", "Plazas"), { TNRoomUI::SizeText(TNRoomLimits::Max) }, 0, [WeakThis](int32 Choice)
		{
			UTN_RoomMenuWidget* Menu = WeakThis.Get();
			if (!Menu || !Menu->SizeOptions.IsValidIndex(Choice)) { return; }
			Menu->Draft.MaxPlayers = Menu->SizeOptions[Choice];
			Menu->RefreshCreateRows();
		});
		SizeRow->SetDescription(NSLOCTEXT("TNRooms", "SizeDesc", "Cuántas tortugas caben en la sala, tú incluida (8 como mucho)."));
	}
	NameRow = AddRow();
	CodeRow = AddRow();
	RefreshCreateRows();

	CreateSummary = TNRoomUI::Label(Tree, FText::GetEmpty(), TEXT("Regular"), 18, TNHUDArt::Foam);
	CreateSummary->SetJustification(ETextJustify::Center);
	CreateSummary->SetAutoWrapText(true);
	TNRoomUI::AddV(Column, CreateSummary, FMargin(10.f, 10.f, 10.f, 18.f));

	UHorizontalBox* Buttons = TNRoomUI::Make<UHorizontalBox>(Tree);
	CreateButton = NewRow();
	if (CreateButton)
	{
		CreateButton->SetupButton(ETNPauseRowStyle::Dialog, NSLOCTEXT("TNRooms", "CreateButton", "Crear partida"), [WeakThis]()
		{
			if (UTN_RoomMenuWidget* Menu = WeakThis.Get()) { Menu->HostRoom(); }
		});
		CreateButton->SetDescription(NSLOCTEXT("TNRooms", "CreateButtonDesc", "Crea la sala y te lleva al lobby del castillo. Desde el menú de pausa («Sala») puedes cerrarla o expulsar a alguien."));
		TNRoomUI::AddH(Buttons, CreateButton, FMargin(10.f, 0.f));
	}
	if (UTN_PauseRow* Back = NewRow())
	{
		Back->SetupButton(ETNPauseRowStyle::Dialog, NSLOCTEXT("TNRooms", "Back", "Volver"), [WeakThis]()
		{
			if (UTN_RoomMenuWidget* Menu = WeakThis.Get()) { Menu->GoBack(); }
		});
		Back->SetDescription(NSLOCTEXT("TNRooms", "BackDesc", "Vuelve al menú principal."));
		TNRoomUI::AddH(Buttons, Back, FMargin(10.f, 0.f));
	}
	TNRoomUI::AddV(Column, Buttons, FMargin(0.f), HAlign_Center);

	return TNRoomUI::Sized(Tree, TNRoomUI::Card(Tree, Column, FMargin(34.f, 26.f, 34.f, 50.f)), TNRoomUI::CreateCardWidth, 0.f);
}

UWidget* UTN_RoomMenuWidget::BuildJoinPage()
{
	UWidgetTree* Tree = WidgetTree;
	TWeakObjectPtr<UTN_RoomMenuWidget> WeakThis(this);
	UVerticalBox* Column = TNRoomUI::Make<UVerticalBox>(Tree);

	// Con código: el campo, «Entrar» y «Pegar».
	UHorizontalBox* CodeLine = TNRoomUI::Make<UHorizontalBox>(Tree);
	TNRoomUI::AddH(CodeLine, TNRoomUI::Sized(Tree, TNRoomUI::Label(Tree, NSLOCTEXT("TNRooms", "HaveCode", "¿Tienes un código?"), TEXT("Bold"), 21, TNHUDArt::Cream),
		250.f, 0.f), FMargin(8.f, 0.f, 12.f, 0.f));
	CodeField = CreateWidget<UTN_RoomCodeField>(this, UTN_RoomCodeField::StaticClass());
	if (CodeField)
	{
		CodeField->OnSubmit = [WeakThis]() { if (UTN_RoomMenuWidget* Menu = WeakThis.Get()) { Menu->JoinByCode(); } };
		CodeField->OnFocused = [WeakThis](UTN_RoomCodeField* Field)
		{
			if (UTN_RoomMenuWidget* Menu = WeakThis.Get()) { Menu->HandleFocused(Field, Field ? Field->GetDescription() : FText::GetEmpty()); }
		};
		CodeField->OnSound = [WeakThis](ETNPauseSound Sound, float Pitch) { if (UTN_RoomMenuWidget* Menu = WeakThis.Get()) { Menu->PlayUISound(Sound, Pitch); } };
		CodeField->OnHint = [WeakThis](const FText& Hint) { if (UTN_RoomMenuWidget* Menu = WeakThis.Get()) { Menu->ShowNotice(Hint, true, 4.f); } };
		TNRoomUI::AddH(CodeLine, CodeField, FMargin(0.f, 0.f, 18.f, 0.f));
	}
	TNRoomUI::AddH(CodeLine, TNRoomUI::Make<USpacer>(Tree), FMargin(0.f), true);
	CodeJoinButton = NewRow();
	if (CodeJoinButton)
	{
		CodeJoinButton->SetupButton(ETNPauseRowStyle::Dialog, NSLOCTEXT("TNRooms", "CodeJoin", "Entrar"), [WeakThis]()
		{
			if (UTN_RoomMenuWidget* Menu = WeakThis.Get()) { Menu->JoinByCode(); }
		});
		CodeJoinButton->SetWidthOverride(190.f);
		CodeJoinButton->SetDescription(NSLOCTEXT("TNRooms", "CodeJoinDesc", "Entra en la sala de ese código (pública o privada)."));
		TNRoomUI::AddH(CodeLine, CodeJoinButton, FMargin(0.f, 0.f, 12.f, 0.f));
	}
	if (UTN_PauseRow* Paste = NewRow())
	{
		Paste->SetupButton(ETNPauseRowStyle::Dialog, NSLOCTEXT("TNRooms", "Paste", "Pegar"), [WeakThis]()
		{
			UTN_RoomMenuWidget* Menu = WeakThis.Get();
			if (Menu && Menu->CodeField) { Menu->CodeField->PasteFromClipboard(); }
		});
		Paste->SetWidthOverride(170.f);
		Paste->SetDescription(NSLOCTEXT("TNRooms", "PasteDesc", "Pega el código que te hayan pasado (lo busca en el texto copiado)."));
		TNRoomUI::AddH(CodeLine, Paste);
	}
	TNRoomUI::AddV(Column, CodeLine, FMargin(0.f, 0.f, 0.f, 6.f));
	UTextBlock* CodeHelp = TNRoomUI::Label(Tree, NSLOCTEXT("TNRooms", "CodeHelp",
		"5 letras y números, sin O, 0, I, 1 ni L. Con mando: A para escribirlo, ↑ ↓ cambian la letra y ← → la casilla."), TEXT("Regular"), 16, TNHUDStyle::TextDim);
	TNRoomUI::AddV(Column, CodeHelp, FMargin(10.f, 0.f, 10.f, 16.f));

	// Salas públicas: cabecera con su estado y «Actualizar», y la lista.
	UHorizontalBox* ListHead = TNRoomUI::Make<UHorizontalBox>(Tree);
	TNRoomUI::AddH(ListHead, TNRoomUI::Label(Tree, NSLOCTEXT("TNRooms", "PublicRooms", "PARTIDAS PÚBLICAS"), TEXT("Black"), 20, TNHUDArt::Gold), FMargin(8.f, 0.f, 16.f, 0.f));
	ListStatus = TNRoomUI::Label(Tree, FText::GetEmpty(), TEXT("Regular"), 17, TNHUDArt::Foam);
	TNRoomUI::AddH(ListHead, ListStatus, FMargin(0.f), true);
	if (UTN_PauseRow* Refresh = NewRow())
	{
		Refresh->SetupButton(ETNPauseRowStyle::Dialog, NSLOCTEXT("TNRooms", "Refresh", "Actualizar"), [WeakThis]()
		{
			if (UTN_RoomMenuWidget* Menu = WeakThis.Get()) { Menu->RefreshRooms(); }
		});
		Refresh->SetWidthOverride(220.f);
		Refresh->SetDescription(NSLOCTEXT("TNRooms", "RefreshDesc", "Vuelve a buscar las salas públicas (F5 o Y del mando). La lista se actualiza sola cada poco."));
		TNRoomUI::AddH(ListHead, Refresh);
	}
	TNRoomUI::AddV(Column, ListHead, FMargin(0.f, 0.f, 0.f, 8.f));

	RoomScroll = TNRoomUI::Make<UScrollBox>(Tree);
	TNRoomUI::StyleScroll(RoomScroll);
	TNRoomUI::AddV(Column, TNRoomUI::Sized(Tree, RoomScroll, 0.f, TNRoomUI::RoomListHeight), FMargin(0.f, 0.f, 0.f, 16.f));

	if (UTN_PauseRow* Back = NewRow())
	{
		Back->SetupButton(ETNPauseRowStyle::Dialog, NSLOCTEXT("TNRooms", "Back", "Volver"), [WeakThis]()
		{
			if (UTN_RoomMenuWidget* Menu = WeakThis.Get()) { Menu->GoBack(); }
		});
		Back->SetDescription(NSLOCTEXT("TNRooms", "BackDesc", "Vuelve al menú principal."));
		TNRoomUI::AddV(Column, Back, FMargin(0.f), HAlign_Center);
	}

	return TNRoomUI::Sized(Tree, TNRoomUI::Card(Tree, Column, FMargin(34.f, 26.f, 34.f, 50.f)), TNRoomUI::JoinCardWidth, 0.f);
}

UTN_PauseRow* UTN_RoomMenuWidget::NewRow()
{
	UTN_PauseRow* Row = CreateWidget<UTN_PauseRow>(this, UTN_PauseRow::StaticClass());
	if (!Row)
	{
		return nullptr;
	}
	TWeakObjectPtr<UTN_RoomMenuWidget> WeakThis(this);
	Row->OnFocused = [WeakThis](UTN_PauseRow* Focused)
	{
		if (UTN_RoomMenuWidget* Menu = WeakThis.Get()) { Menu->HandleFocused(Focused, Focused ? Focused->GetDescription() : FText::GetEmpty()); }
	};
	Row->OnSound = [WeakThis](ETNPauseSound Sound, float Pitch)
	{
		if (UTN_RoomMenuWidget* Menu = WeakThis.Get()) { Menu->PlayUISound(Sound, Pitch); }
	};
	return Row;
}

// ─────────────────────────────────────────────────────────────────────────────
// Menú de salas: abrir, cerrar y avisos
// ─────────────────────────────────────────────────────────────────────────────

void UTN_RoomMenuWidget::Open(ETNRoomMenuPage NewPage)
{
	if (NewPage == ETNRoomMenuPage::Closed)
	{
		Close();
		return;
	}
	UMP_GameInstance* GameInstance = GetRoomGameInstance();
	const bool bWasOpen = IsOpen();
	Page = NewPage;
	if (ScreenLayer) { ScreenLayer->SetVisibility(ESlateVisibility::Visible); }
	if (Toast) { Toast->SetVisibility(ESlateVisibility::Collapsed); }
	ToastTime = 0.f;
	if (Pages) { Pages->SetActiveWidgetIndex(NewPage == ETNRoomMenuPage::Create ? 0 : 1); }
	if (TitleText)
	{
		TitleText->SetText(NewPage == ETNRoomMenuPage::Create ? NSLOCTEXT("TNRooms", "CreateTitle", "CREAR PARTIDA")
			: NSLOCTEXT("TNRooms", "JoinTitle", "UNIRSE A UNA PARTIDA"));
	}

	if (NewPage == ETNRoomMenuPage::Create)
	{
		// Borrador nuevo cada vez: lo último elegido, con un nombre y un código nuevos.
		if (GameInstance)
		{
			Draft = GameInstance->MakeRoomDraft();
			SizeOptions = GameInstance->GetRoomSizeOptions();
		}
		else
		{
			Draft = FTNRoomConfig();
			Draft.NameId = TNRoomNames::Random();
			Draft.Code = TNRoomCode::Generate();
			SizeOptions = { 4, 6, 8 };
		}
		RefreshCreateRows();
		FocusWidget(CreateButton);
	}
	else
	{
		RebuildRoomRows();
		RefreshRooms();
		FocusWidget(CodeField);
	}
	RefreshHint();
	if (!bWasOpen && OnOpenChanged) { OnOpenChanged(true); }
	PlayUISound(ETNPauseSound::Press, 0.f);
}

void UTN_RoomMenuWidget::Close()
{
	const bool bWasOpen = IsOpen();
	Page = ETNRoomMenuPage::Closed;
	if (ScreenLayer) { ScreenLayer->SetVisibility(ESlateVisibility::Collapsed); }
	if (NoticeText) { NoticeText->SetVisibility(ESlateVisibility::Collapsed); }
	NoticeTime = 0.f;
	LastFocused.Reset();
	if (bWasOpen && OnOpenChanged) { OnOpenChanged(false); }
}

void UTN_RoomMenuWidget::ShowNotice(const FText& Text, bool bError, float Seconds)
{
	if (Text.IsEmpty())
	{
		return;
	}
	const FLinearColor Color = bError ? TNHUDArt::CoralLight : TNHUDArt::Gold;
	if (IsOpen())
	{
		if (!NoticeText) { return; }
		NoticeText->SetText(Text);
		NoticeText->SetColorAndOpacity(FSlateColor(Color));
		NoticeText->SetRenderOpacity(1.f);
		NoticeText->SetVisibility(ESlateVisibility::HitTestInvisible);
		NoticeTime = Seconds;
		return;
	}
	if (!Toast || !ToastText) { return; }
	ToastText->SetText(Text);
	ToastText->SetColorAndOpacity(FSlateColor(bError ? TNHUDArt::CoralLight : TNHUDArt::Cream));
	Toast->SetRenderOpacity(1.f);
	Toast->SetVisibility(ESlateVisibility::HitTestInvisible);
	ToastTime = Seconds;
}

void UTN_RoomMenuWidget::HandleRoomNotice(const FText& Message, bool bError)
{
	ShowNotice(Message, bError, bError ? 8.f : 5.f);
}

void UTN_RoomMenuWidget::HandleRoomListChanged()
{
	if (Page == ETNRoomMenuPage::Join)
	{
		RebuildRoomRows();
	}
}

UMP_GameInstance* UTN_RoomMenuWidget::GetRoomGameInstance() const
{
	return Cast<UMP_GameInstance>(GetGameInstance());
}

// ─────────────────────────────────────────────────────────────────────────────
// Crear partida
// ─────────────────────────────────────────────────────────────────────────────

void UTN_RoomMenuWidget::RefreshCreateRows()
{
	TWeakObjectPtr<UTN_RoomMenuWidget> WeakThis(this);
	int32 ModeIndex = 0;
	const TArray<ETNProcGameMode> MenuModes = TNLobbyMission::GetMenuModes();
	for (int32 i = 0; i < MenuModes.Num(); ++i)
	{
		if (MenuModes[i] == Draft.Mode) { ModeIndex = i; }
	}
	if (ModeRow)
	{
		ModeRow->SetChoiceIndex(ModeIndex);
		ModeRow->SetDescription(Draft.Mode == ETNProcGameMode::TwoVsTwo
			? FText::Format(NSLOCTEXT("TNRooms", "Mode2v2Desc", "{0} Si al salir del lobby no sois cuatro, se juega Carrera."),
				TNLobbyMission::ModeBlurb(Draft.Mode))
			: TNLobbyMission::ModeBlurb(Draft.Mode));
	}
	const TArray<FName>& TctArenas = TNLobbyMission::TctArenaOptions();
	Draft.TctArena = TNLobbyMission::ResolveTctArena(Draft.TctArena, TctArenas);
	if (TctArenaRow)
	{
		TctArenaRow->SetChoiceIndex(FMath::Max(0, TctArenas.IndexOfByKey(Draft.TctArena)));
		TctArenaRow->SetVisibility(Draft.Mode == ETNProcGameMode::FreeForAll ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (VisibilityRow) { VisibilityRow->SetChoiceIndex(Draft.bPrivate ? 1 : 0); }
	if (SizeRow)
	{
		if (SizeOptions.Num() == 0) { SizeOptions = { TNRoomLimits::Max }; }
		TArray<FText> Sizes;
		for (const int32 Size : SizeOptions) { Sizes.Add(TNRoomUI::SizeText(Size)); }
		const int32 SizeIndex = SizeOptions.IndexOfByKey(Draft.MaxPlayers);
		if (SizeIndex == INDEX_NONE) { Draft.MaxPlayers = SizeOptions.Last(); }
		SizeRow->SetChoiceOptions(Sizes, SizeIndex == INDEX_NONE ? SizeOptions.Num() - 1 : SizeIndex);
	}
	if (NameRow)
	{
		NameRow->SetupEntry(NSLOCTEXT("TNRooms", "NameRow", "Nombre de la sala"), TNRoomNames::Get(Draft.NameId), NSLOCTEXT("TNRooms", "NameReroll", "Otro nombre"),
			[WeakThis]() { if (UTN_RoomMenuWidget* Menu = WeakThis.Get()) { Menu->RerollName(); } });
		NameRow->SetValueColors(TNHUDArt::SandC, TNHUDArt::SeaLight);
		NameRow->SetDescription(NSLOCTEXT("TNRooms", "NameDesc", "Sale al azar de la lista de salas (cada uno la lee en su idioma). Intro, A o clic: otro nombre."));
	}
	if (CodeRow)
	{
		const FString Code = Draft.Code;
		CodeRow->SetupEntry(NSLOCTEXT("TNRooms", "CodeRow", "Código de la sala"), TNLocText::Literal(Code), NSLOCTEXT("TNRooms", "CodeCopy", "Copiar"),
			[WeakThis, Code]()
			{
				FPlatformApplicationMisc::ClipboardCopy(*Code);
				if (UTN_RoomMenuWidget* Menu = WeakThis.Get())
				{
					Menu->ShowNotice(FText::Format(NSLOCTEXT("TNRooms", "CodeCopied", "Código {0} copiado: pásaselo a tus amigos."), TNLocText::Literal(Code)), false, 4.f);
				}
			});
		CodeRow->SetValueColors(TNHUDArt::Gold, TNHUDArt::SeaLight);
		CodeRow->SetDescription(NSLOCTEXT("TNRooms", "CodeDesc", "Con este código tus amigos entran desde «Unirse». También lo verás en el menú de pausa. Intro, A o clic: copiarlo."));
		// Solo en la privada (a la pública se entra desde la lista).
		CodeRow->SetVisibility(Draft.bPrivate ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (CreateSummary)
	{
		// El mapa de la misión: la arena en Todos contra Todos.
		const FText MissionTitle = TNLobbyMission::MissionTitle(Draft.Mode,
			Draft.Mode == ETNProcGameMode::FreeForAll ? Draft.TctArena : FName(NAME_None));
		CreateSummary->SetText(Draft.bPrivate
			? FText::Format(NSLOCTEXT("TNRooms", "SummaryPrivate", "Sala privada de {0}, para {1} {1}|plural(one=tortuga,other=tortugas): no sale en la lista y tus amigos entran con el código {2} (o por invitación de Steam)."),
				MissionTitle, Draft.MaxPlayers, TNLocText::Literal(Draft.Code))
			: FText::Format(NSLOCTEXT("TNRooms", "SummaryPublic", "Sala pública de {0}, para {1} {1}|plural(one=tortuga,other=tortugas): sale en la lista de «Unirse» y entra quien quiera (puedes cerrarla desde el menú de pausa)."),
				MissionTitle, Draft.MaxPlayers));
	}
}

void UTN_RoomMenuWidget::RerollName()
{
	Draft.NameId = TNRoomNames::Random(Draft.NameId);
	RefreshCreateRows();
}

void UTN_RoomMenuWidget::HostRoom()
{
	UMP_GameInstance* GameInstance = GetRoomGameInstance();
	if (!GameInstance)
	{
		return;
	}
	GameInstance->RememberRoomDraft(Draft);
	// La pantalla de carga tapa el menú; si algo falla, el aviso sale aquí (HandleRoomNotice).
	GameInstance->HostRoom(Draft);
}

// ─────────────────────────────────────────────────────────────────────────────
// Unirse
// ─────────────────────────────────────────────────────────────────────────────

void UTN_RoomMenuWidget::RefreshRooms()
{
	AutoRefreshTime = TNRoomUI::AutoRefreshSeconds;
	if (UMP_GameInstance* GameInstance = GetRoomGameInstance())
	{
		GameInstance->RefreshRoomList();
	}
	RefreshListStatus();
}

void UTN_RoomMenuWidget::JoinByCode()
{
	UMP_GameInstance* GameInstance = GetRoomGameInstance();
	if (!GameInstance || !CodeField)
	{
		return;
	}
	if (!CodeField->IsComplete())
	{
		ShowNotice(NSLOCTEXT("TNRooms", "CodeMissing", "Faltan letras: el código tiene 5."), true, 4.f);
		FocusWidget(CodeField);
		return;
	}
	GameInstance->JoinRoomByCode(CodeField->GetCode());
}

void UTN_RoomMenuWidget::RebuildRoomRows()
{
	UMP_GameInstance* GameInstance = GetRoomGameInstance();
	if (!RoomScroll || !WidgetTree)
	{
		return;
	}
	// Sin perder el sitio: la posición de la fila enfocada (si estaba en la lista) y el desplazamiento.
	UWidget* Focused = LastFocused.Get();
	const int32 FocusedIndex = Focused && Focused->GetParent() == RoomScroll.Get() ? RoomScroll->GetChildIndex(Focused) : INDEX_NONE;
	const float Offset = RoomScroll->GetScrollOffset();
	RoomScroll->ClearChildren();

	TWeakObjectPtr<UTN_RoomMenuWidget> WeakThis(this);
	const TArray<FTNRoomListing> Rooms = GameInstance ? GameInstance->GetRoomListings() : TArray<FTNRoomListing>();
	for (int32 i = 0; i < Rooms.Num(); ++i)
	{
		const FTNRoomListing& Listing = Rooms[i];
		UTN_PauseRow* Row = NewRow();
		if (!Row)
		{
			continue;
		}
		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(RoomScroll->AddChild(Row)))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		const FText RoomName = TNRoomNames::Get(Listing.NameId);
		const FText ModeLine = Listing.HostName.IsEmpty() ? TNLobbyMission::ModeName(Listing.Mode)
			: FText::Format(NSLOCTEXT("TNRooms", "ModeHost", "{0} · de {1}"), TNLobbyMission::ModeName(Listing.Mode), TNLocText::Literal(Listing.HostName));
		const FText Count = FText::Format(NSLOCTEXT("TNRooms", "CountFmt", "{0}/{1}"), FText::AsNumber(Listing.Players), FText::AsNumber(Listing.MaxPlayers));
		FText State = Count;
		if (Listing.bLocked) { State = FText::Format(NSLOCTEXT("TNRooms", "CountLocked", "{0} · cerrada"), Count); }
		else if (Listing.IsFull()) { State = FText::Format(NSLOCTEXT("TNRooms", "CountFull", "{0} · llena"), Count); }
		Row->SetupEntry(RoomName, ModeLine, State, [WeakThis, i]()
		{
			UTN_RoomMenuWidget* Menu = WeakThis.Get();
			if (UMP_GameInstance* Owner = Menu ? Menu->GetRoomGameInstance() : nullptr) { Owner->JoinListedRoom(i); }
		}, Listing.bLocked ? TNRoomArt::LockIcon(true) : nullptr);
		Row->SetValueColors(TNHUDArt::SandC, Listing.CanJoin() ? TNHUDArt::SeaLight : TNHUDArt::CoralLight);
		Row->SetDescription(Listing.bLocked
			? FText::Format(NSLOCTEXT("TNRooms", "RowLockedDesc", "«{0}» está cerrada: el anfitrión no deja entrar a nadie más."), RoomName)
			: (Listing.IsFull() ? FText::Format(NSLOCTEXT("TNRooms", "RowFullDesc", "«{0}» está llena."), RoomName)
				: FText::Format(NSLOCTEXT("TNRooms", "RowDesc", "Intro, A o clic: entrar en «{0}»."), RoomName)));
	}
	if (Rooms.Num() == 0)
	{
		const bool bSearching = GameInstance && GameInstance->IsSearchingRooms();
		UTextBlock* Empty = TNRoomUI::Label(WidgetTree, bSearching || !GameInstance || !GameInstance->HasRoomListResult()
			? NSLOCTEXT("TNRooms", "ListSearching", "Buscando salas públicas...")
			: NSLOCTEXT("TNRooms", "ListEmpty", "No hay partidas públicas ahora mismo. Crea una o entra con un código."), TEXT("Regular"), 19, TNHUDStyle::TextDim);
		Empty->SetAutoWrapText(true);
		if (UScrollBoxSlot* EmptySlot = Cast<UScrollBoxSlot>(RoomScroll->AddChild(Empty)))
		{
			EmptySlot->SetPadding(FMargin(12.f, 10.f, 12.f, 10.f));
		}
	}
	RoomScroll->SetScrollOffset(Offset);
	RefreshListStatus();

	// Si el foco estaba en la lista, a la misma posición (o a lo más cercano).
	if (FocusedIndex != INDEX_NONE)
	{
		for (int32 i = FMath::Min(FocusedIndex, RoomScroll->GetChildrenCount() - 1); i >= 0; --i)
		{
			if (UTN_PauseRow* Row = Cast<UTN_PauseRow>(RoomScroll->GetChildAt(i)))
			{
				FocusWidget(Row);
				return;
			}
		}
		FocusWidget(CodeField);
	}
}

void UTN_RoomMenuWidget::RefreshListStatus()
{
	if (!ListStatus)
	{
		return;
	}
	const UMP_GameInstance* GameInstance = GetRoomGameInstance();
	if (GameInstance && GameInstance->IsSearchingRooms())
	{
		// Puntos que se mueven mientras busca.
		const int32 Dots = 1 + static_cast<int32>(Clock * 2.5f) % 3;
		ListStatus->SetText(FText::Format(NSLOCTEXT("TNRooms", "Searching", "Buscando{0}"), TNLocText::Literal(FString::ChrN(Dots, TEXT('.')))));
		return;
	}
	const int32 Count = GameInstance ? GameInstance->GetRoomListings().Num() : 0;
	if (!GameInstance || !GameInstance->HasRoomListResult())
	{
		ListStatus->SetText(FText::GetEmpty());
	}
	else
	{
		ListStatus->SetText(FText::Format(NSLOCTEXT("TNRooms", "RoomsCount", "{0} {0}|plural(one=sala,other=salas)"), Count));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Entrada, foco y sonido
// ─────────────────────────────────────────────────────────────────────────────

void UTN_RoomMenuWidget::GoBack()
{
	if (!IsOpen())
	{
		return;
	}
	PlayUISound(ETNPauseSound::Press, 0.f);
	Close();
}

void UTN_RoomMenuWidget::FocusWidget(UWidget* Target)
{
	if (Target && Target->IsVisible())
	{
		Target->SetKeyboardFocus();
		LastFocused = Target;
	}
}

void UTN_RoomMenuWidget::FocusFirst()
{
	if (Page == ETNRoomMenuPage::Create)
	{
		UWidget* First = ModeRow ? ModeRow.Get() : CreateButton.Get();
		FocusWidget(First);
	}
	else if (Page == ETNRoomMenuPage::Join)
	{
		FocusWidget(CodeField);
	}
}

void UTN_RoomMenuWidget::HandleFocused(UWidget* Focused, const FText& Description)
{
	LastFocused = Focused;
	if (HelpText) { HelpText->SetText(Description); }
}

void UTN_RoomMenuWidget::RefreshHint()
{
	if (!HintText)
	{
		return;
	}
	HintText->SetText(Page == ETNRoomMenuPage::Join
		? NSLOCTEXT("TNRooms", "HintJoin", "Intro · A  Elegir      Ctrl+V  Pegar el código      F5 · Y  Actualizar      Esc · B  Volver")
		: NSLOCTEXT("TNRooms", "HintCreate", "Intro · A  Elegir      ← →  Cambiar      Esc · B  Volver"));
}

void UTN_RoomMenuWidget::PlayUISound(ETNPauseSound Sound, float Pitch)
{
	// Los mismos «pom» y «plin» que el menú de pausa, sin ametrallar.
	const double Now = FPlatformTime::Seconds();
	if (Sound != ETNPauseSound::Press && Now - LastSoundTime < 0.06)
	{
		return;
	}
	LastSoundTime = Now;
	if (!Synth.IsValid()) { Synth = UTN_ScoreShellSynthComponent::Attach2D(GetOwningPlayer()); }
	UTN_ScoreShellSynthComponent* Comp = Synth.Get();
	if (!Comp)
	{
		return;
	}
	switch (Sound)
	{
	case ETNPauseSound::Hover: Comp->TriggerSound(ETNScoreShellSound::Pom, 0, 7.f, 0.3f); break;
	case ETNPauseSound::Press: Comp->TriggerSound(ETNScoreShellSound::Plin, 1, 0.f, 0.7f); break;
	default:                   Comp->TriggerSound(ETNScoreShellSound::Pom, 0, FMath::Lerp(0.f, 12.f, FMath::Clamp(Pitch, 0.f, 1.f)), 0.4f); break;
	}
}

void UTN_RoomMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Clock += InDeltaTime;

	// Avisos: se apagan solos (el último medio segundo, desvaneciéndose).
	if (NoticeTime > 0.f && NoticeText)
	{
		NoticeTime -= InDeltaTime;
		NoticeText->SetRenderOpacity(FMath::Clamp(NoticeTime / 0.5f, 0.f, 1.f));
		if (NoticeTime <= 0.f) { NoticeText->SetVisibility(ESlateVisibility::Collapsed); }
	}
	if (ToastTime > 0.f && Toast)
	{
		ToastTime -= InDeltaTime;
		Toast->SetRenderOpacity(FMath::Clamp(ToastTime / 0.6f, 0.f, 1.f));
		if (ToastTime <= 0.f) { Toast->SetVisibility(ESlateVisibility::Collapsed); }
	}

	if (!IsOpen())
	{
		return;
	}

	// «Unirse»: el estado de la búsqueda y, cada poco, otra búsqueda sola.
	if (Page == ETNRoomMenuPage::Join)
	{
		const UMP_GameInstance* GameInstance = GetRoomGameInstance();
		if (GameInstance && GameInstance->IsSearchingRooms())
		{
			RefreshListStatus();
		}
		else
		{
			AutoRefreshTime -= InDeltaTime;
			if (AutoRefreshTime <= 0.f) { RefreshRooms(); }
		}
	}

	// Sin nada enfocado (clic en el velo, cambio de ventana...), el foco vuelve a la última opción; con la consola abierta, no.
	const UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	const bool bConsoleOpen = Viewport && Viewport->ViewportConsole && Viewport->ViewportConsole->ConsoleActive();
	if (!bConsoleOpen && !HasFocusedDescendants())
	{
		UWidget* Last = LastFocused.Get();
		if (Last && Last->GetParent() && Last->IsVisible()) { FocusWidget(Last); }
		else { FocusFirst(); }
	}
}

FReply UTN_RoomMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	using TNRoomUI::IsKey;
	if (!IsOpen())
	{
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}
	const FKey Key = InKeyEvent.GetKey();
	if (IsKey(Key, { EKeys::Escape, EKeys::Gamepad_FaceButton_Right, EKeys::Virtual_Back }))
	{
		if (!InKeyEvent.IsRepeat()) { GoBack(); }
		return FReply::Handled();
	}
	if (Page == ETNRoomMenuPage::Join && IsKey(Key, { EKeys::F5, EKeys::Gamepad_FaceButton_Top }))
	{
		// Con gafas, la Y de los Touch llega aquí antes de ir atrás (#648).
		TNVRMenuClaim::Claim();
		if (!InKeyEvent.IsRepeat())
		{
			PlayUISound(ETNPauseSound::Press, 0.f);
			RefreshRooms();
		}
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UTN_RoomMenuWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!IsOpen())
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	// Un clic fuera de las opciones: el foco vuelve a la última.
	UWidget* Last = LastFocused.Get();
	if (Last && Last->GetParent() && Last->IsVisible()) { FocusWidget(Last); }
	return FReply::Handled();
}
