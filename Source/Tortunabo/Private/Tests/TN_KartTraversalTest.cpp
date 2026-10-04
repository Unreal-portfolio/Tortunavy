// Géiseres, cascadas y agua del kart (#293): parábola del géiser, flotación y plegado de las ruedas. Lógica pura.
// Correr desde Session Frontend (categoría "Tortunabo.Kart.Traversal") o headless con UnrealEditor-Win64-DebugGame-Cmd
// <uproject> -ExecCmds="Automation RunTests Tortunabo.Kart; Quit".

#include "Misc/AutomationTest.h"
#include "Kart/TN_KartTraversalComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartGeyserTest, "Tortunabo.Kart.Traversal.GeyserArc",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartGeyserTest::RunTest(const FString& Parameters)
{
	constexpr float Gravity = 980.f;
	const FVector Start(0.0, 0.0, 100.0);
	const FVector Target(1200.0, 300.0, 900.0);
	const FVector V = TNKart::GeyserLaunchVelocity(Start, Target, 450.f, Gravity);
	TestTrue(TEXT("Sale hacia arriba"), V.Z > 0.0);
	// Simulación sencilla de la parábola: pasa por el ápice y cae en el destino.
	const double TimeUp = V.Z / Gravity;
	const double Apex = Start.Z + V.Z * TimeUp - 0.5 * Gravity * TimeUp * TimeUp;
	TestEqual(TEXT("Ápice 4,5 m por encima de la cima"), Apex, Target.Z + 450.0, 1.0);
	const double TimeDown = FMath::Sqrt(2.0 * (Apex - Target.Z) / Gravity);
	const FVector Landing = Start + FVector(V.X, V.Y, 0.0) * (TimeUp + TimeDown);
	TestEqual(TEXT("Cae en el destino (X)"), Landing.X, Target.X, 1.0);
	TestEqual(TEXT("Cae en el destino (Y)"), Landing.Y, Target.Y, 1.0);
	TestEqual(TEXT("El vuelo guiado dura lo mismo que la parábola"), static_cast<double>(TNKart::GeyserFlightSeconds(Start, Target, 450.f, Gravity)),
		TimeUp + TimeDown, 0.01);
	// Bajar también funciona (géiser a un nivel más bajo): el ápice cuenta desde el punto más alto.
	const FVector Down = TNKart::GeyserLaunchVelocity(FVector(0.0, 0.0, 500.0), FVector(800.0, 0.0, 0.0), 300.f, Gravity);
	TestTrue(TEXT("Hacia un sitio más bajo, también sube primero"), Down.Z > 0.0 && Down.X > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartFloatTest, "Tortunabo.Kart.Traversal.FloatAndFold",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartFloatTest::RunTest(const FString& Parameters)
{
	constexpr float Gravity = 980.f;
	TestEqual(TEXT("A ras de agua y quieto, sostiene justo su peso"), TNKart::BuoyancyAccel(0.f, 0.f, Gravity), Gravity, 0.01f);
	TestTrue(TEXT("Hundido, empuja hacia arriba más que la gravedad"), TNKart::BuoyancyAccel(30.f, 0.f, Gravity) > Gravity);
	TestTrue(TEXT("Muy por encima del agua, no empuja"), TNKart::BuoyancyAccel(-60.f, 0.f, Gravity) <= 0.01f);
	TestTrue(TEXT("Subiendo deprisa, el agua frena el rebote"), TNKart::BuoyancyAccel(0.f, 300.f, Gravity) < Gravity);
	TestTrue(TEXT("Hundiéndose, el agua empuja más"), TNKart::BuoyancyAccel(0.f, -300.f, Gravity) > Gravity);
	// Equilibrio: con la flotación y la gravedad, la línea de flotación se queda a ras de agua.
	float Z = 50.f;
	float Vz = 0.f;
	for (int32 Step = 0; Step < 600; ++Step)
	{
		const float Dt = 1.f / 60.f;
		Vz += (TNKart::BuoyancyAccel(-Z, Vz, Gravity) - Gravity) * Dt;
		Z += Vz * Dt;
	}
	TestEqual(TEXT("Flota a ras de agua tras 10 s"), Z, 0.f, 2.f);

	TestEqual(TEXT("Las ruedas se pliegan poco a poco"), TNKart::AdvanceFold(0.f, true, 3.f, 0.1f), 0.3f, 0.001f);
	TestEqual(TEXT("Sin pasarse de plegadas"), TNKart::AdvanceFold(0.9f, true, 3.f, 0.1f), 1.f);
	TestEqual(TEXT("Y se despliegan al salir"), TNKart::AdvanceFold(0.2f, false, 3.f, 0.1f), 0.f);
	return true;
}

#endif
