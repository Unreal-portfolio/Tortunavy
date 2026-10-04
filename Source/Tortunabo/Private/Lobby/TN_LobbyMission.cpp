#include "Lobby/TN_LobbyMission.h"

#include "Core/TN_LocText.h"
#include "Core/TN_GameModeSpawnUtils.h"
#include "Core/TN_Log.h"
#include "Game/TN_TctGameMode.h"
#include "Lobby/TN_GeneralBriefing.h"
#include "Lobby/TN_ProcModeSelector.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"

namespace TNLobbyMissionDetail
{
	UWorld* WorldOf(const UObject* WorldContext)
	{
		return WorldContext && GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	}

	/** La GameInstance del anfitrión: solo en el servidor (o en una partida sola). */
	UMP_GameInstance* HostGameInstance(const UObject* WorldContext)
	{
		const UWorld* World = WorldOf(WorldContext);
		if (!World || World->GetNetMode() == NM_Client)
		{
			return nullptr;
		}
		return Cast<UMP_GameInstance>(World->GetGameInstance());
	}
}

FText TNLobbyMission::ModeName(ETNProcGameMode Mode)
{
	switch (Mode)
	{
	case ETNProcGameMode::Coop:     return NSLOCTEXT("Tortunabo", "MissionModeCoop", "Cooperativo");
	case ETNProcGameMode::Race:     return NSLOCTEXT("Tortunabo", "MissionModeRace", "Carrera");
	case ETNProcGameMode::TwoVsTwo: return NSLOCTEXT("Tortunabo", "MissionMode2v2", "2 vs 2");
	case ETNProcGameMode::Survival: return NSLOCTEXT("Tortunabo", "MissionModeSurvival", "Supervivencia");
	case ETNProcGameMode::Karts:    return NSLOCTEXT("Tortunabo", "MissionModeKarts", "Karts");
	case ETNProcGameMode::FreeForAll: return NSLOCTEXT("Tortunabo", "MissionModeFreeForAll", "Todos contra Todos");
	case ETNProcGameMode::Rally:    return NSLOCTEXT("Tortunabo", "MissionModeRally", "Rally");
	default:                        return NSLOCTEXT("Tortunabo", "MissionModeClassic", "Clásico");
	}
}

bool TNLobbyMission::IsModePlayable(ETNProcGameMode Mode)
{
	if (Mode == ETNProcGameMode::FreeForAll)
	{
		return ATN_TctGameMode::HasDefaultArena();
	}
	if (Mode == ETNProcGameMode::Rally)
	{
		return RallyMapOptions().Num() > 0;
	}
	return Mode < ETNProcGameMode::Count;
}

TArray<ETNProcGameMode> TNLobbyMission::FilterMenuModes(bool bFreeForAllPlayable, bool bRallyPlayable)
{
	TArray<ETNProcGameMode> Modes;
	for (const ETNProcGameMode MenuMode : MenuModes)
	{
		const bool bBlocked = (MenuMode == ETNProcGameMode::FreeForAll && !bFreeForAllPlayable)
			|| (MenuMode == ETNProcGameMode::Rally && !bRallyPlayable);
		if (!bBlocked)
		{
			Modes.Add(MenuMode);
		}
	}
	return Modes;
}

TArray<ETNProcGameMode> TNLobbyMission::GetMenuModes()
{
	return FilterMenuModes(IsModePlayable(ETNProcGameMode::FreeForAll), IsModePlayable(ETNProcGameMode::Rally));
}

ETNProcGameMode TNLobbyMission::NormalizeMenuMode(ETNProcGameMode Mode)
{
	return GetMenuModes().Contains(Mode) ? Mode : ETNProcGameMode::Coop;
}

