#include "Rally/TN_RallyHUDWidget.h"

#include "../UI/Race/TN_RaceUIKit.h"
#include "Core/TN_GameModeSpawnUtils.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Rally/TN_RallyHitReport.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyPlayerState.h"
#include "Rally/TN_RallyVehicle.h"
#include "Rally/UI/TN_RallyDashboard.h"
#include "Rally/TN_RallyCameraDirector.h"
#include "Rally/TN_RallyPlayerController.h"
#include "Rally/TN_RallyTrack.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyGunnerPawn.h"
#include "Vehicles/TN_BuggyHealthComponent.h"
#include "Vehicles/TN_BuggyTurretComponent.h"

namespace TNRallyHUD
{
	/** Cuántos segundos se ve el semáforo en verde tras la salida. */
	constexpr double GreenHoldSeconds = 1.5;
	/** Último segundo de tinta: fundido. */
	constexpr float InkFadeSeconds = 1.f;
	constexpr float TextRefreshSeconds = 0.1f;
	/** Paso máximo de la cuenta del aviso de reaparición (s): un tirón no lo enseña de golpe. */
	constexpr double RespawnHintMaxStepSeconds = 0.5;

	/** Personas en la partida (sin los bots): con una sola no hay a quién esperar. */
	int32 CountHumans(const AGameStateBase& State)
	{
		int32 Count = 0;
		for (const APlayerState* PS : State.PlayerArray) { Count += PS && !TN_IsBotPlayerState(PS) ? 1 : 0; }
		return Count;
	}

	const FLinearColor LightOff(0.06f, 0.07f, 0.09f, 0.9f);
	const FLinearColor LightRed(0.95f, 0.16f, 0.12f, 1.f);
	const FLinearColor LightGreen(0.2f, 0.9f, 0.35f, 1.f);
	const FLinearColor InkColor(0.03f, 0.02f, 0.06f, 1.f);
	/** Marca de acierto: roja si cuenta, celeste si la para el escudo. */
	const FLinearColor HitColor(1.f, 0.22f, 0.16f, 1.f);
	const FLinearColor BlockedColor(0.45f, 0.9f, 1.f, 1.f);
	/** La marca nace algo más grande y se encoge hasta su tamaño (fracción extra al aparecer). */
	constexpr float HitMarkerPop = 0.35f;

	FText AmmoName(ETNRallyAmmo Ammo)
	{
		return TNRallyHitLog::AmmoName(Ammo);
	}

	FText CrewName(const FTNRallyStanding& Entry)
	{
		TArray<FText> Names;
		if (Entry.Driver) { Names.Add(TNLocText::PlayerName(Entry.Driver->GetPlayerName())); }
		if (Entry.Gunner) { Names.Add(TNLocText::PlayerName(Entry.Gunner->GetPlayerName())); }
		return Names.Num() > 0 ? TNLocText::JoinList(Names) : NSLOCTEXT("Rally", "EmptyBuggy", "Buggy vacío");
	}

	int32 CeilSeconds(double Seconds)
	{
		return FMath::Max(0, FMath::CeilToInt(static_cast<float>(Seconds)));
	}

