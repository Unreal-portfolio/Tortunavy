#include "UI/Pause/TN_PauseMenuWidget.h"
#include "TN_PauseArt.h"
#include "../HUD/TN_HUDArt.h"
#include "../HUD/TN_HUDFaces.h"
#include "../HUD/TN_HUDStyle.h"
#include "../Menu/TN_RoomArt.h"
#include "../Shop/TN_ShopArt.h"
#include "Audio/TN_ScoreShellSynthComponent.h"
#include "Game/TN_ProcMapGameState.h"
#include "Game/TN_RunGameMode.h"
#include "Game/TN_SurvivalGameMode.h"
#include "Game/TN_TerrainViewGameMode.h"
#include "Lobby/TN_HQGameMode.h"
#include "Lobby/TN_TutorialPlayerComponent.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Multiplayer/TN_LocalPlaySubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Multiplayer/TN_RoomNames.h"
#include "Player/TortugaCharacter.h"
#include "UI/Credits/TN_CreditsWidget.h"
#include "Kart/TN_KartGameState.h"
#include "Lobby/TN_LobbyMission.h"
#include "Rally/TN_RallyGameMode.h"
#include "World/TN_TctArena.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "Settings/TN_LanguageSettings.h"
#include "UI/Pause/TN_PlayerRowRules.h"
#include "Voice/ProximityVoiceComponent.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WidgetSwitcherSlot.h"
#include "EnhancedActionKeyMapping.h"
#include "Engine/Console.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Kismet/KismetSystemLibrary.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "Styling/SlateTypes.h"
#include "VR/TN_VRMode.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto
// del bloque.
namespace TNPauseUI
{
	/** Medidas en unidades de la interfaz a 1080 p (el motor escala: a 720 p cabe igual y a 4K se dobla). */
	constexpr float HeaderWidth = 1320.f;
	constexpr float CardWidth = 1240.f;
	constexpr float SettingsListHeight = 500.f;
	constexpr float ControlsListHeight = 520.f;
	constexpr float RoomListHeight = 540.f;
	/** Columnas de una entrada (salas, jugadores): la primera y la segunda, y el icono del final. */
	constexpr float EntryValueWidth = 360.f;
	constexpr float EntryValue2Width = 190.f;
	constexpr float EntryIconSize = 34.f;
	constexpr float RowHeight = 52.f;
	constexpr float BigWidth = 500.f;
	constexpr float BigHeight = 70.f;
	constexpr float DialogWidth = 270.f;
	constexpr float DialogHeight = 64.f;
	constexpr float BarWidth = 300.f;
	constexpr float MeterWidth = 250.f;
	constexpr float KeyCapWidth = 330.f;
	constexpr float PadCapWidth = 290.f;
	constexpr float KeyCapHeight = 38.f;

	/** Lienzo de diseño: todo el menú cabe aquí y se encoge entero si la pantalla (en unidades de interfaz) es menor. */
	constexpr float DesignWidth = 1920.f;
	constexpr float DesignHeight = 1080.f;

	/** Segundos que espera «Pulsa una tecla...» antes de rendirse. */
	constexpr float KeyCaptureTimeout = 8.f;

	/** Márgenes de caja de las texturas de TNHUDArt y TNShopArt. */
	const FMargin CardMargin(0.16f, 0.2f, 0.16f, 0.34f);
	const FMargin RibbonMargin(0.14f, 0.f, 0.14f, 0.f);
	const FMargin TagMargin(0.2f, 0.f, 0.2f, 0.f);
	const FMargin PillMargin(0.4f, 0.f, 0.4f, 0.f);

	/** Colores de las filas de ajustes (azul marino translúcido; la enfocada, más clara y con filo turquesa). */
	const FLinearColor RowFill(0.012f, 0.045f, 0.08f, 0.55f);
	const FLinearColor RowEdge(1.f, 1.f, 1.f, 0.08f);

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

	FSlateBrush BoxBrush(UTexture2D* Tex, const FMargin& Margin, const FLinearColor& Tint = FLinearColor::White)
	{
		FSlateBrush Brush;
		Brush.SetResourceObject(Tex);
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.Margin = Margin;
		Brush.TintColor = FSlateColor(Tint);
		if (Tex) { Brush.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY()); }
		return Brush;
	}

	UImage* Picture(UWidgetTree* Tree, UTexture2D* Tex, const FVector2D& Size)
	{
		UImage* Out = Make<UImage>(Tree);
		FSlateBrush Brush;
		Brush.SetResourceObject(Tex);
		Brush.ImageSize = Size;
		Out->SetBrush(Brush);
		return Out;
	}

	/** Cambia la textura de una imagen conservando su tamaño. */
	void SetPicture(UImage* Img, UTexture2D* Tex)
	{
		if (!Img) { return; }
		FSlateBrush Brush = Img->GetBrush();
		if (Brush.GetResourceObject() == Tex) { return; }
		Brush.SetResourceObject(Tex);
		Img->SetBrush(Brush);
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

	/**
	 * Mete Content en una caja de 1920 × 1080 que se encoge entera (sin deformarse) si no cabe: con la interfaz grande o
	 * una ventana pequeña el menú sigue viéndose completo; con la interfaz pequeña, se queda pequeño y centrado.
	 */
	UScaleBox* Fit(UWidgetTree* Tree, UWidget* Content)
	{
		UScaleBox* Box = Make<UScaleBox>(Tree);
		Box->SetStretch(EStretch::ScaleToFit);
		Box->SetStretchDirection(EStretchDirection::DownOnly);
		Box->SetContent(Sized(Tree, Content, DesignWidth, DesignHeight));
		return Box;
	}

	/** Coloca en un lienzo con ancla y alineación en el mismo punto y a su tamaño. */
	UCanvasPanelSlot* Pin(UCanvasPanel* Canvas, UWidget* W, const FVector2D& Anchor, const FVector2D& Offset)
	{
		UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(W);
		CanvasSlot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
		CanvasSlot->SetAlignment(Anchor);
		CanvasSlot->SetPosition(Offset);
		CanvasSlot->SetAutoSize(true);
		return CanvasSlot;
	}

	/** Ocupa todo el lienzo. */
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

	FText Percent(float Value)
	{
		return FText::Format(NSLOCTEXT("TNPause", "PercentFmt", "{0} %"), FText::AsNumber(FMath::RoundToInt(Value * 100.f)));
	}

	/** Nivel RMS a la escala del medidor: de -60 dB (vacío) a 0 dB (lleno). */
	float MeterFromRms(float Rms)
	{
		const float Db = 20.f * FMath::LogX(10.f, FMath::Max(Rms, 1e-5f));
		return FMath::Clamp((Db + 60.f) / 60.f, 0.f, 1.f);
	}

	/** Nombre de una tecla como lo lee un jugador en España. */
	FText KeyName(const FKey& Key)
	{
		return UTN_GameSettingsSubsystem::KeyDisplayName(Key);
	}

	/** Lo que enseña una fila de tecla en un aparato: la tecla o, si ahí no se cambia, lo fijo (el stick). */
	FText BindingText(const FTNKeyBinding& Binding, int32 Device)
	{
		return KeyName(Binding.bEditable[Device] ? Binding.Keys[Device] : Binding.FixedKeys[Device]);
	}

	/** Calidad gráfica por partes: nombre, ayuda y sus funciones de UGameUserSettings. */
	struct FQualityPart
	{
		FText Label;
		FText Description;
		int32 (UGameUserSettings::*Get)() const;
		void (UGameUserSettings::*Set)(int32);
	};

	const TArray<FQualityPart>& QualityParts()
	{
		static const TArray<FQualityPart> Parts = {
			{ NSLOCTEXT("TNPause", "QShadows", "Sombras"), NSLOCTEXT("TNPause", "QShadowsDesc", "Calidad y alcance de las sombras. Es de lo que más pesa."),
				&UGameUserSettings::GetShadowQuality, &UGameUserSettings::SetShadowQuality },
			{ NSLOCTEXT("TNPause", "QEffects", "Efectos"), NSLOCTEXT("TNPause", "QEffectsDesc", "Partículas: polvo, espuma, chispas, confeti..."),
				&UGameUserSettings::GetVisualEffectQuality, &UGameUserSettings::SetVisualEffectQuality },
			{ NSLOCTEXT("TNPause", "QFoliage", "Vegetación"), NSLOCTEXT("TNPause", "QFoliageDesc", "Cuánta hierba y plantas se pintan a la vez."),
				&UGameUserSettings::GetFoliageQuality, &UGameUserSettings::SetFoliageQuality },
			{ NSLOCTEXT("TNPause", "QView", "Distancia de visión"), NSLOCTEXT("TNPause", "QViewDesc", "Hasta dónde se ven los detalles del mapa."),
				&UGameUserSettings::GetViewDistanceQuality, &UGameUserSettings::SetViewDistanceQuality },
			{ NSLOCTEXT("TNPause", "QAA", "Antialiasing"), NSLOCTEXT("TNPause", "QAADesc", "Suaviza los bordes de sierra de los objetos."),
				&UGameUserSettings::GetAntiAliasingQuality, &UGameUserSettings::SetAntiAliasingQuality },
			{ NSLOCTEXT("TNPause", "QTextures", "Texturas"), NSLOCTEXT("TNPause", "QTexturesDesc", "Nitidez de las texturas (usa memoria de vídeo)."),
				&UGameUserSettings::GetTextureQuality, &UGameUserSettings::SetTextureQuality },
			{ NSLOCTEXT("TNPause", "QPost", "Postprocesado"), NSLOCTEXT("TNPause", "QPostDesc", "Brillos, desenfoque de movimiento y otros acabados de la imagen."),
				&UGameUserSettings::GetPostProcessingQuality, &UGameUserSettings::SetPostProcessingQuality },
			{ NSLOCTEXT("TNPause", "QGI", "Iluminación global"), NSLOCTEXT("TNPause", "QGIDesc", "Luz que rebota en el entorno. En Baja, el mapa se ve más plano."),
				&UGameUserSettings::GetGlobalIlluminationQuality, &UGameUserSettings::SetGlobalIlluminationQuality },
			{ NSLOCTEXT("TNPause", "QReflections", "Reflejos"), NSLOCTEXT("TNPause", "QReflectionsDesc", "Reflejos del agua y de las superficies brillantes."),
				&UGameUserSettings::GetReflectionQuality, &UGameUserSettings::SetReflectionQuality },
		};
		return Parts;
	}

	const TArray<FText>& QualityNames()
	{
		static const TArray<FText> Names = { NSLOCTEXT("TNPause", "QLow", "Baja"), NSLOCTEXT("TNPause", "QMedium", "Media"),
			NSLOCTEXT("TNPause", "QHigh", "Alta"), NSLOCTEXT("TNPause", "QEpic", "Épica") };
		return Names;
	}

	/** Texto de una calidad fuera de la lista (Cine, 4) o mezclada (Personalizada, -1). */
	FText QualityOverride(int32 Level)
	{
		if (Level < 0) { return NSLOCTEXT("TNPause", "QCustom", "Personalizada"); }
		if (Level > 3) { return NSLOCTEXT("TNPause", "QCine", "Cine"); }
		return FText::GetEmpty();
	}

	/**
	 * Sesiones del subsistema en línea por defecto (Steam en el juego; el NULL si Steam no arrancó, como en el editor).
	 * Sin pedir Steam por su nombre: se consulta cada medio segundo y no debe reintentar cargarlo.
	 */
	IOnlineSessionPtr SessionInterface()
	{
		IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
		return OnlineSub ? OnlineSub->GetSessionInterface() : nullptr;
	}

	/**
	 * La segunda línea de una tortuga en la lista de jugadores: «Tú», «Tú · anfitrión», «Anfitrión» o su ping. Un invitado ve
	 * también el suyo («Tú · 42 ms»); antes solo veía el de los demás y su propia fila decía «Tú» (#256).
	 */
	FText PlayerSub(const APlayerState* PS, bool bMe, bool bRowHost, bool bLocalIsClient)
	{
		const TNPlayerRowRules::ESub Sub = TNPlayerRowRules::Decide(bMe, bRowHost, bLocalIsClient);
		switch (Sub)
		{
		case TNPlayerRowRules::ESub::YouHost: return NSLOCTEXT("TNPause", "YouHost", "Tú · anfitrión");
		case TNPlayerRowRules::ESub::You: return NSLOCTEXT("TNPause", "You", "Tú");
		case TNPlayerRowRules::ESub::Host: return NSLOCTEXT("TNPause", "Host", "Anfitrión");
		default: break;
		}
		const FText Ping = FText::Format(NSLOCTEXT("TNPause", "Ping", "{0} ms"), FText::AsNumber(PS ? FMath::RoundToInt(PS->GetPingInMilliseconds()) : 0));
		return Sub == TNPlayerRowRules::ESub::YouPing ? FText::Join(INVTEXT(" · "), NSLOCTEXT("TNPause", "You", "Tú"), Ping) : Ping;
	}

	/** Límites de fotogramas (0 = sin límite). */
	const TArray<float>& FrameLimits()
	{
		static const TArray<float> Limits = { 30.f, 60.f, 90.f, 120.f, 144.f, 165.f, 240.f, 0.f };
		return Limits;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Fila
// ─────────────────────────────────────────────────────────────────────────────

void UTN_PauseRow::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// Se enfoca para que el teclado y el mando la recorran (tiene que ser antes de montar su widget de Slate).
	SetIsFocusable(true);
	// Y se le puede dar con el puntero: la navegación de Slate (flechas, cruceta, stick) busca la fila siguiente en la rejilla
	// de lo que se puede tocar de la ventana, y un UUserWidget nace SelfHitTestInvisible, fuera de esa rejilla. Sin esto, el
	// ratón la enfocaba (sus hijos sí se tocan) pero arriba y abajo no llevaban a ninguna parte (#311).
	SetVisibility(ESlateVisibility::Visible);
	SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	// La raíz existe desde ya: las filas se meten en listas que ya están en pantalla antes de montarse (Setup...), y el
	// widget de Slate de la fila se crea al entrar en la lista. Si la raíz llegara después, la fila quedaría vacía:
	// se podría enfocar (la ayuda de abajo cambia), pero no se vería ni se podría pulsar.
	if (WidgetTree && !Sizer)
	{
		Sizer = TNPauseUI::Make<USizeBox>(WidgetTree);
		WidgetTree->RootWidget = Sizer;
	}
}

void UTN_PauseRow::SetupButton(ETNPauseRowStyle InStyle, const FText& InLabel, TFunction<void()> InOnPressed, UTexture2D* InIcon, const FText& InActionText)
{
	Kind = ETNPauseRowKind::Button;
	Style = InStyle;
	OnPressed = MoveTemp(InOnPressed);
	Build();
	SetLabel(InLabel);
	if (IconImage)
	{
		TNPauseUI::SetPicture(IconImage, InIcon);
		IconImage->SetVisibility(InIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (ValueText)
	{
		ValueText->SetText(InActionText.IsEmpty() ? NSLOCTEXT("TNPause", "PressAction", "Pulsar") : InActionText);
	}
	RefreshLook();
}

void UTN_PauseRow::SetupSlider(const FText& InLabel, float InMin, float InMax, float InStep, float InValue, TFunction<FText(float)> InFormat,
	TFunction<void(float)> InOnChanged)
{
	Kind = ETNPauseRowKind::Slider;
	Style = ETNPauseRowStyle::List;
	Min = InMin;
	Max = FMath::Max(InMax, InMin + KINDA_SMALL_NUMBER);
	Step = FMath::Max(InStep, 0.f);
	Value = FMath::Clamp(InValue, Min, Max);
	Format = MoveTemp(InFormat);
	OnValueChanged = MoveTemp(InOnChanged);
	Build();
	SetLabel(InLabel);
	RefreshValue();
	RefreshLook();
}

void UTN_PauseRow::SetupChoice(const FText& InLabel, const TArray<FText>& InOptions, int32 InIndex, TFunction<void(int32)> InOnChanged)
{
	Kind = ETNPauseRowKind::Choice;
	Style = ETNPauseRowStyle::List;
	Options = InOptions;
	Index = InIndex;
	OnChoiceChanged = MoveTemp(InOnChanged);
	Build();
	SetLabel(InLabel);
	RefreshValue();
	RefreshLook();
}

void UTN_PauseRow::SetupInfo(const FText& InLabel, const FText& InValue, const FText& InValue2)
{
	Kind = ETNPauseRowKind::Info;
	Style = ETNPauseRowStyle::List;
	Build();
	SetLabel(InLabel);
	if (ValueText) { ValueText->SetText(InValue); }
	if (Value2Text) { Value2Text->SetText(InValue2); }
	RefreshLook();
}

void UTN_PauseRow::SetupMeter(const FText& InLabel, TFunction<void(float&, float&, FText&)> InSampler)
{
	Kind = ETNPauseRowKind::Meter;
	Style = ETNPauseRowStyle::List;
	Sampler = MoveTemp(InSampler);
	Build();
	SetLabel(InLabel);
	RefreshLook();
}

void UTN_PauseRow::SetupKeyBind(const FText& InLabel, const FString& InBindingId, TFunction<void()> InOnChange, TFunction<void()> InOnReset)
{
	Kind = ETNPauseRowKind::KeyBind;
	Style = ETNPauseRowStyle::List;
	BindingId = InBindingId;
	OnPressed = MoveTemp(InOnChange);
	OnReset = MoveTemp(InOnReset);
	Build();
	SetLabel(InLabel);
	RefreshLook();
}

void UTN_PauseRow::SetupEntry(const FText& InLabel, const FText& InValue, const FText& InValue2, TFunction<void()> InOnPressed, UTexture2D* InIcon)
{
	Kind = ETNPauseRowKind::Entry;
	Style = ETNPauseRowStyle::List;
	OnPressed = MoveTemp(InOnPressed);
	Build();
	SetLabel(InLabel);
	if (ValueText) { ValueText->SetText(InValue); }
	if (Value2Text) { Value2Text->SetText(InValue2); }
	if (IconImage)
	{
		TNPauseUI::SetPicture(IconImage, InIcon);
		IconImage->SetVisibility(InIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	RefreshLook();
}

void UTN_PauseRow::SetValueColors(const FLinearColor& InValue, const FLinearColor& InValue2)
{
	if (ValueText) { ValueText->SetColorAndOpacity(FSlateColor(InValue)); }
	if (Value2Text) { Value2Text->SetColorAndOpacity(FSlateColor(InValue2)); }
}

void UTN_PauseRow::SetWidthOverride(float InWidth)
{
	if (Sizer && InWidth > 0.f) { Sizer->SetWidthOverride(InWidth); }
}

void UTN_PauseRow::SetKeyTexts(const FText& InKeyboard, const FText& InPad, bool bKeyboardEditable, bool bPadEditable)
{
	KeyTexts[0] = InKeyboard;
	KeyTexts[1] = InPad;
	bKeyEditable[0] = bKeyboardEditable;
	bKeyEditable[1] = bPadEditable;
	SetCapturing(bCapturing);
}

void UTN_PauseRow::SetCapturing(bool bInCapturing)
{
	bCapturing = bInCapturing;
	CaptureClock = 0.f;
	if (ValueText)
	{
		ValueText->SetText(bCapturing && bKeyEditable[0] ? NSLOCTEXT("TNPause", "PressKey", "Pulsa una tecla...") : KeyTexts[0]);
	}
	if (Value2Text)
	{
		Value2Text->SetText(bCapturing && bKeyEditable[1]
			? (bKeyEditable[0] ? NSLOCTEXT("TNPause", "OrButton", "...o un botón") : NSLOCTEXT("TNPause", "PressButton", "Pulsa un botón..."))
			: KeyTexts[1]);
	}
	if (!bCapturing)
	{
		if (KeyCap) { KeyCap->SetRenderOpacity(1.f); }
		if (PadCap) { PadCap->SetRenderOpacity(1.f); }
	}
	RefreshLook();
}

void UTN_PauseRow::Build()
{
	if (!WidgetTree || Frame)
	{
		return;
	}
	UWidgetTree* Tree = WidgetTree;
	Frame = TNPauseUI::Make<UBorder>(Tree);
	Frame->SetVerticalAlignment(VAlign_Center);
	if (!Sizer)
	{
		Sizer = TNPauseUI::Make<USizeBox>(Tree);
		Tree->RootWidget = Sizer;
	}
	switch (Style)
	{
	case ETNPauseRowStyle::List:
		Frame->SetHorizontalAlignment(HAlign_Fill);
		Frame->SetPadding(FMargin(20.f, 4.f, 16.f, 4.f));
		Frame->SetContent(BuildListContent());
		Sizer->SetHeightOverride(TNPauseUI::RowHeight);
		break;
	case ETNPauseRowStyle::Big:
		Frame->SetHorizontalAlignment(HAlign_Left);
		Frame->SetPadding(FMargin(34.f, 0.f, 24.f, 0.f));
		Frame->SetContent(BuildBigContent());
		Sizer->SetWidthOverride(TNPauseUI::BigWidth);
		Sizer->SetHeightOverride(TNPauseUI::BigHeight);
		break;
	case ETNPauseRowStyle::Tab:
		Frame->SetHorizontalAlignment(HAlign_Center);
		Frame->SetPadding(FMargin(26.f, 6.f, 26.f, 10.f));
		Frame->SetContent(BuildBigContent());
		Sizer->SetMinDesiredWidth(172.f);
		Sizer->SetHeightOverride(52.f);
		break;
	default:
		Frame->SetHorizontalAlignment(HAlign_Center);
		Frame->SetPadding(FMargin(20.f, 0.f, 20.f, 0.f));
		Frame->SetContent(BuildBigContent());
		Sizer->SetWidthOverride(TNPauseUI::DialogWidth);
		Sizer->SetHeightOverride(TNPauseUI::DialogHeight);
		break;
	}
	Sizer->SetContent(Frame);
}

UWidget* UTN_PauseRow::BuildListContent()
{
	UWidgetTree* Tree = WidgetTree;
	UHorizontalBox* Line = TNPauseUI::Make<UHorizontalBox>(Tree);
	LabelText = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 20, TNHUDStyle::Text);
	TNPauseUI::AddH(Line, LabelText, FMargin(0.f, 0.f, 12.f, 0.f), true);

	switch (Kind)
	{
	case ETNPauseRowKind::Slider:
	case ETNPauseRowKind::Meter:
	{
		const float Width = Kind == ETNPauseRowKind::Meter ? TNPauseUI::MeterWidth : TNPauseUI::BarWidth;
		UOverlay* Stack = TNPauseUI::Make<UOverlay>(Tree);
		Bar = TNPauseUI::Make<UProgressBar>(Tree);
		Bar->SetWidgetStyle(TNHUDStyle::Bar(7.f));
		Bar->SetFillColorAndOpacity(TNHUDArt::SeaLight);
		Bar->SetPercent(0.f);
		TNPauseUI::AddO(Stack, Bar, HAlign_Fill, VAlign_Fill);
		// Marca del umbral (solo el medidor): una rayita dorada encima de la barra.
		MarkImage = TNPauseUI::Make<UImage>(Tree);
		MarkImage->SetColorAndOpacity(TNHUDArt::Gold);
		MarkImage->SetDesiredSizeOverride(FVector2D(4.f, 14.f));
		MarkImage->SetVisibility(ESlateVisibility::Collapsed);
		TNPauseUI::AddO(Stack, MarkImage, HAlign_Left, VAlign_Fill);
		BarBox = TNPauseUI::Sized(Tree, Stack, Width, 14.f);
		TNPauseUI::AddH(Line, BarBox, FMargin(0.f, 0.f, 14.f, 0.f));
		ValueText = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 19, TNHUDArt::SandC);
		ValueText->SetJustification(ETextJustify::Right);
		TNPauseUI::AddH(Line, TNPauseUI::Sized(Tree, ValueText, Kind == ETNPauseRowKind::Meter ? 210.f : 120.f, 0.f));
		break;
	}
	case ETNPauseRowKind::Choice:
	{
		UHorizontalBox* Picker = TNPauseUI::Make<UHorizontalBox>(Tree);
		LeftArrow = TNPauseUI::Picture(Tree, TNShopArt::Arrow(false), FVector2D(30.f, 30.f));
		TNPauseUI::AddH(Picker, LeftArrow);
		ValueText = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 19, TNHUDArt::SandC);
		ValueText->SetJustification(ETextJustify::Center);
		TNPauseUI::AddH(Picker, ValueText, FMargin(8.f, 0.f), true);
		RightArrow = TNPauseUI::Picture(Tree, TNShopArt::Arrow(true), FVector2D(30.f, 30.f));
		TNPauseUI::AddH(Picker, RightArrow);
		TNPauseUI::AddH(Line, TNPauseUI::Sized(Tree, Picker, 450.f, 0.f));
		break;
	}
	case ETNPauseRowKind::KeyBind:
	{
		// Dos «teclas» (teclado y ratón, mando) alineadas con las columnas de la cabecera de la página de controles.
		ValueText = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 18, TNHUDArt::Cream);
		ValueText->SetJustification(ETextJustify::Center);
		KeyCap = TNPauseUI::Make<UBorder>(Tree);
		KeyCap->SetPadding(FMargin(10.f, 0.f));
		KeyCap->SetHorizontalAlignment(HAlign_Center);
		KeyCap->SetVerticalAlignment(VAlign_Center);
		KeyCap->SetContent(ValueText);
		TNPauseUI::AddH(Line, TNPauseUI::Sized(Tree, KeyCap, TNPauseUI::KeyCapWidth, TNPauseUI::KeyCapHeight), FMargin(0.f, 0.f, 12.f, 0.f));
		Value2Text = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 18, TNHUDArt::Cream);
		Value2Text->SetJustification(ETextJustify::Center);
		PadCap = TNPauseUI::Make<UBorder>(Tree);
		PadCap->SetPadding(FMargin(10.f, 0.f));
		PadCap->SetHorizontalAlignment(HAlign_Center);
		PadCap->SetVerticalAlignment(VAlign_Center);
		PadCap->SetContent(Value2Text);
		TNPauseUI::AddH(Line, TNPauseUI::Sized(Tree, PadCap, TNPauseUI::PadCapWidth, TNPauseUI::KeyCapHeight));
		break;
	}
	case ETNPauseRowKind::Entry:
	{
		// Nombre a la izquierda, dos columnas y, si hay, el icono del final (el «⋮» o el candado).
		ValueText = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 18, TNHUDArt::SandC);
		TNPauseUI::AddH(Line, TNPauseUI::Sized(Tree, ValueText, TNPauseUI::EntryValueWidth, 0.f), FMargin(0.f, 0.f, 12.f, 0.f));
		Value2Text = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 18, TNHUDArt::SeaLight);
		Value2Text->SetJustification(ETextJustify::Right);
		TNPauseUI::AddH(Line, TNPauseUI::Sized(Tree, Value2Text, TNPauseUI::EntryValue2Width, 0.f), FMargin(0.f, 0.f, 10.f, 0.f));
		IconImage = TNPauseUI::Picture(Tree, nullptr, FVector2D(TNPauseUI::EntryIconSize, TNPauseUI::EntryIconSize));
		IconImage->SetVisibility(ESlateVisibility::Collapsed);
		TNPauseUI::AddH(Line, TNPauseUI::Sized(Tree, IconImage, TNPauseUI::EntryIconSize, TNPauseUI::EntryIconSize));
		break;
	}
	case ETNPauseRowKind::Info:
	{
		ValueText = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 18, TNHUDArt::SandC);
		ValueText->SetAutoWrapText(true);
		TNPauseUI::AddH(Line, TNPauseUI::Sized(Tree, ValueText, 330.f, 0.f), FMargin(0.f, 0.f, 12.f, 0.f));
		Value2Text = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 18, TNHUDArt::SeaLight);
		Value2Text->SetAutoWrapText(true);
		TNPauseUI::AddH(Line, TNPauseUI::Sized(Tree, Value2Text, 290.f, 0.f));
		break;
	}
	default:
	{
		// Botón en la lista de ajustes: el nombre a la izquierda y lo que hace, en arena, a la derecha.
		ValueText = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 19, TNHUDArt::SandC);
		ValueText->SetJustification(ETextJustify::Right);
		TNPauseUI::AddH(Line, TNPauseUI::Sized(Tree, ValueText, 300.f, 0.f));
		break;
	}
	}
	return Line;
}

