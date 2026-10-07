#include "UI/Shop/TN_MysteryBoxPanel.h"
#include "UI/Shop/TN_ShopWidgets.h"
#include "TN_ShopArt.h"
#include "../HUD/TN_HUDArt.h"
#include "../HUD/TN_HUDStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/TN_CosmeticLook.h"
#include "Core/TN_GameplayPreload.h"
#include "InputCoreTypes.h"
#include "Lobby/TN_CosmeticPreview.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace TNBoxUI
{
	/** Duración de las vueltas antes de pararse en la que ha salido. */
	constexpr float SpinSeconds = 2.4f;
	/** Pausa entre dos skins al empezar y al acabar las vueltas (se frena poco a poco). */
	constexpr float FirstFlip = 0.05f;
	constexpr float LastFlip = 0.34f;
	/** Salto de la revelación: escala de partida y lo que tarda en asentarse. */
	constexpr float PopScale = 1.3f;
	constexpr float PopSeconds = 0.35f;
	constexpr float CaptureExposure = 1.6f;

	UTextBlock* Label(UWidgetTree* Tree, FName Weight, int32 FontSize, const FLinearColor& Color, bool bOutline)
	{
		UTextBlock* Out = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		TNHUDStyle::StyleText(Out, Weight, FontSize, Color, bOutline);
		Out->SetJustification(ETextJustify::Center);
		if (!bOutline) { Out->SetShadowColorAndOpacity(FLinearColor::Transparent); }
		return Out;
	}

	/** Cartel que se estira como caja (márgenes de las texturas de TNHUDArt, como en la tienda). */
	FSlateBrush BoxBrush(UTexture2D* Tex, const FMargin& Margin)
	{
		FSlateBrush Brush;
		Brush.SetResourceObject(Tex);
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.Margin = Margin;
		if (Tex) { Brush.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY()); }
		return Brush;
	}

	void AddRow(UVerticalBox* Box, UWidget* W, const FMargin& Padding)
	{
		UVerticalBoxSlot* VSlot = Box->AddChildToVerticalBox(W);
		VSlot->SetPadding(Padding);
		VSlot->SetHorizontalAlignment(HAlign_Center);
	}

	bool IsKey(const FKey& Key, std::initializer_list<FKey> Options)
	{
		for (const FKey& Option : Options) { if (Key == Option) { return true; } }
		return false;
	}
}

FText TNSkinRarityText::Name(ETNSkinRarity Rarity)
{
	switch (Rarity)
	{
	case ETNSkinRarity::Rare: return NSLOCTEXT("Tortunabo", "SkinRarityRare", "Rara");
	case ETNSkinRarity::Epic: return NSLOCTEXT("Tortunabo", "SkinRarityEpic", "Épica");
	default:                  return NSLOCTEXT("Tortunabo", "SkinRarityCommon", "Común");
	}
}

FLinearColor TNSkinRarityText::Color(ETNSkinRarity Rarity)
{
	switch (Rarity)
	{
	case ETNSkinRarity::Rare: return TNHUDArt::Hex(0x2B6FD6);
	case ETNSkinRarity::Epic: return TNHUDArt::Hex(0x8E3FC9);
	default:                  return TNHUDArt::Hex(0x3F8A4E);
	}
}

void UTN_MysteryBoxPanel::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildTree();
}

