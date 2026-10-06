#include "UI/Shop/TN_ShopWidgets.h"
#include "Core/TN_GameplayPreload.h"
#include "TN_ShopArt.h"
#include "../HUD/TN_HUDFaces.h"
#include "../HUD/TN_HUDStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Audio/TN_MusicSynthComponent.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_LocText.h"
#include "Core/TN_CosmeticLook.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Lobby/TN_ChangingBooth.h"
#include "Lobby/TN_CosmeticPreview.h"
#include "Lobby/TN_ShopKeeper.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Player/MP_GamePlayerController.h"

namespace TNShopUI
{
	/** Exposición de las capturas del escaparate (SceneColorHDR) en la UI. */
	constexpr float CaptureExposure = 1.6f;
	/** Márgenes de caja de las texturas de TNShopArt y TNHUDArt. */
	const FMargin CardBoxMargin(0.16f, 0.2f, 0.16f, 0.34f);
	const FMargin RibbonBoxMargin(0.14f, 0.f, 0.14f, 0.f);
	const FMargin ItemBoxMargin(0.3f, 0.3f, 0.3f, 0.3f);
	const FMargin PillBoxMargin(0.4f, 0.f, 0.4f, 0.f);
	const FMargin SandBoxMargin(0.3f, 0.3f, 0.3f, 0.3f);
	const FMargin BubbleBoxMargin(0.26f, 0.3f, 0.18f, 0.45f);
	/** Colores de las etiquetas y botones (sRGB de la paleta de TNHUDArt). */
	const FLinearColor InkColor = TNHUDArt::Ink;
	const FLinearColor CreamColor = TNHUDArt::Cream;
	const FLinearColor OwnedColor = TNHUDArt::Hex(0x2E9E48);
	const FLinearColor PriceColor = TNHUDArt::Hex(0xD9432F);
	const FLinearColor WornColor = TNHUDArt::Hex(0x1E7FB0);

	/** Si ya es suyo. El buggy va siempre con el perfil guardado; lo demás, con el de este jugador (invitado local incluido). */
	bool IsOwned(const UMP_GameInstance* GI, const APlayerController* PC, ETNCosmeticCategory Category, FName Id)
	{
		return TNIsBuggyCategory(Category) ? GI->IsCosmeticUnlocked(Category, Id) : GI->IsCosmeticUnlockedFor(PC, Category, Id);
	}

	/** Conchas con las que se paga: el buggy, con las del perfil guardado (como RequestPurchaseCosmetic); lo demás, con las de este jugador. */
	int32 Balance(const UMP_GameInstance* GI, const APlayerController* PC, ETNCosmeticCategory Category)
	{
		return TNIsBuggyCategory(Category) ? GI->GetAccumulatedRaceScore() : GI->GetAccumulatedRaceScoreFor(PC);
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

	UImage* Picture(UWidgetTree* Tree, UTexture2D* Tex, const FVector2D& ImageSize)
	{
		UImage* Out = New<UImage>(Tree);
		FSlateBrush Brush;
		Brush.SetResourceObject(Tex);
		Brush.ImageSize = ImageSize;
		Out->SetBrush(Brush);
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

	/** Cartel que se estira como caja con el contenido dentro. */
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

	UCanvasPanelSlot* Pin(UCanvasPanel* Canvas, UWidget* W, const FVector2D& Anchor, const FVector2D& Offset)
	{
		UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(W);
		CanvasSlot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
		CanvasSlot->SetAlignment(Anchor);
		CanvasSlot->SetPosition(Offset);
		CanvasSlot->SetAutoSize(true);
		return CanvasSlot;
	}

	UHorizontalBoxSlot* AddH(UHorizontalBox* Box, UWidget* W, const FMargin& Padding = FMargin(0.f), EVerticalAlignment V = VAlign_Center)
	{
		UHorizontalBoxSlot* HSlot = Box->AddChildToHorizontalBox(W);
		HSlot->SetPadding(Padding);
		HSlot->SetVerticalAlignment(V);
		return HSlot;
	}

	UVerticalBoxSlot* AddV(UVerticalBox* Box, UWidget* W, const FMargin& Padding = FMargin(0.f), EHorizontalAlignment H = HAlign_Fill)
	{
		UVerticalBoxSlot* VSlot = Box->AddChildToVerticalBox(W);
		VSlot->SetPadding(Padding);
		VSlot->SetHorizontalAlignment(H);
		return VSlot;
	}

	bool IsKey(const FKey& Key, std::initializer_list<FKey> Options)
	{
		for (const FKey& Option : Options) { if (Key == Option) { return true; } }
		return false;
	}

	/** Pestañas de la tienda; la del buggy (BuggyModel) enseña también las pinturas. */
	const TArray<ETNCosmeticCategory>& Categories()
	{
		static const TArray<ETNCosmeticCategory> List = { ETNCosmeticCategory::Helmet, ETNCosmeticCategory::Shell, ETNCosmeticCategory::Body,
			ETNCosmeticCategory::Eyes, ETNCosmeticCategory::BuggyModel };
		return List;
	}

	/** Categorías de una pestaña: la del buggy, modelos y luego pinturas. */
	TArray<ETNCosmeticCategory> TabCategories(ETNCosmeticCategory Tab)
	{
		if (TNIsBuggyCategory(Tab)) { return { ETNCosmeticCategory::BuggyModel, ETNCosmeticCategory::BuggyPaint }; }
		return { Tab };
	}

	/** Filas del probador: la página de la tortuga y la del buggy. */
	const TArray<ETNCosmeticCategory>& TurtleRows()
	{
		static const TArray<ETNCosmeticCategory> List = { ETNCosmeticCategory::Helmet, ETNCosmeticCategory::Shell, ETNCosmeticCategory::Body, ETNCosmeticCategory::Eyes };
		return List;
	}

	const TArray<ETNCosmeticCategory>& BuggyRows()
	{
		static const TArray<ETNCosmeticCategory> List = { ETNCosmeticCategory::BuggyModel, ETNCosmeticCategory::BuggyPaint };
		return List;
	}

	constexpr int32 NumCategories = static_cast<int32>(ETNCosmeticCategory::BuggyPaint) + 1;

	FText CategoryTitle(ETNCosmeticCategory Category)
	{
		switch (Category)
		{
		case ETNCosmeticCategory::Helmet:     return NSLOCTEXT("Tortunabo", "ShopTabHelmets", "CASCOS");
		case ETNCosmeticCategory::Shell:      return NSLOCTEXT("Tortunabo", "ShopTabShells", "CAPARAZONES");
		case ETNCosmeticCategory::Eyes:       return NSLOCTEXT("Tortunabo", "ShopTabEyes", "OJOS");
		case ETNCosmeticCategory::BuggyModel:
		case ETNCosmeticCategory::BuggyPaint: return NSLOCTEXT("Tortunabo", "ShopTabBuggy", "BUGGY");
		default:                              return NSLOCTEXT("Tortunabo", "ShopTabBodies", "COLORES");
		}
	}

	FText RowTitle(ETNCosmeticCategory Category)
	{
		switch (Category)
		{
		case ETNCosmeticCategory::Helmet:     return NSLOCTEXT("Tortunabo", "BoothRowHelmet", "CASCO");
		case ETNCosmeticCategory::Shell:      return NSLOCTEXT("Tortunabo", "BoothRowShell", "CAPARAZÓN");
		case ETNCosmeticCategory::Eyes:       return NSLOCTEXT("Tortunabo", "BoothRowEyes", "OJOS");
		case ETNCosmeticCategory::BuggyModel: return NSLOCTEXT("Tortunabo", "BoothRowBuggyModel", "BUGGY");
		case ETNCosmeticCategory::BuggyPaint: return NSLOCTEXT("Tortunabo", "BoothRowBuggyPaint", "PINTURA");
		default:                              return NSLOCTEXT("Tortunabo", "BoothRowBody", "COLOR");
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Botón
// ─────────────────────────────────────────────────────────────────────────────

void UTN_ShopButton::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree || Frame) { return; }
	LabelText = TNShopUI::Label(WidgetTree, FText::GetEmpty(), TEXT("Bold"), 20, TNShopUI::CreamColor, true);
	LabelText->SetJustification(ETextJustify::Center);
	Frame = TNShopUI::Framed(WidgetTree, nullptr, TNShopUI::PillBoxMargin, LabelText, FMargin(26.f, 10.f, 26.f, 14.f));
	Frame->SetHorizontalAlignment(HAlign_Center);
	Frame->SetVerticalAlignment(VAlign_Center);
	Sizer = TNShopUI::Sized(WidgetTree, Frame, 0.f, 0.f);
	WidgetTree->RootWidget = Sizer;
	SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
}

void UTN_ShopButton::Setup(const FText& Label, UTexture2D* Art, const FLinearColor& TextColor, int32 FontSize, const FVector2D& MinSize, TFunction<void()> InOnClick)
{
	OnClick = MoveTemp(InOnClick);
	if (LabelText)
	{
		LabelText->SetText(Label);
		TNHUDStyle::StyleText(LabelText, TEXT("Bold"), FontSize, TextColor, true);
	}
	if (Sizer)
	{
		Sizer->SetMinDesiredWidth(MinSize.X);
		Sizer->SetMinDesiredHeight(MinSize.Y);
	}
	SetArt(Art);
}

void UTN_ShopButton::SetLabel(const FText& Label)
{
	if (LabelText) { LabelText->SetText(Label); }
}

void UTN_ShopButton::SetArt(UTexture2D* Art)
{
	if (Frame) { Frame->SetBrush(TNShopUI::BoxBrush(Art, TNShopUI::PillBoxMargin)); }
}

void UTN_ShopButton::SetDisabled(bool bInDisabled)
{
	bDisabled = bInDisabled;
	SetRenderOpacity(bDisabled ? 0.55f : 1.f);
	ApplyScale(1.f);
}

void UTN_ShopButton::ApplyScale(float Scale)
{
	SetRenderScale(FVector2D(Scale, Scale));
}

FReply UTN_ShopButton::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) { return FReply::Unhandled(); }
	bPressed = true;
	if (!bDisabled) { ApplyScale(0.95f); }
	return FReply::Handled();
}

FReply UTN_ShopButton::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// El segundo clic de un doble clic llega por aquí y no como otro «Down»: sin esto, al soltarlo no se pulsaría.
	return NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UTN_ShopButton::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) { return FReply::Unhandled(); }
	const bool bWasPressed = bPressed;
	bPressed = false;
	ApplyScale(IsHovered() && !bDisabled ? 1.06f : 1.f);
	if (bWasPressed && !bDisabled && OnClick) { OnClick(); }
	return FReply::Handled();
}

