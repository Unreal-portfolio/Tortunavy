#include "UI/Briefing/TN_BriefingWidget.h"
#include "../HUD/TN_HUDFaces.h"
#include "../HUD/TN_HUDStyle.h"
#include "../Shop/TN_ShopArt.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/TN_LocText.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerState.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "Lobby/TN_GeneralBriefing.h"
#include "Lobby/TN_LobbyMission.h"
#include "Lobby/TN_ShopKeeper.h"
#include "Player/MP_GamePlayerController.h"
#include "UI/Shop/TN_ShopWidgets.h"

namespace TNBriefingUI
{
	const FMargin CardBoxMargin(0.16f, 0.2f, 0.16f, 0.34f);
	const FMargin RibbonBoxMargin(0.14f, 0.f, 0.14f, 0.f);
	const FMargin PillBoxMargin(0.4f, 0.f, 0.4f, 0.f);
	const FMargin TagBoxMargin(0.2f, 0.f, 0.2f, 0.f);
	const FMargin BubbleBoxMargin(0.26f, 0.3f, 0.18f, 0.45f);
	const TCHAR* const ControlsFolder = TEXT("/Game/Blueprints/Gameplay/Controls/");

	/** Pestañas, en su orden: «Misión» la primera (se abre en ella). */
	constexpr int32 TabMission = 0;
	constexpr int32 TabHowTo = 1;
	constexpr int32 TabModes = 2;
	constexpr int32 TabRules = 3;
	constexpr int32 NumTabs = 5;

	/** Opciones de «Misión» (TNLobbyMission): las dificultades (los modos, TNLobbyMission::GetMenuModes, dependen de la build). */
	constexpr int32 NumDifficulties = static_cast<int32>(UE_ARRAY_COUNT(TNLobbyMission::Difficulties));

	/** Pastilla azul (pestaña u opción sin elegir) y coral (la elegida). */
	UTexture2D* IdlePill() { return TNShopArt::Pill(0x2A5A92, 0x173A66); }
	UTexture2D* ChosenPill() { return TNShopArt::Pill(0xFF8A70, 0xD9432F); }

	/** Lo que dice el general al elegir cada modo y cada dificultad. */
	FText ModeOrderLine(ETNProcGameMode Mode)
	{
		switch (Mode)
		{
		case ETNProcGameMode::Race:
			return NSLOCTEXT("Tortunabo", "BriefingOrderRace", "¡Carrera! Todas contra todas hasta el agua. Tres conchas y al podio.");
		case ETNProcGameMode::Survival:
			return NSLOCTEXT("Tortunabo", "BriefingOrderSurvival", "¡Supervivencia! Nivel tras nivel, cada uno peor que el anterior. Solo queda en pie la última.");
		case ETNProcGameMode::Karts:
			return NSLOCTEXT("Tortunabo", "BriefingOrderKarts", "¡Karts! Al volante sola o con una artillera detrás. Cajas, géiseres y cascadas hasta la playa.");
		case ETNProcGameMode::Rally:
			return NSLOCTEXT("Tortunabo", "BriefingOrderRally", "¡Rally! Circuito de autor, puertas en orden y una artillera con las cajas «?». Nada de atajos.");
		case ETNProcGameMode::TwoVsTwo:
			return NSLOCTEXT("Tortunabo", "BriefingOrder2v2", "¡2 vs 2! Por parejas y hasta el agua. Si al salir no sois cuatro, se corre la Carrera.");
		case ETNProcGameMode::FreeForAll:
			return NSLOCTEXT("Tortunabo", "BriefingOrderFreeForAll", "¡Todos contra Todos! El mar sube y no cabemos todas. La última en pie se lleva la concha.");
		default:
			return NSLOCTEXT("Tortunabo", "BriefingOrderCoop", "¡Cooperativo! Aquí no se deja a nadie atrás: del castillo al mar, todas juntas.");
		}
	}

	FText DifficultyOrderLine(ETNProcDifficulty Difficulty)
	{
		switch (Difficulty)
		{
		case ETNProcDifficulty::Easy: return NSLOCTEXT("Tortunabo", "BriefingOrderEasy", "Fácil: un paseo por la playa para calentar las aletas.");
		case ETNProcDifficulty::Hard: return NSLOCTEXT("Tortunabo", "BriefingOrderHard", "¡Difícil! Solo para veteranas con el caparazón bien duro.");
		default:                      return NSLOCTEXT("Tortunabo", "BriefingOrderNormal", "Normal: lo que manda el reglamento. Ni más ni menos.");
		}
	}

	template <typename T>
	T* New(UWidgetTree* Tree)
	{
		return Tree->ConstructWidget<T>(T::StaticClass());
	}