void UTN_MysteryBoxPanel::BuildTree()
{
	if (!WidgetTree || WidgetTree->RootWidget) { return; }
	UWidgetTree* Tree = WidgetTree;
	UOverlay* Root = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	Tree->RootWidget = Root;

	UImage* Veil = Tree->ConstructWidget<UImage>(UImage::StaticClass());
	Veil->SetColorAndOpacity(TNHUDArt::Hex(0x0A1C38, 0.7f));
	if (UOverlaySlot* VeilSlot = Root->AddChildToOverlay(Veil))
	{
		VeilSlot->SetHorizontalAlignment(HAlign_Fill);
		VeilSlot->SetVerticalAlignment(VAlign_Fill);
	}

	UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	UTextBlock* Title = TNBoxUI::Label(Tree, TEXT("Black"), 30, TNHUDArt::Cream, true);
	Title->SetText(NSLOCTEXT("Tortunabo", "BoxTitle", "CAJA SORPRESA"));
	UBorder* Ribbon = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Ribbon->SetBrush(TNBoxUI::BoxBrush(TNHUDArt::RibbonTexture(), FMargin(0.14f, 0.f, 0.14f, 0.f)));
	Ribbon->SetPadding(FMargin(60.f, 8.f, 60.f, 14.f));
	Ribbon->SetContent(Title);
	TNBoxUI::AddRow(Column, Ribbon, FMargin(0.f, 0.f, 0.f, 12.f));

	ThumbImage = Tree->ConstructWidget<UImage>(UImage::StaticClass());
	USizeBox* ThumbSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	ThumbSize->SetWidthOverride(240.f);
	ThumbSize->SetHeightOverride(240.f);
	ThumbSize->SetContent(ThumbImage);
	TNBoxUI::AddRow(Column, ThumbSize, FMargin(0.f));
	NameText = TNBoxUI::Label(Tree, TEXT("Black"), 28, TNHUDArt::Ink, false);
	TNBoxUI::AddRow(Column, NameText, FMargin(0.f, 6.f, 0.f, 0.f));
	RarityText = TNBoxUI::Label(Tree, TEXT("Black"), 22, TNHUDArt::Ink, false);
	TNBoxUI::AddRow(Column, RarityText, FMargin(0.f, 2.f, 0.f, 0.f));
	ResultText = TNBoxUI::Label(Tree, TEXT("Bold"), 20, TNHUDArt::Ink, false);
	ResultText->SetAutoWrapText(true);
	USizeBox* ResultSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	ResultSize->SetWidthOverride(520.f);
	ResultSize->SetMinDesiredHeight(56.f);
	ResultSize->SetContent(ResultText);
	TNBoxUI::AddRow(Column, ResultSize, FMargin(0.f, 10.f, 0.f, 10.f));

	UHorizontalBox* Buttons = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	AgainButton = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
	AgainButton->Setup(FText::GetEmpty(), TNShopArt::Pill(0xC08BFF, 0x7A3FC9), TNHUDArt::Cream, 22, FVector2D(300.f, 62.f),
		[this]() { if (!bSpinning && bCanOpenAgain && OnAgain) { OnAgain(); } });
	if (UHorizontalBoxSlot* HSlot = Buttons->AddChildToHorizontalBox(AgainButton)) { HSlot->SetPadding(FMargin(0.f, 0.f, 16.f, 0.f)); }
	CloseButton = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
	CloseButton->Setup(NSLOCTEXT("Tortunabo", "BoxBack", "VOLVER"), TNShopArt::Pill(0x3B6EA8, 0x1D3F6E), TNHUDArt::Cream, 22,
		FVector2D(170.f, 62.f), [this]() { Close(); });
	Buttons->AddChildToHorizontalBox(CloseButton);
	TNBoxUI::AddRow(Column, Buttons, FMargin(0.f, 4.f, 0.f, 0.f));

	Card = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Card->SetBrush(TNBoxUI::BoxBrush(TNHUDArt::CardTexture(), FMargin(0.16f, 0.2f, 0.16f, 0.34f)));
	Card->SetPadding(FMargin(40.f, 26.f, 40.f, 44.f));
	Card->SetContent(Column);
	Card->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	if (UOverlaySlot* CardSlot = Root->AddChildToOverlay(Card))
	{
		CardSlot->SetHorizontalAlignment(HAlign_Center);
		CardSlot->SetVerticalAlignment(VAlign_Center);
	}
}

void UTN_MysteryBoxPanel::SetActions(TFunction<void()> InOnAgain, TFunction<void()> InOnClose)
{
	OnAgain = MoveTemp(InOnAgain);
	OnClose = MoveTemp(InOnClose);
}

void UTN_MysteryBoxPanel::SetAgainState(int32 Price, bool bCanAfford)
{
	bCanOpenAgain = bCanAfford;
	if (!AgainButton) { return; }
	AgainButton->SetLabel(FText::Format(NSLOCTEXT("Tortunabo", "BoxAgain", "ABRIR OTRA · {0}"), FText::AsNumber(Price)));
	AgainButton->SetDisabled(bSpinning || !bCanAfford);
}

void UTN_MysteryBoxPanel::Play(const FTN_MysteryBoxResult& InResult, TArray<TNMysteryBox::FCandidate> InCandidates)
{
	Result = InResult;
	Candidates = MoveTemp(InCandidates);
	SpinStream.Initialize(FMath::Rand());
	bSpinning = true;
	SpinElapsed = 0.f;
	NextFlipAt = 0.f;
	RevealElapsed = -1.f;
	if (ResultText) { ResultText->SetText(NSLOCTEXT("Tortunabo", "BoxSpinning", "¿Qué saldrá?")); }
	if (AgainButton) { AgainButton->SetDisabled(true); }
	if (Card) { Card->SetRenderScale(FVector2D::UnitVector); }
}

