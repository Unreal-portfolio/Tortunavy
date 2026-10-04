// Doble salto físico (plan maestro §3.5): las cuentas del panzazo de TNDiveLogic (TN_DiveDecisions.h) con los ajustes por
// defecto de UTN_TurtleMovementComponent, y el inicio del panzazo en el movimiento guardado (#24). Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Dive; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Player/TN_DiveDecisions.h"
#include "Player/TN_ShellBody.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "Player/TortugaCharacter.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"

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

	/** Mundo de juego sin empezar (nada recibe BeginPlay) para tortugas del personaje de C++. */
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNDiveTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
		}

		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		ATortugaCharacter* SpawnTurtle() const
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<ATortugaCharacter>(FVector(0.0, 0.0, 500.0), FRotator::ZeroRotator, Params);
		}
	};
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNDiveWallSplatTest,
	"Tortunabo.Dive.Wall.Splat",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNDiveWallSplatTest::RunTest(const FString& Parameters)
{
	using namespace TNDiveLogic;
	// Estampado (#355): desde 650 cm/s contra la pared; por debajo, el rebote de #63; en una pendiente, nada.
	const FDiveWallParams Air = GetDefault<UTN_TurtleMovementComponent>()->GetDiveWallParams();
	TestEqual(TEXT("Se estampa desde 650 cm/s"), Air.SplatMinSpeed, 650.f);

	const FVector Wall(-1.0, 0.0, 0.0);
	TestTrue(TEXT("Pared a 400 cm/s: rebota"), ClassifyDiveImpact(Wall, FVector(400.0, 0.0, -250.0), FVector::ZeroVector, Air) == EDiveImpact::Bounce);
	TestTrue(TEXT("Pared a 649 cm/s: aún rebota"), ClassifyDiveImpact(Wall, FVector(649.0, 0.0, -250.0), FVector::ZeroVector, Air) == EDiveImpact::Bounce);
	TestTrue(TEXT("Pared a 650 cm/s: se estampa"), ClassifyDiveImpact(Wall, FVector(650.0, 0.0, -250.0), FVector::ZeroVector, Air) == EDiveImpact::Splat);
	const FVector V700(700.0, 150.0, -300.0);
	TestTrue(TEXT("Pared a 700 cm/s: se estampa"), ClassifyDiveImpact(Wall, V700, FVector::ZeroVector, Air) == EDiveImpact::Splat);
	// Lo que cuenta es la velocidad contra la pared, no el módulo: 700 casi de lado es un roce.
	TestTrue(TEXT("700 cm/s rozando la pared (300 contra ella): rebota"),
		ClassifyDiveImpact(Wall, FVector(300.0, 630.0, 0.0), FVector::ZeroVector, Air) == EDiveImpact::Bounce);

	// La bola sale con la velocidad reflejada (la misma cuenta del rebote): hacia fuera de la pared, la vertical sigue.
	const FVector Ball = ReflectDiveVelocity(V700, FVector::ZeroVector, WallNormal(Wall, Air.MaxNormalZ), Air);
	TestTrue(FString::Printf(TEXT("La bola sale hacia fuera de la pared (%.0f)"), Ball.X), FMath::IsNearlyEqual(Ball.X, -315.0, 0.01));
	TestTrue(TEXT("Con el 60 % a lo largo"), FMath::IsNearlyEqual(Ball.Y, 90.0, 0.01));
	TestEqual(TEXT("La vertical no cambia"), Ball.Z, V700.Z);

	// Pendientes, suelo y techo, a cualquier velocidad: nada (ni rebote ni estampado).
	TestTrue(TEXT("Pendiente de 30° a 900 cm/s: nada"), ClassifyDiveImpact(FVector(-0.5, 0.0, 0.866), FVector(900.0, 0.0, -400.0), FVector::ZeroVector, Air) == EDiveImpact::None);
	TestTrue(TEXT("Pendiente de 60° a 900 cm/s: nada"), ClassifyDiveImpact(FVector(-0.866, 0.0, 0.5), FVector(900.0, 0.0, -400.0), FVector::ZeroVector, Air) == EDiveImpact::None);
	TestTrue(TEXT("Suelo a 900 cm/s: nada"), ClassifyDiveImpact(FVector::UpVector, FVector(900.0, 0.0, -900.0), FVector::ZeroVector, Air) == EDiveImpact::None);

	// Velocidad relativa: un objeto que viene hacia ella suma.
	TestTrue(TEXT("400 contra un objeto que viene a 300: se estampa"), ClassifyDiveImpact(Wall, FVector(400.0, 0.0, 0.0), FVector(-300.0, 0.0, 0.0), Air) == EDiveImpact::Splat);

	// Apagado (0) y el arrastre en el suelo: nunca se estampa.
	FDiveWallParams Off = Air;
	Off.SplatMinSpeed = 0.f;
	TestTrue(TEXT("Sin estampado: a 900 rebota"), ClassifyDiveImpact(Wall, FVector(900.0, 0.0, 0.0), FVector::ZeroVector, Off) == EDiveImpact::Bounce);
	TestEqual(TEXT("El rebote en el suelo no se estampa"), GetDefault<UTN_TurtleMovementComponent>()->GetBellyBounceParams().SplatMinSpeed, 0.f);

	// Fuerza del golpe: 0,55 en el umbral, 1 al doble.
	TestTrue(TEXT("Fuerza en el umbral"), FMath::IsNearlyEqual(SplatStrength(650.f, Air), 0.55f, 1e-4f));
	TestTrue(TEXT("Fuerza al doble"), FMath::IsNearlyEqual(SplatStrength(1300.f, Air), 1.f, 1e-4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNDiveWallSplatBallTest,
	"Tortunabo.Dive.Wall.SplatBall",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNDiveWallSplatBallTest::RunTest(const FString& Parameters)
{
	using namespace TNDiveLogic;
	// Estampado (#355) en el servidor: el movimiento solo lo apunta; ServerDiveSplat (fuera del movimiento) acaba el panzazo
	// y la lanza como bola con la velocidad reflejada.
	TNDiveDecisionsTestDetail::FTestWorld TestWorld;
	ATortugaCharacter* Turtle = TestWorld.SpawnTurtle();
	if (!TestNotNull(TEXT("Tortuga"), Turtle) || !TestNotNull(TEXT("Caparazón"), Turtle->GetShellComponent()))
	{
		return false;
	}
	const FVector BallVelocity(-315.0, 90.0, -300.0);
	const FVector Where = Turtle->GetActorLocation() + FVector(40.0, 0.0, 0.0);

	// Sin panzazo no se apunta.
	Turtle->NoteDiveSplat(BallVelocity, Where, FVector(-1.0, 0.0, 0.0), 0.8f);
	TestFalse(TEXT("Sin panzazo: nada apuntado"), Turtle->HasPendingDiveSplat());

	FVector Launch = FVector::ZeroVector;
	Turtle->SetJumpStartHorizontalVelocity(FVector(600.0, 0.0, 0.0));
	if (!TestTrue(TEXT("Empieza el panzazo"), Turtle->StartDiveFromMove(CompressDiveYaw(FVector::ForwardVector), true, false, true, FVector::ZeroVector, Launch)))
	{
		return false;
	}
	Turtle->NoteDiveSplat(BallVelocity, Where, FVector(-1.0, 0.0, 0.0), 0.8f);
	TestTrue(TEXT("El movimiento lo apunta"), Turtle->HasPendingDiveSplat());
	TestTrue(TEXT("Apuntarlo no crea la bola"), Turtle->GetShellComponent()->GetBody() == nullptr && !Turtle->IsInShell());
	TestTrue(TEXT("Ni acaba el panzazo"), Turtle->IsDiving());

	Turtle->ServerDiveSplat();
	TestFalse(TEXT("Hecho: ya no queda apuntado"), Turtle->HasPendingDiveSplat());
	TestFalse(TEXT("Acaba el panzazo"), Turtle->IsDiving());
	TestTrue(TEXT("En el caparazón"), Turtle->IsInShell());
	const ATN_ShellBody* Body = Turtle->GetShellComponent()->GetBody();
	if (TestNotNull(TEXT("Con bola"), Body) && Body->GetBox())
	{
		const FVector BodyVelocity = Body->GetBox()->GetPhysicsLinearVelocity();
		TestTrue(FString::Printf(TEXT("Con la velocidad reflejada (%s)"), *BodyVelocity.ToString()), BodyVelocity.Equals(BallVelocity, 1.0));
	}
	TestTrue(TEXT("Sin salir en el aire"), Turtle->GetShellComponent()->IsExitLocked());

	// Otro estampado apuntado para un panzazo que ya no está: nada.
	Turtle->NoteDiveSplat(BallVelocity, Where, FVector(-1.0, 0.0, 0.0), 0.8f);
	TestFalse(TEXT("Tras el estampado no se apunta otro"), Turtle->HasPendingDiveSplat());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNDiveStartRulesTest,
	"Tortunabo.Dive.Start.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNDiveStartRulesTest::RunTest(const FString& Parameters)
{
	using namespace TNDiveLogic;
	// Las reglas con que deciden el servidor y el dueño, en el mismo movimiento: solo en el aire, sin otro panzazo, libre,
	// sin otro lanzamiento en el paso y con velocidad.
	FDiveStartContext Air;
	Air.bInAir = true;
	Air.ForwardSpeed = 420.f;
	Air.MinSpeed = 80.f;
	TestTrue(TEXT("En el aire: empieza"), DecideDiveStart(Air) == EDiveStart::Accept);

	FDiveStartContext Ground = Air;
	Ground.bInAir = false;
	TestTrue(TEXT("En el suelo (o nadando): no"), DecideDiveStart(Ground) == EDiveStart::NotInAir);
	FDiveStartContext Again = Air;
	Again.bAlreadyDiving = true;
	TestTrue(TEXT("Ya en un panzazo (en el aire): no"), DecideDiveStart(Again) == EDiveStart::AlreadyDiving);
	FDiveStartContext Blocked = Air;
	Blocked.bBlocked = true;
	TestTrue(TEXT("Derribada, en el caparazón o en brazos: no"), DecideDiveStart(Blocked) == EDiveStart::Blocked);
	FDiveStartContext Launch = Air;
	Launch.bLaunchPending = true;
	TestTrue(TEXT("Otro lanzamiento en el paso: no"), DecideDiveStart(Launch) == EDiveStart::LaunchPending);
	FDiveStartContext Slow = Air;
	Slow.ForwardSpeed = 50.f;
	TestTrue(TEXT("Sin velocidad: no"), DecideDiveStart(Slow) == EDiveStart::TooSlow);
	FDiveStartContext Backwards = Air;
	Backwards.ForwardSpeed = -300.f;
	TestTrue(TEXT("Hacia atrás con velocidad: sí"), DecideDiveStart(Backwards) == EDiveStart::Accept);

	// Velocidad: la base más la inercia del salto según la dirección (la cuenta de Server_StartDive hasta #24).
	FDiveMomentumParams P;
	const FVector Jump(600.0, 0.0, 0.0);
	TestEqual(TEXT("Sin salto: la base"), DiveForwardSpeed(FVector::ForwardVector, FVector::ZeroVector, P), 420.f);
	TestEqual(TEXT("A favor del salto: base + salto"), DiveForwardSpeed(FVector::ForwardVector, Jump, P), 1020.f);
	TestEqual(TEXT("De lado: la base"), DiveForwardSpeed(FVector::RightVector, Jump, P), 420.f);
	TestEqual(TEXT("Contra el salto: base - la mitad"), DiveForwardSpeed(-FVector::ForwardVector, Jump, P), 120.f);
	TestEqual(TEXT("Con tope"), DiveForwardSpeed(FVector::ForwardVector, FVector(2000.0, 0.0, 0.0), P), 1500.f);
	const FVector Launched = DiveLaunchVelocity(FVector::ForwardVector, 1020.f, 200.f);
	TestTrue(TEXT("Sale hacia delante y hacia abajo"), Launched.Equals(FVector(1020.0, 0.0, -200.0), 1e-3));

	// Con los ajustes del personaje: corriendo a 840 cm/s y lanzándose hacia atrás, la inercia anula la base: no empieza.
	const ATortugaCharacter* Defaults = GetDefault<ATortugaCharacter>();
	FDiveStartContext Cancelled = Air;
	Cancelled.ForwardSpeed = DiveForwardSpeed(-FVector::ForwardVector, FVector(840.0, 0.0, 0.0), Defaults->GetDiveMomentumParams());
	Cancelled.MinSpeed = 80.f;
	TestTrue(FString::Printf(TEXT("Inercia que anula el panzazo (%.0f cm/s): no empieza"), Cancelled.ForwardSpeed), DecideDiveStart(Cancelled) == EDiveStart::TooSlow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNDiveStartNetTest,
	"Tortunabo.Dive.Start.NetData",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNDiveStartNetTest::RunTest(const FString& Parameters)
{
	using namespace TNDiveLogic;
	// El giro en 16 bits: la dirección que usan el dueño y el servidor es la misma (la del giro comprimido).
	for (const float Yaw : { 0.f, 37.f, -120.5f, 179.9f, 271.f })
	{
		const FVector Dir = FRotator(0.f, Yaw, 0.f).Vector();
		const FVector Back = DiveDirFromYaw(CompressDiveYaw(Dir));
		TestTrue(FString::Printf(TEXT("Giro %.1f: misma dirección"), Yaw), Back.Equals(Dir, 1e-3));
		TestTrue(TEXT("Horizontal y unitaria"), FMath::IsNearlyZero(Back.Z) && FMath::IsNearlyEqual(Back.Size(), 1.0, 1e-4));
		TestEqual(TEXT("Comprimir lo ya comprimido no cambia"), CompressDiveYaw(Back), CompressDiveYaw(Dir));
	}

	// En los datos del movimiento: con la marca, 16 bits de giro; sin ella, nada.
	const uint16 Sent = CompressDiveYaw(FRotator(0.f, 37.f, 0.f).Vector());
	FBitWriter Writer(64, true);
	uint16 WithFlag = Sent;
	SerializeDiveRequest(Writer, DiveRequestFlag, WithFlag);
	TestEqual(TEXT("Con la marca: 16 bits"), static_cast<int32>(Writer.GetNumBits()), 16);
	uint16 WithoutFlag = Sent;
	SerializeDiveRequest(Writer, 0, WithoutFlag);
	TestEqual(TEXT("Sin la marca: nada"), static_cast<int32>(Writer.GetNumBits()), 16);
	TestEqual(TEXT("Sin la marca, sin giro"), static_cast<int32>(WithoutFlag), 0);
	FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
	uint16 Received = 0;
	SerializeDiveRequest(Reader, DiveRequestFlag, Received);
	TestEqual(TEXT("Llega el mismo giro"), Received, Sent);
	TestFalse(TEXT("Sin errores"), Reader.IsError());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNDiveStartSavedMoveTest,
	"Tortunabo.Dive.Start.SavedMove",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNDiveStartSavedMoveTest::RunTest(const FString& Parameters)
{
	using namespace TNDiveLogic;
	// Falla si el panzazo deja de ir en el movimiento guardado: su marca, su giro, lo que viaja por red y su repetición.
	TNDiveDecisionsTestDetail::FTestWorld TestWorld;
	ATortugaCharacter* Turtle = TestWorld.SpawnTurtle();
	UTN_TurtleMovementComponent* Move = Turtle ? Turtle->GetTurtleMovement() : nullptr;
	FNetworkPredictionData_Client_Character* Data = Move ? Move->GetPredictionData_Client_Character() : nullptr;
	if (!TestNotNull(TEXT("Tortuga con su movimiento"), Data))
	{
		return false;
	}
	constexpr float Dt = 1.f / 60.f;

	FSavedMovePtr Plain = Data->CreateSavedMove();
	Plain->SetMoveFor(Turtle, Dt, FVector::ZeroVector, *Data);
	TestEqual(TEXT("Sin pedirlo: sin marca"), Plain->GetCompressedFlags() & DiveRequestFlag, 0);

	const FVector DiveDir = FRotator(0.f, 37.f, 0.f).Vector();
	const uint16 Yaw = CompressDiveYaw(DiveDir);
	Move->RequestDive(DiveDir);
	TestTrue(TEXT("Pedido"), Move->HasDiveRequest());
	FSavedMovePtr DiveMove = Data->CreateSavedMove();
	DiveMove->SetMoveFor(Turtle, Dt, FVector::ZeroVector, *Data);
	TestEqual(TEXT("El movimiento guardado lleva la marca FLAG_Custom_2"), DiveMove->GetCompressedFlags() & DiveRequestFlag, static_cast<int32>(DiveRequestFlag));
	TestEqual(TEXT("Y el giro"), UTN_TurtleMovementComponent::GetSavedMoveDiveYaw(*DiveMove), Yaw);
	TestTrue(TEXT("Es importante (se reenvía si se pierde)"), DiveMove->IsImportantMove(Plain));
	TestFalse(TEXT("No se junta con uno sin panzazo"), Plain->CanCombineWith(DiveMove, Turtle, 1.f));

	// Lo que viaja al servidor.
	FTNTurtleNetworkMoveData NetData;
	NetData.ClientFillNetworkMoveData(*DiveMove, FCharacterNetworkMoveData::ENetworkMoveType::NewMove);
	TestEqual(TEXT("Por red va la marca"), NetData.CompressedMoveFlags & DiveRequestFlag, static_cast<int32>(DiveRequestFlag));
	TestEqual(TEXT("Por red va el giro"), NetData.DiveYaw, Yaw);
	FTNTurtleNetworkMoveData PlainNet;
	PlainNet.ClientFillNetworkMoveData(*Plain, FCharacterNetworkMoveData::ENetworkMoveType::NewMove);
	TestEqual(TEXT("Sin panzazo, sin giro"), static_cast<int32>(PlainNet.DiveYaw), 0);

	// Al repetirlo tras una corrección vuelve el giro que se pidió (la marca la leen las del movimiento).
	Move->RequestDive(-DiveDir);
	DiveMove->PrepMoveFor(Turtle);
	TestEqual(TEXT("Al repetir: el giro de entonces"), Move->GetMoveDiveYaw(), Yaw);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNDiveStartMoveTest,
	"Tortunabo.Dive.Start.InMove",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNDiveStartMoveTest::RunTest(const FString& Parameters)
{
	using namespace TNDiveLogic;
	// Lo que hacen el servidor y el dueño en el movimiento con la petición (ATortugaCharacter::StartDiveFromMove): la misma
	// decisión, la misma velocidad, el mismo número y la cápsula tumbada. En el suelo, nada.
	TNDiveDecisionsTestDetail::FTestWorld TestWorld;
	ATortugaCharacter* Turtle = TestWorld.SpawnTurtle();
	if (!TestNotNull(TEXT("Tortuga"), Turtle))
	{
		return false;
	}
	const ATortugaCharacter* Defaults = GetDefault<ATortugaCharacter>();
	const uint16 Yaw = CompressDiveYaw(FVector::ForwardVector);
	const float StandHalf = Turtle->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	FVector Launch = FVector::ZeroVector;

	TestFalse(TEXT("En el suelo no empieza"), Turtle->StartDiveFromMove(Yaw, false, false, true, FVector::ZeroVector, Launch));
	TestFalse(TEXT("Ni cuenta"), Turtle->IsDiving() || Turtle->GetDiveSerial() != 0);
	TestFalse(TEXT("Con otro lanzamiento en el paso tampoco"), Turtle->StartDiveFromMove(Yaw, true, true, true, FVector::ZeroVector, Launch));

	// Desde un salto hacia delante a 300 cm/s.
	Turtle->SetJumpStartHorizontalVelocity(FVector(300.0, 0.0, 0.0));
	TestTrue(TEXT("En el aire empieza"), Turtle->StartDiveFromMove(Yaw, true, false, true, FVector(300.0, 0.0, 400.0), Launch));
	const float Forward = DiveForwardSpeed(DiveDirFromYaw(Yaw), FVector(300.0, 0.0, 0.0), Defaults->GetDiveMomentumParams());
	const FVector Expected = DiveLaunchVelocity(DiveDirFromYaw(Yaw), Forward, static_cast<float>(-Launch.Z));
	TestTrue(FString::Printf(TEXT("Con la velocidad de las reglas (%s)"), *Launch.ToString()), Launch.Equals(Expected, 1e-3) && Launch.Z < 0.0);
	TestTrue(TEXT("En panzazo, el nº 1"), Turtle->IsDiving() && Turtle->GetDiveSerial() == 1);
	TestTrue(TEXT("Cápsula tumbada"), Turtle->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() < StandHalf - 1.f);
	TestFalse(TEXT("Otro en el mismo vuelo: no"), Turtle->StartDiveFromMove(Yaw, true, false, true, FVector::ZeroVector, Launch));

	// La corrección del servidor manda: si él no lo empezó, el dueño vuelve a su estado y lo repite desde ahí.
	Turtle->ApplyServerDiveCorrection(false, 0);
	TestTrue(TEXT("Sin panzazo tras la corrección"), !Turtle->IsDiving() && Turtle->GetDiveSerial() == 0);
	TestTrue(TEXT("Al repetir el movimiento vuelve a empezar igual"), Turtle->StartDiveFromMove(Yaw, true, false, true, FVector::ZeroVector, Launch));
	TestEqual(TEXT("Con el mismo número"), static_cast<int32>(Turtle->GetDiveSerial()), 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
