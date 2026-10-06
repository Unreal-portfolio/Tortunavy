#include "Multiplayer/TN_RichPresenceRules.h"
#include "Core/TN_LocText.h"
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
	ETNPresenceMode ModeFor(const UClass* GameModeClass)
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
		default:
			return Plain(TEXT("TN_Playing"), NSLOCTEXT("TNPresence", "Playing", "Jugando"));
		}
	}
}
