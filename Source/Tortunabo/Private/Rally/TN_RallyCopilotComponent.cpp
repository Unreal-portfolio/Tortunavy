#include "Rally/TN_RallyCopilotComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Rally/TN_RallyGameState.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyTrack.h"
#include "Rally/UI/TN_RallyDashboard.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UI/TN_RallyCopilotTabletDraw.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyGunnerPawn.h"

namespace TNRallyCopilotState
{
	/** Cada cuánto se mira si toca cantar (s): a 115 km/h son 3 m entre comprobaciones. */
	constexpr float TickSeconds = 0.1f;
	/** Si el buggy queda a más de esto del arco que se seguía (reaparición), se busca en toda la pista (cm). */
	constexpr double ArcLostDistanceCm = 4000.0;

	TAutoConsoleVariable<int32> CVarCopilot(TEXT("TN.Rally.Copilot"), 1,
		TEXT("Rally: copiloto automático (#331). 0 = apagado, 1 = automático (conductora sin artillera humana), 2 = siempre, también en la plaza de artillera (pruebas)."));
}

UTN_RallyCopilotComponent::UTN_RallyCopilotComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = TNRallyCopilotState::TickSeconds;
	SetIsReplicatedByDefault(false);
	static ConstructorHelpers::FObjectFinder<USoundBase> BeepFinder(TEXT("/Game/Audio/Rally/SFX_Rally_Call_Beep.SFX_Rally_Call_Beep"));
	static ConstructorHelpers::FObjectFinder<USoundBase> CrestFinder(TEXT("/Game/Audio/Rally/SFX_Rally_Call_Crest.SFX_Rally_Call_Crest"));
	static ConstructorHelpers::FObjectFinder<USoundBase> JumpFinder(TEXT("/Game/Audio/Rally/SFX_Rally_Call_Jump.SFX_Rally_Call_Jump"));
	static ConstructorHelpers::FObjectFinder<USoundBase> WaterFinder(TEXT("/Game/Audio/Rally/SFX_Rally_Call_Water.SFX_Rally_Call_Water"));
	BeepSound = BeepFinder.Object;
	CrestSound = CrestFinder.Object;
	JumpSound = JumpFinder.Object;
	WaterSound = WaterFinder.Object;
}

void UTN_RallyCopilotComponent::BeginPlay()
{
	Super::BeginPlay();
	SideAttenuation = NewObject<USoundAttenuation>(this, TEXT("CopilotSideAttenuation"), RF_Transient);
	SideAttenuation->Attenuation.bAttenuate = false;
	SideAttenuation->Attenuation.bSpatialize = true;
}

void UTN_RallyCopilotComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BeepTimer);
	}
	Super::EndPlay(EndPlayReason);
}

APlayerController* UTN_RallyCopilotComponent::GetLocalController() const
{
	APlayerController* Player = Cast<APlayerController>(GetOwner());
	return (Player && Player->IsLocalController()) ? Player : nullptr;
}

void UTN_RallyCopilotComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	SecondsSinceCall += DeltaTime;
	const UWorld* World = GetWorld();
	const ATN_RallyGameState* RallyState = World ? World->GetGameState<ATN_RallyGameState>() : nullptr;
	ATN_Buggy* Buggy = RallyState ? FindCopilotBuggy(*RallyState) : nullptr;
	if (!Buggy || !SyncTrack(*RallyState) || !FollowBuggy(*Buggy))
	{
		ResetCalls();
		return;
	}
	CallNextNote(*Buggy);
}

ATN_Buggy* UTN_RallyCopilotComponent::FindCopilotBuggy(const ATN_RallyGameState& RallyState) const
{
	const int32 Mode = TNRallyCopilotState::CVarCopilot.GetValueOnGameThread();
	const APlayerController* Player = GetLocalController();
	const bool bRacePhase = RallyState.Phase == ETNRallyPhase::Countdown || RallyState.Phase == ETNRallyPhase::Racing
		|| RallyState.Phase == ETNRallyPhase::Finishing;
	if (Mode <= 0 || !Player || !bRacePhase)
	{
		return nullptr;
	}
	ATN_Buggy* Driven = Cast<ATN_Buggy>(Player->GetPawn());
	ATN_Buggy* Buggy = Driven;
	if (!Buggy && Mode >= 2)
	{
		const ATN_BuggyGunnerPawn* Gunner = Cast<ATN_BuggyGunnerPawn>(Player->GetPawn());
		Buggy = Gunner ? Gunner->GetBuggy() : nullptr;
	}
	const FTNRallyStanding* Mine = Buggy ? RallyState.FindStandingForVehicle(Buggy) : nullptr;
	if (!Mine || Mine->bFinished || Mine->bRetired)
	{
		return nullptr;
	}
	const bool bHumanGunner = Mine->Gunner && !Mine->Gunner->IsABot();
	const bool bOn = Mode >= 2 || TNRallyCopilot::IsAutoCopilotActive(Driven != nullptr, bHumanGunner);
	return bOn ? Buggy : nullptr;
}

bool UTN_RallyCopilotComponent::SyncTrack(const ATN_RallyGameState& RallyState)
{
	const ATN_RallyTrack* Track = RallyState.GetTrack();
	if (!Track || !Track->IsBuilt())
	{
		return false;
	}
	const float Length = Track->GetTrackLengthCm();
	if (NotesTrack.Get() == Track && FMath::IsNearlyEqual(Length, NotesTrackLengthCm, 1.f) && TrackNotes.IsValid())
	{
		return true;
	}
	// Una vez por pista (o al reconstruirla), como la tableta.
	TrackNotes = TNRallyPaceNotes::BuildForTrack(*Track);
	NotesTrack = Track;
	NotesTrackLengthCm = Length;
	bHasArc = false;
	CalledArcs.Reset();
	return TrackNotes.IsValid();
}