UWidget* UTN_PauseRow::BuildBigContent()
{
	UWidgetTree* Tree = WidgetTree;
	UHorizontalBox* Line = TNPauseUI::Make<UHorizontalBox>(Tree);
	const bool bBig = Style == ETNPauseRowStyle::Big;
	IconImage = TNPauseUI::Picture(Tree, nullptr, FVector2D(42.f, 42.f));
	IconImage->SetVisibility(ESlateVisibility::Collapsed);
	TNPauseUI::AddH(Line, IconImage, FMargin(0.f, 0.f, 16.f, 0.f));
	if (Style == ETNPauseRowStyle::Tab)
	{
		LabelText = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 19, TNHUDArt::Cream);
	}
	else
	{
		LabelText = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), bBig ? 25 : 23, TNHUDArt::Ink, false);
	}
	TNPauseUI::AddH(Line, LabelText);
	return Line;
}

void UTN_PauseRow::SetLabel(const FText& InLabel)
{
	if (LabelText) { LabelText->SetText(InLabel); }
}

void UTN_PauseRow::SetRowEnabled(bool bInEnabled)
{
	bEnabled = bInEnabled;
	RefreshLook();
}

void UTN_PauseRow::SetActive(bool bInActive)
{
	bActive = bInActive;
	RefreshLook();
}

void UTN_PauseRow::SetSliderValue(float InValue)
{
	Value = FMath::Clamp(InValue, Min, Max);
	RefreshValue();
}

void UTN_PauseRow::SetChoiceIndex(int32 InIndex, const FText& OverrideText)
{
	Index = InIndex;
	ChoiceOverride = OverrideText;
	RefreshValue();
}

void UTN_PauseRow::SetChoiceOptions(const TArray<FText>& InOptions, int32 InIndex)
{
	Options = InOptions;
	Index = InIndex;
	ChoiceOverride = FText::GetEmpty();
	RefreshValue();
}

void UTN_PauseRow::RefreshValue()
{
	if (!ValueText)
	{
		return;
	}
	if (Kind == ETNPauseRowKind::Slider)
	{
		if (Bar) { Bar->SetPercent((Value - Min) / FMath::Max(Max - Min, KINDA_SMALL_NUMBER)); }
		ValueText->SetText(Format ? Format(Value) : FText::AsNumber(Value));
	}
	else if (Kind == ETNPauseRowKind::Choice)
	{
		if (!ChoiceOverride.IsEmpty()) { ValueText->SetText(ChoiceOverride); }
		else { ValueText->SetText(Options.IsValidIndex(Index) ? Options[Index] : FText::GetEmpty()); }
	}
}

void UTN_PauseRow::RefreshLook()
{
	if (!Frame)
	{
		return;
	}
	const bool bLit = bFocused && bEnabled;
	switch (Style)
	{
	case ETNPauseRowStyle::List:
		Frame->SetBrush(TNHUDStyle::Rounded(bLit ? TNHUDArt::Hex(0x1A4273, 0.92f) : TNPauseUI::RowFill, 12.f,
			bLit ? TNHUDStyle::Accent : TNPauseUI::RowEdge, bLit ? 2.f : 1.f));
		if (LabelText) { LabelText->SetColorAndOpacity(FSlateColor(bEnabled ? (bLit ? TNHUDArt::Cream : TNHUDStyle::Text) : TNHUDStyle::TextDim)); }
		if (LeftArrow) { LeftArrow->SetRenderOpacity(bLit ? 1.f : 0.5f); }
		if (RightArrow) { RightArrow->SetRenderOpacity(bLit ? 1.f : 0.5f); }
		// Teclas: azul con filo claro las que se cambian, doradas mientras esperan, apagadas las fijas (el stick).
		for (int32 Device = 0; Device < 2; ++Device)
		{
			UBorder* Cap = Device == 0 ? KeyCap.Get() : PadCap.Get();
			UTextBlock* CapText = Device == 0 ? ValueText.Get() : Value2Text.Get();
			if (!Cap || !CapText)
			{
				continue;
			}
			const bool bEditableCap = bKeyEditable[Device];
			const bool bWaiting = bCapturing && bEditableCap;
			Cap->SetBrush(TNHUDStyle::Rounded(bWaiting ? TNHUDArt::Hex(0x5A3F0C, 0.95f) : (bEditableCap ? TNHUDArt::Hex(0x0B2A4A, 0.92f) : FLinearColor(0.f, 0.f, 0.f, 0.14f)),
				8.f, bWaiting ? TNHUDArt::Gold : (bEditableCap ? FLinearColor(1.f, 1.f, 1.f, bLit ? 0.35f : 0.16f) : FLinearColor::Transparent), bWaiting ? 2.f : 1.f));
			CapText->SetColorAndOpacity(FSlateColor(bWaiting ? TNHUDArt::Gold : (bEditableCap ? TNHUDArt::Cream : TNHUDStyle::TextDim)));
		}
		break;
	case ETNPauseRowStyle::Tab:
		Frame->SetBrush(TNPauseUI::BoxBrush(bActive ? TNShopArt::Pill(0xFFE27A, 0xF2A93B) : TNShopArt::Pill(0x62D2EA, 0x1E7FB0), TNPauseUI::PillMargin,
			bLit || bActive ? FLinearColor::White : FLinearColor(0.82f, 0.86f, 0.9f, 1.f)));
		if (LabelText) { LabelText->SetColorAndOpacity(FSlateColor(bActive ? TNHUDArt::Ink : TNHUDArt::Cream)); }
		if (LabelText) { TNHUDStyle::StyleText(LabelText, TEXT("Bold"), 19, bActive ? TNHUDArt::Ink : TNHUDArt::Cream, !bActive); }
		break;
	default:
		Frame->SetBrush(TNPauseUI::BoxBrush(TNHUDArt::SandTagTexture(), TNPauseUI::TagMargin, bLit ? TNHUDArt::Hex(0xFFE08A) : FLinearColor::White));
		break;
	}
	SetRenderOpacity(bEnabled ? 1.f : 0.55f);
}

void UTN_PauseRow::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Los botones grandes y las pestañas crecen un poco con el foco (y se hunden al pulsar).
	if (Style != ETNPauseRowStyle::List)
	{
		const bool bLit = bFocused && bEnabled;
		const float Grow = Style == ETNPauseRowStyle::Tab ? 1.06f : 1.04f;
		const float Target = (bLit ? Grow : 1.f) * (bPressed ? 0.96f : 1.f);
		const float Next = FMath::FInterpTo(Scale, Target, InDeltaTime, 16.f);
		if (!FMath::IsNearlyEqual(Next, Scale, 0.0005f))
		{
			Scale = Next;
			SetRenderScale(FVector2D(Scale, Scale));
		}
	}

	// Tecla esperando: las columnas que se pueden cambiar laten.
	if (bCapturing)
	{
		CaptureClock += InDeltaTime;
		const float Pulse = 0.55f + 0.45f * FMath::Abs(FMath::Cos(CaptureClock * 4.f));
		if (KeyCap && bKeyEditable[0]) { KeyCap->SetRenderOpacity(Pulse); }
		if (PadCap && bKeyEditable[1]) { PadCap->SetRenderOpacity(Pulse); }
	}

	// Medidor: sube rápido y baja despacio, como un vúmetro.
	if (Kind == ETNPauseRowKind::Meter && Sampler && Bar)
	{
		float Level = 0.f;
		float Mark = -1.f;
		FText Text;
		Sampler(Level, Mark, Text);
		MeterShown = Level > MeterShown ? Level : FMath::FInterpTo(MeterShown, Level, InDeltaTime, 5.f);
		Bar->SetPercent(MeterShown);
		Bar->SetFillColorAndOpacity(Mark >= 0.f && MeterShown >= Mark ? TNHUDArt::ShellGreen : TNHUDArt::SeaLight);
		if (MarkImage)
		{
			MarkImage->SetVisibility(Mark >= 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			if (UOverlaySlot* MarkSlot = Cast<UOverlaySlot>(MarkImage->Slot))
			{
				MarkSlot->SetPadding(FMargin(FMath::Clamp(Mark, 0.f, 1.f) * (TNPauseUI::MeterWidth - 4.f), 0.f, 0.f, 0.f));
			}
		}
		if (ValueText) { ValueText->SetText(Text); }
	}
}

void UTN_PauseRow::PlaySound(ETNPauseSound Sound, float Pitch) const
{
	if (OnSound) { OnSound(Sound, Pitch); }
}

void UTN_PauseRow::StepBy(int32 Direction)
{
	if (!bEnabled || Direction == 0)
	{
		return;
	}
	if (Kind == ETNPauseRowKind::Slider)
	{
		float NewValue = FMath::Clamp(Value + Direction * (Step > 0.f ? Step : (Max - Min) / 20.f), Min, Max);
		if (Step > 0.f) { NewValue = FMath::Clamp(Min + FMath::RoundToFloat((NewValue - Min) / Step) * Step, Min, Max); }
		if (FMath::IsNearlyEqual(NewValue, Value))
		{
			return;
		}
		Value = NewValue;
		RefreshValue();
		PlaySound(ETNPauseSound::Tick, (Value - Min) / FMath::Max(Max - Min, KINDA_SMALL_NUMBER));
		// El aviso, lo último: puede rehacer la lista entera.
		if (OnValueChanged) { OnValueChanged(Value); }
	}
	else if (Kind == ETNPauseRowKind::Choice && Options.Num() > 0)
	{
		Index = Index < 0 ? 0 : (Index + Direction + Options.Num()) % Options.Num();
		ChoiceOverride = FText::GetEmpty();
		RefreshValue();
		PlaySound(ETNPauseSound::Tick, Options.Num() > 1 ? static_cast<float>(Index) / (Options.Num() - 1) : 0.5f);
		if (OnChoiceChanged) { OnChoiceChanged(Index); }
	}
}

void UTN_PauseRow::Activate()
{
	if (!bEnabled)
	{
		return;
	}
	if (Kind == ETNPauseRowKind::Button || Kind == ETNPauseRowKind::Entry)
	{
		PlaySound(ETNPauseSound::Press);
		if (OnPressed) { OnPressed(); }
	}
	else if (Kind == ETNPauseRowKind::Choice)
	{
		StepBy(1);
	}
	else if (Kind == ETNPauseRowKind::KeyBind)
	{
		// Empieza a esperar la tecla nueva (la captura la lleva el menú).
		PlaySound(ETNPauseSound::Press);
		if (OnPressed) { OnPressed(); }
	}
}

void UTN_PauseRow::SetValueFromScreen(const FVector2D& ScreenPosition)
{
	if (!BarBox)
	{
		return;
	}
	const FGeometry& Geo = BarBox->GetCachedGeometry();
	const float Width = Geo.GetLocalSize().X;
	if (Width <= 1.f)
	{
		return;
	}
	const float T = FMath::Clamp(Geo.AbsoluteToLocal(ScreenPosition).X / Width, 0.f, 1.f);
	float NewValue = Min + T * (Max - Min);
	if (Step > 0.f) { NewValue = Min + FMath::RoundToFloat((NewValue - Min) / Step) * Step; }
	NewValue = FMath::Clamp(NewValue, Min, Max);
	if (FMath::IsNearlyEqual(NewValue, Value))
	{
		return;
	}
	Value = NewValue;
	RefreshValue();
	PlaySound(ETNPauseSound::Tick, (Value - Min) / FMath::Max(Max - Min, KINDA_SMALL_NUMBER));
	if (OnValueChanged) { OnValueChanged(Value); }
}

FReply UTN_PauseRow::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	using TNPauseUI::IsKey;
	const FKey Key = InKeyEvent.GetKey();
	const bool bValueRow = Kind == ETNPauseRowKind::Slider || Kind == ETNPauseRowKind::Choice;
	if (bValueRow && IsKey(Key, { EKeys::Left, EKeys::A, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_LeftStick_Left }))
	{
		StepBy(-1);
		return FReply::Handled();
	}
	if (bValueRow && IsKey(Key, { EKeys::Right, EKeys::D, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_LeftStick_Right }))
	{
		StepBy(1);
		return FReply::Handled();
	}
	// Una pestaña: izquierda y derecha cambian de pestaña (el menú deja el foco en la barra).
	if (OnSideStep && IsKey(Key, { EKeys::Left, EKeys::A, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_LeftStick_Left }))
	{
		OnSideStep(-1);
		return FReply::Handled();
	}
	if (OnSideStep && IsKey(Key, { EKeys::Right, EKeys::D, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_LeftStick_Right }))
	{
		OnSideStep(1);
		return FReply::Handled();
	}
	// Fila de tecla: Supr o Y del mando la devuelven a la de serie.
	if (Kind == ETNPauseRowKind::KeyBind && IsKey(Key, { EKeys::Delete, EKeys::Gamepad_FaceButton_Top }))
	{
		if (!InKeyEvent.IsRepeat() && bEnabled && OnReset)
		{
			PlaySound(ETNPauseSound::Press);
			// Lo último: rehace la lista entera.
			OnReset();
		}
		return FReply::Handled();
	}
	if (IsKey(Key, { EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom, EKeys::Virtual_Accept }))
	{
		if (!InKeyEvent.IsRepeat()) { Activate(); }
		return FReply::Handled();
	}
	// WASD también recorre el menú (la mano izquierda ya está ahí).
	if (Key == EKeys::W) { return FReply::Handled().SetNavigation(EUINavigation::Up, ENavigationGenesis::Keyboard); }
	if (Key == EKeys::S) { return FReply::Handled().SetNavigation(EUINavigation::Down, ENavigationGenesis::Keyboard); }
	if (!bValueRow && Key == EKeys::A) { return FReply::Handled().SetNavigation(EUINavigation::Left, ENavigationGenesis::Keyboard); }
	if (!bValueRow && Key == EKeys::D) { return FReply::Handled().SetNavigation(EUINavigation::Right, ENavigationGenesis::Keyboard); }
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FNavigationReply UTN_PauseRow::NativeOnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent, const FNavigationReply& InDefaultReply)
{
	// En los deslizadores, las listas y las pestañas, izquierda y derecha cambian el valor o la pestaña (el stick también
	// manda navegación): no se sale.
	const EUINavigation Direction = InNavigationEvent.GetNavigationType();
	const bool bSideStep = Kind == ETNPauseRowKind::Slider || Kind == ETNPauseRowKind::Choice || OnSideStep;
	if (bSideStep && (Direction == EUINavigation::Left || Direction == EUINavigation::Right))
	{
		return FNavigationReply::Stop();
	}
	return Super::NativeOnNavigation(MyGeometry, InNavigationEvent, InDefaultReply);
}

FReply UTN_PauseRow::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// El menú de un invitado de la partida local lo maneja solo su mando: el ratón es del jugador 1.
	if (UTN_LocalPlaySubsystem::IsGuest(GetOwningPlayer()))
	{
		return FReply::Handled();
	}
	// Fila de tecla: clic derecho, a la de serie.
	if (Kind == ETNPauseRowKind::KeyBind && InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		SetKeyboardFocus();
		if (bEnabled && OnReset)
		{
			PlaySound(ETNPauseSound::Press);
			OnReset();
		}
		return FReply::Handled();
	}
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Handled();
	}
	SetKeyboardFocus();
	if (!bEnabled)
	{
		return FReply::Handled();
	}
	const FVector2D At = InMouseEvent.GetScreenSpacePosition();
	switch (Kind)
	{
	case ETNPauseRowKind::Button:
	case ETNPauseRowKind::KeyBind:
	case ETNPauseRowKind::Entry:
		// Se pulsa al soltar (así el clic que empieza a esperar una tecla no cuenta como la tecla).
		bPressed = true;
		return FReply::Handled();
	case ETNPauseRowKind::Slider:
		if (BarBox && BarBox->GetCachedGeometry().IsUnderLocation(At))
		{
			bDragging = true;
			SetValueFromScreen(At);
			return FReply::Handled().CaptureMouse(TakeWidget());
		}
		return FReply::Handled();
	case ETNPauseRowKind::Choice:
		StepBy(LeftArrow && LeftArrow->GetCachedGeometry().IsUnderLocation(At) ? -1 : 1);
		return FReply::Handled();
	default:
		return FReply::Handled();
	}
}

FReply UTN_PauseRow::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging)
	{
		bDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bPressed)
	{
		bPressed = false;
		if (IsHovered()) { Activate(); }
	}
	return FReply::Handled();
}

FReply UTN_PauseRow::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging)
	{
		SetValueFromScreen(InMouseEvent.GetScreenSpacePosition());
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

void UTN_PauseRow::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	bHovered = true;
	// El ratón mueve el foco: teclado, ratón y mando comparten el mismo resaltado (no en el menú de un invitado de la
	// partida local: el ratón es del jugador 1).
	if (!HasAnyUserFocus() && !UTN_LocalPlaySubsystem::IsGuest(GetOwningPlayer())) { SetKeyboardFocus(); }
}

void UTN_PauseRow::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bHovered = false;
	bPressed = false;
}

void UTN_PauseRow::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);
	bDragging = false;
}

void UTN_PauseRow::NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent)
{
	Super::NativeOnAddedToFocusPath(InFocusEvent);
	if (!bFocused)
	{
		bFocused = true;
		RefreshLook();
		PlaySound(ETNPauseSound::Hover);
		if (OnFocused) { OnFocused(this); }
	}
}

void UTN_PauseRow::NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent)
{
	Super::NativeOnRemovedFromFocusPath(InFocusEvent);
	bFocused = false;
	bPressed = false;
	RefreshLook();
}

// ─────────────────────────────────────────────────────────────────────────────
// Contador de FPS
// ─────────────────────────────────────────────────────────────────────────────

void UTN_FpsCounterWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree || FpsText)
	{
		return;
	}
	UCanvasPanel* Root = TNPauseUI::Make<UCanvasPanel>(WidgetTree);
	WidgetTree->RootWidget = Root;
	FpsText = TNPauseUI::Label(WidgetTree, FText::GetEmpty(), TEXT("Bold"), 17, TNHUDArt::Foam);
	UBorder* Tag = TNPauseUI::Make<UBorder>(WidgetTree);
	TNHUDStyle::StylePanel(Tag, TNHUDStyle::PanelSoft, 10.f, FMargin(12.f, 3.f, 12.f, 5.f));
	Tag->SetContent(FpsText);
	TNPauseUI::Pin(Root, Tag, FVector2D(1.f, 1.f), FVector2D(-14.f, -10.f));
	// Solo se ve: no quita clics a nada.
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UTN_FpsCounterWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Accumulated += InDeltaTime;
	WorstFrame = FMath::Max(WorstFrame, InDeltaTime);
	++Frames;
	if (Accumulated >= 0.5f && FpsText)
	{
		const int32 Fps = FMath::RoundToInt(Frames / Accumulated);
		FpsText->SetText(FText::Format(NSLOCTEXT("TNPause", "FpsFmt", "{0} FPS · {1} ms"), FText::AsNumber(Fps),
			FText::AsNumber(FMath::RoundToInt(WorstFrame * 1000.f))));
		FpsText->SetColorAndOpacity(FSlateColor(Fps >= 55 ? TNHUDArt::Foam : (Fps >= 30 ? TNHUDArt::Gold : TNHUDArt::CoralLight)));
		Accumulated = 0.f;
		Frames = 0;
		WorstFrame = 0.f;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Quién habla
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TalkersWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree || List)
	{
		return;
	}
	UCanvasPanel* Root = TNPauseUI::Make<UCanvasPanel>(WidgetTree);
	WidgetTree->RootWidget = Root;
	List = TNPauseUI::Make<UVerticalBox>(WidgetTree);
	// A la derecha, un poco por debajo del centro (arriba a la derecha está el marcador).
	TNPauseUI::Pin(Root, List, FVector2D(1.f, 0.5f), FVector2D(-20.f, 60.f));
	// Solo se ve: no quita clics a nada.
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UTN_TalkersWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Timer -= InDeltaTime;
	if (Timer > 0.f || !List)
	{
		return;
	}
	Timer = 0.1f;
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const APlayerController* PC = GameInstance ? GameInstance->GetFirstLocalPlayerController(World) : nullptr;
	const APlayerState* Mine = PC ? PC->PlayerState.Get() : nullptr;
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	const UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(this);

	// Quién se oye ahora: los demás, si no están silenciados; uno mismo, si su voz sale.
	TArray<FString> Now;
	if (State)
	{
		for (APlayerState* PS : State->PlayerArray)
		{
			const APawn* Pawn = PS ? PS->GetPawn() : nullptr;
			const UProximityVoiceComponent* Voice = Pawn ? Pawn->FindComponentByClass<UProximityVoiceComponent>() : nullptr;
			if (!Voice || !Voice->IsHeardSpeaking())
			{
				continue;
			}
			const bool bMe = PS == Mine;
			if (bMe ? (Settings && !Settings->IsTransmitAllowed()) : (Settings && Settings->IsPlayerMuted(UTN_GameSettingsSubsystem::PlayerKey(PS))))
			{
				continue;
			}
			Now.Add(bMe ? FString() : PS->GetPlayerName());
		}
	}
	if (Now == Shown)
	{
		return;
	}
	Shown = Now;
	List->ClearChildren();
	UWidgetTree* Tree = WidgetTree;
	for (const FString& Name : Shown)
	{
		const bool bMe = Name.IsEmpty();
		UHorizontalBox* Line = TNPauseUI::Make<UHorizontalBox>(Tree);
		TNPauseUI::AddH(Line, TNPauseUI::Picture(Tree, bMe ? TNPauseArt::MicIcon(false) : TNPauseArt::SpeakerIcon(false), FVector2D(26.f, 26.f)),
			FMargin(0.f, 0.f, 8.f, 0.f));
		TNPauseUI::AddH(Line, TNPauseUI::Label(Tree, bMe ? NSLOCTEXT("TNPause", "TalkerMe", "Tú hablas") : FText::Format(NSLOCTEXT("TNPause", "TalkerOther", "{0} habla"),
			FText::FromString(Name)), TEXT("Bold"), 18, bMe ? TNHUDArt::Gold : TNHUDArt::Cream));
		UBorder* Chip = TNPauseUI::Make<UBorder>(Tree);
		TNHUDStyle::StylePanel(Chip, TNHUDStyle::PanelSoft, 12.f, FMargin(10.f, 4.f, 14.f, 5.f));
		Chip->SetContent(Line);
		TNPauseUI::AddV(List, Chip, FMargin(0.f, 0.f, 0.f, 6.f), HAlign_Right);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Menú: montaje
// ─────────────────────────────────────────────────────────────────────────────

void UTN_PauseMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// El propio menú se enfoca al hacer clic fuera de las opciones: las teclas siguen sin llegar al juego.
	SetIsFocusable(true);
	BuildTree();
}