void UTN_ShopButton::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	if (!bDisabled) { ApplyScale(1.06f); }
}

void UTN_ShopButton::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bPressed = false;
	ApplyScale(1.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Carta del catálogo
// ─────────────────────────────────────────────────────────────────────────────

void UTN_ShopCard::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree || Frame) { return; }
	UVerticalBox* Column = TNShopUI::New<UVerticalBox>(WidgetTree);
	ThumbImage = TNShopUI::New<UImage>(WidgetTree);
	TNShopUI::AddV(Column, TNShopUI::Sized(WidgetTree, ThumbImage, 132.f, 132.f), FMargin(0.f), HAlign_Center);
	NameText = TNShopUI::Label(WidgetTree, FText::GetEmpty(), TEXT("Bold"), 14, TNShopUI::InkColor, false);
	NameText->SetJustification(ETextJustify::Center);
	NameText->SetAutoWrapText(true);
	TNShopUI::AddV(Column, TNShopUI::Sized(WidgetTree, NameText, 150.f, 40.f), FMargin(0.f, 2.f, 0.f, 0.f), HAlign_Center);
	TagText = TNShopUI::Label(WidgetTree, FText::GetEmpty(), TEXT("Black"), 14, TNShopUI::PriceColor, false);
	TagText->SetJustification(ETextJustify::Center);
	TNShopUI::AddV(Column, TagText, FMargin(0.f, 0.f, 0.f, 2.f), HAlign_Center);
	Frame = TNShopUI::Framed(WidgetTree, TNShopArt::ItemCard(false), TNShopUI::ItemBoxMargin, Column, FMargin(12.f, 12.f, 12.f, 12.f));
	WidgetTree->RootWidget = TNShopUI::Sized(WidgetTree, Frame, 178.f, 222.f);
	SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
}

void UTN_ShopCard::Setup(UTextureRenderTarget2D* Thumb, const FText& Name, TFunction<void()> InOnClick)
{
	OnClick = MoveTemp(InOnClick);
	if (NameText) { NameText->SetText(Name); }
	if (ThumbImage && Thumb)
	{
		if (UMaterialInterface* Base = TNPreload::PreviewMaterial())
		{
			ThumbMID = UMaterialInstanceDynamic::Create(Base, this);
			ThumbMID->SetTextureParameterValue(TEXT("Capture"), Thumb);
			ThumbMID->SetScalarParameterValue(TEXT("Exposure"), TNShopUI::CaptureExposure);
			ThumbImage->SetBrushFromMaterial(ThumbMID);
		}
	}
}

void UTN_ShopCard::SetState(bool bInSelected, const FText& Tag, const FLinearColor& TagColor)
{
	bSelected = bInSelected;
	if (TagText)
	{
		TagText->SetText(Tag);
		TagText->SetColorAndOpacity(FSlateColor(TagColor));
	}
	RefreshLook();
}

void UTN_ShopCard::RefreshLook()
{
	if (Frame) { Frame->SetBrush(TNShopUI::BoxBrush(TNShopArt::ItemCard(bSelected), TNShopUI::ItemBoxMargin)); }
	const float Scale = bSelected ? 1.06f : (bHover ? 1.03f : 1.f);
	SetRenderScale(FVector2D(Scale, Scale));
}

FReply UTN_ShopCard::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) { return FReply::Unhandled(); }
	if (OnClick) { OnClick(); }
	return FReply::Handled();
}

void UTN_ShopCard::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	bHover = true;
	RefreshLook();
}

void UTN_ShopCard::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bHover = false;
	RefreshLook();
}

// ─────────────────────────────────────────────────────────────────────────────
// Base: vista previa, foco y cierre
// ─────────────────────────────────────────────────────────────────────────────

void UTN_CosmeticMenuBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
}