	void Show(UWidget* Widget, bool bVisible)
	{
		if (Widget)
		{
			Widget->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
}

UTN_RallyHUDWidget::UTN_RallyHUDWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<USoundBase> BeepFinder(TEXT("/Game/Audio/Rally/SFX_Rally_Light_Beep.SFX_Rally_Light_Beep"));
	static ConstructorHelpers::FObjectFinder<USoundBase> GoFinder(TEXT("/Game/Audio/Rally/SFX_Rally_Light_Go.SFX_Rally_Light_Go"));
	LightBeepSound = BeepFinder.Object;
	LightGoSound = GoFinder.Object;
}

void UTN_RallyHUDWidget::NativeOnInitialized()
{
	BuildTree();
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UTN_RallyHUDWidget::SetTurretHeat(float Heat01)
{
	TurretHeat = FMath::Clamp(Heat01, 0.f, 1.f);
	RefreshTurret();
}

void UTN_RallyHUDWidget::SetInkSeconds(float SecondsLeft)
{
	InkSeconds = FMath::Max(0.f, SecondsLeft);
}

const ATN_Buggy* UTN_RallyHUDWidget::FindLocalBuggy() const
{
	const APlayerController* Player = GetOwningPlayer();
	if (!Player)
	{
		return nullptr;
	}
	const APawn* Pawn = Player->GetPawn();
	if (const ATN_Buggy* Driven = Cast<ATN_Buggy>(Pawn))
	{
		return Driven;
	}
	if (const ATN_BuggyGunnerPawn* Gunner = Cast<ATN_BuggyGunnerPawn>(Pawn))
	{
		return Gunner->GetBuggy();
	}
	// Sin peón propio (reaparición, cambio de plaza): el buggy de su fila de puestos.
	const UWorld* World = GetWorld();
	const ATN_RallyGameState* RallyState = World ? World->GetGameState<ATN_RallyGameState>() : nullptr;
	const FTNRallyStanding* Mine = RallyState ? RallyState->FindStandingForPlayer(Player->GetPlayerState<ATN_RallyPlayerState>()) : nullptr;
	return Mine ? Cast<ATN_Buggy>(Mine->Vehicle) : nullptr;
}

void UTN_RallyHUDWidget::PullFromLocalBuggy()
{
	const ATN_Buggy* Buggy = FindLocalBuggy();
	const UTN_BuggyTurretComponent* Turret = Buggy ? Buggy->GetTurret() : nullptr;
	bTurretOverheated = Turret && Turret->IsOverheated();
	SpecialCharges = Turret ? Turret->GetSpecialCharges() : 0;
	SpecialAmmo = Turret ? Turret->GetSpecialAmmo() : ETNRallyAmmo::None;
	SelectedAmmo = Turret ? Turret->GetSelectedAmmo() : ETNRallyAmmo::Coco;
	GunnerKnockSeconds = Turret ? Turret->GetGunnerKnockSecondsLeft() : 0.f;
	const UTN_BuggyHealthComponent* Health = Buggy ? Buggy->GetHealthComponent() : nullptr;
	Health01 = Health ? Health->GetHealth01() : 1.f;
	SetTurretHeat(Turret ? Turret->GetHeat01() : 0.f);
	SetInkSeconds(Buggy ? Buggy->GetInkSecondsLeft() : 0.f);
	BoostCharge = Buggy ? Buggy->GetBoost01() : 0.f;
	bBoosting = Buggy && Buggy->IsBoosting();
}

void UTN_RallyHUDWidget::BuildTree()
{
	using namespace TNRaceUI;
	if (!WidgetTree || Canvas)
	{
		return;
	}
	UWidgetTree* Tree = WidgetTree;
	Canvas = Make<UCanvasPanel>(Tree, TEXT("RallyCanvas"));
	Tree->RootWidget = Canvas;

	// La tinta va la primera: tapa la carretera pero no los números del HUD.
	BuildInk();

	PlaceText = MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 56, TNHUDArt::Gold);
	Place(Canvas, PlaceText, FVector2D(0.f, 0.f), FVector2D(40.f, 24.f));
	LapText = MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 26, FLinearColor::White);
	Place(Canvas, LapText, FVector2D(0.f, 0.f), FVector2D(44.f, 100.f));