void UTN_PauseMenuWidget::BuildTree()
{
	if (!WidgetTree || Canvas)
	{
		return;
	}
	UWidgetTree* Tree = WidgetTree;
	UCanvasPanel* Root = TNPauseUI::Make<UCanvasPanel>(Tree);
	Tree->RootWidget = Root;

	// Velo azul marino sobre el juego (que sigue moviéndose detrás), a pantalla completa.
	UImage* Veil = TNPauseUI::Make<UImage>(Tree);
	Veil->SetColorAndOpacity(TNHUDArt::Hex(0x0A1C38, 0.72f));
	TNPauseUI::Fill(Root, Veil);

	// El menú, en un lienzo de 1920 × 1080 que se encoge si no cabe (tamaño de la interfaz grande, ventana pequeña).
	Canvas = TNPauseUI::Make<UCanvasPanel>(Tree);
	UScaleBox* CanvasFit = TNPauseUI::Fit(Tree, Canvas);
	// Partida local: en grande, a toda la pantalla aunque la interfaz vaya más pequeña por la pantalla partida.
	if (IsLocalGame()) { CanvasFit->SetStretchDirection(EStretchDirection::Both); }
	TNPauseUI::Fill(Root, CanvasFit);

	TNPauseUI::Pin(Canvas, BuildHeader(), FVector2D(0.5f, 0.f), FVector2D(0.f, 18.f));

	Pages = TNPauseUI::Make<UWidgetSwitcher>(Tree);
	CreditsPage = UTN_CreditsWidget::CreatePage(this);
	// En el orden de ETNPausePage.
	for (UWidget* PageWidget : { BuildHomePage(), BuildSettingsPage(), BuildControlsPage(), BuildRoomPage(), static_cast<UWidget*>(CreditsPage.Get()) })
	{
		if (UWidgetSwitcherSlot* PageSlot = Cast<UWidgetSwitcherSlot>(Pages->AddChild(PageWidget)))
		{
			PageSlot->SetHorizontalAlignment(HAlign_Center);
			PageSlot->SetVerticalAlignment(VAlign_Top);
		}
	}
	TNPauseUI::Pin(Canvas, Pages, FVector2D(0.5f, 0.f), FVector2D(0.f, 258.f));

	// Ayuda de la opción enfocada y atajos, abajo.
	HelpText = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Regular"), 18, TNHUDArt::Foam);
	HelpText->SetJustification(ETextJustify::Center);
	TNPauseUI::Pin(Canvas, TNPauseUI::Sized(Tree, HelpText, 1180.f, 0.f), FVector2D(0.5f, 1.f), FVector2D(0.f, -52.f));
	HintText = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 16, TNHUDStyle::TextDim);
	TNPauseUI::Pin(Canvas, HintText, FVector2D(0.5f, 1.f), FVector2D(0.f, -20.f));
	// Avisos de un momento (teclas cambiadas...), dorados, encima de la ayuda.
	NoticeText = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 19, TNHUDArt::Gold);
	NoticeText->SetJustification(ETextJustify::Center);
	NoticeText->SetAutoWrapText(true);
	NoticeText->SetVisibility(ESlateVisibility::Collapsed);
	TNPauseUI::Pin(Canvas, TNPauseUI::Sized(Tree, NoticeText, 1180.f, 0.f), FVector2D(0.5f, 1.f), FVector2D(0.f, -86.f));

	ConfirmLayer = BuildConfirmLayer();
	TNPauseUI::Fill(Root, ConfirmLayer);
	ConfirmLayer->SetVisibility(ESlateVisibility::Collapsed);

	RefreshHeader();
	RefreshHint();
}

UWidget* UTN_PauseMenuWidget::BuildHeader()
{
	UWidgetTree* Tree = WidgetTree;
	UVerticalBox* Info = TNPauseUI::Make<UVerticalBox>(Tree);
	ModeText = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 24, TNHUDArt::Gold);
	ModeText->SetJustification(ETextJustify::Center);
	TNPauseUI::AddV(Info, ModeText, FMargin(0.f), HAlign_Center);
	SessionText = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Regular"), 18, TNHUDArt::Foam);
	SessionText->SetJustification(ETextJustify::Center);
	TNPauseUI::AddV(Info, SessionText, FMargin(0.f, 2.f, 0.f, 0.f), HAlign_Center);
	PlayersBox = TNPauseUI::Make<UHorizontalBox>(Tree);
	TNPauseUI::AddV(Info, PlayersBox, FMargin(0.f, 10.f, 0.f, 0.f), HAlign_Center);

	// Cartel azul marino con la cinta «PAUSA» encima de su borde.
	UOverlay* Stack = TNPauseUI::Make<UOverlay>(Tree);
	TNPauseUI::AddO(Stack, TNPauseUI::Card(Tree, Info, FMargin(40.f, 34.f, 40.f, 38.f)), HAlign_Fill, VAlign_Top, FMargin(0.f, 34.f, 0.f, 0.f));
	UVerticalBox* TitleColumn = TNPauseUI::Make<UVerticalBox>(Tree);
	UTextBlock* Title = TNPauseUI::Label(Tree, NSLOCTEXT("TNPause", "Title", "PAUSA"), TEXT("Black"), 34, FLinearColor::White);
	UBorder* Ribbon = TNPauseUI::Make<UBorder>(Tree);
	Ribbon->SetBrush(TNPauseUI::BoxBrush(TNHUDArt::RibbonTexture(), TNPauseUI::RibbonMargin));
	Ribbon->SetPadding(FMargin(76.f, 8.f, 76.f, 14.f));
	Ribbon->SetHorizontalAlignment(HAlign_Center);
	Ribbon->SetContent(Title);
	Ribbon->SetRenderTransformAngle(-2.f);
	TNPauseUI::AddV(TitleColumn, Ribbon, FMargin(0.f), HAlign_Center);
	TNPauseUI::AddO(Stack, TitleColumn, HAlign_Center, VAlign_Top);
	return TNPauseUI::Sized(Tree, Stack, TNPauseUI::HeaderWidth, 0.f);
}

UWidget* UTN_PauseMenuWidget::BuildHomePage()
{
	UWidgetTree* Tree = WidgetTree;
	HomeColumn = TNPauseUI::Make<UVerticalBox>(Tree);
	BuildHomeButtons();
	HomeNote = TNPauseUI::Label(Tree, IsLocalGame()
		? NSLOCTEXT("TNLocal", "PauseHomeNote", "La partida está parada para todos mientras este menú está abierto.")
		: NSLOCTEXT("TNPause", "HomeNote", "La partida sigue en marcha mientras miras el menú: tu tortuga se queda quieta."),
		TEXT("Regular"), 18, TNHUDArt::Foam);
	HomeNote->SetJustification(ETextJustify::Center);
	TNPauseUI::AddV(HomeColumn, HomeNote, FMargin(0.f, 6.f, 0.f, 0.f), HAlign_Center);
	return HomeColumn;
}

void UTN_PauseMenuWidget::BuildHomeButtons()
{
	TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
	auto AddBig = [this](const FText& Label, TNPauseArt::EMenuIcon Icon, const FText& Description, TFunction<void()> Action) -> UTN_PauseRow*
	{
		UTN_PauseRow* Row = NewRow();
		if (!Row)
		{
			return nullptr;
		}
		Row->SetupButton(ETNPauseRowStyle::Big, Label, MoveTemp(Action), TNPauseArt::MenuIcon(Icon));
		Row->SetDescription(Description);
		TNPauseUI::AddV(HomeColumn, Row, FMargin(0.f, 0.f, 0.f, 12.f), HAlign_Center);
		HomeRows.Add(Row);
		return Row;
	};

	AddBig(NSLOCTEXT("TNPause", "Resume", "Continuar"), TNPauseArt::EMenuIcon::Resume,
		NSLOCTEXT("TNPause", "ResumeDesc", "Vuelve a la partida."),
		[WeakThis]() { if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->CloseMenu(); } });
	AddBig(NSLOCTEXT("TNPause", "Settings", "Ajustes"), TNPauseArt::EMenuIcon::Settings,
		NSLOCTEXT("TNPause", "SettingsDesc", "Gráficos, sonido, voz, controles y accesibilidad. Todo se aplica al momento."),
		[WeakThis]() { if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->ShowPage(ETNPausePage::Settings); } });
	AddBig(NSLOCTEXT("TNPause", "Controls", "Controles"), TNPauseArt::EMenuIcon::Controls,
		NSLOCTEXT("TNPause", "ControlsDesc", "Todas las teclas y botones del juego, con teclado y ratón o con mando."),
		[WeakThis]() { if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->ShowPage(ETNPausePage::Controls); } });
	// Cuarto botón (HomeRows[3]: el foco vuelve a él al salir de la página).
	if (UTN_PauseRow* CreditsRow = NewRow())
	{
		CreditsRow->SetupButton(ETNPauseRowStyle::Big, NSLOCTEXT("TNCredits", "Button", "Créditos"),
			[WeakThis]() { if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->ShowPage(ETNPausePage::Credits); } }, UTN_CreditsWidget::MenuIcon());
		CreditsRow->SetDescription(NSLOCTEXT("TNCredits", "ButtonDesc", "Quién ha hecho Tortunavy y las licencias de lo que usa: arte, fuentes y motor."));
		TNPauseUI::AddV(HomeColumn, CreditsRow, FMargin(0.f, 0.f, 0.f, 12.f), HAlign_Center);
		HomeRows.Add(CreditsRow);
	}

	// Tutorial de la primera partida (Docs/Tutorial.md): saltarlo, con confirmación. Solo mientras se está en él.
	const UTN_TutorialPlayerComponent* TutorialComp = UTN_TutorialPlayerComponent::FindFor(GetOwningPlayer());
	if (TutorialComp && TutorialComp->IsInTutorial())
	{
		AddBig(NSLOCTEXT("TNTutorial", "PauseSkip", "Saltar el tutorial"), TNPauseArt::EMenuIcon::Lobby,
			NSLOCTEXT("TNTutorial", "PauseSkipDesc", "Bajas directamente al lobby del castillo y no vuelve a salir en las siguientes partidas."),
			[WeakThis]()
			{
				UTN_PauseMenuWidget* Menu = WeakThis.Get();
				if (!Menu)
				{
					return;
				}
				Menu->AskConfirm(NSLOCTEXT("TNTutorial", "PauseSkipTitle", "¿Saltar el tutorial?"),
					NSLOCTEXT("TNTutorial", "PauseSkipText", "Vuelves al lobby y el tutorial queda hecho en este ordenador. Las teclas siempre están en Controles."),
					NSLOCTEXT("TNTutorial", "PauseSkipYes", "Saltar"), [WeakThis]()
					{
						UTN_PauseMenuWidget* SkipMenu = WeakThis.Get();
						if (!SkipMenu)
						{
							return;
						}
						if (UTN_TutorialPlayerComponent* Comp = UTN_TutorialPlayerComponent::FindFor(SkipMenu->GetOwningPlayer()))
						{
							Comp->RequestSkip();
						}
						SkipMenu->CloseMenu();
					});
			});
	}

	// Partida local, en el lobby: el tutorial no sale solo, se hace desde aquí (y se salta con «Saltar el tutorial»).
	if (IsLocalGame() && IsInLobby() && TutorialComp && !TutorialComp->IsInTutorial())
	{
		AddBig(NSLOCTEXT("TNLocal", "PauseTutorial", "Hacer el tutorial"), TNPauseArt::EMenuIcon::Controls,
			NSLOCTEXT("TNLocal", "PauseTutorialDesc", "Subes a las islas del cielo y aprendes todo lo que hace una tortuga. Se salta desde este menú."),
			[WeakThis]()
			{
				UTN_PauseMenuWidget* Menu = WeakThis.Get();
				UTN_TutorialPlayerComponent* Comp = Menu ? UTN_TutorialPlayerComponent::FindFor(Menu->GetOwningPlayer()) : nullptr;
				if (!Menu)
				{
					return;
				}
				Menu->CloseMenu();
				if (Comp) { Comp->RequestStart(true); }
			});
	}

	// Un invitado de la partida local: solo lo suyo; en el lobby, puede dejar de jugar (su vista desaparece).
	if (IsGuestMenu())
	{
		if (IsInLobby())
		{
			AddBig(NSLOCTEXT("TNLocal", "PauseLeave", "Dejar de jugar"), TNPauseArt::EMenuIcon::Menu,
				NSLOCTEXT("TNLocal", "PauseLeaveDesc", "Tu tortuga se va y tu vista desaparece. Para volver, pulsa Start en tu mando. También se sale manteniendo B en el lobby."),
				[WeakThis]()
				{
					UTN_PauseMenuWidget* Menu = WeakThis.Get();
					if (!Menu)
					{
						return;
					}
					Menu->AskConfirm(NSLOCTEXT("TNLocal", "PauseLeaveTitle", "¿Dejar de jugar?"),
						NSLOCTEXT("TNLocal", "PauseLeaveText", "Tu tortuga se va de la partida y los demás siguen jugando."),
						NSLOCTEXT("TNPause", "LeaveYes", "Salir"), [WeakThis]()
						{
							UTN_PauseMenuWidget* LeaveMenu = WeakThis.Get();
							APlayerController* PC = LeaveMenu ? LeaveMenu->GetOwningPlayer() : nullptr;
							UTN_LocalPlaySubsystem* LocalPlay = UTN_LocalPlaySubsystem::Get(LeaveMenu);
							if (!LeaveMenu)
							{
								return;
							}
							LeaveMenu->CloseMenu();
							if (LocalPlay) { LocalPlay->LeaveGame(PC); }
						});
				});
		}
		return;
	}

	// Sala (partida en red): su nombre y código, cerrarla y abrirla y expulsar (el anfitrión), y quién está dentro.
	if (HasRoomPage())
	{
		RoomHomeRow = NewRow();
		if (RoomHomeRow)
		{
			RoomHomeRow->SetupButton(ETNPauseRowStyle::Big, NSLOCTEXT("TNPause", "Room", "Sala"),
				[WeakThis]() { if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->ShowPage(ETNPausePage::Room); } }, TNRoomArt::RoomMenuIcon());
			RoomHomeRow->SetDescription(IsHost()
				? NSLOCTEXT("TNPause", "RoomDescHost", "El nombre y el código de tu sala, cerrarla para que no entre nadie más y expulsar a alguien.")
				: NSLOCTEXT("TNPause", "RoomDescGuest", "El nombre y el código de la sala y quién está dentro."));
			TNPauseUI::AddV(HomeColumn, RoomHomeRow, FMargin(0.f, 0.f, 0.f, 12.f), HAlign_Center);
			HomeRows.Add(RoomHomeRow);
		}
	}

	if (CanReturnToLobby())
	{
		AddBig(NSLOCTEXT("TNPause", "Lobby", "Volver al lobby"), TNPauseArt::EMenuIcon::Lobby,
			IsHost() ? NSLOCTEXT("TNPause", "LobbyDescHost", "Acaba la ronda y lleva a todo el grupo de vuelta al lobby del castillo.")
				: NSLOCTEXT("TNPause", "LobbyDescGuest", "Al grupo lo lleva al lobby el anfitrión; tú sales de la partida al menú principal."),
			[WeakThis]() { if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->ReturnToLobby(); } });
	}

	const bool bGuest = GetWorld() && GetWorld()->GetNetMode() == NM_Client;
	AddBig(bGuest ? NSLOCTEXT("TNPause", "LeaveGame", "Salir de la partida") : NSLOCTEXT("TNPause", "MainMenu", "Menú principal"),
		TNPauseArt::EMenuIcon::Menu,
		bGuest ? NSLOCTEXT("TNPause", "LeaveGameDesc", "Sales de la sesión y vuelves al menú principal. Los demás siguen jugando.")
			: NSLOCTEXT("TNPause", "MainMenuDesc", "Cierra la partida para todos y vuelve al menú principal."),
		[WeakThis]() { if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->LeaveToMenu(); } });
	AddBig(NSLOCTEXT("TNPause", "Quit", "Salir al escritorio"), TNPauseArt::EMenuIcon::Quit,
		NSLOCTEXT("TNPause", "QuitDesc", "Cierra Tortunavy."),
		[WeakThis]() { if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->QuitToDesktop(); } });
}

UWidget* UTN_PauseMenuWidget::BuildSettingsPage()
{
	UWidgetTree* Tree = WidgetTree;
	UVerticalBox* Column = TNPauseUI::Make<UVerticalBox>(Tree);

	// Pestañas.
	UHorizontalBox* TabBar = TNPauseUI::Make<UHorizontalBox>(Tree);
	const FText TabNames[] = { NSLOCTEXT("TNPause", "TabGraphics", "GRÁFICOS"), NSLOCTEXT("TNPause", "TabSound", "SONIDO"),
		NSLOCTEXT("TNPause", "TabVoice", "VOZ"), NSLOCTEXT("TNPause", "TabControls", "CONTROLES"), NSLOCTEXT("TNPause", "TabGame", "JUEGO") };
	TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
	for (int32 i = 0; i < static_cast<int32>(ETNPauseTab::Count); ++i)
	{
		UTN_PauseRow* TabRow = NewRow();
		if (!TabRow)
		{
			continue;
		}
		TabRow->SetupButton(ETNPauseRowStyle::Tab, TabNames[i], [WeakThis, i]()
		{
			if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->ShowTab(static_cast<ETNPauseTab>(i)); }
		});
		TabRow->SetDescription(NSLOCTEXT("TNPause", "TabDesc", "Q y E (o LB y RB) cambian de pestaña."));
		// En la barra, izquierda y derecha también: se abre la pestaña de al lado de esta (no de la abierta: subiendo desde
		// la lista se puede llegar a otra) y el foco sigue en la barra.
		TabRow->OnSideStep = [WeakThis, i](int32 Direction)
		{
			UTN_PauseMenuWidget* Menu = WeakThis.Get();
			if (!Menu)
			{
				return;
			}
			const ETNPauseTab Next = Menu->StepTab(static_cast<ETNPauseTab>(i), Direction);
			if (Next == static_cast<ETNPauseTab>(i))
			{
				return;
			}
			Menu->PlayUISound(ETNPauseSound::Press, 0.f);
			// La de al lado ya está abierta: solo el foco.
			if (Next == Menu->Tab && Menu->TabRows.IsValidIndex(static_cast<int32>(Next))) { Menu->FocusRow(Menu->TabRows[static_cast<int32>(Next)]); }
			else { Menu->ShowTab(Next, false); }
		};
		TNPauseUI::AddH(TabBar, TabRow, FMargin(5.f, 0.f));
		TabRows.Add(TabRow);
		// Un invitado de la partida local, solo Controles y Juego; en la partida local, sin voz.
		if (!IsTabAvailable(static_cast<ETNPauseTab>(i))) { TabRow->SetVisibility(ESlateVisibility::Collapsed); }
	}
	TNPauseUI::AddV(Column, TabBar, FMargin(0.f, 0.f, 0.f, 14.f), HAlign_Center);

	SettingsList = TNPauseUI::Make<UScrollBox>(Tree);
	// La barra de desplazamiento de serie, con el fondo y el tirador del estilo del HUD.
	FScrollBarStyle BarStyle = SettingsList->GetWidgetBarStyle();
	BarStyle.SetVerticalBackgroundImage(TNHUDStyle::Rounded(FLinearColor(0.f, 0.02f, 0.04f, 0.5f), 4.f));
	BarStyle.SetNormalThumbImage(TNHUDStyle::Rounded(TNHUDArt::Hex(0x62D2EA, 0.55f), 4.f));
	BarStyle.SetHoveredThumbImage(TNHUDStyle::Rounded(TNHUDArt::Hex(0x62D2EA, 0.85f), 4.f));
	BarStyle.SetDraggedThumbImage(TNHUDStyle::Rounded(TNHUDArt::Gold, 4.f));
	SettingsList->SetWidgetBarStyle(BarStyle);
	SettingsList->SetScrollbarThickness(FVector2D(8.f, 8.f));
	SettingsList->SetScrollbarPadding(FMargin(8.f, 0.f, 0.f, 0.f));
	TNPauseUI::AddV(Column, TNPauseUI::Sized(Tree, SettingsList, 0.f, TNPauseUI::SettingsListHeight));

	return TNPauseUI::Sized(Tree, TNPauseUI::Card(Tree, Column, FMargin(34.f, 24.f, 34.f, 48.f)), TNPauseUI::CardWidth, 0.f);
}

UWidget* UTN_PauseMenuWidget::BuildControlsPage()
{
	UWidgetTree* Tree = WidgetTree;
	UVerticalBox* Column = TNPauseUI::Make<UVerticalBox>(Tree);
	UTextBlock* Title = TNPauseUI::Label(Tree, NSLOCTEXT("TNPause", "ControlsTitle", "CONTROLES"), TEXT("Black"), 28, TNHUDArt::Gold);
	TNPauseUI::AddV(Column, Title, FMargin(0.f, 0.f, 0.f, 8.f), HAlign_Center);

	// Cabecera de columnas (alineada con las filas de texto: nombre, teclado y ratón, mando).
	UHorizontalBox* Heads = TNPauseUI::Make<UHorizontalBox>(Tree);
	TNPauseUI::AddH(Heads, TNPauseUI::Label(Tree, NSLOCTEXT("TNPause", "ColAction", "ACCIÓN"), TEXT("Bold"), 16, TNHUDStyle::TextDim), FMargin(20.f, 0.f, 12.f, 0.f), true);
	TNPauseUI::AddH(Heads, TNPauseUI::Sized(Tree, TNPauseUI::Label(Tree, NSLOCTEXT("TNPause", "ColKeyboard", "TECLADO Y RATÓN"), TEXT("Bold"), 16, TNHUDStyle::TextDim), 330.f, 0.f),
		FMargin(0.f, 0.f, 12.f, 0.f));
	TNPauseUI::AddH(Heads, TNPauseUI::Sized(Tree, TNPauseUI::Label(Tree, NSLOCTEXT("TNPause", "ColPad", "MANDO"), TEXT("Bold"), 16, TNHUDStyle::TextDim), 306.f, 0.f));
	TNPauseUI::AddV(Column, Heads, FMargin(0.f, 0.f, 0.f, 6.f));

	ControlsList = TNPauseUI::Make<UScrollBox>(Tree);
	// La misma barra que la de los ajustes (esa página se monta antes).
	if (SettingsList) { ControlsList->SetWidgetBarStyle(SettingsList->GetWidgetBarStyle()); }
	ControlsList->SetScrollbarThickness(FVector2D(8.f, 8.f));
	ControlsList->SetScrollbarPadding(FMargin(8.f, 0.f, 0.f, 0.f));
	TNPauseUI::AddV(Column, TNPauseUI::Sized(Tree, ControlsList, 0.f, TNPauseUI::ControlsListHeight));

	return TNPauseUI::Sized(Tree, TNPauseUI::Card(Tree, Column, FMargin(34.f, 22.f, 34.f, 48.f)), TNPauseUI::CardWidth, 0.f);
}

UWidget* UTN_PauseMenuWidget::BuildRoomPage()
{
	UWidgetTree* Tree = WidgetTree;
	UVerticalBox* Column = TNPauseUI::Make<UVerticalBox>(Tree);
	UTextBlock* Title = TNPauseUI::Label(Tree, NSLOCTEXT("TNPause", "RoomTitle", "SALA"), TEXT("Black"), 28, TNHUDArt::Gold);
	TNPauseUI::AddV(Column, Title, FMargin(0.f, 0.f, 0.f, 10.f), HAlign_Center);

	RoomList = TNPauseUI::Make<UScrollBox>(Tree);
	// La misma barra que la de los ajustes (esa página se monta antes).
	if (SettingsList) { RoomList->SetWidgetBarStyle(SettingsList->GetWidgetBarStyle()); }
	RoomList->SetScrollbarThickness(FVector2D(8.f, 8.f));
	RoomList->SetScrollbarPadding(FMargin(8.f, 0.f, 0.f, 0.f));
	TNPauseUI::AddV(Column, TNPauseUI::Sized(Tree, RoomList, 0.f, TNPauseUI::RoomListHeight));

	return TNPauseUI::Sized(Tree, TNPauseUI::Card(Tree, Column, FMargin(34.f, 22.f, 34.f, 48.f)), TNPauseUI::CardWidth, 0.f);
}

