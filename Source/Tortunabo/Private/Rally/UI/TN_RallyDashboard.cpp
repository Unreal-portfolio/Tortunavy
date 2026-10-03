#include "Rally/UI/TN_RallyDashboard.h"

#include "../../UI/Race/TN_RaceUIKit.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/ProgressBar.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextBlock.h"
#include "Components/WidgetComponent.h"
#include "Core/TN_LocText.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Rally/TN_RallyGameState.h"
#include "Rally/TN_RallyLogic.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyHealthComponent.h"

namespace TNRallyDashboard
{
	/** Refresco de los textos (s): los paneles no necesitan más. */
	constexpr float RefreshSeconds = 0.1f;
	/** Fondo de los paneles: el azul de la interfaz, más opaco para que se lea sobre la arena. */
	const FLinearColor PanelFill(0.012f, 0.045f, 0.08f, 0.88f);

	FPanelLayout DashLayout()
	{
		// Sobre el morro, a la derecha (lado de la artillera): desde la cámara de persecución no lo tapan ni la conductora, ni
		// el cañón de la torreta (en el centro) ni el cartel del arco (a la izquierda).
		FPanelLayout Layout;
		Layout.OffsetFromDriverSeat = FVector(85.0, 115.0, 80.0);
		Layout.Rotation = FRotator(15.0, 180.0, 0.0);
		Layout.DrawSizePx = FIntPoint(400, 220);
		Layout.CmPerPx = 0.3f;
		Layout.MainFontPx = 128;
		return Layout;
	}

	FPanelLayout RollBarLayout()
	{
		// En lo alto del arco, detrás de la cabeza de la conductora y hacia fuera (a la izquierda del cañón de la torreta),
		// mirando a la cámara de persecución.
		FPanelLayout Layout;
		Layout.OffsetFromDriverSeat = FVector(-45.0, -50.0, 150.0);
		Layout.Rotation = FRotator(10.0, 180.0, 0.0);
		Layout.DrawSizePx = FIntPoint(520, 240);
		Layout.CmPerPx = 0.3f;
		Layout.MainFontPx = 100;
		return Layout;
	}

	FPanelLayout CallLayout()
	{
		// Encima del salpicadero (mismo lado y misma inclinación), para no tapar la velocidad mientras se frena para la curva.
		FPanelLayout Layout;
		Layout.OffsetFromDriverSeat = FVector(85.0, 115.0, 152.0);
		Layout.Rotation = FRotator(12.0, 180.0, 0.0);
		Layout.DrawSizePx = FIntPoint(480, 230);
		Layout.CmPerPx = 0.3f;
		Layout.MainFontPx = 112;
		return Layout;
	}

	float MainGlyphCm(const FPanelLayout& Layout)
	{
		return Layout.MainFontPx * CapHeightRatio * Layout.CmPerPx;
	}

	float ProjectedGlyphPx(float GlyphCm, float DistanceCm, float HorizontalFovDeg, const FIntPoint& ScreenSize)
	{
		if (DistanceCm <= 0.f || ScreenSize.X <= 0 || ScreenSize.Y <= 0)
		{
			return 0.f;
		}
		const float HalfWidthAtDistance = DistanceCm * FMath::Tan(FMath::DegreesToRadians(HorizontalFovDeg * 0.5f));
		const float PxPerCm = ScreenSize.X / (2.f * HalfWidthAtDistance);
		return GlyphCm * PxPerCm;
	}

	FText PlaceLine(int32 Place, int32 Total)
	{
		return FText::Format(NSLOCTEXT("Rally", "PlaceOfTotal", "{0}.º / {1}"), TNLocText::Int(Place), TNLocText::Int(Total));
	}