	SpeedText = MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 64, FLinearColor::White);
	Place(Canvas, SpeedText, FVector2D(1.f, 1.f), FVector2D(-48.f, -64.f));
	SpeedUnitText = MakeText(Tree, NSLOCTEXT("Rally", "SpeedUnit", "km/h"), TEXT("Regular"), 24, TNHUDStyle::TextDim);
	Place(Canvas, SpeedUnitText, FVector2D(1.f, 1.f), FVector2D(-48.f, -30.f));
	BoostLabel = MakeText(Tree, NSLOCTEXT("Rally", "Boost", "Turbo"), TEXT("Regular"), 22, TNHUDStyle::TextDim);
	Place(Canvas, BoostLabel, FVector2D(1.f, 1.f), FVector2D(-48.f, -184.f));
	BoostBar = Make<UProgressBar>(Tree);
	BoostBar->SetWidgetStyle(TNHUDStyle::Bar(9.f));
	BoostBar->SetPercent(0.f);
	Place(Canvas, MakeSize(Tree, BoostBar, 260.f, 18.f), FVector2D(1.f, 1.f), FVector2D(-48.f, -158.f));

	BuildSemaphore();

	CenterText = MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 110, FLinearColor::White);
	Place(Canvas, CenterText, FVector2D(0.5f, 0.3f), FVector2D::ZeroVector);
	StatusText = MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 30, TNHUDArt::SandLight);
	Place(Canvas, StatusText, FVector2D(0.5f, 0.f), FVector2D(0.f, 130.f));
	WrongWayText = MakeText(Tree, NSLOCTEXT("Rally", "WrongWay", "¡CONTRAMANO!"), TEXT("Bold"), 64, TNHUDArt::CoralC);
	Place(Canvas, WrongWayText, FVector2D(0.5f, 0.42f), FVector2D::ZeroVector);
	RespawnText = MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 36, TNHUDArt::SandC);
	// Por encima y por debajo del buggy: en el centro están el salpicadero y el cartel del arco (#299).
	Place(Canvas, RespawnText, FVector2D(0.5f, 0.36f), FVector2D::ZeroVector);
	RespawnHintText = MakeText(Tree, NSLOCTEXT("Rally", "RespawnHint", "Mantén R para volver a la pista"), TEXT("Bold"), 30,
		TNHUDArt::SandLight);
	Place(Canvas, RespawnHintText, FVector2D(0.5f, 0.8f), FVector2D::ZeroVector);
	SpectateText = MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 34, FLinearColor::White);
	Place(Canvas, SpectateText, FVector2D(0.5f, 1.f), FVector2D(0.f, -92.f));
	SpectateHintText = MakeText(Tree, NSLOCTEXT("Rally", "SpectateHint", "A / D · LB / RB: cambiar de vista"), TEXT("Regular"), 24,
		TNHUDStyle::TextDim);
	Place(Canvas, SpectateHintText, FVector2D(0.5f, 1.f), FVector2D(0.f, -54.f));
	Crosshair = MakeText(Tree, TNLocText::Literal(TEXT("+")), TEXT("Bold"), 48, FLinearColor::White);
	Place(Canvas, Crosshair, FVector2D(0.5f, 0.5f), FVector2D::ZeroVector);
	// «×» (U+00D7) sobre el «+» del punto de mira.
	HitMarker = MakeText(Tree, TNLocText::Literal(TEXT("×")), TEXT("Bold"), 64, TNRallyHUD::HitColor);
	Place(Canvas, HitMarker, FVector2D(0.5f, 0.5f), FVector2D::ZeroVector);

	BuildTurretPanel();

	BuildResults();

	for (UWidget* Hidden : TArray<UWidget*>{ SpectateText, SpectateHintText, WrongWayText, RespawnText, RespawnHintText, Crosshair, HitMarker, AmmoText, CenterText, StatusText,
		ResultsPanel, BoostLabel, BoostBar ? BoostBar->GetParent() : nullptr, HealthLabel, HealthBar ? HealthBar->GetParent() : nullptr,
		KnockText })
	{
		TNRallyHUD::Show(Hidden, false);
	}
	RefreshTurret();
}

void UTN_RallyHUDWidget::BuildSemaphore()
{
	using namespace TNRaceUI;
	UWidgetTree* Tree = WidgetTree;
	UHorizontalBox* Lights = Make<UHorizontalBox>(Tree);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UImage* Light = Make<UImage>(Tree);
		Light->SetBrush(TNHUDStyle::Rounded(FLinearColor::White, 32.f, FLinearColor(0.f, 0.f, 0.f, 0.6f), 3.f));
		Light->SetColorAndOpacity(TNRallyHUD::LightOff);
		if (UHorizontalBoxSlot* LightSlot = Lights->AddChildToHorizontalBox(MakeSize(Tree, Light, 64.f, 64.f)))
		{
			LightSlot->SetPadding(FMargin(8.f, 0.f));
		}
		SemaphoreLights.Add(Light);
	}
	UBorder* LightsPanel = Make<UBorder>(Tree);
	TNHUDStyle::StylePanel(LightsPanel, TNHUDStyle::Panel, 24.f, FMargin(14.f, 10.f));
	LightsPanel->SetContent(Lights);
	SemaphoreBox = LightsPanel;
	Place(Canvas, LightsPanel, FVector2D(0.5f, 0.f), FVector2D(0.f, 36.f));
}

void UTN_RallyHUDWidget::BuildTurretPanel()
{
	using namespace TNRaceUI;
	UWidgetTree* Tree = WidgetTree;
	AmmoText = MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 28, TNHUDArt::Foam);
	Place(Canvas, AmmoText, FVector2D(0.f, 1.f), FVector2D(40.f, -84.f));
	HeatLabel = MakeText(Tree, NSLOCTEXT("Rally", "TurretHeat", "Torreta"), TEXT("Regular"), 22, TNHUDStyle::TextDim);
	Place(Canvas, HeatLabel, FVector2D(0.f, 1.f), FVector2D(40.f, -52.f));
	HeatBar = Make<UProgressBar>(Tree);
	HeatBar->SetWidgetStyle(TNHUDStyle::Bar(9.f));
	HeatBar->SetPercent(0.f);
	Place(Canvas, MakeSize(Tree, HeatBar, 260.f, 18.f), FVector2D(0.f, 1.f), FVector2D(40.f, -28.f));
	// Vida del buggy: pequeña, por encima de la munición, para no competir con la velocidad ni con el turbo.
	HealthLabel = MakeText(Tree, NSLOCTEXT("Rally", "BuggyHealth", "Buggy"), TEXT("Regular"), 18, TNHUDStyle::TextDim);
	Place(Canvas, HealthLabel, FVector2D(0.f, 1.f), FVector2D(40.f, -150.f));
	HealthBar = Make<UProgressBar>(Tree);
	HealthBar->SetWidgetStyle(TNHUDStyle::Bar(5.f));
	HealthBar->SetPercent(1.f);
	Place(Canvas, MakeSize(Tree, HealthBar, 160.f, 10.f), FVector2D(0.f, 1.f), FVector2D(40.f, -134.f));
	KnockText = MakeText(Tree, FText::GetEmpty(), TEXT("Bold"), 44, TNHUDArt::CoralC);
	Place(Canvas, KnockText, FVector2D(0.5f, 0.72f), FVector2D::ZeroVector);
}

