#include "Kart/TN_KartHUDWidget.h"

#include "../UI/Race/TN_RaceUIKit.h"
#include "../World/Beach/TN_RaceItemArt.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kart/TN_KartBuggy.h"
#include "Kart/TN_KartGameState.h"
#include "Kart/TN_KartGunnerPawn.h"
#include "Kart/TN_KartItemComponent.h"
#include "Kart/TN_KartTrack.h"
#include "Rally/TN_RallyPlayerState.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "Settings/TN_InputDeviceSubsystem.h"
#include "VR/TN_VRControls.h"
#include "VR/TN_VRMode.h"

namespace TNKartHUD
{
	/** Caras de la ruleta por segundo y objetos que pasan por ella. */
	constexpr double RouletteFacesPerSecond = 12.0;
	constexpr ETNKartItem RouletteFaces[] = { ETNKartItem::Coco, ETNKartItem::Concha, ETNKartItem::Alga, ETNKartItem::ConchaGuiada,
		ETNKartItem::Tinta, ETNKartItem::Estrella, ETNKartItem::TripleCoco, ETNKartItem::Mortero, ETNKartItem::Erizos, ETNKartItem::Medusa,
		ETNKartItem::PezGlobo, ETNKartItem::Arpon };
	constexpr float DistanceRefreshSeconds = 0.25f;
	constexpr float IconSize = 76.f;

	void Show(UWidget* Widget, bool bVisible)
	{
		if (Widget)
		{
			Widget->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
}

void UTN_KartHUDWidget::NativeOnInitialized()
{
	BuildTree();
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UTN_KartHUDWidget::BuildTree()
{
	using namespace TNRaceUI;
	if (!WidgetTree || Canvas)
	{
		return;
	}
	UWidgetTree* Tree = WidgetTree;
	Canvas = Make<UCanvasPanel>(Tree, TEXT("KartCanvas"));
	Tree->RootWidget = Canvas;

	// Cartel del objeto: icono a la izquierda y nombre con su ayuda a la derecha.
	UHorizontalBox* Row = Make<UHorizontalBox>(Tree);
	ItemImage = MakeImage(Tree, nullptr, FVector2D(TNKartHUD::IconSize));
	if (UHorizontalBoxSlot* IconSlot = Row->AddChildToHorizontalBox(MakeSize(Tree, ItemImage, TNKartHUD::IconSize, TNKartHUD::IconSize)))
	{
		IconSlot->SetVerticalAlignment(VAlign_Center);
		IconSlot->SetPadding(FMargin(0.f, 0.f, 14.f, 0.f));
	}
	UVerticalBox* Texts = Make<UVerticalBox>(Tree);
	ItemText = MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 30, TNHUDArt::Gold);
	ItemHint = MakeText(Tree, FText::GetEmpty(), TEXT("Regular"), 18, TNHUDStyle::TextDim);
	Texts->AddChildToVerticalBox(ItemText);
	Texts->AddChildToVerticalBox(ItemHint);
	if (UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Texts))
	{
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}
	ItemCard = MakeCard(Tree, TNHUDArt::CardTexture(), CardMargin, Row, FMargin(22.f, 12.f, 26.f, 18.f));
	Place(Canvas, ItemCard, FVector2D(0.5f, 0.f), FVector2D(0.f, 20.f));

	DistanceText = MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 22, FLinearColor::White);
	Place(Canvas, DistanceText, FVector2D(0.5f, 0.f), FVector2D(0.f, 140.f));

	// Peso de la artillera: dos barras que crecen hacia cada lado desde el centro.
	UVerticalBox* Lean = Make<UVerticalBox>(Tree);
	LeanLabel = MakeText(Tree, NSLOCTEXT("Karts", "LeanLabel", "Peso (A/D · stick izquierdo)"), TEXT("Regular"), 16,
		TNHUDStyle::TextDim);
	if (UVerticalBoxSlot* LabelSlot = Lean->AddChildToVerticalBox(LeanLabel))
	{
		LabelSlot->SetHorizontalAlignment(HAlign_Center);
	}
	UHorizontalBox* Bars = Make<UHorizontalBox>(Tree);
	LeanLeft = Make<UProgressBar>(Tree);
	LeanLeft->SetBarFillType(EProgressBarFillType::RightToLeft);
	LeanLeft->SetFillColorAndOpacity(TNHUDArt::Gold);
	LeanRight = Make<UProgressBar>(Tree);
	LeanRight->SetFillColorAndOpacity(TNHUDArt::Gold);
	Bars->AddChildToHorizontalBox(MakeSize(Tree, LeanLeft, 110.f, 12.f));
	Bars->AddChildToHorizontalBox(MakeSize(Tree, LeanRight, 110.f, 12.f));
	Lean->AddChildToVerticalBox(Bars);
	LeanPanel = Lean;
	Place(Canvas, LeanPanel, FVector2D(0.5f, 1.f), FVector2D(0.f, -150.f));

