// Presencia de Steam (Multiplayer/TN_RichPresenceRules.h): qué texto, clave y parámetros salen del menú, del lobby (con sus
// plazas) y de la partida, y qué modo sale de cada GameMode. Sin Steam ni mundo. Correr desde Session Frontend (categoría
// "Tortunabo.Online.RichPresence") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Online.RichPresence; Quit" -nullrhi -unattended

#include "Game/TN_RunGameMode.h"
#include "GameFramework/GameModeBase.h"
#include "Lobby/TN_HQGameMode.h"
#include "Menu/MP_MenuGameMode.h"
#include "Misc/AutomationTest.h"
#include "Multiplayer/TN_RichPresenceRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRichPresenceTestDetail
{
	FTNPresenceState Make(ETNPresenceMode Mode, int32 Players = 0, int32 MaxPlayers = 0)
	{
		FTNPresenceState State;
		State.Mode = Mode;
		State.Players = Players;
		State.MaxPlayers = MaxPlayers;
		return State;
	}

	FString Param(const FTNPresenceInfo& Info, const TCHAR* Key)
	{
		for (const TPair<FString, FString>& Pair : Info.Params)
		{
			if (Pair.Key == Key)
			{
				return Pair.Value;
			}
		}
		return TEXT("<no está>");
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Texto (en el idioma origen), clave de Steam y parámetros de cada estado
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRichPresenceTextTest,
	"Tortunabo.Online.RichPresence.Text",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRichPresenceTextTest::RunTest(const FString& Parameters)
{
	using namespace TNRichPresenceTestDetail;
	struct FCase
	{
		FTNPresenceState State;
		const TCHAR* Text;
		const TCHAR* Token;
		int32 NumParams;
	};
	const FCase Cases[] = {
		{ Make(ETNPresenceMode::Menu), TEXT("En el menú"), TEXT("TN_Menu"), 0 },
		{ Make(ETNPresenceMode::Lobby, 3, 8), TEXT("En el lobby (3/8)"), TEXT("TN_LobbyCount"), 2 },
		{ Make(ETNPresenceMode::Lobby, 1, 0), TEXT("En el lobby"), TEXT("TN_Lobby"), 0 },
		{ Make(ETNPresenceMode::Playing), TEXT("Jugando"), TEXT("TN_Playing"), 0 },
	};
	for (const FCase& Case : Cases)
	{
		const FTNPresenceInfo Info = TNRichPresence::Build(Case.State);
		TestEqual(FString::Printf(TEXT("Texto de %s"), Case.Token), Info.Status.BuildSourceString(), FString(Case.Text));
		TestEqual(FString::Printf(TEXT("Clave de «%s»"), Case.Text), Info.Token, FString(Case.Token));
		TestEqual(FString::Printf(TEXT("Parámetros de «%s»"), Case.Text), Info.Params.Num(), Case.NumParams);
		TestFalse(FString::Printf(TEXT("La clave de «%s» va sin almohadilla (la pone el subsistema de Steam)"), Case.Text),
			Info.Token.StartsWith(TEXT("#")));
	}

	// Los parámetros llevan el nombre de su {Argumento} en minúsculas: así los usa el archivo de presencia de Steamworks.
	const FTNPresenceInfo Lobby = TNRichPresence::Build(Make(ETNPresenceMode::Lobby, 3, 8));
	TestEqual(TEXT("Lobby: players"), Param(Lobby, TEXT("players")), FString(TEXT("3")));
	TestEqual(TEXT("Lobby: max"), Param(Lobby, TEXT("max")), FString(TEXT("8")));

	// Más tortugas que plazas (alguien entrando mientras se lee): nunca «9/8».
	const FTNPresenceInfo Over = TNRichPresence::Build(Make(ETNPresenceMode::Lobby, 9, 8));
	TestEqual(TEXT("La cuenta del lobby no pasa de las plazas"), Over.Status.BuildSourceString(), FString(TEXT("En el lobby (8/8)")));

	// La firma cambia con la cuenta y no con nada más: es lo que evita mandar lo mismo dos veces.
	TestEqual(TEXT("Mismo estado, misma firma"), TNRichPresence::Build(Make(ETNPresenceMode::Lobby, 2, 8)).Signature(),
		TNRichPresence::Build(Make(ETNPresenceMode::Lobby, 2, 8)).Signature());
	TestNotEqual(TEXT("Otra cuenta, otra firma"), TNRichPresence::Build(Make(ETNPresenceMode::Lobby, 2, 8)).Signature(),
		TNRichPresence::Build(Make(ETNPresenceMode::Lobby, 3, 8)).Signature());
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Modo según la clase del GameMode (la que replica AGameStateBase::GameModeClass)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRichPresenceModeTest,
	"Tortunabo.Online.RichPresence.Mode",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRichPresenceModeTest::RunTest(const FString& Parameters)
{
	using TNRichPresence::ModeFor;
	TestTrue(TEXT("Menú principal"), ModeFor(AMP_MenuGameMode::StaticClass()) == ETNPresenceMode::Menu);
	TestTrue(TEXT("Cuartel (lobby)"), ModeFor(ATN_HQGameMode::StaticClass()) == ETNPresenceMode::Lobby);
	TestTrue(TEXT("El modo único (Run): jugando"), ModeFor(ATN_RunGameMode::StaticClass()) == ETNPresenceMode::Playing);
	TestTrue(TEXT("GameMode sin texto propio"), ModeFor(AGameModeBase::StaticClass()) == ETNPresenceMode::Playing);
	TestTrue(TEXT("Sin clase (aún no ha replicado)"), ModeFor(nullptr) == ETNPresenceMode::Playing);
	return true;
}

#endif