void UTN_RallyHUDWidget::BuildInk()
{
	// Manchas de tinta (posición en fracción de pantalla y tamaño en px): se ven mientras el buggy tiene tinta.
	struct FSplat { FVector2D Anchor; float Size; };
	const FSplat Splats[] = {
		{ FVector2D(0.3f, 0.35f), 420.f }, { FVector2D(0.62f, 0.3f), 360.f }, { FVector2D(0.5f, 0.6f), 480.f },
		{ FVector2D(0.18f, 0.7f), 300.f }, { FVector2D(0.8f, 0.62f), 340.f }, { FVector2D(0.45f, 0.18f), 260.f } };
	for (const FSplat& Splat : Splats)
	{
		UImage* Ink = TNRaceUI::Make<UImage>(WidgetTree);
		Ink->SetBrush(TNHUDStyle::Rounded(TNRallyHUD::InkColor, Splat.Size * 0.5f));
		Ink->SetRenderOpacity(0.f);
		UWidget* Sized = TNRaceUI::MakeSize(WidgetTree, Ink, Splat.Size, Splat.Size * 0.85f);
		TNRaceUI::Place(Canvas, Sized, Splat.Anchor, FVector2D::ZeroVector);
		InkSplats.Add(Ink);
	}
}

void UTN_RallyHUDWidget::BuildResults()
{
	using namespace TNRaceUI;
	UWidgetTree* Tree = WidgetTree;
	UVerticalBox* Content = Make<UVerticalBox>(Tree);
	UTextBlock* Title = MakeText(Tree, NSLOCTEXT("Rally", "ResultsTitle", "Resultados"), TEXT("Bold"), 44, TNHUDArt::Gold);
	if (UVerticalBoxSlot* TitleSlot = Content->AddChildToVerticalBox(Title))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));
	}
	ResultsRows = Make<UVerticalBox>(Tree);
	Content->AddChildToVerticalBox(ResultsRows);
	ResultsFooter = MakeText(Tree, FText::GetEmpty(), TEXT("Regular"), 24, TNHUDStyle::TextDim);
	if (UVerticalBoxSlot* FooterSlot = Content->AddChildToVerticalBox(ResultsFooter))
	{
		FooterSlot->SetHorizontalAlignment(HAlign_Center);
		FooterSlot->SetPadding(FMargin(0.f, 16.f, 0.f, 0.f));
	}
	ResultsPanel = Make<UBorder>(Tree);
	TNHUDStyle::StylePanel(ResultsPanel, TNHUDStyle::Panel, 22.f, FMargin(40.f, 28.f));
	ResultsPanel->SetContent(Content);
	Place(Canvas, ResultsPanel, FVector2D(0.5f, 0.5f), FVector2D::ZeroVector);
}

void UTN_RallyHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	PullFromLocalBuggy();
	TickHitMarker(InDeltaTime);
	const float InkOpacity = FMath::Clamp(InkSeconds / TNRallyHUD::InkFadeSeconds, 0.f, 1.f) * 0.94f;
	for (UImage* Ink : InkSplats)
	{
		Ink->SetRenderOpacity(InkOpacity);
	}

	const UWorld* World = GetWorld();
	const ATN_RallyGameState* RallyState = World ? World->GetGameState<ATN_RallyGameState>() : nullptr;
	if (!RallyState)
	{
		return;
	}
	RefreshSemaphore(*RallyState, RallyState->GetServerWorldTimeSeconds());
	TextAccumulator += InDeltaTime;
	if (TextAccumulator >= TNRallyHUD::TextRefreshSeconds)
	{
		TextAccumulator = 0.f;
		Refresh(*RallyState);
	}
}

void UTN_RallyHUDWidget::ShowHitMarker(bool bBlocked)
{
	if (!HitMarker || !Crosshair || !Crosshair->IsVisible())
	{
		return;
	}
	HitMarkerSeconds = TNRallyHitLog::MarkerSeconds;
	HitMarker->SetColorAndOpacity(FSlateColor(bBlocked ? TNRallyHUD::BlockedColor : TNRallyHUD::HitColor));
	TNRallyHUD::Show(HitMarker, true);
	TickHitMarker(0.f);
}