	TNKartHUD::Show(ItemCard, false);
	TNKartHUD::Show(LeanPanel, false);
}

ATN_KartBuggy* UTN_KartHUDWidget::FindLocalKart(bool& bOutGunner) const
{
	bOutGunner = false;
	const APlayerController* Player = GetOwningPlayer();
	const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	if (ATN_KartBuggy* Driven = Cast<ATN_KartBuggy>(const_cast<APawn*>(Pawn)))
	{
		return Driven;
	}
	if (const ATN_KartGunnerPawn* Gunner = Cast<ATN_KartGunnerPawn>(Pawn))
	{
		bOutGunner = true;
		return Cast<ATN_KartBuggy>(Gunner->GetBuggy());
	}
	// Sin peón propio (reaparición, cambio de plaza): el kart de su fila de puestos.
	const UWorld* World = GetWorld();
	const ATN_RallyGameState* RallyState = World ? World->GetGameState<ATN_RallyGameState>() : nullptr;
	const APlayerState* Mine = Player ? Player->GetPlayerState<APlayerState>() : nullptr;
	const FTNRallyStanding* Entry = RallyState ? RallyState->FindStandingForPlayer(Mine) : nullptr;
	if (Entry)
	{
		bOutGunner = Entry->Gunner == Mine;
		return Cast<ATN_KartBuggy>(Entry->Vehicle);
	}
	return nullptr;
}

UTexture2D* UTN_KartHUDWidget::ItemIcon(ETNKartItem Item, int32 Charges)
{
	switch (Item)
	{
	case ETNKartItem::Coco: return TNRaceItemArt::GetIcon(ETNRaceItem::Coconut);
	case ETNKartItem::TripleCoco:
		return TNRaceItemArt::GetIcon(Charges >= 3 ? ETNRaceItem::TripleCoconut3 : (Charges == 2 ? ETNRaceItem::TripleCoconut2 : ETNRaceItem::TripleCoconut1));
	case ETNKartItem::Concha: return TNHUDArt::ShellIcon();
	case ETNKartItem::ConchaGuiada: return TNHUDArt::ShellIconTier(3);
	case ETNKartItem::Alga: return TNHUDArt::SeaIcon();
	case ETNKartItem::Tinta: return TNHUDArt::StormIcon();
	case ETNKartItem::Estrella: return TNHUDArt::BubbleIcon();
	// #774: los mismos iconos que la munición de la torreta en el HUD del Rally.
	case ETNKartItem::Mortero: return TNRaceItemArt::GetIcon(ETNRaceItem::GoldenCoconut);
	case ETNKartItem::Erizos: return TNRaceItemArt::GetIcon(ETNRaceItem::HomingCrab);
	case ETNKartItem::Medusa: return TNRaceItemArt::GetIcon(ETNRaceItem::PelicanTaxi);
	case ETNKartItem::PezGlobo: return TNRaceItemArt::GetIcon(ETNRaceItem::SandMine);
	case ETNKartItem::Arpon: return TNHUDArt::RopeRing();
	default: return nullptr;
	}
}

void UTN_KartHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	bool bGunner = false;
	const ATN_KartBuggy* Kart = FindLocalKart(bGunner);
	// Con artillera, la conductora va sin interfaz: el objeto y la distancia los ve la artillera (#718).
	const bool bDriverWithGunner = Kart && !bGunner && Kart->HasGunner();
	RefreshItem(bDriverWithGunner ? nullptr : Kart, bGunner);
	RefreshDistance(bDriverWithGunner ? nullptr : Kart, InDeltaTime);
	RefreshLean(bGunner && Kart);
}

