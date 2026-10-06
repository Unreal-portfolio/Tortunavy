// Turbo en la predicción del movimiento (issue #22): lo que el servidor reconoce a un movimiento marcado con turbo
// (TNItemRuntime::ResolveClaimedBoost y BoostGraceSeconds, TN_ItemRuntime.h) y la velocidad y la aceleración que pone el
// turbo (TNMovementLimits::RaceBoostWalkSpeed y RaceBoostAcceleration), las mismas que usa UTN_TurtleMovementComponent.
// Correr desde Session Frontend (categoría "Tortunabo.ItemRuntime.BoostNet") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.ItemRuntime.BoostNet; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_MovementLimits.h"
#include "Game/TN_ItemRuntime.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRaceBoostClaimTest,
	"Tortunabo.ItemRuntime.BoostNet.Claim",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRaceBoostClaimTest::RunTest(const FString& Parameters)
{
	using namespace TNItemRuntime;
	const float Grace = BoostGraceSeconds(0.1f);

	// Con turbo ahora en el servidor, el suyo (aunque el dueño crea otro: manda el servidor).
	TestEqual(TEXT("Turbo activo: el del servidor"), ResolveClaimedBoost(2.f, 2.f, 0.0, Grace), 2.f);
	TestEqual(TEXT("Turbo y protector: el combinado"), ResolveClaimedBoost(2.4f, 1.25f, 0.0, Grace), 2.4f);

	// Recién acabado aquí: el dueño aún no lo ha visto acabar y sus movimientos siguen marcados; se le reconoce el que tenía.
	TestEqual(TEXT("Acabado hace 0,1 s: el de antes"), ResolveClaimedBoost(1.f, 2.f, 0.1, Grace), 2.f);
	TestEqual(TEXT("Justo en el margen: el de antes"), ResolveClaimedBoost(1.f, 2.f, static_cast<double>(Grace), Grace), 2.f);

	// Ni ahora ni hace poco: sin turbo (un cliente que lo pide sin tenerlo recibe la corrección).
	TestEqual(TEXT("Pasado el margen: sin turbo"), ResolveClaimedBoost(1.f, 2.f, static_cast<double>(Grace) + 0.01, Grace), 1.f);
	TestEqual(TEXT("Nunca lo ha tenido: sin turbo"), ResolveClaimedBoost(1.f, 1.f, -1.0, Grace), 1.f);
	TestEqual(TEXT("Sin hora apuntada: sin turbo"), ResolveClaimedBoost(1.f, 2.f, -1.0, Grace), 1.f);

	// El margen: ping de ida y vuelta más un cuarto de segundo, entre 0,3 y 1 s.
	TestEqual(TEXT("Sin ping: 0,3 s"), BoostGraceSeconds(0.f), 0.3f);
	TestEqual(TEXT("Ping negativo (sin medir): 0,3 s"), BoostGraceSeconds(-1.f), 0.3f);
	TestEqual(TEXT("100 ms: 0,35 s"), BoostGraceSeconds(0.1f), 0.35f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("300 ms: 0,55 s"), BoostGraceSeconds(0.3f), 0.55f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("2 s: tope de 1 s"), BoostGraceSeconds(2.f), 1.f);

	// Con ping alto el dueño acaba su turbo hasta un ping de ida y vuelta después que el servidor: siempre dentro del margen.
	for (const float RoundTrip : { 0.f, 0.05f, 0.15f, 0.3f, 0.6f })
	{
		TestTrue(FString::Printf(TEXT("Margen de sobra con %.0f ms"), RoundTrip * 1000.f), BoostGraceSeconds(RoundTrip) >= RoundTrip + 0.2f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRaceBoostSpeedTest,
	"Tortunabo.ItemRuntime.BoostNet.Speed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRaceBoostSpeedTest::RunTest(const FString& Parameters)
{
	using namespace TNMovementLimits;
	constexpr float Walk = 450.f;
	constexpr float Sprint = 800.f;

	// Sin turbo: la base con el tope (lo de MaxWalkSpeed).
	TestEqual(TEXT("Andando sin turbo"), RaceBoostWalkSpeed(Walk, Sprint, 1.f, NoCap), Walk);
	TestEqual(TEXT("Sin turbo con tope"), RaceBoostWalkSpeed(Walk, Sprint, 1.f, 300.f), 300.f);

	// Con turbo: al menos la de correr aunque ande, por el multiplicador.
	TestEqual(TEXT("Coco turbo andando: correr x2"), RaceBoostWalkSpeed(Walk, Sprint, 2.f, NoCap), 1600.f);
	TestEqual(TEXT("Coco turbo corriendo: correr x2"), RaceBoostWalkSpeed(Sprint, Sprint, 2.f, NoCap), 1600.f);
	TestEqual(TEXT("Protector: correr x1,25"), RaceBoostWalkSpeed(Walk, Sprint, 1.25f, NoCap), 1000.f);
	// La penalización de la energía sin fin (x0,75) no frena el turbo: manda la de correr.
	TestEqual(TEXT("Con la penalización: correr x2"), RaceBoostWalkSpeed(Walk * 0.75f, Sprint, 2.f, NoCap), 1600.f);
	// Los topes mandan también con turbo (caparazón 0, algas, zonas lentas...).
	TestEqual(TEXT("En el caparazón: quieta"), RaceBoostWalkSpeed(Walk, Sprint, 2.f, 0.f), 0.f);
	TestEqual(TEXT("Zona lenta: el tope"), RaceBoostWalkSpeed(Walk, Sprint, 2.f, 250.f), 250.f);

	// Aceleración: x(1 + 2·(m - 1)); sin turbo, la de siempre.
	TestEqual(TEXT("Aceleración sin turbo"), RaceBoostAcceleration(2048.f, 1.f), 2048.f);
	TestEqual(TEXT("Aceleración con x2: el triple"), RaceBoostAcceleration(2048.f, 2.f), 6144.f);
	TestEqual(TEXT("Aceleración con el protector"), RaceBoostAcceleration(2048.f, 1.25f), 3072.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
