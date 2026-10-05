// Salida del proyectil de la torreta (#717, «va donde quiere y no donde apunta»): la velocidad de salida apunta hacia donde se
// dispara aunque el buggy corra, y el disparo de la artillera sale hacia lo que cubre la mira. Lógica pura
// (TNRallyTurret::ShotVelocity y AimedShotDirection), sin mundo. Correr desde Session Frontend (categoría
// "Tortunabo.Rally.Turret.ShotAim") o headless con UnrealEditor-Win64-DebugGame-Cmd <uproject>
// -ExecCmds="Automation RunTests Tortunabo.Rally.Turret.ShotAim; Quit".

#include "Misc/AutomationTest.h"
#include "Vehicles/TN_RallyTurretLogic.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyShotVelocityTest, "Tortunabo.Rally.Turret.ShotAim.Velocity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyShotVelocityTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	const FVector Buggy(3000.0, 0.0, 0.0);
	constexpr float Coco = 6000.f;

	// Lateral: antes salía 26° hacia delante (atan(3000/6000)); ahora la velocidad resultante va hacia donde se dispara.
	const FVector Right(0.0, 1.0, 0.0);
	const FVector Side = ShotVelocity(Right, Buggy, Coco);
	TestTrue(TEXT("lateral: la velocidad resultante apunta a la derecha"), Side.GetSafeNormal().Equals(Right, 1e-4f));
	TestEqual(TEXT("lateral: respecto del buggy sale a la rapidez del coco"), static_cast<float>((Side - Buggy).Size()), Coco, 0.5f);
	const FVector Old = Right * Coco + Buggy;
	TestTrue(TEXT("lateral: la suma de antes se iba más de 20° de la mira"), FVector::DotProduct(Old.GetSafeNormal(), Right) < FMath::Cos(FMath::DegreesToRadians(20.0)));

	// En diagonal hacia delante-izquierda y hacia atrás-derecha: siempre hacia la mira.
	for (const FVector& Dir : { FVector(1.0, -1.0, 0.0).GetSafeNormal(), FVector(-1.0, 1.0, 0.3).GetSafeNormal(), FVector(0.2, 1.0, 0.5).GetSafeNormal() })
	{
		const FVector V = ShotVelocity(Dir, Buggy, Coco);
		TestTrue(TEXT("diagonal: hacia la mira"), V.GetSafeNormal().Equals(Dir, 1e-4f));
		TestEqual(TEXT("diagonal: rapidez relativa del coco"), static_cast<float>((V - Buggy).Size()), Coco, 0.5f);
	}

	// Hacia delante y hacia atrás sale lo de siempre.
	TestTrue(TEXT("hacia delante: la suma"), ShotVelocity(FVector::ForwardVector, Buggy, Coco).Equals(FVector(9000.0, 0.0, 0.0), 0.5f));
	TestTrue(TEXT("hacia atrás: la suma"), ShotVelocity(-FVector::ForwardVector, Buggy, Coco).Equals(FVector(-3000.0, 0.0, 0.0), 0.5f));

	// Parado o sin velocidad heredada (la burbuja): Dir por la rapidez.
	TestTrue(TEXT("buggy parado"), ShotVelocity(Right, FVector::ZeroVector, Coco).Equals(Right * Coco, 0.01f));

	// El buggy corre más que el proyectil: lateral no tiene solución y sale la suma de siempre.
	const FVector Slow = ShotVelocity(Right, Buggy, 2800.f);
	TestTrue(TEXT("proyectil más lento que el buggy: la suma"), Slow.Equals(Right * 2800.f + Buggy, 0.5f));
	// Sigue habiendo solución hacia delante.
	TestTrue(TEXT("hacia delante con proyectil lento"), ShotVelocity(FVector::ForwardVector, Buggy, 2800.f).Equals(FVector(5800.0, 0.0, 0.0), 0.5f));

	// Dirección nula o rapidez 0: no revienta.
	TestTrue(TEXT("sin dirección, la velocidad del buggy"), ShotVelocity(FVector::ZeroVector, Buggy, Coco).Equals(Buggy, 0.01f));
	TestTrue(TEXT("sin rapidez, la velocidad del buggy"), ShotVelocity(Right, Buggy, 0.f).Equals(Buggy, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyAimedShotTest, "Tortunabo.Rally.Turret.ShotAim.AimedDirection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyAimedShotTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	const FVector Forward = FVector::ForwardVector;
	const FVector Muzzle(0.0, 0.0, 0.0);
	const auto PitchDeg = [](const FVector& Dir)
	{
		return FMath::RadiansToDegrees(FMath::Asin(Dir.Z));
	};

	// Un punto justo delante, sin gravedad ni buggy: hacia él.
	TestTrue(TEXT("punto de frente"), AimedShotDirection(Muzzle, FVector(4000.0, 0.0, 0.0), Forward, FVector::ZeroVector, 6000.f, 0.f).Equals(Forward, 1e-4f));
	TestTrue(TEXT("sale unitaria"), FMath::IsNearlyEqual(AimedShotDirection(Muzzle, FVector(4000.0, 900.0, 300.0), Forward, FVector::ZeroVector, 6000.f, 0.f).Size(), 1.0, 1e-4));

	// La boca está por encima de lo que cubre la mira: dispara hacia abajo, hacia el punto (antes salía recto).
	const FVector High(0.0, 0.0, 200.0);
	const FVector Down = AimedShotDirection(High, FVector(4000.0, 0.0, 0.0), Forward, FVector::ZeroVector, 6000.f, 0.f);
	TestEqual(TEXT("desde 2 m de altura a 40 m: -2,86°"), static_cast<float>(PitchDeg(Down)), -2.86f, 0.02f);

	// La caída del proyectil: a 40 m con el coco (6000 cm/s, 0,3 g) cae 65 cm; el tiro sube ese ángulo.
	const FVector WithGravity = AimedShotDirection(Muzzle, FVector(4000.0, 0.0, 0.0), Forward, FVector::ZeroVector, 6000.f, 980.f * 0.3f);
	TestEqual(TEXT("compensa la caída del coco"), static_cast<float>(PitchDeg(WithGravity)), 0.93f, 0.03f);
	// La velocidad del buggy hacia delante acorta el vuelo: sube menos.
	const FVector Moving = AimedShotDirection(Muzzle, FVector(4000.0, 0.0, 0.0), Forward, FVector(3000.0, 0.0, 0.0), 6000.f, 980.f * 0.3f);
	TestTrue(TEXT("con el buggy corriendo, el vuelo es más corto y sube menos"), PitchDeg(Moving) < PitchDeg(WithGravity) && PitchDeg(Moving) > 0.0);
	// Un mortero (gravedad entera, 2800 cm/s) a 30 m sube mucho; con tope.
	const FVector Mortar = AimedShotDirection(Muzzle, FVector(3000.0, 0.0, 0.0), Forward, FVector::ZeroVector, 2800.f, 980.f);
	TestTrue(TEXT("el mortero se arquea hacia el punto"), PitchDeg(Mortar) > 10.f);
	const FVector Capped = AimedShotDirection(Muzzle, FVector(8000.0, 0.0, 0.0), Forward, FVector::ZeroVector, 900.f, 980.f);
	TestEqual(TEXT("la compensación tiene tope"), static_cast<float>(PitchDeg(Capped)), MaxDropCompensationDeg, 0.1f);

	// Puntos que no valen: pegados a la boca, detrás o muy fuera de la mira. Sale la dirección de la cámara.
	TestTrue(TEXT("pegado a la boca: la cámara"), AimedShotDirection(Muzzle, FVector(100.0, 0.0, 0.0), Forward, FVector::ZeroVector, 6000.f, 0.f).Equals(Forward, 1e-4f));
	TestTrue(TEXT("detrás de la boca: la cámara"), AimedShotDirection(Muzzle, FVector(-3000.0, 0.0, 0.0), Forward, FVector::ZeroVector, 6000.f, 0.f).Equals(Forward, 1e-4f));
	TestTrue(TEXT("a 90° de la mira: la cámara"), AimedShotDirection(Muzzle, FVector(0.0, 3000.0, 0.0), Forward, FVector::ZeroVector, 6000.f, 0.f).Equals(Forward, 1e-4f));
	const FVector Slightly = AimedShotDirection(Muzzle, FVector(3000.0, 1000.0, 0.0), Forward, FVector::ZeroVector, 6000.f, 0.f);
	TestTrue(TEXT("a 18° de la mira: hacia el punto"), Slightly.Equals(FVector(3000.0, 1000.0, 0.0).GetSafeNormal(), 1e-4f));
	TestTrue(TEXT("punto no finito: la cámara"), AimedShotDirection(Muzzle, FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0), Forward, FVector::ZeroVector, 6000.f, 0.f).Equals(Forward, 1e-4f));

	// El margen del servidor cubre la cámara de hombro (8° por debajo del cañón) más lo que sube la caída.
	TestTrue(TEXT("el margen cubre la cámara de hombro y la caída"), MaxCameraAimErrorDeg >= 8.f + 12.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
