#include "Audio/TN_MatchMusicSubsystem.h"
#include "Audio/TN_MusicSynthComponent.h"
#include "Core/TN_CoopGameState.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_Log.h"
#include "Core/TN_MatchFlowTypes.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Stats/Stats.h"

// Ayudas de este fichero en un espacio de nombres propio (compilación unity: nada suelto en el ámbito global).
namespace TNMatchMusicLocal
{
	float GMatchMusicVolume = 1.f;
	FAutoConsoleVariableRef CVarMatchMusicVolume(
		TEXT("TN.Music.MatchVolume"),
		GMatchMusicVolume,
		TEXT("Volumen de la música de fin de partida (victoria, derrota y eliminado), de 0 a 1,5 (1 por defecto)."));

	/** Diez fotos por segundo: de sobra para la música (el estado se replica a 10-30 Hz). */
	constexpr double PollIntervalSeconds = 0.1;

	float GetMatchVolume()
	{
		return FMath::Clamp(GMatchMusicVolume, 0.f, 1.5f);
	}

	APlayerController* FindLocalController(UWorld& InWorld)
	{
		for (FConstPlayerControllerIterator It = InWorld.GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (Controller && Controller->IsLocalController())
			{
				return Controller;
			}
		}
		return nullptr;
	}

	TNMatchMusic::EFlow ToDirectorFlow(ETNMatchFlowState InState)
	{
		switch (InState)
		{
		case ETNMatchFlowState::Countdown: return TNMatchMusic::EFlow::Countdown;
		case ETNMatchFlowState::Cinematic: return TNMatchMusic::EFlow::Cinematic;
		case ETNMatchFlowState::InProgress: return TNMatchMusic::EFlow::InProgress;
		case ETNMatchFlowState::Results: return TNMatchMusic::EFlow::Results;
		default: return TNMatchMusic::EFlow::WaitingForPlayers;
		}
	}

	bool ParseTrackName(const FString& InName, ETNMusicTrack& OutTrack)
	{
		struct FTrackName
		{
			const TCHAR* English;
			const TCHAR* Spanish;
			ETNMusicTrack Track;
		};
		static const FTrackName Names[] = {
			{ TEXT("Victory"), TEXT("Victoria"), ETNMusicTrack::Victory },
			{ TEXT("Defeat"), TEXT("Derrota"), ETNMusicTrack::Defeat },
			{ TEXT("Eliminated"), TEXT("Eliminado"), ETNMusicTrack::Eliminated },
			{ TEXT("Shop"), TEXT("Tienda"), ETNMusicTrack::Shop },
			{ TEXT("Booth"), TEXT("Probador"), ETNMusicTrack::Booth },
			{ TEXT("None"), TEXT("Silencio"), ETNMusicTrack::None },
		};
		for (const FTrackName& Candidate : Names)
		{
			if (InName.Equals(Candidate.English, ESearchCase::IgnoreCase) || InName.Equals(Candidate.Spanish, ESearchCase::IgnoreCase))
			{
				OutTrack = Candidate.Track;
				return true;
			}
		}
		return false;
	}

	void HandlePlayCommand(const TArray<FString>& InArgs, UWorld* InWorld)
	{
		ETNMusicTrack Track = ETNMusicTrack::None;
		if (InArgs.Num() < 1 || !ParseTrackName(InArgs[0], Track))
		{
			UE_LOG(LogTortunabo, Display, TEXT("[MatchMusic] Uso: TN.Music.Play Victory|Defeat|Eliminated|Shop|Booth|None"));
			return;
		}
		UTN_MatchMusicSubsystem* MatchMusic = InWorld ? InWorld->GetSubsystem<UTN_MatchMusicSubsystem>() : nullptr;
		if (!MatchMusic)
		{
			UE_LOG(LogTortunabo, Display, TEXT("[MatchMusic] Aquí no hay música de partida (servidor dedicado, sin audio o sin mundo de juego)."));
			return;
		}
		MatchMusic->DebugPlayTrack(Track);
	}