void UTN_MysteryBoxPanel::ShowItem(const TNMysteryBox::FCandidate& Item)
{
	if (NameText) { NameText->SetText(UTN_CosmeticLook::GetDisplayName(this, Item.Category, Item.Id)); }
	if (RarityText)
	{
		RarityText->SetText(TNSkinRarityText::Name(Item.Rarity).ToUpper());
		RarityText->SetColorAndOpacity(FSlateColor(TNSkinRarityText::Color(Item.Rarity)));
	}
	ATN_CosmeticPreview* Stage = ATN_CosmeticPreview::GetFor(GetOwningPlayer());
	UTextureRenderTarget2D* Thumb = Stage ? Stage->GetThumbnail(Item.Category, Item.Id) : nullptr;
	if (!ThumbImage || !Thumb) { return; }
	if (!ThumbMID)
	{
		UMaterialInterface* Base = TNPreload::PreviewMaterial();
		ThumbMID = Base ? UMaterialInstanceDynamic::Create(Base, this) : nullptr;
		if (!ThumbMID) { return; }
		ThumbMID->SetScalarParameterValue(TEXT("Exposure"), TNBoxUI::CaptureExposure);
		ThumbImage->SetBrushFromMaterial(ThumbMID);
	}
	ThumbMID->SetTextureParameterValue(TEXT("Capture"), Thumb);
}

void UTN_MysteryBoxPanel::Reveal()
{
	bSpinning = false;
	RevealElapsed = 0.f;
	if (Card) { Card->SetRenderTransformAngle(0.f); }
	ShowItem({ Result.SkinId, Result.Category, Result.Rarity });
	if (ResultText)
	{
		ResultText->SetText(Result.bDuplicate
			? FText::Format(NSLOCTEXT("Tortunabo", "BoxDuplicate", "Repetida: ya la tenías. Te devuelvo {0} {0}|plural(one=punto,other=puntos)."),
				Result.RefundPoints)
			: NSLOCTEXT("Tortunabo", "BoxNew", "¡Nueva! Ya es tuya: pruébatela en las botellas del probador."));
	}
	if (AgainButton) { AgainButton->SetDisabled(!bCanOpenAgain); }
}

void UTN_MysteryBoxPanel::Close()
{
	if (bSpinning) { Reveal(); }
	if (OnClose) { OnClose(); }
}

bool UTN_MysteryBoxPanel::HandleKey(const FKey& Key)
{
	if (TNBoxUI::IsKey(Key, { EKeys::Escape, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_Special_Right }))
	{
		Close();
		return true;
	}
	if (TNBoxUI::IsKey(Key, { EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom }))
	{
		if (bSpinning) { Reveal(); }
		else if (bCanOpenAgain && OnAgain) { OnAgain(); }
		return true;
	}
	// El catálogo de debajo no se toca mientras la caja está abierta.
	return true;
}

void UTN_MysteryBoxPanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bSpinning)
	{
		SpinElapsed += InDeltaTime;
		const float Alpha = FMath::Clamp(SpinElapsed / TNBoxUI::SpinSeconds, 0.f, 1.f);
		// La carta se balancea mientras gira y se va quedando quieta al frenar.
		if (Card) { Card->SetRenderTransformAngle(FMath::Sin(SpinElapsed * 20.f) * 7.f * (1.f - Alpha)); }
		if (SpinElapsed >= NextFlipAt && Candidates.Num() > 0)
		{
			ShowItem(Candidates[SpinStream.RandRange(0, Candidates.Num() - 1)]);
			NextFlipAt = SpinElapsed + FMath::Lerp(TNBoxUI::FirstFlip, TNBoxUI::LastFlip, Alpha * Alpha);
		}
		if (Alpha >= 1.f) { Reveal(); }
		return;
	}
	if (RevealElapsed >= 0.f && Card)
	{
		RevealElapsed += InDeltaTime;
		const float T = FMath::Clamp(RevealElapsed / TNBoxUI::PopSeconds, 0.f, 1.f);
		const float Scale = FMath::Lerp(TNBoxUI::PopScale, 1.f, 1.f - FMath::Square(1.f - T));
		Card->SetRenderScale(FVector2D(Scale, Scale));
		if (T >= 1.f) { RevealElapsed = -1.f; }
	}
}
