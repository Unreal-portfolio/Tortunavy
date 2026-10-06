#include "UI/HUD/TN_GhostHUDWidget.h"
#include "TN_HUDArt.h"
#include "Core/TN_LocText.h"
#include "TN_HUDGhostFace.h"
#include "TN_HUDStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Player/MP_GamePlayerController.h"
#include "Player/TN_SpectatorGhost.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "Settings/TN_InputDeviceSubsystem.h"
#include "UI/TN_ScreenHost.h"

namespace TNGhostHUDDetail
{
	/** Capa del cartel: encima del HUD de la tortuga y del cartel de estado (4 y 5), debajo de la voz y las ruedas. */
	constexpr int32 ZOrder = 6;

	/** Márgenes de caja de los carteles con arte de TNHUDArt (los mismos que UTN_RunHUDWidget). */
	const FMargin CardMargin(0.16f, 0.2f, 0.16f, 0.34f);
	const FMargin RibbonMargin(0.14f, 0.f, 0.14f, 0.f);

	template <typename T>
	T* Make(UWidgetTree* Tree)
	{
		return Tree->ConstructWidget<T>(T::StaticClass());
	}

	UTextBlock* MakeText(UWidgetTree* Tree, const FText& Content, FName Weight, int32 FontSize, const FLinearColor& Color)
	{
		UTextBlock* Block = Make<UTextBlock>(Tree);
		Block->SetText(Content);
		TNHUDStyle::StyleText(Block, Weight, FontSize, Color, true);
		return Block;
	}

	UImage* MakeImage(UWidgetTree* Tree, UTexture2D* Texture, const FVector2D& ImageSize)
	{
		UImage* Image = Make<UImage>(Tree);
		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = ImageSize;
		Image->SetBrush(Brush);
		return Image;
	}

	/** Cartel con arte que se estira como caja (como MakeCard de UTN_RunHUDWidget). */
	UBorder* MakeCard(UWidgetTree* Tree, UTexture2D* Texture, const FMargin& Margin, UWidget* Content, const FMargin& Padding)
	{
		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.Margin = Margin;
		if (Texture)
		{
			Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		}
		UBorder* Card = Make<UBorder>(Tree);
		Card->SetBrush(Brush);
		Card->SetPadding(Padding);
		Card->SetHorizontalAlignment(HAlign_Left);
		Card->SetVerticalAlignment(VAlign_Center);
		Card->SetContent(Content);
		return Card;
	}

	UCanvasPanelSlot* Place(UCanvasPanel* Canvas, UWidget* Widget, const FVector2D& Anchor, const FVector2D& Offset)
	{
		UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(Widget);
		CanvasSlot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
		CanvasSlot->SetAlignment(Anchor);
		CanvasSlot->SetPosition(Offset);
		CanvasSlot->SetAutoSize(true);
		return CanvasSlot;
	}

	/** Cartel de cada jugador local. */
	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<UTN_GhostHUDWidget>>& Widgets()
	{
		static TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<UTN_GhostHUDWidget>> Map;
		return Map;
	}
}