	UTextBlock* Label(UWidgetTree* Tree, const FText& Content, FName Weight, int32 FontSize, const FLinearColor& Color, bool bOutline)
	{
		UTextBlock* Out = New<UTextBlock>(Tree);
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

	UBorder* Framed(UWidgetTree* Tree, UTexture2D* Tex, const FMargin& Margin, UWidget* Content, const FMargin& Padding)
	{
		UBorder* Out = New<UBorder>(Tree);
		Out->SetBrush(BoxBrush(Tex, Margin));
		Out->SetPadding(Padding);
		Out->SetHorizontalAlignment(HAlign_Fill);
		Out->SetVerticalAlignment(VAlign_Fill);
		if (Content) { Out->SetContent(Content); }
		return Out;
	}

	USizeBox* Sized(UWidgetTree* Tree, UWidget* Content, float W, float H)
	{
		USizeBox* Out = New<USizeBox>(Tree);
		if (W > 0.f) { Out->SetWidthOverride(W); }
		if (H > 0.f) { Out->SetHeightOverride(H); }
		if (Content) { Out->SetContent(Content); }
		return Out;
	}

	void Pin(UCanvasPanel* Canvas, UWidget* W, const FVector2D& Anchor, const FVector2D& Offset)
	{
		UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(W);
		CanvasSlot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
		CanvasSlot->SetAlignment(Anchor);
		CanvasSlot->SetPosition(Offset);
		CanvasSlot->SetAutoSize(true);
	}

	void AddH(UHorizontalBox* Box, UWidget* W, const FMargin& Padding = FMargin(0.f), EVerticalAlignment V = VAlign_Center)
	{
		UHorizontalBoxSlot* HSlot = Box->AddChildToHorizontalBox(W);
		HSlot->SetPadding(Padding);
		HSlot->SetVerticalAlignment(V);
	}

	void AddV(UVerticalBox* Box, UWidget* W, const FMargin& Padding = FMargin(0.f), EHorizontalAlignment H = HAlign_Fill)
	{
		UVerticalBoxSlot* VSlot = Box->AddChildToVerticalBox(W);
		VSlot->SetPadding(Padding);
		VSlot->SetHorizontalAlignment(H);
	}

	bool IsKey(const FKey& Key, std::initializer_list<FKey> Options)
	{
		for (const FKey& Option : Options) { if (Key == Option) { return true; } }
		return false;
	}

	FString Action(const TCHAR* Name)
	{
		return FString::Printf(TEXT("%s%s.%s"), ControlsFolder, Name, Name);
	}

	/**
	 * Nombre corto de una tecla, en el idioma del juego (las raras, como las da el motor). Los símbolos y las letras de los
	 * botones del mando no se traducen (INVTEXT); las palabras («Espacio», «Clic izq.») sí, con las claves de TNKeys.
	 */
	FText KeyLabel(const FKey& Key)
	{
		struct FNamed { FKey Key; FText Name; };
		// Estático local, no de archivo: los NSLOCTEXT se crean con el sistema de localización ya en marcha.
		static const FNamed Names[] = {
			{ EKeys::SpaceBar, NSLOCTEXT("TNKeys", "Space", "Espacio") }, { EKeys::LeftShift, NSLOCTEXT("TNKeys", "ShortShift", "Mayús") },
			{ EKeys::RightShift, NSLOCTEXT("TNKeys", "RightShift", "Mayús der.") },
			{ EKeys::LeftControl, NSLOCTEXT("TNKeys", "ShortCtrl", "Ctrl") }, { EKeys::RightControl, NSLOCTEXT("TNKeys", "RightCtrl", "Ctrl der.") },
			{ EKeys::LeftAlt, NSLOCTEXT("TNKeys", "Alt", "Alt") },
			{ EKeys::Mouse2D, NSLOCTEXT("TNKeys", "Mouse", "Ratón") }, { EKeys::MouseX, NSLOCTEXT("TNKeys", "Mouse", "Ratón") },
			{ EKeys::MouseY, NSLOCTEXT("TNKeys", "Mouse", "Ratón") },
			{ EKeys::LeftMouseButton, NSLOCTEXT("TNKeys", "ShortMouseLeft", "Clic izq.") },
			{ EKeys::RightMouseButton, NSLOCTEXT("TNKeys", "ShortMouseRight", "Clic der.") },
			{ EKeys::MiddleMouseButton, NSLOCTEXT("TNKeys", "ShortMouseMiddle", "Clic rueda") },
			{ EKeys::MouseScrollUp, NSLOCTEXT("TNKeys", "Wheel", "Rueda") }, { EKeys::MouseScrollDown, NSLOCTEXT("TNKeys", "Wheel", "Rueda") },
			{ EKeys::MouseWheelAxis, NSLOCTEXT("TNKeys", "Wheel", "Rueda") },
			{ EKeys::Enter, NSLOCTEXT("TNKeys", "Enter", "Intro") }, { EKeys::Escape, NSLOCTEXT("TNKeys", "Escape", "Esc") },
			{ EKeys::Tab, NSLOCTEXT("TNKeys", "Tab", "Tab") },
			{ EKeys::Up, INVTEXT("↑") }, { EKeys::Down, INVTEXT("↓") }, { EKeys::Left, INVTEXT("←") }, { EKeys::Right, INVTEXT("→") },
			{ EKeys::Gamepad_FaceButton_Bottom, INVTEXT("A") }, { EKeys::Gamepad_FaceButton_Right, INVTEXT("B") },
			{ EKeys::Gamepad_FaceButton_Left, INVTEXT("X") }, { EKeys::Gamepad_FaceButton_Top, INVTEXT("Y") },
			{ EKeys::Gamepad_LeftShoulder, INVTEXT("LB") }, { EKeys::Gamepad_RightShoulder, INVTEXT("RB") },
			{ EKeys::Gamepad_LeftTrigger, INVTEXT("LT") }, { EKeys::Gamepad_LeftTriggerAxis, INVTEXT("LT") },
			{ EKeys::Gamepad_RightTrigger, INVTEXT("RT") }, { EKeys::Gamepad_RightTriggerAxis, INVTEXT("RT") },
			{ EKeys::Gamepad_Left2D, NSLOCTEXT("TNKeys", "ShortLeftStick", "Stick izq.") }, { EKeys::Gamepad_LeftX, NSLOCTEXT("TNKeys", "ShortLeftStick", "Stick izq.") },
			{ EKeys::Gamepad_LeftY, NSLOCTEXT("TNKeys", "ShortLeftStick", "Stick izq.") },
			{ EKeys::Gamepad_Right2D, NSLOCTEXT("TNKeys", "ShortRightStick", "Stick der.") }, { EKeys::Gamepad_RightX, NSLOCTEXT("TNKeys", "ShortRightStick", "Stick der.") },
			{ EKeys::Gamepad_RightY, NSLOCTEXT("TNKeys", "ShortRightStick", "Stick der.") },
			{ EKeys::Gamepad_LeftThumbstick, INVTEXT("L3") }, { EKeys::Gamepad_RightThumbstick, INVTEXT("R3") },
			{ EKeys::Gamepad_DPad_Up, NSLOCTEXT("TNKeys", "ShortDpadUp", "Cruceta ↑") }, { EKeys::Gamepad_DPad_Down, NSLOCTEXT("TNKeys", "ShortDpadDown", "Cruceta ↓") },
			{ EKeys::Gamepad_DPad_Left, NSLOCTEXT("TNKeys", "ShortDpadLeft", "Cruceta ←") }, { EKeys::Gamepad_DPad_Right, NSLOCTEXT("TNKeys", "ShortDpadRight", "Cruceta →") },
			{ EKeys::Gamepad_Special_Right, NSLOCTEXT("TNKeys", "ShortStart", "Start") }, { EKeys::Gamepad_Special_Left, NSLOCTEXT("TNKeys", "ShortSelect", "Select") },
		};
		for (const FNamed& Named : Names)
		{
			if (Named.Key == Key) { return Named.Name; }
		}
		return Key.GetDisplayName(false);
	}

	/** Añade un nombre de tecla si no está ya (FText no se compara con ==: se mira el texto que se ve). */
	void AddUniqueLabel(TArray<FText>& List, const FText& Label)
	{
		const FString Shown = Label.ToString();
		for (const FText& Existing : List)
		{
			if (Existing.ToString().Equals(Shown)) { return; }
		}
		List.Add(Label);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Construcción
// ─────────────────────────────────────────────────────────────────────────────

void UTN_BriefingWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	BuildTree();
}

void UTN_BriefingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// La radio del puesto baja mientras el general habla.
	ATN_ShopKeeper::SetRadiosDucked(GetWorld(), true);
	SetKeyboardFocus();
}

void UTN_BriefingWidget::NativeDestruct()
{
	ATN_ShopKeeper::SetRadiosDucked(GetWorld(), false);
	Super::NativeDestruct();
}

void UTN_BriefingWidget::BuildTree()
{
	using namespace TNBriefingUI;
	if (!WidgetTree || WidgetTree->RootWidget) { return; }
	UWidgetTree* Tree = WidgetTree;
	UCanvasPanel* Canvas = New<UCanvasPanel>(Tree);
	Tree->RootWidget = Canvas;

	// Velo azul marino sobre el juego.
	UImage* Veil = New<UImage>(Tree);
	Veil->SetColorAndOpacity(TNHUDArt::Hex(0x0A1C38, 0.66f));
	if (UCanvasPanelSlot* VeilSlot = Canvas->AddChildToCanvas(Veil))
	{
		VeilSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		VeilSlot->SetOffsets(FMargin(0.f));
	}

	// Título en cinta coral.
	TitleText = Label(Tree, NSLOCTEXT("Tortunabo", "BriefingTitle", "CUARTEL GENERAL"), TEXT("Black"), 34, TNHUDArt::Cream, true);
	UBorder* Title = Framed(Tree, TNHUDArt::RibbonTexture(), RibbonBoxMargin, TitleText, FMargin(70.f, 8.f, 70.f, 14.f));
	Title->SetHorizontalAlignment(HAlign_Center);
	Pin(Canvas, Title, FVector2D(0.5f, 0.f), FVector2D(0.f, 26.f));

	// Izquierda: el general y su bocadillo.
	UVerticalBox* Left = New<UVerticalBox>(Tree);
	{
		UImage* Face = New<UImage>(Tree);
		FSlateBrush FaceBrush;
		FaceBrush.SetResourceObject(TNHUDFaces::TurtleFace(ETNTurtleFace::Happy));
		FaceBrush.ImageSize = FVector2D(170.f, 170.f);
		Face->SetBrush(FaceBrush);
		AddV(Left, Face, FMargin(0.f), HAlign_Center);
		NameText = Label(Tree, NSLOCTEXT("Tortunabo", "GeneralName", "General Galápago"), TEXT("Bold"), 19, TNHUDArt::Cream, true);
		UBorder* NameRibbon = Framed(Tree, TNHUDArt::RibbonTexture(), RibbonBoxMargin, NameText, FMargin(34.f, 5.f, 34.f, 10.f));
		NameRibbon->SetHorizontalAlignment(HAlign_Center);
		AddV(Left, NameRibbon, FMargin(0.f, -16.f, 0.f, 10.f), HAlign_Center);
		DialogText = Label(Tree, FText::GetEmpty(), TEXT("Bold"), 21, TNHUDArt::Ink, false);
		DialogText->SetAutoWrapText(true);
		USizeBox* DialogSize = Sized(Tree, DialogText, 420.f, 0.f);
		DialogSize->SetMinDesiredHeight(150.f);
		AddV(Left, Framed(Tree, TNHUDArt::ChatBubbleTexture(), BubbleBoxMargin, DialogSize, FMargin(40.f, 18.f, 26.f, 32.f)), FMargin(0.f), HAlign_Center);
	}
	Pin(Canvas, Left, FVector2D(0.04f, 0.52f), FVector2D::ZeroVector);

	// Derecha: pestañas, página y botón de cerrar.
	UVerticalBox* Right = New<UVerticalBox>(Tree);
	{
		UHorizontalBox* TabRow = New<UHorizontalBox>(Tree);
		const FText TabNames[NumTabs] = {
			NSLOCTEXT("Tortunabo", "BriefingTabMission", "MISIÓN"),
			NSLOCTEXT("Tortunabo", "BriefingTabHowTo", "CÓMO SE JUEGA"),
			NSLOCTEXT("Tortunabo", "BriefingTabModes", "MODOS DE JUEGO"),
			NSLOCTEXT("Tortunabo", "BriefingTabRules", "REGLAS"),
			NSLOCTEXT("Tortunabo", "BriefingTabControls", "CONTROLES"),
		};
		for (int32 i = 0; i < NumTabs; ++i)
		{
			UTN_ShopButton* TabButton = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
			// Cinco pestañas en el ancho de la página (antes eran cuatro de 240).
			TabButton->Setup(TabNames[i], IdlePill(), TNHUDArt::Cream, 18, FVector2D(186.f, 56.f),
				[this, i]() { ShowTab(i); });
			AddH(TabRow, TabButton, FMargin(0.f, 0.f, 8.f, 0.f));
			Tabs.Add(TabButton);
		}
		AddV(Right, TabRow, FMargin(10.f, 0.f, 0.f, 8.f), HAlign_Left);

		Page = New<UVerticalBox>(Tree);
		Scroll = New<UScrollBox>(Tree);
		Scroll->AddChild(Page);
		AddV(Right, Framed(Tree, TNHUDArt::CardTexture(), CardBoxMargin, Sized(Tree, Scroll, 980.f, 540.f), FMargin(30.f, 26.f, 30.f, 44.f)),
			FMargin(0.f), HAlign_Left);

		UHorizontalBox* Bottom = New<UHorizontalBox>(Tree);
		UTN_ShopButton* OkButton = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
		OkButton->Setup(NSLOCTEXT("Tortunabo", "BriefingOk", "¡ENTENDIDO!"), TNShopArt::Pill(0xFFD95E, 0xF2A93B), TNHUDArt::Ink, 24, FVector2D(320.f, 66.f),
			[this]() { Close(); });
		AddH(Bottom, OkButton, FMargin(0.f, 0.f, 20.f, 0.f));
		KeysHint = Label(Tree, NSLOCTEXT("Tortunabo", "BriefingKeys", "Q/E o flechas: pestaña · ↑/↓: desplazar · Esc: salir"),
			TEXT("Regular"), 15, TNHUDArt::SeaLight, true);
		AddH(Bottom, KeysHint);
		AddV(Right, Bottom, FMargin(10.f, 14.f, 0.f, 0.f), HAlign_Left);
	}
	Pin(Canvas, Right, FVector2D(0.97f, 0.54f), FVector2D::ZeroVector);
}

void UTN_BriefingWidget::SetGeneral(ATN_GeneralBriefing* InGeneral)
{
	General = InGeneral;
	if (InGeneral)
	{
		if (TitleText) { TitleText->SetText(InGeneral->GetHeadquartersName().ToUpper()); }
		if (NameText) { NameText->SetText(InGeneral->GetGeneralName()); }
	}
	ShowTab(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Páginas
// ─────────────────────────────────────────────────────────────────────────────

void UTN_BriefingWidget::AddHeading(const FText& Text)
{
	UTextBlock* Heading = TNBriefingUI::Label(WidgetTree, Text, TEXT("Black"), 23, TNHUDArt::CoralDeep, false);
	TNBriefingUI::AddV(Page, Heading, FMargin(0.f, Page->GetChildrenCount() > 0 ? 14.f : 0.f, 0.f, 4.f));
}

void UTN_BriefingWidget::AddParagraph(const FText& Text)
{
	UTextBlock* Paragraph = TNBriefingUI::Label(WidgetTree, Text, TEXT("Regular"), 19, TNHUDArt::Ink, false);
	Paragraph->SetAutoWrapText(true);
	TNBriefingUI::AddV(Page, Paragraph, FMargin(0.f, 0.f, 8.f, 6.f));
}

void UTN_BriefingWidget::KeysFor(const TArray<FString>& ActionPaths, TArray<FText>& OutKeyboard, TArray<FText>& OutGamepad) const
{
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	const UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Input)
	{
		return;
	}
	for (const FString& Path : ActionPaths)
	{
		const UInputAction* InputAction = LoadObject<UInputAction>(nullptr, *Path);
		if (!InputAction)
		{
			continue;
		}
		for (const FKey& Key : Input->QueryKeysMappedToAction(InputAction))
		{
			if (!Key.IsValid())
			{
				continue;
			}
			TNBriefingUI::AddUniqueLabel(Key.IsGamepadKey() ? OutGamepad : OutKeyboard, TNBriefingUI::KeyLabel(Key));
		}
	}
}

void UTN_BriefingWidget::AddControlRow(const FText& ActionName, const TArray<FString>& ActionPaths)
{
	using namespace TNBriefingUI;
	UWidgetTree* Tree = WidgetTree;
	TArray<FText> Keyboard, Gamepad;
	KeysFor(ActionPaths, Keyboard, Gamepad);

	UHorizontalBox* Row = New<UHorizontalBox>(Tree);
	UTextBlock* Name = Label(Tree, ActionName, TEXT("Bold"), 19, TNHUDArt::Ink, false);
	Name->SetAutoWrapText(true);
	AddH(Row, Sized(Tree, Name, 330.f, 0.f), FMargin(0.f, 0.f, 12.f, 0.f));
	if (Keyboard.Num() == 0 && Gamepad.Num() == 0)
	{
		AddH(Row, Label(Tree, NSLOCTEXT("Tortunabo", "BriefingNoKey", "— sin tecla —"), TEXT("Regular"), 17, TNHUDArt::WetSand, false));
	}
	for (const FText& KeyName : Keyboard)
	{
		UTextBlock* KeyText = Label(Tree, KeyName, TEXT("Bold"), 16, TNHUDArt::Ink, false);
		AddH(Row, Framed(Tree, TNHUDArt::SandTagTexture(), TagBoxMargin, KeyText, FMargin(16.f, 4.f, 16.f, 8.f)), FMargin(0.f, 0.f, 6.f, 0.f));
	}
	for (const FText& KeyName : Gamepad)
	{
		UTextBlock* KeyText = Label(Tree, KeyName, TEXT("Bold"), 15, TNHUDArt::Cream, true);
		AddH(Row, Framed(Tree, TNShopArt::Pill(0x3B6EA8, 0x1D3F6E), PillBoxMargin, KeyText, FMargin(18.f, 5.f, 18.f, 9.f)), FMargin(0.f, 0.f, 6.f, 0.f));
	}
	AddV(Page, Row, FMargin(0.f, 3.f, 0.f, 3.f));
}

void UTN_BriefingWidget::ShowTab(int32 Index)
{
	using TNBriefingUI::Action;
	using TNBriefingUI::NumTabs;
	Tab = (Index % NumTabs + NumTabs) % NumTabs;
	for (int32 i = 0; i < Tabs.Num(); ++i)
	{
		Tabs[i]->SetArt(i == Tab ? TNBriefingUI::ChosenPill() : TNBriefingUI::IdlePill());
	}
	RefreshKeysHint();
	if (!Page)
	{
		return;
	}
	Page->ClearChildren();
	// Lo de «Misión» solo existe mientras se ve.
	ModeButtons.Reset();
	DifficultyButtons.Reset();
	SeatsButtons.Reset();
	MapButtons.Reset();
	ModeHeading = nullptr;
	DifficultyHeading = nullptr;
	SeatsHeading = nullptr;
	SeatsRow = nullptr;
	MapHeading = nullptr;
	MapRow = nullptr;
	MissionOrders = nullptr;

	const APlayerState* PS = GetOwningPlayer() ? GetOwningPlayer()->PlayerState : nullptr;
	const FText Who = PS ? TNLocText::Literal(PS->GetPlayerName()) : NSLOCTEXT("Tortunabo", "BriefingRecruit", "recluta");

	switch (Tab)
	{
	case TNBriefingUI::TabMission:
		BuildMissionPage();
		break;
	case TNBriefingUI::TabHowTo:
		Say(FText::Format(NSLOCTEXT("Tortunabo", "BriefingSayHowTo",
			"¡Firmes, {0}! Soy el General Galápago. De este cuartel se sale hacia el mar... y se sale sabiendo."), Who));
		AddHeading(NSLOCTEXT("Tortunabo", "BriefingGoalH", "El objetivo"));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingGoal",
			"Acabáis de salir del huevo y el mar os espera. El camino cruza selva, playa, desierto, volcán, rocas y manglar: seguidlo hasta la meta, el arco de neumático gigante junto a la orilla, y ¡al agua!"));
		AddHeading(NSLOCTEXT("Tortunabo", "BriefingMoveH", "Moverse"));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingMove1",
			"•  Anda y esprinta (el sprint gasta aliento: mira el salvavidas del HUD). Salta y, en el aire, pulsa saltar otra vez para el panzazo: te lanzas en plancha y cruzas huecos que andando no se cruzan."));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingMove2",
			"•  En el agua se nada. Si saltas nadando, das un impulso para subir a las orillas y a las isletas."));
		AddHeading(NSLOCTEXT("Tortunabo", "BriefingShellH", "El caparazón"));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingShell1",
			"•  Métete en tu caparazón: caes al suelo y ruedas, resbalas y rebotas con física de verdad (¡y los demás te pueden empujar!). Pulsa otra vez para salir."));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingShell2",
			"•  Con Interactuar coges a una tortuga metida en su caparazón (o noqueada) y la lanzas hacia donde miras: sale dando volteretas y no puede salir hasta que se para. Si te llevan a ti, muévete sin parar dos segundos para escaparte."));
		AddHeading(NSLOCTEXT("Tortunabo", "BriefingDangerH", "Cuidado"));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingDanger1",
			"•  Plátanos y golpes te noquean: te quedas un momento en el suelo con los pajaritos dando vueltas y luego te levantas."));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingDanger2",
			"•  Si caes más de 5 m te haces bola tú solo; si caes de muy alto, te rompes. Géiseres, toboganes y agua no cuentan, ni el salto del acantilado de la meta en la carrera: ese se hace de cabeza."));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingDanger3",
			"•  La fauna del agua muerde. Las pilas de huevos del camino son vuestros puntos de reaparición: pasad por ellas."));
		break;
	case TNBriefingUI::TabModes:
		Say(NSLOCTEXT("Tortunabo", "BriefingSayModes",
			"Cuatro formas de llegar al mar. La de hoy la fija el anfitrión conmigo, en la pestaña «Misión»."));
		AddHeading(NSLOCTEXT("Tortunabo", "BriefingCoopH", "Cooperativo (de 1 a 8 tortugas)"));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingCoop",
			"Todo el equipo tiene que llegar a la meta. Una tormenta avanza por el camino detrás de vosotros y nunca va más rápido que una tortuga andando: si os quedáis atrás, os alcanza. Es el mapa más largo."));
		AddHeading(NSLOCTEXT("Tortunabo", "BriefingRaceH", "Carrera (de 1 a 8)"));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingRace",
			"Todas contra todas en la playa: la primera que salta del acantilado y toca el agua gana la ronda y una concha, y la partida es para quien consiga tres. Aquí no se muere nadie: lo que en el cooperativo mata, en la playa te deja un rato hecha una bola."));
		AddHeading(NSLOCTEXT("Tortunabo", "Briefing2v2H", "2 vs 2 (exactamente 4)"));
		AddParagraph(NSLOCTEXT("Tortunabo", "Briefing2v2",
			"Por parejas: gana la ronda la pareja cuyos dos miembros llegan antes. Hay muros que solo se superan lanzando al compañero (o bajando la rampa con el interruptor) y compuertas para sabotear a la otra pareja. Las parejas cambian cada ronda; si no sois cuatro, se juega Carrera."));
		AddHeading(NSLOCTEXT("Tortunabo", "BriefingClassicH", "Clásico"));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingClassic", "El recorrido de siempre, por tramos, para los veteranos del cuartel."));
		AddHeading(NSLOCTEXT("Tortunabo", "BriefingSurvivalH", "Supervivencia"));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingSurvival",
			"Niveles cortos del recorrido de siempre, uno tras otro y cada vez más duros. Cuando todas las que siguen vivas llegan a la meta, empieza otro. Quien cae mira desde la grada, y gana la última en pie (si caen las últimas a la vez, la que cayó más cerca de la meta). Sola, dura hasta que caigas."));
		AddHeading(NSLOCTEXT("Tortunabo", "BriefingPickH", "Cómo se elige"));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingPick",
			"El anfitrión, aquí conmigo, en la pestaña «Misión»: Cooperativo, Carrera o Supervivencia, y la dificultad (Fácil, Normal o Difícil), que en el cooperativo cambia el tamaño del mapa, los cruces colosales, los huecos y lo rápida que va la tormenta. También al crear la partida en el menú y, si el cuartel tiene selectores de modo y dificultad, con ellos (2 vs 2 y Clásico solo salen ahí)."));
		break;
	case TNBriefingUI::TabRules:
		Say(NSLOCTEXT("Tortunabo", "BriefingSayRules", "Las normas del cuartel no se discuten. Bueno, se pueden discutir... pero se pierde."));
		AddHeading(NSLOCTEXT("Tortunabo", "BriefingStartH", "La salida"));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingStart",
			"•  Cuando estéis listos, id a la sala de espera junto a la gran puerta del castillo y meteos cada uno en un huevo. Con todos dentro empieza la cuenta atrás, la puerta se abre... ¡y a la playa!"));
		AddHeading(NSLOCTEXT("Tortunabo", "BriefingRespawnH", "Reaparición"));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingRespawn1",
			"•  Si caes, vuelves a la última pila de huevos alcanzada que quede por delante de la tormenta: en Cooperativo, la más lejana del equipo; en Carrera y 2 vs 2, la tuya."));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingRespawn2", "•  Sin pila válida te quedas en el suelo y un compañero tiene que rescatarte."));
		AddHeading(NSLOCTEXT("Tortunabo", "BriefingTimeH", "Tiempo"));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingTime",
			"•  En la Carrera de la playa cada ronda dura como mucho 6 minutos: si se acaba, gana la más cerca del mar. En 2 vs 2, 15 minutos, y gana el más adelantado en el camino."));
		AddHeading(NSLOCTEXT("Tortunabo", "BriefingFairH", "Juego limpio"));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingFair",
			"•  Se puede coger y lanzar a cualquiera que esté metido en su caparazón, también a los rivales. Lo que no se puede es quedarse en la salida molestando: el general lo ve todo."));
		break;
	default:
		Say(NSLOCTEXT("Tortunabo", "BriefingSayControls", "Estas son tus teclas de verdad, recluta. Apréndetelas antes de salir del huevo."));
		AddHeading(NSLOCTEXT("Tortunabo", "BriefingKeysH", "Tus teclas"));
		AddParagraph(NSLOCTEXT("Tortunabo", "BriefingKeysNote", "Leídas de tu configuración actual. En azul, las del mando."));
		AddControlRow(NSLOCTEXT("Tortunabo", "CtlMove", "Moverse"), { Action(TEXT("IA_Move")) });
		AddControlRow(NSLOCTEXT("Tortunabo", "CtlLook", "Mirar"), { Action(TEXT("IA_Look")) });
		AddControlRow(NSLOCTEXT("Tortunabo", "CtlJump", "Saltar (en el aire, otra vez: panzazo)"), { Action(TEXT("IA_Jump")) });
		AddControlRow(NSLOCTEXT("Tortunabo", "CtlSprint", "Esprintar"), { Action(TEXT("IA_Sprint")) });
		AddControlRow(NSLOCTEXT("Tortunabo", "CtlShell", "Meterse o salir del caparazón"), { Action(TEXT("IA_Shell")) });
		AddControlRow(NSLOCTEXT("Tortunabo", "CtlInteract", "Interactuar, coger y lanzar"), { Action(TEXT("IA_Interact")) });
		AddControlRow(NSLOCTEXT("Tortunabo", "CtlDrop", "Soltar el objeto (o a quien llevas)"), { Action(TEXT("IA_DropItem")) });
		AddControlRow(NSLOCTEXT("Tortunabo", "CtlRotate", "Cambiar de objeto"), { Action(TEXT("IA_RotateInventory")) });
		AddControlRow(NSLOCTEXT("Tortunabo", "CtlEmoteWheel", "Rueda de emotes"), { Action(TEXT("IA_OpenEmoteWheel")) });
		AddControlRow(NSLOCTEXT("Tortunabo", "CtlChatWheel", "Chat rápido"), { Action(TEXT("IA_OpenChatWheel")) });
		{
			TArray<FString> EmotePaths;
			for (int32 i = 1; i <= 9; ++i) { EmotePaths.Add(Action(*FString::Printf(TEXT("IA_Emote%d"), i))); }
			EmotePaths.Add(Action(TEXT("IA_Emote")));
			AddControlRow(NSLOCTEXT("Tortunabo", "CtlEmotes", "Emotes directos"), EmotePaths);
		}
		break;
	}
	if (Scroll) { Scroll->ScrollToStart(); }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pestaña «Misión»: modo y dificultad de la próxima partida (TNLobbyMission)
