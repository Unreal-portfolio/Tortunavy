// Brinco desde el agua predicho (#573): la espera entre brincos se mide con el tiempo de simulación de los movimientos,
// así que el dueño y el servidor brincan en los mismos movimientos aunque el servidor junte los que no saltan, y nunca
// antes de 0,6 s. Se testea TN_SwimHopRules.h, que usa UTN_TurtleMovementComponent (CanAttemptJump, DoJump y el paso de
// cada movimiento).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Movement.SwimHop; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_SwimHopRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNSwimHopTest
{
	struct FMove
	{
		float DeltaSeconds = 0.f;
		bool bJump = false;
	};

	/** Machacar el salto nadando: pulsado cada Every fotogramas de 1/60 s durante Seconds. */
	TArray<FMove> Mash(int32 Every, float Seconds)
	{
		TArray<FMove> Moves;
		const int32 Frames = FMath::RoundToInt(Seconds * 60.f);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Moves.Add({ 1.f / 60.f, Frame % Every == 0 });
		}
		return Moves;
	}

	/** El dueño: cada movimiento por separado. Devuelve el instante (s) de cada brinco. */
	TArray<float> RunClient(const TArray<FMove>& Moves)
	{
		TArray<float> Hops;
		float Remaining = 0.f;
		float Clock = 0.f;
		for (const FMove& Move : Moves)
		{
			const TNSwimHop::FStep Step = TNSwimHop::Step(Remaining, Move.bJump, true, Move.DeltaSeconds);
			if (Step.bHop)
			{
				Hops.Add(Clock);
			}
			Remaining = Step.RemainingAfter;
			Clock += Move.DeltaSeconds;
		}
		return Hops;
	}

	/**
	 * El servidor: los movimientos que no saltan llegan de dos en dos (el cliente los junta) y sus DeltaTime salen de
	 * restar marcas de tiempo, con su redondeo.
	 */
	TArray<float> RunServer(const TArray<FMove>& Moves)
	{
		TArray<float> Hops;
		float Remaining = 0.f;
		float Clock = 0.f;
		for (int32 Index = 0; Index < Moves.Num(); ++Index)
		{
			FMove Move = Moves[Index];
			const bool bCanCombine = !Move.bJump && Moves.IsValidIndex(Index + 1) && !Moves[Index + 1].bJump;
			if (bCanCombine)
			{
				Move.DeltaSeconds += Moves[Index + 1].DeltaSeconds;
				++Index;
			}
			const float TimeStampDelta = (Clock + Move.DeltaSeconds + 100.f) - (Clock + 100.f);
			const TNSwimHop::FStep Step = TNSwimHop::Step(Remaining, Move.bJump, true, TimeStampDelta);
			if (Step.bHop)
			{
				Hops.Add(Clock);
			}
			Remaining = Step.RemainingAfter;
			Clock += Move.DeltaSeconds;
		}
		return Hops;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSwimHopCooldownTest,
	"Tortunabo.Movement.SwimHop.Cooldown",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSwimHopCooldownTest::RunTest(const FString& Parameters)
{
	using namespace TNSwimHop;

	TestTrue(TEXT("Sin brinco previo: listo"), IsReady(0.f));
	TNSwimHop::FStep Step = TNSwimHop::Step(0.f, true, true, 1.f / 60.f);
	TestTrue(TEXT("Nadando y pulsado: brinca"), Step.bHop);
	TestTrue(TEXT("Tras brincar queda la espera menos el movimiento"), FMath::IsNearlyEqual(Step.RemainingAfter, CooldownSeconds - 1.f / 60.f, 1.e-5f));
	TestFalse(TEXT("Recién brincado: no brinca otra vez"), TNSwimHop::Step(Step.RemainingAfter, true, true, 1.f / 60.f).bHop);
	TestFalse(TEXT("Fuera del agua no hay brinco"), TNSwimHop::Step(0.f, true, false, 1.f / 60.f).bHop);
	TestFalse(TEXT("Sin pulsar no hay brinco"), TNSwimHop::Step(0.f, false, true, 1.f / 60.f).bHop);
	TestEqual(TEXT("La espera nunca baja de 0"), Advance(0.1f, 1.f), 0.f);
	TestEqual(TEXT("Un DeltaTime negativo no la alarga"), Advance(0.3f, -1.f), 0.3f);

	// Machacando el salto cada 3 fotogramas durante 10 s: brincos separados 0,6 s (el fotograma en que acaba la espera).
	const TArray<float> Hops = TNSwimHopTest::RunClient(TNSwimHopTest::Mash(3, 10.f));
	TestTrue(TEXT("Brinca varias veces"), Hops.Num() >= 10);
	for (int32 Index = 1; Index < Hops.Num(); ++Index)
	{
		const float Gap = Hops[Index] - Hops[Index - 1];
		TestTrue(*FString::Printf(TEXT("Brinco %d: al menos 0,6 s después del anterior (%.4f s)"), Index, Gap), Gap >= CooldownSeconds - ReadyTolerance);
		TestTrue(*FString::Printf(TEXT("Brinco %d: en cuanto acaba la espera (%.4f s)"), Index, Gap), Gap <= CooldownSeconds + 3.f / 60.f + 1.e-4f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSwimHopPredictionTest,
	"Tortunabo.Movement.SwimHop.ServerAgrees",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSwimHopPredictionTest::RunTest(const FString& Parameters)
{
	// El servidor simula los mismos movimientos (juntando los que no saltan y con el redondeo de las marcas de tiempo): brinca
	// en los mismos instantes que el dueño. Antes medía la espera con su reloj al llegar la RPC y rechazaba parte.
	for (const int32 Every : { 1, 2, 3, 5, 7 })
	{
		const TArray<TNSwimHopTest::FMove> Moves = TNSwimHopTest::Mash(Every, 12.f);
		const TArray<float> Client = TNSwimHopTest::RunClient(Moves);
		const TArray<float> Server = TNSwimHopTest::RunServer(Moves);
		if (!TestEqual(*FString::Printf(TEXT("Pulsando cada %d fotogramas: mismos brincos"), Every), Server.Num(), Client.Num()))
		{
			continue;
		}
		for (int32 Index = 0; Index < Client.Num(); ++Index)
		{
			TestTrue(*FString::Printf(TEXT("Pulsando cada %d: brinco %d en el mismo movimiento"), Every, Index),
				FMath::IsNearlyEqual(Client[Index], Server[Index], 1.e-3f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSwimHopVelocityTest,
	"Tortunabo.Movement.SwimHop.Velocity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSwimHopVelocityTest::RunTest(const FString& Parameters)
{
	// Mirando un poco hacia abajo, el empuje hacia delante es el mismo: solo cuenta la dirección horizontal.
	const FVector Hop = TNSwimHop::HopVelocity(FVector(0.8, 0.0, -0.6), 250.f, 640.f);
	TestTrue(TEXT("Hacia delante, 250 cm/s"), Hop.Equals(FVector(250.0, 0.0, 640.0), 0.01));
	TestTrue(TEXT("Mirando en vertical: solo hacia arriba"), TNSwimHop::HopVelocity(FVector::DownVector, 250.f, 640.f).Equals(FVector(0.0, 0.0, 640.0), 0.01));
	return true;
}

#endif