void UTN_GhostHUDWidget::EnsureFor(APlayerController* PC)
{
	if (!PC || !PC->IsLocalController() || !PC->IsA<AMP_GamePlayerController>() || PC->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<UTN_GhostHUDWidget>>& Map = TNGhostHUDDetail::Widgets();
	for (auto It = Map.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Value().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	UTN_GhostHUDWidget* Widget = nullptr;
	if (const TWeakObjectPtr<UTN_GhostHUDWidget>* Found = Map.Find(PC))
	{
		Widget = Found->Get();
	}
	if (!Widget)
	{
		Widget = CreateWidget<UTN_GhostHUDWidget>(PC, UTN_GhostHUDWidget::StaticClass());
		if (!Widget)
		{
			return;
		}
		Map.Add(PC, Widget);
	}
	if (!TNScreen::IsOnScreen(Widget))
	{
		TNScreen::AddToScreen(Widget, TNGhostHUDDetail::ZOrder);
	}
}

void UTN_GhostHUDWidget::NativeOnInitialized()
{
	BuildTree();
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UTN_GhostHUDWidget::BuildTree()
{
	using namespace TNGhostHUDDetail;
	if (!WidgetTree || Canvas)
	{
		return;
	}
	UWidgetTree* Tree = WidgetTree;
	Canvas = Make<UCanvasPanel>(Tree);
	Tree->RootWidget = Canvas;

	// ── De fantasma (abajo a la derecha): tu icono con tu nombre y el cartel de a quién miras y cómo ──
	{
		UHorizontalBox* Row = Make<UHorizontalBox>(Tree);
		UVerticalBox* Badge = Make<UVerticalBox>(Tree);
		GhostIcon = MakeImage(Tree, TNHUDGhostFace::Texture(), FVector2D(104.0, 104.0));
		GhostIcon->SetRenderTransformPivot(FVector2D(0.5, 0.8));
		if (UVerticalBoxSlot* IconSlot = Badge->AddChildToVerticalBox(GhostIcon))
		{
			IconSlot->SetHorizontalAlignment(HAlign_Center);
		}
		OwnNameText = MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 14, FLinearColor::White);
		if (UVerticalBoxSlot* NameSlot = Badge->AddChildToVerticalBox(MakeCard(Tree, TNHUDArt::RibbonTexture(), RibbonMargin, OwnNameText, FMargin(30.f, 14.f, 30.f, 16.f))))
		{
			NameSlot->SetHorizontalAlignment(HAlign_Center);
			NameSlot->SetPadding(FMargin(0.f, -10.f, 0.f, 0.f));
		}
		if (UHorizontalBoxSlot* BadgeSlot = Row->AddChildToHorizontalBox(Badge))
		{
			BadgeSlot->SetVerticalAlignment(VAlign_Bottom);
			BadgeSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		}

		UVerticalBox* Lines = Make<UVerticalBox>(Tree);
		Lines->AddChildToVerticalBox(MakeText(Tree, NSLOCTEXT("TNGhost", "Title", "¡Eres un fantasma!"), TEXT("Bold"), 15, TNHUDArt::Foam));
		FollowText = MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 21, TNHUDArt::SandC);
		if (UVerticalBoxSlot* FollowSlot = Lines->AddChildToVerticalBox(FollowText)) { FollowSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f)); }
		CameraText = MakeText(Tree, FText::GetEmpty(), TEXT("Regular"), 15, TNHUDArt::SeaLight);
		if (UVerticalBoxSlot* CameraSlot = Lines->AddChildToVerticalBox(CameraText)) { CameraSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f)); }
		KeysText = MakeText(Tree, FText::GetEmpty(), TEXT("Regular"), 14, TNHUDArt::Cream);
		if (UVerticalBoxSlot* KeysSlot = Lines->AddChildToVerticalBox(KeysText)) { KeysSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f)); }
		AlsoText = MakeText(Tree, FText::GetEmpty(), TEXT("Regular"), 14, TNHUDArt::Foam);
		AlsoText->SetVisibility(ESlateVisibility::Collapsed);
		if (UVerticalBoxSlot* AlsoSlot = Lines->AddChildToVerticalBox(AlsoText)) { AlsoSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f)); }
		USizeBox* Limit = Make<USizeBox>(Tree);
		Limit->SetMaxDesiredWidth(420.f);
		Limit->SetContent(Lines);
		if (UHorizontalBoxSlot* CardSlot = Row->AddChildToHorizontalBox(MakeCard(Tree, TNHUDArt::CardTexture(), CardMargin, Limit, FMargin(24.f, 26.f, 30.f, 40.f))))
		{
			CardSlot->SetVerticalAlignment(VAlign_Bottom);
		}
		Row->SetRenderTransformPivot(FVector2D(1.0, 1.0));
		Row->SetVisibility(ESlateVisibility::Collapsed);
		GhostCard = Row;
		// Por encima de la esquina de abajo a la derecha (ahí va el contador de fotogramas del menú de ajustes).
		Place(Canvas, Row, FVector2D(1.0, 1.0), FVector2D(-24.0, -56.0));
	}

	// ── Con tortuga (abajo a la izquierda, encima del distintivo): los fantasmas que te miran ──
	{
		UHorizontalBox* Row = Make<UHorizontalBox>(Tree);
		WatchersIcon = MakeImage(Tree, TNHUDGhostFace::Texture(), FVector2D(48.0, 48.0));
		if (UHorizontalBoxSlot* IconSlot = Row->AddChildToHorizontalBox(WatchersIcon)) { IconSlot->SetVerticalAlignment(VAlign_Center); }
		WatchersText = MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 15, TNHUDArt::Cream);
		UBorder* Pill = Make<UBorder>(Tree);
		TNHUDStyle::StylePanel(Pill, TNHUDArt::Hex(0x0A1C38, 0.75f), 12.f, FMargin(12.f, 4.f, 14.f, 5.f), FLinearColor::Transparent, 0.f);
		Pill->SetContent(WatchersText);
		if (UHorizontalBoxSlot* PillSlot = Row->AddChildToHorizontalBox(Pill))
		{
			PillSlot->SetVerticalAlignment(VAlign_Center);
			PillSlot->SetPadding(FMargin(-6.f, 0.f, 0.f, 0.f));
		}
		Row->SetRenderTransformPivot(FVector2D(0.0, 1.0));
		Row->SetVisibility(ESlateVisibility::Collapsed);
		WatchersCard = Row;
		Place(Canvas, Row, FVector2D(0.0, 1.0), FVector2D(30.0, -292.0));
	}
}

