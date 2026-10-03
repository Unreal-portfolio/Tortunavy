#include "Rally/UI/TN_RallyCopilotTablet.h"

#include "Components/InputComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "InputCoreTypes.h"
#include "Rally/TN_RallyGameState.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyTrack.h"
#include "UObject/ObjectKey.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyGunnerPawn.h"
#include "Vehicles/TN_BuggyMath.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "VR/TN_VRMode.h"

namespace TNRallyTabletState
{
	/** Por encima del HUD del Rally y por debajo de los menús. */
	constexpr int32 ViewportZOrder = 20;
	/** Si el buggy queda a más de esto del arco que se seguía (reaparición), se busca en toda la pista (cm). */
	constexpr double ArcLostDistanceCm = 4000.0;

	/** Tableta de cada jugador local. Débil: la mantiene viva el viewport, no este registro. */
	TMap<TObjectKey<APlayerController>, TWeakObjectPtr<UTN_RallyCopilotTablet>>& Registry()
	{
		static TMap<TObjectKey<APlayerController>, TWeakObjectPtr<UTN_RallyCopilotTablet>> Map;
		return Map;
	}

	void PruneRegistry()
	{
		for (auto It = Registry().CreateIterator(); It; ++It)
		{
			if (!It.Value().IsValid() || !It.Key().ResolveObjectPtr()) { It.RemoveCurrent(); }
		}
	}
}

// ── Acceso por jugador local ───────────────────────────────────────────────────────────────────────────────────

UTN_RallyCopilotTablet* UTN_RallyCopilotTablet::FindFor(const APlayerController* Player)
{
	if (!Player)
	{
		return nullptr;
	}
	const TWeakObjectPtr<UTN_RallyCopilotTablet>* Found = TNRallyTabletState::Registry().Find(TObjectKey<APlayerController>(Player));
	return Found ? Found->Get() : nullptr;
}

UTN_RallyCopilotTablet* UTN_RallyCopilotTablet::FindOrCreateFor(APlayerController* Player)
{
	if (!Player || !Player->IsLocalController())
	{
		return nullptr;
	}
	if (UTN_RallyCopilotTablet* Existing = FindFor(Player))
	{
		return Existing;
	}
	UTN_RallyCopilotTablet* Tablet = CreateFor(Player);
	if (Tablet)
	{
		Tablet->ShowOnScreen();
	}
	return Tablet;
}

UTN_RallyCopilotTablet* UTN_RallyCopilotTablet::CreateFor(APlayerController* Player)
{
	UTN_RallyCopilotTablet* Tablet = CreateWidget<UTN_RallyCopilotTablet>(Player, UTN_RallyCopilotTablet::StaticClass());
	if (!Tablet)
	{
		return nullptr;
	}
	TNRallyTabletState::PruneRegistry();
	TNRallyTabletState::Registry().Add(TObjectKey<APlayerController>(Player), Tablet);
	return Tablet;
}

UTN_RallyCopilotTablet* UTN_RallyCopilotTablet::PresentInWorldFor(APlayerController* Player, UWidgetComponent* Host)
{
	if (!Player || !Player->IsLocalController() || !IsValid(Host))
	{
		return nullptr;
	}
	UTN_RallyCopilotTablet* Tablet = FindFor(Player);
	Tablet = Tablet ? Tablet : CreateFor(Player);
	if (Tablet)
	{
		Tablet->ShowInWorld(*Host);
	}
	return Tablet;
}

UTN_RallyCopilotTablet* UTN_RallyCopilotTablet::PresentOnScreenFor(APlayerController* Player)
{
	UTN_RallyCopilotTablet* Tablet = FindFor(Player);
	if (!Tablet)
	{
		return FindOrCreateFor(Player);
	}
	Tablet->ShowOnScreen();
	return Tablet;
}

bool UTN_RallyCopilotTablet::IsPresented() const
{
	if (Presentation == ETNRallyTabletPresentation::Screen)
	{
		return TNVR::IsOnScreen(this);
	}
	// Con el panel asignado aunque aún no haya construido su widget de Slate; deja de estarlo cuando se destruye.
	return WorldHost.IsValid();
}