// ─────────────────────────────────────────────────────────────────────────────

void UTN_BriefingWidget::BuildMissionPage()
{
	using namespace TNBriefingUI;
	UWidgetTree* Tree = WidgetTree;
	const bool bHost = CanChooseMission();
	const APlayerState* PS = GetOwningPlayer() ? GetOwningPlayer()->PlayerState : nullptr;
	const FText Who = PS ? TNLocText::Literal(PS->GetPlayerName()) : NSLOCTEXT("Tortunabo", "BriefingRecruit", "recluta");
	Say(bHost
		? FText::Format(NSLOCTEXT("Tortunabo", "BriefingSayMissionHost",
			"¡Firmes, {0}! Tú mandas: ¿qué misión le damos hoy a la tropa? Elige el modo y la dificultad."), Who)
		: FText::Format(NSLOCTEXT("Tortunabo", "BriefingSayMissionGuest",
			"¡Firmes, {0}! Esta es la orden del día. La misión la decide el anfitrión: tú, a prepararte."), Who));

	// Modo: una pastilla por modo y, debajo, una línea de cada uno (las mismas del menú principal).
	ModeHeading = Label(Tree, FText::GetEmpty(), TEXT("Black"), 23, TNHUDArt::CoralDeep, false);
	AddV(Page, ModeHeading, FMargin(0.f, 0.f, 0.f, 6.f));
	// Con más de tres modos las pastillas se estrechan y van en filas de ModePillsPerRow (#632: con siete, una sola fila
	// medía más de 1300 px y se salía de la página): cada fila mide como mucho lo que medían cuatro.
	const TArray<ETNProcGameMode> MenuModes = TNLobbyMission::GetMenuModes();
	const bool bManyModes = MenuModes.Num() > 3;
	UHorizontalBox* ModeRow = nullptr;
	for (int32 Index = 0; Index < MenuModes.Num(); ++Index)
	{
		if (Index % ModePillsPerRow == 0)
		{
			ModeRow = New<UHorizontalBox>(Tree);
			AddV(Page, ModeRow, FMargin(0.f, 0.f, 0.f, 8.f), HAlign_Left);
		}
		const ETNProcGameMode Mode = MenuModes[Index];
		UTN_ShopButton* Option = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
		Option->Setup(TNLobbyMission::ModeName(Mode).ToUpper(), IdlePill(), TNHUDArt::Cream, bManyModes ? 17 : 21,
			FVector2D(bManyModes ? 200.f : 260.f, 58.f),
			[this, Mode]() { PickMode(Mode); });
		AddH(ModeRow, Option, FMargin(0.f, 0.f, 12.f, 0.f));
		ModeButtons.Add(Option);
	}
	for (const ETNProcGameMode Mode : MenuModes)
	{
		AddParagraph(FText::Format(NSLOCTEXT("Tortunabo", "BriefingMissionModeLine", "•  {0}: {1}"),
			TNLobbyMission::ModeName(Mode), TNLobbyMission::ModeBlurb(Mode)));
	}

	// Dificultad.
	DifficultyHeading = Label(Tree, FText::GetEmpty(), TEXT("Black"), 23, TNHUDArt::CoralDeep, false);
	AddV(Page, DifficultyHeading, FMargin(0.f, 12.f, 0.f, 6.f));
	UHorizontalBox* DifficultyRow = New<UHorizontalBox>(Tree);
	for (const ETNProcDifficulty Difficulty : TNLobbyMission::Difficulties)
	{
		UTN_ShopButton* Option = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
		Option->Setup(TNLobbyMission::DifficultyName(Difficulty).ToUpper(), IdlePill(), TNHUDArt::Cream, 21, FVector2D(200.f, 58.f),
			[this, Difficulty]() { PickDifficulty(Difficulty); });
		AddH(DifficultyRow, Option, FMargin(0.f, 0.f, 12.f, 0.f));
		DifficultyButtons.Add(Option);
	}
	AddV(Page, DifficultyRow, FMargin(0.f, 0.f, 0.f, 8.f), HAlign_Left);
	AddParagraph(FText::Format(NSLOCTEXT("Tortunabo", "BriefingMissionDiffLine",
		"•  En el cooperativo. {0}: {1} {2}: {3} {4}: {5} La playa de la carrera es siempre la misma."),
		TNLobbyMission::DifficultyName(ETNProcDifficulty::Easy), TNLobbyMission::DifficultyBlurb(ETNProcDifficulty::Easy),
		TNLobbyMission::DifficultyName(ETNProcDifficulty::Normal), TNLobbyMission::DifficultyBlurb(ETNProcDifficulty::Normal),
		TNLobbyMission::DifficultyName(ETNProcDifficulty::Hard), TNLobbyMission::DifficultyBlurb(ETNProcDifficulty::Hard)));

	// Rally y Karts (solo el anfitrión, que es quien lo elige): una tortuga por buggy o por parejas (la segunda, de
	// artillera) y, en el Rally, el circuito.
	if (bHost)
	{
		UVerticalBox* SeatsBox = New<UVerticalBox>(Tree);
		SeatsHeading = Label(Tree, FText::GetEmpty(), TEXT("Black"), 23, TNHUDArt::CoralDeep, false);
		AddV(SeatsBox, SeatsHeading, FMargin(0.f, 12.f, 0.f, 6.f));
		UHorizontalBox* SeatsOptions = New<UHorizontalBox>(Tree);
		for (int32 Seats = 1; Seats <= 2; ++Seats)
		{
			UTN_ShopButton* Option = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
			Option->Setup(TNLobbyMission::RallySeatsName(Seats).ToUpper(), IdlePill(), TNHUDArt::Cream, 21, FVector2D(260.f, 58.f),
				[this, Seats]() { PickSeats(Seats); });
			AddH(SeatsOptions, Option, FMargin(0.f, 0.f, 12.f, 0.f));
			SeatsButtons.Add(Option);
		}
		AddV(SeatsBox, SeatsOptions, FMargin(0.f, 0.f, 0.f, 8.f), HAlign_Left);
		UTextBlock* SeatsLine = Label(Tree, NSLOCTEXT("Tortunabo", "BriefingBuggySeatsLine",
			"•  Por parejas, la segunda de cada buggy va de artillera: maneja la torreta (en Karts, también los objetos) y su peso cambia cuánto gira el buggy."),
			TEXT("Regular"), 18, TNHUDArt::Ink, false);
		SeatsLine->SetAutoWrapText(true);
		AddV(SeatsBox, SeatsLine, FMargin(0.f, 0.f, 8.f, 0.f));
		SeatsRow = SeatsBox;
		AddV(Page, SeatsBox, FMargin(0.f));

		UVerticalBox* MapBox = New<UVerticalBox>(Tree);
		MapHeading = Label(Tree, FText::GetEmpty(), TEXT("Black"), 23, TNHUDArt::CoralDeep, false);
		AddV(MapBox, MapHeading, FMargin(0.f, 12.f, 0.f, 6.f));
		UHorizontalBox* MapOptions = nullptr;
		const TArray<FName>& Maps = TNLobbyMission::RallyMapOptions();
		for (int32 Index = 0; Index < Maps.Num(); ++Index)
		{
			if (Index % ModePillsPerRow == 0)
			{
				MapOptions = New<UHorizontalBox>(Tree);
				AddV(MapBox, MapOptions, FMargin(0.f, 0.f, 0.f, 8.f), HAlign_Left);
			}
			const FName Map = Maps[Index];
			UTN_ShopButton* Option = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
			Option->Setup(TNLobbyMission::RallyMapName(Map).ToUpper(), IdlePill(), TNHUDArt::Cream, 17, FVector2D(200.f, 58.f),
				[this, Map]() { PickRallyMap(Map); });
			AddH(MapOptions, Option, FMargin(0.f, 0.f, 12.f, 0.f));
			MapButtons.Add(Option);
		}
		UTextBlock* MapLine = Label(Tree, NSLOCTEXT("Tortunabo", "BriefingRallyCircuitLine",
			"•  Los circuitos del Rally tienen puertas en orden, vueltas y saltos de autor."), TEXT("Regular"), 18, TNHUDArt::Ink, false);
		MapLine->SetAutoWrapText(true);
		AddV(MapBox, MapLine, FMargin(0.f, 0.f, 8.f, 0.f));
		MapRow = MapBox;
		AddV(Page, MapBox, FMargin(0.f));

		UVerticalBox* ArenaBox = New<UVerticalBox>(Tree);
		ArenaHeading = Label(Tree, FText::GetEmpty(), TEXT("Black"), 23, TNHUDArt::CoralDeep, false);
		AddV(ArenaBox, ArenaHeading, FMargin(0.f, 12.f, 0.f, 6.f));
		UHorizontalBox* ArenaOptions = nullptr;
		const TArray<FName>& Arenas = TNLobbyMission::TctArenaOptions();
		for (int32 Index = 0; Index < Arenas.Num(); ++Index)
		{
			if (Index % ArenaPillsPerRow == 0)
			{
				ArenaOptions = New<UHorizontalBox>(Tree);
				AddV(ArenaBox, ArenaOptions, FMargin(0.f, 0.f, 0.f, 6.f), HAlign_Left);
			}
			const FName Arena = Arenas[Index];
			UTN_ShopButton* Option = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
			Option->Setup(TNLobbyMission::TctArenaName(Arena).ToUpper(), IdlePill(), TNHUDArt::Cream, 14, FVector2D(150.f, 46.f),
				[this, Arena]() { PickTctArena(Arena); });
			AddH(ArenaOptions, Option, FMargin(0.f, 0.f, 8.f, 0.f));
			ArenaButtons.Add(Option);
		}
		UTextBlock* ArenaLine = Label(Tree, NSLOCTEXT("Tortunabo", "BriefingTctArenaLine",
			"•  Cada arena se inunda a su manera: manda quien aguanta arriba y caer al agua es la muerte."), TEXT("Regular"), 18, TNHUDArt::Ink, false);
		ArenaLine->SetAutoWrapText(true);
		AddV(ArenaBox, ArenaLine, FMargin(0.f, 0.f, 8.f, 0.f));
		ArenaRow = ArenaBox;
		AddV(Page, ArenaBox, FMargin(0.f));
	}

	// La orden del día (lo que vale ahora) y quién manda.
	MissionOrders = Label(Tree, FText::GetEmpty(), TEXT("Black"), 21, TNHUDArt::Ink, false);
	AddV(Page, Framed(Tree, TNHUDArt::SandTagTexture(), TagBoxMargin, MissionOrders, FMargin(24.f, 6.f, 24.f, 10.f)),
		FMargin(0.f, 12.f, 0.f, 6.f), HAlign_Left);
	UTextBlock* Notice = Label(Tree, bHost
		? NSLOCTEXT("Tortunabo", "BriefingMissionHostNote",
			"Tú mandas: lo que elijas vale para toda la tropa en cuanto salgáis del lobby y todas lo ven al momento (también en mi pizarra). Teclado o mando: ↑/↓ elige la fila y ←/→ cambia; con el ratón, clic.")
		: NSLOCTEXT("Tortunabo", "BriefingMissionGuestNote",
			"El modo y la dificultad los elige el anfitrión. Si los cambia, lo verás aquí y en la pizarra de mi mesa al momento."),
		TEXT("Bold"), 18, bHost ? TNHUDArt::Ink : TNHUDArt::CoralDeep, false);
	Notice->SetAutoWrapText(true);
	AddV(Page, Notice, FMargin(0.f, 2.f, 8.f, 0.f));

	MissionRow = 0;
	ShownMode = GetMissionMode();
	ShownDifficulty = GetMissionDifficulty();
	RefreshMission(false);
}

