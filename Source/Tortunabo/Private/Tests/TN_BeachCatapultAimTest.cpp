// Puntería de la catapulta de playa (#257): la bola de caparazón cae donde acaba el arco de conchitas aunque frene en el aire.
// Se testean las funciones de TN_BeachCatapultAim.h que usa ATN_BeachCatapult.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Beach.CatapultAim; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Player/TN_ShellBody.h"
#include "World/Beach/TN_BeachCatapultAim.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachCatapultAimTest,
	"Tortunabo.Beach.CatapultAim",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachCatapultAimTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachCatapultAim;
	constexpr double G = 980.0;
	const double Pitch = FMath::DegreesToRadians(44.0);

	// La puntería frena la bola como la frena su caja de verdad: si alguien cambia una, tiene que cambiar la otra.
	const ATN_ShellBody* ShellBodyDefaults = GetDefault<ATN_ShellBody>();
	const UBoxComponent* ShellBox = ShellBodyDefaults ? ShellBodyDefaults->GetBox() : nullptr;
	if (TestNotNull(TEXT("La caja de la bola de caparazón"), ShellBox))
	{
		TestEqual(TEXT("El frenado de la puntería es el de la caja de la bola"), ShellBox->BodyInstance.LinearDamping,
			ShellBallLinearDamping);
	}

	// Sin rozamiento y al mismo nivel: el alcance de siempre, v²·sin(2θ)/g.
	const double Ideal = 2300.0 * 2300.0 * FMath::Sin(2.0 * Pitch) / G;
	TestEqual(TEXT("Sin rozamiento: alcance de un tiro parabólico"), LandingDistance(2300.0, Pitch, 0.0, 0.0, G), Ideal, Ideal * 0.01);

	// Con el frenado de la bola llega bastante menos lejos: por eso se quedaba corta del arco.
	const double Damped = LandingDistance(2300.0, Pitch, 0.0, ShellBallLinearDamping, G);
	TestTrue(TEXT("Con frenado, menos de lo que dibuja un tiro sin rozamiento"), Damped > 0.0 && Damped < 0.8 * Ideal);

	// La rapidez que se busca cae donde se pide, también más abajo que la salida (el arco baja desde lo alto de la catapulta).
	for (const double Height : { 0.0, -400.0, 200.0 })
	{
		const double Speed = SpeedToReach(3600.0, Height, Pitch, ShellBallLinearDamping, G, 500.0, 8000.0);
		TestEqual(FString::Printf(TEXT("Cae a 36 m con %.0f cm de desnivel"), Height),
			LandingDistance(Speed, Pitch, Height, ShellBallLinearDamping, G), 3600.0, 15.0);
	}

	// Inalcanzable con el tope: el tope.
	TestEqual(TEXT("Demasiado lejos: el tope de rapidez"), SpeedToReach(1e6, 0.0, Pitch, ShellBallLinearDamping, G, 500.0, 4000.0), 4000.0);

	// Tan flojo que no sube hasta la altura pedida: no cae nunca allí.
	TestTrue(TEXT("Sin llegar a la altura: -1"), LandingDistance(300.0, Pitch, 2000.0, ShellBallLinearDamping, G) < 0.0);
	return true;
}

#endif
