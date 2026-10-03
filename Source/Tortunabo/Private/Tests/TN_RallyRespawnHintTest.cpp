// Aviso «Mantén R para volver a la pista» del HUD del Rally (#303, TNRallyRespawnHint). Sin mundo. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.RespawnHint; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Rally/TN_RallyRespawnHint.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRespawnHintTest
{
	constexpr float Step = 0.1f;

	/** Parado fuera de la calzada, en carrera y sin reaparecer. */
	TNRallyRespawnHint::FInput Stuck()
	{
		TNRallyRespawnHint::FInput Input;
		Input.bRacing = true;
		Input.SpeedCms = 20.f;
		Input.DistanceToAxisCm = 1500.f;
		return Input;
	}

	/** Llama a Update durante Seconds en pasos de Step; devuelve si el aviso se veía al final. */
	bool Run(TNRallyRespawnHint::FState& State, const TNRallyRespawnHint::FInput& Input, float Seconds)
	{
		bool bShown = false;
		const int32 Steps = FMath::Max(1, FMath::RoundToInt(Seconds / Step));
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			bShown = TNRallyRespawnHint::Update(State, Input, Step);
		}
		return bShown;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyRespawnHintShowsTest,
	"Tortunabo.Rally.RespawnHint.Shows",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyRespawnHintShowsTest::RunTest(const FString& Parameters)
{
	using namespace TNRespawnHintTest;
	using namespace TNRallyRespawnHint;
	FState State;
	TestFalse(TEXT("recién atascado todavía no"), Run(State, Stuck(), 1.f));
	TestTrue(TEXT("parado fuera de la calzada unos segundos: aviso"), Run(State, Stuck(), StuckHintSeconds));

	FInput Flipped = Stuck();
	Flipped.bFlipped = true;
	Flipped.DistanceToAxisCm = 0.f;
	Flipped.SpeedCms = 400.f;
	State = FState();
	TestFalse(TEXT("recién volcado todavía no"), Run(State, Flipped, 1.f));
	TestTrue(TEXT("volcado en la pista unos segundos: aviso"), Run(State, Flipped, 1.f));

	// Se mueve otra vez: fuera el aviso y la cuenta vuelve a empezar.
	FInput Moving = Stuck();
	Moving.SpeedCms = 800.f;
	TestFalse(TEXT("arranca: sin aviso"), Run(State, Moving, Step));
	TestFalse(TEXT("y vuelve a esperar entera"), Run(State, Stuck(), 1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyRespawnHintQuietTest,
	"Tortunabo.Rally.RespawnHint.QuietDuringRespawn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyRespawnHintQuietTest::RunTest(const FString& Parameters)
{
	using namespace TNRespawnHintTest;
	using namespace TNRallyRespawnHint;
	FState State;
	TestTrue(TEXT("atascado: aviso"), Run(State, Stuck(), StuckHintSeconds + 0.5f));

	// La reaparición automática empieza: el aviso se va en ese mismo instante.
	FInput Respawning = Stuck();
	Respawning.bRespawning = true;
	Respawning.SecondsSinceRespawn = 0.f;
	TestFalse(TEXT("durante la reaparición automática no sale"), Update(State, Respawning, Step));
	TestFalse(TEXT("ni aunque dure (la espera entera)"), Run(State, Respawning, 3.f));

	// Justo después (sigue parado, aún en la zona): no vuelve a salir.
	FInput JustAfter = Stuck();
	JustAfter.SecondsSinceRespawn = 1.f;
	TestFalse(TEXT("justo después de reaparecer no sale aunque siga parado"), Run(State, JustAfter, 3.f));

	// Pasado el silencio, si se vuelve a atascar, espera de nuevo los segundos de atasco.
	FInput Later = Stuck();
	Later.SecondsSinceRespawn = QuietAfterRespawnSeconds + 1.f;
	TestFalse(TEXT("pasado el silencio no sale de golpe"), Update(State, Later, Step));
	TestTrue(TEXT("y vuelve a salir tras la espera"), Run(State, Later, StuckHintSeconds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyRespawnHintNotStrandedTest,
	"Tortunabo.Rally.RespawnHint.NotStranded",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyRespawnHintNotStrandedTest::RunTest(const FString& Parameters)
{
	using namespace TNRespawnHintTest;
	using namespace TNRallyRespawnHint;
	FState State;
	FInput OnRoad = Stuck();
	OnRoad.DistanceToAxisCm = 300.f;
	TestFalse(TEXT("parado en la calzada (esperando, en un choque): no"), Run(State, OnRoad, 10.f));

	FInput Fast = Stuck();
	Fast.SpeedCms = 1200.f;
	TestFalse(TEXT("fuera de la calzada pero corriendo: no"), Run(State, Fast, 10.f));

	FInput NoTrack = Stuck();
	NoTrack.DistanceToAxisCm = -1.f;
	TestFalse(TEXT("sin pista conocida, parado: no"), Run(State, NoTrack, 10.f));

	FInput NotRacing = Stuck();
	NotRacing.bRacing = false;
	TestFalse(TEXT("fuera de carrera (parrilla, meta, resultados): no"), Run(State, NotRacing, 10.f));

	FInput Broken = Stuck();
	Broken.SpeedCms = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("velocidad no finita: no"), Run(State, Broken, 10.f));
	State = FState();
	TestFalse(TEXT("un paso no finito no lo enseña"), Update(State, Stuck(), std::numeric_limits<float>::infinity()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
