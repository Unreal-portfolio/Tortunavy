// Calibración de la cabeza con gafas (#916, VR/TN_VRHeadCalibration.h): la cámara sale de los ojos de la tortuga aunque el
// origen del seguimiento esté a un metro de la cabeza. Lógica pura, sin gafas.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.VR.HeadCalibration; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "VR/TN_VRHeadCalibration.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRHeadCalibrationTest,
	"Tortunabo.VR.HeadCalibration",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRHeadCalibrationTest::RunTest(const FString& Parameters)
{
	const FVector Hmd(-100.0, 20.0, 160.0);
	const float Dt = 1.f / 72.f;

	// Espera unos fotogramas a que la pose se asiente y calibra con las gafas puestas.
	FTNVRHeadCalibration Cal;
	TestFalse(TEXT("Sin calibrar al empezar"), Cal.bValid);
	int32 Frames = 0;
	while (!Cal.Update(Hmd, true, Dt) && Frames < 20)
	{
		++Frames;
	}
	TestEqual(TEXT("Calibra pasados SettleFrames fotogramas"), Frames, FTNVRHeadCalibration::SettleFrames);
	TestTrue(TEXT("Calibrada"), Cal.bValid);
	TestTrue(TEXT("Guarda la posición de las gafas"), Cal.Base.Equals(Hmd, 0.01));
	TestFalse(TEXT("Calibrada, no vuelve a calibrar sola"), Cal.Update(FVector(5.0, 5.0, 5.0), true, Dt));
	TestTrue(TEXT("Sigue con la misma base"), Cal.Base.Equals(Hmd, 0.01));

	// Con las gafas quitadas espera (pose sin valor); pasado el tope calibra de todos modos (dispositivo que no lo dice).
	FTNVRHeadCalibration Off;
	bool bCalibrated = false;
	float Elapsed = 0.f;
	while (Elapsed < FTNVRHeadCalibration::MaxWaitSeconds * 0.5f)
	{
		bCalibrated |= Off.Update(Hmd, false, Dt);
		Elapsed += Dt;
	}
	TestFalse(TEXT("Con las gafas quitadas no calibra enseguida"), bCalibrated);
	while (Elapsed < FTNVRHeadCalibration::MaxWaitSeconds + 0.5f && !bCalibrated)
	{
		bCalibrated |= Off.Update(Hmd, false, Dt);
		Elapsed += Dt;
	}
	TestTrue(TEXT("Pasado el tope calibra de todos modos"), bCalibrated);

	// Pedir otra: se mantiene la anterior hasta medir la nueva (la cámara no salta antes de tiempo).
	Cal.Request();
	TestFalse(TEXT("Pedida otra: sin calibrar"), Cal.bValid);
	TestTrue(TEXT("Hasta medir, vale la anterior"), Cal.Base.Equals(Hmd, 0.01));
	const FVector Moved(40.0, -30.0, 120.0);
	for (int32 i = 0; i <= FTNVRHeadCalibration::SettleFrames; ++i)
	{
		Cal.Update(Moved, true, Dt);
	}
	TestTrue(TEXT("Mide la nueva posición"), Cal.bValid && Cal.Base.Equals(Moved, 0.01));
	Cal.Reset();
	TestTrue(TEXT("Reset: la posición vuelve a contar entera"), Cal.Base.IsNearlyZero() && !Cal.bValid);

	// El origen, desplazado: con el origen girado cualquier ángulo, la cámara (origen + giro * pose) cae en los ojos cuando la
	// cabeza está donde se calibró, y se mueve lo que la cabeza se mueva después.
	Cal.Base = Hmd;
	Cal.bValid = true;
	const FVector Eyes(1200.0, -340.0, 95.0);
	for (const double Yaw : { 0.0, 30.0, 90.0, -135.0, 180.0 })
	{
		const FRotator Rotation(0.0, Yaw, 0.0);
		const FVector Origin = Eyes - Cal.OriginShift(Yaw);
		const FVector Camera = Origin + Rotation.RotateVector(Hmd);
		TestTrue(FString::Printf(TEXT("Cámara en los ojos con el origen girado %.0f°"), Yaw), Camera.Equals(Eyes, 0.01));
		const FVector Step(0.0, 25.0, -10.0);
		const FVector Leaned = Origin + Rotation.RotateVector(Hmd + Step);
		TestTrue(FString::Printf(TEXT("Lo que se mueve la cabeza se ve (%.0f°)"), Yaw), Leaned.Equals(Eyes + Rotation.RotateVector(Step), 0.01));
	}

	// Girar el origen alrededor de la cabeza calibrada no la mueve de los ojos (lo que hace AddVRYaw con AddWorldOffset).
	{
		const double OldYaw = 20.0;
		const double NewYaw = 110.0;
		FVector Origin = Eyes - Cal.OriginShift(OldYaw);
		Origin += Cal.OriginShift(OldYaw) - Cal.OriginShift(NewYaw);
		const FVector Camera = Origin + FRotator(0.0, NewYaw, 0.0).RotateVector(Hmd);
		TestTrue(TEXT("Un giro de golpe no saca la cabeza de los ojos"), Camera.Equals(Eyes, 0.01));
	}
	return true;
}

#endif
