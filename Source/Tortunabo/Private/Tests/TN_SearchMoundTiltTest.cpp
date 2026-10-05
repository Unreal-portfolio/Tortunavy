// El suelo bajo el anillo fijo de un rebuscable y bajo su montículo de arena (#744): el mismo plano en los dos, para que el
// montículo se incline con la cuesta como su anillo. Correr desde Session Frontend (categoría "Tortunabo.Search.MoundTilt")
// o sin ventana:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Search.MoundTilt; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/ProcMap/TN_ProcSearchSpot.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSearchMoundTiltTest,
	"Tortunabo.Search.MoundTilt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSearchMoundTiltTest::RunTest(const FString& Parameters)
{
	constexpr double Reach = 130.0;
	// Cuatro puntos del suelo a 0°, 90°, 180° y 270° del centro en el plano Z = Base + SlopeX * X + SlopeY * Y.
	const auto TiltOf = [Reach](double SlopeX, double SlopeY)
	{
		const auto Rim = [Reach, SlopeX, SlopeY](double X, double Y) { return FVector(X * Reach, Y * Reach, 40.0 + SlopeX * X * Reach + SlopeY * Y * Reach); };
		return TNSearchMarker::GroundTilt(Rim(1.0, 0.0), Rim(0.0, 1.0), Rim(-1.0, 0.0), Rim(0.0, -1.0));
	};

	// En llano, sin inclinar (igual que antes de este arreglo).
	TestTrue(TEXT("Llano: sin giro"), TiltOf(0.0, 0.0).Equals(FQuat::Identity, 1e-6));

	// Cuesta que sube hacia +X: el montículo se echa con ella (su +Z, la normal del plano; su +X, paralelo a la cuesta).
	const FQuat Slope = TiltOf(0.3, 0.0);
	const FVector Up = Slope.RotateVector(FVector::UpVector);
	TestTrue(TEXT("Cuesta: el +Z del montículo es la normal del suelo"), Up.Equals(FVector(-0.3, 0.0, 1.0).GetSafeNormal(), 1e-6));
	const FVector Along = Slope.RotateVector(FVector::ForwardVector);
	TestTrue(TEXT("Cuesta: su +X sigue la pendiente del suelo (ni enterrado ni flotando)"), FMath::IsNearlyEqual(Along.Z / Along.X, 0.3, 1e-6));
	TestTrue(TEXT("Cuesta: su +Y queda horizontal"), FMath::IsNearlyZero(Slope.RotateVector(FVector::RightVector).Z, 1e-6));

	// Cuesta cruzada: se inclina en las dos direcciones a la vez.
	const FVector Cross = TiltOf(-0.2, 0.25).RotateVector(FVector::UpVector);
	TestTrue(TEXT("Cuesta cruzada: la normal mira cuesta abajo en X e Y"), Cross.Equals(FVector(0.2, -0.25, 1.0).GetSafeNormal(), 1e-6));

	// Muy empinado: se queda plano, como el anillo.
	TestTrue(TEXT("Muy empinado: sin giro"), TiltOf(2.0, 0.0).Equals(FQuat::Identity, 1e-6));
	TestTrue(TEXT("Justo bajo el límite: se inclina"), !TiltOf(1.2, 0.0).Equals(FQuat::Identity, 1e-6));

	// El radio del anillo es el de siempre: la base del montículo con los terrones (110 x tamaño) más el margen, entre el
	// punto donde empiezan los guiones.
	TestTrue(TEXT("Radio del anillo de un montículo de tamaño 1"), FMath::IsNearlyEqual(TNSearchMarker::RingRadiusForFoot(110.f), 135.f / 0.87f, 0.01f));
	TestTrue(TEXT("Más grande, más radio"), TNSearchMarker::RingRadiusForFoot(137.5f) > TNSearchMarker::RingRadiusForFoot(88.f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