void UTN_RallyCopilotTablet::ShowOnScreen()
{
	if (UWidgetComponent* Host = WorldHost.Get(); Host && Host->GetWidget() == this)
	{
		Host->SetWidget(nullptr);
	}
	WorldHost.Reset();
	Presentation = ETNRallyTabletPresentation::Screen;
	if (!TNVR::IsOnScreen(this))
	{
		// En VR va al panel del mundo, como el resto de la interfaz.
		TNVR::AddToScreen(this, TNRallyTabletState::ViewportZOrder);
	}
}

void UTN_RallyCopilotTablet::ShowInWorld(UWidgetComponent& Host)
{
	if (WorldHost.Get() == &Host && Host.GetWidget() == this)
	{
		return;
	}
	// Primero el estado: NativeDestruct (al salir de la pantalla) no debe sacarla del registro.
	UWidgetComponent* Previous = WorldHost.Get();
	Presentation = ETNRallyTabletPresentation::World;
	WorldHost = &Host;
	if (Previous && Previous != &Host && Previous->GetWidget() == this)
	{
		Previous->SetWidget(nullptr);
	}
	if (TNVR::IsOnScreen(this))
	{
		RemoveFromParent();
	}
	if (APlayerController* Player = GetOwningPlayer())
	{
		Host.SetOwnerPlayer(Player->GetLocalPlayer());
	}
	Host.SetWidget(this);
}

bool UTN_RallyCopilotTablet::ToggleFor(APlayerController* Player)
{
	UTN_RallyCopilotTablet* Tablet = FindOrCreateFor(Player);
	if (!Tablet)
	{
		return false;
	}
	Tablet->ToggleTablet();
	return Tablet->IsTabletOpen();
}

bool UTN_RallyCopilotTablet::IsOpenFor(const APlayerController* Player)
{
	const UTN_RallyCopilotTablet* Tablet = FindFor(Player);
	return Tablet && Tablet->IsTabletOpen();
}

void UTN_RallyCopilotTablet::BindToggleKeys(UInputComponent* Input, APawn* Pawn)
{
	if (!Input || !Pawn)
	{
		return;
	}
	const TWeakObjectPtr<APawn> WeakPawn(Pawn);
	// Teclas fijas mientras no haya acción de Enhanced Input para la tableta (UTN_BuggyInputSet).
	for (const FKey& Key : { EKeys::Tab, EKeys::M, EKeys::Gamepad_Special_Left })
	{
		FInputKeyBinding Binding{ FInputChord(Key), IE_Pressed };
		Binding.bConsumeInput = true;
		Binding.KeyDelegate.GetDelegateForManualSet().BindLambda([WeakPawn]()
		{
			if (const APawn* Owner = WeakPawn.Get())
			{
				ToggleFor(Cast<APlayerController>(Owner->GetController()));
			}
		});
		Input->KeyBindings.Add(MoveTemp(Binding));
	}
}

FTNRallyTabletAmmo UTN_RallyCopilotTablet::ReadAmmo(const UTN_BuggyTurretComponent* Turret)
{
	FTNRallyTabletAmmo Result;
	if (!Turret)
	{
		return Result;
	}
	Result.Special = Turret->GetSpecialAmmo();
	Result.SpecialCharges = Turret->GetSpecialCharges();
	Result.Heat01 = Turret->GetHeat01();
	Result.bOverheated = Turret->IsOverheated();
	// La que dispara el botón principal (la cambian la rueda y las crucetas: UTN_BuggyTurretComponent::CycleAmmo).
	Result.Selected = Turret->GetSelectedAmmo();
	return Result;
}

// ── Estado ─────────────────────────────────────────────────────────────────────────────────────────────────────

void UTN_RallyCopilotTablet::SetTabletOpen(bool bInOpen)
{
	bOpen = bInOpen;
}

void UTN_RallyCopilotTablet::ToggleTablet()
{
	SetTabletOpen(!bOpen);
}

bool UTN_RallyCopilotTablet::IsTabletOpen() const
{
	return bOpen && !bCompact;
}

void UTN_RallyCopilotTablet::SetCompactMode(bool bInCompact)
{
	bAutoRole = false;
	bCompact = bInCompact;
}

void UTN_RallyCopilotTablet::SetAutoRole(bool bInAutoRole)
{
	bAutoRole = bInAutoRole;
}

FText UTN_RallyCopilotTablet::GetNextNoteText() const
{
	return Ahead.Num() > 0 ? TNRallyPaceNotes::NoteText(Ahead[0].Note) : FText::GetEmpty();
}