void UTN_GhostHUDWidget::SetTextIfChanged(UTextBlock* Block, const FText& Value)
{
	if (Block && !Block->GetText().EqualTo(Value))
	{
		Block->SetText(Value);
	}
}

void UTN_GhostHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Time += InDeltaTime;
	APlayerController* PC = GetOwningPlayer();
	const UWorld* World = GetWorld();
	if (!PC || !World || !GhostCard || !WatchersCard)
	{
		return;
	}
	TArray<ATN_SpectatorGhost*> Ghosts;
	ATN_SpectatorGhost::GetActiveGhosts(World, Ghosts);
	const APlayerState* OwnState = PC->PlayerState;
	ATN_SpectatorGhost* Mine = nullptr;
	for (ATN_SpectatorGhost* Ghost : Ghosts)
	{
		if (Ghost->GetOwner() == PC || (OwnState && Ghost->GetGhostPlayerState() == OwnState))
		{
			Mine = Ghost;
		}
	}

	// ── Cartel del fantasma ──
	const bool bShowCard = Mine && Mine->GetStage() == ETNGhostStage::Spectating && !PC->GetPawn();
	if (bShowCard != bCardShown)
	{
		bCardShown = bShowCard;
		GhostCard->SetVisibility(bShowCard ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		CardPop = bShowCard ? 1.f : 0.f;
	}
	if (bShowCard)
	{
		SetTextIfChanged(OwnNameText, TNLocText::PlayerName(OwnState ? OwnState->GetPlayerName() : FString()));
		APawn* SubjectPawn = nullptr;
		APlayerState* Subject = nullptr;
		TNGhost::GetHUDSubject(PC, SubjectPawn, Subject);
		const bool bFollowing = SubjectPawn && Subject;
		const FText SubjectName = Subject ? TNLocText::Literal(Subject->GetPlayerName()) : FText::GetEmpty();
		SetTextIfChanged(FollowText, bFollowing
			? FText::Format(NSLOCTEXT("TNGhost", "Watching", "Mirando a {0}"), SubjectName)
			: NSLOCTEXT("TNGhost", "NobodyToWatch", "No queda nadie a quien mirar"));
		const UTN_InputDeviceSubsystem* Devices = UTN_InputDeviceSubsystem::Get(PC);
		SetTextIfChanged(CameraText, Mine->IsFreeCamera()
			? NSLOCTEXT("TNGhost", "FreeCamera", "Cámara libre: gira alrededor de la tortuga")
			: FText::Format(NSLOCTEXT("TNGhost", "FixedCamera", "Cámara fija: lo que ve {0}"), SubjectName));
		// Con mando, los nombres de los botones del que se tiene en las manos: LB/RB/RS en Xbox, L1/R1/R3 en PlayStation (#347).
		const bool bPad = UTN_GameSettingsSubsystem::IsUsingGamepad(PC);
		const ETNPadFamily Family = Devices ? Devices->GetPadFamily() : ETNPadFamily::Xbox;
		auto PadName = [Family](const FKey& Key) { return TNInputGlyphs::GlyphFor(Key, Family).Label; };
		SetTextIfChanged(KeysText, bPad
			? FText::Format(NSLOCTEXT("TNGhost", "KeysPadButtons", "{0} / {1}  cambiar   ·   {2}  cámara   ·   gatillos  zoom"),
				PadName(EKeys::Gamepad_LeftShoulder), PadName(EKeys::Gamepad_RightShoulder), PadName(EKeys::Gamepad_RightThumbstick))
			: NSLOCTEXT("TNGhost", "KeysMouse", "← / →  cambiar   ·   C  cámara   ·   rueda  zoom"));
		// Quién más mira a esa tortuga.
		TArray<FText> Others;
		for (const ATN_SpectatorGhost* Ghost : Ghosts)
		{
			if (Ghost != Mine && Subject && Ghost->GetFollowedPlayerState() == Subject && Ghost->GetGhostPlayerState())
			{
				Others.Add(TNLocText::Literal(Ghost->GetGhostPlayerState()->GetPlayerName()));
			}
		}
		AlsoText->SetVisibility(Others.Num() > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (Others.Num() > 0)
		{
			SetTextIfChanged(AlsoText, FText::Format(NSLOCTEXT("TNGhost", "AlsoWatching", "También miran: {0}"), TNLocText::JoinList(Others)));
		}
		// El icono flota y se mece; el cartel entra con un rebote.
		GhostIcon->SetRenderTranslation(FVector2D(0.0, -5.0 * FMath::Sin(Time * 2.4f)));
		GhostIcon->SetRenderTransformAngle(4.f * FMath::Sin(Time * 1.7f));
		CardPop = FMath::Max(0.f, CardPop - InDeltaTime * 3.f);
		GhostCard->SetRenderScale(FVector2D(1.f + 0.12f * FMath::Sin(CardPop * PI)));
	}

	// ── Con tortuga: quién te está mirando ──
	TArray<FText> Watchers;
	if (!Mine && OwnState && PC->GetPawn())
	{
		for (const ATN_SpectatorGhost* Ghost : Ghosts)
		{
			if (Ghost->GetStage() == ETNGhostStage::Spectating && Ghost->GetFollowedPlayerState() == OwnState && Ghost->GetGhostPlayerState())
			{
				Watchers.Add(TNLocText::Literal(Ghost->GetGhostPlayerState()->GetPlayerName()));
			}
		}
	}
	const bool bShowWatchers = Watchers.Num() > 0;
	if (bShowWatchers != bWatchersShown)
	{
		bWatchersShown = bShowWatchers;
		WatchersCard->SetVisibility(bShowWatchers ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		WatchersPop = bShowWatchers ? 1.f : 0.f;
	}
	if (bShowWatchers)
	{
		SetTextIfChanged(WatchersText, FText::Format(Watchers.Num() == 1
			? NSLOCTEXT("TNGhost", "WatchedByOne", "Te mira {0}")
			: NSLOCTEXT("TNGhost", "WatchedByMany", "Te miran {0}"), TNLocText::JoinList(Watchers)));
		WatchersIcon->SetRenderTranslation(FVector2D(0.0, -3.0 * FMath::Sin(Time * 2.6f)));
		WatchersPop = FMath::Max(0.f, WatchersPop - InDeltaTime * 3.f);
		WatchersCard->SetRenderScale(FVector2D(1.f + 0.15f * FMath::Sin(WatchersPop * PI)));
	}
}