UWidget* UTN_PauseMenuWidget::BuildConfirmLayer()
{
	UWidgetTree* Tree = WidgetTree;
	UOverlay* Layer = TNPauseUI::Make<UOverlay>(Tree);
	// Velo más oscuro que tapa (y bloquea) el resto del menú.
	UImage* Dim = TNPauseUI::Make<UImage>(Tree);
	Dim->SetColorAndOpacity(TNHUDArt::Hex(0x050E1C, 0.62f));
	TNPauseUI::AddO(Layer, Dim, HAlign_Fill, VAlign_Fill);

	UVerticalBox* Column = TNPauseUI::Make<UVerticalBox>(Tree);
	ConfirmTitle = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Black"), 30, TNHUDArt::Gold);
	ConfirmTitle->SetJustification(ETextJustify::Center);
	TNPauseUI::AddV(Column, ConfirmTitle, FMargin(0.f, 0.f, 0.f, 10.f), HAlign_Center);
	ConfirmText = TNPauseUI::Label(Tree, FText::GetEmpty(), TEXT("Regular"), 21, TNHUDArt::Cream);
	ConfirmText->SetJustification(ETextJustify::Center);
	ConfirmText->SetAutoWrapText(true);
	TNPauseUI::AddV(Column, ConfirmText, FMargin(0.f, 0.f, 0.f, 22.f), HAlign_Fill);

	UHorizontalBox* Buttons = TNPauseUI::Make<UHorizontalBox>(Tree);
	TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
	ConfirmYes = NewRow();
	ConfirmNo = NewRow();
	if (ConfirmYes)
	{
		ConfirmYes->SetupButton(ETNPauseRowStyle::Dialog, NSLOCTEXT("TNPause", "Yes", "Sí"), [WeakThis]()
		{
			if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->CloseConfirm(true); }
		});
		TNPauseUI::AddH(Buttons, ConfirmYes, FMargin(10.f, 0.f));
	}
	if (ConfirmNo)
	{
		ConfirmNo->SetupButton(ETNPauseRowStyle::Dialog, NSLOCTEXT("TNPause", "No", "Cancelar"), [WeakThis]()
		{
			if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->CloseConfirm(false); }
		});
		TNPauseUI::AddH(Buttons, ConfirmNo, FMargin(10.f, 0.f));
	}
	TNPauseUI::AddV(Column, Buttons, FMargin(0.f), HAlign_Center);

	UWidget* Box = TNPauseUI::Sized(Tree, TNPauseUI::Card(Tree, Column, FMargin(46.f, 30.f, 46.f, 52.f)), 700.f, 0.f);
	// El velo, a pantalla completa; el cuadro, en la misma caja que encoge el menú.
	UOverlay* Stage = TNPauseUI::Make<UOverlay>(Tree);
	TNPauseUI::AddO(Stage, Box, HAlign_Center, VAlign_Center);
	UScaleBox* StageFit = TNPauseUI::Fit(Tree, Stage);
	if (IsLocalGame()) { StageFit->SetStretchDirection(EStretchDirection::Both); }
	TNPauseUI::AddO(Layer, StageFit, HAlign_Fill, VAlign_Fill);
	return Layer;
}

UTN_PauseRow* UTN_PauseMenuWidget::NewRow()
{
	UTN_PauseRow* Row = CreateWidget<UTN_PauseRow>(this, UTN_PauseRow::StaticClass());
	if (!Row)
	{
		return nullptr;
	}
	TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
	Row->OnFocused = [WeakThis](UTN_PauseRow* Focused)
	{
		if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->HandleRowFocused(Focused); }
	};
	Row->OnSound = [WeakThis](ETNPauseSound Sound, float Pitch)
	{
		if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->PlayUISound(Sound, Pitch); }
	};
	return Row;
}

UTN_PauseRow* UTN_PauseMenuWidget::AddListRow(UScrollBox* List)
{
	UTN_PauseRow* Row = NewRow();
	if (Row && List)
	{
		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(List->AddChild(Row)))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}
	return Row;
}

void UTN_PauseMenuWidget::AddListHeader(UScrollBox* List, const FText& Title)
{
	UTextBlock* Header = TNPauseUI::Label(WidgetTree, Title, TEXT("Black"), 18, TNHUDArt::Gold);
	if (UScrollBoxSlot* HeaderSlot = Cast<UScrollBoxSlot>(List->AddChild(Header)))
	{
		HeaderSlot->SetPadding(FMargin(8.f, List->GetChildrenCount() > 1 ? 14.f : 0.f, 0.f, 6.f));
	}
}

void UTN_PauseMenuWidget::AddListNote(UScrollBox* List, const FText& Note)
{
	UTextBlock* Text = TNPauseUI::Label(WidgetTree, Note, TEXT("Regular"), 17, TNHUDStyle::TextDim);
	Text->SetAutoWrapText(true);
	if (UScrollBoxSlot* NoteSlot = Cast<UScrollBoxSlot>(List->AddChild(Text)))
	{
		NoteSlot->SetPadding(FMargin(10.f, 2.f, 10.f, 8.f));
	}
}

UTN_PauseRow* UTN_PauseMenuWidget::AddVolumeRow(const FText& Label, const FText& Description, float Value, TFunction<void(float)> OnChanged, float MaxValue)
{
	UTN_PauseRow* Row = AddListRow(SettingsList);
	if (Row)
	{
		Row->SetupSlider(Label, 0.f, MaxValue, 0.05f, Value, [](float V) { return TNPauseUI::Percent(V); }, MoveTemp(OnChanged));
		Row->SetDescription(Description);
	}
	return Row;
}

UTN_PauseRow* UTN_PauseMenuWidget::AddToggleRow(const FText& Label, const FText& Description, bool bValue, TFunction<void(bool)> OnChanged)
{
	UTN_PauseRow* Row = AddListRow(SettingsList);
	if (Row)
	{
		const TArray<FText> Options = { NSLOCTEXT("TNPause", "Off", "No"), NSLOCTEXT("TNPause", "On", "Sí") };
		Row->SetupChoice(Label, Options, bValue ? 1 : 0, [Callback = MoveTemp(OnChanged)](int32 Choice) { if (Callback) { Callback(Choice == 1); } });
		Row->SetDescription(Description);
	}
	return Row;
}

UTN_PauseRow* UTN_PauseMenuWidget::AddQualityRow(const FText& Label, const FText& Description, int32 Value, TFunction<void(int32)> OnChanged)
{
	UTN_PauseRow* Row = AddListRow(SettingsList);
	if (Row)
	{
		Row->SetupChoice(Label, TNPauseUI::QualityNames(), Value >= 0 && Value <= 3 ? Value : INDEX_NONE, MoveTemp(OnChanged));
		Row->SetChoiceIndex(Value >= 0 && Value <= 3 ? Value : INDEX_NONE, TNPauseUI::QualityOverride(Value));
		Row->SetDescription(Description);
	}
	return Row;
}

UTN_PauseRow* UTN_PauseMenuWidget::AddKeyBindRow(UScrollBox* List, const FString& Id, const FText& Description)
{
	const UTN_GameSettingsSubsystem* Settings = GetSettings();
	if (!Settings || !List)
	{
		return nullptr;
	}
	const TArray<FTNKeyBinding> Bindings = Settings->GetKeyBindings();
	const FTNKeyBinding* Binding = Bindings.FindByPredicate([&Id](const FTNKeyBinding& Candidate) { return Candidate.Id == Id; });
	UTN_PauseRow* Row = Binding ? AddListRow(List) : nullptr;
	if (!Row)
	{
		return nullptr;
	}
	TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
	TWeakObjectPtr<UTN_PauseRow> WeakRow(Row);
	Row->SetupKeyBind(Binding->Label, Id,
		[WeakThis, WeakRow, Id]() { if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->StartKeyCapture(WeakRow.Get(), Id); } },
		[WeakThis, Id]() { if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->ResetKeyRow(Id); } });
	Row->SetKeyTexts(TNPauseUI::BindingText(*Binding, 0), TNPauseUI::BindingText(*Binding, 1), Binding->bEditable[0], Binding->bEditable[1]);
	Row->SetDescription(Description);
	return Row;
}

// ─────────────────────────────────────────────────────────────────────────────
// Menú: cabecera (mapa, sesión y jugadores)
// ─────────────────────────────────────────────────────────────────────────────

void UTN_PauseMenuWidget::RefreshHeader()
{
	const UWorld* World = GetWorld();
	if (!World || !ModeText || !SessionText)
	{
		return;
	}
	const AGameStateBase* State = World->GetGameState();
	const UClass* ModeClass = State ? State->GameModeClass.Get() : nullptr;
	const FString Map = UWorld::RemovePIEPrefix(World->GetMapName());

	// Mapa o modo.
	FText Mode = FText::FromString(Map);
	if (ModeClass && ModeClass->IsChildOf(ATN_HQGameMode::StaticClass()))
	{
		Mode = Map.Contains(TEXT("HQ")) ? NSLOCTEXT("TNPause", "ModeHQ", "Lobby · el cuartel") : NSLOCTEXT("TNPause", "ModeLobby", "Lobby · el castillo de arena");
	}
	else if (ModeClass && ModeClass->IsChildOf(ATN_TerrainViewGameMode::StaticClass()))
	{
		Mode = NSLOCTEXT("TNPause", "ModeTerrain", "Solo terreno · paseo por el mapa procedural");
	}
	else if (const ATN_KartGameState* Karts = Cast<ATN_KartGameState>(State))
	{
		static const FText KartDifficulties[] = { NSLOCTEXT("TNPause", "DiffEasy", "fácil"), NSLOCTEXT("TNPause", "DiffNormal", "normal"),
			NSLOCTEXT("TNPause", "DiffHard", "difícil") };
		const int32 Difficulty = FMath::Clamp(static_cast<int32>(Karts->Difficulty), 0, 2);
		Mode = FText::Format(NSLOCTEXT("TNPause", "ModeKarts", "Karts · el camino del cooperativo · {0} · semilla {1}"),
			KartDifficulties[Difficulty], FText::AsNumber(Karts->MapSeed, &FNumberFormattingOptions::DefaultNoGrouping()));
	}
	else if (const ATN_RallyGameState* Rally = Cast<ATN_RallyGameState>(State))
	{
		Mode = FText::Format(NSLOCTEXT("TNPause", "ModeRallyCircuit", "Rally · {0}"), TNLobbyMission::RallyMapName(Rally->Variant));
	}
	else if (const ATN_ProcMapGameState* Proc = Cast<ATN_ProcMapGameState>(State))
	{
		const FText Round = FText::AsNumber(FMath::Max(1, Proc->CurrentRound));
		const FText Target = FText::AsNumber(Proc->RoundTarget);
		const bool bBeach = Map.Contains(TEXT("BeachRace"));
		switch (Proc->ProcMode)
		{
		case ETNProcGameMode::Race:
			Mode = FText::Format(bBeach ? NSLOCTEXT("TNPause", "ModeBeach", "Carrera en la playa · ronda {0} · gana quien llegue a {1} conchas")
				: NSLOCTEXT("TNPause", "ModeRace", "Carrera · ronda {0} · gana quien llegue a {1} victorias"), Round, Target);
			break;
		case ETNProcGameMode::TwoVsTwo:
			Mode = FText::Format(NSLOCTEXT("TNPause", "Mode2v2", "2 contra 2 · ronda {0} · gana la pareja que llegue a {1}"), Round, Target);
			break;
		case ETNProcGameMode::FreeForAll:
		{
			// La arena (replicada en ATN_TctArena); hasta que llega, sin ella.
			const ATN_TctArena* Arena = ATN_TctArena::Find(World);
			const FName ArenaVariant = Arena ? Arena->GetArenaVariant() : NAME_None;
			Mode = ArenaVariant.IsNone()
				? FText::Format(NSLOCTEXT("TNPause", "ModeFreeForAll", "Todos contra Todos · ronda {0} · gana quien llegue a {1} rondas"), Round, Target)
				: FText::Format(NSLOCTEXT("TNPause", "ModeFreeForAllArena", "Todos contra Todos · {2} · ronda {0} · gana quien llegue a {1} rondas"),
					Round, Target, TNLobbyMission::TctArenaName(ArenaVariant));
			break;
		}
		default:
		{
			static const FText Difficulties[] = { NSLOCTEXT("TNPause", "DiffEasy", "fácil"), NSLOCTEXT("TNPause", "DiffNormal", "normal"),
				NSLOCTEXT("TNPause", "DiffHard", "difícil") };
			const int32 Difficulty = FMath::Clamp(static_cast<int32>(Proc->ProcDifficulty), 0, 2);
			Mode = FText::Format(NSLOCTEXT("TNPause", "ModeCoop", "Cooperativo · ronda {0} de {1} · {2} · semilla {3}"), Round, Target,
				Difficulties[Difficulty], FText::AsNumber(Proc->MapSeed, &FNumberFormattingOptions::DefaultNoGrouping()));
			break;
		}
		}
	}
	else if (Map.Contains(TEXT("LVL_Run")))
	{
		Mode = ModeClass && ModeClass->IsChildOf(ATN_SurvivalGameMode::StaticClass())
			? NSLOCTEXT("TNPause", "ModeSurvival", "Supervivencia · nivel tras nivel hasta que quede una")
			: NSLOCTEXT("TNPause", "ModeClassic", "Carrera clásica");
	}
	ModeText->SetText(Mode);

	// Sesión: la sala (nombre, código si es privada, «3/4» y si está cerrada) y su anfitrión; sin sala, la sesión como
	// antes; sin sesión (editor, partida local), quién eres en la partida.
	const int32 Players = State ? State->PlayerArray.Num() : 1;
	FText Session;
	const IOnlineSessionPtr Sessions = TNPauseUI::SessionInterface();
	const FNamedOnlineSession* Named = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	const UMP_GameInstance* RoomGameInstance = Cast<UMP_GameInstance>(GetGameInstance());
	FTNRoomSnapshot Room;
	if (RoomGameInstance && RoomGameInstance->GetRoomSnapshot(Room))
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Room"), TNRoomNames::Get(Room.NameId));
		Args.Add(TEXT("Host"), FText::FromString(Room.HostName.IsEmpty() ? FString(TEXT("?")) : Room.HostName));
		Args.Add(TEXT("Kind"), Room.bPrivate
			? FText::Format(NSLOCTEXT("TNPause", "RoomPrivateCode", "privada · código {0}"), FText::FromString(Room.Code))
			: NSLOCTEXT("TNPause", "RoomPublic", "pública"));
		Args.Add(TEXT("Players"), FText::AsNumber(Room.Players));
		Args.Add(TEXT("Max"), FText::AsNumber(FMath::Max(Room.Players, Room.MaxPlayers)));
		Args.Add(TEXT("Locked"), Room.bLocked ? NSLOCTEXT("TNPause", "RoomLockedTag", " · cerrada") : FText::GetEmpty());
		Session = FText::Format(NSLOCTEXT("TNPause", "RoomSessionFmt", "«{Room}» de {Host} · {Kind} · {Players}/{Max} tortugas{Locked}"), Args);
	}
	else if (Named)
	{
		FString Code = Named->SessionInfo.IsValid() ? Named->SessionInfo->GetSessionId().ToString() : FString();
		Code = Code.Right(6).ToUpper();
		const FText Host = FText::FromString(Named->OwningUserName.IsEmpty() ? FString(TEXT("?")) : Named->OwningUserName);
		Session = Code.IsEmpty()
			? FText::Format(NSLOCTEXT("TNPause", "SessionNoCode", "Partida de {0} · {1} de {2} tortugas"), Host, FText::AsNumber(Players),
				FText::AsNumber(FMath::Max(Players, Named->SessionSettings.NumPublicConnections)))
			: FText::Format(NSLOCTEXT("TNPause", "SessionFmt", "Partida de {0} · sala {1} · {2} de {3} tortugas"), Host, FText::FromString(Code),
				FText::AsNumber(Players), FText::AsNumber(FMath::Max(Players, Named->SessionSettings.NumPublicConnections)));
	}
	else
	{
		const ENetMode NetMode = World->GetNetMode();
		const FText Role = NetMode == NM_Client ? NSLOCTEXT("TNPause", "RoleGuest", "Estás de invitado")
			: (NetMode == NM_Standalone ? NSLOCTEXT("TNPause", "RoleSolo", "Partida local") : NSLOCTEXT("TNPause", "RoleHost", "Eres el anfitrión"));
		Session = IsLocalGame()
			? FText::Format(NSLOCTEXT("TNLocal", "PauseSession", "Partida local · {0} tortugas · menú del jugador {1}: solo lo maneja él"),
				FText::AsNumber(Players), FText::AsNumber(UTN_LocalPlaySubsystem::GetPlayerNumber(GetOwningPlayer())))
			: FText::Format(NSLOCTEXT("TNPause", "SessionLocal", "{0} · {1} tortugas conectadas"), Role, FText::AsNumber(Players));
	}
	SessionText->SetText(Session);

	// Jugadores: se rehace la lista si alguien entra o sale.
	TArray<TWeakObjectPtr<APlayerState>> Now;
	if (State)
	{
		for (APlayerState* PS : State->PlayerArray) { if (PS) { Now.Add(PS); } }
	}
	if (Now != ChipPlayers) { RebuildPlayers(); }
}

void UTN_PauseMenuWidget::RebuildPlayers()
{
	if (!PlayersBox)
	{
		return;
	}
	PlayersBox->ClearChildren();
	ChipVoiceIcons.Reset();
	ChipPlayers.Reset();
	const UWorld* World = GetWorld();
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	if (!State)
	{
		return;
	}

	// Anfitrión: el dueño de la sesión o, en el anfitrión, él mismo.
	FUniqueNetIdRepl HostId;
	const IOnlineSessionPtr Sessions = TNPauseUI::SessionInterface();
	if (const FNamedOnlineSession* Named = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr)
	{
		if (Named->OwningUserId.IsValid()) { HostId = FUniqueNetIdRepl(Named->OwningUserId); }
	}
	const APlayerState* Mine = GetOwningPlayerState();
	UWidgetTree* Tree = WidgetTree;
	for (APlayerState* PS : State->PlayerArray)
	{
		if (!PS)
		{
			continue;
		}
		const bool bMe = PS == Mine;
		const bool bHost = (HostId.IsValid() && PS->GetUniqueId() == HostId) || (bMe && World->GetNetMode() == NM_ListenServer);

		UHorizontalBox* Chip = TNPauseUI::Make<UHorizontalBox>(Tree);
		TNPauseUI::AddH(Chip, TNPauseUI::Picture(Tree, TNHUDFaces::TurtleFace(ETNTurtleFace::Happy), FVector2D(44.f, 44.f)), FMargin(0.f, 0.f, 8.f, 0.f));
		UVerticalBox* Names = TNPauseUI::Make<UVerticalBox>(Tree);
		UTextBlock* Name = TNPauseUI::Label(Tree, FText::FromString(PS->GetPlayerName()), TEXT("Bold"), 18, bMe ? TNHUDArt::Gold : TNHUDArt::Cream);
		TNPauseUI::AddV(Names, TNPauseUI::Sized(Tree, Name, 0.f, 0.f), FMargin(0.f), HAlign_Left);
		const bool bLocalGame = IsLocalGame();
		// En la partida local los demás juegan en este PC: sin ping.
		const FText Sub = (!bMe && bLocalGame) ? NSLOCTEXT("TNLocal", "ChipLocal", "En este PC")
			: TNPauseUI::PlayerSub(PS, bMe, bHost, World->GetNetMode() == NM_Client);
		TNPauseUI::AddV(Names, TNPauseUI::Label(Tree, Sub, TEXT("Regular"), 14, TNHUDArt::SeaLight), FMargin(0.f), HAlign_Left);
		TNPauseUI::AddH(Chip, Names, FMargin(0.f, 0.f, 8.f, 0.f));
		if (bHost) { TNPauseUI::AddH(Chip, TNPauseUI::Picture(Tree, TNPauseArt::HostCrown(), FVector2D(26.f, 26.f)), FMargin(0.f, 0.f, 6.f, 0.f)); }
		UImage* Voice = TNPauseUI::Picture(Tree, bMe ? TNPauseArt::MicIcon(false) : TNPauseArt::SpeakerIcon(false), FVector2D(32.f, 32.f));
		Voice->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		// Partida local: sin chat de voz.
		if (bLocalGame) { Voice->SetVisibility(ESlateVisibility::Collapsed); }
		TNPauseUI::AddH(Chip, Voice);

		UBorder* Frame = TNPauseUI::Make<UBorder>(Tree);
		TNHUDStyle::StylePanel(Frame, TNHUDStyle::Panel, 14.f, FMargin(8.f, 5.f, 12.f, 5.f), bMe ? TNHUDArt::Hex(0xFFCB3D, 0.6f) : TNHUDStyle::Edge);
		Frame->SetContent(Chip);
		TNPauseUI::AddH(PlayersBox, Frame, FMargin(6.f, 0.f));
		ChipVoiceIcons.Add(Voice);
		ChipPlayers.Add(PS);
	}
}