void UTN_RallyCopilotTablet::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// Solo pinta: no coge el ratón, el teclado ni el mando.
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UTN_RallyCopilotTablet::NativeConstruct()
{
	Super::NativeConstruct();
	// También al volver de un panel del mundo a la pantalla (o al revés): sigue siendo la tableta de su jugador.
	if (const APlayerController* Player = GetOwningPlayer())
	{
		TNRallyTabletState::Registry().Add(TObjectKey<APlayerController>(Player), this);
	}
}

void UTN_RallyCopilotTablet::NativeDestruct()
{
	const APlayerController* Player = GetOwningPlayer();
	const TWeakObjectPtr<UTN_RallyCopilotTablet>* Found = Player
		? TNRallyTabletState::Registry().Find(TObjectKey<APlayerController>(Player)) : nullptr;
	// Al pasar de la pantalla a un panel del mundo (o al revés) se destruye el widget de Slate anterior: si ya está
	// puesta en el otro sitio, sigue registrada.
	if (Found && Found->Get() == this && !IsPresented())
	{
		TNRallyTabletState::Registry().Remove(TObjectKey<APlayerController>(Player));
	}
	Super::NativeDestruct();
}

void UTN_RallyCopilotTablet::AddHitLine(const FText& Line, const FLinearColor& Color)
{
	TNRallyHitLog::FLine Entry;
	Entry.Text = Line;
	Entry.Color = Color;
	HitLog = TNRallyHitLog::Push(HitLog, Entry);
}

void UTN_RallyCopilotTablet::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Clock += InDeltaTime;
	if (HitLog.Num() > 0)
	{
		HitLog = TNRallyHitLog::Age(HitLog, InDeltaTime);
	}
	const UWorld* World = GetWorld();
	const ATN_RallyGameState* RallyState = World ? World->GetGameState<ATN_RallyGameState>() : nullptr;
	if (RallyState)
	{
		RefreshTrack(*RallyState);
	}
	RefreshView(RallyState);
	if (View == ETNRallyTabletView::Hidden || !RallyState)
	{
		Ahead.Reset();
		BoxesAhead.Reset();
		return;
	}
	RefreshMarks(*RallyState);
	if (const ATN_Buggy* Buggy = FindLocalBuggy(RallyState))
	{
		RefreshProgress(*Buggy);
		Ammo = ReadAmmo(Buggy->GetTurret());
	}
	Ahead = bHasArc ? TNRallyPaceNotes::NotesAhead(TrackNotes, MyArcCm, LookAheadCm) : TArray<TNRallyPaceNotes::FNoteAhead>();
	BoxesAhead = bHasArc ? TNRallyPaceNotes::ArcsAhead(AmmoRowNoteArcs, TrackNotes.LengthCm, TrackNotes.bClosed, MyArcCm, LookAheadCm)
		: TArray<double>();
}

// ── Refresco ───────────────────────────────────────────────────────────────────────────────────────────────────

const FTNRallyStanding* UTN_RallyCopilotTablet::FindLocalStanding(const ATN_RallyGameState* RallyState) const
{
	const APlayerController* Player = GetOwningPlayer();
	return (RallyState && Player) ? RallyState->FindStandingForPlayer(Player->PlayerState) : nullptr;
}

const ATN_Buggy* UTN_RallyCopilotTablet::FindLocalBuggy(const ATN_RallyGameState* RallyState) const
{
	const APawn* Pawn = GetOwningPlayerPawn();
	if (const ATN_Buggy* Driven = Cast<ATN_Buggy>(Pawn))
	{
		return Driven;
	}
	if (const ATN_BuggyGunnerPawn* Gunner = Cast<ATN_BuggyGunnerPawn>(Pawn))
	{
		return Gunner->GetBuggy();
	}
	// Sin peón propio (reaparición, cambio de plaza): el buggy de su fila de puestos.
	const FTNRallyStanding* Mine = FindLocalStanding(RallyState);
	return Mine ? Cast<ATN_Buggy>(Mine->Vehicle) : nullptr;
}