void UTN_RallyHUDWidget::TickHitMarker(float DeltaTime)
{
	if (!HitMarker || HitMarkerSeconds <= 0.f)
	{
		return;
	}
	HitMarkerSeconds = FMath::Max(0.f, HitMarkerSeconds - DeltaTime);
	const float Alpha = HitMarkerSeconds / TNRallyHitLog::MarkerSeconds;
	HitMarker->SetRenderScale(FVector2D(1.f + TNRallyHUD::HitMarkerPop * Alpha));
	HitMarker->SetRenderOpacity(FMath::Clamp(Alpha * 2.f, 0.f, 1.f));
	if (HitMarkerSeconds <= 0.f)
	{
		TNRallyHUD::Show(HitMarker, false);
	}
}

void UTN_RallyHUDWidget::Refresh(const ATN_RallyGameState& RallyState)
{
	using namespace TNRallyHUD;
	const double ServerTime = RallyState.GetServerWorldTimeSeconds();
	const APlayerController* Player = GetOwningPlayer();
	const ATN_RallyPlayerState* Me = Player ? Player->GetPlayerState<ATN_RallyPlayerState>() : nullptr;
	const FTNRallyStanding* Mine = RallyState.FindStandingForPlayer(Me);
	const ITN_RallyVehicle* Vehicle = Mine ? Cast<ITN_RallyVehicle>(Mine->Vehicle) : nullptr;
	const bool bRacing = RallyState.Phase == ETNRallyPhase::Racing || RallyState.Phase == ETNRallyPhase::Finishing;

	// La conductora no tiene interfaz de pantalla salvo los avisos (semáforo, contramano, reaparición, meta y resultados):
	// velocidad, turbo y vida van en el salpicadero, y puesto y vuelta en el cartel del arco (UTN_RallyDashboardComponent);
	// el mapa, las notas y su munición si va sola, en la tableta compacta. La artillera conserva su HUD.
	const bool bSeatedView = Vehicle != nullptr && RallyState.Phase != ETNRallyPhase::Results && !Mine->bFinished;
	const bool bGunner = Me && Me->IsGunner();
	const bool bGunnerView = bSeatedView && bGunner;
	RefreshPlace(RallyState, Mine, bGunnerView);

	Show(SpeedText, bGunnerView);
	Show(SpeedUnitText, bGunnerView);
	RefreshBoost(bGunnerView);
	if (bGunnerView)
	{
		SpeedText->SetText(TNLocText::Int(FMath::RoundToInt(static_cast<float>(TNRally::CmsToKmh(FMath::Abs(Vehicle->GetForwardSpeedCms()))))));
	}
	const bool bShowWeapon = bGunnerView;
	RefreshAmmo(bShowWeapon);
	RefreshHealth(bGunnerView);
	RefreshKnock(bSeatedView && bGunner);
	HeatLabel->SetText(bTurretOverheated ? NSLOCTEXT("Rally", "TurretOverheated", "¡Torreta sobrecalentada!")
		: NSLOCTEXT("Rally", "TurretHeat", "Torreta"));
	Show(Crosshair, bGunnerView);
	Show(HeatBar ? HeatBar->GetParent() : nullptr, bShowWeapon);
	Show(HeatLabel, bShowWeapon);

	Show(WrongWayText, bRacing && Mine && Mine->bWrongWay && !Mine->bFinished);
	const double RespawnLeft = Mine ? Mine->RespawnEndServerTime - ServerTime : 0.0;
	Show(RespawnText, Mine && Mine->RespawnEndServerTime > 0.f && RespawnLeft > 0.0);
	if (RespawnLeft > 0.0)
	{
		RespawnText->SetText(FText::Format(NSLOCTEXT("Rally", "Respawning", "Reapareciendo… {0}"), TNLocText::Int(CeilSeconds(RespawnLeft))));
	}

	RefreshRespawnHint(RallyState, Mine, ServerTime, bRacing);
	RefreshSpectate(RallyState);
	RefreshStatus(RallyState, ServerTime, Mine);
	RefreshResults(RallyState, ServerTime);
}