	FAutoConsoleCommandWithWorldAndArgs PlayCommand(
		TEXT("TN.Music.Play"),
		TEXT("Hace sonar una pista de música en el jugador local: Victory, Defeat, Eliminated, Shop, Booth o None (también Victoria, Derrota, Eliminado, Tienda, Probador, Silencio)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandlePlayCommand));
}

// ─────────────────────────────────────────────────────────────────────────────
// UTN_MatchMusicSubsystem
// ─────────────────────────────────────────────────────────────────────────────

bool UTN_MatchMusicSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Solo donde alguien escucha: ni servidor dedicado ni ejecuciones sin audio.
	return !IsRunningDedicatedServer() && FApp::CanEverRenderAudio() && Super::ShouldCreateSubsystem(Outer);
}

bool UTN_MatchMusicSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTN_MatchMusicSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &UTN_MatchMusicSubsystem::HandleWorldBeginTearDown);
}

void UTN_MatchMusicSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);
	TearDownHandle.Reset();
	ReleaseMusic();
	Director.Reset();
	Super::Deinitialize();
}

TStatId UTN_MatchMusicSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTN_MatchMusicSubsystem, STATGROUP_Tickables);
}

void UTN_MatchMusicSubsystem::HandleWorldBeginTearDown(UWorld* InWorld)
{
	if (InWorld && InWorld == GetWorld())
	{
		// Vuelta al lobby, salida al menú u otro mapa: el PlayerController viaja al mundo nuevo, pero sin esta música.
		ReleaseMusic();
		Director.Reset();
	}
}

void UTN_MatchMusicSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown)
	{
		return;
	}
	const double NowSeconds = World->GetRealTimeSeconds();
	if (NowSeconds < NextPollSeconds)
	{
		return;
	}
	NextPollSeconds = NowSeconds + TNMatchMusicLocal::PollIntervalSeconds;
	Poll(*World, NowSeconds);
}

void UTN_MatchMusicSubsystem::Poll(UWorld& InWorld, double InNowSeconds)
{
	const ATN_CoopGameState* MatchState = InWorld.GetGameState<ATN_CoopGameState>();
	APlayerController* LocalController = TNMatchMusicLocal::FindLocalController(InWorld);
	if (!MatchState || !LocalController)
	{
		return;
	}

	TNMatchMusic::FSnapshot Snap;
	Snap.NowSeconds = InNowSeconds;
	Snap.Flow = TNMatchMusicLocal::ToDirectorFlow(MatchState->MatchFlowState);
	Snap.CountdownValue = MatchState->CountdownValue;
	if (const ATN_CoopPlayerState* LocalState = LocalController->GetPlayerState<ATN_CoopPlayerState>())
	{
		Snap.bHasLocalPlayer = true;
		// MarkPlayerDead también marca bHasFinishedRun: llegar a la meta es terminar sin estar eliminado.
		Snap.bLocalFinished = LocalState->bHasFinishedRun && !LocalState->bIsEliminated;
		// El DBNO (derribado a la espera de rescate) aún no cuenta: solo la muerte o la eliminación de verdad.
		Snap.bLocalOut = LocalState->bIsEliminated || !LocalState->bIsAlive;
		Snap.LocalRoundWins = LocalState->RoundWins;
	}
	for (const APlayerState* BaseState : MatchState->PlayerArray)
	{
		if (const ATN_CoopPlayerState* CoopState = Cast<ATN_CoopPlayerState>(BaseState))
		{
			Snap.MaxRoundWins = FMath::Max(Snap.MaxRoundWins, CoopState->RoundWins);
			Snap.bTeamReachedGoal |= CoopState->bHasFinishedRun && !CoopState->bIsEliminated;
		}
	}
	// La tabla de resultados también cuenta (conserva a quien llegó aunque luego se haya desconectado).
	for (const FTN_RaceResultEntry& ResultEntry : MatchState->RaceResults)
	{
		Snap.bTeamReachedGoal |= !ResultEntry.bIsEliminated && ResultEntry.FinishRank > 0;
	}
	// Con la carrera en marcha el componente se prepara ya, en silencio: cuando llegue la fanfarria o el jingle el
	// motor está caliente (volumen asentado) y su primer golpe suena entero.
	if (Snap.Flow == TNMatchMusic::EFlow::InProgress)
	{
		EnsureMusic(LocalController);
	}

	const TNMatchMusic::FRequest Request = Director.Update(Snap);
	if (Request.bValid)
	{
		ApplyRequest(LocalController, Request);
	}
	RefreshVolume();
}

