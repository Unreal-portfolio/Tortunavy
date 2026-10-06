// Títulos de fin de partida (#798, TN_EndTitles.h): Saltarín para quien más salta, empate para quien entró antes en la sala
// y nadie si nadie ha saltado; y su texto en la pantalla de resultados (TN_ResultsTexts.h). Sin mundo: el mismo código que
// usan ATN_CoopGameState y UTN_CoopFlowHUDWidget. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Coop.Titles; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Core/TN_EndTitles.h"
#include "UI/HUD/TN_ResultsTexts.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNEndTitlesJumperTest,
	"Tortunabo.Coop.Titles.Jumper",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNEndTitlesJumperTest::RunTest(const FString& Parameters)
{
	using namespace TNEndTitles;
	TestEqual(TEXT("sin jugadoras: nadie"), PickTop({}), INDEX_NONE);
	TestEqual(TEXT("nadie ha saltado: nadie"), PickTop({ { 256, 0 }, { 257, 0 } }), INDEX_NONE);
	TestEqual(TEXT("se lo lleva quien más salta"), PickTop({ { 256, 12 }, { 257, 40 }, { 258, 7 } }), 1);
	TestEqual(TEXT("empate: la primera del orden"), PickTop({ { 256, 5 }, { 257, 30 }, { 258, 30 } }), 1);
	TestEqual(TEXT("una sola que ha saltado"), PickTop({ { 256, 0 }, { 257, 1 } }), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNEndTitlesTextTest,
	"Tortunabo.Coop.Titles.Text",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNEndTitlesTextTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("sin título: vacío"), TNResultsTexts::JumperTitle(FTN_EndTitle()).IsEmpty());
	FTN_EndTitle Title;
	Title.PlayerId = 257;
	Title.PlayerName = TEXT("Rubi");
	Title.Count = 42;
	TestTrue(TEXT("con título: lo dice"), Title.IsAwarded());
	const FString Text = TNResultsTexts::JumperTitle(Title).ToString();
	TestTrue(FString::Printf(TEXT("lleva el nombre y la cifra (%s)"), *Text), Text.Contains(TEXT("Rubi")) && Text.Contains(TEXT("42")));
	FTN_EndTitle Zero = Title;
	Zero.Count = 0;
	TestTrue(TEXT("con cero saltos no hay título"), TNResultsTexts::JumperTitle(Zero).IsEmpty());
	return true;
}

#endif
