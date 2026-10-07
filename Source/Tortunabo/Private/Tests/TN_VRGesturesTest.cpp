// Gestos físicos con gafas (#918, VR/TN_VRGestures.h): el guantazo con la mano de lado a lado y agachar la cabeza para
// meterse en el caparazón, sin mundo ni actores:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.VR.Gestures; Quit" -nullrhi -nosound -unattended

#include "Misc/AutomationTest.h"
#include "VR/TN_VRGestures.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNVRGesturesTest
{
	constexpr float Dt = 1.f / 90.f;
	/** Cuerpo mirando a +X: su derecha es +Y. */
	const FVector Right(0.0, 1.0, 0.0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRGesturesTest,
	"Tortunabo.VR.Gestures",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRGesturesTest::RunTest(const FString& Parameters)
{
	using namespace TNVRGesturesTest;

	// ── Guantazo ──
	TestTrue(TEXT("De lado y deprisa a la derecha: guantazo"), TNVRGestures::IsSlapSwipe(FVector(0.0, 600.0, 0.0), Right));
	TestTrue(TEXT("De lado y deprisa a la izquierda: guantazo"), TNVRGestures::IsSlapSwipe(FVector(50.0, -600.0, 30.0), Right));
	TestFalse(TEXT("De lado pero despacio: no"), TNVRGestures::IsSlapSwipe(FVector(0.0, 300.0, 0.0), Right));
	TestFalse(TEXT("Deprisa hacia delante (un puñetazo o un lanzamiento): no"), TNVRGestures::IsSlapSwipe(FVector(900.0, 200.0, 0.0), Right));
	TestFalse(TEXT("Deprisa hacia arriba: no"), TNVRGestures::IsSlapSwipe(FVector(0.0, 100.0, 900.0), Right));
	TestFalse(TEXT("Quieta: no"), TNVRGestures::IsSlapSwipe(FVector::ZeroVector, Right));
	TestTrue(TEXT("La velocidad lateral es la parte hacia la derecha"), FMath::IsNearlyEqual(TNVRGestures::LateralSpeed(FVector(10.0, 450.0, 5.0), Right), 450.f, 0.01f));
	TestTrue(TEXT("El eje del cuerpo no hace falta unitario"), TNVRGestures::IsSlapSwipe(FVector(0.0, 600.0, 0.0), Right * 7.0));

	TNVRGestures::FSlapGesture Slap;
	TestFalse(TEXT("Sin movimiento no hay guantazo"), Slap.Step(FVector::ZeroVector, Right, Dt));
	TestTrue(TEXT("El gesto dispara"), Slap.Step(FVector(0.0, 700.0, 0.0), Right, Dt));
	TestFalse(TEXT("Mientras sigue el gesto no repite (enfriamiento)"), Slap.Step(FVector(0.0, 700.0, 0.0), Right, Dt));
	float Elapsed = 0.f;
	bool bAgain = false;
	while (Elapsed < TNVRGestures::SlapCooldownSeconds * 2.f && !bAgain)
	{
		bAgain = Slap.Step(FVector(0.0, -700.0, 0.0), Right, Dt);
		Elapsed += Dt;
	}
	TestTrue(TEXT("Pasado el enfriamiento vuelve a disparar"), bAgain);
	TestTrue(FString::Printf(TEXT("No antes de %.1f s (fue a los %.2f s)"), TNVRGestures::SlapCooldownSeconds, Elapsed), Elapsed >= TNVRGestures::SlapCooldownSeconds - 2.f * Dt);

	// ── Agacharse ──
	TNVRGestures::FDuckGesture Duck;
	const float Stand = 160.f;
	bool bFired = false;
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		bFired |= Duck.Step(Stand, Dt);
	}
	TestFalse(TEXT("De pie y quieta no se agacha"), bFired);
	// Un agachón de 35 cm en medio segundo.
	int32 Fires = 0;
	for (int32 Frame = 1; Frame <= 45; ++Frame)
	{
		Fires += Duck.Step(Stand - 35.f * Frame / 45.f, Dt) ? 1 : 0;
	}
	TestEqual(TEXT("Agachar la cabeza 35 cm dispara una vez"), Fires, 1);
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		Fires += Duck.Step(Stand - 35.f, Dt) ? 1 : 0;
	}
	TestEqual(TEXT("Seguir agachada no repite"), Fires, 1);
	// Se levanta y se vuelve a agachar pasado el enfriamiento: otra vez.
	for (int32 Frame = 1; Frame <= 45; ++Frame)
	{
		Fires += Duck.Step(Stand - 35.f + 35.f * Frame / 45.f, Dt) ? 1 : 0;
	}
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		Fires += Duck.Step(Stand, Dt) ? 1 : 0;
	}
	for (int32 Frame = 1; Frame <= 45; ++Frame)
	{
		Fires += Duck.Step(Stand - 35.f * Frame / 45.f, Dt) ? 1 : 0;
	}
	TestEqual(TEXT("Levantarse y volver a agacharse dispara otra vez"), Fires, 2);

	// Agacharse poco (inclinarse 12 cm) o muy despacio (bajar 40 cm en 30 s, sentándose) no cuenta.
	TNVRGestures::FDuckGesture Lean;
	bool bLeanFired = false;
	for (int32 Frame = 0; Frame < 90; ++Frame) { bLeanFired |= Lean.Step(Stand, Dt); }
	for (int32 Frame = 1; Frame <= 45; ++Frame) { bLeanFired |= Lean.Step(Stand - 12.f * Frame / 45.f, Dt); }
	TestFalse(TEXT("Inclinarse 12 cm no es agacharse"), bLeanFired);
	TNVRGestures::FDuckGesture Sit;
	bool bSitFired = false;
	const int32 SitFrames = static_cast<int32>(30.f / Dt);
	for (int32 Frame = 0; Frame <= SitFrames; ++Frame) { bSitFired |= Sit.Step(Stand - 40.f * Frame / SitFrames, Dt); }
	TestFalse(TEXT("Bajar 40 cm en 30 s (sentarse) no es agacharse"), bSitFired);
	// Un salto de la pose de un fotograma (recentrar) no dispara.
	TNVRGestures::FDuckGesture Jump;
	Jump.Step(Stand, Dt);
	TestFalse(TEXT("Un salto de la pose no es agacharse"), Jump.Step(Stand - 90.f, Dt));
	TestFalse(TEXT("Y se vuelve a medir desde ahí"), Jump.Step(Stand - 90.f, Dt));
	return true;
}

#endif
