// Reglas puras de entrada tardía y reconexión (TN_LateJoinRules.h, #345). Sin mundo ni actores. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Net.LateJoin+Tortunabo.Net.Reconnect; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Game/TN_LateJoinRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FTNJoinContext MakeJoin(ETNLateJoinPolicy Policy, bool bInProgress, bool bReactivated = false, bool bWasAlive = true, bool bHadFinished = false)
	{
		FTNJoinContext Context;
		Context.Policy = Policy;
		Context.bMatchInProgress = bInProgress;
		Context.bReactivated = bReactivated;
		Context.bWasAlive = bWasAlive;
		Context.bHadFinished = bHadFinished;
		return Context;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNLateJoinSpectateTest,
	"Tortunabo.Net.LateJoin",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNLateJoinSpectateTest::RunTest(const FString& Parameters)
{
	// Antes de empezar se entra a jugar como siempre.
	const FTNJoinDecision Staging = TNLateJoinLogic::DecideJoin(MakeJoin(ETNLateJoinPolicy::SpectateUntilMatchEnds, false));
	TestTrue(TEXT("En la espera juega"), Staging.Role == ETNJoinRole::PlayFromStart && !Staging.bSitsOut);

	// A mitad de nivel: espectador, fuera de la partida.
	const FTNJoinDecision Late = TNLateJoinLogic::DecideJoin(MakeJoin(ETNLateJoinPolicy::SpectateUntilMatchEnds, true));
	TestTrue(TEXT("Nuevo a mitad: espectador"), Late.Role == ETNJoinRole::Spectate);
	TestTrue(TEXT("Nuevo a mitad: no juega"), Late.bSitsOut);

	// Quien se fue y vuelve vivo tampoco recupera la partida.
	const FTNJoinDecision Back = TNLateJoinLogic::DecideJoin(MakeJoin(ETNLateJoinPolicy::SpectateUntilMatchEnds, true, true, true));
	TestTrue(TEXT("Vuelve vivo: espectador"), Back.Role == ETNJoinRole::Spectate && Back.bSitsOut);
	TestFalse(TEXT("Vuelve: conserva sus puntos"), Back.bResetRaceState);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNReconnectRulesTest,
	"Tortunabo.Net.Reconnect",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNReconnectRulesTest::RunTest(const FString& Parameters)
{
	// Clásico y Carrera: lo de siempre, también a mitad.
	const FTNJoinDecision Classic = TNLateJoinLogic::DecideJoin(MakeJoin(ETNLateJoinPolicy::FreshStart, true, true, false));
	TestTrue(TEXT("Clásico: juega desde la salida"), Classic.Role == ETNJoinRole::PlayFromStart);
	TestTrue(TEXT("Clásico: carrera a cero"), Classic.bResetRaceState && !Classic.bSitsOut);

	// Coop: nuevo a mitad → camino, de cero.
	const FTNJoinDecision CoopNew = TNLateJoinLogic::DecideJoin(MakeJoin(ETNLateJoinPolicy::ResumeOnPath, true));
	TestTrue(TEXT("Coop nuevo: en el camino"), CoopNew.Role == ETNJoinRole::PlayOnPath && CoopNew.bResetRaceState);

	// Coop: vuelve vivo → camino, con sus puntos.
	const FTNJoinDecision CoopAlive = TNLateJoinLogic::DecideJoin(MakeJoin(ETNLateJoinPolicy::ResumeOnPath, true, true, true));
	TestTrue(TEXT("Coop vuelve vivo: en el camino"), CoopAlive.Role == ETNJoinRole::PlayOnPath);
	TestFalse(TEXT("Coop vuelve vivo: conserva"), CoopAlive.bResetRaceState);

	// Coop: vuelve muerto o tras la meta → espectador, sin revivir.
	const FTNJoinDecision CoopDead = TNLateJoinLogic::DecideJoin(MakeJoin(ETNLateJoinPolicy::ResumeOnPath, true, true, false));
	TestTrue(TEXT("Coop vuelve muerto: espectador"), CoopDead.Role == ETNJoinRole::Spectate && !CoopDead.bResetRaceState);
	const FTNJoinDecision CoopDone = TNLateJoinLogic::DecideJoin(MakeJoin(ETNLateJoinPolicy::ResumeOnPath, true, true, true, true));
	TestTrue(TEXT("Coop vuelve tras la meta: espectador"), CoopDone.Role == ETNJoinRole::Spectate);

	// Ronda competitiva: mira hasta la siguiente.
	const FTNJoinDecision Round = TNLateJoinLogic::DecideJoin(MakeJoin(ETNLateJoinPolicy::SpectateUntilNextRound, true, true, true));
	TestTrue(TEXT("Ronda en curso: espectador"), Round.Role == ETNJoinRole::Spectate && Round.bSitsOut);

	// Derribado al irse = muerto al volver; vivo sigue vivo.
	FTNReconnectState Downed;
	Downed.bIsDBNO = true;
	const FTNReconnectState Saved = TNLateJoinLogic::SanitizeForReconnect(Downed);
	TestFalse(TEXT("DBNO al irse: vuelve muerto"), Saved.bIsAlive);
	TestFalse(TEXT("DBNO al irse: sin derribo"), Saved.bIsDBNO);
	TestTrue(TEXT("Vivo al irse: vuelve vivo"), TNLateJoinLogic::SanitizeForReconnect(FTNReconnectState()).bIsAlive);
	return true;
}

#endif