	FText RaceTime(float Seconds)
	{
		const float Safe = FMath::Max(0.f, Seconds);
		const int32 Minutes = FMath::FloorToInt(Safe / 60.f);
		FNumberFormattingOptions Options;
		Options.UseGrouping = false;
		Options.MinimumIntegralDigits = 2;
		Options.MinimumFractionalDigits = 1;
		Options.MaximumFractionalDigits = 1;
		Options.RoundingMode = ERoundingMode::ToZero;
		return FText::Format(NSLOCTEXT("Rally", "RaceTime", "{0}:{1}"), TNLocText::Int(Minutes),
			FText::AsNumber(Safe - 60.f * Minutes, &Options));
	}

	FText LapLine(const FTNRallyStanding& Mine, int32 Laps, int32 NumGates, bool bCircuit)
	{
		if (Mine.bFinished)
		{
			return FText::Format(NSLOCTEXT("Rally", "FinishedTime", "¡Meta! {0}"), RaceTime(Mine.FinishSeconds));
		}
		const int32 GateTotal = bCircuit ? NumGates : FMath::Max(0, NumGates - 1);
		const int32 GateShown = (bCircuit && Mine.NextGate == 0) ? (Mine.Lap > 0 ? GateTotal : 0) : Mine.NextGate;
		const FText Gate = FText::Format(NSLOCTEXT("Rally", "GateOfTotal", "Puerta {0}/{1}"), TNLocText::Int(GateShown),
			TNLocText::Int(GateTotal));
		if (Laps > 1)
		{
			return FText::Format(NSLOCTEXT("Rally", "LapAndGate", "Vuelta {0}/{1} · {2}"), TNLocText::Int(FMath::Max(1, Mine.Lap)),
				TNLocText::Int(Laps), Gate);
		}
		return Gate;
	}
}

// ── Widget ─────────────────────────────────────────────────────────────────────────────────────────────────────

void UTN_RallyDashboardWidget::Configure(ATN_Buggy* InBuggy, ETNRallyDashboardPanel InPanel)
{
	Buggy = InBuggy;
	Panel = InPanel;
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}
	switch (Panel)
	{
	case ETNRallyDashboardPanel::Dash: BuildDash(); break;
	case ETNRallyDashboardPanel::RollBar: BuildRollBar(); break;
	default: BuildCall(); break;
	}
}