void UTN_CosmeticMenuBase::NativeConstruct()
{
	Super::NativeConstruct();
	Preview = ATN_CosmeticPreview::GetFor(GetOwningPlayer());
	if (ATN_CosmeticPreview* Stage = Preview.Get())
	{
		Stage->SetLiveCapture(true);
		if (PreviewMID) { PreviewMID->SetTextureParameterValue(TEXT("Capture"), Stage->GetRenderTarget()); }
	}
	// Música del menú en 2D (en el PlayerController): la de la tienda o la del probador; la radio del puesto baja.
	if (APlayerController* PC = GetOwningPlayer())
	{
		UTN_MusicSynthComponent* MenuMusic = PC->FindComponentByClass<UTN_MusicSynthComponent>();
		if (!MenuMusic) { MenuMusic = UTN_MusicSynthComponent::AttachMusic2D(PC); }
		if (MenuMusic) { MenuMusic->PlayTrack(IsA<UTN_BoothWidget>() ? ETNMusicTrack::Booth : ETNMusicTrack::Shop); }
	}
	ATN_ShopKeeper::SetRadiosDucked(GetWorld(), true);
	SetKeyboardFocus();
}

void UTN_CosmeticMenuBase::NativeDestruct()
{
	if (ATN_CosmeticPreview* Stage = Preview.Get()) { Stage->SetLiveCapture(false); }
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (UTN_MusicSynthComponent* MenuMusic = PC->FindComponentByClass<UTN_MusicSynthComponent>()) { MenuMusic->StopMusic(); }
	}
	ATN_ShopKeeper::SetRadiosDucked(GetWorld(), false);
	Super::NativeDestruct();
}

AMP_GamePlayerController* UTN_CosmeticMenuBase::GetTNPC() const
{
	return Cast<AMP_GamePlayerController>(GetOwningPlayer());
}

UMP_GameInstance* UTN_CosmeticMenuBase::GetTNGI() const
{
	return Cast<UMP_GameInstance>(GetGameInstance());
}

FTN_TurtleLook UTN_CosmeticMenuBase::GetWornLook() const
{
	FTN_TurtleLook Worn;
	const APlayerController* PC = GetOwningPlayer();
	if (const ATN_CoopPlayerState* TNPS = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr)
	{
		Worn.HelmetId = TNPS->EquippedHelmetId;
		Worn.ShellId = TNPS->EquippedShellId;
		Worn.SkinId = TNPS->EquippedSkinId;
		Worn.EyesId = TNPS->EquippedEyesId;
	}
	else if (const UMP_GameInstance* GI = GetTNGI())
	{
		Worn.HelmetId = GI->GetEquippedHelmetIdFor(PC);
		Worn.ShellId = GI->GetEquippedShellIdFor(PC);
		Worn.SkinId = GI->GetEquippedSkinIdFor(PC);
		Worn.EyesId = GI->GetEquippedEyesIdFor(PC);
	}
	return Worn;
}

FTN_BuggyLook UTN_CosmeticMenuBase::GetWornBuggyLook() const
{
	const APlayerController* PC = GetOwningPlayer();
	if (const ATN_CoopPlayerState* TNPS = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr)
	{
		return TNPS->EquippedBuggyLook;
	}
	const UMP_GameInstance* GI = GetTNGI();
	return GI ? GI->GetEquippedBuggyLook() : FTN_BuggyLook();
}

UTextureRenderTarget2D* UTN_CosmeticMenuBase::Thumbnail(ETNCosmeticCategory Category, FName Id) const
{
	ATN_CosmeticPreview* Stage = Preview.IsValid() ? Preview.Get() : ATN_CosmeticPreview::GetFor(GetOwningPlayer());
	return Stage ? Stage->GetThumbnail(Category, Id) : nullptr;
}

UMaterialInstanceDynamic* UTN_CosmeticMenuBase::MakeCaptureMID(UTextureRenderTarget2D* RT)
{
	UMaterialInterface* Base = TNPreload::PreviewMaterial();
	UMaterialInstanceDynamic* MID = Base ? UMaterialInstanceDynamic::Create(Base, this) : nullptr;
	if (MID)
	{
		MID->SetScalarParameterValue(TEXT("Exposure"), TNShopUI::CaptureExposure);
		if (RT) { MID->SetTextureParameterValue(TEXT("Capture"), RT); }
	}
	return MID;
}

UWidget* UTN_CosmeticMenuBase::MakePreviewPanel(float PanelSize)
{
	UVerticalBox* Column = TNShopUI::New<UVerticalBox>(WidgetTree);
	PreviewImage = TNShopUI::New<UImage>(WidgetTree);
	PreviewMID = MakeCaptureMID(nullptr);
	if (PreviewMID) { PreviewImage->SetBrushFromMaterial(PreviewMID); }
	TNShopUI::AddV(Column, TNShopUI::Sized(WidgetTree, PreviewImage, PanelSize, PanelSize), FMargin(0.f), HAlign_Center);
	UTextBlock* Hint = TNShopUI::Label(WidgetTree, NSLOCTEXT("Tortunabo", "PreviewHint", "Arrastra para girar"), TEXT("Bold"), 15, TNHUDArt::SeaLight, true);
	TNShopUI::AddV(Column, Hint, FMargin(0.f, -6.f, 0.f, 4.f), HAlign_Center);
	return TNShopUI::Framed(WidgetTree, TNHUDArt::CardTexture(), TNShopUI::CardBoxMargin, Column, FMargin(24.f, 18.f, 24.f, 40.f));
}

void UTN_CosmeticMenuBase::CloseMenu()
{
	if (AMP_GamePlayerController* PC = GetTNPC()) { PC->CloseShopUI(); }
	else { RemoveFromParent(); }
}

