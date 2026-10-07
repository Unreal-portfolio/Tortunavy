// Lógica pura de las gaviotas (ronda 4, tarea 5: el nerf; TN_BeachGullTuning.h), con las velocidades de verdad de la
// tortuga (las del Blueprint: andando 200 cm/s, corriendo 400). Lo que se pidió: andando te pilla; esprintando en línea
// recta le ganas distancia a la sombra y te libras (#636), y también cambiando de dirección corriendo en el momento justo
// o tirándote en plancha a tiempo (ventana de la plancha frente a la caca: Tortunabo.Beach.Gull.PoopWalkSprintDive), del
// picado y de la cagada de la zona. Sin mundo ni actores: se simula el blanco con la misma
// función que usa ATN_BeachGullZone en el servidor, a 60 pasos por segundo.
// Correr desde Session Frontend (categoría "Tortunabo.Beach.Gull") o sin ventana:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Beach.Gull; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/Beach/TN_BeachGullTuning.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNBeachGullTuningTest
{
	/** Velocidades de la tortuga (cm/s): las del Blueprint, que son las que se juegan. */
	constexpr float TurtleWalk = TNBeachGullTuning::TurtleWalkSpeed;
	constexpr float TurtleRun = TNBeachGullTuning::TurtleRunSpeed;

	/** Un tramo de la huida: desde Start s, en dirección Dir (unitaria) a Speed cm/s. */
	struct FLeg
	{
		float Start = 0.f;
		FVector2D Dir = FVector2D(1.0, 0.0);
		float Speed = 0.f;
	};

	FVector2D Heading(float Degrees)
	{
		const float Radians = FMath::DegreesToRadians(Degrees);
		return FVector2D(FMath::Cos(Radians), FMath::Sin(Radians));
	}

	/**
	 * Simula un ataque con Plan: la tortuga sale del origen y se mueve por tramos; el blanco nace sobre ella y la sigue con
	 * TNBeachGullTuning::StepAim hasta Plan.EndAt (o hasta StopAt, si es antes). Devuelve la distancia en planta entre el
	 * blanco y la tortuga en ese momento (al golpe, si no se pide otro).
	 */
	double SimulateMissDistance(const TNBeachGullTuning::FChasePlan& Plan, const TArray<FLeg>& Legs, float StopAt = -1.f)
	{
		constexpr float Dt = 1.f / 60.f;
		FVector2D Turtle = FVector2D::ZeroVector;
		FVector2D Aim = Turtle;
		TNBeachGullTuning::FChaseState State;
		const float End = StopAt >= 0.f ? FMath::Min(StopAt, Plan.EndAt) : Plan.EndAt;
		for (float T = 0.f; T < End - KINDA_SMALL_NUMBER; T += Dt)
		{
			const float Step = FMath::Min(Dt, End - T);
			// La tortuga va con el último tramo que ya ha empezado.
			FVector2D Velocity = FVector2D::ZeroVector;
			for (const FLeg& Leg : Legs)
			{
				if (T >= Leg.Start)
				{
					Velocity = Leg.Dir * Leg.Speed;
				}
			}
			Turtle += Velocity * Step;
			// Como en el servidor: después de moverse ella, el blanco va hacia donde está.
			Aim = TNBeachGullTuning::StepAim(Plan, T, State, Aim, Turtle, Velocity, Step);
		}
		return FVector2D::Distance(Aim, Turtle);
	}

	/** Lo que coge el pico y lo que alcanza cada cagada, con el tamaño SizeK. */
	bool DiveCatches(double Miss, float SizeK = 1.f)
	{
		return TNBeachGullTuning::IsInsideHit(Miss, TNBeachGullTuning::GrabRadius, SizeK, TNBeachGullTuning::GrabPad);
	}

	bool PoopHits(double Miss, float SizeK = 1.f)
	{
		return TNBeachGullTuning::IsInsideHit(Miss, TNBeachGullTuning::SplatRadius, SizeK, TNBeachGullTuning::SplatPad);
	}

	/**
	 * Las mismas situaciones para los dos ataques. Hit(Miss, SizeK) dice si le da; se prueba con el tamaño más pequeño y el
	 * más grande de las zonas (0,75 y 1,3) donde importa.
	 */
	template <typename FHit>
	void CheckDodges(FAutomationTestBase& Test, const TCHAR* What, const TNBeachGullTuning::FChasePlan& Plan, FHit Hit)
	{
		const float Commit = Plan.CommitAt + 0.05f;
		const FVector2D Ahead = Heading(0.f);
		const FVector2D Side = Heading(90.f);
		const FVector2D Back = Heading(180.f);
		auto Miss = [&Plan](const TArray<FLeg>& Legs) { return SimulateMissDistance(Plan, Legs); };

		Test.TestTrue(FString::Printf(TEXT("%s: quieta, le da"), What), Hit(Miss({}), 0.75f));
		Test.TestTrue(FString::Printf(TEXT("%s: andando en línea recta, le da"), What), Hit(Miss({ { 0.f, Ahead, TurtleWalk } }), 0.75f));
		// Esprintando en línea recta se le gana distancia a la sombra todo el rato y, al golpe, ni el pájaro más grande alcanza (#636).
		const TArray<FLeg> Sprint = { { 0.f, Ahead, TurtleRun } };
		const double SprintEarly = SimulateMissDistance(Plan, Sprint, Plan.CommitAt * 0.5f);
		const double SprintAtCommit = SimulateMissDistance(Plan, Sprint, Plan.CommitAt);
		const double SprintAtHit = Miss(Sprint);
		Test.AddInfo(FString::Printf(TEXT("%s: esprintando en línea recta, la sombra a %.2f m a mitad, %.2f m al lanzarse y %.2f m al golpe"),
			What, SprintEarly / 100.0, SprintAtCommit / 100.0, SprintAtHit / 100.0));
		Test.TestTrue(FString::Printf(TEXT("%s: esprintando en línea recta, gana distancia a la sombra"), What),
			SprintEarly > 1.0 && SprintAtCommit > SprintEarly && SprintAtHit > SprintAtCommit);
		Test.TestFalse(FString::Printf(TEXT("%s: esprintando en línea recta, se libra (también del más grande)"), What), Hit(SprintAtHit, 1.3f));
		Test.TestTrue(FString::Printf(TEXT("%s: andando, la sombra no se despega al lanzarse"), What),
			SimulateMissDistance(Plan, { { 0.f, Ahead, TurtleWalk } }, Plan.CommitAt) < 1.0);
		Test.TestFalse(FString::Printf(TEXT("%s: corriendo y, al lanzarse, girando de lado, se libra"), What),
			Hit(Miss({ { 0.f, Ahead, TurtleRun }, { Commit, Side, TurtleRun } }), 1.3f));
		Test.TestFalse(FString::Printf(TEXT("%s: corriendo y, al lanzarse, girando 60°, se libra"), What),
			Hit(Miss({ { 0.f, Ahead, TurtleRun }, { Commit, Heading(60.f), TurtleRun } }), 1.3f));
		// Esprintando, la sombra queda detrás: darse la vuelta al lanzarse es cruzarla, y que dé o no depende del ataque y del
		// tamaño (#636). Solo se apunta, para la documentación.
		Test.AddInfo(FString::Printf(TEXT("%s: esprintando y, al lanzarse, dándose la vuelta hacia la sombra, queda a %.2f m"),
			What, Miss({ { 0.f, Ahead, TurtleRun }, { Commit, Back, TurtleRun } }) / 100.0));
		Test.TestFalse(FString::Printf(TEXT("%s: quieta y, al lanzarse, echando a correr, se libra"), What),
			Hit(Miss({ { Commit, Side, TurtleRun } }), 1.3f));
		Test.TestTrue(FString::Printf(TEXT("%s: andando y, al lanzarse, girando de lado, le da"), What),
			Hit(Miss({ { 0.f, Ahead, TurtleWalk }, { Commit, Side, TurtleWalk } }), 0.75f));
		Test.TestTrue(FString::Printf(TEXT("%s: andando y, al lanzarse, dándose la vuelta, le da"), What),
			Hit(Miss({ { 0.f, Ahead, TurtleWalk }, { Commit, Back, TurtleWalk } }), 0.75f));
		Test.TestTrue(FString::Printf(TEXT("%s: andando y girando antes de que se lance (0,2 s), le da"), What),
			Hit(Miss({ { 0.f, Ahead, TurtleWalk }, { Plan.CommitAt - 0.2f, Side, TurtleWalk } }), 0.75f));
		Test.TestTrue(FString::Printf(TEXT("%s: andando y girando demasiado tarde (0,4 s antes del golpe), le da"), What),
			Hit(Miss({ { 0.f, Ahead, TurtleWalk }, { Plan.EndAt - 0.4f, Side, TurtleWalk } }), 0.75f));
		Test.TestTrue(FString::Printf(TEXT("%s: esprintando y echándose a andar demasiado tarde (0,5 s antes del golpe), se sigue librando"), What),
			!Hit(Miss({ { 0.f, Ahead, TurtleRun }, { Plan.EndAt - 0.5f, Ahead, TurtleWalk } }), 1.f));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Tramos del blanco
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachGullChasePlanTest,
	"Tortunabo.Beach.Gull.ChasePlan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachGullChasePlanTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachGullTuning;
	using namespace TNBeachGullTuningTest;

	// #636: más rápido de lo que se anda (andando no se despega) y más despacio de lo que se corre (esprintando, sí).
	TestTrue(TEXT("Picado: persigue entre andar y correr"), DiveChaseSpeed > TurtleWalk && DiveChaseSpeed < TurtleRun);
	TestTrue(TEXT("Cagada: ídem"), PoopChaseSpeed > TurtleWalk && PoopChaseSpeed < TurtleRun);
	TestTrue(TEXT("Lanzado, hacia los lados corrige mucho menos de lo que se anda"),
		DiveLateCorrection < TurtleWalk * 0.5f && PoopLateCorrection < TurtleWalk * 0.5f);
	TestTrue(TEXT("Picado: se lanza antes de llegar abajo y después de empezar a bajar"),
		DivePlan().CommitAt > DiveClimbTime && DivePlan().CommitAt < DivePlan().EndAt);
	TestTrue(TEXT("El «!» de la cagada se queda fijo cuando ya cae por su línea"),
		!IsCommitted(PoopPlan(), PoopPlan().CommitAt - 0.01f) && IsCommitted(PoopPlan(), PoopPlan().CommitAt));
	TestTrue(TEXT("La velocidad del suavizado de los clientes cubre la persecución"),
		MaxChaseSpeed() >= DiveChaseSpeed && MaxChaseSpeed() >= PoopChaseSpeed);

	{
		const FVector2D Stepped = StepToward(FVector2D(0.0, 0.0), FVector2D(100.0, 0.0), 600.f, 0.1f);
		TestTrue(TEXT("El blanco no se pasa de su objetivo"), Stepped.Equals(FVector2D(60.0, 0.0), 0.01));
		const FVector2D Arrived = StepToward(FVector2D(0.0, 0.0), FVector2D(30.0, 40.0), 600.f, 0.1f);
		TestTrue(TEXT("Si llega en este paso, se queda justo encima"), Arrived.Equals(FVector2D(30.0, 40.0), 0.01));
	}
	{
		// Lanzado: por su línea acompaña lo que ella avanza por ella, nunca hacia atrás.
		const FChasePlan Plan = DivePlan();
		FChaseState State;
		const FVector2D Forward = StepAim(Plan, Plan.CommitAt, State, FVector2D::ZeroVector, FVector2D(40.0, 0.0), FVector2D(TurtleRun, 0.0), 0.1f);
		TestTrue(TEXT("Lanzado: apunta su línea y su velocidad"), State.bCommitted && State.Dir.Equals(FVector2D(1.0, 0.0), 0.001));
		TestTrue(TEXT("Lanzado: avanza con ella por su línea (hasta la velocidad de persecución)"), Forward.X >= FMath::Min(TurtleRun, Plan.ChaseSpeed) * 0.1f - 1.0);
		const FVector2D Backwards = StepAim(Plan, Plan.CommitAt + 0.1f, State, FVector2D::ZeroVector, FVector2D(-40.0, 0.0), FVector2D(-TurtleRun, 0.0), 0.1f);
		TestTrue(TEXT("Lanzado: si ella se da la vuelta, él no retrocede más que su corrección"),
			Backwards.X >= -static_cast<double>(Plan.LateCorrection) * 0.1 - 0.01);
		const FVector2D After = StepAim(Plan, Plan.EndAt + 0.1f, State, FVector2D(5.0, 5.0), FVector2D(300.0, 0.0), FVector2D(TurtleRun, 0.0), 0.1f);
		TestTrue(TEXT("Tras el golpe el blanco no se mueve"), After.Equals(FVector2D(5.0, 5.0), 0.01));
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Esquivar el picado y la cagada
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachGullDodgeTest,
	"Tortunabo.Beach.Gull.Dodge",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachGullDodgeTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachGullTuning;
	using namespace TNBeachGullTuningTest;

	CheckDodges(*this, TEXT("Picado"), DivePlan(), [](double Miss, float SizeK) { return DiveCatches(Miss, SizeK); });
	CheckDodges(*this, TEXT("Cagada"), PoopPlan(), [](double Miss, float SizeK) { return PoopHits(Miss, SizeK); });

	// La plancha: tirarse en el momento justo (en el aire o aún deprisa sobre la tripa), no tumbarse a esperar.
	TestTrue(TEXT("Plancha en el aire: libra"), DodgesByBellyDive(true, true, 0.f));
	TestTrue(TEXT("Plancha arrastrándose aún deprisa: libra"), DodgesByBellyDive(true, false, BellyDodgeMinSpeed + 10.f));
	TestFalse(TEXT("Tumbada casi parada sobre la tripa: no libra"), DodgesByBellyDive(true, false, BellyDodgeMinSpeed * 0.5f));
	TestFalse(TEXT("Sin plancha, aunque vaya por el aire (un salto): no libra"), DodgesByBellyDive(false, true, 900.f));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// La caca frente a andar, esprintar y la plancha (#636)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachGullPoopDodgeTest,
	"Tortunabo.Beach.Gull.PoopWalkSprintDive",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachGullPoopDodgeTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachGullTuning;
	using namespace TNBeachGullTuningTest;

	const FChasePlan Plan = PoopPlan();
	const FVector2D Ahead = Heading(0.f);
	const double WalkMiss = SimulateMissDistance(Plan, { { 0.f, Ahead, TurtleWalk } });
	const double SprintMiss = SimulateMissDistance(Plan, { { 0.f, Ahead, TurtleRun } });
	for (const float SizeK : { 0.75f, 1.f, 1.3f })
	{
		TestTrue(FString::Printf(TEXT("Andando en línea recta, la caca de tamaño %.2f le da"), SizeK), PoopHits(WalkMiss, SizeK));
		TestFalse(FString::Printf(TEXT("Esprintando en línea recta, la caca de tamaño %.2f no le da"), SizeK), PoopHits(SprintMiss, SizeK));
	}

	// La ventana de la plancha, documentada en TN_BeachGullTuning.h: corriendo, 0,59-0,69 s; andando, 0,48-0,58 s.
	const float RunMin = BellyDiveDodgeWindow(TurtleRun, BellyDiveAirSecondsMin);
	const float RunMax = BellyDiveDodgeWindow(TurtleRun, BellyDiveAirSecondsMax);
	const float WalkMin = BellyDiveDodgeWindow(TurtleWalk, BellyDiveAirSecondsMin);
	const float WalkMax = BellyDiveDodgeWindow(TurtleWalk, BellyDiveAirSecondsMax);
	AddInfo(FString::Printf(TEXT("Ventana de la plancha: corriendo %.2f-%.2f s, andando %.2f-%.2f s"), RunMin, RunMax, WalkMin, WalkMax));
	TestEqual(TEXT("La plancha corriendo toca la arena a 675 cm/s"), BellyDiveLandingSpeed(TurtleRun), 675.f, 0.5f);
	TestTrue(TEXT("Ventana corriendo: 0,59-0,69 s"), FMath::IsNearlyEqual(RunMin, 0.59f, 0.01f) && FMath::IsNearlyEqual(RunMax, 0.69f, 0.01f));
	TestTrue(TEXT("Ventana andando: 0,48-0,58 s"), FMath::IsNearlyEqual(WalkMin, 0.48f, 0.01f) && FMath::IsNearlyEqual(WalkMax, 0.58f, 0.01f));
	TestTrue(TEXT("Al acabar la ventana, el arrastre va justo a la velocidad mínima"),
		FMath::IsNearlyEqual(BellySlideSpeedAt(BellyDiveLandingSpeed(TurtleRun), RunMin - BellyDiveAirSecondsMin), BellyDodgeMinSpeed, 1.f));

	// Andando le da; con la plancha en el momento justo (la caca cae dentro de la ventana) le pasa por encima.
	const auto PoopLands = [WalkMiss](float DiveBeforeImpact, float AirSeconds)
	{
		return PoopHits(WalkMiss, 1.f) && !DodgesByBellyDiveAt(DiveBeforeImpact, TurtleWalk, AirSeconds);
	};
	TestFalse(TEXT("Plancha 0,2 s antes de que caiga (en el aire): se libra"), PoopLands(0.2f, BellyDiveAirSecondsMin));
	TestFalse(TEXT("Plancha 0,4 s antes de que caiga (arrastrándose deprisa): se libra"), PoopLands(0.4f, BellyDiveAirSecondsMin));
	TestFalse(TEXT("Plancha justo dentro de la ventana: se libra"), PoopLands(WalkMin - 0.02f, BellyDiveAirSecondsMin));
	TestTrue(TEXT("Plancha justo fuera de la ventana: le da"), PoopLands(WalkMin + 0.02f, BellyDiveAirSecondsMin));
	TestTrue(TEXT("Plancha demasiado pronto (1 s antes, ya tumbada): le da"), PoopLands(1.f, BellyDiveAirSecondsMax));
	TestTrue(TEXT("Plancha demasiado tarde (después de que caiga): le da"), PoopLands(-0.1f, BellyDiveAirSecondsMin));
	TestTrue(TEXT("Corriendo, la plancha 0,65 s antes con 0,4 s de aire aún libra"), DodgesByBellyDiveAt(0.65f, TurtleRun, BellyDiveAirSecondsMax));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
