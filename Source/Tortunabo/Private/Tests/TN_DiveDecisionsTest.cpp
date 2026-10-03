// Doble salto físico (plan maestro §3.5): las cuentas del panzazo de TNDiveLogic (TN_DiveDecisions.h) con los ajustes por
// defecto de UTN_TurtleMovementComponent. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Dive; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_DiveDecisions.h"
#include "Player/TN_TurtleMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNDiveDecisionsTestDetail
{
	/** Suelo inclinado Degrees grados que baja hacia +X. */
	FVector SlopeNormal(float Degrees)
	{
		const float Rad = FMath::DegreesToRadians(Degrees);
		return FVector(FMath::Sin(Rad), 0.0, FMath::Cos(Rad));
	}

	/** Arrastre sobre arena con los ajustes por defecto del componente (sin el Blueprint). */
	struct FSandSlide
	{
		const UTN_TurtleMovementComponent* Defaults = GetDefault<UTN_TurtleMovementComponent>();
		/** Ángulo desde el que sigue cayendo (90 = como antes de #62). */
		float MinAngle = 12.f;
		float BellyTime = 0.f;
		float SlopeTime = 0.f;

		TNDiveLogic::FBellyStepInput Input(const FVector& Normal) const
		{
			TNDiveLogic::FBellyStepInput In;
			In.Normal = Normal;
			In.Gravity = 980.f;
			// Como UTN_TurtleMovementComponent::SlideFrictionNow sobre arena: la rampa con el tiempo que corre.
			const float Ramp = 1.f + Defaults->BellyFrictionRamp * FMath::Max(0.f, BellyTime - Defaults->BellyFrictionRampStart);
			In.Friction = Defaults->BellyFrictionSand * Ramp;
			In.Drag = Defaults->BellyDrag;
			In.Slope.SlopeGravity = Defaults->BellySlopeGravity;
			In.Slope.MinAngleDeg = MinAngle;
			In.Slope.FrictionScale = Defaults->BellySlopeFrictionScale;
			In.Slope.DragScale = Defaults->BellySlopeDragScale;
			In.Slope.MaxSpeed = Defaults->BellyMaxSpeed;
			return In;
		}

		/** Un movimiento de Dt: el tiempo que toca (como TickBellyPhase) y la velocidad (como CalcBellySlideVelocity). */
		FVector Move(const FVector& V, const FVector& Normal, float Dt)
		{
			if (TNDiveLogic::ShouldAdvanceBellyTimer(Normal, V, MinAngle))
			{
				BellyTime += Dt;
			}
			else
			{
				SlopeTime += Dt;
			}
			return TNDiveLogic::IntegrateBellyVelocity(V, Input(Normal), Dt);
		}

		/** Segundos hasta pararse del todo (o Limit si no se para). */
		float TimeToStop(FVector V, const FVector& Normal, float Limit = 5.f)
		{
			constexpr float Dt = 1.f / 60.f;
			float T = 0.f;
			while (T < Limit && !V.IsNearlyZero(1.0))
			{
				V = Move(V, Normal, Dt);
				T += Dt;
			}
			return T;
		}

		/** Velocidad horizontal tras Seconds. */
		FVector After(FVector V, const FVector& Normal, float Seconds, float Dt = 1.f / 60.f)
		{
			for (float T = 0.f; T < Seconds - Dt * 0.5f; T += Dt)
			{
				V = Move(V, Normal, Dt);
			}
			return V;
		}
	};

	/** Velocidad típica al empezar a arrastrarse: panzazo andando, 350 + 450 cm/s, un 90 % tras el golpe. */
	const FVector EntryVelocity(720.0, 0.0, 0.0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNDiveSlopeFlatTest,
	"Tortunabo.Dive.Slope.Flat",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNDiveSlopeFlatTest::RunTest(const FString& Parameters)
{
	using namespace TNDiveDecisionsTestDetail;
	FSandSlide Slide;
	const float Stop = Slide.TimeToStop(EntryVelocity, FVector::UpVector);
	TestTrue(FString::Printf(TEXT("Arena en llano: se para en menos de 0,8 s (%.2f s)"), Stop), Stop < 0.8f);
	AddInfo(FString::Printf(TEXT("Arena en llano desde 720 cm/s: parada en %.2f s"), Stop));
	TestEqual(TEXT("En llano el tiempo del arrastre corre"), Slide.SlopeTime, 0.f);

	// El llano y las cuestas suaves no cambian con la pendiente (mismas cuentas que con ella apagada).
	for (const float Degrees : { 0.f, 5.f, 11.f })
	{
		FSandSlide On;
		FSandSlide Off;
		Off.MinAngle = 90.f;
		const FVector N = SlopeNormal(Degrees);
		const FVector VOn = On.After(EntryVelocity, N, 0.4f);
		const FVector VOff = Off.After(EntryVelocity, N, 0.4f);
		TestTrue(FString::Printf(TEXT("A %.0f° igual que antes"), Degrees), VOn.Equals(VOff, 1e-3));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNDiveSlopeDownhillTest,
	"Tortunabo.Dive.Slope.Downhill",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNDiveSlopeDownhillTest::RunTest(const FString& Parameters)
{
	using namespace TNDiveDecisionsTestDetail;
	const FVector Sand25 = SlopeNormal(25.f);

	// Arena a 25°: sigue cayendo, desde el golpe típico y desde parada.
	FSandSlide FromEntry;
	const float SpeedEntry = static_cast<float>(FromEntry.After(EntryVelocity, Sand25, 2.f).Size());
	TestTrue(FString::Printf(TEXT("Arena a 25°, entrando a 720: más de 200 cm/s tras 2 s (%.0f)"), SpeedEntry), SpeedEntry > 200.f);
	FSandSlide FromRest;
	const float SpeedRest = static_cast<float>(FromRest.After(FVector::ZeroVector, Sand25, 2.f).Size());
	TestTrue(FString::Printf(TEXT("Arena a 25°, desde parada: más de 200 cm/s tras 2 s (%.0f)"), SpeedRest), SpeedRest > 200.f);
	AddInfo(FString::Printf(TEXT("Arena a 25° tras 2 s: %.0f cm/s entrando a 720, %.0f cm/s desde parada"), SpeedEntry, SpeedRest));
	TestEqual(TEXT("Cuesta abajo el tiempo del arrastre no corre"), FromRest.BellyTime, 0.f);
	TestTrue(TEXT("Cuenta como tiempo cuesta abajo"), FMath::IsNearlyEqual(FromRest.SlopeTime, 2.f, 0.02f));
	TestTrue(TEXT("Nunca pasa del tope"), SpeedEntry <= FromEntry.Defaults->BellyMaxSpeed + 0.5f);

	// Caso negativo: con la pendiente apagada (los ajustes de antes), la arena a 30° no acelera: se para.
	FSandSlide Before;
	Before.MinAngle = 90.f;
	const float BeforeRest = static_cast<float>(Before.After(FVector::ZeroVector, SlopeNormal(30.f), 1.f).Size());
	TestEqual(TEXT("Antes: arena a 30° desde parada no se mueve"), BeforeRest, 0.f);
	FSandSlide Before2;
	Before2.MinAngle = 90.f;
	TestTrue(TEXT("Antes: arena a 30° entrando a 720 se para"), Before2.TimeToStop(EntryVelocity, SlopeNormal(30.f)) < 2.f);

	// Subida a 20°: frena antes que en llano (la reducción es solo cuesta abajo).
	FSandSlide Flat;
	FSandSlide Up;
	const float FlatStop = Flat.TimeToStop(EntryVelocity, FVector::UpVector);
	const float UpStop = Up.TimeToStop(EntryVelocity, SlopeNormal(-20.f));
	TestTrue(FString::Printf(TEXT("Subida a 20° (%.2f s) frena antes que el llano (%.2f s)"), UpStop, FlatStop), UpStop < FlatStop);
	AddInfo(FString::Printf(TEXT("Parada desde 720 cm/s: subida a 20° en %.2f s, llano en %.2f s"), UpStop, FlatStop));
	TestTrue(TEXT("En subida el tiempo corre"), Up.SlopeTime == 0.f && Up.BellyTime > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNDiveSlopeRulesTest,
	"Tortunabo.Dive.Slope.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNDiveSlopeRulesTest::RunTest(const FString& Parameters)
{
	using namespace TNDiveLogic;
	using namespace TNDiveDecisionsTestDetail;

	// Desde 12°.
	TestFalse(TEXT("11° no es cuesta de caer"), IsFallSlope(SlopeNormal(11.f), 12.f));
	TestTrue(TEXT("12° sí"), IsFallSlope(SlopeNormal(12.1f), 12.f));
	TestFalse(TEXT("Con 90° nunca (apagado)"), IsFallSlope(SlopeNormal(40.f), 90.f));

	// El tiempo: corre en llano y en subida, no cuesta abajo (tampoco parada en la cuesta, que empieza a caer).
	const FVector N25 = SlopeNormal(25.f);
	TestTrue(TEXT("Llano: corre"), ShouldAdvanceBellyTimer(FVector::UpVector, FVector(500.0, 0.0, 0.0), 12.f));
	TestFalse(TEXT("Bajando: no corre"), ShouldAdvanceBellyTimer(N25, FVector(500.0, 0.0, 0.0), 12.f));
	TestFalse(TEXT("Parada en la cuesta: no corre"), ShouldAdvanceBellyTimer(N25, FVector::ZeroVector, 12.f));
	TestFalse(TEXT("Cruzando la cuesta: no corre"), ShouldAdvanceBellyTimer(N25, FVector(0.0, 500.0, 0.0), 12.f));
	TestTrue(TEXT("Subiendo: corre"), ShouldAdvanceBellyTimer(N25, FVector(-500.0, 0.0, 0.0), 12.f));
	TestTrue(TEXT("Cuesta suave: corre"), ShouldAdvanceBellyTimer(SlopeNormal(8.f), FVector(500.0, 0.0, 0.0), 12.f));

	// Casi parada en una cuesta: solo se levanta si la cuesta no la va a llevar más deprisa que BellyStopSpeed.
	const FSandSlide Sand;
	const float StopSpeed = Sand.Defaults->BellyStopSpeed;
	const float Terminal25 = DownhillTerminalSpeed(Sand.Input(N25));
	TestTrue(FString::Printf(TEXT("Arena a 25°: tiende a %.0f cm/s, sigue cayendo"), Terminal25), Terminal25 > 200.f);
	const float Terminal13 = DownhillTerminalSpeed(Sand.Input(SlopeNormal(13.f)));
	TestTrue(FString::Printf(TEXT("Arena a 13°: tiende a %.0f cm/s, se levanta"), Terminal13), Terminal13 <= StopSpeed);
	TestEqual(TEXT("Llano: 0"), DownhillTerminalSpeed(Sand.Input(FVector::UpVector)), 0.f);

	// Al caer de tripa: en llano, la horizontal con Keep y tope (como antes).
	const FVector Fall(600.0, 0.0, -700.0);
	const FVector OnFlat = LandingSlideVelocity(Fall, FVector::UpVector, 0.9f, 850.f, 1000.f, 12.f);
	TestTrue(TEXT("Llano: 90 % de la horizontal"), OnFlat.Equals(FVector(540.0, 0.0, 0.0), 0.01));
	TestTrue(TEXT("Llano: con tope"), FMath::IsNearlyEqual(LandingSlideVelocity(FVector(1200.0, 0.0, -300.0), FVector::UpVector, 1.f, 850.f, 1000.f, 12.f).X, 850.0, 0.01));
	// En una bajada de 25°: la caída que va a lo largo de la cuesta cuenta entera (módulo 3D), con su tope.
	const FVector Along = Fall - N25 * FVector::DotProduct(Fall, N25);
	const FVector Down = LandingSlideVelocity(Fall, N25, 0.9f, 850.f, 1000.f, 12.f);
	TestTrue(TEXT("Bajada: módulo 3D a lo largo de la cuesta"), FMath::IsNearlyEqual(Down.Size(), Along.Size() * 0.9, 0.01));
	TestTrue(TEXT("Bajada: más que solo la horizontal"), Down.Size() > FVector(Along.X, Along.Y, 0.0).Size() * 0.9 + 1.0);
	TestTrue(TEXT("Bajada: hacia abajo de la cuesta"), Down.X > 0.0 && FMath::IsNearlyZero(Down.Z));
	TestTrue(TEXT("Bajada: tope propio"), FMath::IsNearlyEqual(LandingSlideVelocity(FVector(1500.0, 0.0, -1500.0), N25, 1.f, 850.f, 1000.f, 12.f).Size(), 1000.0, 0.01));
	// Apagada (90°): como antes, solo la horizontal.
	const FVector DownOff = LandingSlideVelocity(Fall, N25, 0.9f, 850.f, 1000.f, 90.f);
	TestTrue(TEXT("Apagada: solo la horizontal"), FMath::IsNearlyEqual(DownOff.Size(), FVector(Along.X, Along.Y, 0.0).Size() * 0.9, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNDiveWallBounceTest,
	"Tortunabo.Dive.Wall.Bounce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNDiveWallBounceTest::RunTest(const FString& Parameters)
{
	using namespace TNDiveLogic;
	const FDiveWallParams Air = GetDefault<UTN_TurtleMovementComponent>()->GetDiveWallParams();
	TestEqual(TEXT("Restitución en vuelo 0,45"), Air.Restitution, 0.45f);
	TestEqual(TEXT("60 % a lo largo"), Air.TangentKeep, 0.6f);

	// Pared que mira hacia -X; el panzazo va hacia +X a 400 cm/s, algo de lado y cayendo.
	const FVector Wall(-1.0, 0.0, 0.0);
	const FVector V(400.0, 100.0, -250.0);
	TestTrue(TEXT("Pared a 400 cm/s: rebota"), ClassifyDiveImpact(Wall, V, FVector::ZeroVector, Air) == EDiveImpact::Bounce);
	const FVector Out = ReflectDiveVelocity(V, FVector::ZeroVector, WallNormal(Wall, Air.MaxNormalZ), Air);
	TestTrue(FString::Printf(TEXT("Sale hacia atrás con el 45 %% (%.0f)"), Out.X), FMath::IsNearlyEqual(Out.X, -180.0, 0.01));
	TestTrue(TEXT("Conserva el 60 % a lo largo de la pared"), FMath::IsNearlyEqual(Out.Y, 60.0, 0.01));
	TestEqual(TEXT("La vertical no cambia"), Out.Z, V.Z);
	AddInfo(FString::Printf(TEXT("Pared a 400 cm/s en vuelo: sale a (%.0f, %.0f) cm/s"), Out.X, Out.Y));

	// Lo que no es pared en vuelo: suelo, pendiente andable, techo; roce lento; objeto que se aleja igual de deprisa.
	TestTrue(TEXT("Suelo: nada"), ClassifyDiveImpact(FVector::UpVector, FVector(400.0, 0.0, -600.0), FVector::ZeroVector, Air) == EDiveImpact::None);
	const FVector Slope30(-0.5, 0.0, 0.866);
	TestTrue(TEXT("Pendiente de 30°: nada"), ClassifyDiveImpact(Slope30, V, FVector::ZeroVector, Air) == EDiveImpact::None);
	TestTrue(TEXT("Muy empinada (normal Z 0,3): pared"), ClassifyDiveImpact(FVector(-0.954, 0.0, 0.3), V, FVector::ZeroVector, Air) == EDiveImpact::Bounce);
	TestTrue(TEXT("Techo: nada"), ClassifyDiveImpact(FVector(-0.2, 0.0, -0.98), V, FVector::ZeroVector, Air) == EDiveImpact::None);
	TestTrue(TEXT("Roce a 100 cm/s: nada"), ClassifyDiveImpact(Wall, FVector(100.0, 400.0, 0.0), FVector::ZeroVector, Air) == EDiveImpact::None);
	TestTrue(TEXT("Objeto que se aleja a la misma velocidad: nada"), ClassifyDiveImpact(Wall, V, FVector(400.0, 0.0, 0.0), Air) == EDiveImpact::None);
	TestTrue(TEXT("Objeto que viene hacia ella: cuenta la relativa"), ClassifyDiveImpact(Wall, FVector(60.0, 0.0, 0.0), FVector(-200.0, 0.0, 0.0), Air) == EDiveImpact::Bounce);
	const FVector OutMoving = ReflectDiveVelocity(FVector(60.0, 0.0, 0.0), FVector(-200.0, 0.0, 0.0), Wall, Air);
	TestTrue(TEXT("Relativa: sale con el objeto más el 45 % de la relativa"), FMath::IsNearlyEqual(OutMoving.X, -200.0 - 0.45 * 260.0, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNDiveWallGroundTest,
	"Tortunabo.Dive.Wall.GroundUnchanged",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNDiveWallGroundTest::RunTest(const FString& Parameters)
{
	using namespace TNDiveLogic;
	// El rebote arrastrándose en el suelo sigue con sus ajustes (0,35 y 75 %) y da lo mismo que la cuenta de antes de #63.
	const UTN_TurtleMovementComponent* Defaults = GetDefault<UTN_TurtleMovementComponent>();
	const FDiveWallParams Ground = Defaults->GetBellyBounceParams();
	TestEqual(TEXT("Restitución en el suelo: la de siempre"), Ground.Restitution, Defaults->BellyBounceRestitution);
	TestEqual(TEXT("0,35"), Ground.Restitution, 0.35f);
	TestEqual(TEXT("75 % a lo largo"), Ground.TangentKeep, 0.75f);
	TestEqual(TEXT("Desde 120 cm/s"), Ground.MinSpeed, 120.f);

	const FVector N(-0.6, 0.8, 0.0);
	for (const FVector& Intent : { FVector(500.0, -100.0, 0.0), FVector(300.0, 0.0, 0.0), FVector(130.0, -60.0, 0.0) })
	{
		// La cuenta de antes (UTN_TurtleMovementComponent::OnMovementUpdated hasta #62).
		const double Into = FVector::DotProduct(Intent, N);
		const bool bOldBounces = -Into >= static_cast<double>(Defaults->BellyBounceMinSpeed);
		const FVector OldBounced = (Intent - N * Into) * static_cast<double>(Defaults->BellyBounceTangentKeep)
			- N * (Into * static_cast<double>(Defaults->BellyBounceRestitution));
		const bool bNewBounces = WallImpactSpeed(Intent, FVector::ZeroVector, N) >= Ground.MinSpeed;
		TestEqual(TEXT("Rebota en los mismos casos"), bNewBounces, bOldBounces);
		if (bOldBounces)
		{
			const FVector NewBounced = ReflectDiveVelocity(Intent, FVector::ZeroVector, N, Ground);
			TestTrue(TEXT("Con la misma velocidad"), FVector(NewBounced.X, NewBounced.Y, 0.0).Equals(FVector(OldBounced.X, OldBounced.Y, 0.0), 1e-6));
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