FReply UTN_CosmeticMenuBase::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (HandleKey(Key)) { return FReply::Handled(); }
	if (TNShopUI::IsKey(Key, { EKeys::Escape, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_Special_Right }))
	{
		CloseMenu();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UTN_CosmeticMenuBase::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && PreviewImage
		&& PreviewImage->GetCachedGeometry().IsUnderLocation(InMouseEvent.GetScreenSpacePosition()))
	{
		bDragging = true;
		LastMouse = InMouseEvent.GetScreenSpacePosition();
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UTN_CosmeticMenuBase::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UTN_CosmeticMenuBase::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging)
	{
		const FVector2D Now = InMouseEvent.GetScreenSpacePosition();
		if (ATN_CosmeticPreview* Stage = Preview.Get()) { Stage->AddSpin(-(Now.X - LastMouse.X) * 0.45f); }
		LastMouse = Now;
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tienda
// ─────────────────────────────────────────────────────────────────────────────

void UTN_ShopWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

void UTN_ShopWidget::BuildTree()
{
	if (!WidgetTree || WidgetTree->RootWidget) { return; }
	UWidgetTree* Tree = WidgetTree;
	UCanvasPanel* Canvas = TNShopUI::New<UCanvasPanel>(Tree);
	Tree->RootWidget = Canvas;

	// Velo azul marino sobre el juego.
	UImage* Veil = TNShopUI::New<UImage>(Tree);
	Veil->SetColorAndOpacity(TNHUDArt::Hex(0x0A1C38, 0.62f));
	if (UCanvasPanelSlot* VeilSlot = Canvas->AddChildToCanvas(Veil))
	{
		VeilSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		VeilSlot->SetOffsets(FMargin(0.f));
	}

	// Título en cinta coral.
	TitleText = TNShopUI::Label(Tree, FText::GetEmpty(), TEXT("Black"), 34, TNShopUI::CreamColor, true);
	UBorder* Title = TNShopUI::Framed(Tree, TNHUDArt::RibbonTexture(), TNShopUI::RibbonBoxMargin, TitleText, FMargin(70.f, 8.f, 70.f, 14.f));
	Title->SetHorizontalAlignment(HAlign_Center);
	TNShopUI::Pin(Canvas, Title, FVector2D(0.5f, 0.f), FVector2D(0.f, 26.f));

	// Escaparate a la izquierda.
	TNShopUI::Pin(Canvas, MakePreviewPanel(600.f), FVector2D(0.04f, 0.56f), FVector2D::ZeroVector);

	// Columna derecha: tendero, pestañas, catálogo y botones.
	UVerticalBox* Right = TNShopUI::New<UVerticalBox>(Tree);
	{
		UHorizontalBox* Talk = TNShopUI::New<UHorizontalBox>(Tree);
		UVerticalBox* Who = TNShopUI::New<UVerticalBox>(Tree);
		TNShopUI::AddV(Who, TNShopUI::Picture(Tree, TNHUDFaces::TurtleFace(ETNTurtleFace::Happy), FVector2D(128.f, 128.f)), FMargin(0.f), HAlign_Center);
		KeeperNameText = TNShopUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 17, TNShopUI::CreamColor, true);
		UBorder* NameRibbon = TNShopUI::Framed(Tree, TNHUDArt::RibbonTexture(), TNShopUI::RibbonBoxMargin, KeeperNameText, FMargin(30.f, 4.f, 30.f, 9.f));
		NameRibbon->SetHorizontalAlignment(HAlign_Center);
		TNShopUI::AddV(Who, NameRibbon, FMargin(0.f, -14.f, 0.f, 0.f), HAlign_Center);
		TNShopUI::AddH(Talk, Who, FMargin(0.f, 0.f, 6.f, 0.f), VAlign_Top);
		DialogText = TNShopUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 21, TNShopUI::InkColor, false);
		DialogText->SetAutoWrapText(true);
		USizeBox* DialogSize = TNShopUI::Sized(Tree, DialogText, 640.f, 0.f);
		DialogSize->SetMinDesiredHeight(92.f);
		UBorder* Bubble = TNShopUI::Framed(Tree, TNHUDArt::ChatBubbleTexture(), TNShopUI::BubbleBoxMargin, DialogSize, FMargin(40.f, 16.f, 26.f, 30.f));
		TNShopUI::AddH(Talk, Bubble, FMargin(0.f, 8.f, 0.f, 0.f), VAlign_Top);
		TNShopUI::AddV(Right, Talk, FMargin(0.f, 0.f, 0.f, 10.f));
	}
	{
		UHorizontalBox* TabRow = TNShopUI::New<UHorizontalBox>(Tree);
		for (const ETNCosmeticCategory Category : TNShopUI::Categories())
		{
			UTN_ShopButton* TabButton = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
			TabButton->Setup(TNShopUI::CategoryTitle(Category), TNShopArt::Pill(0x2A5A92, 0x173A66), TNShopUI::CreamColor, 17, FVector2D(146.f, 56.f),
				[this, Category]() { ShowTab(Category); });
			TNShopUI::AddH(TabRow, TabButton, FMargin(0.f, 0.f, 8.f, 0.f));
			Tabs.Add(TabButton);
		}
		TNShopUI::AddV(Right, TabRow, FMargin(12.f, 0.f, 0.f, 8.f), HAlign_Left);
	}
	{
		Grid = TNShopUI::New<UWrapBox>(Tree);
		Grid->SetInnerSlotPadding(FVector2D(12.f, 14.f));
		Grid->SetWrapSize(4 * 190.f);
		Grid->SetExplicitWrapSize(true);
		GridScroll = TNShopUI::New<UScrollBox>(Tree);
		GridScroll->AddChild(Grid);
		UBorder* GridCard = TNShopUI::Framed(Tree, TNHUDArt::CardTexture(), TNShopUI::CardBoxMargin, TNShopUI::Sized(Tree, GridScroll, 4 * 190.f + 20.f, 486.f),
			FMargin(22.f, 20.f, 22.f, 40.f));
		TNShopUI::AddV(Right, GridCard, FMargin(0.f), HAlign_Left);
	}
	{
		UHorizontalBox* Bottom = TNShopUI::New<UHorizontalBox>(Tree);
		UHorizontalBox* Wallet = TNShopUI::New<UHorizontalBox>(Tree);
		TNShopUI::AddH(Wallet, TNShopUI::Picture(Tree, TNHUDArt::ShellIcon(), FVector2D(46.f, 46.f)), FMargin(0.f, 0.f, 6.f, 0.f));
		WalletText = TNShopUI::Label(Tree, FText::GetEmpty(), TEXT("Black"), 24, TNShopUI::InkColor, false);
		TNShopUI::AddH(Wallet, WalletText);
		UBorder* WalletTag = TNShopUI::Framed(Tree, TNHUDArt::SandTagTexture(), FMargin(0.2f, 0.f, 0.2f, 0.f), Wallet, FMargin(24.f, 6.f, 30.f, 10.f));
		TNShopUI::AddH(Bottom, WalletTag, FMargin(0.f, 0.f, 24.f, 0.f));
		BuyButton = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
		BuyButton->Setup(FText::GetEmpty(), TNShopArt::Pill(0xFFD95E, 0xF2A93B), TNShopUI::InkColor, 24, FVector2D(330.f, 66.f), [this]() { Buy(); });
		TNShopUI::AddH(Bottom, BuyButton, FMargin(0.f, 0.f, 16.f, 0.f));
		UTN_ShopButton* ExitButton = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
		ExitButton->Setup(NSLOCTEXT("Tortunabo", "ShopExit", "SALIR"), TNShopArt::Pill(0x3B6EA8, 0x1D3F6E), TNShopUI::CreamColor, 22, FVector2D(170.f, 66.f),
			[this]() { CloseMenu(); });
		TNShopUI::AddH(Bottom, ExitButton);
		TNShopUI::AddV(Right, Bottom, FMargin(10.f, 14.f, 0.f, 0.f), HAlign_Left);
		UTextBlock* Keys = TNShopUI::Label(Tree, NSLOCTEXT("Tortunabo", "ShopKeys", "Flechas: elegir · Q/E: pestaña · Intro: comprar · Esc: salir"),
			TEXT("Regular"), 14, TNHUDArt::SeaLight, true);
		TNShopUI::AddV(Right, Keys, FMargin(12.f, 8.f, 0.f, 0.f), HAlign_Left);
	}
	TNShopUI::Pin(Canvas, Right, FVector2D(0.97f, 0.56f), FVector2D::ZeroVector);
}