void UTN_MatchMusicSubsystem::ApplyRequest(APlayerController* InLocalController, const TNMatchMusic::FRequest& InRequest)
{
	const ETNMusicTrack Track = static_cast<ETNMusicTrack>(InRequest.Track);
	UE_LOG(LogTortunabo, Log, TEXT("[MatchMusic] %s (fundido %.1f s)"), *UEnum::GetValueAsString(Track), InRequest.FadeSeconds);
	if (Track == ETNMusicTrack::None)
	{
		if (UTN_MusicSynthComponent* Existing = Music.Get())
		{
			Existing->StopMusic(InRequest.FadeSeconds);
		}
		return;
	}
	if (UTN_MusicSynthComponent* MusicComp = EnsureMusic(InLocalController))
	{
		MusicComp->PlayTrack(Track, InRequest.FadeSeconds);
	}
}

UTN_MusicSynthComponent* UTN_MatchMusicSubsystem::EnsureMusic(APlayerController* InLocalController)
{
	UTN_MusicSynthComponent* Existing = Music.Get();
	if (Existing && !Existing->IsBeingDestroyed() && Existing->GetOwner() == InLocalController)
	{
		return Existing;
	}
	ReleaseMusic();
	if (!InLocalController)
	{
		return nullptr;
	}
	// Componente propio (no el de la tienda, que puede haber viajado desde el lobby con el PlayerController).
	UTN_MusicSynthComponent* Created = UTN_MusicSynthComponent::AttachMusic2D(InLocalController);
	if (Created)
	{
		AppliedVolume = TNMatchMusicLocal::GetMatchVolume();
		Created->SetMusicVolume(AppliedVolume);
		Music = Created;
		// Si el director ya tenía una pista sonando (componente perdido y recreado), la retoma.
		const uint8 PlayingTrack = Director.GetCurrentTrack();
		if (PlayingTrack != TNMusic::ETrack::None)
		{
			Created->PlayTrack(static_cast<ETNMusicTrack>(PlayingTrack), 0.5f);
		}
	}
	return Created;
}

void UTN_MatchMusicSubsystem::ReleaseMusic()
{
	if (UTN_MusicSynthComponent* Existing = Music.Get())
	{
		if (IsValid(Existing) && !Existing->IsBeingDestroyed())
		{
			Existing->DestroyComponent();
		}
	}
	Music.Reset();
}

void UTN_MatchMusicSubsystem::RefreshVolume()
{
	UTN_MusicSynthComponent* Existing = Music.Get();
	const float Wanted = TNMatchMusicLocal::GetMatchVolume();
	if (Existing && !FMath::IsNearlyEqual(Wanted, AppliedVolume))
	{
		AppliedVolume = Wanted;
		Existing->SetMusicVolume(Wanted);
	}
}

void UTN_MatchMusicSubsystem::DebugPlayTrack(ETNMusicTrack InTrack)
{
	UWorld* World = GetWorld();
	APlayerController* LocalController = World ? TNMatchMusicLocal::FindLocalController(*World) : nullptr;
	if (InTrack == ETNMusicTrack::None)
	{
		if (UTN_MusicSynthComponent* Existing = Music.Get())
		{
			Existing->StopMusic(0.5f);
		}
	}
	else if (UTN_MusicSynthComponent* MusicComp = EnsureMusic(LocalController))
	{
		MusicComp->PlayTrack(InTrack, 0.3f);
	}
	Director.NotifyExternalTrack(static_cast<uint8>(InTrack));
	UE_LOG(LogTortunabo, Display, TEXT("[MatchMusic] Prueba: %s"), *UEnum::GetValueAsString(InTrack));
}