void UTN_RallyHUDWidget::RefreshSemaphore(const ATN_RallyGameState& RallyState, double ServerTime)
{
	using namespace TNRallyHUD;
	const double ToStart = RallyState.StartServerTime - ServerTime;
	const bool bCounting = RallyState.Phase == ETNRallyPhase::Countdown && ToStart > 0.0;
	const bool bGreen = (RallyState.Phase == ETNRallyPhase::Countdown && ToStart <= 0.0)
		|| (RallyState.Phase == ETNRallyPhase::Racing && -ToStart < GreenHoldSeconds);
	Show(SemaphoreBox, bCounting || bGreen);
	Show(CenterText, bCounting || bGreen);
	if (bCounting)
	{
		// Una luz roja más por segundo: 3 s → una, 2 s → dos, 1 s → tres.
		const int32 Lit = FMath::Clamp(3 - FMath::FloorToInt(static_cast<float>(ToStart)), 1, 3);
		if (Lit > SemaphoreStepHeard)
		{
			SemaphoreStepHeard = Lit;
			if (LightBeepSound)
			{
				UGameplayStatics::PlaySound2D(this, LightBeepSound);
			}
		}
		for (int32 Index = 0; Index < SemaphoreLights.Num(); ++Index)
		{
			SemaphoreLights[Index]->SetColorAndOpacity(Index < Lit ? LightRed : LightOff);
		}
		CenterText->SetText(TNLocText::Int(CeilSeconds(ToStart)));
		CenterText->SetColorAndOpacity(FSlateColor(LightRed));
	}
	else if (bGreen)
	{
		for (UImage* Light : SemaphoreLights)
		{
			Light->SetColorAndOpacity(LightGreen);
		}
		CenterText->SetText(NSLOCTEXT("Rally", "Go", "¡YA!"));
		CenterText->SetColorAndOpacity(FSlateColor(LightGreen));
		// Solo si se ha oído la cuenta: quien entra con la carrera ya en verde no oye la salida.
		if (SemaphoreStepHeard > 0 && SemaphoreStepHeard < 4)
		{
			SemaphoreStepHeard = 4;
			if (LightGoSound)
			{
				UGameplayStatics::PlaySound2D(this, LightGoSound);
			}
		}
	}
	else
	{
		SemaphoreStepHeard = 0;
	}
}

void UTN_RallyHUDWidget::RefreshStatus(const ATN_RallyGameState& RallyState, double ServerTime, const FTNRallyStanding* Mine)
{
	using namespace TNRallyHUD;
	FText Status;
	const double PhaseLeft = RallyState.PhaseEndServerTime - ServerTime;
	switch (RallyState.Phase)
	{
	case ETNRallyPhase::Warmup:
		// Sin cuenta atrás (#289): el semáforo es el único temporizador de la salida.
		// Solo si hay a quién esperar: sola (con bots o sin ellos), nada (#755).
		Status = RallyState.IsWaitingForPlayers() && TNRallyHUD::CountHumans(RallyState) > 1
			? NSLOCTEXT("Rally", "WaitingForOthers", "Esperando a los demás…") : FText::GetEmpty();
		break;
	case ETNRallyPhase::Racing:
		Status = Mine ? FText::GetEmpty() : NSLOCTEXT("Rally", "Spectating", "Mirando la carrera");
		break;
	case ETNRallyPhase::Finishing:
		Status = FText::Format(NSLOCTEXT("Rally", "FinishCountdown", "Fin de la carrera en {0} s"), TNLocText::Int(CeilSeconds(PhaseLeft)));
		break;
	default:
		break;
	}
	Show(StatusText, !Status.IsEmpty());
	if (!Status.IsEmpty())
	{
		StatusText->SetText(Status);
		StatusText->SetColorAndOpacity(FSlateColor(RallyState.Phase == ETNRallyPhase::Finishing && PhaseLeft < 5.0 ? TNHUDArt::CoralC : TNHUDArt::SandLight));
	}
}