FText TNLobbyMission::RallyMapName(FName Variant)
{
	const FString Id = Variant.ToString();
	if (Id.Equals(TEXT("E01B_espana_rally"), ESearchCase::IgnoreCase)) { return NSLOCTEXT("Tortunabo", "RallyMapE01B", "España"); }
	if (Id.Equals(TEXT("I03R_tortuga_magna"), ESearchCase::IgnoreCase)) { return NSLOCTEXT("Tortunabo", "RallyMapI03R", "Tortuga Magna"); }
	if (Id.Equals(TEXT("I04_volcan_hueco"), ESearchCase::IgnoreCase)) { return NSLOCTEXT("Tortunabo", "RallyMapI04", "Volcán Hueco"); }
	if (Id.Equals(TEXT("I06_feroe"), ESearchCase::IgnoreCase)) { return NSLOCTEXT("Tortunabo", "RallyMapI06", "Islas Feroe"); }
	// Una variante que aún no tiene nombre traducido: su identificador, tal cual.
	return TNLocText::Literal(Variant.IsNone() ? FString() : Id);
}

FText TNLobbyMission::DifficultyName(ETNProcDifficulty Difficulty)
{
	switch (Difficulty)
	{
	case ETNProcDifficulty::Easy: return NSLOCTEXT("Tortunabo", "MissionDiffEasy", "Fácil");
	case ETNProcDifficulty::Hard: return NSLOCTEXT("Tortunabo", "MissionDiffHard", "Difícil");
	default:                      return NSLOCTEXT("Tortunabo", "MissionDiffNormal", "Normal");
	}
}

FText TNLobbyMission::ModeBlurb(ETNProcGameMode Mode)
{
	switch (Mode)
	{
	case ETNProcGameMode::Coop:
		return NSLOCTEXT("Tortunabo", "MissionBlurbCoop", "todas juntas, del castillo de arena al mapa procedural.");
	case ETNProcGameMode::Race:
		return NSLOCTEXT("Tortunabo", "MissionBlurbRace", "todas contra todas en la playa; gana quien consigue tres conchas.");
	case ETNProcGameMode::TwoVsTwo:
		return NSLOCTEXT("Tortunabo", "MissionBlurb2v2", "por parejas y con exactamente cuatro; gana la pareja que llega antes.");
	case ETNProcGameMode::Survival:
		return NSLOCTEXT("Tortunabo", "MissionBlurbSurvival", "nivel tras nivel, cada vez más difícil; gana la última tortuga en pie (sola, hasta que caigas).");
	case ETNProcGameMode::Karts:
		return NSLOCTEXT("Tortunabo", "MissionBlurbKarts", "en kart por el camino del cooperativo, sola o de dos en dos (una conduce y la otra dispara y usa los objetos); gana quien llega antes a la playa.");
	case ETNProcGameMode::Rally:
		return NSLOCTEXT("Tortunabo", "MissionBlurbRally", "en buggy por un circuito de autor, sola o por parejas (una conduce y la otra dispara la munición de las cajas «?» y carga el peso en las curvas); gana quien llega antes.");
	case ETNProcGameMode::FreeForAll:
		return NSLOCTEXT("Tortunabo", "MissionBlurbFreeForAll", "de 2 a 8 en una arena que se inunda; gana la ronda la última en pie y la partida, quien gane tres.");
	default:
		return NSLOCTEXT("Tortunabo", "MissionBlurbClassic", "el recorrido de siempre, por tramos.");
	}
}

FText TNLobbyMission::DifficultyBlurb(ETNProcDifficulty Difficulty)
{
	switch (Difficulty)
	{
	case ETNProcDifficulty::Easy:
		return NSLOCTEXT("Tortunabo", "MissionDiffBlurbEasy", "menos peligros y huecos, y la tormenta va más despacio.");
	case ETNProcDifficulty::Hard:
		return NSLOCTEXT("Tortunabo", "MissionDiffBlurbHard", "más peligros, huecos y cruces colosales, y la tormenta pisándote los talones.");
	default:
		return NSLOCTEXT("Tortunabo", "MissionDiffBlurbNormal", "lo que manda el reglamento.");
	}
}