void UTN_PauseMenuWidget::UpdateVoiceIcons()
{
	const UTN_GameSettingsSubsystem* Settings = GetSettings();
	const APlayerState* Mine = GetOwningPlayerState();
	for (int32 i = 0; i < ChipPlayers.Num() && i < ChipVoiceIcons.Num(); ++i)
	{
		UImage* Icon = ChipVoiceIcons[i];
		const APlayerState* PS = ChipPlayers[i].Get();
		if (!Icon || !PS)
		{
			continue;
		}
		const bool bMe = PS == Mine;
		const APawn* Pawn = PS->GetPawn();
		const UProximityVoiceComponent* Voice = Pawn ? Pawn->FindComponentByClass<UProximityVoiceComponent>() : nullptr;
		const bool bMuted = Settings && (bMe ? Settings->GetEditedSettings().bMicMuted : Settings->IsPlayerMuted(UTN_GameSettingsSubsystem::PlayerKey(PS)));
		const bool bSpeaking = !bMuted && Voice && Voice->IsHeardSpeaking();
		TNPauseUI::SetPicture(Icon, bMe ? TNPauseArt::MicIcon(bMuted) : TNPauseArt::SpeakerIcon(bMuted));
		// Hablando: late y brilla; callado: apagado.
		const float Pulse = bSpeaking ? 1.f + 0.12f * FMath::Abs(FMath::Sin(Clock * 9.f)) : 1.f;
		Icon->SetRenderScale(FVector2D(Pulse, Pulse));
		Icon->SetRenderOpacity(bSpeaking || bMuted ? 1.f : 0.45f);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Menú: páginas y pestañas
// ─────────────────────────────────────────────────────────────────────────────

void UTN_PauseMenuWidget::ShowPage(ETNPausePage NewPage)
{
	CancelKeyCapture(true);
	const ETNPausePage Previous = Page;
	Page = NewPage;
	if (Pages) { Pages->SetActiveWidgetIndex(static_cast<int32>(NewPage)); }
	if (NewPage == ETNPausePage::Settings)
	{
		ShowTab(Tab);
	}
	else if (NewPage == ETNPausePage::Controls)
	{
		FillControlsList();
		FocusFirstOfPage();
	}
	else if (NewPage == ETNPausePage::Room)
	{
		RoomListSignature.Reset();
		FillRoomList();
		FocusFirstOfPage();
	}
	else if (NewPage == ETNPausePage::Credits)
	{
		// Sin filas enfocables: el foco va a la lista y la ayuda de abajo se vacía.
		HandleRowFocused(nullptr);
		FocusFirstOfPage();
	}
	else
	{
		// Al volver a la portada, el foco en el botón por el que se salió.
		UTN_PauseRow* Target = nullptr;
		if (Previous == ETNPausePage::Settings && HomeRows.IsValidIndex(1)) { Target = HomeRows[1]; }
		else if (Previous == ETNPausePage::Controls && HomeRows.IsValidIndex(2)) { Target = HomeRows[2]; }
		else if (Previous == ETNPausePage::Room) { Target = RoomHomeRow; }
		else if (Previous == ETNPausePage::Credits && HomeRows.IsValidIndex(3)) { Target = HomeRows[3]; }
		if (Target) { FocusRow(Target); }
		else { FocusFirstOfPage(); }
	}
	RefreshHint();
}

void UTN_PauseMenuWidget::ShowTab(ETNPauseTab NewTab, bool bFocusList)
{
	if (!IsTabAvailable(NewTab))
	{
		NewTab = StepTab(1);
	}
	Tab = NewTab;
	for (int32 i = 0; i < TabRows.Num(); ++i)
	{
		if (TabRows[i]) { TabRows[i]->SetActive(i == static_cast<int32>(NewTab)); }
	}
	FillTab();
	UTN_PauseRow* TabRow = TabRows.IsValidIndex(static_cast<int32>(NewTab)) ? TabRows[static_cast<int32>(NewTab)].Get() : nullptr;
	if (!bFocusList && TabRow) { FocusRow(TabRow); }
	else { FocusFirstOfPage(); }
}

void UTN_PauseMenuWidget::FillTab()
{
	if (!SettingsList)
	{
		return;
	}
	CancelKeyCapture(true);
	SettingsList->ClearChildren();
	// Partida local: qué es de cada jugador y qué vale para todos.
	if (IsGuestMenu() && (Tab == ETNPauseTab::Controls || Tab == ETNPauseTab::Game))
	{
		AddListNote(SettingsList, FText::Format(NSLOCTEXT("TNLocal", "GuestSettingsNote",
			"Jugador {0}: estos ajustes son solo tuyos y duran esta partida. El sonido, la imagen y el idioma los elige el jugador 1."),
			FText::AsNumber(UTN_LocalPlaySubsystem::GetPlayerNumber(GetOwningPlayer()))));
	}
	else if (IsLocalGame() && (Tab == ETNPauseTab::Controls || Tab == ETNPauseTab::Game))
	{
		AddListNote(SettingsList, NSLOCTEXT("TNLocal", "PrimarySettingsNote",
			"La cámara, los controles, el temblor y el campo de visión son solo tuyos; lo demás vale para todos los jugadores de este PC."));
	}
	OverallRow = nullptr;
	ResolutionRow = nullptr;
	ResScaleRow = nullptr;
	QualityRows.Reset();
	VoiceTabPlayers.Reset();
	switch (Tab)
	{
	case ETNPauseTab::Graphics: FillGraphicsTab(); break;
	case ETNPauseTab::Sound:    FillSoundTab(); break;
	case ETNPauseTab::Voice:    FillVoiceTab(); break;
	case ETNPauseTab::Controls: FillControlsTab(); break;
	default:                    FillGameTab(); break;
	}
	SettingsList->ScrollToStart();
}

void UTN_PauseMenuWidget::FillGraphicsTab()
{
	UGameUserSettings* GUS = UGameUserSettings::GetGameUserSettings();
	UTN_GameSettingsSubsystem* Settings = GetSettings();
	if (!GUS || !Settings)
	{
		return;
	}
	TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
	TWeakObjectPtr<UTN_GameSettingsSubsystem> WeakSettings(Settings);
	const bool bCanVideo = UTN_GameSettingsSubsystem::CanChangeVideoMode();

	AddListHeader(SettingsList, NSLOCTEXT("TNPause", "HeadScreen", "PANTALLA"));
	if (!bCanVideo)
	{
		AddListNote(SettingsList, NSLOCTEXT("TNPause", "EditorVideoNote", "En el editor la ventana es la del editor: el modo de ventana y la resolución solo cambian en el juego."));
	}
	{
		static const EWindowMode::Type Modes[] = { EWindowMode::Fullscreen, EWindowMode::WindowedFullscreen, EWindowMode::Windowed };
		const TArray<FText> Names = { NSLOCTEXT("TNPause", "WinFull", "Pantalla completa"), NSLOCTEXT("TNPause", "WinBorderless", "Ventana sin bordes"),
			NSLOCTEXT("TNPause", "WinWindowed", "Ventana") };
		int32 Current = 1;
		for (int32 i = 0; i < 3; ++i) { if (Modes[i] == GUS->GetFullscreenMode()) { Current = i; } }
		if (UTN_PauseRow* Row = AddListRow(SettingsList))
		{
			Row->SetupChoice(NSLOCTEXT("TNPause", "WindowMode", "Modo de ventana"), Names, Current, [WeakThis](int32 Choice)
			{
				UGameUserSettings* UserSettings = UGameUserSettings::GetGameUserSettings();
				UTN_PauseMenuWidget* Menu = WeakThis.Get();
				if (!UserSettings || !Menu) { return; }
				UserSettings->SetFullscreenMode(Modes[FMath::Clamp(Choice, 0, 2)]);
				// Sin bordes va siempre a la resolución del escritorio.
				if (Modes[FMath::Clamp(Choice, 0, 2)] == EWindowMode::WindowedFullscreen) { UserSettings->SetScreenResolution(UserSettings->GetDesktopResolution()); }
				Menu->RefreshGraphicsRows();
				Menu->OnVideoModeChanged();
			});
			Row->SetDescription(NSLOCTEXT("TNPause", "WindowModeDesc", "Pantalla completa va más fina; sin bordes deja cambiar de programa al momento."));
			Row->SetRowEnabled(bCanVideo);
		}
	}
	if (UTN_PauseRow* Row = AddListRow(SettingsList))
	{
		ResolutionRow = Row;
		Row->SetupChoice(NSLOCTEXT("TNPause", "Resolution", "Resolución"), {}, 0, [WeakThis](int32 Choice)
		{
			UGameUserSettings* UserSettings = UGameUserSettings::GetGameUserSettings();
			UTN_PauseMenuWidget* Menu = WeakThis.Get();
			if (!UserSettings || !Menu || !Menu->ResolutionChoices.IsValidIndex(Choice)) { return; }
			UserSettings->SetScreenResolution(Menu->ResolutionChoices[Choice]);
			Menu->OnVideoModeChanged();
		});
		Row->SetDescription(NSLOCTEXT("TNPause", "ResolutionDesc", "Las que admite tu pantalla. Si no ves bien la nueva, se deshace sola en unos segundos."));
	}
	{
		float Normalized = 1.f, Current = 100.f, MinScale = 50.f, MaxScale = 100.f;
		GUS->GetResolutionScaleInformationEx(Normalized, Current, MinScale, MaxScale);
		if (UTN_PauseRow* Row = AddListRow(SettingsList))
		{
			ResScaleRow = Row;
			Row->SetupSlider(NSLOCTEXT("TNPause", "ResScale", "Escala de resolución"), MinScale, MaxScale, 5.f, Current,
				[](float V) { return FText::Format(NSLOCTEXT("TNPause", "ResScaleFmt", "{0} %"), FText::AsNumber(FMath::RoundToInt(V))); },
				[WeakSettings](float V)
				{
					UGameUserSettings* UserSettings = UGameUserSettings::GetGameUserSettings();
					if (UserSettings) { UserSettings->SetResolutionScaleValueEx(V); }
					if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->ApplyGraphicsChange(); }
				});
			Row->SetDescription(NSLOCTEXT("TNPause", "ResScaleDesc", "Pinta el juego a menos resolución y lo estira: más fotogramas a cambio de nitidez."));
		}
	}
	AddToggleRow(NSLOCTEXT("TNPause", "VSync", "Sincronización vertical"),
		NSLOCTEXT("TNPause", "VSyncDesc", "Quita los cortes de la imagen a cambio de un poco de retraso en los controles."),
		GUS->IsVSyncEnabled(), [WeakSettings](bool bOn)
		{
			if (UGameUserSettings* UserSettings = UGameUserSettings::GetGameUserSettings()) { UserSettings->SetVSyncEnabled(bOn); }
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->ApplyGraphicsChange(); }
		});
	{
		const TArray<float>& Limits = TNPauseUI::FrameLimits();
		TArray<FText> Names;
		int32 Current = Limits.Num() - 1;
		for (int32 i = 0; i < Limits.Num(); ++i)
		{
			Names.Add(Limits[i] > 0.f ? FText::Format(NSLOCTEXT("TNPause", "FpsLimitFmt", "{0} FPS"), FText::AsNumber(FMath::RoundToInt(Limits[i])))
				: NSLOCTEXT("TNPause", "NoLimit", "Sin límite"));
			if (Limits[i] > 0.f && FMath::IsNearlyEqual(Limits[i], GUS->GetFrameRateLimit(), 0.5f)) { Current = i; }
		}
		if (UTN_PauseRow* Row = AddListRow(SettingsList))
		{
			Row->SetupChoice(NSLOCTEXT("TNPause", "FpsLimit", "Límite de fotogramas"), Names, Current, [WeakSettings](int32 Choice)
			{
				const TArray<float>& Values = TNPauseUI::FrameLimits();
				if (UGameUserSettings* UserSettings = UGameUserSettings::GetGameUserSettings()) { UserSettings->SetFrameRateLimit(Values[FMath::Clamp(Choice, 0, Values.Num() - 1)]); }
				if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->ApplyGraphicsChange(); }
			});
			Row->SetDescription(NSLOCTEXT("TNPause", "FpsLimitDesc", "Tope de fotogramas por segundo: ahorra batería y calor sin que se note."));
		}
	}
	if (UTN_PauseRow* Row = AddListRow(SettingsList))
	{
		Row->SetupSlider(NSLOCTEXT("TNPause", "Brightness", "Brillo"), 0.f, 1.f, 0.05f, Settings->GetEditedSettings().Brightness,
			[](float V) { return TNPauseUI::Percent(V); },
			[WeakSettings](float V) { if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([V](FTNGameSettings& Data) { Data.Brightness = V; }); } });
		Row->SetDescription(NSLOCTEXT("TNPause", "BrightnessDesc", "Gamma de la imagen. 50 % es el brillo de siempre."));
	}
	AddToggleRow(NSLOCTEXT("TNPause", "ShowFps", "Mostrar FPS"),
		NSLOCTEXT("TNPause", "ShowFpsDesc", "Contador de fotogramas por segundo abajo a la derecha (y el peor fotograma, en milisegundos)."),
		Settings->GetEditedSettings().bShowFps, [WeakSettings](bool bOn)
		{
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([bOn](FTNGameSettings& Data) { Data.bShowFps = bOn; }); }
		});

	AddListHeader(SettingsList, NSLOCTEXT("TNPause", "HeadQuality", "CALIDAD"));
	OverallRow = AddQualityRow(NSLOCTEXT("TNPause", "Overall", "Calidad general"),
		NSLOCTEXT("TNPause", "OverallDesc", "Pone todas las partes de abajo a la vez. Si luego cambias una, aquí sale «Personalizada»."),
		GUS->GetOverallScalabilityLevel(), [WeakThis, WeakSettings](int32 Choice)
		{
			if (UGameUserSettings* UserSettings = UGameUserSettings::GetGameUserSettings()) { UserSettings->SetOverallScalabilityLevel(Choice); }
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->ApplyGraphicsChange(); }
			if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->RefreshGraphicsRows(); }
		});
	const TArray<TNPauseUI::FQualityPart>& Parts = TNPauseUI::QualityParts();
	for (int32 PartIndex = 0; PartIndex < Parts.Num(); ++PartIndex)
	{
		const TNPauseUI::FQualityPart& Part = Parts[PartIndex];
		QualityRows.Add(AddQualityRow(Part.Label, Part.Description, (GUS->*Part.Get)(), [WeakThis, WeakSettings, PartIndex](int32 Choice)
		{
			if (UGameUserSettings* UserSettings = UGameUserSettings::GetGameUserSettings())
			{
				(UserSettings->*(TNPauseUI::QualityParts()[PartIndex].Set))(Choice);
			}
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->ApplyGraphicsChange(); }
			if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->RefreshGraphicsRows(); }
		}));
	}
	if (UTN_PauseRow* Row = AddListRow(SettingsList))
	{
		Row->SetupButton(ETNPauseRowStyle::List, NSLOCTEXT("TNPause", "AutoQuality", "Calidad recomendada"), [WeakThis, WeakSettings]()
		{
			if (UGameUserSettings* UserSettings = UGameUserSettings::GetGameUserSettings())
			{
				// Prueba rápida del procesador y la tarjeta gráfica (un tirón de un par de segundos) y aplica lo que aguanta.
				UserSettings->RunHardwareBenchmark();
				UserSettings->ApplyHardwareBenchmarkResults();
			}
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->ApplyGraphicsChange(); }
			if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->RefreshGraphicsRows(); }
		}, nullptr, NSLOCTEXT("TNPause", "AutoQualityAction", "Probar el equipo"));
		Row->SetDescription(NSLOCTEXT("TNPause", "AutoQualityDesc", "Mide tu equipo (la imagen se congela un momento) y elige la calidad que aguanta bien."));
	}
	RefreshGraphicsRows();
}

void UTN_PauseMenuWidget::RefreshGraphicsRows()
{
	UGameUserSettings* GUS = UGameUserSettings::GetGameUserSettings();
	if (!GUS)
	{
		return;
	}
	if (UTN_PauseRow* Row = OverallRow.Get())
	{
		const int32 Level = GUS->GetOverallScalabilityLevel();
		Row->SetChoiceIndex(Level >= 0 && Level <= 3 ? Level : INDEX_NONE, TNPauseUI::QualityOverride(Level));
	}
	const TArray<TNPauseUI::FQualityPart>& Parts = TNPauseUI::QualityParts();
	for (int32 i = 0; i < QualityRows.Num() && i < Parts.Num(); ++i)
	{
		if (UTN_PauseRow* Row = QualityRows[i].Get())
		{
			const int32 Level = (GUS->*Parts[i].Get)();
			Row->SetChoiceIndex(Level >= 0 && Level <= 3 ? Level : INDEX_NONE, TNPauseUI::QualityOverride(Level));
		}
	}
	if (UTN_PauseRow* Row = ResScaleRow.Get())
	{
		float Normalized = 1.f, Current = 100.f, MinScale = 50.f, MaxScale = 100.f;
		GUS->GetResolutionScaleInformationEx(Normalized, Current, MinScale, MaxScale);
		Row->SetSliderValue(Current);
	}
	if (UTN_PauseRow* Row = ResolutionRow.Get())
	{
		// Las de pantalla completa o las cómodas para ventana; sin bordes, la del escritorio.
		const EWindowMode::Type Mode = GUS->GetFullscreenMode();
		ResolutionChoices.Reset();
		if (Mode == EWindowMode::WindowedFullscreen)
		{
			ResolutionChoices.Add(GUS->GetDesktopResolution());
		}
		else if (Mode == EWindowMode::Fullscreen)
		{
			UKismetSystemLibrary::GetSupportedFullscreenResolutions(ResolutionChoices);
		}
		else
		{
			UKismetSystemLibrary::GetConvenientWindowedResolutions(ResolutionChoices);
		}
		const FIntPoint Current = GUS->GetScreenResolution();
		ResolutionChoices.AddUnique(Current);
		ResolutionChoices.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X * A.Y < B.X * B.Y || (A.X * A.Y == B.X * B.Y && A.X < B.X); });
		TArray<FText> Names;
		for (const FIntPoint& Size : ResolutionChoices)
		{
			Names.Add(FText::Format(NSLOCTEXT("TNPause", "ResFmt", "{0} × {1}"), FText::AsNumber(Size.X, &FNumberFormattingOptions::DefaultNoGrouping()),
				FText::AsNumber(Size.Y, &FNumberFormattingOptions::DefaultNoGrouping())));
		}
		Row->SetChoiceOptions(Names, ResolutionChoices.IndexOfByKey(Current));
		Row->SetRowEnabled(UTN_GameSettingsSubsystem::CanChangeVideoMode() && Mode != EWindowMode::WindowedFullscreen);
	}
}

void UTN_PauseMenuWidget::FillSoundTab()
{
	UTN_GameSettingsSubsystem* Settings = GetSettings();
	if (!Settings)
	{
		return;
	}
	TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
	TWeakObjectPtr<UTN_GameSettingsSubsystem> WeakSettings(Settings);
	const FTNGameSettings& Data = Settings->GetEditedSettings();
	auto Edit = [WeakSettings](TFunction<void(FTNGameSettings&, float)> Apply)
	{
		return [WeakSettings, Apply](float V)
		{
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([&Apply, V](FTNGameSettings& D) { Apply(D, V); }); }
		};
	};

	AddListHeader(SettingsList, NSLOCTEXT("TNPause", "HeadVolume", "VOLUMEN"));
	AddVolumeRow(NSLOCTEXT("TNPause", "Master", "General"), NSLOCTEXT("TNPause", "MasterDesc", "Todo lo que suena en el juego, voces incluidas."),
		Data.MasterVolume, Edit([](FTNGameSettings& D, float V) { D.MasterVolume = V; }));
	AddVolumeRow(NSLOCTEXT("TNPause", "Music", "Música"), NSLOCTEXT("TNPause", "MusicDesc", "La música de la tienda, del probador y de fin de partida."),
		Data.MusicVolume, Edit([](FTNGameSettings& D, float V) { D.MusicVolume = V; }));
	AddVolumeRow(NSLOCTEXT("TNPause", "Effects", "Efectos"),
		NSLOCTEXT("TNPause", "EffectsDesc", "Pasos, saltos, trampas, enemigos, conchas, bailes y los sonidos de los menús."),
		Data.EffectsVolume, Edit([](FTNGameSettings& D, float V) { D.EffectsVolume = V; }));
	AddVolumeRow(NSLOCTEXT("TNPause", "Ambient", "Ambiente"), NSLOCTEXT("TNPause", "AmbientDesc", "Olas, viento, selva, cascadas y el resto del paisaje sonoro."),
		Data.AmbientVolume, Edit([](FTNGameSettings& D, float V) { D.AmbientVolume = V; }));
	AddToggleRow(NSLOCTEXT("TNPause", "MuteBackground", "Silenciar sin el foco de la ventana"),
		NSLOCTEXT("TNPause", "MuteBackgroundDesc", "Si cambias a otro programa, el juego se calla (voces incluidas) hasta que vuelvas."),
		Data.bMuteInBackground, [WeakSettings](bool bOn)
		{
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([bOn](FTNGameSettings& D) { D.bMuteInBackground = bOn; }); }
		});
	AddListNote(SettingsList, NSLOCTEXT("TNPause", "VoiceElsewhere", "La voz de los compañeros y el micrófono están en la pestaña VOZ."));
	if (UTN_PauseRow* Row = AddListRow(SettingsList))
	{
		Row->SetupButton(ETNPauseRowStyle::List, NSLOCTEXT("TNPause", "ResetSound", "Restablecer el sonido"), [WeakThis, WeakSettings]()
		{
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->ResetGroup(ETNSettingsGroup::Sound); }
			if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->ShowTab(ETNPauseTab::Sound); }
		}, nullptr, NSLOCTEXT("TNPause", "ResetAction", "Restablecer"));
		Row->SetDescription(NSLOCTEXT("TNPause", "ResetSoundDesc", "Todos los volúmenes al 100 % y el juego suena también sin el foco."));
	}
}

void UTN_PauseMenuWidget::FillVoiceTab()
{
	UTN_GameSettingsSubsystem* Settings = GetSettings();
	if (!Settings)
	{
		return;
	}
	TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
	TWeakObjectPtr<UTN_GameSettingsSubsystem> WeakSettings(Settings);
	const FTNGameSettings& Data = Settings->GetEditedSettings();

	AddListHeader(SettingsList, NSLOCTEXT("TNPause", "HeadMates", "COMPAÑEROS"));
	AddVolumeRow(NSLOCTEXT("TNPause", "MatesVoice", "Voz de los compañeros"), NSLOCTEXT("TNPause", "MatesVoiceDesc", "Volumen de la voz de todos los demás jugadores."),
		Data.VoiceVolume, [WeakSettings](float V)
		{
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([V](FTNGameSettings& D) { D.VoiceVolume = V; }); }
		});
	const UWorld* World = GetWorld();
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	const APlayerState* Mine = GetOwningPlayerState();
	if (State)
	{
		for (APlayerState* PS : State->PlayerArray)
		{
			if (!PS || PS == Mine)
			{
				continue;
			}
			VoiceTabPlayers.Add(PS);
			const FString Key = UTN_GameSettingsSubsystem::PlayerKey(PS);
			const FText Name = FText::FromString(PS->GetPlayerName());
			AddVolumeRow(FText::Format(NSLOCTEXT("TNPause", "PlayerVoice", "Voz de {0}"), Name),
				NSLOCTEXT("TNPause", "PlayerVoiceDesc", "Solo cambia cómo lo oyes tú. Hasta el 200 % si habla muy bajito."),
				Settings->GetPlayerVoiceVolume(Key), [WeakSettings, Key](float V)
				{
					if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->SetPlayerVoiceVolume(Key, V); }
				}, 2.f);
			AddToggleRow(FText::Format(NSLOCTEXT("TNPause", "PlayerMute", "Silenciar a {0}"), Name),
				NSLOCTEXT("TNPause", "PlayerMuteDesc", "Dejas de oírle. No se entera nadie."),
				Settings->IsPlayerMuted(Key), [WeakSettings, Key](bool bMuted)
				{
					if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->SetPlayerMuted(Key, bMuted); }
				});
		}
	}
	if (VoiceTabPlayers.Num() == 0)
	{
		AddListNote(SettingsList, NSLOCTEXT("TNPause", "NoMates", "Cuando haya más tortugas en la partida, aquí podrás ajustar la voz de cada una o silenciarla."));
	}

	AddListHeader(SettingsList, NSLOCTEXT("TNPause", "HeadMic", "MICRÓFONO"));
	AddToggleRow(NSLOCTEXT("TNPause", "MuteMe", "Silenciar mi micrófono"), NSLOCTEXT("TNPause", "MuteMeDesc", "No se envía nada de lo que digas."),
		Data.bMicMuted, [WeakSettings](bool bMuted)
		{
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([bMuted](FTNGameSettings& D) { D.bMicMuted = bMuted; }); }
		});
	if (UTN_PauseRow* Row = AddListRow(SettingsList))
	{
		const TArray<FText> Modes = { NSLOCTEXT("TNPause", "OpenMic", "Voz abierta"), NSLOCTEXT("TNPause", "PushToTalk", "Pulsar para hablar") };
		Row->SetupChoice(NSLOCTEXT("TNPause", "MicMode", "Modo"), Modes, Data.bPushToTalk ? 1 : 0, [WeakSettings](int32 Choice)
		{
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([Choice](FTNGameSettings& D) { D.bPushToTalk = Choice == 1; }); }
		});
		Row->SetDescription(NSLOCTEXT("TNPause", "MicModeDesc", "Voz abierta: se te oye al hablar más alto que el umbral. Pulsar para hablar: solo con la tecla pulsada."));
	}
	AddKeyBindRow(SettingsList, TEXT("Talk"), NSLOCTEXT("TNPause", "TalkKeyDesc",
		"Con el modo pulsar para hablar: mantén esta tecla o este botón. Intro o clic: cambiar; Supr, Y o clic derecho: la de serie."));
	{
		// Micrófonos activos de Windows (se enumeran al abrir la pestaña).
		TArray<TPair<FString, FString>> Devices;
		UProximityVoiceComponent::GetCaptureDevices(Devices);
		TArray<FText> Names = { NSLOCTEXT("TNPause", "DefaultMic", "Predeterminado de Windows") };
		TArray<FString> Ids = { FString() };
		for (const TPair<FString, FString>& Device : Devices)
		{
			Names.Add(FText::FromString(Device.Value));
			Ids.Add(Device.Key);
		}
		const int32 Current = Ids.IndexOfByKey(Data.CaptureDeviceId);
		if (UTN_PauseRow* Row = AddListRow(SettingsList))
		{
			Row->SetupChoice(NSLOCTEXT("TNPause", "MicDevice", "Micrófono"), Names, Current == INDEX_NONE ? 0 : Current, [WeakThis, WeakSettings, Ids](int32 Choice)
			{
				UTN_GameSettingsSubsystem* S = WeakSettings.Get();
				if (!S || !Ids.IsValidIndex(Choice)) { return; }
				S->SetCaptureDevice(Ids[Choice]);
				if (UTN_PauseMenuWidget* Menu = WeakThis.Get())
				{
					Menu->ShowNotice(S->IsCaptureDeviceChangePending()
						? NSLOCTEXT("TNPause", "MicLater", "Micrófono elegido: se abre al reaparecer o al cambiar de mapa.")
						: NSLOCTEXT("TNPause", "MicNow", "Ese es el micrófono que ya se está usando."));
				}
			});
			// Uno elegido que ya no está conectado: se usa el predeterminado hasta que vuelva.
			if (Current == INDEX_NONE) { Row->SetChoiceIndex(0, NSLOCTEXT("TNPause", "MicGone", "No conectado: el predeterminado")); }
			Row->SetDescription(NSLOCTEXT("TNPause", "MicDeviceDesc",
				"El que se usa para hablar. Se cambia al reaparecer o al cambiar de mapa: el micrófono abierto no se puede cambiar sin riesgo de colgar el juego."));
		}
	}
	if (UTN_PauseRow* Row = AddListRow(SettingsList))
	{
		Row->SetupSlider(NSLOCTEXT("TNPause", "MicSensitivity", "Sensibilidad"), 0.f, 1.f, 0.05f, Data.MicSensitivity,
			[](float V) { return TNPauseUI::Percent(V); },
			[WeakSettings](float V) { if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([V](FTNGameSettings& D) { D.MicSensitivity = V; }); } });
		Row->SetDescription(NSLOCTEXT("TNPause", "MicSensitivityDesc", "Más alta: se te oye aunque hables bajito. Más baja: no se cuela el ruido de fondo. Mira la raya dorada del medidor."));
	}
	if (UTN_PauseRow* Row = AddListRow(SettingsList))
	{
		Row->SetupSlider(NSLOCTEXT("TNPause", "MicGain", "Ganancia"), 0.25f, 3.f, 0.05f, Data.MicGain,
			[](float V) { return TNPauseUI::Percent(V); },
			[WeakSettings](float V) { if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([V](FTNGameSettings& D) { D.MicGain = V; }); } });
		Row->SetDescription(NSLOCTEXT("TNPause", "MicGainDesc", "Sube o baja tu voz antes de enviarla. 100 % es la de siempre."));
	}
	if (UTN_PauseRow* Row = AddListRow(SettingsList))
	{
		Row->SetupMeter(NSLOCTEXT("TNPause", "MicMeter", "Nivel del micrófono"), [WeakSettings](float& OutLevel, float& OutMark, FText& OutText)
		{
			const UTN_GameSettingsSubsystem* S = WeakSettings.Get();
			if (!S || !S->IsMicCapturing())
			{
				OutLevel = 0.f;
				OutMark = -1.f;
				OutText = NSLOCTEXT("TNPause", "NoMic", "Sin micrófono");
				return;
			}
			const FTNGameSettings& D = S->GetEditedSettings();
			OutLevel = TNPauseUI::MeterFromRms(S->GetMicLevel());
			OutMark = TNPauseUI::MeterFromRms(S->GetSpeakingThreshold());
			if (D.bMicMuted) { OutText = NSLOCTEXT("TNPause", "MicMuted", "Silenciado"); }
			else if (!S->IsTransmitAllowed())
			{
				OutText = FText::Format(NSLOCTEXT("TNPause", "HoldToTalk", "Mantén {0}"), TNPauseUI::KeyName(FKey(D.PushToTalkKey)));
			}
			else if (S->IsCaptureDeviceChangePending()) { OutText = NSLOCTEXT("TNPause", "MicPending", "Micro nuevo al reaparecer"); }
			else { OutText = OutLevel >= OutMark ? NSLOCTEXT("TNPause", "Heard", "¡Se te oye!") : NSLOCTEXT("TNPause", "Quiet", "En silencio"); }
		});
		Row->SetDescription(NSLOCTEXT("TNPause", "MicMeterDesc", "Habla y mira la barra: cuando pasa de la raya dorada, se te oye."));
	}
	if (UTN_PauseRow* Row = AddListRow(SettingsList))
	{
		Row->SetupButton(ETNPauseRowStyle::List, NSLOCTEXT("TNPause", "ResetVoice", "Restablecer la voz"), [WeakThis, WeakSettings]()
		{
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->ResetGroup(ETNSettingsGroup::Voice); }
			if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->ShowTab(ETNPauseTab::Voice); }
		}, nullptr, NSLOCTEXT("TNPause", "ResetAction", "Restablecer"));
		Row->SetDescription(NSLOCTEXT("TNPause", "ResetVoiceDesc", "Voz abierta, micrófono predeterminado con su sensibilidad y ganancia de siempre y nadie silenciado."));
	}
}