void UTN_RallyHUDWidget::RefreshResults(const ATN_RallyGameState& RallyState, double ServerTime)
{
	using namespace TNRallyHUD;
	const bool bResults = RallyState.Phase == ETNRallyPhase::Results;
	Show(ResultsPanel, bResults);
	if (!bResults)
	{
		ShownResultsHash = 0;
		return;
	}
	ResultsFooter->SetText(FText::Format(RallyState.ReturnsToLobbyAfterResults() ? NSLOCTEXT("Rally", "BackToLobby", "Vuelta al lobby en {0} s")
		: NSLOCTEXT("Rally", "NextRace", "Carrera nueva en {0} s"), TNLocText::Int(CeilSeconds(RallyState.PhaseEndServerTime - ServerTime))));

	uint32 Hash = 1;
	for (const FTNRallyStanding& Entry : RallyState.Standings)
	{
		Hash = HashCombine(Hash, GetTypeHash(Entry.TeamIndex));
		Hash = HashCombine(Hash, GetTypeHash(Entry.Place));
		Hash = HashCombine(Hash, GetTypeHash(Entry.FinishSeconds));
		Hash = HashCombine(Hash, GetTypeHash(Entry.Driver.Get()));
		Hash = HashCombine(Hash, GetTypeHash(Entry.Gunner.Get()));
	}
	if (Hash == ShownResultsHash)
	{
		return;
	}
	ShownResultsHash = Hash;
	ResultsRows->ClearChildren();
	const APlayerController* Player = GetOwningPlayer();
	const APlayerState* Me = Player ? Player->PlayerState : nullptr;
	for (const FTNRallyStanding& Entry : RallyState.Standings)
	{
		const FText Time = Entry.bFinished ? TNRallyDashboard::RaceTime(Entry.FinishSeconds) : NSLOCTEXT("Rally", "NotFinished", "Sin llegar");
		const FText Line = FText::Format(NSLOCTEXT("Rally", "ResultsRow", "{0}.º   {1}   {2}   {3} pts"),
			TNLocText::Int(Entry.Place), CrewName(Entry), Time, TNLocText::Int(Entry.Points));
		const bool bMine = Me && (Entry.Driver == Me || Entry.Gunner == Me);
		UTextBlock* Row = TNRaceUI::MakeText(WidgetTree, Line, TEXT("Bold"), 30, bMine ? TNHUDArt::Gold : FLinearColor::White);
		if (UVerticalBoxSlot* RowSlot = ResultsRows->AddChildToVerticalBox(Row))
		{
			RowSlot->SetPadding(FMargin(0.f, 4.f));
		}
	}
}

void UTN_RallyHUDWidget::RefreshBoost(bool bVisible)
{
	TNRallyHUD::Show(BoostLabel, bVisible);
	TNRallyHUD::Show(BoostBar ? BoostBar->GetParent() : nullptr, bVisible);
	if (!bVisible || !BoostBar)
	{
		return;
	}
	BoostBar->SetPercent(FMath::Clamp(BoostCharge, 0.f, 1.f));
	BoostBar->SetFillColorAndOpacity(bBoosting ? TNHUDArt::Gold : TNHUDArt::Foam);
	BoostLabel->SetColorAndOpacity(FSlateColor(bBoosting ? TNHUDArt::Gold : TNHUDStyle::TextDim));
}

void UTN_RallyHUDWidget::RefreshSpectate(const ATN_RallyGameState& RallyState)
{
	using namespace TNRallyHUD;
	const ATN_RallyPlayerController* Player = Cast<ATN_RallyPlayerController>(GetOwningPlayer());
	const UTN_RallyCameraDirector* Director = Player ? Player->GetCameraDirector() : nullptr;
	const bool bSpectating = Director && Director->IsSpectating();
	Show(SpectateText, bSpectating);
	Show(SpectateHintText, bSpectating);
	if (!bSpectating)
	{
		return;
	}
	const int32 Team = Director->GetSpectatedTeam();
	const FTNRallyStanding* Watched = RallyState.Standings.FindByPredicate([Team](const FTNRallyStanding& Entry) { return Entry.TeamIndex == Team; });
	SpectateText->SetText(Director->IsDroneView() || !Watched
		? NSLOCTEXT("Rally", "SpectateDrone", "Dron: siguiendo al líder")
		: FText::Format(NSLOCTEXT("Rally", "SpectateCrew", "Mirando a {0} ({1}.º)"), CrewName(*Watched), TNLocText::Int(Watched->Place)));
}

void UTN_RallyHUDWidget::RefreshRespawnHint(const ATN_RallyGameState& RallyState, const FTNRallyStanding* Mine, double ServerTime,
	bool bRacing)
{
	const APawn* Vehicle = Mine ? Mine->Vehicle.Get() : nullptr;
	const ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Vehicle);
	const double Step = RespawnHintCheckServerTime < 0.0 ? 0.0
		: FMath::Clamp(ServerTime - RespawnHintCheckServerTime, 0.0, TNRallyHUD::RespawnHintMaxStepSeconds);
	RespawnHintCheckServerTime = ServerTime;

	TNRallyRespawnHint::FInput Input;
	if (Mine && RallyVehicle)
	{
		Input.bRacing = bRacing && !Mine->bFinished && !Mine->bRetired;
		Input.bRespawning = Mine->RespawnEndServerTime > 0.f && ServerTime < Mine->RespawnEndServerTime;
		Input.SecondsSinceRespawn = Mine->LastRespawnServerTime > 0.f
			? FMath::Max(0.f, static_cast<float>(ServerTime - Mine->LastRespawnServerTime)) : -1.f;
		Input.bFlipped = RallyVehicle->IsFlipped();
		Input.SpeedCms = static_cast<float>(Vehicle->GetVelocity().Size());
		// La reaparición teletransporta: el arco cercano se vuelve a buscar en toda la pista.
		if (Input.bRespawning)
		{
			RespawnHintArc = -1.0;
		}
		Input.DistanceToAxisCm = DistanceToTrackAxis(RallyState, Vehicle->GetActorLocation());
	}
	TNRallyHUD::Show(RespawnHintText, TNRallyRespawnHint::Update(RespawnHintState, Input, static_cast<float>(Step)));
}