void UTN_ShopWidget::SetShop(ATN_ShopKeeper* InShop)
{
	Shop = InShop;
	const FText ShopTitle = InShop ? InShop->GetShopName() : NSLOCTEXT("Tortunabo", "ShopDefaultName", "La Concha Dorada");
	if (TitleText) { TitleText->SetText(ShopTitle.ToUpper()); }
	if (KeeperNameText) { KeeperNameText->SetText(InShop ? InShop->GetKeeperName() : NSLOCTEXT("Tortunabo", "ShopKeeperName", "Don Tortugo")); }
	if (ATN_CosmeticPreview* Stage = ATN_CosmeticPreview::GetFor(GetOwningPlayer())) { Stage->SetLook(GetWornLook()); }
	RefreshWallet();
	ShowTab(ETNCosmeticCategory::Helmet);

	const APlayerState* PS = GetOwningPlayer() ? GetOwningPlayer()->PlayerState : nullptr;
	const FText Who = PS ? TNLocText::Literal(PS->GetPlayerName()) : NSLOCTEXT("Tortunabo", "ShopSailor", "marinero");
	Say(FText::Format(NSLOCTEXT("Tortunabo", "ShopHelloBuggy", "¡Hola, {0}! Pasa, pasa: hoy los cascos, caparazones, colores y ojos son gratis. Los buggies del Rally y sus pinturas van por conchas. Pruébatelo todo luego en las botellas."),
		Who));
}

#if !UE_BUILD_SHIPPING
void UTN_ShopWidget::DebugShowTab(int32 Index, int32 Item)
{
	const TArray<ETNCosmeticCategory>& List = TNShopUI::Categories();
	ShowTab(List[FMath::Clamp(Index, 0, List.Num() - 1)]);
	if (Items.IsValidIndex(Item)) { Select(Item, true); }
}

void UTN_BoothWidget::DebugShowPage(int32 InPage, int32 Row, int32 Steps)
{
	ShowPage(InPage);
	FocusRow(Row);
	for (int32 i = 0; i < Steps; ++i) { Cycle(Row, 1); }
}
#endif

void UTN_ShopWidget::ShowTab(ETNCosmeticCategory Category)
{
	Tab = Category;
	const bool bBuggyTab = TNIsBuggyCategory(Category);
	for (int32 i = 0; i < Tabs.Num(); ++i)
	{
		const bool bActive = TNShopUI::Categories()[i] == Category;
		Tabs[i]->SetArt(bActive ? TNShopArt::Pill(0xFF8A70, 0xD9432F) : TNShopArt::Pill(0x2A5A92, 0x173A66));
	}
	Items.Reset();
	const UMP_GameInstance* GI = GetTNGI();
	for (const ETNCosmeticCategory Part : TNShopUI::TabCategories(Category))
	{
		Items.Add({ Part, NAME_None });
		if (GI) { for (const FName Id : GI->GetCosmeticCatalog(Part)) { Items.Add({ Part, Id }); } }
	}
	// En la pestaña del buggy, el escaparate enseña el buggy con la tortuga al volante.
	if (ATN_CosmeticPreview* Stage = ATN_CosmeticPreview::GetFor(GetOwningPlayer()))
	{
		Stage->SetBuggyMode(bBuggyTab);
		if (bBuggyTab) { Stage->SetBuggyLook(GetWornBuggyLook()); }
	}

	Grid->ClearChildren();
	Cards.Reset();
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		UTN_ShopCard* Card = CreateWidget<UTN_ShopCard>(this, UTN_ShopCard::StaticClass());
		Card->Setup(Thumbnail(Items[i].Category, Items[i].Id), UTN_CosmeticLook::GetDisplayName(this, Items[i].Category, Items[i].Id),
			[this, i]() { Select(i, true); });
		Grid->AddChildToWrapBox(Card);
		Cards.Add(Card);
	}
	GridScroll->ScrollToStart();
	// Empieza en lo que lleva puesto (en el buggy, su modelo).
	const FName WornId = bBuggyTab ? GetWornBuggyLook().Get(ETNCosmeticCategory::BuggyModel) : GetWornLook().Get(Category);
	const int32 WornIndex = Items.IndexOfByPredicate([&](const FTNShopItem& Item) { return Item.Category == (bBuggyTab ? ETNCosmeticCategory::BuggyModel : Category) && Item.Id == WornId; });
	RefreshWallet();
	Select(WornIndex == INDEX_NONE ? 0 : WornIndex, false);
}

FText UTN_ShopWidget::TagFor(const FTNShopItem& Item, FLinearColor& OutColor) const
{
	const UMP_GameInstance* GI = GetTNGI();
	const FName WornId = TNIsBuggyCategory(Item.Category) ? GetWornBuggyLook().Get(Item.Category) : GetWornLook().Get(Item.Category);
	if (WornId == Item.Id)
	{
		OutColor = TNShopUI::WornColor;
		return NSLOCTEXT("Tortunabo", "ShopTagWorn", "PUESTO");
	}
	if (!GI || TNShopUI::IsOwned(GI, GetOwningPlayer(), Item.Category, Item.Id))
	{
		OutColor = TNShopUI::OwnedColor;
		return NSLOCTEXT("Tortunabo", "ShopTagOwned", "¡TUYO!");
	}
	OutColor = TNShopUI::PriceColor;
	const int32 Price = GI->GetCosmeticPrice(Item.Category, Item.Id);
	return Price <= 0 ? NSLOCTEXT("Tortunabo", "ShopTagFree", "GRATIS")
		: FText::Format(NSLOCTEXT("Tortunabo", "ShopTagPrice", "{0} {0}|plural(one=concha,other=conchas)"), Price);
}

void UTN_ShopWidget::RefreshCards()
{
	for (int32 i = 0; i < Cards.Num(); ++i)
	{
		FLinearColor TagColor;
		const FText Tag = TagFor(Items[i], TagColor);
		Cards[i]->SetState(i == Selected, Tag, TagColor);
	}
}

void UTN_ShopWidget::RefreshBuyButton()
{
	if (!BuyButton || !Items.IsValidIndex(Selected)) { return; }
	const FTNShopItem& Item = Items[Selected];
	const UMP_GameInstance* GI = GetTNGI();
	const bool bOwned = !GI || TNShopUI::IsOwned(GI, GetOwningPlayer(), Item.Category, Item.Id);
	if (bOwned)
	{
		BuyButton->SetLabel(NSLOCTEXT("Tortunabo", "ShopBuyOwned", "¡YA ES TUYO!"));
		BuyButton->SetDisabled(true);
		return;
	}
	const int32 Price = GI->GetCosmeticPrice(Item.Category, Item.Id);
	BuyButton->SetLabel(Price <= 0 ? NSLOCTEXT("Tortunabo", "ShopBuyFree", "COMPRAR · GRATIS")
		: FText::Format(NSLOCTEXT("Tortunabo", "ShopBuyPrice", "COMPRAR · {0}"), FText::AsNumber(Price)));
	BuyButton->SetDisabled(Price > TNShopUI::Balance(GI, GetOwningPlayer(), Item.Category));
}

void UTN_ShopWidget::RefreshWallet()
{
	const UMP_GameInstance* GI = GetTNGI();
	if (WalletText) { WalletText->SetText(FText::AsNumber(GI ? TNShopUI::Balance(GI, GetOwningPlayer(), Tab) : 0)); }
}