bool UTN_RallyCopilotComponent::FollowBuggy(const ATN_Buggy& Buggy)
{
	const ATN_RallyTrack* Track = NotesTrack.Get();
	if (!Track || NotesTrackLengthCm <= 0.f)
	{
		return false;
	}
	const FVector Location = Buggy.GetActorLocation();
	double Arc = bHasArc ? Track->FindArcNear(Location, TrackArcCm) : Track->FindArcGlobal(Location);
	if (FVector::Dist2D(Track->GetLocationAtArc(Arc), Location) > TNRallyCopilotState::ArcLostDistanceCm)
	{
		Arc = Track->FindArcGlobal(Location);
	}
	TrackArcCm = Arc;
	bHasArc = true;
	// El eje de las notas es una polilínea de la spline: su longitud difiere unas milésimas.
	NoteArcCm = TrackArcCm * (TrackNotes.LengthCm / NotesTrackLengthCm);
	return true;
}

void UTN_RallyCopilotComponent::CallNextNote(ATN_Buggy& Buggy)
{
	const TArray<TNRallyPaceNotes::FNoteAhead> Ahead = TNRallyPaceNotes::NotesAhead(TrackNotes, NoteArcCm);
	CalledArcs = TNRallyCopilot::KeepCalledAhead(CalledArcs, Ahead);
	const double SpeedCms = FMath::Max(0.0, static_cast<double>(Buggy.GetForwardSpeedCms()));
	const int32 Index = TNRallyCopilot::PickNoteToCall(Ahead, SpeedCms, CalledArcs, SecondsSinceCall);
	if (Index == INDEX_NONE)
	{
		return;
	}
	const TNRallyPaceNotes::FNoteAhead& Entry = Ahead[Index];
	CalledArcs.Add(Entry.Note.ArcCm);
	SecondsSinceCall = 0.0;
	++CallCount;
	UE_LOG(LogTNRally, Log, TEXT("[RallyCopilot] %s a %.0f m, %.0f km/h (%.1f s): %s"), *GetNameSafe(&Buggy), Entry.DistanceCm / 100.0,
		TNRally::CmsToKmh(SpeedCms), SpeedCms > 1.0 ? Entry.DistanceCm / SpeedCms : 0.0, *TNRallyPaceNotes::NoteText(Entry.Note).ToString());
	PresentCall(Entry.Note, &Buggy);
}

void UTN_RallyCopilotComponent::ResetCalls()
{
	CalledArcs.Reset();
	bHasArc = false;
}

void UTN_RallyCopilotComponent::PresentCall(const TNRallyPaceNotes::FPaceNote& Note, ATN_Buggy* Buggy)
{
	PlaySignal(TNRallyCopilot::SignalFor(Note));
	if (UTN_RallyDashboardComponent* Dashboard = UTN_RallyDashboardComponent::FindOn(Buggy))
	{
		Dashboard->ShowCall(TNRallyCopilot::PlateHeadline(Note), TNRallyCopilot::PlateDetail(Note), TNRallyTablet::NoteColor(Note),
			TNRallyCopilot::PlateSeconds);
	}
}

void UTN_RallyCopilotComponent::PresentQuickCall(const FText& Headline, const TNRallyCopilot::FCallSignal& Signal,
	const FLinearColor& Accent, ATN_Buggy* Buggy)
{
	PlaySignal(Signal);
	if (UTN_RallyDashboardComponent* Dashboard = UTN_RallyDashboardComponent::FindOn(Buggy))
	{
		Dashboard->ShowCall(Headline, FText::GetEmpty(), Accent, TNRallyCopilot::PlateSeconds);
	}
}

void UTN_RallyCopilotComponent::PlaySignal(const TNRallyCopilot::FCallSignal& Signal)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	World->GetTimerManager().ClearTimer(BeepTimer);
	PendingSignal = Signal;
	PendingBeeps = FMath::Max(1, Signal.Beeps);
	PlayNextBeep();
	if (PendingBeeps > 0)
	{
		World->GetTimerManager().SetTimer(BeepTimer, this, &UTN_RallyCopilotComponent::PlayNextBeep,
			TNRallyCopilot::BeepIntervalSeconds, true);
	}
}

void UTN_RallyCopilotComponent::PlayNextBeep()
{
	if (PendingBeeps <= 0)
	{
		if (const UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(BeepTimer);
		}
		return;
	}
	--PendingBeeps;
	const APlayerController* Player = GetLocalController();
	APlayerCameraManager* Camera = Player ? Player->PlayerCameraManager.Get() : nullptr;
	USoundBase* Sound = SoundFor(PendingSignal.Sound);
	if (!Camera || !Sound)
	{
		return;
	}
	// Pegada a la cámara (la posición del oyente) y desplazada hacia el lado de la curva: se oye a ese lado aunque el buggy gire.
	const FVector Side(0.0, PendingSignal.Pan * SideOffsetCm, 0.0);
	UGameplayStatics::SpawnSoundAttached(Sound, Camera->GetRootComponent(), NAME_None, Side, FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset, false, Volume, PendingSignal.Pitch, 0.f, SideAttenuation);
}

USoundBase* UTN_RallyCopilotComponent::SoundFor(TNRallyCopilot::ECallSound Sound) const
{
	switch (Sound)
	{
	case TNRallyCopilot::ECallSound::Crest: return CrestSound;
	case TNRallyCopilot::ECallSound::Jump: return JumpSound;
	case TNRallyCopilot::ECallSound::Water: return WaterSound;
	default: return BeepSound;
	}
}
