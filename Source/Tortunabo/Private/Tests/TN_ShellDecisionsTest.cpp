// Fase 1 de la mecánica de caparazón (issue #6): lógica pura de entrada y salida.
// Sin mundo, sin componentes — se testean las funciones de TN_ShellDecisions.h que
// UTN_ShellComponent usa en producción. Correr desde Session Frontend (categoría
// "Tortunabo.Shell") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Shell; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_ShellDecisions.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNShellTestHelpers
{
	/** Contexto de un personaje sano, fuera del agua y con las manos libres: puede entrar. */
	static TNShellLogic::FShellEnterContext MakeValidContext()
	{
		TNShellLogic::FShellEnterContext Context;
		Context.bIsSwimming = false;
		Context.bIsDead = false;
		Context.bIsKnockedDown = false;
		Context.bIsDiving = false;
		Context.bHasEquippedItem = false;
		Context.bIsCarrying = false;
		return Context;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Entrar al caparazón
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNShellEnterTest,
	"Tortunabo.Shell.Enter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNShellEnterTest::RunTest(const FString& Parameters)
{
	using namespace TNShellLogic;
	using namespace TNShellTestHelpers;

	TestTrue(TEXT("Sano, fuera del agua y con las manos libres (de pie o en pleno salto) → entra"),
		CanEnterShell(MakeValidContext()));

	{
		FShellEnterContext Context = MakeValidContext();
		Context.bIsSwimming = true;
		TestFalse(TEXT("Nadando no se entra"), CanEnterShell(Context));
	}

	{
		FShellEnterContext Context = MakeValidContext();
		Context.bIsDead = true;
		TestFalse(TEXT("Muerto no se entra"), CanEnterShell(Context));
	}

	{
		FShellEnterContext Context = MakeValidContext();
		Context.bIsKnockedDown = true;
		TestFalse(TEXT("Derribado no se entra"), CanEnterShell(Context));
	}

	{
		FShellEnterContext Context = MakeValidContext();
		Context.bIsDiving = true;
		TestFalse(TEXT("Buceando no se entra"), CanEnterShell(Context));
	}

	{
		FShellEnterContext Context = MakeValidContext();
		Context.bHasEquippedItem = true;
		TestFalse(TEXT("Con un objeto en la mano no se entra"), CanEnterShell(Context));
	}

	{
		// #572: el servidor rechaza la bola del portador (o de la llevada) aunque el cliente no supiera aún que llevaba a nadie.
		FShellEnterContext Context = MakeValidContext();
		Context.bIsCarrying = true;
		TestFalse(TEXT("Llevando o llevada no se entra"), CanEnterShell(Context));
	}

	{
		// Varias condiciones a la vez: el resultado sigue siendo negativo.
		FShellEnterContext Context = MakeValidContext();
		Context.bIsSwimming = true;
		Context.bIsDead = true;
		Context.bHasEquippedItem = true;
		TestFalse(TEXT("Varios impedimentos a la vez → no entra"), CanEnterShell(Context));
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Salir del caparazón — permanencia mínima
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNShellExitTest,
	"Tortunabo.Shell.Exit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNShellExitTest::RunTest(const FString& Parameters)
{
	using namespace TNShellLogic;

	constexpr float MinTime = 0.3f;

	TestFalse(TEXT("Salir en el mismo instante de entrar → bloqueado"),
		CanExitShell(0.f, MinTime));

	TestFalse(TEXT("Salir antes del mínimo → bloqueado"),
		CanExitShell(0.29f, MinTime));

	TestTrue(TEXT("Salir justo en el mínimo → permitido"),
		CanExitShell(MinTime, MinTime));

	TestTrue(TEXT("Salir pasado el mínimo → permitido"),
		CanExitShell(5.f, MinTime));

	TestTrue(TEXT("Sin permanencia mínima configurada se sale siempre"),
		CanExitShell(0.f, 0.f));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Instrumento de la bola: torbellino, hundida y saltos de velocidad
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNShellMotionAnomalyTest,
	"Tortunabo.Shell.MotionAnomaly",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNShellMotionAnomalyTest::RunTest(const FString& Parameters)
{
	using namespace TNShellLogic;

	// Rodando normal por la arena (≈ 300 cm/s → ~12 rad/s), encima del terreno y con la velocidad suave: nada.
	{
		FShellMotionSample Sample;
		Sample.DeltaSeconds = 1.f / 60.f;
		Sample.AngularSpeed = 12.f;
		Sample.VelocityChange = 40.f;
		Sample.BottomDepthUnderTerrain = -2.f;
		Sample.AgeSeconds = 2.f;
		float Spin = 0.f;
		for (int32 Step = 0; Step < 120; ++Step)
		{
			TestTrue(TEXT("Rodar deprisa no es un torbellino"), ClassifyShellMotion(Sample, Spin) == EShellMotionAnomaly::None);
		}
		TestEqual(TEXT("Por debajo del umbral el acumulador de giro no crece"), Spin, 0.f);
	}

	// Girando al tope (15,7 rad/s): un rebote corto no cuenta; sostenido 0,4 s, sí.
	{
		FShellMotionSample Sample;
		Sample.DeltaSeconds = 0.1f;
		Sample.AngularSpeed = 15.7f;
		Sample.AgeSeconds = 2.f;
		float Spin = 0.f;
		TestTrue(TEXT("0,1 s al tope: todavía nada"), ClassifyShellMotion(Sample, Spin) == EShellMotionAnomaly::None);
		TestTrue(TEXT("0,2 s al tope: todavía nada"), ClassifyShellMotion(Sample, Spin) == EShellMotionAnomaly::None);
		TestTrue(TEXT("0,3 s al tope: todavía nada"), ClassifyShellMotion(Sample, Spin) == EShellMotionAnomaly::None);
		TestTrue(TEXT("0,4 s al tope: torbellino"), ClassifyShellMotion(Sample, Spin) == EShellMotionAnomaly::Spin);

		// Baja del umbral un paso: el acumulador vuelve a cero y hay que sostenerlo otra vez.
		Sample.AngularSpeed = 5.f;
		TestTrue(TEXT("Frena: nada"), ClassifyShellMotion(Sample, Spin) == EShellMotionAnomaly::None);
		TestEqual(TEXT("Frena: el acumulador vuelve a cero"), Spin, 0.f);
		Sample.AngularSpeed = 15.7f;
		TestTrue(TEXT("Vuelve al tope un paso: nada"), ClassifyShellMotion(Sample, Spin) == EShellMotionAnomaly::None);
	}

	// Bajo la arena: manda sobre todo lo demás; tocar la arena (unos cm) no cuenta.
	{
		FShellMotionSample Sample;
		Sample.DeltaSeconds = 1.f / 60.f;
		Sample.AgeSeconds = 2.f;
		float Spin = 0.f;
		Sample.BottomDepthUnderTerrain = 5.f;
		TestTrue(TEXT("5 cm dentro de la arena: nada"), ClassifyShellMotion(Sample, Spin) == EShellMotionAnomaly::None);
		Sample.BottomDepthUnderTerrain = 40.f;
		Sample.VelocityChange = 2000.f;
		TestTrue(TEXT("40 cm bajo el terreno: hundida (antes que el salto de velocidad)"), ClassifyShellMotion(Sample, Spin) == EShellMotionAnomaly::Sunk);
	}

	// Salto de velocidad: el del propio lanzamiento (recién nacida o recién relanzada) no cuenta; más tarde, sí.
	{
		FShellMotionSample Sample;
		Sample.DeltaSeconds = 1.f / 60.f;
		Sample.VelocityChange = 1500.f;
		float Spin = 0.f;
		Sample.AgeSeconds = 0.05f;
		TestTrue(TEXT("Salto en el lanzamiento: nada"), ClassifyShellMotion(Sample, Spin) == EShellMotionAnomaly::None);
		Sample.AgeSeconds = 1.f;
		TestTrue(TEXT("Salto sin lanzamiento: salto de velocidad"), ClassifyShellMotion(Sample, Spin) == EShellMotionAnomaly::VelocityJump);
		Sample.VelocityChange = 500.f;
		TestTrue(TEXT("Un choque normal (5 m/s de cambio): nada"), ClassifyShellMotion(Sample, Spin) == EShellMotionAnomaly::None);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNShellUnexplainedChangeTest,
	"Tortunabo.Shell.UnexplainedVelocityChange",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNShellUnexplainedChangeTest::RunTest(const FString& Parameters)
{
	using namespace TNShellLogic;
	const FShellMotionThresholds Thresholds;
	auto Classify = [](const FVector& Prev, const FVector& Velocity)
	{
		FShellMotionSample Sample;
		Sample.DeltaSeconds = 1.f / 60.f;
		Sample.VelocityChange = UnexplainedVelocityChange(Prev, Velocity);
		Sample.BottomDepthUnderTerrain = -5.f;
		Sample.AgeSeconds = 1.8f;
		float Spin = 0.f;
		return ClassifyShellMotion(Sample, Spin);
	};

	// Monkey del 2026-10-03 (semillas 202 y 303): la bola lanzada aterriza a 1,8 s de vuelo. Los 8 avisos eran esto.
	{
		const FVector Prev(1164.84, 0.0, -884.02);
		const FVector Velocity(1043.19, -1.72, 163.5);
		TestTrue(TEXT("Aterrizaje de una bola lanzada: el cambio bruto pasa del umbral"), (Velocity - Prev).Size() > Thresholds.VelocityJump);
		TestTrue(TEXT("Aterrizaje: lo no explicado es el rebote (< 1,5 m/s)"), UnexplainedVelocityChange(Prev, Velocity) < 150.f);
		TestTrue(TEXT("Aterrizaje: no es un salto de velocidad"), Classify(Prev, Velocity) == EShellMotionAnomaly::None);
	}
	{
		const FVector Prev(715.01, 0.0, -1159.54);
		const FVector Velocity(488.07, 47.91, 216.25);
		TestTrue(TEXT("Aterrizaje a 11,6 m/s hacia abajo: no es un salto de velocidad"), Classify(Prev, Velocity) == EShellMotionAnomaly::None);
	}
	{
		// Contra la pared de la fortaleza: se para de golpe.
		const FVector Prev(-1000.0, 150.0, -50.0);
		const FVector Velocity(180.0, 120.0, -60.0);
		TestTrue(TEXT("Choque contra una pared que la para y rebota: no es un salto"), Classify(Prev, Velocity) == EShellMotionAnomaly::None);
	}

	// La depenetración que la escupe (monkey del 2026-09-29) sí lo es, también si venía cayendo o rodando.
	{
		TestTrue(TEXT("Escupida desde quieta a 11,7 m/s: salto"), Classify(FVector::ZeroVector, FVector(0.0, 0.0, 1170.0)) == EShellMotionAnomaly::VelocityJump);
		TestTrue(TEXT("Escupida mientras cae a 3 m/s: salto"), Classify(FVector(0.0, 0.0, -300.0), FVector(0.0, 0.0, 1170.0)) == EShellMotionAnomaly::VelocityJump);
		TestTrue(TEXT("Escupida de lado mientras rueda: salto"), Classify(FVector(500.0, 0.0, 0.0), FVector(500.0, 1100.0, 200.0)) == EShellMotionAnomaly::VelocityJump);
		const float Spit = UnexplainedVelocityChange(FVector(0.0, 0.0, -300.0), FVector(0.0, 0.0, 1170.0));
		TestEqual(TEXT("Cayendo a 3 m/s y escupida a 11,7: no explicados 11,7 m/s"), Spit, 1170.f, 0.5f);
	}
	TestEqual(TEXT("Sin cambio: 0"), UnexplainedVelocityChange(FVector(300.0, 0.0, 0.0), FVector(300.0, 0.0, 0.0)), 0.f);
	TestEqual(TEXT("Solo frena: 0"), UnexplainedVelocityChange(FVector(800.0, 0.0, 0.0), FVector(300.0, 0.0, 0.0)), 0.f);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bola bajo el terreno: recolocación en el servidor
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNShellSunkRescueTest,
	"Tortunabo.Shell.SunkRescue",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNShellSunkRescueTest::RunTest(const FString& Parameters)
{
	using namespace TNShellLogic;

	// Las cinco bolas del registro del 29-09 estaban 1,7-2,1 m bajo la arena: se recolocan a la segunda muestra.
	{
		int32 Strikes = 0;
		TestFalse(TEXT("Primera muestra a 170 cm: todavía no (un paso de la física no cuenta)"), ShouldRescueSunkenBody(170.f, Strikes));
		TestTrue(TEXT("Segunda muestra seguida a 180 cm: se recoloca"), ShouldRescueSunkenBody(180.f, Strikes));
		TestEqual(TEXT("Tras recolocarla el contador vuelve a cero"), Strikes, 0);
	}
	{
		int32 Strikes = 0;
		TestFalse(TEXT("Rozando la arena (20 cm) no cuenta"), ShouldRescueSunkenBody(20.f, Strikes));
		TestFalse(TEXT("Una muestra hundida..."), ShouldRescueSunkenBody(40.f, Strikes));
		TestFalse(TEXT("...y sale sola: se olvida"), ShouldRescueSunkenBody(-5.f, Strikes));
		TestFalse(TEXT("Otra hundida suelta: todavía no"), ShouldRescueSunkenBody(40.f, Strikes));
	}

	TestTrue(TEXT("Subir 2 m es recolocarla en el sitio"), IsSunkLiftAllowed(200.f));
	TestFalse(TEXT("Subir 6 m es un teletransporte: a la red de seguridad"), IsSunkLiftAllowed(600.f));
	TestFalse(TEXT("Bajar nunca"), IsSunkLiftAllowed(-10.f));

	{
		FVector Linear(300.0, 0.0, -900.0);
		FVector Angular(0.0, 0.0, 15.7);
		SettleRescuedVelocity(Linear, Angular);
		TestEqual(TEXT("Sin velocidad hacia abajo"), Linear.Z, 0.0);
		TestEqual(TEXT("Conserva la horizontal"), Linear.X, 300.0);
		TestTrue(TEXT("Giro limitado a 6 rad/s"), Angular.Size() <= 6.0 + KINDA_SMALL_NUMBER);
	}
	{
		FVector Linear(0.0, 0.0, 250.0);
		FVector Angular(1.0, 0.0, 0.0);
		SettleRescuedVelocity(Linear, Angular);
		TestEqual(TEXT("Si subía, sigue subiendo"), Linear.Z, 250.0);
		TestEqual(TEXT("Un giro suave no se toca"), Angular.X, 1.0);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bloques sólidos de los enemigos: empuje propio sobre la bola
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNShellBlockPushTest,
	"Tortunabo.Shell.EnemyBlockPush",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNShellBlockPushTest::RunTest(const FString& Parameters)
{
	using namespace TNShellLogic;
	const FVector Extent(150.0, 100.0, 60.0);
	const float Radius = 25.f;
	const float Speed = 350.f;

	{
		FVector Velocity(0.0, 0.0, -400.0);
		TestFalse(TEXT("Lejos del bloque: nada"), PushBallOutOfBlock(FVector(400.0, 0.0, 0.0), Extent, Radius, Speed, Velocity));
		TestEqual(TEXT("Lejos del bloque: no toca su velocidad"), Velocity.Z, -400.0);
	}
	{
		// El caso del torbellino: el bloque baja sobre la bola, que queda debajo de su borde y la aplasta contra la arena.
		FVector Velocity(0.0, 0.0, -600.0);
		TestTrue(TEXT("Bajo el borde del bloque: la saca"), PushBallOutOfBlock(FVector(160.0, 20.0, -70.0), Extent, Radius, Speed, Velocity));
		TestTrue(TEXT("Nunca hacia abajo"), Velocity.Z >= 0.0);
		TestTrue(TEXT("Sale por el lado más cercano (+X) a la velocidad mínima"), Velocity.X >= Speed - KINDA_SMALL_NUMBER);
		TestEqual(TEXT("Sin empujón de lado"), Velocity.Y, 0.0);
	}
	{
		FVector Velocity(0.0, 0.0, 0.0);
		TestTrue(TEXT("Dentro, junto a la cara -Y"), PushBallOutOfBlock(FVector(10.0, -110.0, 0.0), Extent, Radius, Speed, Velocity));
		TestTrue(TEXT("Sale por -Y"), Velocity.Y <= -Speed + KINDA_SMALL_NUMBER);
	}
	{
		FVector Velocity(800.0, 0.0, 0.0);
		TestTrue(TEXT("Ya sale deprisa por +X"), PushBallOutOfBlock(FVector(160.0, 0.0, 0.0), Extent, Radius, Speed, Velocity));
		TestEqual(TEXT("No la frena"), Velocity.X, 800.0);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// La caja nace encima del terreno y el terreno no la escupe (#54)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNShellSpawnAboveTerrainTest,
	"Tortunabo.Shell.SpawnAboveTerrain",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNShellSpawnAboveTerrainTest::RunTest(const FString& Parameters)
{
	using namespace TNShellLogic;
	const FVector Extent(27.5, 23.0, 21.0);
	TestEqual(TEXT("Tumbada: su parte de abajo, a 21 cm del centro"), BoxHalfHeight(FQuat::Identity, Extent), 21.0, 0.01);
	TestEqual(TEXT("De pie (como nace a mano): a 27,5 cm"), BoxHalfHeight(FRotator(90.f, 35.f, 0.f).Quaternion(), Extent), 27.5, 0.01);

	TestEqual(TEXT("Encima de la arena: no se sube"), SpawnLiftAboveTerrain(-30.f), 0.f);
	TestEqual(TEXT("Apoyada en la arena: no se sube"), SpawnLiftAboveTerrain(0.f), 0.f);
	TestEqual(TEXT("Sin terreno o junto al acantilado (-1000): no se sube"), SpawnLiftAboveTerrain(-1000.f), 0.f);
	// Lo del monkey: la parte de abajo 14-127 cm dentro de la malla del generador.
	TestEqual(TEXT("14 cm dentro: se sube 16 (2 por encima)"), SpawnLiftAboveTerrain(14.f), 16.f);
	TestEqual(TEXT("127 cm dentro (entera por debajo): se sube 129"), SpawnLiftAboveTerrain(127.f), 129.f);
	TestTrue(TEXT("Subida, queda encima"), IsBottomAboveTerrain(127.f - SpawnLiftAboveTerrain(127.f)));
	TestTrue(TEXT("Más de 4 m dentro: no es encima de donde estaba, se busca otro sitio"), SpawnLiftAboveTerrain(500.f) < 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNShellTerrainPushOutTest,
	"Tortunabo.Shell.TerrainPushOut",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNShellTerrainPushOutTest::RunTest(const FString& Parameters)
{
	using namespace TNShellLogic;
	// La caja pesa 38 kg (ATN_ShellBody). Umbral: el impulso que da la depenetración además de parar la caída no pasa del de
	// salir a 300 cm/s (el tope al nacer metida en algo).
	const double Mass = 38.0;
	const FTerrainPushOutRules Rules;
	const double MaxImpulse = Mass * Rules.MaxPushOutSpeed + 1.0;
	auto PushOutImpulse = [Mass](const FVector& Prev, const FVector& After)
	{
		const FVector Allowed(Prev.X, Prev.Y, FMath::Max(Prev.Z, 0.0));
		return Mass * (After - Allowed).Size();
	};

	{
		// Monkey del 2026-09-29: 50 cm dentro, cayendo a 3 m/s, y el terreno la saca a 11,7 m/s (impulso de 55 860).
		const FVector Prev(0.0, 0.0, -300.0);
		FVector Velocity(0.0, 0.0, 1170.0);
		TestTrue(TEXT("Metida y escupida hacia arriba: se limita"), LimitTerrainPushOut(Prev, 50.f, Velocity, Rules));
		TestTrue(TEXT("Impulso de la depenetración por debajo del umbral"), PushOutImpulse(Prev, Velocity) <= MaxImpulse);
		TestEqual(TEXT("Sale a 300 cm/s hacia arriba"), Velocity.Z, 300.0, 0.5);
		TestTrue(TEXT("El instrumento no lo vería como salto (menos de 9 m/s)"), (Velocity - Prev).Size() < 900.0);
	}
	{
		// Junto a la fortaleza: 93 cm dentro e impulso de 432 690 (más de 100 m/s), casi todo de lado.
		const FVector Prev(200.0, 0.0, -100.0);
		FVector Velocity(-10800.0, 3500.0, 900.0);
		TestTrue(TEXT("Escupida de lado: se limita"), LimitTerrainPushOut(Prev, 93.f, Velocity, Rules));
		TestTrue(TEXT("Impulso por debajo del umbral"), PushOutImpulse(Prev, Velocity) <= MaxImpulse);
	}
	{
		// Aterrizaje normal: encima de la arena antes del paso; el rebote es cosa del material.
		const FVector Prev(300.0, 0.0, -1200.0);
		FVector Velocity(250.0, 0.0, 240.0);
		TestFalse(TEXT("Aterrizaje desde encima: no se toca"), LimitTerrainPushOut(Prev, -5.f, Velocity, Rules));
		TestEqual(TEXT("Aterrizaje: el rebote sigue igual"), Velocity.Z, 240.0);
	}
	{
		// Patada o cama elástica sobre la arena: no estaba metida.
		const FVector Prev(0.0, 0.0, 0.0);
		FVector Velocity(400.0, 0.0, 1500.0);
		TestFalse(TEXT("Lanzada desde encima de la arena: no se toca"), LimitTerrainPushOut(Prev, 0.f, Velocity, Rules));
		TestEqual(TEXT("Lanzada: la velocidad sigue igual"), Velocity.Z, 1500.0);
	}
	{
		const FVector Prev(0.0, 0.0, -900.0);
		FVector Velocity(0.0, 0.0, 0.0);
		TestFalse(TEXT("Metida, el terreno solo para la caída: no se toca"), LimitTerrainPushOut(Prev, 40.f, Velocity, Rules));
	}
	{
		const FVector Prev(800.0, 0.0, 0.0);
		FVector Velocity(300.0, 0.0, 50.0);
		TestFalse(TEXT("Metida, el terreno la frena: no se toca"), LimitTerrainPushOut(Prev, 40.f, Velocity, Rules));
		TestEqual(TEXT("Frenada: sigue frenada"), Velocity.X, 300.0);
	}
	{
		const FVector Prev(400.0, 0.0, 0.0);
		FVector Velocity(450.0, 0.0, 100.0);
		TestFalse(TEXT("Metida, sale despacio (menos de 3 m/s de más): no se toca"), LimitTerrainPushOut(Prev, 20.f, Velocity, Rules));
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pose de la bola de un cliente en el anfitrión (#246)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNShellRemotePoseTest,
	"Tortunabo.Shell.RemotePose",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNShellRemotePoseTest::RunTest(const FString& Parameters)
{
	using TNShellLogic::OnlyTickPoseFromClientMoves;

	// Andando, la tortuga de un cliente se anima en el servidor con sus movimientos (lo de serie del motor).
	TestTrue(TEXT("Cliente remoto andando: con sus movimientos"), OnlyTickPoseFromClientMoves(true, false));
	// En la bola su cliente ya no manda movimientos: se anima cada fotograma (si no, se queda con la pose de antes de meterse).
	TestFalse(TEXT("Cliente remoto en la bola: cada fotograma"), OnlyTickPoseFromClientMoves(true, true));
	// La propia del anfitrión y todas en los clientes se animan siempre cada fotograma.
	TestFalse(TEXT("Local o en un cliente, andando"), OnlyTickPoseFromClientMoves(false, false));
	TestFalse(TEXT("Local o en un cliente, en la bola"), OnlyTickPoseFromClientMoves(false, true));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