void UTN_ShopWidget::Select(int32 Index, bool bSpeak)
{
	if (!Items.IsValidIndex(Index)) { return; }
	const bool bChanged = Index != Selected;
	Selected = Index;
	const FTNShopItem Item = Items[Index];
	if (ATN_CosmeticPreview* Stage = ATN_CosmeticPreview::GetFor(GetOwningPlayer()))
	{
		if (TNIsBuggyCategory(Item.Category))
		{
			// El buggy se prueba el modelo o la pintura que miras con el resto de lo que tiene puesto.
			FTN_BuggyLook TryingBuggy = GetWornBuggyLook();
			TryingBuggy.Set(Item.Category, Item.Id);
			Stage->SetBuggyLook(TryingBuggy);
		}
		else
		{
			// La tortuga se prueba lo que miras encima de lo que lleva.
			FTN_TurtleLook Trying = GetWornLook();
			Trying.Set(Item.Category, Item.Id);
			Stage->SetLook(Trying);
		}
		if (bSpeak && bChanged) { Stage->PlayPose(false); }
	}
	RefreshCards();
	RefreshBuyButton();
	if (bSpeak)
	{
		const FText Desc = UTN_CosmeticLook::GetDescription(this, Item.Category, Item.Id);
		Say(Desc.IsEmpty() ? UTN_CosmeticLook::GetDisplayName(this, Item.Category, Item.Id) : Desc);
	}
	// Que la carta elegida quede a la vista.
	if (Cards.IsValidIndex(Index)) { GridScroll->ScrollWidgetIntoView(Cards[Index], true, EDescendantScrollDestination::IntoView); }
}

void UTN_ShopWidget::Buy()
{
	if (!Items.IsValidIndex(Selected)) { return; }
	const FTNShopItem Item = Items[Selected];
	const FName Id = Item.Id;
	const bool bBuggy = TNIsBuggyCategory(Item.Category);
	UMP_GameInstance* GI = GetTNGI();
	if (Id == NAME_None)
	{
		Say(bBuggy ? NSLOCTEXT("Tortunabo", "ShopSerieBuggy", "Ese viene con el carnet de conducir: todas las tortugas del Rally lo tienen de serie.")
			: NSLOCTEXT("Tortunabo", "ShopSerie", "Eso ya viene de serie con tu caparazón. ¡Gratis desde que naciste!"));
		return;
	}
	if (GI && TNShopUI::IsOwned(GI, GetOwningPlayer(), Item.Category, Id))
	{
		Say(NSLOCTEXT("Tortunabo", "ShopAlready", "Ese ya es tuyo. Pruébatelo en las botellas: te queda de maravilla."));
		return;
	}
	AMP_GamePlayerController* PC = GetTNPC();
	if (PC && PC->RequestPurchaseCosmetic(Item.Category, Id))
	{
		Say(FText::Format(bBuggy ? NSLOCTEXT("Tortunabo", "ShopBoughtBuggy", "¡Hecho! {0} ya es tuyo. En la botella del probador, pasa a la página del buggy y póntelo.")
			: NSLOCTEXT("Tortunabo", "ShopBought", "¡Hecho! {0} ya es tuyo. Ve a una botella del probador y póntelo."),
			UTN_CosmeticLook::GetDisplayName(this, Item.Category, Id)));
		if (ATN_CosmeticPreview* Stage = ATN_CosmeticPreview::GetFor(GetOwningPlayer())) { Stage->PlayPose(true); }
		RefreshWallet();
		RefreshCards();
		RefreshBuyButton();
	}
	else
	{
		Say(NSLOCTEXT("Tortunabo", "ShopNoMoney", "Uy, te faltan conchas para esto. ¡Vuelve después de otra carrera!"));
	}
}

void UTN_ShopWidget::Say(const FText& Line)
{
	FullLine = Line.ToString();
	Reveal = 0.f;
	if (DialogText) { DialogText->SetText(FText::GetEmpty()); }
}

void UTN_ShopWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// El tendero habla letra a letra.
	if (DialogText && Reveal < FullLine.Len())
	{
		Reveal = FMath::Min<float>(FullLine.Len(), Reveal + InDeltaTime * 70.f);
		// Letra a letra: es un trozo del texto ya traducido, no un texto nuevo.
		DialogText->SetText(TNLocText::Literal(FullLine.Left(FMath::CeilToInt(Reveal))));
	}
}

