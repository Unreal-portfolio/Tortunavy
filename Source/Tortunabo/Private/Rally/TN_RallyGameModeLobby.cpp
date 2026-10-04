// ATN_RallyGameMode: lo que el modo Rally único (#631) trae del lobby y de los karts. Dificultad y plazas del anfitrión,
// parrilla completada con bots hasta MinTeams, bots ajustados a la dificultad y vuelta al lobby al acabar los resultados.
// Vale igual en los circuitos de LVL_Rally y en el mapa generado (ATN_KartGameMode).
#include "Rally/TN_RallyGameMode.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Rally/TN_RallyAIController.h"
#include "Rally/TN_RallyLogic.h"

namespace TNRallyLobby
{
	TAutoConsoleVariable<int32> CVarRallyBots(TEXT("TN.Rally.Bots"), -1,
		TEXT("Rally: bots de la parrilla (-1 = los que falten hasta MinTeams buggies; ?Bots= en la URL manda). Vale para la próxima partida."));

	TAutoConsoleVariable<int32> CVarRallySeats(TEXT("TN.Rally.Seats"), 0,
		TEXT("Rally: tortugas por buggy (1 o 2; 0 = las del anfitrión; ?Seats= en la URL manda). Vale para la próxima partida."));

	const TCHAR* const DefaultLobbyPath = TEXT("/Game/Maps/Lobby/LVL_Lobby");
	/** Variedad de velocidad entre bots (km/h a cada lado de la de su dificultad). */
	constexpr float BotSpeedSpreadKmh = 4.f;
}

ETNProcDifficulty TNRallyRace::ParseDifficulty(const FString& Option, ETNProcDifficulty Fallback)
{
	if (Option.Equals(TEXT("Easy"), ESearchCase::IgnoreCase)) { return ETNProcDifficulty::Easy; }
	if (Option.Equals(TEXT("Normal"), ESearchCase::IgnoreCase)) { return ETNProcDifficulty::Normal; }
	if (Option.Equals(TEXT("Hard"), ESearchCase::IgnoreCase)) { return ETNProcDifficulty::Hard; }
	return Fallback >= ETNProcDifficulty::Count ? ETNProcDifficulty::Normal : Fallback;
}

int32 TNRallyRace::DifficultyIndex(ETNProcDifficulty Difficulty)
{
	return FMath::Clamp(static_cast<int32>(Difficulty), 0, 2);
}

int32 TNRallyRace::DefaultBotCount(int32 ExpectedHumans, int32 Seats, int32 MinTeams, int32 Forced)
{
	const int32 HumanTeams = FMath::Clamp(FMath::DivideAndRoundUp(FMath::Max(1, ExpectedHumans), FMath::Clamp(Seats, 1, 2)), 0,
		TNRally::MaxGridSlots);
	const int32 FreeSlots = TNRally::MaxGridSlots - HumanTeams;
	if (Forced >= 0)
	{
		return FMath::Min(Forced, TNRally::MaxGridSlots);
	}
	return FMath::Clamp(MinTeams - HumanTeams, 0, FreeSlots);
}

float TNRallyRace::BotMaxSpeedKmh(const FVector& PerDifficulty, ETNProcDifficulty Difficulty, int32 Ordinal)
{
	// Ordinal 0, 1, 2, 3...: -4, 0, +4, -4... km/h.
	const float Spread = static_cast<float>((FMath::Abs(Ordinal) % 3) - 1) * TNRallyLobby::BotSpeedSpreadKmh;
	return static_cast<float>(PerDifficulty[DifficultyIndex(Difficulty)]) + Spread;
}

bool TNRallyRace::ShouldEndWarmup(const FWarmupGate& Gate)
{
	if (!Gate.bHasTeams || Gate.WarmupEndTime <= 0.0 || Gate.Now < Gate.WarmupEndTime)
	{
		return false;
	}
	if (!Gate.bWaitForLobby)
	{
		return true;
	}
	return Gate.ArrivedHumans >= FMath::Max(1, Gate.ExpectedHumans) || Gate.WaitedSeconds >= Gate.WaitMaxSeconds;
}

bool ATN_RallyGameMode::ShouldWaitForLobby() const
{
	return bFromLobby && !bAutoStart;
}

int32 ATN_RallyGameMode::CountArrivedHumans() const
{
	int32 Spectators = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Player = It->Get();
		Spectators += Player && Player->PlayerState && Player->PlayerState->IsOnlyASpectator() ? 1 : 0;
	}
	return CountSeatedHumans() + Spectators;
}