void UTN_BriefingWidget::RefreshMission(bool bAnnounce)
{
	using namespace TNBriefingUI;
	const ETNProcGameMode Mode = GetMissionMode();
	const ETNProcDifficulty Difficulty = GetMissionDifficulty();
	const bool bChanged = Mode != ShownMode || Difficulty != ShownDifficulty;
	ShownMode = Mode;
	ShownDifficulty = Difficulty;
	const bool bHost = CanChooseMission();

	// La elegida en coral; los demás no pueden pulsar (se ven apagadas).
	const TArray<ETNProcGameMode> MenuModes = TNLobbyMission::GetMenuModes();
	for (int32 i = 0; i < ModeButtons.Num() && i < MenuModes.Num(); ++i)
	{
		if (UTN_ShopButton* Option = ModeButtons[i])
		{
			Option->SetArt(MenuModes[i] == Mode ? ChosenPill() : IdlePill());
			Option->SetDisabled(!bHost);
		}
	}
	for (int32 i = 0; i < DifficultyButtons.Num() && i < NumDifficulties; ++i)
	{
		if (UTN_ShopButton* Option = DifficultyButtons[i])
		{
			Option->SetArt(TNLobbyMission::Difficulties[i] == Difficulty ? ChosenPill() : IdlePill());
			Option->SetDisabled(!bHost);
		}
	}

	// La fila con el foco del teclado y el mando, marcada (solo el anfitrión tiene foco).
	const auto StyleHeading = [this, bHost](UTextBlock* Heading, int32 Row, const FText& HeadingText)
	{
		if (!Heading)
		{
			return;
		}
		const bool bFocus = bHost && MissionRow == Row;
		Heading->SetText(bFocus ? FText::Format(NSLOCTEXT("Tortunabo", "BriefingMissionFocus", "» {0}"), HeadingText) : HeadingText);
		Heading->SetColorAndOpacity(FSlateColor(bFocus || !bHost ? TNHUDArt::CoralDeep : TNHUDArt::Ink));
	};
	StyleHeading(ModeHeading, 0, NSLOCTEXT("Tortunabo", "BriefingMissionModeH", "Modo de la misión"));
	StyleHeading(DifficultyHeading, 1, NSLOCTEXT("Tortunabo", "BriefingMissionDiffH", "Dificultad"));
	StyleHeading(SeatsHeading, 2, NSLOCTEXT("Tortunabo", "BriefingBuggySeatsH", "Tortugas por buggy"));
	StyleHeading(MapHeading, 3, NSLOCTEXT("Tortunabo", "BriefingRallyCircuitH", "Circuito del Rally"));
	StyleHeading(ArenaHeading, 2, NSLOCTEXT("Tortunabo", "BriefingTctArenaH", "Arena"));
	if (SeatsRow)
	{
		SeatsRow->SetVisibility(HasSeatsRow() ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (MapRow)
	{
		MapRow->SetVisibility(HasMapRow() ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (ArenaRow)
	{
		ArenaRow->SetVisibility(HasArenaRow() ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	PaintChoicePills(MapButtons, TNLobbyMission::RallyMapOptions(), TNLobbyMission::GetHostRallyMap(this));
	PaintChoicePills(ArenaButtons, TNLobbyMission::TctArenaOptions(), TNLobbyMission::GetHostTctArena(this));
	for (int32 i = 0; i < SeatsButtons.Num(); ++i)
	{
		if (UTN_ShopButton* Option = SeatsButtons[i])
		{
			Option->SetArt(GetKartSeats() == i + 1 ? ChosenPill() : IdlePill());
		}
	}
	MissionRow = FMath::Min(MissionRow, MaxMissionRow());

	if (MissionOrders)
	{
		MissionOrders->SetText(FText::Format(NSLOCTEXT("Tortunabo", "BriefingMissionOrders", "Orden del día: {0} · {1}"),
			TNLobbyMission::MissionTitle(Mode, GetMissionRallyMap()).ToUpper(), TNLobbyMission::DifficultyName(Difficulty).ToUpper()));
	}
	if (bAnnounce && bChanged)
	{
		Say(FText::Format(NSLOCTEXT("Tortunabo", "BriefingSayMissionChanged", "¡Atención, tropa! Nueva orden del día: {0}, dificultad {1}."),
			TNLobbyMission::ModeName(Mode), TNLobbyMission::DifficultyName(Difficulty).ToLower()));
	}
}

void UTN_BriefingWidget::PickMode(ETNProcGameMode Mode)
{
	SetMissionRow(0);
	if (!CanChooseMission())
	{
		Say(NSLOCTEXT("Tortunabo", "BriefingSayHostOnly", "¡Alto ahí, recluta! La misión la decide el anfitrión."));
		return;
	}
	if (Mode != GetMissionMode() && !TNLobbyMission::SetMode(this, Mode))
	{
		return;
	}
	Say(TNBriefingUI::ModeOrderLine(Mode));
	RefreshMission(false);
}

void UTN_BriefingWidget::PickDifficulty(ETNProcDifficulty Difficulty)
{
	SetMissionRow(1);
	if (!CanChooseMission())
	{
		Say(NSLOCTEXT("Tortunabo", "BriefingSayHostOnly", "¡Alto ahí, recluta! La misión la decide el anfitrión."));
		return;
	}
	if (Difficulty != GetMissionDifficulty() && !TNLobbyMission::SetDifficulty(this, Difficulty))
	{
		return;
	}
	Say(TNBriefingUI::DifficultyOrderLine(Difficulty));
	RefreshMission(false);
}

int32 UTN_BriefingWidget::GetKartSeats() const
{
	const UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance());
	return GI ? FMath::Clamp(GI->SelectedKartSeats, 1, 2) : 2;
}

bool UTN_BriefingWidget::HasSeatsRow() const
{
	const ETNProcGameMode Mode = GetMissionMode();
	return CanChooseMission() && (Mode == ETNProcGameMode::Karts || Mode == ETNProcGameMode::Rally) && SeatsButtons.Num() > 0;
}

bool UTN_BriefingWidget::HasMapRow() const
{
	return CanChooseMission() && GetMissionMode() == ETNProcGameMode::Rally && MapButtons.Num() > 0;
}

bool UTN_BriefingWidget::HasArenaRow() const
{
	return CanChooseMission() && GetMissionMode() == ETNProcGameMode::FreeForAll && ArenaButtons.Num() > 0;
}

int32 UTN_BriefingWidget::MaxMissionRow() const
{
	return HasMapRow() ? 3 : (HasSeatsRow() || HasArenaRow() ? 2 : 1);
}

void UTN_BriefingWidget::PaintChoicePills(const TArray<TObjectPtr<UTN_ShopButton>>& Buttons, const TArray<FName>& Options, FName Chosen)
{
	for (int32 i = 0; i < Buttons.Num() && i < Options.Num(); ++i)
	{
		if (UTN_ShopButton* Option = Buttons[i])
		{
			Option->SetArt(Options[i] == Chosen ? TNBriefingUI::ChosenPill() : TNBriefingUI::IdlePill());
		}
	}
}

void UTN_BriefingWidget::PickTctArena(FName Arena)
{
	if (!CanChooseMission() || !TNLobbyMission::SetTctArena(this, Arena))
	{
		return;
	}
	MissionRow = 2;
	Say(FText::Format(NSLOCTEXT("Tortunabo", "BriefingSayTctArena", "¡A la arena {0}! Arriba manda quien aguanta; el agua no perdona."),
		TNLobbyMission::TctArenaName(Arena)));
	RefreshMission(false);
}

void UTN_BriefingWidget::PickRallyMap(FName Variant)
{
	if (!CanChooseMission() || !TNLobbyMission::SetRallyMap(this, Variant))
	{
		return;
	}
	MissionRow = 3;
	Say(FText::Format(NSLOCTEXT("Tortunabo", "BriefingSayRallyCircuit", "¡Circuito de {0}! Puertas en orden y nada de atajos por la arena."),
		TNLobbyMission::RallyMapName(Variant)));
	RefreshMission(false);
}

void UTN_BriefingWidget::PickSeats(int32 Seats)
{
	UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance());
	if (!CanChooseMission() || !GI)
	{
		return;
	}
	MissionRow = 2;
	TNLobbyMission::SetRallySeats(this, Seats);
	const bool bKarts = GetMissionMode() == ETNProcGameMode::Karts;
	if (GI->SelectedKartSeats == 1)
	{
		Say(bKarts ? NSLOCTEXT("Tortunabo", "BriefingSayKartsSolo", "¡Cada tortuga a su kart! Al volante y con la torreta, todo tuyo.")
			: NSLOCTEXT("Tortunabo", "BriefingSayRallySolo", "¡Cada tortuga a su buggy! Al volante y con la torreta, todo tuyo."));
	}
	else
	{
		Say(bKarts ? NSLOCTEXT("Tortunabo", "BriefingSayKartsPairs", "¡Por parejas! Una conduce y la otra dispara, usa los objetos y carga el peso en las curvas.")
			: NSLOCTEXT("Tortunabo", "BriefingSayRallyPairs", "¡Por parejas! Una conduce y la otra dispara la munición de las cajas y carga el peso en las curvas."));
	}
	RefreshMission(false);
}

void UTN_BriefingWidget::StepMission(int32 Direction)
{
	using namespace TNBriefingUI;
	if (MissionRow == 2 && HasArenaRow())
	{
		const TArray<FName>& Arenas = TNLobbyMission::TctArenaOptions();
		const int32 Current = Arenas.IndexOfByKey(TNLobbyMission::GetHostTctArena(this));
		const int32 NextArena = FMath::Clamp(Current + Direction, 0, Arenas.Num() - 1);
		if (Arenas.IsValidIndex(NextArena) && NextArena != Current)
		{
			PickTctArena(Arenas[NextArena]);
		}
		return;
	}
	if (MissionRow == 2)
	{
		PickSeats(FMath::Clamp(GetKartSeats() + Direction, 1, 2));
		return;
	}
	if (MissionRow == 3)
	{
		const TArray<FName>& Maps = TNLobbyMission::RallyMapOptions();
		const int32 Current = Maps.IndexOfByKey(TNLobbyMission::GetHostRallyMap(this));
		const int32 NextMap = FMath::Clamp(Current + Direction, 0, Maps.Num() - 1);
		if (Maps.IsValidIndex(NextMap) && NextMap != Current)
		{
			PickRallyMap(Maps[NextMap]);
		}
		return;
	}
	const bool bModeRow = MissionRow == 0;
	const TArray<ETNProcGameMode> MenuModes = TNLobbyMission::GetMenuModes();
	const int32 NumOptions = bModeRow ? MenuModes.Num() : NumDifficulties;
	int32 Index = INDEX_NONE;
	for (int32 i = 0; i < NumOptions; ++i)
	{
		const bool bCurrent = bModeRow ? MenuModes[i] == GetMissionMode() : TNLobbyMission::Difficulties[i] == GetMissionDifficulty();
		if (bCurrent) { Index = i; }
	}
	// Sin la actual entre las opciones (Clásico, del selector del lobby viejo): la primera o la última.
	const int32 Next = Index == INDEX_NONE ? (Direction > 0 ? 0 : NumOptions - 1) : FMath::Clamp(Index + Direction, 0, NumOptions - 1);
	if (Next == Index)
	{
		return;
	}
	if (bModeRow)
	{
		PickMode(MenuModes[Next]);
	}
	else
	{
		PickDifficulty(TNLobbyMission::Difficulties[Next]);
	}
}

void UTN_BriefingWidget::SetMissionRow(int32 Row)
{
	MissionRow = FMath::Clamp(Row, 0, MaxMissionRow());
	RefreshMission(false);
}

bool UTN_BriefingWidget::CanChooseMission() const
{
	return TNLobbyMission::CanLocalPlayerChoose(this);
}

FName UTN_BriefingWidget::GetMissionRallyMap() const
{
	if (CanChooseMission())
	{
		return TNLobbyMission::GetHostMissionMap(this);
	}
	const ATN_GeneralBriefing* Speaker = General.Get();
	return Speaker ? Speaker->GetMissionRallyVariant() : FName(NAME_None);
}

ETNProcGameMode UTN_BriefingWidget::GetMissionMode() const
{
	if (CanChooseMission())
	{
		return TNLobbyMission::GetHostMode(this);
	}
	const ATN_GeneralBriefing* Speaker = General.Get();
	return Speaker ? Speaker->GetMissionMode() : ETNProcGameMode::Coop;
}

ETNProcDifficulty UTN_BriefingWidget::GetMissionDifficulty() const
{
	if (CanChooseMission())
	{
		return TNLobbyMission::GetHostDifficulty(this);
	}
	const ATN_GeneralBriefing* Speaker = General.Get();
	return Speaker ? Speaker->GetMissionDifficulty() : ETNProcDifficulty::Normal;
}

void UTN_BriefingWidget::RefreshKeysHint()
{
	if (!KeysHint)
	{
		return;
	}
	KeysHint->SetText(Tab == TNBriefingUI::TabMission && CanChooseMission()
		? NSLOCTEXT("Tortunabo", "BriefingKeysMission", "Q/E: pestaña · ↑/↓: fila · ←/→: cambiar · Esc: salir")
		: NSLOCTEXT("Tortunabo", "BriefingKeys", "Q/E o flechas: pestaña · ↑/↓: desplazar · Esc: salir"));
}

void UTN_BriefingWidget::Say(const FText& Line)
{
	FullLine = Line.ToString();
	Reveal = 0.f;
	if (DialogText) { DialogText->SetText(FText::GetEmpty()); }
}

void UTN_BriefingWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// El general habla letra a letra.
	if (DialogText && Reveal < FullLine.Len())
	{
		Reveal = FMath::Min<float>(FullLine.Len(), Reveal + InDeltaTime * 70.f);
		// Letra a letra: es un trozo del texto ya traducido, no un texto nuevo.
		DialogText->SetText(TNLocText::Literal(FullLine.Left(FMath::CeilToInt(Reveal))));
	}
	// «Misión»: si el anfitrión la cambia (a los demás les llega replicada en el general), se repinta y el general avisa.
	if (Tab == TNBriefingUI::TabMission && ModeButtons.Num() > 0
		&& (GetMissionMode() != ShownMode || GetMissionDifficulty() != ShownDifficulty))
	{
		RefreshMission(true);
	}
}

void UTN_BriefingWidget::Close()
{
	if (AMP_GamePlayerController* PC = Cast<AMP_GamePlayerController>(GetOwningPlayer())) { PC->CloseShopUI(); }
	else { RemoveFromParent(); }
}

FReply UTN_BriefingWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	using TNBriefingUI::IsKey;
	const FKey Key = InKeyEvent.GetKey();
	// «Misión» del anfitrión: arriba/abajo eligen la fila (modo o dificultad) e izquierda/derecha cambian la opción. Las
	// pestañas siguen con Q/E, Tab o LB/RB.
	if (Tab == TNBriefingUI::TabMission && CanChooseMission())
	{
		if (IsKey(Key, { EKeys::Up, EKeys::W, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_LeftStick_Up })) { SetMissionRow(MissionRow - 1); return FReply::Handled(); }
		if (IsKey(Key, { EKeys::Down, EKeys::S, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_LeftStick_Down })) { SetMissionRow(MissionRow + 1); return FReply::Handled(); }
		if (IsKey(Key, { EKeys::Left, EKeys::A, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_LeftStick_Left })) { StepMission(-1); return FReply::Handled(); }
		if (IsKey(Key, { EKeys::Right, EKeys::D, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_LeftStick_Right })) { StepMission(1); return FReply::Handled(); }
	}
	if (IsKey(Key, { EKeys::Q, EKeys::Left, EKeys::A, EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_DPad_Left })) { ShowTab(Tab - 1); return FReply::Handled(); }
	if (IsKey(Key, { EKeys::E, EKeys::Tab, EKeys::Right, EKeys::D, EKeys::Gamepad_RightShoulder, EKeys::Gamepad_DPad_Right })) { ShowTab(Tab + 1); return FReply::Handled(); }
	if (IsKey(Key, { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five }))
	{
		ShowTab(Key == EKeys::One ? 0 : Key == EKeys::Two ? 1 : Key == EKeys::Three ? 2 : Key == EKeys::Four ? 3 : 4);
		return FReply::Handled();
	}
	if (Scroll && IsKey(Key, { EKeys::Up, EKeys::W, EKeys::Gamepad_DPad_Up }))
	{
		Scroll->SetScrollOffset(FMath::Max(0.f, Scroll->GetScrollOffset() - 90.f));
		return FReply::Handled();
	}
	if (Scroll && IsKey(Key, { EKeys::Down, EKeys::S, EKeys::Gamepad_DPad_Down }))
	{
		Scroll->SetScrollOffset(FMath::Min(Scroll->GetScrollOffsetOfEnd(), Scroll->GetScrollOffset() + 90.f));
		return FReply::Handled();
	}
	if (IsKey(Key, { EKeys::Escape, EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_Special_Right }))
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