void UTN_PauseMenuWidget::FillControlsTab()
{
	UTN_GameSettingsSubsystem* Settings = GetSettings();
	if (!Settings)
	{
		return;
	}
	TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
	TWeakObjectPtr<UTN_GameSettingsSubsystem> WeakSettings(Settings);
	const FTNGameSettings& Data = Settings->GetEditedSettings();
	auto SensitivityRow = [this, WeakSettings](const FText& Label, const FText& Description, float Value, bool bPad)
	{
		if (UTN_PauseRow* Row = AddListRow(SettingsList))
		{
			Row->SetupSlider(Label, 0.2f, 3.f, 0.05f, Value, [](float V) { return TNPauseUI::Percent(V); }, [WeakSettings, bPad](float V)
			{
				if (UTN_GameSettingsSubsystem* S = WeakSettings.Get())
				{
					S->EditSettings([V, bPad](FTNGameSettings& D) { if (bPad) { D.GamepadSensitivity = V; } else { D.MouseSensitivity = V; } });
				}
			});
			Row->SetDescription(Description);
		}
	};

	AddListHeader(SettingsList, NSLOCTEXT("TNPause", "HeadCamera", "CÁMARA"));
	SensitivityRow(NSLOCTEXT("TNPause", "MouseSens", "Sensibilidad del ratón"), NSLOCTEXT("TNPause", "MouseSensDesc", "Lo rápido que gira la cámara con el ratón. 100 % es la de siempre."),
		Data.MouseSensitivity, false);
	SensitivityRow(NSLOCTEXT("TNPause", "PadSens", "Sensibilidad del mando"), NSLOCTEXT("TNPause", "PadSensDesc", "Lo rápido que gira la cámara con el stick derecho."),
		Data.GamepadSensitivity, true);
	AddToggleRow(NSLOCTEXT("TNPause", "InvertMouse", "Invertir eje Y (ratón)"), NSLOCTEXT("TNPause", "InvertMouseDesc", "Subir el ratón mira hacia abajo, como en un avión."),
		Data.bInvertMouseY, [WeakSettings](bool bOn)
		{
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([bOn](FTNGameSettings& D) { D.bInvertMouseY = bOn; }); }
		});
	AddToggleRow(NSLOCTEXT("TNPause", "InvertPad", "Invertir eje Y (mando)"), NSLOCTEXT("TNPause", "InvertPadDesc", "Subir el stick derecho mira hacia abajo."),
		Data.bInvertGamepadY, [WeakSettings](bool bOn)
		{
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([bOn](FTNGameSettings& D) { D.bInvertGamepadY = bOn; }); }
		});
	AddToggleRow(NSLOCTEXT("TNPause", "PadVibration", "Vibración del mando"),
		NSLOCTEXT("TNPause", "PadVibrationDesc", "El mando vibra al recibir un golpe, más fuerte cuanto más fuerte es."),
		Data.bGamepadVibration, [WeakSettings](bool bOn)
		{
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([bOn](FTNGameSettings& D) { D.bGamepadVibration = bOn; }); }
		});
	AddListNote(SettingsList, NSLOCTEXT("TNPause", "DeviceNote", "La cámara usa la sensibilidad del último aparato que hayas tocado: ratón o mando."));
	if (UTN_PauseRow* Row = AddListRow(SettingsList))
	{
		Row->SetupButton(ETNPauseRowStyle::List, NSLOCTEXT("TNPause", "SeeControls", "Cambiar teclas y botones"), [WeakThis]()
		{
			if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->ShowPage(ETNPausePage::Controls); }
		}, nullptr, NSLOCTEXT("TNPause", "SeeAction", "Abrir"));
		Row->SetDescription(NSLOCTEXT("TNPause", "SeeControlsDesc", "Todas las teclas y los botones del mando del juego: se cambian ahí mismo."));
	}
	if (UTN_PauseRow* Row = AddListRow(SettingsList))
	{
		Row->SetupButton(ETNPauseRowStyle::List, NSLOCTEXT("TNPause", "ResetControls", "Restablecer los controles"), [WeakThis, WeakSettings]()
		{
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->ResetGroup(ETNSettingsGroup::Controls); }
			if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->ShowTab(ETNPauseTab::Controls); }
		}, nullptr, NSLOCTEXT("TNPause", "ResetAction", "Restablecer"));
		Row->SetDescription(NSLOCTEXT("TNPause", "ResetControlsDesc", "Sensibilidad al 100 % y sin invertir (las teclas se restablecen en su página)."));
	}
}

void UTN_PauseMenuWidget::FillGameTab()
{
	UTN_GameSettingsSubsystem* Settings = GetSettings();
	if (!Settings)
	{
		return;
	}
	TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
	TWeakObjectPtr<UTN_GameSettingsSubsystem> WeakSettings(Settings);
	const FTNGameSettings& Data = Settings->GetEditedSettings();
	const bool bGuest = IsGuestMenu();

	// El idioma, lo primero de la pestaña (y el título en dos idiomas): quien no lea el que tiene puesto debe poder encontrarlo.
	// Un invitado de la partida local no lo ve (es del PC: lo elige el jugador 1).
	if (!bGuest)
	{
		AddListHeader(SettingsList, NSLOCTEXT("TNPause", "HeadLanguage", "IDIOMA / LANGUAGE"));
	}
	if (UTN_PauseRow* Row = bGuest ? nullptr : AddListRow(SettingsList))
	{
		TArray<FText> Names;
		TArray<FString> Cultures;
		for (const FTNLanguageEntry& Entry : TNLanguage::GetLanguages())
		{
			Names.Add(FText::FromString(TNLanguage::GetDisplayName(Entry)));
			Cultures.Add(Entry.Culture);
		}
		const int32 Current = FMath::Max(0, TNLanguage::IndexOf(Settings->GetLanguage()));
		Row->SetupChoice(NSLOCTEXT("TNPause", "Language", "Idioma / Language"), Names, Current, [WeakThis, WeakSettings, Cultures](int32 Choice)
		{
			UTN_GameSettingsSubsystem* S = WeakSettings.Get();
			if (!S || !Cultures.IsValidIndex(Choice))
			{
				return;
			}
			// Se pone en caliente: los textos son FText y se traducen solos; el menú solo refresca lo que guarda como texto.
			S->SetLanguage(Cultures[Choice]);
			if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->OnLanguageChanged(); }
		});
		Row->SetDescription(NSLOCTEXT("TNPause", "LanguageDesc",
			"El idioma de todo el juego, al momento. Sin elegir, el de tu sistema. «Restablecer esta pestaña» lo vuelve a poner así."));
	}

	AddListHeader(SettingsList, NSLOCTEXT("TNPause", "HeadCamera", "CÁMARA"));
	AddToggleRow(NSLOCTEXT("TNPause", "Shake", "Temblor de cámara"),
		NSLOCTEXT("TNPause", "ShakeDesc", "Golpes, quads de la carrera, tormenta... Apágalo si te marea."),
		Data.bCameraShake, [WeakSettings](bool bOn)
		{
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([bOn](FTNGameSettings& D) { D.bCameraShake = bOn; }); }
		});
	{
		// El campo de visión se enseña en grados: el de la tortuga en reposo más el desplazamiento.
		float BaseFov = 72.f;
		const APlayerController* PC = GetOwningPlayer();
		if (const ATortugaCharacter* Turtle = PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr)
		{
			BaseFov = Turtle->GetClass()->GetDefaultObject<ATortugaCharacter>()->GetCameraFOVDefault();
		}
		else
		{
			BaseFov = GetDefault<ATortugaCharacter>()->GetCameraFOVDefault();
		}
		if (UTN_PauseRow* Row = AddListRow(SettingsList))
		{
			Row->SetupSlider(NSLOCTEXT("TNPause", "Fov", "Campo de visión"), -15.f, 20.f, 1.f, Data.FieldOfViewOffset,
				[BaseFov](float V) { return FText::Format(NSLOCTEXT("TNPause", "FovFmt", "{0}°"), FText::AsNumber(FMath::RoundToInt(BaseFov + V))); },
				[WeakSettings](float V) { if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([V](FTNGameSettings& D) { D.FieldOfViewOffset = V; }); } });
			Row->SetDescription(NSLOCTEXT("TNPause", "FovDesc", "Cuánto se ve a los lados. Al correr se abre un poco más, como siempre."));
		}
	}
	if (!bGuest)
	{
		AddToggleRow(NSLOCTEXT("TNPause", "Fisheye", "Ojo de pez leve"),
			NSLOCTEXT("TNPause", "FisheyeDesc", "Curva un poco los bordes de la imagen para que todo se vea aún más inmenso. No toca el HUD. Apágalo si te marea."),
			Data.bFisheye, [WeakSettings](bool bOn)
			{
				if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([bOn](FTNGameSettings& D) { D.bFisheye = bOn; }); }
			});

		AddListHeader(SettingsList, NSLOCTEXT("TNPause", "HeadInterface", "INTERFAZ"));
		if (UTN_PauseRow* Row = AddListRow(SettingsList))
		{
			Row->SetupSlider(NSLOCTEXT("TNPause", "UIScale", "Tamaño de la interfaz"), 0.75f, 1.3f, 0.05f, Data.UIScale,
				[](float V) { return TNPauseUI::Percent(V); },
				[WeakSettings](float V) { if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([V](FTNGameSettings& D) { D.UIScale = V; }); } });
			Row->SetDescription(NSLOCTEXT("TNPause", "UIScaleDesc", "Agranda o achica el HUD y los menús del juego (el editor no cambia). Este menú se encoge si no cabe."));
		}

		AddListHeader(SettingsList, NSLOCTEXT("TNPause", "HeadAccess", "ACCESIBILIDAD"));
		if (UTN_PauseRow* Row = AddListRow(SettingsList))
		{
			const TArray<FText> Filters = { NSLOCTEXT("TNPause", "FilterNone", "No"), NSLOCTEXT("TNPause", "FilterDeuter", "Deuteranopía (verde)"),
				NSLOCTEXT("TNPause", "FilterProtan", "Protanopía (rojo)"), NSLOCTEXT("TNPause", "FilterTritan", "Tritanopía (azul)") };
			Row->SetupChoice(NSLOCTEXT("TNPause", "ColorFilter", "Filtro para daltónicos"), Filters, FMath::Clamp<int32>(Data.ColorFilter, 0, 3), [WeakSettings](int32 Choice)
			{
				if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([Choice](FTNGameSettings& D) { D.ColorFilter = static_cast<uint8>(Choice); }); }
			});
			Row->SetDescription(NSLOCTEXT("TNPause", "ColorFilterDesc", "Corrige los colores de toda la imagen para distinguirlos mejor: el mapa, el HUD y sus marcadores."));
		}
		if (UTN_PauseRow* Row = AddListRow(SettingsList))
		{
			Row->SetupSlider(NSLOCTEXT("TNPause", "FilterStrength", "Intensidad del filtro"), 0.f, 1.f, 0.05f, Data.ColorFilterStrength,
				[](float V) { return TNPauseUI::Percent(V); },
				[WeakSettings](float V) { if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([V](FTNGameSettings& D) { D.ColorFilterStrength = V; }); } });
			Row->SetDescription(NSLOCTEXT("TNPause", "FilterStrengthDesc", "Cuánto corrige el filtro para daltónicos."));
		}
		AddToggleRow(NSLOCTEXT("TNPause", "Talkers", "Quién habla (texto)"),
			NSLOCTEXT("TNPause", "TalkersDesc", "A la derecha de la pantalla, el nombre de quien está hablando por voz. Para jugar sin sonido o si oyes mal."),
			Data.bShowTalkers, [WeakSettings](bool bOn)
			{
				if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([bOn](FTNGameSettings& D) { D.bShowTalkers = bOn; }); }
			});

		// Cámara (Docs/Modo_VR.md, «Primera persona»): la tortuga la mira cada fotograma, se aplica en el acto.
		// Solo para el jugador 1: la tortuga mira sus ajustes (GetSettings).
		if (UTN_PauseRow* Row = AddListRow(SettingsList))
		{
			const TArray<FText> Views = { NSLOCTEXT("TNPause", "CameraThird", "Tercera persona"), NSLOCTEXT("TNPause", "CameraFirst", "Primera persona") };
			Row->SetupChoice(NSLOCTEXT("TNPause", "CameraView", "Cámara"), Views, FMath::Clamp<int32>(Data.CameraView, 0, 1), [WeakSettings](int32 Choice)
			{
				if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([Choice](FTNGameSettings& D) { D.CameraView = static_cast<uint8>(Choice); }); }
			});
			// Con las teclas de ahora de «Cambiar de cámara» (T y el clic del stick derecho de serie; se cambian en Controles).
			Row->SetDescription(FText::Format(NSLOCTEXT("TNPause", "CameraViewDesc",
				"Primera persona: la vista va en la cabeza de tu tortuga (también tumbada) y al mirar abajo ves tu cuerpo, tus aletas y tu lengua. Dentro del caparazón se ve desde dentro, a oscuras. También se cambia jugando con {0} o, con el mando, con {1} (en Controles, «Cambiar de cámara»)."),
				TNPauseUI::KeyName(Settings->GetCameraToggleKey(false)), TNPauseUI::KeyName(Settings->GetCameraToggleKey(true))));
		}

		// Modo VR (Docs/Modo_VR.md): se aplica en el acto (UTN_VRSubsystem lo mira cada fotograma).
		AddListHeader(SettingsList, NSLOCTEXT("TNPause", "HeadVR", "REALIDAD VIRTUAL"));
		if (UTN_PauseRow* Row = AddListRow(SettingsList))
		{
			const TArray<FText> Modes = { NSLOCTEXT("TNPause", "VRModeAuto", "Automático"), NSLOCTEXT("TNPause", "VRModeOff", "Desactivado"),
				NSLOCTEXT("TNPause", "VRModeSim", "Simulado sin gafas") };
			Row->SetupChoice(NSLOCTEXT("TNPause", "VRMode", "Modo VR"), Modes, FMath::Clamp<int32>(Data.VRMode, 0, 2), [WeakSettings](int32 Choice)
			{
				if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([Choice](FTNGameSettings& D) { D.VRMode = static_cast<uint8>(Choice); }); }
			});
			Row->SetDescription(NSLOCTEXT("TNPause", "VRModeDesc",
				"Automático: en primera persona con las gafas si el juego arranca con ellas (-vr o «VR Preview»). Desactivado: con gafas, la pantalla plana de siempre. Simulado: el modo VR sin gafas, con el ratón como aleta, para probarlo en el PC."));
		}
		if (UTN_PauseRow* Row = AddListRow(SettingsList))
		{
			const TArray<FText> Turns = { NSLOCTEXT("TNPause", "VRTurn30", "Por pasos de 30°"), NSLOCTEXT("TNPause", "VRTurn45", "Por pasos de 45°"),
				NSLOCTEXT("TNPause", "VRTurnSmooth", "Suave") };
			Row->SetupChoice(NSLOCTEXT("TNPause", "VRTurn", "Giro en VR"), Turns, FMath::Clamp<int32>(Data.VRTurn, 0, 2), [WeakSettings](int32 Choice)
			{
				if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->EditSettings([Choice](FTNGameSettings& D) { D.VRTurn = static_cast<uint8>(Choice); }); }
			});
			Row->SetDescription(NSLOCTEXT("TNPause", "VRTurnDesc",
				"Cómo gira la tortuga con el stick derecho. A pasos marea mucho menos; suave, para quien ya está acostumbrado."));
		}
	}

	if (UTN_PauseRow* Row = AddListRow(SettingsList))
	{
		Row->SetupButton(ETNPauseRowStyle::List, NSLOCTEXT("TNPause", "ResetGame", "Restablecer esta pestaña"), [WeakThis, WeakSettings]()
		{
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->ResetGroup(ETNSettingsGroup::Game); }
			if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->ShowTab(ETNPauseTab::Game); }
		}, nullptr, NSLOCTEXT("TNPause", "ResetAction", "Restablecer"));
		Row->SetDescription(bGuest ? NSLOCTEXT("TNLocal", "ResetGameGuestDesc", "Temblor de cámara encendido y campo de visión de siempre (solo los tuyos).")
			: NSLOCTEXT("TNPause", "ResetGameDesc", "Temblor de cámara y ojo de pez encendidos, campo de visión e interfaz de siempre, sin filtro de color, sin «Quién habla», el idioma de tu sistema, la cámara en tercera persona y el modo VR automático con giro a pasos de 30°."));
	}
	if (UTN_PauseRow* Row = AddListRow(SettingsList))
	{
		Row->SetupButton(ETNPauseRowStyle::List, NSLOCTEXT("TNPause", "ResetAll", "Restablecer todos los ajustes"), [WeakThis, WeakSettings]()
		{
			UTN_PauseMenuWidget* Menu = WeakThis.Get();
			if (!Menu)
			{
				return;
			}
			Menu->AskConfirm(NSLOCTEXT("TNPause", "ResetAllTitle", "¿Restablecer todos los ajustes?"),
				Menu->IsGuestMenu() ? NSLOCTEXT("TNLocal", "ResetAllGuestText", "Tu cámara, tus controles (teclas y botones incluidos), el temblor y el campo de visión vuelven a los de serie.")
					: NSLOCTEXT("TNPause", "ResetAllText", "Sonido, voz, micrófono, controles (teclas incluidas), juego (idioma incluido), brillo y FPS vuelven a los de serie. La calidad gráfica y la pantalla no se tocan."),
				NSLOCTEXT("TNPause", "ResetAllYes", "Restablecer"), [WeakThis, WeakSettings]()
				{
					if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->ResetAll(); }
					if (UTN_PauseMenuWidget* Target = WeakThis.Get())
					{
						Target->ShowTab(ETNPauseTab::Game);
						Target->ShowNotice(NSLOCTEXT("TNPause", "ResetAllDone", "Todo como recién instalado (menos los gráficos)."));
					}
				});
		}, nullptr, NSLOCTEXT("TNPause", "ResetAction", "Restablecer"));
		Row->SetDescription(bGuest ? NSLOCTEXT("TNLocal", "ResetAllGuestDesc", "Todos tus ajustes de jugador a los de serie. Pide confirmación.")
			: NSLOCTEXT("TNPause", "ResetAllDesc", "Todo lo del menú a los valores de serie, menos la calidad gráfica y la pantalla. Pide confirmación."));
	}
}

