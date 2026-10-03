#include "Lobby/TN_LobbyMission.h"
#include "Core/TN_GameModeSpawnUtils.h"
#include "Core/TN_Log.h"
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
	case ETNProcGameMode::FreeForAll: return NSLOCTEXT("Tortunabo", "MissionModeFreeForAll", "Todos contra Todos");
	default:                        return NSLOCTEXT("Tortunabo", "MissionModeClassic", "Clásico");
	}
}

ETNProcGameMode TNLobbyMission::NormalizeMenuMode(ETNProcGameMode Mode)
{
	for (const ETNProcGameMode MenuMode : MenuModes)
	{
		if (MenuMode == Mode)
		{
			return Mode;
		}
	}
	return ETNProcGameMode::Coop;
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

ETNProcGameMode TNLobbyMission::NextSelectorMode(ETNProcGameMode Current, int32 ConnectedPlayers)
{
	const int32 Count = static_cast<int32>(ETNProcGameMode::Count);
	int32 Next = static_cast<int32>(Current);
	for (int32 Step = 0; Step < Count; ++Step)
	{
		Next = (Next + 1) % Count;
		// 2 vs 2 solo se ofrece con exactamente 4 jugadores.
		if (static_cast<ETNProcGameMode>(Next) != ETNProcGameMode::TwoVsTwo || ConnectedPlayers == 4)
		{
			break;
		}
	}
	return static_cast<ETNProcGameMode>(Next);
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
	if (Mode == ETNProcGameMode::TwoVsTwo)
	{
		const UWorld* World = TNLobbyMissionDetail::WorldOf(WorldContext);
		if (TN_CountConnectedCoopPlayers(World ? World->GetGameState() : nullptr) != 4)
		{
			return false;
		}
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