int32 ATN_RallyGameMode::GetForcedSeats() const
{
	return TNRallyLobby::CVarRallySeats.GetValueOnGameThread();
}

int32 ATN_RallyGameMode::GetForcedBots() const
{
	return TNRallyLobby::CVarRallyBots.GetValueOnGameThread();
}

FString ATN_RallyGameMode::ResolveLobbyOptions(const FString& Options)
{
	const UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance());
	ExpectedHumans = GI ? FMath::Max(1, GI->PendingTravelPlayerCount) : 1;
	Difficulty = TNRallyRace::ParseDifficulty(UGameplayStatics::ParseOption(Options, TEXT("ProcDifficulty")),
		GI ? GI->SelectedProcDifficulty : ETNProcDifficulty::Normal);
	bFromLobby = UGameplayStatics::HasOption(Options, TEXT("FromLobby"));
	bReturnToLobbyAfterResults = bFromLobby && !UGameplayStatics::HasOption(Options, TEXT("Races"));

	// Plazas y bots: lo que no diga la URL lo ponen el anfitrión (con el general o al crear la sala) y la parrilla mínima.
	FString Resolved = Options;
	int32 TeamSeats = FMath::Clamp(DefaultSeats, 1, 2);
	if (UGameplayStatics::HasOption(Options, TEXT("Seats")))
	{
		TeamSeats = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Seats"), TeamSeats), 1, 2);
	}
	else
	{
		TeamSeats = GI ? FMath::Clamp(GI->SelectedKartSeats, 1, 2) : TeamSeats;
		const int32 Forced = GetForcedSeats();
		TeamSeats = Forced == 1 || Forced == 2 ? Forced : TeamSeats;
		Resolved += FString::Printf(TEXT("?Seats=%d"), TeamSeats);
	}
	if (!UGameplayStatics::HasOption(Options, TEXT("Bots")))
	{
		const int32 Forced = GetForcedBots();
		Resolved += FString::Printf(TEXT("?Bots=%d"), TNRallyRace::DefaultBotCount(ExpectedHumans, TeamSeats, MinTeams, Forced));
	}
	return Resolved;
}

void ATN_RallyGameMode::ConfigureBot(ATN_RallyAIController& Pilot)
{
	const int32 Index = TNRallyRace::DifficultyIndex(Difficulty);
	Pilot.MaxSpeedKmh = TNRallyRace::BotMaxSpeedKmh(BotMaxSpeedKmh, Difficulty, NextBotOrdinal++);
	Pilot.SpecialFireChance = FMath::Clamp(static_cast<float>(BotSpecialFireChance[Index]), 0.f, 1.f);
	Pilot.FireIntervalSeconds = FMath::Max(0.2f, static_cast<float>(BotFireIntervalSeconds[Index]));
	Pilot.FireRangeCm = FMath::Max(500.f, static_cast<float>(BotFireRangeCm[Index]));
	UE_LOG(LogTNRally, Verbose, TEXT("[RallyGameMode] Bot %s: %.0f km/h, disparo cada %.1f s (%s)."), *Pilot.GetName(),
		Pilot.MaxSpeedKmh, Pilot.FireIntervalSeconds, *UEnum::GetValueAsString(Difficulty));
}

void ATN_RallyGameMode::ReturnToLobbyNow()
{
	UWorld* World = GetWorld();
	if (bReturning || !World || World->IsInSeamlessTravel())
	{
		return;
	}
	bReturning = true;
	bRestartRequested = true;
	// Peones fuera antes del viaje (buggies, peones de artillera y espectadores): al lobby no llega nada de la carrera.
	TArray<APawn*> Pawns;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		Pawns.Add(*It);
	}
	for (APawn* Pawn : Pawns)
	{
		if (IsValid(Pawn))
		{
			Pawn->Destroy();
		}
	}
	// Al lobby del que se salió (lo apunta ATN_HQGameMode); «?game=» quita el alias del modo de la URL.
	const UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance());
	const FString Lobby = GI && !GI->LobbyReturnMapPath.IsEmpty() ? GI->LobbyReturnMapPath : FString(TNRallyLobby::DefaultLobbyPath);
	const FString TravelURL = Lobby + TEXT("?game=");
	UE_LOG(LogTNRally, Log, TEXT("[RallyGameMode] Vuelta al lobby: %s"), *TravelURL);
	World->ServerTravel(TravelURL);
}
