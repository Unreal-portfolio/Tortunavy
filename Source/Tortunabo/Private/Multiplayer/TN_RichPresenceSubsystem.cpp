#include "Multiplayer/TN_RichPresenceSubsystem.h"
#include "Core/TN_CoopGameState.h"
#include "Core/TN_Log.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/TN_ProcMapGameState.h"
#include "GameFramework/GameStateBase.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlinePresenceInterface.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Multiplayer/TN_RoomTypes.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"

bool UTN_RichPresenceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Un servidor dedicado no tiene jugador local ni lista de amigos.
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UTN_RichPresenceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PollHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UTN_RichPresenceSubsystem::Poll), PollSeconds);
}

void UTN_RichPresenceSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(PollHandle);
	PollHandle.Reset();
	Super::Deinitialize();
}

TOptional<FTNPresenceState> UTN_RichPresenceSubsystem::ReadState() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState)
	{
		return {};
	}

	const ATN_ProcMapGameState* ProcState = Cast<ATN_ProcMapGameState>(GameState);
	FTNPresenceState State;
	State.Mode = TNRichPresence::ModeFor(GameState->GameModeClass, ProcState ? ProcState->ProcMode : ETNProcGameMode::Coop);
	if (ProcState)
	{
		// En el mapa procedural la ronda es el nivel del Coop y la ronda de la Carrera y del 2 contra 2.
		State.Level = ProcState->CurrentRound;
		State.Round = ProcState->CurrentRound;
	}
	else if (const ATN_CoopGameState* CoopState = Cast<ATN_CoopGameState>(GameState))
	{
		State.Level = CoopState->CurrentLevel;
	}

	if (State.Mode == ETNPresenceMode::Lobby)
	{
		FTNRoomSnapshot Room;
		const UMP_GameInstance* MPGameInstance = Cast<UMP_GameInstance>(GameInstance);
		if (MPGameInstance && MPGameInstance->GetRoomSnapshot(Room))
		{
			State.Players = Room.Players;
			State.MaxPlayers = Room.MaxPlayers;
		}
		else
		{
			// Fuera de una sala en red (lobby en solitario): sin plazas, no se enseña la cuenta.
			State.Players = GameState->PlayerArray.Num();
		}
	}
	return State;
}

bool UTN_RichPresenceSubsystem::Poll(float DeltaTime)
{
	const TOptional<FTNPresenceState> State = ReadState();
	if (!State.IsSet())
	{
		return true;
	}
	const FTNPresenceInfo Info = TNRichPresence::Build(State.GetValue());
	const FString Signature = Info.Signature();
	if (Signature == LastPushed)
	{
		return true;
	}
	const bool bSent = Push(Info);
	if (bSent)
	{
		LastPushed = Signature;
	}
	if (Signature != LastLogged)
	{
		LastLogged = Signature;
		UE_LOG(LogTortunabo, Log, TEXT("[Presencia] %s (#%s)%s"), *Info.Status.ToString(), *Info.Token,
			bSent ? TEXT("") : TEXT(": sin Steam, no se manda."));
	}
	return true;
}

bool UTN_RichPresenceSubsystem::Push(const FTNPresenceInfo& Info) const
{
	const UGameInstance* GameInstance = GetGameInstance();
	IOnlineSubsystem* const OnlineSub = Online::GetSubsystem(GameInstance ? GameInstance->GetWorld() : nullptr);
	if (!OnlineSub || OnlineSub->GetSubsystemName() != STEAM_SUBSYSTEM)
	{
		return false;
	}
	const IOnlinePresencePtr Presence = OnlineSub->GetPresenceInterface();
	const IOnlineIdentityPtr Identity = OnlineSub->GetIdentityInterface();
	if (!Presence.IsValid() || !Identity.IsValid() || Identity->GetLoginStatus(0) != ELoginStatus::LoggedIn)
	{
		return false;
	}
	const FUniqueNetIdPtr UserId = Identity->GetUniquePlayerId(0);
	if (!UserId.IsValid())
	{
		return false;
	}

	FOnlineUserPresenceStatus Status;
	Status.State = EOnlinePresenceState::Online;
	Status.StatusStr = Info.Token;
	Status.Properties.Add(TEXT("status"), FVariantData(Info.Status.ToString()));
	for (const TPair<FString, FString>& Param : Info.Params)
	{
		Status.Properties.Add(Param.Key, FVariantData(Param.Value));
	}
	Presence->SetPresence(*UserId, Status, IOnlinePresence::FOnPresenceTaskCompleteDelegate::CreateLambda(
		[Token = Info.Token](const FUniqueNetId&, const bool bWasSuccessful)
		{
			if (!bWasSuccessful)
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[Presencia] Steam no aceptó la presencia #%s."), *Token);
			}
		}));
	return true;
}