void UTN_RallyDashboardWidget::ShowCall(const FText& Headline, const FText& Detail, const FLinearColor& Accent, float Seconds)
{
	if (Panel != ETNRallyDashboardPanel::Call || !CallContent || !MainText || !SubText)
	{
		return;
	}
	TNHUDStyle::StylePanel(CallBack, TNRallyDashboard::PanelFill, 26.f, FMargin(0.f), Accent, 5.f);
	MainText->SetText(Headline);
	MainText->SetColorAndOpacity(FSlateColor(Accent));
	SubText->SetText(Detail);
	CallRemaining = FMath::Max(0.f, Seconds);
	CallContent->SetVisibility(CallRemaining > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UTN_RallyDashboardWidget::TickCall(float DeltaTime)
{
	if (CallRemaining <= 0.f || !CallContent)
	{
		return;
	}
	CallRemaining -= DeltaTime;
	if (CallRemaining <= 0.f)
	{
		CallContent->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UTN_RallyDashboardWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UTN_RallyDashboardWidget::BuildDash()
{
	using namespace TNRaceUI;
	const TNRallyDashboard::FPanelLayout Layout = TNRallyDashboard::DashLayout();
	UCanvasPanel* Canvas = Make<UCanvasPanel>(WidgetTree, TEXT("DashCanvas"));
	WidgetTree->RootWidget = Canvas;
	UBorder* Back = Make<UBorder>(WidgetTree);
	TNHUDStyle::StylePanel(Back, TNRallyDashboard::PanelFill, 28.f, FMargin(0.f), TNHUDArt::Cream, 4.f);
	Fill(Canvas, Back);
	MainText = MakeText(WidgetTree, FText::GetEmpty(), TEXT("Bold"), Layout.MainFontPx, FLinearColor::White);
	Place(Canvas, MainText, FVector2D(0.5f, 0.42f), FVector2D(-40.f, 0.f));
	SubText = MakeText(WidgetTree, NSLOCTEXT("Rally", "SpeedUnit", "km/h"), TEXT("Bold"), 34, TNHUDStyle::TextDim);
	Place(Canvas, SubText, FVector2D(0.86f, 0.52f), FVector2D::ZeroVector);
	BoostBar = Make<UProgressBar>(WidgetTree);
	BoostBar->SetWidgetStyle(TNHUDStyle::Bar(12.f));
	Place(Canvas, MakeSize(WidgetTree, BoostBar, Layout.DrawSizePx.X - 60.f, 28.f), FVector2D(0.5f, 0.84f), FVector2D::ZeroVector);
	HealthBar = Make<UProgressBar>(WidgetTree);
	HealthBar->SetWidgetStyle(TNHUDStyle::Bar(5.f));
	Place(Canvas, MakeSize(WidgetTree, HealthBar, Layout.DrawSizePx.X - 60.f, 10.f), FVector2D(0.5f, 0.1f), FVector2D::ZeroVector);
}

void UTN_RallyDashboardWidget::BuildRollBar()
{
	using namespace TNRaceUI;
	const TNRallyDashboard::FPanelLayout Layout = TNRallyDashboard::RollBarLayout();
	UCanvasPanel* Canvas = Make<UCanvasPanel>(WidgetTree, TEXT("RollBarCanvas"));
	WidgetTree->RootWidget = Canvas;
	UBorder* Back = Make<UBorder>(WidgetTree);
	TNHUDStyle::StylePanel(Back, TNRallyDashboard::PanelFill, 22.f, FMargin(0.f), TNHUDArt::Gold, 4.f);
	Fill(Canvas, Back);
	MainText = MakeText(WidgetTree, FText::GetEmpty(), TEXT("Bold"), Layout.MainFontPx, TNHUDArt::Gold);
	Place(Canvas, MainText, FVector2D(0.5f, 0.36f), FVector2D::ZeroVector);
	SubText = MakeText(WidgetTree, FText::GetEmpty(), TEXT("Bold"), 32, FLinearColor::White);
	Place(Canvas, SubText, FVector2D(0.5f, 0.84f), FVector2D::ZeroVector);
}

void UTN_RallyDashboardWidget::BuildCall()
{
	using namespace TNRaceUI;
	const TNRallyDashboard::FPanelLayout Layout = TNRallyDashboard::CallLayout();
	UCanvasPanel* Canvas = Make<UCanvasPanel>(WidgetTree, TEXT("CallCanvas"));
	WidgetTree->RootWidget = Canvas;
	UCanvasPanel* Content = Make<UCanvasPanel>(WidgetTree, TEXT("CallContent"));
	Fill(Canvas, Content);
	CallContent = Content;
	CallBack = Make<UBorder>(WidgetTree);
	TNHUDStyle::StylePanel(CallBack, TNRallyDashboard::PanelFill, 26.f, FMargin(0.f), TNHUDArt::Gold, 5.f);
	Fill(Content, CallBack);
	MainText = MakeText(WidgetTree, FText::GetEmpty(), TEXT("Bold"), Layout.MainFontPx, TNHUDArt::Gold);
	Place(Content, MainText, FVector2D(0.5f, 0.38f), FVector2D::ZeroVector);
	SubText = MakeText(WidgetTree, FText::GetEmpty(), TEXT("Bold"), 30, FLinearColor::White);
	SubText->SetJustification(ETextJustify::Center);
	SubText->SetAutoWrapText(true);
	SubText->SetWrapTextAt(Layout.DrawSizePx.X - 40.f);
	Place(Content, SubText, FVector2D(0.5f, 0.84f), FVector2D::ZeroVector);
	Content->SetVisibility(ESlateVisibility::Collapsed);
}

void UTN_RallyDashboardWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (Panel == ETNRallyDashboardPanel::Call)
	{
		TickCall(InDeltaTime);
		return;
	}
	RefreshAccumulator += InDeltaTime;
	const ATN_Buggy* Target = Buggy.Get();
	if (!Target || !MainText || RefreshAccumulator < TNRallyDashboard::RefreshSeconds)
	{
		return;
	}
	RefreshAccumulator = 0.f;
	Panel == ETNRallyDashboardPanel::Dash ? RefreshDash(*Target) : RefreshRollBar(*Target);
}

void UTN_RallyDashboardWidget::RefreshDash(const ATN_Buggy& Target)
{
	const float Kmh = static_cast<float>(TNRally::CmsToKmh(FMath::Abs(Target.GetForwardSpeedCms())));
	MainText->SetText(TNLocText::Int(FMath::RoundToInt(Kmh)));
	BoostBar->SetPercent(FMath::Clamp(Target.GetBoost01(), 0.f, 1.f));
	BoostBar->SetFillColorAndOpacity(Target.IsBoosting() ? TNHUDArt::Gold : TNHUDArt::Foam);
	const UTN_BuggyHealthComponent* Health = Target.GetHealthComponent();
	const float Health01 = Health ? FMath::Clamp(Health->GetHealth01(), 0.f, 1.f) : 1.f;
	HealthBar->SetPercent(Health01);
	HealthBar->SetFillColorAndOpacity(FMath::Lerp(TNHUDArt::CoralC, TNHUDStyle::Accent, Health01));
}

void UTN_RallyDashboardWidget::RefreshRollBar(const ATN_Buggy& Target)
{
	const UWorld* World = GetWorld();
	const ATN_RallyGameState* RallyState = World ? World->GetGameState<ATN_RallyGameState>() : nullptr;
	const FTNRallyStanding* Mine = RallyState ? RallyState->FindStandingForVehicle(&Target) : nullptr;
	if (!Mine || Mine->Place <= 0)
	{
		MainText->SetText(FText::GetEmpty());
		SubText->SetText(FText::GetEmpty());
		return;
	}
	MainText->SetText(TNRallyDashboard::PlaceLine(Mine->Place, RallyState->Standings.Num()));
	SubText->SetText(TNRallyDashboard::LapLine(*Mine, RallyState->Laps, RallyState->NumGates, RallyState->bCircuit));
}

// ── Componente ─────────────────────────────────────────────────────────────────────────────────────────────────

UTN_RallyDashboardComponent::UTN_RallyDashboardComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

UTN_RallyDashboardComponent* UTN_RallyDashboardComponent::FindOn(const ATN_Buggy* Buggy)
{
	return Buggy ? Buggy->FindComponentByClass<UTN_RallyDashboardComponent>() : nullptr;
}

void UTN_RallyDashboardComponent::RemoveFrom(ATN_Buggy* Buggy)
{
	if (UTN_RallyDashboardComponent* Existing = FindOn(Buggy))
	{
		Existing->DestroyComponent();
	}
}

UTN_RallyDashboardComponent* UTN_RallyDashboardComponent::AttachTo(ATN_Buggy* Buggy, APlayerController* Player)
{
	if (!Buggy || !Player || !Player->IsLocalController() || !Player->GetLocalPlayer() || !FApp::CanEverRender())
	{
		return nullptr;
	}
	if (UTN_RallyDashboardComponent* Existing = FindOn(Buggy))
	{
		return Existing;
	}
	UTN_RallyDashboardComponent* Dashboard = NewObject<UTN_RallyDashboardComponent>(Buggy, TEXT("RallyDashboard"), RF_Transient);
	Dashboard->SetupAttachment(Buggy->GetMesh());
	Dashboard->SetRelativeLocation(ATN_Buggy::DriverSeatLocal);
	Dashboard->RegisterComponent();
	Dashboard->Panels.Add(Dashboard->MakePanel(*Buggy, *Player, ETNRallyDashboardPanel::Dash, TNRallyDashboard::DashLayout()));
	Dashboard->Panels.Add(Dashboard->MakePanel(*Buggy, *Player, ETNRallyDashboardPanel::RollBar, TNRallyDashboard::RollBarLayout()));
	UWidgetComponent* CallPanel = Dashboard->MakePanel(*Buggy, *Player, ETNRallyDashboardPanel::Call, TNRallyDashboard::CallLayout());
	Dashboard->Panels.Add(CallPanel);
	Dashboard->CallWidget = CallPanel ? Cast<UTN_RallyDashboardWidget>(CallPanel->GetWidget()) : nullptr;
	UE_LOG(LogTNRally, Log, TEXT("[RallyDashboard] Salpicadero, cartel del arco y placa de notas en %s para %s."), *GetNameSafe(Buggy),
		*GetNameSafe(Player));
	return Dashboard;
}

void UTN_RallyDashboardComponent::ShowCall(const FText& Headline, const FText& Detail, const FLinearColor& Accent, float Seconds)
{
	if (CallWidget)
	{
		CallWidget->ShowCall(Headline, Detail, Accent, Seconds);
	}
}

UWidgetComponent* UTN_RallyDashboardComponent::MakePanel(ATN_Buggy& Buggy, APlayerController& Player, ETNRallyDashboardPanel Kind,
	const TNRallyDashboard::FPanelLayout& Layout)
{
	UTN_RallyDashboardWidget* Widget = CreateWidget<UTN_RallyDashboardWidget>(&Player, UTN_RallyDashboardWidget::StaticClass());
	if (!Widget)
	{
		return nullptr;
	}
	Widget->Configure(&Buggy, Kind);
	UWidgetComponent* Component = NewObject<UWidgetComponent>(&Buggy, NAME_None, RF_Transient);
	Component->SetupAttachment(this);
	Component->SetRelativeLocationAndRotation(Layout.OffsetFromDriverSeat, Layout.Rotation);
	Component->SetRelativeScale3D(FVector(Layout.CmPerPx));
	Component->SetWidgetSpace(EWidgetSpace::World);
	Component->SetDrawSize(FVector2D(Layout.DrawSizePx));
	Component->SetBlendMode(EWidgetBlendMode::Transparent);
	Component->SetTwoSided(false);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(false);
	Component->SetOwnerPlayer(Player.GetLocalPlayer());
	Component->RegisterComponent();
	Component->SetWidget(Widget);
	return Component;
}

void UTN_RallyDashboardComponent::OnUnregister()
{
	for (UWidgetComponent* PanelComponent : Panels)
	{
		if (IsValid(PanelComponent))
		{
			PanelComponent->DestroyComponent();
		}
	}
	Panels.Reset();
	CallWidget = nullptr;
	Super::OnUnregister();
}

#if !UE_BUILD_SHIPPING
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"

namespace TNRallyDashboardDebug
{
	// Comprobación de legibilidad sin editor (#299): captura de pantalla con la interfaz pasado un rato, para mirar el
	// salpicadero y el cartel del arco a la resolución de la ventana (-RenderOffscreen -ResX=1920 -ResY=1080).
	FAutoConsoleCommandWithWorldAndArgs CmdShotLater(TEXT("TN.Rally.ShotLater"),
		TEXT("Rally: TN.Rally.ShotLater <espera> [nombre = RallyShot]: captura de pantalla con la interfaz (Saved/Screenshots)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			const float Wait = Args.IsValidIndex(0) ? FMath::Max(0.1f, FCString::Atof(*Args[0])) : 10.f;
			const FString Name = Args.IsValidIndex(1) ? Args[1] : FString(TEXT("RallyShot"));
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Name](float)
			{
				FScreenshotRequest::RequestScreenshot(Name, true, false);
				UE_LOG(LogTNRally, Display, TEXT("[RallyDashboard] Captura pedida: %s"), *Name);
				return false;
			}), Wait);
		}));
}
#endif
