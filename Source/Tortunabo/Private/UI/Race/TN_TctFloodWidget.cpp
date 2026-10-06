#include "UI/Race/TN_TctFloodWidget.h"
#include "TN_RaceUIKit.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Game/TN_TctGameState.h"
#include "Game/TN_TctItemComponent.h"
#include "GameFramework/PlayerController.h"
#include "VR/TN_VRMode.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del bloque.
namespace TNTctFloodWidgetDetail
{
	constexpr float PillY = 28.f;
	constexpr float PillRight = -24.f;
	constexpr float WarningY = 120.f;
	constexpr float PoisonBottom = -150.f;
	constexpr float PoisonBarWidth = 300.f;
	constexpr float PoisonBarHeight = 22.f;
	const FLinearColor ToxicGreen = FLinearColor(0.55f, 0.92f, 0.18f);

	/** «0:17». */
	FText Clock(int32 Seconds)
	{
		return TNLocText::MinutesSeconds(FMath::Max(0, Seconds));
	}
}

void UTN_TctFloodWidget::NativeOnInitialized()
{
	BuildTree();
	Super::NativeOnInitialized();
}

void UTN_TctFloodWidget::BuildTree()
{
	using namespace TNTctFloodWidgetDetail;
	if (!WidgetTree || Canvas) { return; }
	UWidgetTree* Tree = WidgetTree;
	Canvas = TNRaceUI::Make<UCanvasPanel>(Tree, TEXT("TctFloodCanvas"));
	Tree->RootWidget = Canvas;
	SetVisibility(ESlateVisibility::HitTestInvisible);

	// Pastilla de la próxima subida y del tramo.
	{
		UHorizontalBox* Row = TNRaceUI::Make<UHorizontalBox>(Tree);
		UVerticalBox* Col = TNRaceUI::Make<UVerticalBox>(Tree);
		PillLabel = TNRaceUI::MakeText(Tree, NSLOCTEXT("TNTct", "FloodNextLabel", "PRÓXIMA SUBIDA"), TEXT("Bold"), 13, TNHUDArt::SandC, false);
		if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(PillLabel)) { S->SetHorizontalAlignment(HAlign_Center); }
		PillTime = TNRaceUI::MakeText(Tree, Clock(0), TEXT("Black"), 34, TNHUDArt::Cream);
		PillTime->SetJustification(ETextJustify::Center);
		if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(PillTime)) { S->SetHorizontalAlignment(HAlign_Center); S->SetPadding(FMargin(0.f, -4.f, 0.f, 0.f)); }
		PillTier = TNRaceUI::MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 14, TNHUDArt::ShellGreen, false);
		if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(PillTier)) { S->SetHorizontalAlignment(HAlign_Center); }
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Col)) { S->SetVerticalAlignment(VAlign_Center); }

		UBorder* Card = TNRaceUI::Make<UBorder>(Tree);
		Card->SetBrush(TNHUDStyle::Rounded(TNHUDArt::Hex(0x12305A, 0.9f), 22.f, ToxicGreen, 3.f));
		Card->SetPadding(FMargin(20.f, 8.f, 20.f, 10.f));
		Card->SetContent(Row);
		Card->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		Pill = Card;
		TNRaceUI::Place(Canvas, Card, FVector2D(1.f, 0.f), FVector2D(PillRight, PillY));
	}

	// Cinta del aviso antes de cada subida.
	{
		UVerticalBox* Col = TNRaceUI::Make<UVerticalBox>(Tree);
		WarningText = TNRaceUI::MakeText(Tree, FText::GetEmpty(), TEXT("Black"), 34, FLinearColor::White);
		UBorder* Ribbon = TNRaceUI::MakeCard(Tree, TNHUDArt::RibbonTexture(), TNRaceUI::RibbonMargin, WarningText, FMargin(56.f, 14.f, 56.f, 18.f));
		if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(Ribbon)) { S->SetHorizontalAlignment(HAlign_Center); }
		WarningSubText = TNRaceUI::MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 19, TNHUDArt::Ink, false);
		UBorder* Tag = TNRaceUI::MakeCard(Tree, TNHUDArt::SandTagTexture(), TNRaceUI::TagMargin, WarningSubText, FMargin(30.f, 11.f, 30.f, 13.f));
		if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(Tag)) { S->SetHorizontalAlignment(HAlign_Center); S->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f)); }
		Col->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		Col->SetVisibility(ESlateVisibility::Collapsed);
		Warning = Col;
		TNRaceUI::Place(Canvas, Col, FVector2D(0.5f, 0.f), FVector2D(0.f, WarningY));
	}

	// Veneno de la tortuga propia: una barra verde que se llena.
	{
		UVerticalBox* Col = TNRaceUI::Make<UVerticalBox>(Tree);
		PoisonText = TNRaceUI::MakeText(Tree, NSLOCTEXT("TNTct", "PoisonLabel", "VENENO"), TEXT("Black"), 18, ToxicGreen);
		if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(PoisonText)) { S->SetHorizontalAlignment(HAlign_Center); }
		UOverlay* Bar = TNRaceUI::Make<UOverlay>(Tree);
		UBorder* Back = TNRaceUI::Make<UBorder>(Tree);
		Back->SetBrush(TNHUDStyle::Rounded(FLinearColor(0.f, 0.05f, 0.02f, 0.7f), 11.f, FLinearColor(1.f, 1.f, 1.f, 0.35f), 2.f));
		TNRaceUI::AddAt(Bar, Back, HAlign_Fill, VAlign_Fill);
		PoisonFill = TNRaceUI::Make<UBorder>(Tree);
		PoisonFill->SetBrush(TNHUDStyle::Rounded(FLinearColor::White, 9.f));
		PoisonFill->SetBrushColor(ToxicGreen);
		PoisonFill->SetRenderTransformPivot(FVector2D(0.f, 0.5f));
		TNRaceUI::AddAt(Bar, PoisonFill, HAlign_Fill, VAlign_Fill, FMargin(3.f));
		if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(TNRaceUI::MakeSize(Tree, Bar, PoisonBarWidth, PoisonBarHeight))) { S->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f)); }
		Col->SetVisibility(ESlateVisibility::Collapsed);
		PoisonBox = Col;
		TNRaceUI::Place(Canvas, Col, FVector2D(0.5f, 1.f), FVector2D(0.f, PoisonBottom));
	}
	SetRenderOpacity(0.f);
}

