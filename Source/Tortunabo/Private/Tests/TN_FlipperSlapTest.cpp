// Guantazo con la aleta (#832, #709, Player/TN_FlipperSlapRules.h), sin mundo ni actores: alcance y cono, a quién da cuando hay
// varias, el empujoncito, la espera entre golpes y las fases del movimiento.
// Correr desde Session Frontend (categoría "Tortunabo.FlipperSlap") o sin ventana:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.FlipperSlap; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_FlipperSlapRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNFlipperSlapArcTest,
	"Tortunabo.FlipperSlap.Arc",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNFlipperSlapArcTest::RunTest(const FString& Parameters)
{
	using namespace TNFlipperSlap;
	const FVector Origin(0.0, 0.0, 100.0);
	const FVector Forward(1.0, 0.0, 0.0);
	float Score = 0.f;

	TestTrue(TEXT("Justo delante y a tiro"), InArc(Origin, Forward, FVector(150.0, 0.0, 100.0), Score));
	TestEqual(TEXT("Centrada: la puntuación es la distancia"), Score, 150.f, 0.01f);
	TestFalse(TEXT("Más allá del alcance"), InArc(Origin, Forward, FVector(Reach + 20.0, 0.0, 100.0), Score));
	TestFalse(TEXT("Detrás, lejos de la pegada"), InArc(Origin, Forward, FVector(-150.0, 0.0, 100.0), Score));
	TestTrue(TEXT("Detrás pero pegada: se da igual"), InArc(Origin, Forward, FVector(-CloseRange * 0.5, 0.0, 100.0), Score));
	TestFalse(TEXT("De lado, fuera del cono"), InArc(Origin, Forward, FVector(10.0, 150.0, 100.0), Score));
	TestTrue(TEXT("En diagonal, dentro del cono"), InArc(Origin, Forward, FVector(100.0, 100.0, 100.0), Score));
	TestFalse(TEXT("Mucho más arriba"), InArc(Origin, Forward, FVector(100.0, 0.0, 100.0 + MaxHeightDifference + 10.0), Score));
	TestTrue(TEXT("Algo más arriba, vale"), InArc(Origin, Forward, FVector(100.0, 0.0, 100.0 + MaxHeightDifference - 10.0), Score));

	// La vista inclinada no cambia el cono: solo cuenta la horizontal.
	TestTrue(TEXT("Mirando algo hacia arriba, el cono sigue siendo horizontal"),
		InArc(Origin, FVector(1.0, 0.0, 0.7), FVector(150.0, 0.0, 100.0), Score));
	TestTrue(TEXT("Sin horizontal en la vista, mira hacia delante de serie"),
		InArc(Origin, FVector(0.0, 0.0, 1.0), FVector(150.0, 0.0, 100.0), Score));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNFlipperSlapPickTest,
	"Tortunabo.FlipperSlap.Pick",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNFlipperSlapPickTest::RunTest(const FString& Parameters)
{
	using namespace TNFlipperSlap;
	const FVector Origin(0.0, 0.0, 0.0);
	const FVector Forward(0.0, 1.0, 0.0);

	TestEqual(TEXT("Sin nadie, nadie"), PickTarget(Origin, Forward, TArray<FVector>()), static_cast<int32>(INDEX_NONE));

	TArray<FVector> Victims;
	Victims.Add(FVector(0.0, 190.0, 0.0));
	Victims.Add(FVector(0.0, 90.0, 0.0));
	Victims.Add(FVector(0.0, -150.0, 0.0));
	TestEqual(TEXT("La más cercana de las que están delante"), PickTarget(Origin, Forward, Victims), 1);

	TArray<FVector> OnlyBehind;
	OnlyBehind.Add(FVector(0.0, -150.0, 0.0));
	TestEqual(TEXT("Solo hay una detrás: nadie"), PickTarget(Origin, Forward, OnlyBehind), static_cast<int32>(INDEX_NONE));

	// A igual distancia, la mejor centrada.
	TArray<FVector> Tie;
	Tie.Add(FVector(120.0 * FMath::Sin(FMath::DegreesToRadians(40.0)), 120.0 * FMath::Cos(FMath::DegreesToRadians(40.0)), 0.0));
	Tie.Add(FVector(0.0, 120.0, 0.0));
	TestEqual(TEXT("A igual distancia, la que está más de frente"), PickTarget(Origin, Forward, Tie), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNFlipperSlapPushTest,
	"Tortunabo.FlipperSlap.Push",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNFlipperSlapPushTest::RunTest(const FString& Parameters)
{
	using namespace TNFlipperSlap;
	const FVector Origin(0.0, 0.0, 0.0);
	const FVector Forward(1.0, 0.0, 0.0);

	const FVector Ahead = PushVelocity(Origin, Forward, FVector(100.0, 0.0, 0.0));
	TestEqual(TEXT("Delante: la empuja hacia delante"), static_cast<float>(Ahead.X), PushSpeed, 0.5f);
	TestEqual(TEXT("...sin desviarla"), static_cast<float>(Ahead.Y), 0.f, 0.5f);
	TestEqual(TEXT("...y con un saltito"), static_cast<float>(Ahead.Z), PushUp, 0.01f);
	TestEqual(TEXT("La velocidad horizontal es la del empujoncito"), static_cast<float>(FVector(Ahead.X, Ahead.Y, 0.0).Size()), PushSpeed, 0.5f);

	const FVector Side = PushVelocity(Origin, Forward, FVector(100.0, 100.0, 0.0));
	TestTrue(TEXT("En diagonal, la empuja hacia su lado"), Side.Y > 0.f && Side.X > 0.f);
	TestEqual(TEXT("...con la misma fuerza"), static_cast<float>(FVector(Side.X, Side.Y, 0.0).Size()), PushSpeed, 0.5f);

	const FVector Behind = PushVelocity(Origin, Forward, FVector(-30.0, 0.0, 0.0));
	TestTrue(TEXT("Pegada y detrás: la empuja hacia atrás, no se queda parada"), Behind.X < 0.f && FMath::Abs(Behind.X) > 1.f);

	TestEqual(TEXT("Justo encima: empuja hacia donde mira"), static_cast<float>(PushVelocity(Origin, Forward, Origin).X), PushSpeed, 0.5f);
	TestTrue(TEXT("Es un empujoncito, no un lanzamiento (el de la pala es de 1350)"), PushSpeed < 700.f && PushUp < 250.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNFlipperSlapTimingTest,
	"Tortunabo.FlipperSlap.Timing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNFlipperSlapTimingTest::RunTest(const FString& Parameters)
{
	using namespace TNFlipperSlap;
	TestTrue(TEXT("El golpe es muy rápido"), SwingSeconds <= 0.3f);
	TestTrue(TEXT("Hay una pequeña espera entre golpes"), CooldownSeconds > SwingSeconds && CooldownSeconds <= 0.8f);
	TestEqual(TEXT("Mareo de medio segundo"), DizzySeconds, 0.5f);

	TestTrue(TEXT("Recién empezado, no se puede otro"), !IsCooledDown(10.0, 10.0));
	TestTrue(TEXT("Pasada la espera, sí"), IsCooledDown(10.0 + CooldownSeconds, 10.0));
	TestTrue(TEXT("El servidor acepta el golpe con algo menos de espera"), IsCooledDown(10.0 + CooldownSeconds * 0.8, 10.0, ServerCooldownTolerance));
	TestFalse(TEXT("...pero no sin esperar nada"), IsCooledDown(10.0 + CooldownSeconds * 0.2, 10.0, ServerCooldownTolerance));
	TestTrue(TEXT("El primero de la partida siempre se puede"), IsCooledDown(3.0, -100.0));

	TestEqual(TEXT("Antes de empezar, sin fase"), SwingPhase(9.9, 10.0), -1.f);
	TestEqual(TEXT("Al empezar, fase 0"), SwingPhase(10.0, 10.0), 0.f, 1e-4f);
	TestEqual(TEXT("A mitad, fase 0,5"), SwingPhase(10.0 + SwingSeconds * 0.5, 10.0), 0.5f, 1e-4f);
	TestEqual(TEXT("Acabado, sin fase"), SwingPhase(10.0 + SwingSeconds, 10.0), -1.f);

	TestEqual(TEXT("El impacto, a su fase del golpe"), ImpactDelay(10.0, 10.0), StrikePhase * SwingSeconds, 1e-4f);
	TestEqual(TEXT("Pasado el impacto, ya"), ImpactDelay(10.0 + SwingSeconds, 10.0), 0.f);
	TestTrue(TEXT("Un aviso que llega a tiempo espera lo que falta"), ImpactDelay(10.05, 10.0) < ImpactDelay(10.0, 10.0));

	TestEqual(TEXT("Sin golpe, la aleta no manda"), SwingWeight(-1.f), 0.f);
	TestEqual(TEXT("Al empezar, aún no manda"), SwingWeight(0.f), 0.f, 1e-4f);
	TestEqual(TEXT("En el impacto, manda entera"), SwingWeight(StrikePhase), 1.f, 1e-4f);
	TestEqual(TEXT("Acabado, suelta"), SwingWeight(1.f), 0.f);
	TestTrue(TEXT("Entra deprisa y suelta despacio"), SwingWeight(0.1f) > 0.5f && SwingWeight(0.9f) < 0.5f);
	return true;
}

#endif
