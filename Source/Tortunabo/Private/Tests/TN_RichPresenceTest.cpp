// Presencia de Steam (Multiplayer/TN_RichPresenceRules.h): qué texto, clave y parámetros salen de cada modo, ronda, nivel y
// plazas, y qué modo sale de cada GameMode. Sin Steam ni mundo. Correr desde Session Frontend (categoría
// "Tortunabo.Online.RichPresence") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Online.RichPresence; Quit" -nullrhi -unattended

#include "Game/TN_BeachRaceGameMode.h"
#include "Game/TN_ProcMapGameMode.h"
#include "Game/TN_RunGameMode.h"
#include "Game/TN_SurvivalGameMode.h"
#include "GameFramework/GameModeBase.h"
#include "Lobby/TN_HQGameMode.h"
#include "Menu/MP_MenuGameMode.h"
#include "Misc/AutomationTest.h"
#include "Multiplayer/TN_RichPresenceRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRichPresenceTestDetail
{
	FTNPresenceState Make(ETNPresenceMode Mode, int32 Level = 0, int32 Round = 0, int32 Players = 0, int32 MaxPlayers = 0)
	{
		FTNPresenceState State;
		State.Mode = Mode;
		State.Level = Level;
		State.Round = Round;
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
		{ Make(ETNPresenceMode::Lobby, 0, 0, 3, 8), TEXT("En el lobby (3/8)"), TEXT("TN_LobbyCount"), 2 },
		{ Make(ETNPresenceMode::Lobby, 0, 0, 1, 0), TEXT("En el lobby"), TEXT("TN_Lobby"), 0 },
		{ Make(ETNPresenceMode::Coop, 2), TEXT("Coop, nivel 2"), TEXT("TN_CoopLevel"), 1 },
		{ Make(ETNPresenceMode::Coop, 0), TEXT("Coop"), TEXT("TN_Coop"), 0 },
		{ Make(ETNPresenceMode::Race, 0, 4), TEXT("Carrera, ronda 4"), TEXT("TN_RaceRound"), 1 },
		{ Make(ETNPresenceMode::Race, 7, 0), TEXT("Carrera"), TEXT("TN_Race"), 0 },
		{ Make(ETNPresenceMode::TwoVsTwo, 0, 3), TEXT("2 contra 2, ronda 3"), TEXT("TN_TwoVsTwoRound"), 1 },
		{ Make(ETNPresenceMode::Survival, 5), TEXT("Supervivencia, nivel 5"), TEXT("TN_SurvivalLevel"), 1 },
		{ Make(ETNPresenceMode::Survival, 0, 9), TEXT("Supervivencia"), TEXT("TN_Survival"), 0 },
		{ Make(ETNPresenceMode::Playing), TEXT("En partida"), TEXT("TN_Playing"), 0 },
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
	const FTNPresenceInfo Lobby = TNRichPresence::Build(Make(ETNPresenceMode::Lobby, 0, 0, 3, 8));
	TestEqual(TEXT("Lobby: players"), Param(Lobby, TEXT("players")), FString(TEXT("3")));
	TestEqual(TEXT("Lobby: max"), Param(Lobby, TEXT("max")), FString(TEXT("8")));
	TestEqual(TEXT("Coop: level"), Param(TNRichPresence::Build(Make(ETNPresenceMode::Coop, 12)), TEXT("level")), FString(TEXT("12")));
	TestEqual(TEXT("Carrera: round"), Param(TNRichPresence::Build(Make(ETNPresenceMode::Race, 0, 4)), TEXT("round")), FString(TEXT("4")));

	// Más tortugas que plazas (alguien entrando mientras se lee): nunca «9/8».
	const FTNPresenceInfo Over = TNRichPresence::Build(Make(ETNPresenceMode::Lobby, 0, 0, 9, 8));
	TestEqual(TEXT("La cuenta del lobby no pasa de las plazas"), Over.Status.BuildSourceString(), FString(TEXT("En el lobby (8/8)")));

	// La firma cambia con el número y no con nada más: es lo que evita mandar lo mismo dos veces.
	TestEqual(TEXT("Mismo estado, misma firma"), TNRichPresence::Build(Make(ETNPresenceMode::Coop, 2)).Signature(),
		TNRichPresence::Build(Make(ETNPresenceMode::Coop, 2)).Signature());
	TestNotEqual(TEXT("Otro nivel, otra firma"), TNRichPresence::Build(Make(ETNPresenceMode::Coop, 2)).Signature(),
		TNRichPresence::Build(Make(ETNPresenceMode::Coop, 3)).Signature());
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
	const ETNProcGameMode Coop = ETNProcGameMode::Coop;
	TestTrue(TEXT("Menú principal"), ModeFor(AMP_MenuGameMode::StaticClass(), Coop) == ETNPresenceMode::Menu);
	TestTrue(TEXT("Cuartel (lobby)"), ModeFor(ATN_HQGameMode::StaticClass(), Coop) == ETNPresenceMode::Lobby);
	TestTrue(TEXT("Supervivencia antes que su padre (Run)"), ModeFor(ATN_SurvivalGameMode::StaticClass(), Coop) == ETNPresenceMode::Survival);
	TestTrue(TEXT("Carrera de la playa antes que su padre (Run)"), ModeFor(ATN_BeachRaceGameMode::StaticClass(), Coop) == ETNPresenceMode::Race);
	TestTrue(TEXT("Run (Coop por chunks)"), ModeFor(ATN_RunGameMode::StaticClass(), Coop) == ETNPresenceMode::Coop);
	TestTrue(TEXT("Mapa procedural en Coop"), ModeFor(ATN_ProcMapGameMode::StaticClass(), ETNProcGameMode::Coop) == ETNPresenceMode::Coop);
	TestTrue(TEXT("Mapa procedural en Carrera"), ModeFor(ATN_ProcMapGameMode::StaticClass(), ETNProcGameMode::Race) == ETNPresenceMode::Race);
	TestTrue(TEXT("Mapa procedural en 2 contra 2"), ModeFor(ATN_ProcMapGameMode::StaticClass(), ETNProcGameMode::TwoVsTwo) == ETNPresenceMode::TwoVsTwo);
	TestTrue(TEXT("Mapa procedural en Supervivencia"), ModeFor(ATN_ProcMapGameMode::StaticClass(), ETNProcGameMode::Survival) == ETNPresenceMode::Survival);
	// El modo procedural solo cuenta en el mapa procedural.
	TestTrue(TEXT("El cuartel sigue siendo lobby aunque llegue un modo de carrera"), ModeFor(ATN_HQGameMode::StaticClass(), ETNProcGameMode::Race) == ETNPresenceMode::Lobby);
	TestTrue(TEXT("GameMode sin texto propio"), ModeFor(AGameModeBase::StaticClass(), Coop) == ETNPresenceMode::Playing);
	TestTrue(TEXT("Sin clase (aún no ha replicado)"), ModeFor(nullptr, Coop) == ETNPresenceMode::Playing);
	return true;
}

#endif