void UTN_RallyCopilotTablet::RefreshView(const ATN_RallyGameState* RallyState)
{
	const FTNRallyStanding* Mine = FindLocalStanding(RallyState);
	// En meta se guarda: la cámara pasa al podio y al espectador (#306).
	const bool bUsable = RallyState && TrackNotes.IsValid() && RallyState->Phase != ETNRallyPhase::Results && !(Mine && Mine->bFinished);
	if (!bAutoRole)
	{
		View = !bUsable ? ETNRallyTabletView::Hidden
			: (bCompact ? ETNRallyTabletView::Compact : (bOpen ? ETNRallyTabletView::Full : ETNRallyTabletView::Hidden));
		return;
	}
	const APawn* Pawn = GetOwningPlayerPawn();
	const bool bGunner = Cast<ATN_BuggyGunnerPawn>(Pawn) != nullptr;
	const ATN_Buggy* Driven = Cast<ATN_Buggy>(Pawn);
	// Conductora sola: su fila no tiene artillera (o, sin fila todavía, el buggy no lleva peón de artillera).
	const bool bDriverAlone = Driven && (Mine ? Mine->Gunner == nullptr : Driven->GetGunnerPawn() == nullptr);
	bCompact = !bGunner;
	if (!bGunner)
	{
		// Al dejar la plaza de artillera se guarda: al volver a ella empieza cerrada.
		bOpen = false;
	}
	if (!bUsable)
	{
		View = ETNRallyTabletView::Hidden;
		return;
	}
	View = bGunner ? (bOpen ? ETNRallyTabletView::Full : ETNRallyTabletView::Hidden)
		: (bDriverAlone ? ETNRallyTabletView::Compact : ETNRallyTabletView::Hidden);
}

void UTN_RallyCopilotTablet::RefreshTrack(const ATN_RallyGameState& RallyState)
{
	const ATN_RallyTrack* Track = RallyState.GetTrack();
	if (!Track || !Track->IsBuilt())
	{
		if (TrackNotes.IsValid() || NotesTrack.IsValid())
		{
			TrackNotes = TNRallyPaceNotes::FTrackNotes();
			AmmoRowNoteArcs.Reset();
			NotesTrack.Reset();
			bHasArc = false;
		}
		return;
	}
	const float Length = Track->GetTrackLengthCm();
	if (NotesTrack.Get() == Track && FMath::IsNearlyEqual(Length, NotesTrackLengthCm, 1.f) && TrackNotes.IsValid())
	{
		return;
	}
	// Una vez por pista (o al reconstruirla): unos pocos miles de muestras de la spline.
	TrackNotes = TNRallyPaceNotes::BuildForTrack(*Track);
	NotesTrack = Track;
	NotesTrackLengthCm = Length;
	bHasArc = false;
	RefreshMapBounds();
	// Las filas de cajas, en el eje de las notas (una polilínea de la spline: su longitud difiere unas milésimas).
	AmmoRowNoteArcs.Reset();
	const double Scale = Length > 0.f ? TrackNotes.LengthCm / Length : 1.0;
	for (const double Arc : Track->GetAmmoRowArcs())
	{
		AmmoRowNoteArcs.Add(Arc * Scale);
	}
}

void UTN_RallyCopilotTablet::RefreshMapBounds()
{
	MapBounds = FBox2D(ForceInit);
	for (const FVector& Point : TrackNotes.Points)
	{
		MapBounds += FVector2D(Point.X, Point.Y);
	}
}

void UTN_RallyCopilotTablet::RefreshMarks(const ATN_RallyGameState& RallyState)
{
	Marks.Reset();
	const FTNRallyStanding* Mine = FindLocalStanding(&RallyState);
	for (const FTNRallyStanding& Entry : RallyState.Standings)
	{
		if (!Entry.Vehicle)
		{
			continue;
		}
		FTNRallyTabletMark Mark;
		Mark.Location = Entry.Vehicle->GetActorLocation();
		Mark.Color = TNBuggy::TeamColor(Entry.TeamIndex);
		Mark.Place = Entry.Place;
		Mark.bMine = Mine && Mine->TeamIndex == Entry.TeamIndex;
		Mark.bOut = Entry.bRetired || Entry.bFinished;
		if (Mark.bMine)
		{
			MyColor = Mark.Color;
		}
		Marks.Add(Mark);
	}
}

void UTN_RallyCopilotTablet::RefreshProgress(const ATN_Buggy& Buggy)
{
	const ATN_RallyTrack* Track = NotesTrack.Get();
	if (!Track || NotesTrackLengthCm <= 0.f)
	{
		bHasArc = false;
		return;
	}
	const FVector Location = Buggy.GetActorLocation();
	double Arc = bHasArc ? Track->FindArcNear(Location, TrackArcCm) : Track->FindArcGlobal(Location);
	if (FVector::Dist2D(Track->GetLocationAtArc(Arc), Location) > TNRallyTabletState::ArcLostDistanceCm)
	{
		// Reaparición o atajo: la ventana de FindArcNear ya no lo encuentra.
		Arc = Track->FindArcGlobal(Location);
	}
	TrackArcCm = Arc;
	bHasArc = true;
	// El eje de las notas es una polilínea de la spline: su longitud difiere unas milésimas.
	MyArcCm = TrackArcCm * (TrackNotes.LengthCm / NotesTrackLengthCm);
}

