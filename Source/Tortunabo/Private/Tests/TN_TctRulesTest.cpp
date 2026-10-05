// Reglas puras de Todos contra Todos (TN_TctRules.h, #651). Sin mundo ni actores. Correr desde Session Frontend (categoría
// "Tortunabo.Tct") o sin ventana:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Tct; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Game/TN_TctRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTctRulesTestDetail
{
	FTNTctFighter Fighter(int32 Id, bool bAlive = true, bool bConnected = true, int32 Wins = 0)
	{
		FTNTctFighter F;
		F.Id = Id;
		F.bAlive = bAlive;
		F.bConnected = bConnected;
		F.Wins = Wins;
		return F;
	}

	/** Suelo de una diana como A01: cinco anillos con su parte de suelo y algo de rampa entre ellos. */
	TArray<float> DianaHeights()
	{
		TArray<float> Heights;
		const struct { float Z; int32 Count; } Rings[] = { { -200.f, 41 }, { 0.f, 30 }, { 200.f, 19 }, { 500.f, 8 }, { 800.f, 2 } };
		for (const auto& Ring : Rings)
		{
			for (int32 Index = 0; Index < Ring.Count * 10; ++Index) { Heights.Add(Ring.Z); }
		}
		// Rampas: pocas muestras repartidas entre los anillos (no son pisos).
		for (float Z = -190.f; Z < 800.f; Z += 25.f) { Heights.Add(Z); }
		return Heights;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Rondas
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctRoundEliminationTest,
	"Tortunabo.Tct.Round.Elimination",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctRoundEliminationTest::RunTest(const FString& Parameters)
{
	using namespace TNTctRulesTestDetail;
	using namespace TNTctRules;

	TestTrue(TEXT("Tres en pie → sigue"), DecideRound({ Fighter(1), Fighter(2), Fighter(3) }, 3, false).Outcome == ETNTctRoundOutcome::Continue);
	TestTrue(TEXT("Una eliminada, dos en pie → sigue"),
		DecideRound({ Fighter(1), Fighter(2, false), Fighter(3) }, 3, false).Outcome == ETNTctRoundOutcome::Continue);

	const FTNTctRoundDecision Last = DecideRound({ Fighter(1, false), Fighter(2, false), Fighter(3) }, 3, false);
	TestTrue(TEXT("Queda una en pie → gana la ronda"), Last.Outcome == ETNTctRoundOutcome::Winner);
	TestEqual(TEXT("Gana la última en pie"), Last.WinnerId, 3);

	const FTNTctRoundDecision Eight = DecideRound({ Fighter(1, false), Fighter(2, false), Fighter(3, false), Fighter(4, false),
		Fighter(5, false), Fighter(6), Fighter(7, false), Fighter(8, false) }, 8, false);
	TestTrue(TEXT("A ocho: queda una → gana"), Eight.Outcome == ETNTctRoundOutcome::Winner && Eight.WinnerId == 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctRoundDrawTest,
	"Tortunabo.Tct.Round.Draw",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctRoundDrawTest::RunTest(const FString& Parameters)
{
	using namespace TNTctRulesTestDetail;
	using namespace TNTctRules;

	const FTNTctRoundDecision AllDown = DecideRound({ Fighter(1, false), Fighter(2, false) }, 2, false);
	TestTrue(TEXT("Las dos últimas caen a la vez → empate"), AllDown.Outcome == ETNTctRoundOutcome::Draw);
	TestEqual(TEXT("Sin ganadora"), AllDown.WinnerId, static_cast<int32>(INDEX_NONE));

	TestTrue(TEXT("Tiempo agotado con dos en pie → empate"),
		DecideRound({ Fighter(1), Fighter(2), Fighter(3, false) }, 3, true).Outcome == ETNTctRoundOutcome::Draw);
	TestTrue(TEXT("Tiempo agotado con una en pie → gana ella, no empate"),
		DecideRound({ Fighter(1), Fighter(2, false) }, 2, true).Outcome == ETNTctRoundOutcome::Winner);

	// Partida de prueba de una sola tortuga: no gana por estar sola; acaba al caer o con el tiempo.
	TestTrue(TEXT("Sola y en pie → sigue"), DecideRound({ Fighter(1) }, 1, false).Outcome == ETNTctRoundOutcome::Continue);
	TestTrue(TEXT("Sola y caída → empate"), DecideRound({ Fighter(1, false) }, 1, false).Outcome == ETNTctRoundOutcome::Draw);
	TestTrue(TEXT("Sola con el tiempo agotado → empate"), DecideRound({ Fighter(1) }, 1, true).Outcome == ETNTctRoundOutcome::Draw);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctRoundDisconnectTest,
	"Tortunabo.Tct.Round.Disconnect",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctRoundDisconnectTest::RunTest(const FString& Parameters)
{
	using namespace TNTctRulesTestDetail;
	using namespace TNTctRules;

	const FTNTctRoundDecision Left = DecideRound({ Fighter(1), Fighter(2, true, false) }, 2, false);
	TestTrue(TEXT("Dos en pie y una se va → gana la que queda"), Left.Outcome == ETNTctRoundOutcome::Winner);
	TestEqual(TEXT("Gana la conectada"), Left.WinnerId, 1);

	TestTrue(TEXT("Tres en pie y una se va → sigue"),
		DecideRound({ Fighter(1), Fighter(2), Fighter(3, true, false) }, 3, false).Outcome == ETNTctRoundOutcome::Continue);
	TestTrue(TEXT("Se van todas → empate"),
		DecideRound({ Fighter(1, true, false), Fighter(2, true, false) }, 2, false).Outcome == ETNTctRoundOutcome::Draw);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Partida
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctMatchTest,
	"Tortunabo.Tct.Match",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctMatchTest::RunTest(const FString& Parameters)
{
	using namespace TNTctRulesTestDetail;
	using namespace TNTctRules;

	TestTrue(TEXT("Nadie a 3 → otra ronda"),
		DecideMatch({ Fighter(1, true, true, 2), Fighter(2, true, true, 2) }, 3, false).Outcome == ETNTctMatchOutcome::NextRound);

	const FTNTctMatchDecision Champion = DecideMatch({ Fighter(1, true, true, 1), Fighter(2, true, true, 3), Fighter(3, true, true, 2) }, 3, false);
	TestTrue(TEXT("Una llega a 3 → campeona"), Champion.Outcome == ETNTctMatchOutcome::Champion);
	TestEqual(TEXT("La de 3 rondas"), Champion.ChampionId, 2);

	const FTNTctMatchDecision Forfeit = DecideMatch({ Fighter(1, true, true, 0), Fighter(2, true, false, 2) }, 3, false);
	TestTrue(TEXT("Solo queda una en la partida → campeona aunque no llegue a 3"), Forfeit.Outcome == ETNTctMatchOutcome::Champion);
	TestEqual(TEXT("La que queda"), Forfeit.ChampionId, 1);

	TestTrue(TEXT("No queda nadie → partida abandonada"),
		DecideMatch({ Fighter(1, true, false, 1), Fighter(2, true, false, 1) }, 3, false).Outcome == ETNTctMatchOutcome::Abandoned);
	TestTrue(TEXT("Prueba en solitario → sigue aunque esté sola"),
		DecideMatch({ Fighter(1, true, true, 0) }, 3, true).Outcome == ETNTctMatchOutcome::NextRound);

	const TArray<int32> Ranked = RankFighters({ Fighter(5, true, true, 1), Fighter(2, true, true, 3), Fighter(3, true, true, 1), Fighter(4, true, true, 2) }, 2);
	TestEqual(TEXT("Podio: campeona primero"), Ranked[0], 2);
	TestEqual(TEXT("Podio: luego por rondas"), Ranked[1], 4);
	TestEqual(TEXT("Podio: a igualdad, por orden de llegada"), Ranked[2], 3);
	TestEqual(TEXT("Podio: la última"), Ranked[3], 5);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Subida del agua
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctFloodLevelsTest,
	"Tortunabo.Tct.Flood.Levels",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctFloodLevelsTest::RunTest(const FString& Parameters)
{
	using namespace TNTctRulesTestDetail;
	using namespace TNTctRules;

	// Diana: inunda anillo a anillo desde fuera y deja seco el centro (el 10 % alto) para la muerte súbita.
	const TArray<float> Diana = ComputeFloodLevels(DianaHeights(), 4, 0.08f, 150.f, 60.f);
	TestEqual(TEXT("Diana: tres escalones (los tres anillos de fuera)"), Diana.Num(), 3);
	if (Diana.Num() == 3)
	{
		TestTrue(TEXT("Primero: cubre el anillo exterior y no el segundo"), Diana[0] > -200.f && Diana[0] < 0.f);
		TestTrue(TEXT("Segundo: cubre el segundo anillo y no el tercero"), Diana[1] > 0.f && Diana[1] < 200.f);
		TestTrue(TEXT("Tercero: cubre el tercer anillo y no el cuarto"), Diana[2] > 200.f && Diana[2] < 500.f);
	}

	TArray<float> Flat;
	Flat.Init(100.f, 500);
	TestEqual(TEXT("Arena plana: sin escalones (solo la muerte súbita)"), ComputeFloodLevels(Flat, 4, 0.08f, 150.f, 60.f).Num(), 0);

	const TArray<float> Two = ComputeFloodLevels(DianaHeights(), 2, 0.08f, 150.f, 60.f);
	TestEqual(TEXT("Con dos escalones como mucho, dos"), Two.Num(), 2);
	if (Two.Num() == 2 && Diana.Num() == 3)
	{
		TestEqual(TEXT("El último escalón es siempre el más alto posible"), Two[1], Diana[2]);
	}

	// Un piso justo en el borde de dos franjas (A01: el segundo anillo a 0, con muestras a -2 y a +11) da un solo escalón.
	TArray<float> Straddle;
	for (int32 Index = 0; Index < 300; ++Index) { Straddle.Add(-200.f); }
	for (int32 Index = 0; Index < 150; ++Index) { Straddle.Add(-2.f); Straddle.Add(11.f); }
	for (int32 Index = 0; Index < 100; ++Index) { Straddle.Add(500.f); }
	const TArray<float> Split = ComputeFloodLevels(Straddle, 4, 0.08f, 150.f, 60.f);
	TestEqual(TEXT("Piso partido entre franjas: un escalón por piso"), Split.Num(), 2);
	if (Split.Num() == 2)
	{
		TestTrue(TEXT("El escalón del piso partido cubre sus dos mitades"), Split[1] >= 11.f + 60.f - KINDA_SMALL_NUMBER);
	}

	const TArray<float> KeepHalf = ComputeFloodLevels(DianaHeights(), 4, 0.5f, 150.f, 60.f);
	TestEqual(TEXT("Dejando seca la mitad: solo el anillo exterior (41 %)"), KeepHalf.Num(), 1);
	TestEqual(TEXT("Sin suelo: sin escalones"), ComputeFloodLevels({}, 4, 0.08f, 150.f, 60.f).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctFloodTimelineTest,
	"Tortunabo.Tct.Flood.Timeline",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctFloodTimelineTest::RunTest(const FString& Parameters)
{
	using namespace TNTctRules;

	// Los valores de serie del plan (#778: el agua sube más despacio).
	FTNTctFloodPlan Plan;
	TestEqual(TEXT("Empieza a subir a los 25 s"), Plan.StartDelay, 25.f);
	TestEqual(TEXT("Un escalón cada 24 s"), Plan.StepSeconds, 24.f);
	TestEqual(TEXT("Cada escalón tarda 7 s"), Plan.RiseSeconds, 7.f);
	TestEqual(TEXT("La muerte súbita tarda 40 s"), Plan.SuddenDeathRiseSeconds, 40.f);
	Plan.BaseZ = -400.f;
	Plan.Levels = { -140.f, 60.f, 260.f };
	Plan.SuddenDeathZ = 1100.f;

	TestEqual(TEXT("Antes de la primera subida, el mar"), WaterZAt(Plan, 20.f), -400.f);
	TestEqual(TEXT("A mitad de la primera subida"), WaterZAt(Plan, 28.5f), -270.f);
	TestEqual(TEXT("Primer escalón alcanzado"), WaterZAt(Plan, 33.f), -140.f);
	TestEqual(TEXT("Segundo escalón a los 49 s"), StepStartSeconds(Plan, 1), 49.f);
	TestEqual(TEXT("Segundo escalón alcanzado"), WaterZAt(Plan, 60.f), 60.f);
	TestEqual(TEXT("Tercer escalón alcanzado"), WaterZAt(Plan, 90.f), 260.f);
	TestEqual(TEXT("Muerte súbita a los 97 s"), StepStartSeconds(Plan, 3), 97.f);
	TestTrue(TEXT("La muerte súbita sube despacio"), WaterZAt(Plan, 110.f) > 260.f && WaterZAt(Plan, 110.f) < 1100.f);
	TestEqual(TEXT("Cima cubierta a los 137 s"), FloodTopSeconds(Plan), 137.f);
	TestEqual(TEXT("Al final, todo cubierto"), WaterZAt(Plan, 200.f), 1100.f);

	// Entre subidas, StepSeconds.
	for (int32 Step = 1; Step <= Plan.Levels.Num(); ++Step)
	{
		TestEqual(TEXT("Entre subidas, 24 s"), StepStartSeconds(Plan, Step) - StepStartSeconds(Plan, Step - 1), 24.f);
	}

	// Una arena de cuatro pisos (lo más que hace el GameMode): 25 + 4 × 24 = 121 s y la cima a los 161 s (antes, 108 s).
	FTNTctFloodPlan Tall = Plan;
	Tall.Levels = { -140.f, 60.f, 260.f, 460.f };
	TestEqual(TEXT("Cuatro pisos: muerte súbita a los 121 s"), StepStartSeconds(Tall, Tall.Levels.Num()), 121.f);
	TestEqual(TEXT("Cuatro pisos: cima cubierta a los 161 s"), FloodTopSeconds(Tall), 161.f);
	TestTrue(TEXT("El tiempo de la ronda deja llegar el agua a la cima"),
		TNTctFloodDefaults::RoundTimeLimitSeconds >= FloodTopSeconds(Tall));
	TestEqual(TEXT("El GameMode usa como mucho cuatro escalones"), TNTctFloodDefaults::MaxSteps, Tall.Levels.Num());

	FTNTctFloodPlan Flat = Plan;
	Flat.Levels.Reset();
	TestEqual(TEXT("Sin escalones: la muerte súbita empieza en la primera subida"), StepStartSeconds(Flat, 0), 25.f);
	TestEqual(TEXT("Sin escalones: acaba todo cubierto"), WaterZAt(Flat, 100.f), 1100.f);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Caídas y salidas
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctEliminationBoundsTest,
	"Tortunabo.Tct.Bounds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctEliminationBoundsTest::RunTest(const FString& Parameters)
{
	using namespace TNTctRules;

	FTNTctArenaBounds Bounds;
	Bounds.Min = FVector(0.f, 0.f, -300.f);
	Bounds.Max = FVector(20000.f, 20000.f, 900.f);
	Bounds.OutMargin = 4000.f;
	Bounds.WadeDepth = 20.f;
	Bounds.FallDepth = 3000.f;

	FTNTctBody Body;
	Body.HalfHeight = 90.f;
	Body.Location = FVector(5000.f, 5000.f, 90.f);
	TestFalse(TEXT("De pie en el suelo (pies a 0) con el agua a -400 → sigue"), ShouldEliminate(Body, Bounds, -400.f));
	TestFalse(TEXT("Vadeando (pies 10 bajo el agua) → sigue"), ShouldEliminate(Body, Bounds, 10.f));
	TestTrue(TEXT("El agua le pasa de los pies → fuera"), ShouldEliminate(Body, Bounds, 60.f));

	Body.Location = FVector(-3000.f, 5000.f, 500.f);
	TestFalse(TEXT("Saltando fuera del borde, aún cerca → sigue"), ShouldEliminate(Body, Bounds, -400.f));
	Body.Location = FVector(-4500.f, 25000.f, 500.f);
	TestTrue(TEXT("Lanzada lejos de la arena → fuera"), ShouldEliminate(Body, Bounds, -400.f));
	Body.Location = FVector(5000.f, 5000.f, -3500.f);
	TestTrue(TEXT("Por debajo de la arena sin agua → fuera"), ShouldEliminate(Body, Bounds, -100000.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctSpreadSpawnsTest,
	"Tortunabo.Tct.Spawns",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctSpreadSpawnsTest::RunTest(const FString& Parameters)
{
	using namespace TNTctRules;

	// Un anillo de 16 sitios y el centro: ocho salidas, todas en el anillo y separadas.
	TArray<FVector> Candidates;
	Candidates.Add(FVector::ZeroVector);
	for (int32 Index = 0; Index < 16; ++Index)
	{
		const float Angle = 2.f * PI * Index / 16.f;
		Candidates.Add(FVector(FMath::Cos(Angle) * 5000.f, FMath::Sin(Angle) * 5000.f, 0.f));
	}
	const TArray<int32> Picked = PickSpreadPoints(Candidates, 8, FVector::ZeroVector);
	TestEqual(TEXT("Ocho salidas"), Picked.Num(), 8);
	TestFalse(TEXT("El centro no es salida (lo más lejos del centro primero)"), Picked.Contains(0));
	float Closest = TNumericLimits<float>::Max();
	for (int32 A = 0; A < Picked.Num(); ++A)
	{
		for (int32 B = A + 1; B < Picked.Num(); ++B)
		{
			Closest = FMath::Min(Closest, static_cast<float>(FVector::Dist2D(Candidates[Picked[A]], Candidates[Picked[B]])));
		}
	}
	// Uno de cada dos sitios del anillo: separados la cuerda de 45°.
	TestTrue(TEXT("Repartidas (ninguna pegada a otra)"), Closest > 3500.f);
	TestEqual(TEXT("Con menos sitios que jugadoras, todos"), PickSpreadPoints({ FVector::ZeroVector, FVector(100.f, 0.f, 0.f) }, 8, FVector::ZeroVector).Num(), 2);
	TestEqual(TEXT("Sin sitios, ninguna"), PickSpreadPoints({}, 4, FVector::ZeroVector).Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