void UTN_PauseMenuWidget::FillControlsList()
{
	if (!ControlsList)
	{
		return;
	}
	CancelKeyCapture(true);
	ControlsList->ClearChildren();
	UTN_GameSettingsSubsystem* Settings = GetSettings();
	if (!Settings)
	{
		return;
	}
	auto AddInfo = [this](const FText& Label, const FText& Keyboard, const FText& Pad, const FText& Description)
	{
		if (UTN_PauseRow* Row = AddListRow(ControlsList))
		{
			Row->SetupInfo(Label, Keyboard, Pad);
			Row->SetDescription(Description);
		}
	};
	const FText ChangeHelp = NSLOCTEXT("TNPause", "KeyRowDesc", "Intro, A o clic: cambiar (pulsa luego la tecla o el botón). Supr, Y o clic derecho: la de serie.");

	// Lo que hay en IMC_Player (así la lista es siempre la de verdad), con las teclas del jugador.
	AddListHeader(ControlsList, NSLOCTEXT("TNPause", "HeadPlay", "JUGANDO"));
	int32 Actions = 0;
	for (const FTNKeyBinding& Binding : Settings->GetKeyBindings())
	{
		if (Binding.Id == TEXT("Talk") || Binding.Id == TEXT("Pause"))
		{
			continue;
		}
		FText Description = Binding.bEditable[1] ? ChangeHelp
			: FText::Format(NSLOCTEXT("TNPause", "KeyRowPadFixed", "{0} Con el mando va con {1}."), ChangeHelp, TNPauseUI::KeyName(Binding.FixedKeys[1]));
		if (Binding.Id == TEXT("Camera"))
		{
			Description = FText::Format(NSLOCTEXT("TNPause", "CameraKeyRowDesc", "Tercera o primera persona, sin gafas (como Ajustes > Juego > Cámara). {0}"), ChangeHelp);
		}
		AddKeyBindRow(ControlsList, Binding.Id, Description);
		++Actions;
	}
	if (Actions == 0)
	{
		AddListNote(ControlsList, NSLOCTEXT("TNPause", "NoMapping", "No se ha podido leer la lista de controles (IMC_Player)."));
	}

	// Partida local: sin chat de voz (no hay tecla de hablar).
	if (IsLocalGame())
	{
		AddListHeader(ControlsList, NSLOCTEXT("TNLocal", "HeadMenu", "MENÚ"));
	}
	else
	{
		AddListHeader(ControlsList, NSLOCTEXT("TNPause", "HeadVoiceMenu", "VOZ Y MENÚ"));
		AddKeyBindRow(ControlsList, TEXT("Talk"), FText::Format(NSLOCTEXT("TNPause", "TalkKeyRowDesc", "Solo con el modo pulsar para hablar (Ajustes > Voz). {0}"), ChangeHelp));
	}
	AddKeyBindRow(ControlsList, TEXT("Pause"), FText::Format(NSLOCTEXT("TNPause", "PauseKeyRowDesc", "Esc lo abre y lo cierra siempre (en el editor, Tab). {0}"), ChangeHelp));
	if (UTN_PauseRow* Row = AddListRow(ControlsList))
	{
		TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
		Row->SetupButton(ETNPauseRowStyle::List, NSLOCTEXT("TNPause", "ResetKeys", "Restablecer todos los controles"), [WeakThis]()
		{
			UTN_PauseMenuWidget* Menu = WeakThis.Get();
			if (!Menu)
			{
				return;
			}
			Menu->AskConfirm(NSLOCTEXT("TNPause", "ResetKeysTitle", "¿Restablecer los controles?"),
				NSLOCTEXT("TNPause", "ResetKeysText", "Todas las teclas y los botones vuelven a los de serie (hablar y el menú incluidos)."),
				NSLOCTEXT("TNPause", "ResetKeysYes", "Restablecer"), [WeakThis]()
				{
					UTN_PauseMenuWidget* Target = WeakThis.Get();
					UTN_GameSettingsSubsystem* S = Target ? Target->GetSettings() : nullptr;
					if (!S)
					{
						return;
					}
					S->ResetAllKeyBindings();
					Target->ShowNotice(NSLOCTEXT("TNPause", "ResetKeysDone", "Controles de serie."));
					Target->RefreshKeyRows(FString());
				});
		}, nullptr, NSLOCTEXT("TNPause", "ResetAction", "Restablecer"));
		Row->SetDescription(Settings->HasCustomKeys() ? NSLOCTEXT("TNPause", "ResetKeysDesc", "Todas las teclas y los botones a los de serie. Pide confirmación.")
			: NSLOCTEXT("TNPause", "ResetKeysDescNone", "Ahora mismo ya van todos con los de serie."));
	}

	// Lo que no se cambia: las acciones de ejes (cámara, rueda), el espectador y los menús.
	AddListHeader(ControlsList, NSLOCTEXT("TNPause", "HeadFixed", "SIEMPRE IGUAL"));
	for (const FTNKeyBinding& Fixed : Settings->GetFixedControls())
	{
		AddInfo(Fixed.Label, TNPauseUI::KeyName(Fixed.FixedKeys[0]), TNPauseUI::KeyName(Fixed.FixedKeys[1]),
			NSLOCTEXT("TNPause", "FixedDesc", "Va con el ratón y los sticks: su sensibilidad está en Ajustes > Controles."));
	}
	AddInfo(NSLOCTEXT("TNPause", "SpectateRow", "Espectador: cambiar de tortuga"), NSLOCTEXT("TNPause", "SpectateKeys", "← → · Re Pág · Av Pág · rueda"),
		NSLOCTEXT("TNPause", "SpectatePad", "LB · RB · cruceta ← →"), NSLOCTEXT("TNPause", "SpectateDesc", "Cuando miras a los demás tras caer o llegar a la meta (la rueda, con la cámara fija)."));
	AddInfo(NSLOCTEXT("TNPause", "SpectateCamRow", "Espectador: cámara libre o fija"), NSLOCTEXT("TNPause", "SpectateCamKeys", "C"),
		TNPauseUI::KeyName(EKeys::Gamepad_RightThumbstick), NSLOCTEXT("TNPause", "SpectateCamDesc", "La libre gira con el ratón o el stick derecho, con tu sensibilidad."));
	AddInfo(NSLOCTEXT("TNPause", "SpectateZoomRow", "Espectador: acercar y alejar"), NSLOCTEXT("TNPause", "SpectateZoomKeys", "Rueda"),
		NSLOCTEXT("TNPause", "SpectateZoomPad", "Gatillos"), NSLOCTEXT("TNPause", "SpectateZoomDesc", "Con la cámara libre."));
	AddInfo(NSLOCTEXT("TNPause", "MenuNav", "Moverse por los menús"), NSLOCTEXT("TNPause", "MenuNavKeys", "Flechas · WASD · Intro · Esc"),
		NSLOCTEXT("TNPause", "MenuNavPad", "Stick · cruceta · A · B"), FText::GetEmpty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Menú: sala (Docs/Salas.md)
// ─────────────────────────────────────────────────────────────────────────────

FString UTN_PauseMenuWidget::BuildRoomSignature() const
{
	const UMP_GameInstance* GameInstance = Cast<UMP_GameInstance>(GetGameInstance());
	FTNRoomSnapshot Room;
	const bool bRoom = GameInstance && GameInstance->GetRoomSnapshot(Room);
	FString Signature = FString::Printf(TEXT("%d|%d|%s|%d|%d|%d"), bRoom ? 1 : 0, Room.NameId, *Room.Code, Room.bLocked ? 1 : 0, Room.MaxPlayers,
		Room.bIsHost ? 1 : 0);
	const UWorld* World = GetWorld();
	if (const AGameStateBase* State = World ? World->GetGameState() : nullptr)
	{
		for (const APlayerState* PS : State->PlayerArray)
		{
			if (PS) { Signature += FString::Printf(TEXT("|%d:%s"), PS->GetPlayerId(), *PS->GetPlayerName()); }
		}
	}
	return Signature;
}

void UTN_PauseMenuWidget::FillRoomList()
{
	if (!RoomList)
	{
		return;
	}
	RoomListSignature = BuildRoomSignature();

	// Para no perder el sitio al rehacerla: la posición de la fila enfocada y el desplazamiento.
	UTN_PauseRow* FocusedRow = LastFocused.Get();
	const int32 FocusedIndex = FocusedRow && FocusedRow->GetParent() == RoomList.Get() ? RoomList->GetChildIndex(FocusedRow) : INDEX_NONE;
	const float Offset = RoomList->GetScrollOffset();
	RoomList->ClearChildren();

	UMP_GameInstance* GameInstance = Cast<UMP_GameInstance>(GetGameInstance());
	FTNRoomSnapshot Room;
	const bool bRoom = GameInstance && GameInstance->GetRoomSnapshot(Room);
	TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
	TWeakObjectPtr<UMP_GameInstance> WeakGameInstance(GameInstance);
	const UWorld* World = GetWorld();
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	const int32 Inside = bRoom ? Room.Players : (State ? State->PlayerArray.Num() : 1);
	const FText Places = FText::Format(NSLOCTEXT("TNPause", "RoomPlaces", "{0}/{1} tortugas"), FText::AsNumber(Inside),
		FText::AsNumber(FMath::Max(Inside, Room.MaxPlayers)));

	AddListHeader(RoomList, NSLOCTEXT("TNPause", "HeadRoom", "LA SALA"));
	if (!bRoom)
	{
		AddListNote(RoomList, NSLOCTEXT("TNPause", "RoomUnknown", "Aún no ha llegado la información de la sala: un momento..."));
	}
	else
	{
		if (UTN_PauseRow* Row = AddListRow(RoomList))
		{
			Row->SetupInfo(NSLOCTEXT("TNPause", "RoomName", "Nombre"), TNRoomNames::Get(Room.NameId), TNRoomText::Visibility(Room.bPrivate));
			Row->SetDescription(Room.bPrivate
				? NSLOCTEXT("TNPause", "RoomNamePrivateDesc", "Sala privada: no sale en la lista de partidas; se entra con el código o por invitación de Steam.")
				: NSLOCTEXT("TNPause", "RoomNamePublicDesc", "Sala pública: sale en la lista de partidas del menú principal («Unirse»)."));
		}
		if (UTN_PauseRow* Row = AddListRow(RoomList))
		{
			const FString RoomCode = Room.Code;
			Row->SetupEntry(NSLOCTEXT("TNPause", "RoomCode", "Código de la sala"), FText::FromString(RoomCode), NSLOCTEXT("TNPause", "RoomCopy", "Copiar"),
				[WeakThis, RoomCode]()
				{
					FPlatformApplicationMisc::ClipboardCopy(*RoomCode);
					if (UTN_PauseMenuWidget* Menu = WeakThis.Get())
					{
						Menu->ShowNotice(FText::Format(NSLOCTEXT("TNPause", "RoomCopied", "Código {0} copiado: pégalo donde quieras (Ctrl+V)."), FText::FromString(RoomCode)));
					}
				});
			Row->SetDescription(NSLOCTEXT("TNPause", "RoomCodeDesc",
				"Con este código se entra en la sala desde el menú principal («Unirse» y escribirlo). Intro, A o clic: copiarlo."));
			Row->SetValueColors(TNHUDArt::Gold, TNHUDArt::SeaLight);
		}
		if (Room.bIsHost)
		{
			if (UTN_PauseRow* Row = AddListRow(RoomList))
			{
				const TArray<FText> Doors = { NSLOCTEXT("TNPause", "RoomOpen", "Abierta"), NSLOCTEXT("TNPause", "RoomClosed", "Cerrada") };
				Row->SetupChoice(NSLOCTEXT("TNPause", "RoomDoor", "Entrada"), Doors, Room.bLocked ? 1 : 0, [WeakThis, WeakGameInstance](int32 Choice)
				{
					if (UMP_GameInstance* RoomOwner = WeakGameInstance.Get()) { RoomOwner->SetRoomLocked(Choice == 1); }
					if (UTN_PauseMenuWidget* Menu = WeakThis.Get())
					{
						Menu->ShowNotice(Choice == 1 ? NSLOCTEXT("TNPause", "RoomLockedNotice", "Sala cerrada: no entra nadie más (los que ya estaban pueden volver).")
							: NSLOCTEXT("TNPause", "RoomUnlockedNotice", "Sala abierta: puede entrar gente otra vez."));
					}
				});
				Row->SetDescription(NSLOCTEXT("TNPause", "RoomDoorDesc",
					"Cerrada: no entra nadie nuevo, ni por la lista, ni con el código ni por invitación. Los que ya están siguen y pueden volver si se les cae la conexión."));
			}
		}
		else if (UTN_PauseRow* Row = AddListRow(RoomList))
		{
			Row->SetupInfo(NSLOCTEXT("TNPause", "RoomDoor", "Entrada"), Room.bLocked ? NSLOCTEXT("TNPause", "RoomClosed", "Cerrada")
				: NSLOCTEXT("TNPause", "RoomOpen", "Abierta"), Places);
			Row->SetDescription(NSLOCTEXT("TNPause", "RoomDoorGuestDesc", "Solo el anfitrión puede cerrar la sala o expulsar a alguien."));
		}
		if (GameInstance && GameInstance->CanInviteFriends())
		{
			if (UTN_PauseRow* Row = AddListRow(RoomList))
			{
				Row->SetupButton(ETNPauseRowStyle::List, NSLOCTEXT("TNPause", "RoomInvite", "Invitar a amigos de Steam"), [WeakGameInstance]()
				{
					if (UMP_GameInstance* RoomOwner = WeakGameInstance.Get()) { RoomOwner->InviteFriends(); }
				}, nullptr, NSLOCTEXT("TNPause", "RoomInviteAction", "Abrir Steam"));
				Row->SetDescription(NSLOCTEXT("TNPause", "RoomInviteDesc", "La lista de amigos de Steam, para invitarles (la invitación también vale en las salas privadas)."));
			}
		}
	}

	// Quién está dentro: nombre, anfitrión o ping y, para el anfitrión, el «⋮» de los demás.
	AddListHeader(RoomList, FText::Format(NSLOCTEXT("TNPause", "HeadRoomPlayers", "TORTUGAS EN LA SALA · {0}"), Places));
	FUniqueNetIdRepl HostId;
	const IOnlineSessionPtr Sessions = TNPauseUI::SessionInterface();
	if (const FNamedOnlineSession* Named = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr)
	{
		if (Named->OwningUserId.IsValid()) { HostId = FUniqueNetIdRepl(Named->OwningUserId); }
	}
	const APlayerState* Mine = GetOwningPlayerState();
	const bool bHostView = IsHost();
	if (State)
	{
		for (APlayerState* PS : State->PlayerArray)
		{
			if (!PS)
			{
				continue;
			}
			const bool bMe = PS == Mine;
			const bool bRowHost = (bMe && bHostView) || (!bHostView && ((HostId.IsValid() && PS->GetUniqueId() == HostId)
				|| (bRoom && !Room.HostName.IsEmpty() && PS->GetPlayerName() == Room.HostName)));
			const bool bCanKick = bHostView && !bMe;
			const FText Sub = TNPauseUI::PlayerSub(PS, bMe, bRowHost, World && World->GetNetMode() == NM_Client);
			TWeakObjectPtr<APlayerState> WeakPlayer(PS);
			if (UTN_PauseRow* Row = AddListRow(RoomList))
			{
				Row->SetupEntry(FText::FromString(PS->GetPlayerName()), Sub, FText::GetEmpty(), [WeakThis, WeakPlayer, bCanKick]()
				{
					UTN_PauseMenuWidget* Menu = WeakThis.Get();
					if (Menu && bCanKick && WeakPlayer.IsValid()) { Menu->OpenPlayerOptions(WeakPlayer.Get()); }
				}, bCanKick ? TNRoomArt::MoreIcon() : (bRowHost ? TNPauseArt::HostCrown() : nullptr));
				Row->SetDescription(bCanKick ? NSLOCTEXT("TNPause", "PlayerRowHostDesc", "Intro, A o clic: opciones de esta tortuga (expulsarla de la sala).")
					: (bMe ? NSLOCTEXT("TNPause", "PlayerRowMeDesc", "Tu tortuga.") : NSLOCTEXT("TNPause", "PlayerRowGuestDesc", "Solo el anfitrión puede expulsar a alguien.")));
				Row->SetValueColors(bMe ? TNHUDArt::Gold : TNHUDArt::SandC, TNHUDArt::SeaLight);
			}
		}
	}

	RoomList->SetScrollOffset(Offset);
	// Si el foco estaba en esta lista, vuelve a la misma posición (o a la fila de antes, si esa ya no es una fila).
	if (FocusedIndex != INDEX_NONE)
	{
		for (int32 i = FMath::Min(FocusedIndex, RoomList->GetChildrenCount() - 1); i >= 0; --i)
		{
			if (UTN_PauseRow* Row = Cast<UTN_PauseRow>(RoomList->GetChildAt(i)))
			{
				FocusRow(Row);
				return;
			}
		}
		FocusFirstOfPage();
	}
}

void UTN_PauseMenuWidget::OpenPlayerOptions(APlayerState* Target)
{
	if (!Target || !IsHost())
	{
		return;
	}
	const FText Name = FText::FromString(Target->GetPlayerName());
	TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
	TWeakObjectPtr<APlayerState> WeakTarget(Target);
	// Las opciones del «⋮» (hoy, expulsar) y, después, la confirmación.
	AskConfirm(Name, FText::Format(NSLOCTEXT("TNPause", "PlayerOptionsText", "Opciones del anfitrión para {0}."), Name),
		NSLOCTEXT("TNPause", "PlayerKick", "Expulsar"), [WeakThis, WeakTarget, Name]()
		{
			UTN_PauseMenuWidget* Menu = WeakThis.Get();
			if (!Menu || !WeakTarget.IsValid())
			{
				return;
			}
			Menu->AskConfirm(FText::Format(NSLOCTEXT("TNPause", "KickTitle", "¿Expulsar a {0}?"), Name),
				NSLOCTEXT("TNPause", "KickText", "Sale de la partida y vuelve al menú principal. No podrá volver a entrar en esta sala mientras dure."),
				NSLOCTEXT("TNPause", "KickYes", "Sí, expulsar"), [WeakThis, WeakTarget, Name]()
				{
					UTN_PauseMenuWidget* KickMenu = WeakThis.Get();
					UMP_GameInstance* RoomOwner = KickMenu ? Cast<UMP_GameInstance>(KickMenu->GetGameInstance()) : nullptr;
					APlayerState* Kicked = WeakTarget.Get();
					if (!KickMenu)
					{
						return;
					}
					const bool bKicked = RoomOwner && Kicked && RoomOwner->KickFromRoom(Kicked);
					KickMenu->ShowNotice(FText::Format(bKicked ? NSLOCTEXT("TNPause", "KickDone", "{0} ha sido expulsado de la sala.")
						: NSLOCTEXT("TNPause", "KickGone", "{0} ya no está en la sala."), Name));
				});
		});
}

// ─────────────────────────────────────────────────────────────────────────────
// Menú: acciones
// ─────────────────────────────────────────────────────────────────────────────

void UTN_PauseMenuWidget::GoBack()
{
	if (IsConfirmOpen())
	{
		CloseConfirm(false);
		return;
	}
	if (Page != ETNPausePage::Home)
	{
		PlayUISound(ETNPauseSound::Press, 0.f);
		ShowPage(ETNPausePage::Home);
		return;
	}
	CloseMenu();
}

void UTN_PauseMenuWidget::CloseMenu()
{
	if (bLeaving)
	{
		return;
	}
	bLeaving = true;
	if (UTN_GameSettingsSubsystem* Settings = GetSettings()) { Settings->ClosePauseMenu(); }
	if (TNVR::IsOnScreen(this)) { RemoveFromParent(); }
}

void UTN_PauseMenuWidget::ReturnToLobby()
{
	TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
	if (!IsHost())
	{
		// Un invitado no mueve al grupo: sale él de la sesión al menú principal (los demás siguen jugando).
		AskConfirm(NSLOCTEXT("TNPause", "LobbyGuestTitle", "¿Salir de la partida?"),
			NSLOCTEXT("TNPause", "LobbyGuestText", "Solo el anfitrión puede llevar al grupo al lobby. Tú vuelves al menú principal y los demás siguen jugando."),
			NSLOCTEXT("TNPause", "LeaveYes", "Salir"), [WeakThis]()
			{
				UTN_PauseMenuWidget* Menu = WeakThis.Get();
				UMP_GameInstance* GameInstance = Menu ? Cast<UMP_GameInstance>(Menu->GetGameInstance()) : nullptr;
				if (!Menu)
				{
					return;
				}
				Menu->CloseMenu();
				if (GameInstance) { GameInstance->HandleReturnToMenu(); }
			});
		return;
	}
	AskConfirm(NSLOCTEXT("TNPause", "LobbyTitle", "¿Volver al lobby?"),
		NSLOCTEXT("TNPause", "LobbyText", "La ronda se acaba para todos y el grupo vuelve al lobby del castillo."),
		NSLOCTEXT("TNPause", "LobbyYes", "Al lobby"), [WeakThis]()
		{
			UTN_PauseMenuWidget* Menu = WeakThis.Get();
			UWorld* World = Menu ? Menu->GetWorld() : nullptr;
			if (!World || World->IsInSeamlessTravel())
			{
				return;
			}
			// Rally y Karts (ATN_KartGameMode hereda de él): la misma vuelta que al acabar la carrera.
			if (ATN_RallyGameMode* RallyMode = World->GetAuthGameMode<ATN_RallyGameMode>())
			{
				Menu->CloseMenu();
				RallyMode->ReturnToLobbyNow();
				return;
			}
			ATN_RunGameMode* GameMode = World->GetAuthGameMode<ATN_RunGameMode>();
			if (!GameMode)
			{
				return;
			}
			Menu->CloseMenu();
			// La misma vuelta que al acabar la ronda: peones fuera (voz a salvo) y viaje sin cortes; los invitados vienen detrás.
			GameMode->ReturnToLobbyNow();
		});
}

void UTN_PauseMenuWidget::LeaveToMenu()
{
	const bool bGuest = GetWorld() && GetWorld()->GetNetMode() == NM_Client;
	TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
	AskConfirm(bGuest ? NSLOCTEXT("TNPause", "LeaveTitle", "¿Salir de la partida?") : NSLOCTEXT("TNPause", "MenuTitle", "¿Volver al menú principal?"),
		bGuest ? NSLOCTEXT("TNPause", "LeaveText", "Vuelves al menú principal. Los demás siguen jugando sin ti.")
			: NSLOCTEXT("TNPause", "MenuText", "La partida se cierra para todos: tus compañeros también vuelven al menú."),
		NSLOCTEXT("TNPause", "LeaveYes", "Salir"), [WeakThis]()
		{
			UTN_PauseMenuWidget* Menu = WeakThis.Get();
			UMP_GameInstance* GameInstance = Menu ? Cast<UMP_GameInstance>(Menu->GetGameInstance()) : nullptr;
			if (!Menu)
			{
				return;
			}
			Menu->CloseMenu();
			// Anfitrión: cierra la sesión y todos vuelven al menú; invitado: se va él solo (UMP_GameInstance::HandleReturnToMenu).
			if (GameInstance) { GameInstance->HandleReturnToMenu(); }
		});
}

void UTN_PauseMenuWidget::QuitToDesktop()
{
	const UWorld* World = GetWorld();
	const bool bHostWithGuests = World && World->GetNetMode() == NM_ListenServer && World->GetGameState() && World->GetGameState()->PlayerArray.Num() > 1;
	TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
	AskConfirm(NSLOCTEXT("TNPause", "QuitTitle", "¿Salir al escritorio?"),
		bHostWithGuests ? NSLOCTEXT("TNPause", "QuitTextHost", "Se cierra Tortunavy y, como eres el anfitrión, la partida se acaba para todos.")
			: NSLOCTEXT("TNPause", "QuitText", "Se cierra Tortunavy."),
		NSLOCTEXT("TNPause", "QuitYes", "Salir"), [WeakThis]()
		{
			UTN_PauseMenuWidget* Menu = WeakThis.Get();
			if (!Menu)
			{
				return;
			}
			UWorld* QuitWorld = Menu->GetWorld();
			APlayerController* PC = Menu->GetOwningPlayer();
			Menu->CloseMenu();
			UKismetSystemLibrary::QuitGame(QuitWorld, PC, EQuitPreference::Quit, false);
		});
}

void UTN_PauseMenuWidget::AskConfirm(const FText& Title, const FText& Text, const FText& YesLabel, TFunction<void()> OnYes, TFunction<void()> OnNo, float CountdownSeconds)
{
	if (!ConfirmLayer)
	{
		return;
	}
	ConfirmAction = MoveTemp(OnYes);
	CancelAction = MoveTemp(OnNo);
	ConfirmCountdown = CountdownSeconds;
	ConfirmBaseText = Text;
	if (ConfirmTitle) { ConfirmTitle->SetText(Title); }
	if (ConfirmText)
	{
		ConfirmText->SetText(CountdownSeconds > 0.f ? FText::Format(ConfirmBaseText, FText::AsNumber(FMath::CeilToInt(CountdownSeconds))) : ConfirmBaseText);
	}
	if (ConfirmYes) { ConfirmYes->SetLabel(YesLabel); }
	if (!FocusBeforeConfirm.IsValid()) { FocusBeforeConfirm = LastFocused; }
	ConfirmLayer->SetVisibility(ESlateVisibility::Visible);
	// Lo de detrás no se puede recorrer ni pulsar mientras tanto.
	if (Pages) { Pages->SetVisibility(ESlateVisibility::HitTestInvisible); }
	// Con cuenta atrás (resolución nueva), el foco en «Mantener»; en lo demás, en lo seguro: «Cancelar».
	FocusRow(CountdownSeconds > 0.f ? ConfirmYes.Get() : ConfirmNo.Get());
	PlayUISound(ETNPauseSound::Press, 0.f);
}

void UTN_PauseMenuWidget::CloseConfirm(bool bAccepted)
{
	if (!IsConfirmOpen())
	{
		return;
	}
	ConfirmLayer->SetVisibility(ESlateVisibility::Collapsed);
	if (Pages) { Pages->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	ConfirmCountdown = -1.f;
	TFunction<void()> Action = bAccepted ? MoveTemp(ConfirmAction) : MoveTemp(CancelAction);
	ConfirmAction = nullptr;
	CancelAction = nullptr;
	UTN_PauseRow* Back = FocusBeforeConfirm.Get();
	FocusBeforeConfirm.Reset();
	if (Back) { FocusRow(Back); }
	else { FocusFirstOfPage(); }
	if (Action) { Action(); }
}

bool UTN_PauseMenuWidget::IsConfirmOpen() const
{
	return ConfirmLayer && ConfirmLayer->GetVisibility() == ESlateVisibility::Visible;
}

void UTN_PauseMenuWidget::OnVideoModeChanged()
{
	UTN_GameSettingsSubsystem* Settings = GetSettings();
	if (!Settings || !UTN_GameSettingsSubsystem::CanChangeVideoMode())
	{
		return;
	}
	Settings->ApplyVideoMode();
	TWeakObjectPtr<UTN_PauseMenuWidget> WeakThis(this);
	TWeakObjectPtr<UTN_GameSettingsSubsystem> WeakSettings(Settings);
	AskConfirm(NSLOCTEXT("TNPause", "VideoTitle", "¿Mantener esta pantalla?"),
		NSLOCTEXT("TNPause", "VideoText", "Si algo no se ve bien, no toques nada: se deshace sola en {0} s."),
		NSLOCTEXT("TNPause", "VideoKeep", "Mantener"),
		[WeakThis, WeakSettings]()
		{
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->FinishVideoModeChange(true); }
			if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->RefreshGraphicsRows(); }
		},
		[WeakThis, WeakSettings]()
		{
			if (UTN_GameSettingsSubsystem* S = WeakSettings.Get()) { S->FinishVideoModeChange(false); }
			if (UTN_PauseMenuWidget* Menu = WeakThis.Get()) { Menu->ShowTab(ETNPauseTab::Graphics); }
		},
		12.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Menú: teclas
// ─────────────────────────────────────────────────────────────────────────────

void UTN_PauseMenuWidget::StartKeyCapture(UTN_PauseRow* Row, const FString& Id)
{
	if (!Row || Id.IsEmpty())
	{
		return;
	}
	CancelKeyCapture(true);
	CaptureRow = Row;
	CaptureId = Id;
	CaptureElapsed = 0.f;
	Row->SetCapturing(true);
	FText Label = FText::FromString(Id);
	if (const UTN_GameSettingsSubsystem* Settings = GetSettings())
	{
		for (const FTNKeyBinding& Binding : Settings->GetKeyBindings())
		{
			if (Binding.Id == Id) { Label = Binding.Label; }
		}
	}
	ShowNotice(FText::Format(NSLOCTEXT("TNPause", "CaptureNotice", "Pulsa la tecla o el botón del mando para «{0}». Esc o Select/Vista: dejarlo como está."), Label),
		TNPauseUI::KeyCaptureTimeout + 0.5f);
}

void UTN_PauseMenuWidget::FinishKeyCapture(const FKey& Key)
{
	UTN_GameSettingsSubsystem* Settings = GetSettings();
	if (!Settings || !IsCapturingKey())
	{
		return;
	}
	FText Message;
	const ETNRebindResult Result = Settings->RebindKey(CaptureId, Key, Message);
	if (Result == ETNRebindResult::Refused)
	{
		// No vale (reservada, o ese aparato no se cambia): se dice y se sigue esperando otra.
		ShowNotice(Message, TNPauseUI::KeyCaptureTimeout);
		CaptureElapsed = 0.f;
		PlayUISound(ETNPauseSound::Hover, 0.f);
		return;
	}
	const FString Id = CaptureId;
	CancelKeyCapture(true);
	ShowNotice(Message, 6.f);
	PlayUISound(ETNPauseSound::Press, 0.f);
	RefreshKeyRows(Id);
}

void UTN_PauseMenuWidget::CancelKeyCapture(bool bSilent)
{
	if (!IsCapturingKey())
	{
		return;
	}
	if (UTN_PauseRow* Row = CaptureRow.Get()) { Row->SetCapturing(false); }
	CaptureRow.Reset();
	CaptureId.Reset();
	CaptureElapsed = 0.f;
	if (!bSilent) { ShowNotice(NSLOCTEXT("TNPause", "CaptureCancelled", "Sin cambios."), 2.5f); }
}

void UTN_PauseMenuWidget::ResetKeyRow(const FString& Id)
{
	UTN_GameSettingsSubsystem* Settings = GetSettings();
	if (!Settings)
	{
		return;
	}
	CancelKeyCapture(true);
	FText Message;
	Settings->ResetKeyBinding(Id, Message);
	ShowNotice(Message, 6.f);
	RefreshKeyRows(Id);
}

void UTN_PauseMenuWidget::RefreshKeyRows(const FString& FocusId)
{
	// Se rehace la lista (una tecla cambiada puede haber movido otra) sin perder por dónde iba.
	UScrollBox* List = Page == ETNPausePage::Controls ? ControlsList.Get() : (Page == ETNPausePage::Settings ? SettingsList.Get() : nullptr);
	if (!List)
	{
		return;
	}
	const float Offset = List->GetScrollOffset();
	if (Page == ETNPausePage::Controls) { FillControlsList(); }
	else { FillTab(); }
	List->SetScrollOffset(Offset);
	const int32 Count = List->GetChildrenCount();
	for (int32 i = 0; i < Count; ++i)
	{
		UTN_PauseRow* Row = Cast<UTN_PauseRow>(List->GetChildAt(i));
		if (Row && !FocusId.IsEmpty() && Row->GetBindingId() == FocusId)
		{
			FocusRow(Row);
			return;
		}
	}
	FocusFirstOfPage();
}

void UTN_PauseMenuWidget::ShowNotice(const FText& Text, float Seconds)
{
	if (!NoticeText || Text.IsEmpty())
	{
		return;
	}
	NoticeText->SetText(Text);
	NoticeText->SetRenderOpacity(1.f);
	NoticeText->SetVisibility(ESlateVisibility::HitTestInvisible);
	NoticeTime = Seconds;
}

// ─────────────────────────────────────────────────────────────────────────────
// Menú: entrada, foco y sonido
// ─────────────────────────────────────────────────────────────────────────────

void UTN_PauseMenuWidget::TakeInput()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC || bInputTaken)
	{
		return;
	}
	bInputTaken = true;

	// Lo que ya había en pantalla (para saber al cerrar si ha salido otro menú que quiere el cursor).
	// En VR los menús no están en el viewport sino dentro del panel de la interfaz (no son de primer nivel).
	TArray<UUserWidget*> Found;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Found, UUserWidget::StaticClass(), !TNVR::IsEnabled());
	for (UUserWidget* Widget : Found) { WidgetsAtOpen.Add(Widget); }

	// Suelta lo que estuviera pulsado (correr, andar) y deja la tortuga quieta: ni se mueve ni gira la cámara.
	PC->FlushPressedKeys();
	if (!PC->IsMoveInputIgnored()) { PC->SetIgnoreMoveInput(true); ++IgnoreMoveApplied; }
	if (!PC->IsLookInputIgnored()) { PC->SetIgnoreLookInput(true); ++IgnoreLookApplied; }
	ApplyMenuInputMode();
	ShowPage(ETNPausePage::Home);
	PlayUISound(ETNPauseSound::Press, 0.f);
}