ETNProcGameMode TNLobbyMission::NextSelectorMode(ETNProcGameMode Current, int32 ConnectedPlayers, bool bFreeForAllPlayable,
	bool bRallyPlayable)
{
	const int32 Count = static_cast<int32>(ETNProcGameMode::Count);
	int32 Next = static_cast<int32>(Current);
	for (int32 Step = 0; Step < Count; ++Step)
	{
		Next = (Next + 1) % Count;
		const ETNProcGameMode Candidate = static_cast<ETNProcGameMode>(Next);
		// 2 vs 2 solo se ofrece con exactamente 4 jugadores, Todos contra Todos solo con su arena y el Rally con algún circuito.
		const bool bTwoVsTwoBlocked = Candidate == ETNProcGameMode::TwoVsTwo && ConnectedPlayers != 4;
		const bool bFreeForAllBlocked = Candidate == ETNProcGameMode::FreeForAll && !bFreeForAllPlayable;
		const bool bRallyBlocked = Candidate == ETNProcGameMode::Rally && !bRallyPlayable;
		if (!bTwoVsTwoBlocked && !bFreeForAllBlocked && !bRallyBlocked)
		{
			break;
		}
	}
	return static_cast<ETNProcGameMode>(Next);
}

ETNProcGameMode TNLobbyMission::NextSelectorMode(ETNProcGameMode Current, int32 ConnectedPlayers)
{
	return NextSelectorMode(Current, ConnectedPlayers, IsModePlayable(ETNProcGameMode::FreeForAll),
		IsModePlayable(ETNProcGameMode::Rally));
}

bool TNLobbyMission::CanLocalPlayerChoose(const UObject* WorldContext)
{
	return TNLobbyMissionDetail::HostGameInstance(WorldContext) != nullptr;
}

ETNProcGameMode TNLobbyMission::GetHostMode(const UObject* WorldContext)
{
	const UWorld* World = TNLobbyMissionDetail::WorldOf(WorldContext);
	const UMP_GameInstance* GI = World ? Cast<UMP_GameInstance>(World->GetGameInstance()) : nullptr;
	return GI ? GI->SelectedProcMode : ETNProcGameMode::Coop;
}

ETNProcDifficulty TNLobbyMission::GetHostDifficulty(const UObject* WorldContext)
{
	const UWorld* World = TNLobbyMissionDetail::WorldOf(WorldContext);
	const UMP_GameInstance* GI = World ? Cast<UMP_GameInstance>(World->GetGameInstance()) : nullptr;
	return GI ? GI->SelectedProcDifficulty : ETNProcDifficulty::Normal;
}

bool TNLobbyMission::SetMode(const UObject* WorldContext, ETNProcGameMode Mode)
{
	UMP_GameInstance* GI = TNLobbyMissionDetail::HostGameInstance(WorldContext);
	if (!GI || Mode >= ETNProcGameMode::Count)
	{
		return false;
	}
	if (!IsModePlayable(Mode))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Misión] %s no se puede jugar en esta build (falta su arena): no se elige."),
			*UEnum::GetValueAsString(Mode));
		return false;
	}
	if (GI->SelectedProcMode != Mode)
	{
		GI->SelectedProcMode = Mode;
		UE_LOG(LogTortunabo, Log, TEXT("[Misión] Modo de la próxima partida: %s"), *UEnum::GetValueAsString(Mode));
	}
	SyncLobby(WorldContext);
	return true;
}

bool TNLobbyMission::SetDifficulty(const UObject* WorldContext, ETNProcDifficulty Difficulty)
{
	UMP_GameInstance* GI = TNLobbyMissionDetail::HostGameInstance(WorldContext);
	if (!GI || Difficulty >= ETNProcDifficulty::Count)
	{
		return false;
	}
	if (GI->SelectedProcDifficulty != Difficulty)
	{
		GI->SelectedProcDifficulty = Difficulty;
		UE_LOG(LogTortunabo, Log, TEXT("[Misión] Dificultad de la próxima partida: %s"), *UEnum::GetValueAsString(Difficulty));
	}
	SyncLobby(WorldContext);
	return true;
}

void TNLobbyMission::SyncLobby(const UObject* WorldContext)
{
	UWorld* World = TNLobbyMissionDetail::WorldOf(WorldContext);
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	// Cada uno replica lo suyo (y solo si ha cambiado): la pizarra del general y las etiquetas de los selectores.
	for (TActorIterator<ATN_GeneralBriefing> It(World); It; ++It)
	{
		It->SyncMissionFromGameInstance();
	}
	for (TActorIterator<ATN_ProcModeSelector> It(World); It; ++It)
	{
		It->SyncFromGameInstance();
	}
}