void UTN_KartHUDWidget::RefreshItem(const ATN_KartBuggy* Kart, bool bGunner)
{
	using namespace TNKartHUD;
	const UTN_KartItemComponent* Items = Kart ? Kart->GetItems() : nullptr;
	const float Star = Items ? Items->GetStarSecondsLeft() : 0.f;
	const ETNKartItem Item = Items ? Items->GetItem() : ETNKartItem::None;
	Show(ItemCard, Item != ETNKartItem::None || Star > 0.f);
	if (!Items || (Item == ETNKartItem::None && Star <= 0.f))
	{
		return;
	}
	if (Item == ETNKartItem::None)
	{
		// Estrella en marcha: sus segundos.
		TNRaceUI::SetImageTexture(ItemImage, ItemIcon(ETNKartItem::Estrella, 1));
		ItemText->SetText(FText::Format(NSLOCTEXT("Karts", "StarLeft", "¡{0}! {1} s"), TNKart::ItemName(ETNKartItem::Estrella),
			TNLocText::Int(FMath::CeilToInt(Star))));
		ItemHint->SetText(NSLOCTEXT("Karts", "StarHint", "Invulnerable: aparta a los karts que tocas"));
		return;
	}
	if (Items->GetRouletteSecondsLeft() > 0.f)
	{
		// Ruleta: van pasando los objetos hasta que sale el que ha tocado.
		const int32 Face = static_cast<int32>(FPlatformTime::Seconds() * RouletteFacesPerSecond) % UE_ARRAY_COUNT(RouletteFaces);
		TNRaceUI::SetImageTexture(ItemImage, ItemIcon(RouletteFaces[Face], 3));
		ItemText->SetText(NSLOCTEXT("Karts", "Roulette", "¿Qué toca?"));
		ItemHint->SetText(FText::GetEmpty());
		return;
	}
	TNRaceUI::SetImageTexture(ItemImage, ItemIcon(Item, Items->GetCharges()));
	const FText Name = TNKart::ItemName(Item);
	ItemText->SetText(Items->GetCharges() > 1
		? FText::Format(NSLOCTEXT("Karts", "ItemCharges", "{0} ×{1}"), Name, TNLocText::Int(Items->GetCharges())) : Name);
	const APlayerController* Player = GetOwningPlayer();
	if (Kart->MayUseItems(Player))
	{
		// Con gafas (#644): el botón de los Touch (B conduciendo, el gatillo izquierdo de artillera), no las teclas ni el mando.
		const UTN_InputDeviceSubsystem* Devices = UTN_InputDeviceSubsystem::Get(Player);
		if (Devices && Devices->IsUsingVR(Player))
		{
			ItemHint->SetText(FText::Format(NSLOCTEXT("Karts", "UseHintVR", "Usar: {0}"),
				TNVRControls::KeyName(bGunner ? FTNVRKeys::LeftTrigger : FTNVRKeys::B)));
			return;
		}
		ItemHint->SetText(bGunner ? NSLOCTEXT("Karts", "UseHintGunner", "Usar: E · clic derecho · LT (con Q o LB, hacia atrás)")
			: NSLOCTEXT("Karts", "UseHintDriver", "Usar: E · clic derecho · LB (con Q o B, hacia atrás)"));
	}
	else
	{
		ItemHint->SetText(NSLOCTEXT("Karts", "UseHintPartner", "Lo usa tu artillera"));
	}
}

void UTN_KartHUDWidget::RefreshDistance(const ATN_KartBuggy* Kart, float DeltaSeconds)
{
	DistanceAccumulator += DeltaSeconds;
	if (DistanceAccumulator < TNKartHUD::DistanceRefreshSeconds)
	{
		return;
	}
	DistanceAccumulator = 0.f;
	const UWorld* World = GetWorld();
	const ATN_KartGameState* KartState = World ? World->GetGameState<ATN_KartGameState>() : nullptr;
	const ATN_KartTrack* Track = KartState ? KartState->GetKartTrack() : nullptr;
	const FTNRallyStanding* Mine = KartState && Kart ? KartState->FindStandingForVehicle(Kart) : nullptr;
	const bool bShow = Track && Track->IsBuilt() && Kart && Mine && !Mine->bFinished
		&& (KartState->Phase == ETNRallyPhase::Racing || KartState->Phase == ETNRallyPhase::Finishing);
	TNKartHUD::Show(DistanceText, bShow);
	if (!bShow)
	{
		LocalArcCm = -1.0;
		return;
	}
	const FVector At = Kart->GetActorLocation();
	LocalArcCm = LocalArcCm < 0.0 ? Track->FindArcGlobal(At) : Track->FindArcNear(At, LocalArcCm);
	const double Left = FMath::Max(0.0, Track->GetFinishArc() - LocalArcCm);
	FNumberFormattingOptions Km;
	Km.MinimumFractionalDigits = 1;
	Km.MaximumFractionalDigits = 1;
	DistanceText->SetText(FText::Format(NSLOCTEXT("Karts", "KmLeft", "Quedan {0} km"), FText::AsNumber(Left / 100000.0, &Km)));
}

void UTN_KartHUDWidget::RefreshLean(bool bGunner)
{
	TNKartHUD::Show(LeanPanel, bGunner);
	if (!bGunner)
	{
		return;
	}
	const APlayerController* Player = GetOwningPlayer();
	if (LeanLabel)
	{
		// Con gafas (#644) el peso se echa con la cabeza (y se suma el stick izquierdo).
		const UTN_InputDeviceSubsystem* Devices = UTN_InputDeviceSubsystem::Get(Player);
		LeanLabel->SetText(Devices && Devices->IsUsingVR(Player)
			? FText::Format(NSLOCTEXT("Karts", "LeanLabelVR", "Peso (cabeza · {0})"), TNVRControls::KeyName(FTNVRKeys::LeftStickX))
			: NSLOCTEXT("Karts", "LeanLabel", "Peso (A/D · stick izquierdo)"));
	}
	const ATN_KartGunnerPawn* Gunner = Player ? Cast<ATN_KartGunnerPawn>(Player->GetPawn()) : nullptr;
	const float Lean = Gunner ? Gunner->GetLocalLean() : 0.f;
	LeanLeft->SetPercent(FMath::Max(0.f, -Lean));
	LeanRight->SetPercent(FMath::Max(0.f, Lean));
}