bool UTN_ShopWidget::HandleKey(const FKey& Key)
{
	using TNShopUI::IsKey;
	constexpr int32 Columns = 4;
	if (IsKey(Key, { EKeys::Left, EKeys::A, EKeys::Gamepad_DPad_Left })) { Select(FMath::Max(0, Selected - 1), true); return true; }
	if (IsKey(Key, { EKeys::Right, EKeys::D, EKeys::Gamepad_DPad_Right })) { Select(FMath::Min(Items.Num() - 1, Selected + 1), true); return true; }
	if (IsKey(Key, { EKeys::Up, EKeys::W, EKeys::Gamepad_DPad_Up })) { Select(FMath::Max(0, Selected - Columns), true); return true; }
	if (IsKey(Key, { EKeys::Down, EKeys::S, EKeys::Gamepad_DPad_Down })) { Select(FMath::Min(Items.Num() - 1, Selected + Columns), true); return true; }
	const int32 TabIndex = TNShopUI::Categories().IndexOfByKey(Tab);
	const int32 NumTabs = TNShopUI::Categories().Num();
	if (IsKey(Key, { EKeys::Q, EKeys::Gamepad_LeftShoulder })) { ShowTab(TNShopUI::Categories()[(TabIndex + NumTabs - 1) % NumTabs]); return true; }
	if (IsKey(Key, { EKeys::E, EKeys::Tab, EKeys::Gamepad_RightShoulder })) { ShowTab(TNShopUI::Categories()[(TabIndex + 1) % NumTabs]); return true; }
	if (IsKey(Key, { EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom })) { Buy(); return true; }
	return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Probador
// ─────────────────────────────────────────────────────────────────────────────

void UTN_BoothWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

void UTN_BoothWidget::BuildTree()
{
	if (!WidgetTree || WidgetTree->RootWidget) { return; }
	UWidgetTree* Tree = WidgetTree;
	UCanvasPanel* Canvas = TNShopUI::New<UCanvasPanel>(Tree);
	Tree->RootWidget = Canvas;

	// Velo suave: detrás se ve la botella cerrada.
	UImage* Veil = TNShopUI::New<UImage>(Tree);
	Veil->SetColorAndOpacity(TNHUDArt::Hex(0x0A1C38, 0.35f));
	if (UCanvasPanelSlot* VeilSlot = Canvas->AddChildToCanvas(Veil))
	{
		VeilSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		VeilSlot->SetOffsets(FMargin(0.f));
	}

	UTextBlock* TitleLabel = TNShopUI::Label(Tree, NSLOCTEXT("Tortunabo", "BoothTitle", "PROBADOR"), TEXT("Black"), 34, TNShopUI::CreamColor, true);
	UBorder* Title = TNShopUI::Framed(Tree, TNHUDArt::RibbonTexture(), TNShopUI::RibbonBoxMargin, TitleLabel, FMargin(80.f, 8.f, 80.f, 14.f));
	Title->SetHorizontalAlignment(HAlign_Center);
	TNShopUI::Pin(Canvas, Title, FVector2D(0.5f, 0.f), FVector2D(0.f, 26.f));

	TNShopUI::Pin(Canvas, MakePreviewPanel(620.f), FVector2D(0.06f, 0.56f), FVector2D::ZeroVector);

	UVerticalBox* Right = TNShopUI::New<UVerticalBox>(Tree);
	{
		// Páginas: la tortuga y su buggy del Rally (Q/E).
		UHorizontalBox* PageRow = TNShopUI::New<UHorizontalBox>(Tree);
		const FText PageTitles[] = { NSLOCTEXT("Tortunabo", "BoothPageTurtle", "TORTUGA"), NSLOCTEXT("Tortunabo", "BoothPageBuggy", "BUGGY") };
		for (int32 p = 0; p < 2; ++p)
		{
			UTN_ShopButton* PageButton = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
			PageButton->Setup(PageTitles[p], TNShopArt::Pill(0x2A5A92, 0x173A66), TNShopUI::CreamColor, 19, FVector2D(190.f, 54.f), [this, p]() { ShowPage(p); });
			TNShopUI::AddH(PageRow, PageButton, FMargin(0.f, 0.f, p == 0 ? 12.f : 0.f, 0.f));
			PageButtons.Add(PageButton);
		}
		TNShopUI::AddV(Right, PageRow, FMargin(0.f, 0.f, 0.f, 12.f), HAlign_Center);
	}
	const TArray<ETNCosmeticCategory>& Rows = TNShopUI::TurtleRows();
	for (int32 r = 0; r < Rows.Num(); ++r)
	{
		UHorizontalBox* Line = TNShopUI::New<UHorizontalBox>(Tree);
		UTextBlock* Caption = TNShopUI::Label(Tree, TNShopUI::RowTitle(Rows[r]), TEXT("Black"), 22, TNShopUI::InkColor, false);
		RowCaptions.Add(Caption);
		TNShopUI::AddH(Line, TNShopUI::Sized(Tree, Caption, 170.f, 0.f), FMargin(6.f, 0.f, 6.f, 0.f));

		UTN_ShopButton* Prev = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
		Prev->Setup(FText::GetEmpty(), TNShopArt::Arrow(false), TNShopUI::CreamColor, 22, FVector2D(56.f, 56.f), [this, r]() { FocusRow(r); Cycle(r, -1); });
		TNShopUI::AddH(Line, Prev, FMargin(0.f, 0.f, 10.f, 0.f));

		UImage* Thumb = TNShopUI::New<UImage>(Tree);
		TNShopUI::AddH(Line, TNShopUI::Sized(Tree, Thumb, 96.f, 96.f), FMargin(0.f, 0.f, 10.f, 0.f));
		RowThumbs.Add(Thumb);
		RowThumbMIDs.Add(MakeCaptureMID(nullptr));
		if (RowThumbMIDs.Last()) { Thumb->SetBrushFromMaterial(RowThumbMIDs.Last()); }

		UVerticalBox* NameColumn = TNShopUI::New<UVerticalBox>(Tree);
		UTextBlock* NameLabel = TNShopUI::Label(Tree, FText::GetEmpty(), TEXT("Bold"), 21, TNShopUI::InkColor, false);
		NameLabel->SetAutoWrapText(true);
		TNShopUI::AddV(NameColumn, TNShopUI::Sized(Tree, NameLabel, 250.f, 0.f));
		UTextBlock* CountLabel = TNShopUI::Label(Tree, FText::GetEmpty(), TEXT("Regular"), 15, TNHUDArt::WetSand, false);
		TNShopUI::AddV(NameColumn, CountLabel, FMargin(0.f, 2.f, 0.f, 0.f));
		TNShopUI::AddH(Line, NameColumn, FMargin(0.f, 0.f, 10.f, 0.f));
		RowNames.Add(NameLabel);
		RowCounts.Add(CountLabel);

		UTN_ShopButton* Next = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
		Next->Setup(FText::GetEmpty(), TNShopArt::Arrow(true), TNShopUI::CreamColor, 22, FVector2D(56.f, 56.f), [this, r]() { FocusRow(r); Cycle(r, 1); });
		TNShopUI::AddH(Line, Next);

		UBorder* RowFrame = TNShopUI::Framed(Tree, TNShopArt::SandPanel(false), TNShopUI::SandBoxMargin, Line, FMargin(18.f, 9.f, 18.f, 9.f));
		RowFrames.Add(RowFrame);
		TNShopUI::AddV(Right, RowFrame, FMargin(0.f, 0.f, 0.f, 10.f), HAlign_Fill);
	}
	{
		UHorizontalBox* Buttons = TNShopUI::New<UHorizontalBox>(Tree);
		UTN_ShopButton* Done = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
		Done->Setup(NSLOCTEXT("Tortunabo", "BoothDone", "¡LISTO!"), TNShopArt::Pill(0xFFD95E, 0xF2A93B), TNShopUI::InkColor, 26, FVector2D(300.f, 70.f), [this]() { Accept(); });
		TNShopUI::AddH(Buttons, Done, FMargin(0.f, 0.f, 16.f, 0.f));
		UTN_ShopButton* Cancel = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
		Cancel->Setup(NSLOCTEXT("Tortunabo", "BoothCancel", "CANCELAR"), TNShopArt::Pill(0x3B6EA8, 0x1D3F6E), TNShopUI::CreamColor, 22, FVector2D(200.f, 70.f),
			[this]() { CloseMenu(); });
		TNShopUI::AddH(Buttons, Cancel);
		TNShopUI::AddV(Right, Buttons, FMargin(0.f, 10.f, 0.f, 0.f), HAlign_Center);
		UTextBlock* Keys = TNShopUI::Label(Tree, NSLOCTEXT("Tortunabo", "BoothKeysPages", "Arriba/abajo: fila · Izquierda/derecha: cambiar · Q/E: tortuga o buggy · Intro: listo · Esc: cancelar"),
			TEXT("Regular"), 14, TNHUDArt::SeaLight, true);
		TNShopUI::AddV(Right, Keys, FMargin(0.f, 10.f, 0.f, 0.f), HAlign_Center);
	}
	UBorder* RightCard = TNShopUI::Framed(Tree, TNHUDArt::CardTexture(), TNShopUI::CardBoxMargin, Right, FMargin(30.f, 30.f, 30.f, 46.f));
	TNShopUI::Pin(Canvas, RightCard, FVector2D(0.94f, 0.56f), FVector2D::ZeroVector);
}

void UTN_BoothWidget::SetBooth(ATN_ChangingBooth* InBooth)
{
	Booth = InBooth;
	Initial = GetWornLook();
	InitialBuggy = GetWornBuggyLook();
	LoadOptions();
	ShowPage(0);
}

const TArray<ETNCosmeticCategory>& UTN_BoothWidget::PageRows() const
{
	return Page == 1 ? TNShopUI::BuggyRows() : TNShopUI::TurtleRows();
}

void UTN_BoothWidget::LoadOptions()
{
	const UMP_GameInstance* GI = GetTNGI();
	Options.Reset();
	Choice.Reset();
	Options.SetNum(TNShopUI::NumCategories);
	Choice.Init(0, TNShopUI::NumCategories);
	for (int32 c = 0; c < TNShopUI::NumCategories; ++c)
	{
		const ETNCosmeticCategory Category = static_cast<ETNCosmeticCategory>(c);
		TArray<FName>& Owned = Options[c];
		Owned.Add(NAME_None);
		if (GI)
		{
			for (const FName Id : GI->GetCosmeticCatalog(Category))
			{
				if (TNShopUI::IsOwned(GI, GetOwningPlayer(), Category, Id)) { Owned.Add(Id); }
			}
		}
		const FName WornId = TNIsBuggyCategory(Category) ? InitialBuggy.Get(Category) : Initial.Get(Category);
		const int32 Worn = Owned.IndexOfByKey(WornId);
		Choice[c] = Worn == INDEX_NONE ? 0 : Worn;
	}
}

FName UTN_BoothWidget::ChosenId(ETNCosmeticCategory Category) const
{
	const int32 c = static_cast<int32>(Category);
	return Options.IsValidIndex(c) && Options[c].IsValidIndex(Choice[c]) ? Options[c][Choice[c]] : NAME_None;
}

FTN_TurtleLook UTN_BoothWidget::ChosenLook() const
{
	FTN_TurtleLook Look;
	for (const ETNCosmeticCategory Category : TNShopUI::TurtleRows()) { Look.Set(Category, ChosenId(Category)); }
	return Look;
}

FTN_BuggyLook UTN_BoothWidget::ChosenBuggyLook() const
{
	FTN_BuggyLook Look;
	for (const ETNCosmeticCategory Category : TNShopUI::BuggyRows()) { Look.Set(Category, ChosenId(Category)); }
	return Look;
}

void UTN_BoothWidget::ShowPage(int32 NewPage)
{
	Page = FMath::Clamp(NewPage, 0, 1);
	for (int32 p = 0; p < PageButtons.Num(); ++p)
	{
		PageButtons[p]->SetArt(p == Page ? TNShopArt::Pill(0xFF8A70, 0xD9432F) : TNShopArt::Pill(0x2A5A92, 0x173A66));
	}
	const int32 Count = PageRows().Num();
	for (int32 r = 0; r < RowFrames.Num(); ++r)
	{
		RowFrames[r]->SetVisibility(r < Count ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (r < Count && RowCaptions.IsValidIndex(r)) { RowCaptions[r]->SetText(TNShopUI::RowTitle(PageRows()[r])); }
	}
	if (ATN_CosmeticPreview* Stage = ATN_CosmeticPreview::GetFor(GetOwningPlayer())) { Stage->SetBuggyMode(Page == 1); }
	FocusRow(0);
	RefreshRows();
}

void UTN_BoothWidget::RefreshRows()
{
	const TArray<ETNCosmeticCategory>& Rows = PageRows();
	for (int32 r = 0; r < Rows.Num(); ++r)
	{
		const ETNCosmeticCategory Category = Rows[r];
		const int32 c = static_cast<int32>(Category);
		const FName Id = ChosenId(Category);
		if (RowNames.IsValidIndex(r)) { RowNames[r]->SetText(UTN_CosmeticLook::GetDisplayName(this, Category, Id)); }
		if (RowCounts.IsValidIndex(r))
		{
			RowCounts[r]->SetText(FText::Format(NSLOCTEXT("Tortunabo", "BoothCount", "{0} de {1}"), FText::AsNumber(Choice[c] + 1), FText::AsNumber(Options[c].Num())));
		}
		if (RowThumbMIDs.IsValidIndex(r) && RowThumbMIDs[r])
		{
			if (UTextureRenderTarget2D* RT = Thumbnail(Category, Id)) { RowThumbMIDs[r]->SetTextureParameterValue(TEXT("Capture"), RT); }
		}
	}
	if (ATN_CosmeticPreview* Stage = ATN_CosmeticPreview::GetFor(GetOwningPlayer()))
	{
		Stage->SetLook(ChosenLook());
		Stage->SetBuggyLook(ChosenBuggyLook());
	}
}

void UTN_BoothWidget::FocusRow(int32 Row)
{
	FocusedRow = FMath::Clamp(Row, 0, FMath::Max(0, PageRows().Num() - 1));
	for (int32 r = 0; r < RowFrames.Num(); ++r)
	{
		RowFrames[r]->SetBrush(TNShopUI::BoxBrush(TNShopArt::SandPanel(r == FocusedRow), TNShopUI::SandBoxMargin));
		RowFrames[r]->SetRenderScale(FVector2D(r == FocusedRow ? 1.03f : 1.f, r == FocusedRow ? 1.03f : 1.f));
		RowFrames[r]->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	}
}

void UTN_BoothWidget::Cycle(int32 Row, int32 Dir)
{
	if (!PageRows().IsValidIndex(Row)) { return; }
	const int32 c = static_cast<int32>(PageRows()[Row]);
	if (!Options.IsValidIndex(c) || Options[c].Num() == 0) { return; }
	const int32 Num = Options[c].Num();
	Choice[c] = (Choice[c] + Dir + Num) % Num;
	RefreshRows();
	if (ATN_CosmeticPreview* Stage = ATN_CosmeticPreview::GetFor(GetOwningPlayer())) { Stage->PlayPose(false); }
}

void UTN_BoothWidget::Accept()
{
	AMP_GamePlayerController* PC = GetTNPC();
	const FTN_TurtleLook Chosen = ChosenLook();
	const FTN_BuggyLook ChosenBuggy = ChosenBuggyLook();
	if (PC)
	{
		if (Chosen.HelmetId != Initial.HelmetId)
		{
			if (Chosen.HelmetId == NAME_None) { PC->RequestUnequipHelmet(); }
			else { PC->RequestEquipHelmet(Chosen.HelmetId); }
		}
		if (Chosen.ShellId != Initial.ShellId) { PC->RequestEquipShell(Chosen.ShellId); }
		if (Chosen.SkinId != Initial.SkinId) { PC->RequestEquipSkin(Chosen.SkinId); }
		if (Chosen.EyesId != Initial.EyesId) { PC->RequestEquipEyes(Chosen.EyesId); }
		if (ChosenBuggy != InitialBuggy) { PC->RequestEquipBuggyLook(ChosenBuggy); }
	}
	CloseMenu();
}

bool UTN_BoothWidget::HandleKey(const FKey& Key)
{
	using TNShopUI::IsKey;
	const int32 Rows = FMath::Max(1, PageRows().Num());
	if (IsKey(Key, { EKeys::Up, EKeys::W, EKeys::Gamepad_DPad_Up })) { FocusRow((FocusedRow + Rows - 1) % Rows); return true; }
	if (IsKey(Key, { EKeys::Down, EKeys::S, EKeys::Gamepad_DPad_Down })) { FocusRow((FocusedRow + 1) % Rows); return true; }
	if (IsKey(Key, { EKeys::Left, EKeys::A, EKeys::Gamepad_DPad_Left })) { Cycle(FocusedRow, -1); return true; }
	if (IsKey(Key, { EKeys::Right, EKeys::D, EKeys::Gamepad_DPad_Right })) { Cycle(FocusedRow, 1); return true; }
	if (IsKey(Key, { EKeys::Q, EKeys::E, EKeys::Tab, EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_RightShoulder })) { ShowPage(1 - Page); return true; }
	if (IsKey(Key, { EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom })) { Accept(); return true; }
	return false;
}