#if !UE_BUILD_SHIPPING
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

namespace TNRallyTabletDebug
{
	/** Panel de prueba delante del buggy, mirando a la cámara de persecución (cm, en el espacio del buggy). */
	const FVector PanelOffset(300.0, 0.0, 170.0);
	constexpr float PanelCmPerPx = 0.25f;

	APlayerController* FirstLocalPlayer()
	{
		for (const FWorldContext& Context : GEngine ? GEngine->GetWorldContexts() : TIndirectArray<FWorldContext>())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld())
			{
				return World->GetFirstPlayerController();
			}
		}
		return nullptr;
	}

	/** Pone la tableta del primer jugador en un panel 3D sobre su peón (#334) o la devuelve a la pantalla. */
	void HostOnPawn(bool bWorld)
	{
		APlayerController* Player = FirstLocalPlayer();
		APawn* Pawn = Player ? Player->GetPawn() : nullptr;
		if (!Pawn || !bWorld)
		{
			UTN_RallyCopilotTablet::PresentOnScreenFor(Player);
			return;
		}
		UWidgetComponent* Panel = NewObject<UWidgetComponent>(Pawn, TEXT("RallyTabletDebugPanel"), RF_Transient);
		Panel->SetupAttachment(Pawn->GetRootComponent());
		Panel->SetRelativeLocationAndRotation(PanelOffset, FRotator(10.0, 180.0, 0.0));
		Panel->SetRelativeScale3D(FVector(PanelCmPerPx));
		Panel->SetWidgetSpace(EWidgetSpace::World);
		Panel->SetDrawSize(FVector2D(TNRallyTabletLayout::DesignSize(ETNRallyTabletView::Compact)));
		Panel->SetBlendMode(EWidgetBlendMode::Transparent);
		Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Panel->RegisterComponent();
		const UTN_RallyCopilotTablet* Tablet = UTN_RallyCopilotTablet::PresentInWorldFor(Player, Panel);
		UE_LOG(LogTemp, Display, TEXT("[RallyTablet] Tableta en un panel del mundo sobre %s: %s"), *GetNameSafe(Pawn),
			Tablet && Tablet->GetPresentation() == ETNRallyTabletPresentation::World ? TEXT("sí") : TEXT("no"));
	}

	FAutoConsoleCommandWithWorldAndArgs CmdTabletWorldLater(TEXT("TN.Rally.TabletWorldLater"),
		TEXT("Rally: TN.Rally.TabletWorldLater <espera> [1 = panel 3D sobre el peón, 0 = pantalla]: presentación de la tableta (#334)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			const float Wait = Args.IsValidIndex(0) ? FMath::Max(0.1f, FCString::Atof(*Args[0])) : 5.f;
			const bool bWorld = !Args.IsValidIndex(1) || FCString::Atoi(*Args[1]) != 0;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bWorld](float)
			{
				HostOnPawn(bWorld);
				return false;
			}), Wait);
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdTabletOpenLater(TEXT("TN.Rally.TabletOpenLater"),
		TEXT("Rally: TN.Rally.TabletOpenLater <espera>: abre la tableta grande del jugador local (para fotos y pruebas sin teclado)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			const float Wait = Args.IsValidIndex(0) ? FMath::Max(0.1f, FCString::Atof(*Args[0])) : 5.f;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
			{
				APlayerController* Player = FirstLocalPlayer();
				UTN_RallyCopilotTablet* Tablet = Player ? UTN_RallyCopilotTablet::FindOrCreateFor(Player) : nullptr;
				if (Tablet)
				{
					Tablet->SetTabletOpen(true);
				}
				UE_LOG(LogTNRally, Log, TEXT("[RallyTablet] TN.Rally.TabletOpenLater: %s"), Tablet && Tablet->IsTabletOpen() ? TEXT("abierta") : TEXT("sin abrir"));
				return false;
			}), Wait);
		}));
}
#endif