void UTN_TctFloodWidget::SetView(const FTNTctFloodView& InView)
{
	using namespace TNTctFloodWidgetDetail;
	BuildTree();
	View = InView;
	if (DismissAt >= 0.f) { DismissAt = -1.f; }
	const FTNTctNextRise& Next = View.Next;

	// Pastilla: cuenta atrás de la próxima subida y tramo («TRAMO 2 DE 4»); con el agua subiendo, lo dice; sin más subidas, también.
	const int32 Seconds = Next.bUpcoming ? FMath::CeilToInt(Next.SecondsLeft) : -1;
	if (Seconds != ShownSeconds || Next.bRising != bShownRising || Next.bSuddenDeath != bShownFinal)
	{
		ShownSeconds = Seconds;
		bShownRising = Next.bRising;
		bShownFinal = Next.bSuddenDeath;
		PopAt = Time;
		if (PillTime)
		{
			PillTime->SetText(Next.bUpcoming ? Clock(Seconds) : INVTEXT("—"));
			const bool bHurry = Next.bUpcoming && Next.SecondsLeft <= TNTctPoisonDefaults::WarnSeconds;
			PillTime->SetColorAndOpacity(FSlateColor(bHurry ? TNHUDArt::CoralLight : TNHUDArt::Cream));
		}
		if (PillLabel)
		{
			PillLabel->SetText(Next.bRising ? NSLOCTEXT("TNTct", "FloodRisingLabel", "EL AGUA SUBE")
				: (Next.bSuddenDeath ? NSLOCTEXT("TNTct", "FloodFinalLabel", "MAREA FINAL EN") : NSLOCTEXT("TNTct", "FloodNextLabel", "PRÓXIMA SUBIDA")));
		}
		if (PillTier)
		{
			PillTier->SetText(Next.bUpcoming
				? FText::Format(NSLOCTEXT("TNTct", "FloodTier", "TRAMO {0} DE {1}"), TNLocText::Int(Next.Step + 1), TNLocText::Int(Next.Tiers))
				: NSLOCTEXT("TNTct", "FloodTierDone", "Ya no sube más"));
		}
	}

	// Cinta de aviso: en los segundos antes de cada subida, con la cuenta atrás; el número entero rebota al cambiar.
	const bool bWarn = Next.bUpcoming && !Next.bRising && Next.SecondsLeft <= TNTctPoisonDefaults::WarnSeconds;
	if (Warning)
	{
		Warning->SetVisibility(bWarn ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (bWarn && Seconds != ShownWarnSeconds)
	{
		ShownWarnSeconds = Seconds;
		if (WarningText)
		{
			WarningText->SetText(FText::Format(Next.bSuddenDeath
				? NSLOCTEXT("TNTct", "FloodWarnFinal", "¡La marea final llega en {0}!")
				: NSLOCTEXT("TNTct", "FloodWarn", "¡El agua sube en {0}!"), TNLocText::Int(Seconds)));
		}
		if (WarningSubText)
		{
			WarningSubText->SetText(Next.bSuddenDeath
				? NSLOCTEXT("TNTct", "FloodWarnFinalSub", "Cubrirá todo el mapa: sube a lo más alto")
				: NSLOCTEXT("TNTct", "FloodWarnSub", "Llegará hasta la marca verde: el agua es veneno"));
		}
		PopAt = Time;
	}
	else if (!bWarn)
	{
		ShownWarnSeconds = -1;
	}

	// Veneno propio: visible mientras queda algo.
	if (PoisonBox)
	{
		PoisonBox->SetVisibility(View.Poison > 0.01f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (PoisonFill)
	{
		PoisonFill->SetRenderScale(FVector2D(FMath::Clamp(View.Poison, 0.f, 1.f), 1.f));
	}
}

void UTN_TctFloodWidget::Dismiss()
{
	if (DismissAt < 0.f) { DismissAt = Time; }
}

void UTN_TctFloodWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	using namespace TNTctFloodWidgetDetail;
	Super::NativeTick(MyGeometry, InDeltaTime);
	Time += FMath::Min(InDeltaTime, 0.1f);

	float Opacity = FMath::Clamp(Time / 0.25f, 0.f, 1.f);
	if (DismissAt >= 0.f)
	{
		const float Out = FMath::Clamp((Time - DismissAt) / 0.3f, 0.f, 1.f);
		Opacity *= 1.f - Out;
		if (Out >= 1.f)
		{
			RemoveFromParent();
			return;
		}
	}
	SetRenderOpacity(Opacity);

	// Rebote al cambiar el segundo; en los de aviso, la pastilla late.
	const float Since = Time - PopAt;
	const bool bHurry = View.Next.bUpcoming && View.Next.SecondsLeft <= TNTctPoisonDefaults::WarnSeconds;
	if (Pill)
	{
		const float Pop = 1.f + (bHurry ? 0.14f : 0.05f) * FMath::Exp(-Since * 8.f) * FMath::Cos(Since * 16.f);
		Pill->SetRenderScale(FVector2D(Pop, Pop));
	}
	if (Warning && Warning->GetVisibility() != ESlateVisibility::Collapsed)
	{
		const float Pop = TNRaceUI::PopIn(Since / 0.25f);
		Warning->SetRenderScale(FVector2D(Pop, Pop));
		Warning->SetRenderTransformAngle(1.2f * FMath::Sin(Time * 2.2f));
	}
	// Con mucho veneno, la barra late en coral.
	if (PoisonFill)
	{
		const float Beat = View.Poison > 0.75f ? 0.5f + 0.5f * FMath::Sin(Time * 14.f) : 0.f;
		PoisonFill->SetBrushColor(FMath::Lerp(ToxicGreen, TNHUDArt::CoralC, Beat));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Subsistema
// ─────────────────────────────────────────────────────────────────────────────

bool UTN_TctHudSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

bool UTN_TctHudSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UTN_TctHudSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTN_TctHudSubsystem, STATGROUP_Tickables);
}

void UTN_TctHudSubsystem::Deinitialize()
{
	if (IsValid(Widget)) { Widget->RemoveFromParent(); }
	Widget = nullptr;
	Super::Deinitialize();
}

void UTN_TctHudSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown) { return; }
	const ATN_TctGameState* State = World->GetGameState<ATN_TctGameState>();
	APlayerController* PC = nullptr;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (It->Get() && It->Get()->IsLocalController()) { PC = It->Get(); break; }
	}
	FTNTctFloodView View;
	const bool bWanted = PC && State && State->RacePhase == ETNBeachRacePhase::Racing && State->GetNextRise(View.Next);
	if (!bWanted)
	{
		if (IsValid(Widget)) { Widget->Dismiss(); }
		Widget = nullptr;
		return;
	}
	if (const UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOn(PC->GetPawn()))
	{
		View.Poison = Effects->GetPoison();
	}
	if (!Widget || Widget->IsDismissing())
	{
		Widget = CreateWidget<UTN_TctFloodWidget>(PC, UTN_TctFloodWidget::StaticClass());
		if (!Widget) { return; }
		TNVR::AddToFullScreen(Widget, ZOrder);
	}
	Widget->SetView(View);
}
