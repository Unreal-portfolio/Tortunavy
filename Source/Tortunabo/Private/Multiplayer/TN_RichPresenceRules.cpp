#include "Multiplayer/TN_RichPresenceRules.h"
#include "Core/TN_LocText.h"
#include "Game/TN_BeachRaceGameMode.h"
#include "Game/TN_ProcMapGameMode.h"
#include "Game/TN_RunGameMode.h"
#include "Game/TN_SurvivalGameMode.h"
#include "Lobby/TN_HQGameMode.h"
#include "Menu/MP_MenuGameMode.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto
// del bloque.
namespace TNRichPresenceDetail
{
	/** Un estado sin números: solo la clave y el texto. */
	FTNPresenceInfo Plain(const TCHAR* Token, const FText& Status)
	{
		FTNPresenceInfo Info;
		Info.Token = Token;
		Info.Status = Status;
		return Info;
	}

	/** Un estado con un número ({Level} o {Round}): su parámetro va a Steam con el nombre en minúsculas. */
	FTNPresenceInfo WithNumber(const TCHAR* Token, const FText& Pattern, const TCHAR* ArgName, int32 Value)
	{
		FFormatNamedArguments Args;
		Args.Add(ArgName, TNLocText::Int(Value));
		FTNPresenceInfo Info;
		Info.Token = Token;
		Info.Status = FText::Format(Pattern, Args);
		Info.Params.Emplace(FString(ArgName).ToLower(), FString::FromInt(Value));
		return Info;
	}

	FTNPresenceInfo Lobby(const FTNPresenceState& State)
	{
		if (State.MaxPlayers <= 0)
		{
			return Plain(TEXT("TN_Lobby"), NSLOCTEXT("TNPresence", "Lobby", "En el lobby"));
		}
		const int32 Players = FMath::Clamp(State.Players, 0, State.MaxPlayers);
		FFormatNamedArguments Args;
		Args.Add(TEXT("Players"), TNLocText::Int(Players));
		Args.Add(TEXT("Max"), TNLocText::Int(State.MaxPlayers));
		FTNPresenceInfo Info;
		Info.Token = TEXT("TN_LobbyCount");
		Info.Status = FText::Format(NSLOCTEXT("TNPresence", "LobbyCount", "En el lobby ({Players}/{Max})"), Args);
		Info.Params.Emplace(TEXT("players"), FString::FromInt(Players));
		Info.Params.Emplace(TEXT("max"), FString::FromInt(State.MaxPlayers));
		return Info;
	}
}

FString FTNPresenceInfo::Signature() const
{
	FString Out = Token + TEXT("|") + Status.ToString();
	for (const TPair<FString, FString>& Param : Params)
	{
		Out += FString::Printf(TEXT("|%s=%s"), *Param.Key, *Param.Value);
	}
	return Out;
}

namespace TNRichPresence
{
	ETNPresenceMode ModeFor(const UClass* GameModeClass, ETNProcGameMode ProcMode)
	{
		if (!GameModeClass)
		{
			return ETNPresenceMode::Playing;
		}
		if (GameModeClass->IsChildOf(AMP_MenuGameMode::StaticClass()))
		{
			return ETNPresenceMode::Menu;
		}
		if (GameModeClass->IsChildOf(ATN_HQGameMode::StaticClass()))
		{
			return ETNPresenceMode::Lobby;
		}
		// Los dos derivan de ATN_RunGameMode: antes que él.
		if (GameModeClass->IsChildOf(ATN_SurvivalGameMode::StaticClass()))
		{
			return ETNPresenceMode::Survival;
		}
		if (GameModeClass->IsChildOf(ATN_BeachRaceGameMode::StaticClass()))
		{
			return ETNPresenceMode::Race;
		}
		if (GameModeClass->IsChildOf(ATN_ProcMapGameMode::StaticClass()))
		{
			switch (ProcMode)
			{
			case ETNProcGameMode::Race:
				return ETNPresenceMode::Race;
			case ETNProcGameMode::TwoVsTwo:
				return ETNPresenceMode::TwoVsTwo;
			case ETNProcGameMode::Survival:
				return ETNPresenceMode::Survival;
			default:
				return ETNPresenceMode::Coop;
			}
		}
		if (GameModeClass->IsChildOf(ATN_RunGameMode::StaticClass()))
		{
			return ETNPresenceMode::Coop;
		}
		return ETNPresenceMode::Playing;
	}

	FTNPresenceInfo Build(const FTNPresenceState& State)
	{
		using namespace TNRichPresenceDetail;
		switch (State.Mode)
		{
		case ETNPresenceMode::Menu:
			return Plain(TEXT("TN_Menu"), NSLOCTEXT("TNPresence", "Menu", "En el menú"));
		case ETNPresenceMode::Lobby:
			return Lobby(State);
		case ETNPresenceMode::Coop:
			return State.Level > 0
				? WithNumber(TEXT("TN_CoopLevel"), NSLOCTEXT("TNPresence", "CoopLevel", "Coop, nivel {Level}"), TEXT("Level"), State.Level)
				: Plain(TEXT("TN_Coop"), NSLOCTEXT("TNPresence", "Coop", "Coop"));
		case ETNPresenceMode::Race:
			return State.Round > 0
				? WithNumber(TEXT("TN_RaceRound"), NSLOCTEXT("TNPresence", "RaceRound", "Carrera, ronda {Round}"), TEXT("Round"), State.Round)
				: Plain(TEXT("TN_Race"), NSLOCTEXT("TNPresence", "Race", "Carrera"));
		case ETNPresenceMode::TwoVsTwo:
			return State.Round > 0
				? WithNumber(TEXT("TN_TwoVsTwoRound"), NSLOCTEXT("TNPresence", "TwoVsTwoRound", "2 contra 2, ronda {Round}"), TEXT("Round"),
					State.Round)
				: Plain(TEXT("TN_TwoVsTwo"), NSLOCTEXT("TNPresence", "TwoVsTwo", "2 contra 2"));
		case ETNPresenceMode::Survival:
			return State.Level > 0
				? WithNumber(TEXT("TN_SurvivalLevel"), NSLOCTEXT("TNPresence", "SurvivalLevel", "Supervivencia, nivel {Level}"), TEXT("Level"),
					State.Level)
				: Plain(TEXT("TN_Survival"), NSLOCTEXT("TNPresence", "Survival", "Supervivencia"));
		default:
			return Plain(TEXT("TN_Playing"), NSLOCTEXT("TNPresence", "Playing", "En partida"));
		}
	}
}