float UTN_RallyHUDWidget::DistanceToTrackAxis(const ATN_RallyGameState& RallyState, const FVector& Location)
{
	const ATN_RallyTrack* Track = RallyState.GetTrack();
	if (!Track || Track->GetTrackLengthCm() <= 0.f)
	{
		return -1.f;
	}
	RespawnHintArc = RespawnHintArc < 0.0 ? Track->FindArcGlobal(Location) : Track->FindArcNear(Location, RespawnHintArc);
	return static_cast<float>(FVector::Dist(Location, Track->GetLocationAtArc(RespawnHintArc)));
}

void UTN_RallyHUDWidget::RefreshTurret()
{
	if (!HeatBar)
	{
		return;
	}
	HeatBar->SetPercent(TurretHeat);
	HeatBar->SetFillColorAndOpacity(FMath::Lerp(TNHUDStyle::Accent, TNHUDArt::CoralC, TurretHeat));
}

void UTN_RallyHUDWidget::RefreshAmmo(bool bVisible)
{
	using namespace TNRallyHUD;
	Show(AmmoText, bVisible);
	if (!bVisible)
	{
		return;
	}
	const bool bHasSpecial = SpecialAmmo != ETNRallyAmmo::None && SpecialAmmo != ETNRallyAmmo::Coco && SpecialCharges > 0;
	const bool bSpecialSelected = bHasSpecial && SelectedAmmo == SpecialAmmo;
	FText Line;
	if (bSpecialSelected)
	{
		Line = FText::Format(NSLOCTEXT("Rally", "SpecialAmmoCharges", "Munición: {0} ×{1}"), AmmoName(SpecialAmmo),
			TNLocText::Int(SpecialCharges));
	}
	else if (bHasSpecial)
	{
		// Coco seleccionado y una especial en reserva: se recuerda que la rueda o la cruceta la eligen.
		Line = FText::Format(NSLOCTEXT("Rally", "SelectedAmmoReserve", "Munición: {0} · {1} ×{2}"), AmmoName(ETNRallyAmmo::Coco),
			AmmoName(SpecialAmmo), TNLocText::Int(SpecialCharges));
	}
	else
	{
		Line = FText::Format(NSLOCTEXT("Rally", "SelectedAmmo", "Munición: {0}"), AmmoName(ETNRallyAmmo::Coco));
	}
	AmmoText->SetText(Line);
	AmmoText->SetColorAndOpacity(FSlateColor(bSpecialSelected ? TNHUDArt::Gold : TNHUDArt::Foam));
}

void UTN_RallyHUDWidget::RefreshHealth(bool bVisible)
{
	TNRallyHUD::Show(HealthLabel, bVisible);
	TNRallyHUD::Show(HealthBar ? HealthBar->GetParent() : nullptr, bVisible);
	if (!bVisible || !HealthBar)
	{
		return;
	}
	const float Health = FMath::Clamp(Health01, 0.f, 1.f);
	HealthBar->SetPercent(Health);
	HealthBar->SetFillColorAndOpacity(FMath::Lerp(TNHUDArt::CoralC, TNHUDStyle::Accent, Health));
}

void UTN_RallyHUDWidget::RefreshKnock(bool bGunner)
{
	const bool bKnocked = bGunner && GunnerKnockSeconds > 0.f;
	TNRallyHUD::Show(KnockText, bKnocked);
	if (bKnocked)
	{
		KnockText->SetText(FText::Format(NSLOCTEXT("Rally", "GunnerKnocked", "¡Noqueada! {0}"),
			TNLocText::Int(TNRallyHUD::CeilSeconds(GunnerKnockSeconds))));
	}
}

void UTN_RallyHUDWidget::RefreshPlace(const ATN_RallyGameState& RallyState, const FTNRallyStanding* Mine, bool bVisible)
{
	using namespace TNRallyHUD;
	Show(PlaceText, bVisible && Mine != nullptr);
	Show(LapText, bVisible && Mine != nullptr);
	if (!bVisible || !Mine)
	{
		return;
	}
	PlaceText->SetText(TNRallyDashboard::PlaceLine(Mine->Place, RallyState.Standings.Num()));
	LapText->SetText(TNRallyDashboard::LapLine(*Mine, RallyState.Laps, RallyState.NumGates, RallyState.bCircuit));
}
