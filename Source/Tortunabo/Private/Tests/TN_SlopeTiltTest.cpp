// Inclinación visual de la tortuga con la pendiente (#586): lógica pura de TN_SlopeTiltDecisions.h, la que usa
// UTN_SlopeTiltComponent en producción. Sin mundo ni componentes. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Player.SlopeTilt; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_SlopeTiltDecisions.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNSlopeTiltTest
{
	constexpr float MaxTilt = 35.f;
	constexpr double Tolerance = 0.01;

	/** Normal de una cuesta de SlopeDeg que sube hacia el rumbo UphillYawDeg (mundo). */
	FVector SlopeNormal(float SlopeDeg, float UphillYawDeg)
	{
		const float SlopeRad = FMath::DegreesToRadians(SlopeDeg);
		const FVector Uphill = FRotator(0.f, UphillYawDeg, 0.f).Vector();
		return (-Uphill * FMath::Sin(SlopeRad) + FVector::UpVector * FMath::Cos(SlopeRad)).GetSafeNormal();
	}

	/** Un fotograma de UTN_SlopeTiltComponent sobre una malla de prueba: lee su giro relativo y escribe el que le dan. */
	void TickMesh(TNSlopeTilt::FTiltDriver& Driver, FRotator& Mesh, const TNSlopeTilt::FTiltGate& Gate, const FRotator& FloorTilt,
		float DeltaTime)
	{
		FQuat NewRelative;
		if (Driver.Tick(Mesh, Gate, FloorTilt, DeltaTime, TNSlopeTilt::DefaultInterpSpeed, TNSlopeTilt::DefaultMaxRateDegPerSec, NewRelative))
		{
			Mesh = NewRelative.Rotator();
			Driver.NoteWritten(Mesh);
		}
	}

	/** Ángulo (grados) entre dos giros relativos de la malla. */
	double AngleDeg(const FRotator& A, const FRotator& B)
	{
		return FMath::RadiansToDegrees(A.Quaternion().AngularDistance(B.Quaternion()));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSlopeTiltComputeTest,
	"Tortunabo.Player.SlopeTilt.Compute",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSlopeTiltComputeTest::RunTest(const FString& Parameters)
{
	using namespace TNSlopeTilt;
	using namespace TNSlopeTiltTest;

	const FRotator Flat = ComputeTilt(FVector::UpVector, 37.f, MaxTilt);
	TestEqual(TEXT("Llano: sin cabeceo"), Flat.Pitch, 0.0, Tolerance);
	TestEqual(TEXT("Llano: sin alabeo"), Flat.Roll, 0.0, Tolerance);

	const FRotator Front = ComputeTilt(SlopeNormal(25.f, 0.f), 0.f, MaxTilt);
	TestEqual(TEXT("25° de frente (cuesta arriba): cabeceo +25"), Front.Pitch, 25.0, Tolerance);
	TestEqual(TEXT("25° de frente: sin alabeo"), Front.Roll, 0.0, Tolerance);

	const FRotator Down = ComputeTilt(SlopeNormal(25.f, 180.f), 0.f, MaxTilt);
	TestEqual(TEXT("25° de frente cuesta abajo: cabeceo -25"), Down.Pitch, -25.0, Tolerance);

	const FRotator SideRight = ComputeTilt(SlopeNormal(25.f, 90.f), 0.f, MaxTilt);
	TestEqual(TEXT("25° de lado (sube por la derecha): sin cabeceo"), SideRight.Pitch, 0.0, Tolerance);
	TestEqual(TEXT("25° de lado (sube por la derecha): alabeo -25"), SideRight.Roll, -25.0, Tolerance);

	const FRotator SideLeft = ComputeTilt(SlopeNormal(25.f, -90.f), 0.f, MaxTilt);
	TestEqual(TEXT("25° de lado (sube por la izquierda): alabeo +25"), SideLeft.Roll, 25.0, Tolerance);

	const FRotator Steep = ComputeTilt(SlopeNormal(60.f, 0.f), 0.f, MaxTilt);
	TestEqual(TEXT("60° de frente: cabeceo en el tope"), Steep.Pitch, 35.0, Tolerance);
	TestEqual(TEXT("60° de frente: sin alabeo"), Steep.Roll, 0.0, Tolerance);

	const FRotator SteepSide = ComputeTilt(SlopeNormal(60.f, 90.f), 0.f, MaxTilt);
	TestEqual(TEXT("60° de lado: alabeo en el tope"), FMath::Abs(SteepSide.Roll), 35.0, Tolerance);

	// El rumbo de la tortuga cuenta: la misma cuesta de frente con la tortuga girada 90° pasa a ser de lado.
	const FRotator Turned = ComputeTilt(SlopeNormal(25.f, 90.f), 90.f, MaxTilt);
	TestEqual(TEXT("Rumbo 90° y cuesta que sube hacia 90°: cabeceo +25"), Turned.Pitch, 25.0, Tolerance);
	TestEqual(TEXT("Rumbo 90° y cuesta que sube hacia 90°: sin alabeo"), Turned.Roll, 0.0, Tolerance);

	const FRotator Gentle = ComputeTilt(SlopeNormal(1.5f, 0.f), 0.f, MaxTilt, 2.f);
	TestTrue(TEXT("Por debajo del mínimo se considera llano"), Gentle.IsZero());

	TestTrue(TEXT("Normal nula: sin inclinación"), ComputeTilt(FVector::ZeroVector, 0.f, MaxTilt).IsZero());
	TestTrue(TEXT("Normal hacia abajo (techo): sin inclinación"), ComputeTilt(-FVector::UpVector, 0.f, MaxTilt).IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSlopeTiltParallelTest,
	"Tortunabo.Player.SlopeTilt.ParallelToGround",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSlopeTiltParallelTest::RunTest(const FString& Parameters)
{
	using namespace TNSlopeTilt;
	using namespace TNSlopeTiltTest;

	// Cuestas en diagonal: con la inclinación compuesta sobre la base, el eje Z de la malla queda sobre la normal
	// (en los ejes de la cápsula), sea cual sea el giro propio de la malla (la del juego lleva Yaw = 90).
	const FQuat MeshBase = FRotator(0.f, 90.f, 0.f).Quaternion();
	const float Yaws[] = { 0.f, 30.f, 135.f, -100.f };
	const float Uphills[] = { 0.f, 45.f, 200.f, -70.f };
	for (const float Yaw : Yaws)
	{
		for (const float Uphill : Uphills)
		{
			const FVector Normal = SlopeNormal(28.f, Uphill);
			const FRotator Tilt = ComputeTilt(Normal, Yaw, MaxTilt);
			const FVector LocalNormal = FRotator(0.f, Yaw, 0.f).UnrotateVector(Normal);
			const FVector MeshUp = ComposeTilt(MeshBase, Tilt).RotateVector(FVector::UpVector);
			TestTrue(*FString::Printf(TEXT("Rumbo %.0f, cuesta hacia %.0f: la malla queda paralela al suelo"), Yaw, Uphill),
				MeshUp.Equals(LocalNormal, 1.e-3));
		}
	}

	// Por encima del tope, la malla se inclina exactamente MaxTilt y hacia el mismo lado que la cuesta.
	const FVector Steep = SlopeNormal(55.f, 30.f);
	const FVector SteepUp = ComposeTilt(FQuat::Identity, ComputeTilt(Steep, 0.f, MaxTilt)).RotateVector(FVector::UpVector);
	TestEqual(TEXT("Tope: inclinación total 35°"), FMath::RadiansToDegrees(FMath::Acos(SteepUp.Z)), 35.0, 1.e-2);
	TestTrue(TEXT("Tope: misma dirección horizontal que la cuesta"),
		FVector2D(SteepUp.X, SteepUp.Y).GetSafeNormal().Equals(FVector2D(Steep.X, Steep.Y).GetSafeNormal(), 1.e-3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSlopeTiltGateTest,
	"Tortunabo.Player.SlopeTilt.Gate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSlopeTiltGateTest::RunTest(const FString& Parameters)
{
	using namespace TNSlopeTilt;

	FTiltGate Ground;
	Ground.bOnGround = true;
	TestTrue(TEXT("En el suelo, de pie: se inclina"), ShouldTilt(Ground));

	TestFalse(TEXT("En el aire: recta"), ShouldTilt(FTiltGate()));

	FTiltGate Shell = Ground;
	Shell.bInShell = true;
	TestFalse(TEXT("En la bola: recta"), ShouldTilt(Shell));

	FTiltGate Carried = Ground;
	Carried.bCarried = true;
	TestFalse(TEXT("Llevada: recta"), ShouldTilt(Carried));

	FTiltGate Knocked = Ground;
	Knocked.bKnockedDown = true;
	TestFalse(TEXT("Derribada: recta"), ShouldTilt(Knocked));

	FTiltGate Dead = Ground;
	Dead.bDead = true;
	TestFalse(TEXT("Muerta: recta"), ShouldTilt(Dead));

	FTiltGate Ragdoll = Ground;
	Ragdoll.bRagdoll = true;
	TestFalse(TEXT("En ragdoll: recta"), ShouldTilt(Ragdoll));

	// En el aire vuelve a recto interpolando; en la bola, llevada, derribada, muerta o en ragdoll se quita al momento.
	TestFalse(TEXT("En el aire: se interpola, no se quita de golpe"), IsTakenOver(FTiltGate()));
	TestTrue(TEXT("En la bola: se quita al momento"), IsTakenOver(Shell));
	TestTrue(TEXT("Llevada: se quita al momento"), IsTakenOver(Carried));
	TestTrue(TEXT("Derribada: se quita al momento"), IsTakenOver(Knocked));
	TestTrue(TEXT("Muerta: se quita al momento"), IsTakenOver(Dead));
	TestTrue(TEXT("En ragdoll: se quita al momento"), IsTakenOver(Ragdoll));

	// En la pausa del huevo la eclosión anima la malla desde su foto: ni se inclina ni se escribe en ella.
	FTiltGate Hatching = Ground;
	Hatching.bHatching = true;
	TestFalse(TEXT("En el huevo: no se inclina"), ShouldTilt(Hatching));
	TestTrue(TEXT("En el huevo: otro sistema manda en la malla"), IsTakenOver(Hatching));
	TestTrue(TEXT("En el huevo: la malla sale de su foto (no se toca)"), HoldsSnapshot(Hatching));
	TestTrue(TEXT("En ragdoll: la malla sale de su foto (no se toca)"), HoldsSnapshot(Ragdoll));
	TestFalse(TEXT("Derribada sin ragdoll: se devuelve a su base"), HoldsSnapshot(Knocked));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSlopeTiltInterpTest,
	"Tortunabo.Player.SlopeTilt.Interp",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSlopeTiltInterpTest::RunTest(const FString& Parameters)
{
	using namespace TNSlopeTilt;

	// Sin saltos: a 60 fps y velocidad 8, el primer fotograma avanza menos de un 15 % del camino.
	const FRotator Target(25.f, 0.f, -10.f);
	const FRotator First = StepTilt(FRotator::ZeroRotator, Target, 1.f / 60.f, 8.f);
	TestTrue(TEXT("Primer paso: avanza"), First.Pitch > 0.f && First.Roll < 0.f);
	TestTrue(TEXT("Primer paso: sin salto"), First.Pitch < 25.f * 0.15f);

	// Converge sin pasarse y se pega al objetivo.
	FRotator Current = FRotator::ZeroRotator;
	bool bOvershoot = false;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Current = StepTilt(Current, Target, 1.f / 60.f, 8.f);
		bOvershoot |= Current.Pitch > Target.Pitch || Current.Roll < Target.Roll;
	}
	TestFalse(TEXT("No se pasa del objetivo"), bOvershoot);
	TestTrue(TEXT("En 2 s llega exacto"), Current.Equals(Target, 0.f));

	// De vuelta a recto (al saltar o meterse en la bola) también llega a cero exacto, para dejar de escribir en la malla.
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Current = StepTilt(Current, FRotator::ZeroRotator, 1.f / 60.f, 8.f);
	}
	TestTrue(TEXT("Vuelve a cero exacto"), Current.IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSlopeTiltLandingTest,
	"Tortunabo.Player.SlopeTilt.LandingNoJerk",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSlopeTiltLandingTest::RunTest(const FString& Parameters)
{
	using namespace TNSlopeTilt;

	// Aterrizaje del panzazo (#586): en el vuelo la inclinación es cero y al tocar una cuesta de 35° (el tope) pasa a la
	// del suelo. Con los valores del componente, ningún fotograma gira más de 5° (el tirón que se ve), a 30 y a 60 fps,
	// y en menos de 1 s queda paralela al suelo. Solo con la interpolación exponencial, el primer paso eran 9,3° a 30 fps.
	constexpr float MaxJerkDeg = 5.f;
	const FRotator Targets[] = { FRotator(-35.f, 0.f, 0.f), FRotator(35.f, 0.f, 0.f), FRotator(-24.7f, 0.f, 24.7f) };
	const float FrameRates[] = { 30.f, 60.f };
	for (const FRotator& Target : Targets)
	{
		for (const float Fps : FrameRates)
		{
			const float DeltaTime = 1.f / Fps;
			FRotator Current = FRotator::ZeroRotator;
			float MaxStepDeg = 0.f;
			bool bOffDirection = false;
			for (int32 Frame = 0; Frame < FMath::CeilToInt(Fps); ++Frame)
			{
				const FRotator Next = StepTilt(Current, Target, DeltaTime, DefaultInterpSpeed, DefaultMaxRateDegPerSec);
				const FVector2D Step(Next.Pitch - Current.Pitch, Next.Roll - Current.Roll);
				const FVector2D ToTarget(Target.Pitch - Current.Pitch, Target.Roll - Current.Roll);
				MaxStepDeg = FMath::Max(MaxStepDeg, static_cast<float>(Step.Size()));
				// El tope recorta el paso sin torcerlo: sigue apuntando al objetivo.
				bOffDirection |= !ToTarget.IsNearlyZero() && FVector2D::DotProduct(Step.GetSafeNormal(), ToTarget.GetSafeNormal()) < 0.999;
				Current = Next;
			}
			const FString Case = FString::Printf(TEXT("(%.1f, %.1f) a %.0f fps"), Target.Pitch, Target.Roll, Fps);
			TestTrue(FString::Printf(TEXT("%s: ningún fotograma gira más de %.0f° (máx. %.2f°)"), *Case, MaxJerkDeg, MaxStepDeg), MaxStepDeg <= MaxJerkDeg);
			TestFalse(FString::Printf(TEXT("%s: el paso va hacia el objetivo"), *Case), bOffDirection);
			TestTrue(FString::Printf(TEXT("%s: en 1 s queda a menos de 0,5° del suelo"), *Case), Current.Equals(Target, 0.5f));
		}
	}

	// Sin tope (0), el paso es el exponencial de siempre.
	const FRotator Free = StepTilt(FRotator::ZeroRotator, FRotator(-35.f, 0.f, 0.f), 1.f / 30.f, DefaultInterpSpeed, 0.f);
	TestTrue(TEXT("Sin tope: el primer paso es el exponencial"), FMath::IsNearlyEqual(Free.Pitch, -35.f * DefaultInterpSpeed / 30.f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSlopeTiltResumeTest,
	"Tortunabo.Player.SlopeTilt.ResumeAfterSnapshot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSlopeTiltResumeTest::RunTest(const FString& Parameters)
{
	using namespace TNSlopeTilt;

	// Derribo en una cuesta de 35° (#586): el derribo hace una foto de la malla inclinada, la suelta a la física y al
	// levantarse la devuelve. Al retomar se sigue desde la inclinación de la foto: el primer paso hacia el llano (o hacia
	// otra cuesta) es de unos grados, no los 35° de golpe que se veían al poner la base.
	constexpr float Tolerance = 1.e-2f;
	const FQuat Base = FRotator(0.f, -90.f, 0.f).Quaternion();
	const FRotator Tilt(-35.f, 0.f, 0.f);
	const FRotator Written = ComposeTilt(Base, Tilt).Rotator();

	FTiltResume Resume;
	Resume.Remember(Written, Base, Tilt);
	// Durante el derribo otros sistemas escriben su pose: se queda la de la foto.
	Resume.Remember(FRotator(90.f, -90.f, 0.f), FQuat::Identity, FRotator(-10.f, 0.f, 0.f));

	FQuat OutBase = FQuat::Identity;
	FRotator OutTilt = FRotator::ZeroRotator;
	TestFalse(TEXT("Con otra pose no se retoma"), Resume.TryResume(FRotator(90.f, -90.f, 0.f), Tolerance, OutBase, OutTilt));
	// La foto vuelve por una transformación (cuaternión ida y vuelta).
	const FRotator Restored = FTransform(Written).GetRotation().Rotator();
	TestTrue(TEXT("Con la foto se retoma"), Resume.TryResume(Restored, Tolerance, OutBase, OutTilt));
	TestTrue(TEXT("Retoma la inclinación de la foto"), OutTilt.Equals(Tilt, 0.01f));
	TestTrue(TEXT("Retoma la base de la foto (sin inclinación doble)"), OutBase.Equals(Base, 1.e-4f));
	TestFalse(TEXT("Se retoma una sola vez"), Resume.TryResume(Restored, Tolerance, OutBase, OutTilt));

	const FRotator Next = StepTilt(OutTilt, FRotator::ZeroRotator, 1.f / 30.f, DefaultInterpSpeed, DefaultMaxRateDegPerSec);
	const float FirstStepDeg = static_cast<float>(FVector2D(Next.Pitch - OutTilt.Pitch, Next.Roll - OutTilt.Roll).Size());
	TestTrue(FString::Printf(TEXT("Primer paso al llano a 30 fps <= 5° (%.2f°)"), FirstStepDeg), FirstStepDeg <= 5.f);

	FTiltResume Forgotten;
	Forgotten.Remember(Written, Base, Tilt);
	Forgotten.Forget();
	TestFalse(TEXT("Olvidada no se retoma"), Forgotten.TryResume(Written, Tolerance, OutBase, OutTilt));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSlopeTiltEggHatchTest,
	"Tortunabo.Player.SlopeTilt.EggHatchResume",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSlopeTiltEggHatchTest::RunTest(const FString& Parameters)
{
	using namespace TNSlopeTilt;
	using namespace TNSlopeTiltTest;

	// Eclosión del huevo en una cuesta de 35° (#586, revisión de Mokius): la eclosión hace una foto de la malla inclinada,
	// sujeta a la tortuga (MOVE_None: ya no está «en el suelo»), pone su vaivén sobre la foto en cada fotograma y la
	// devuelve al lanzarla. Antes, el primer fotograma tras la foto ya enderezaba la malla (en el aire), se recordaba ese
	// giro y no el de la foto, y al lanzarla la tortuga seguía torcida en el aire.
	constexpr float DeltaTime = 1.f / 30.f;
	const FRotator MeshDefault(0.f, -90.f, 0.f);
	FTiltDriver Driver;
	FRotator Mesh = MeshDefault;

	FTiltGate Ground;
	Ground.bOnGround = true;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		TickMesh(Driver, Mesh, Ground, FRotator(-35.f, 0.f, 0.f), DeltaTime);
	}
	TestEqual(TEXT("En la cuesta: inclinada 35°"), AngleDeg(Mesh, MeshDefault), 35.0, 0.5);

	const FQuat Photo = Mesh.Quaternion();
	FTiltGate Hatching;
	Hatching.bHatching = true;
	bool bTouchedPose = false;
	for (int32 Frame = 0; Frame < 45; ++Frame)
	{
		const double TwistDeg = 12.0 * FMath::Sin(0.9 * Frame);
		Mesh = (FQuat(FVector::UpVector, FMath::DegreesToRadians(TwistDeg)) * Photo).Rotator();
		const FRotator Posed = Mesh;
		TickMesh(Driver, Mesh, Hatching, FRotator::ZeroRotator, DeltaTime);
		bTouchedPose |= !Mesh.Equals(Posed, 1.e-3f);
	}
	TestFalse(TEXT("En la pausa del huevo no se toca la pose de la eclosión"), bTouchedPose);

	// El lanzamiento devuelve la foto y la tortuga sale volando: se retoma la inclinación de la foto y se endereza poco a poco.
	Mesh = Photo.Rotator();
	const FTiltGate Air;
	double MaxStepDeg = 0.0;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		const FRotator Before = Mesh;
		TickMesh(Driver, Mesh, Air, FRotator::ZeroRotator, DeltaTime);
		MaxStepDeg = FMath::Max(MaxStepDeg, AngleDeg(Before, Mesh));
	}
	TestTrue(FString::Printf(TEXT("Tras el huevo ningún fotograma gira más de 5° (máx. %.2f°)"), MaxStepDeg), MaxStepDeg <= 5.0);
	const double LeftDeg = AngleDeg(Mesh, MeshDefault);
	TestTrue(FString::Printf(TEXT("En 1 s en el aire queda recta, sin la inclinación de la foto (%.2f°)"), LeftDeg), LeftDeg < 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSlopeTiltForgetWhenFreeTest,
	"Tortunabo.Player.SlopeTilt.ForgetResumeWhenFree",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSlopeTiltForgetWhenFreeTest::RunTest(const FString& Parameters)
{
	using namespace TNSlopeTilt;
	using namespace TNSlopeTiltTest;

	// Ragdoll en una cuesta: se recuerda la inclinación, pero al levantarse la malla vuelve a su giro por defecto (no a la
	// foto). Con la malla libre y otro giro, esa foto ya no vuelve: se olvida para que no tape la siguiente (la del huevo).
	constexpr float DeltaTime = 1.f / 30.f;
	const FRotator MeshDefault(0.f, -90.f, 0.f);
	const FRotator Slope(-20.f, 0.f, 0.f);
	FTiltDriver Driver;
	FRotator Mesh = MeshDefault;

	FTiltGate Ground;
	Ground.bOnGround = true;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		TickMesh(Driver, Mesh, Ground, Slope, DeltaTime);
	}
	FTiltGate Ragdoll = Ground;
	Ragdoll.bRagdoll = true;
	TickMesh(Driver, Mesh, Ragdoll, FRotator::ZeroRotator, DeltaTime);
	TestTrue(TEXT("En ragdoll se recuerda la inclinación"), Driver.Resume.bPending);

	Mesh = MeshDefault;
	TickMesh(Driver, Mesh, FTiltGate(), FRotator::ZeroRotator, DeltaTime);
	TestFalse(TEXT("Con la malla libre y otro giro, la foto pendiente se olvida"), Driver.Resume.bPending);

	// Un fotograma en la cuesta y enseguida el huevo: se retoma esa inclinación, no la del ragdoll.
	TickMesh(Driver, Mesh, Ground, Slope, DeltaTime);
	const FRotator Photo = Mesh;
	TestTrue(TEXT("Empieza a inclinarse"), AngleDeg(Photo, MeshDefault) > 1.0);
	FTiltGate Hatching;
	Hatching.bHatching = true;
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		TickMesh(Driver, Mesh, Hatching, FRotator::ZeroRotator, DeltaTime);
	}
	Mesh = Photo;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		TickMesh(Driver, Mesh, FTiltGate(), FRotator::ZeroRotator, DeltaTime);
	}
	const double LeftDeg = AngleDeg(Mesh, MeshDefault);
	TestTrue(FString::Printf(TEXT("Tras el huevo queda recta en el aire (%.2f°)"), LeftDeg), LeftDeg < 0.5);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