void UTN_PauseMenuWidget::ApplyMenuInputMode()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}
	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
}

void UTN_PauseMenuWidget::ReleaseInput()
{
	if (!bInputTaken)
	{
		return;
	}
	bInputTaken = false;
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC) || !PC->IsLocalController())
	{
		return;
	}
	for (; IgnoreMoveApplied > 0; --IgnoreMoveApplied) { PC->SetIgnoreMoveInput(false); }
	for (; IgnoreLookApplied > 0; --IgnoreLookApplied) { PC->SetIgnoreLookInput(false); }

	// Si mientras tanto ha salido otro menú que se puede pulsar (p. ej. el campeón de la carrera), se le deja el cursor.
	bool bOtherMenu = false;
	TArray<UUserWidget*> Found;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Found, UUserWidget::StaticClass(), !TNVR::IsEnabled());
	for (UUserWidget* Widget : Found)
	{
		// Solo cuenta lo que se ve, se puede pulsar y no estaba al abrir (no el contador de FPS ni el recuento).
		const ESlateVisibility Shown = Widget ? Widget->GetVisibility() : ESlateVisibility::Collapsed;
		if (!Widget || Widget == this || !TNVR::IsOnScreen(Widget)
			|| (Shown != ESlateVisibility::Visible && Shown != ESlateVisibility::SelfHitTestInvisible)
			|| WidgetsAtOpen.ContainsByPredicate([Widget](const TWeakObjectPtr<UUserWidget>& Old) { return Old.Get() == Widget; }))
		{
			continue;
		}
		// Un menú: se enfoca él (tienda, probador, general) o tiene botones o filas que se enfocan (campeón de la carrera).
		bool bInteractive = Widget->IsFocusable();
		if (!bInteractive && Widget->WidgetTree)
		{
			Widget->WidgetTree->ForEachWidget([&bInteractive](UWidget* Child)
			{
				const UUserWidget* ChildUserWidget = Cast<UUserWidget>(Child);
				bInteractive = bInteractive || Child->IsA<UButton>() || (ChildUserWidget && ChildUserWidget->IsFocusable());
			});
		}
		if (bInteractive)
		{
			bOtherMenu = true;
			break;
		}
	}
	if (bOtherMenu)
	{
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
		if (FSlateApplication::IsInitialized())
		{
			// Con la pantalla partida, solo el foco de quien lo abrió (los menús de los demás siguen con el suyo).
			const ULocalPlayer* Player = PC->GetLocalPlayer();
			if (Player && IsLocalGame()) { FSlateApplication::Get().SetUserFocusToGameViewport(Player->GetControllerId()); }
			else { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
		}
	}
}

void UTN_PauseMenuWidget::NativeDestruct()
{
	// También al quitarse por un viaje (el mundo se limpia): la entrada vuelve a como estaba.
	CancelKeyCapture(true);
	ReleaseInput();
	if (UTN_GameSettingsSubsystem* Settings = GetSettings()) { Settings->NotifyPauseMenuClosed(this); }
	Super::NativeDestruct();
}

void UTN_PauseMenuWidget::FocusRow(UTN_PauseRow* Row)
{
	if (Row)
	{
		FocusForOwner(Row);
		LastFocused = Row;
	}
}

void UTN_PauseMenuWidget::FocusForOwner(UWidget* Widget)
{
	if (!Widget)
	{
		return;
	}
	// Con la pantalla partida, el foco del jugador que lo abrió (un invitado lo maneja con su mando); si no, el del teclado.
	APlayerController* PC = GetOwningPlayer();
	if (PC && IsLocalGame())
	{
		Widget->SetUserFocus(PC);
		return;
	}
	Widget->SetKeyboardFocus();
}

void UTN_PauseMenuWidget::FocusFirstOfPage()
{
	switch (Page)
	{
	case ETNPausePage::Home:
		for (UTN_PauseRow* Row : HomeRows)
		{
			if (Row && Row->IsRowEnabled()) { FocusRow(Row); return; }
		}
		break;
	case ETNPausePage::Settings:
	case ETNPausePage::Controls:
	case ETNPausePage::Room:
	{
		UScrollBox* List = Page == ETNPausePage::Settings ? SettingsList.Get() : (Page == ETNPausePage::Controls ? ControlsList.Get() : RoomList.Get());
		const int32 Count = List ? List->GetChildrenCount() : 0;
		for (int32 i = 0; i < Count; ++i)
		{
			if (UTN_PauseRow* Row = Cast<UTN_PauseRow>(List->GetChildAt(i))) { FocusRow(Row); return; }
		}
		break;
	}
	case ETNPausePage::Credits:
		if (CreditsPage) { CreditsPage->FocusList(); return; }
		break;
	}
	FocusForOwner(this);
}

void UTN_PauseMenuWidget::HandleRowFocused(UTN_PauseRow* Row)
{
	LastFocused = Row;
	if (HelpText) { HelpText->SetText(Row ? Row->GetDescription() : FText::GetEmpty()); }
}

void UTN_PauseMenuWidget::RefreshHint()
{
	if (!HintText)
	{
		return;
	}
	switch (Page)
	{
	case ETNPausePage::Settings:
		HintText->SetText(NSLOCTEXT("TNPause", "HintSettings", "← →  Cambiar      Q E · LB RB  Pestañas      Esc · B  Volver      Tab · Start  Cerrar"));
		break;
	case ETNPausePage::Controls:
		HintText->SetText(NSLOCTEXT("TNPause", "HintControls", "Intro · A  Cambiar      Supr · Y  De serie      Esc · B  Volver      Tab · Start  Cerrar"));
		break;
	case ETNPausePage::Room:
		HintText->SetText(NSLOCTEXT("TNPause", "HintRoom", "Intro · A  Elegir      ← →  Cambiar      Esc · B  Volver      Tab · Start  Cerrar"));
		break;
	case ETNPausePage::Credits:
		HintText->SetText(NSLOCTEXT("TNCredits", "HintPage", "↑ ↓ · LB RB  Desplazar      Esc · B  Volver      Tab · Start  Cerrar"));
		break;
	default:
		HintText->SetText(NSLOCTEXT("TNPause", "HintHome", "Intro · A  Elegir      Esc · B  Continuar      Tab · Start  Cerrar"));
		break;
	}
}

void UTN_PauseMenuWidget::OnLanguageChanged()
{
	// Los rótulos son FText de la localización y se traducen solos; aquí, lo que el menú compone como texto: la cabecera, el pie
	// y la ayuda de la fila enfocada. No se rehace la lista para no perder el foco ni el desplazamiento.
	RefreshHeader();
	RefreshHint();
	if (UTN_PauseRow* Row = LastFocused.Get()) { HandleRowFocused(Row); }
}

void UTN_PauseMenuWidget::PlayUISound(ETNPauseSound Sound, float Pitch)
{
	// Sin ametrallar: como mucho un «pom» cada 60 ms (arrastrar un deslizador suena a escala).
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

void UTN_PauseMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Clock += InDeltaTime;

	InfoTimer -= InDeltaTime;
	if (InfoTimer <= 0.f)
	{
		InfoTimer = 0.5f;
		RefreshHeader();
		// Pestaña de voz: si alguien entra o sale, se rehace (sin robar el foco si no hace falta).
		if (Page == ETNPausePage::Settings && Tab == ETNPauseTab::Voice && !IsConfirmOpen())
		{
			TArray<TWeakObjectPtr<APlayerState>> Now;
			const UWorld* World = GetWorld();
			const AGameStateBase* State = World ? World->GetGameState() : nullptr;
			const APlayerState* Mine = GetOwningPlayerState();
			if (State) { for (APlayerState* PS : State->PlayerArray) { if (PS && PS != Mine) { Now.Add(PS); } } }
			if (Now != VoiceTabPlayers)
			{
				FillTab();
				FocusFirstOfPage();
			}
		}
		// Página «Sala»: si alguien entra o sale, o la sala se cierra o se abre, se rehace (FillRoomList conserva el foco).
		if (Page == ETNPausePage::Room && !IsConfirmOpen() && BuildRoomSignature() != RoomListSignature)
		{
			FillRoomList();
		}
	}
	UpdateVoiceIcons();

	// Aviso de un momento: se apaga solo (el último medio segundo, desvaneciéndose).
	if (NoticeTime > 0.f && NoticeText)
	{
		NoticeTime -= InDeltaTime;
		NoticeText->SetRenderOpacity(FMath::Clamp(NoticeTime / 0.5f, 0.f, 1.f));
		if (NoticeTime <= 0.f) { NoticeText->SetVisibility(ESlateVisibility::Collapsed); }
	}

	// «Pulsa una tecla...»: si no llega ninguna, se deja como estaba.
	if (IsCapturingKey())
	{
		CaptureElapsed += InDeltaTime;
		if (!CaptureRow.IsValid()) { CancelKeyCapture(true); }
		else if (CaptureElapsed > TNPauseUI::KeyCaptureTimeout) { CancelKeyCapture(false); }
	}

	// Cuenta atrás del cuadro de la resolución: al llegar a cero, se deshace sola.
	if (ConfirmCountdown > 0.f && IsConfirmOpen())
	{
		const int32 Before = FMath::CeilToInt(ConfirmCountdown);
		ConfirmCountdown -= InDeltaTime;
		if (ConfirmCountdown <= 0.f)
		{
			CloseConfirm(false);
		}
		else if (ConfirmText && FMath::CeilToInt(ConfirmCountdown) != Before)
		{
			ConfirmText->SetText(FText::Format(ConfirmBaseText, FText::AsNumber(FMath::CeilToInt(ConfirmCountdown))));
		}
	}

	// Se cura solo si algo le quita la entrada mientras está abierto (reaparecer, otra pantalla que se cierra...).
	APlayerController* PC = GetOwningPlayer();
	if (bInputTaken && !bLeaving && PC)
	{
		if (!PC->ShouldShowMouseCursor()) { ApplyMenuInputMode(); }
		if (!PC->IsMoveInputIgnored()) { PC->SetIgnoreMoveInput(true); ++IgnoreMoveApplied; }
		if (!PC->IsLookInputIgnored()) { PC->SetIgnoreLookInput(true); ++IgnoreLookApplied; }
		// Sin ninguna opción enfocada (clic en el velo, cambio de ventana, reaparecer...), el foco vuelve a la última;
		// con la consola abierta, no se le quita.
		const UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
		const bool bConsoleOpen = Viewport && Viewport->ViewportConsole && Viewport->ViewportConsole->ConsoleActive();
		if (!bConsoleOpen && !HasFocusedDescendants())
		{
			// La última solo si sigue en el menú (al cambiar de pestaña, sus filas se quitan).
			UTN_PauseRow* Row = LastFocused.Get();
			if (Row && Row->GetParent() && Row->IsVisible()) { FocusRow(Row); }
			else { FocusFirstOfPage(); }
		}
	}
}

FReply UTN_PauseMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	using TNPauseUI::IsKey;
	const FKey Key = InKeyEvent.GetKey();
	if (IsKey(Key, { EKeys::Escape, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_Special_Left, EKeys::Virtual_Back, EKeys::BackSpace }))
	{
		if (!InKeyEvent.IsRepeat()) { GoBack(); }
		return FReply::Handled();
	}
	if (IsKey(Key, { EKeys::Tab, EKeys::Gamepad_Special_Right }))
	{
		if (!InKeyEvent.IsRepeat())
		{
			if (IsConfirmOpen()) { CloseConfirm(false); }
			else { CloseMenu(); }
		}
		return FReply::Handled();
	}
	if (Page == ETNPausePage::Settings && !IsConfirmOpen())
	{
		const int32 Count = static_cast<int32>(ETNPauseTab::Count);
		if (IsKey(Key, { EKeys::Q, EKeys::Gamepad_LeftShoulder }))
		{
			PlayUISound(ETNPauseSound::Press, 0.f);
			ShowTab(Count > 0 ? StepTab(-1) : Tab);
			return FReply::Handled();
		}
		if (IsKey(Key, { EKeys::E, EKeys::Gamepad_RightShoulder }))
		{
			PlayUISound(ETNPauseSound::Press, 0.f);
			ShowTab(Count > 0 ? StepTab(1) : Tab);
			return FReply::Handled();
		}
	}
	// Las flechas y la cruceta, a la navegación de Slate.
	if (IsKey(Key, { EKeys::Up, EKeys::Down, EKeys::Left, EKeys::Right, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Down,
		EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right }))
	{
		return FReply::Unhandled();
	}
	// La consola y la tecla de pulsar para hablar siguen llegando al juego.
	if (GetDefault<UInputSettings>()->ConsoleKeys.Contains(Key))
	{
		return FReply::Unhandled();
	}
	if (const UTN_GameSettingsSubsystem* Settings = GetSettings())
	{
		const FTNGameSettings& Data = Settings->GetEditedSettings();
		// La tecla o el botón elegidos para el menú lo cierran, como Tab y Start.
		if ((!Data.PauseKey.IsNone() && Key.GetFName() == Data.PauseKey) || (!Data.PausePadKey.IsNone() && Key.GetFName() == Data.PausePadKey))
		{
			if (!InKeyEvent.IsRepeat())
			{
				if (IsConfirmOpen()) { CloseConfirm(false); }
				else { CloseMenu(); }
			}
			return FReply::Handled();
		}
		if (Data.bPushToTalk && (Key.GetFName() == Data.PushToTalkKey || Key.GetFName() == Data.PushToTalkPadKey))
		{
			return FReply::Unhandled();
		}
	}
	// Lo demás se queda en el menú: que la tortuga no salte, no se meta en el caparazón ni abra la tienda.
	return FReply::Handled();
}

FReply UTN_PauseMenuWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// Esperando una tecla: la primera que se pulse es la nueva (antes de que la use la fila o la navegación).
	if (!IsCapturingKey())
	{
		return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
	}
	if (InKeyEvent.IsRepeat())
	{
		return FReply::Handled();
	}
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Left)
	{
		CancelKeyCapture(false);
		return FReply::Handled();
	}
	// Un roce del stick o un eje no cuentan: se sigue esperando.
	if (!UTN_GameSettingsSubsystem::IsIgnoredWhileCapturing(Key))
	{
		FinishKeyCapture(Key);
	}
	return FReply::Handled();
}

FReply UTN_PauseMenuWidget::NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// El menú de un invitado de la partida local lo maneja solo su mando: el ratón (del jugador 1) no hace nada.
	if (IsGuestMenu())
	{
		return FReply::Handled();
	}
	// Esperando una tecla: un botón del ratón también vale (el clic que empezó a esperar ya pasó: las filas se pulsan al soltar).
	if (!IsCapturingKey())
	{
		return Super::NativeOnPreviewMouseButtonDown(InGeometry, InMouseEvent);
	}
	FinishKeyCapture(InMouseEvent.GetEffectingButton());
	return FReply::Handled();
}

FReply UTN_PauseMenuWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Un clic fuera de las opciones no llega al juego y el foco vuelve a la última opción.
	UTN_PauseRow* Row = LastFocused.Get();
	if (Row && Row->GetParent() && Row->IsVisible()) { FocusRow(Row); }
	return FReply::Handled();
}

FReply UTN_PauseMenuWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Las listas usan la rueda antes; lo que sobra no cambia la cámara del espectador.
	return FReply::Handled();
}

FReply UTN_PauseMenuWidget::NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent)
{
	// El stick izquierdo mueve el foco (navegación de Slate); los gatillos y el stick derecho no llegan al juego. Esperando
	// una tecla, nada se mueve.
	const FKey Key = InAnalogEvent.GetKey();
	if (!IsCapturingKey() && (Key == EKeys::Gamepad_LeftX || Key == EKeys::Gamepad_LeftY))
	{
		return Super::NativeOnAnalogValueChanged(InGeometry, InAnalogEvent);
	}
	return FReply::Handled();
}

FNavigationReply UTN_PauseMenuWidget::NativeOnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent,
	const FNavigationReply& InDefaultReply)
{
	// Ajustes: arriba desde la primera fila de la lista lleva a la pestaña abierta (Slate, por la geometría, elegiría cualquiera
	// de la barra), así izquierda y derecha siguen desde ella.
	const UTN_PauseRow* Focused = LastFocused.Get();
	if (Page == ETNPausePage::Settings && !IsConfirmOpen() && InNavigationEvent.GetNavigationType() == EUINavigation::Up
		&& SettingsList && Focused && Focused->GetParent() == SettingsList && TabRows.IsValidIndex(static_cast<int32>(Tab)) && TabRows[static_cast<int32>(Tab)])
	{
		const UTN_PauseRow* FirstRow = nullptr;
		for (int32 i = 0; i < SettingsList->GetChildrenCount() && !FirstRow; ++i)
		{
			FirstRow = Cast<UTN_PauseRow>(SettingsList->GetChildAt(i));
		}
		if (FirstRow == Focused)
		{
			return FNavigationReply::Explicit(TabRows[static_cast<int32>(Tab)]->TakeWidget());
		}
	}
	return Super::NativeOnNavigation(MyGeometry, InNavigationEvent, InDefaultReply);
}

// ─────────────────────────────────────────────────────────────────────────────
// Menú: contexto
// ─────────────────────────────────────────────────────────────────────────────

UTN_GameSettingsSubsystem* UTN_PauseMenuWidget::GetSettings() const
{
	return UTN_GameSettingsSubsystem::Get(this);
}

bool UTN_PauseMenuWidget::IsHost() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

bool UTN_PauseMenuWidget::IsInLobby() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	return State && State->GameModeClass && State->GameModeClass->IsChildOf(ATN_HQGameMode::StaticClass());
}

bool UTN_PauseMenuWidget::HasRoomPage() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Standalone && Cast<UMP_GameInstance>(GetGameInstance()) != nullptr;
}

bool UTN_PauseMenuWidget::IsGuestMenu() const
{
	return UTN_LocalPlaySubsystem::IsGuest(GetOwningPlayer());
}

bool UTN_PauseMenuWidget::IsLocalGame() const
{
	return UTN_LocalPlaySubsystem::IsLocalGame(this);
}

bool UTN_PauseMenuWidget::IsTabAvailable(ETNPauseTab InTab) const
{
	if (InTab == ETNPauseTab::Count)
	{
		return false;
	}
	if (IsGuestMenu())
	{
		return InTab == ETNPauseTab::Controls || InTab == ETNPauseTab::Game;
	}
	return !(InTab == ETNPauseTab::Voice && IsLocalGame());
}

ETNPauseTab UTN_PauseMenuWidget::StepTab(ETNPauseTab From, int32 Direction) const
{
	const int32 Count = static_cast<int32>(ETNPauseTab::Count);
	const int32 Step = Direction < 0 ? Count - 1 : 1;
	int32 Index = static_cast<int32>(From);
	for (int32 Tries = 0; Tries < Count; ++Tries)
	{
		Index = (Index + Step) % Count;
		if (IsTabAvailable(static_cast<ETNPauseTab>(Index)))
		{
			return static_cast<ETNPauseTab>(Index);
		}
	}
	return Tab;
}

bool UTN_PauseMenuWidget::CanReturnToLobby() const
{
	// Solo desde una partida (Run, mapa procedural, carrera en la playa): en el lobby ya se está y el nivel de solo
	// terreno no sale de un lobby.
	const UWorld* World = GetWorld();
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	return State && State->GameModeClass && (State->GameModeClass->IsChildOf(ATN_RunGameMode::StaticClass())
		|| State->GameModeClass->IsChildOf(ATN_RallyGameMode::StaticClass()));
}
