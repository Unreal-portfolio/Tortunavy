// Topes de velocidad predichos en el movimiento (#575 llevar a otra, #574 mareo): el dueño pide en cada movimiento los
// topes que conoce y el servidor simula ese movimiento con lo que pide solo dentro de la ventana que abrió su último cambio,
// medida con el tiempo de los movimientos del dueño; se cierra al reconocerla el dueño o al vencer, y el tiempo concedido
// sale de un presupuesto que se recarga despacio. Se testean las reglas de TN_MovementLimits.h y UTN_StaminaComponent, que
// usa UTN_TurtleMovementComponent (MoveAutonomous en el servidor y GetMaxSpeed).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Movement.PredictedCaps; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Player/TN_MovementLimits.h"
#include "Player/TN_StaminaComponent.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNPredictedCapsTest
{
	constexpr float Frame = 1.f / 60.f;

	/** Lo que el servidor deja correr como mucho en un movimiento tras un hueco (MaxMoveDeltaTime 0,125 s por 1,75). */
	constexpr float ServerMaxMoveDelta = 0.125f * 1.75f;

	UTN_StaminaComponent* NewStamina()
	{
		return NewObject<UTN_StaminaComponent>(GetTransientPackage());
	}

	/** Servidor: el dueño manda Seconds de movimientos de un fotograma pidiendo Claimed. Devuelve cuánto tiempo se le concedió sin tope. */
	float FeedUncapped(UTN_StaminaComponent* Stamina, uint8 Claimed, float Seconds)
	{
		float Uncapped = 0.f;
		for (float Clock = 0.f; Clock < Seconds - KINDA_SMALL_NUMBER; Clock += Frame)
		{
			const uint8 Applied = Stamina->ConsumeClientPredictedCaps(Claimed, Frame);
			Uncapped += (Applied & TNMovementLimits::PredictedCapCarryBit) == 0 ? Frame : 0.f;
		}
		return Uncapped;
	}

	/** Red de una partida: retrasos de ida (del dueño al servidor) y de vuelta, pérdidas y atascos. Relojes alineados. */
	struct FNetScenario
	{
		double GrabAt = 0.5;
		double ReleaseAt = 2.0;
		double EndAt = 3.0;
		double Uplink = 0.05;
		double Downlink = 0.05;
		/** Los movimientos hechos en [StallFrom, StallTo) se quedan atascados y llegan juntos en StallTo + Uplink. */
		double StallFrom = -1.0;
		double StallTo = -1.0;
		/** Los movimientos hechos en [LossFrom, LossTo) se pierden. */
		double LossFrom = -1.0;
		double LossTo = -1.0;
		/** Además, se pierde uno de cada LossEvery (0 = ninguno). */
		int32 LossEvery = 0;
	};

	struct FNetResult
	{
		int32 Mismatches = 0;
		int32 CappedMoves = 0;
		int32 Processed = 0;
		/** Lo más tarde que llegó, después de cambiar el tope en el servidor, un movimiento hecho sin saberlo (s). */
		double LatestStaleArrival = 0.0;
	};

	/** Mundo de juego mínimo cuyo reloj hace de hora de llegada en el servidor (lo que no debe decidir la gracia). */
	struct FServerWorld
	{
		UWorld* World = nullptr;
		AActor* Owner = nullptr;

		FServerWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNPredictedCapsTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			Owner = World->SpawnActor<AActor>();
		}

		~FServerWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	/**
	 * El dueño hace un movimiento cada fotograma pidiendo el tope que conoce (el del servidor de hace Downlink); el servidor
	 * los procesa en orden al llegar, con su DeltaTime sacado de las marcas de tiempo (recortado tras un hueco), y coge y
	 * suelta a la otra tortuga a su hora. El reloj del mundo va con la llegada de cada movimiento. Cuenta los movimientos que
	 * el servidor simula con otro tope que el dueño.
	 */
	FNetResult RunNet(const FNetScenario& Net)
	{
		using namespace TNMovementLimits;
		FServerWorld Server;
		UTN_StaminaComponent* Stamina = NewObject<UTN_StaminaComponent>(Server.Owner);
		bool bServerCarrying = false;
		bool bGrabbed = false;
		bool bReleased = false;
		// El servidor coge y suelta a su hora (el reloj del mundo marca ese instante), entre los movimientos que le llegan.
		auto ServerApply = [&](double ServerTime)
		{
			if (!bGrabbed && ServerTime >= Net.GrabAt)
			{
				bGrabbed = true;
				bServerCarrying = true;
				Server.World->TimeSeconds = Net.GrabAt;
				Stamina->SetSpeedCap(CarrySource(), 330.f);
			}
			if (!bReleased && ServerTime >= Net.ReleaseAt)
			{
				bReleased = true;
				bServerCarrying = false;
				Server.World->TimeSeconds = Net.ReleaseAt;
				Stamina->ClearSpeedCap(CarrySource());
			}
		};

		FNetResult Result;
		double LastArrival = 0.0;
		double LastProcessedStamp = -1.0;
		int32 Index = 0;
		for (double ClientTime = 0.0; ClientTime < Net.EndAt; ClientTime += Frame, ++Index)
		{
			const bool bLost = (ClientTime >= Net.LossFrom && ClientTime < Net.LossTo) || (Net.LossEvery > 0 && Index % Net.LossEvery == 1);
			if (bLost)
			{
				continue;
			}
			const double Known = ClientTime - Net.Downlink;
			const bool bClientKnows = Known >= Net.GrabAt && Known < Net.ReleaseAt;
			const bool bStalled = ClientTime >= Net.StallFrom && ClientTime < Net.StallTo;
			const double Arrival = FMath::Max(LastArrival, (bStalled ? Net.StallTo : ClientTime) + Net.Uplink);
			LastArrival = Arrival;

			// Lo que el servidor haya hecho antes de que llegue este movimiento.
			ServerApply(Arrival);
			Server.World->TimeSeconds = Arrival;
			const float Delta = LastProcessedStamp < 0.0 ? Frame : FMath::Min(ServerMaxMoveDelta, static_cast<float>(ClientTime - LastProcessedStamp));
			LastProcessedStamp = ClientTime;

			const uint8 Claimed = bClientKnows ? PredictedCapCarryBit : 0;
			const uint8 Applied = Stamina->ConsumeClientPredictedCaps(Claimed, Delta);
			++Result.Processed;
			Result.Mismatches += Applied != Claimed ? 1 : 0;
			Result.CappedMoves += Applied != 0 ? 1 : 0;
			if (bClientKnows != bServerCarrying)
			{
				const double ChangedAt = bServerCarrying ? Net.GrabAt : Net.ReleaseAt;
				Result.LatestStaleArrival = FMath::Max(Result.LatestStaleArrival, Arrival - ChangedAt);
			}
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPredictedCapsRuleTest,
	"Tortunabo.Movement.PredictedCaps.Rule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPredictedCapsRuleTest::RunTest(const FString& Parameters)
{
	using namespace TNMovementLimits;
	constexpr float Frame = TNPredictedCapsTest::Frame;

	TestEqual(TEXT("Llevar a otra es predicho"), PredictedCapBit(CarrySource()), PredictedCapCarryBit);
	TestEqual(TEXT("El mareo es predicho"), PredictedCapBit(MareoSource()), PredictedCapMareoBit);
	TestEqual(TEXT("El caparazón no"), PredictedCapBit(ShellSource()), static_cast<uint8>(0));

	const FPredictedCapGrace Closed;
	// De acuerdo: lo que haya.
	TestTrue(TEXT("Los dos con el tope: tope"), StepPredictedCap(Closed, true, true, 10.0, Frame).bApply);
	TestFalse(TEXT("Ninguno: sin tope"), StepPredictedCap(Closed, false, false, 0.0, Frame).bApply);
	// Sin ventana abierta manda el servidor, pida lo que pida el cliente (criterio de #575).
	TestFalse(TEXT("Pide el tope sin llevar a nadie y sin cambio reciente: sin tope"), StepPredictedCap(Closed, true, false, 10.0, Frame).bApply);
	TestTrue(TEXT("Lleva a otra y no lo pide, sin cambio reciente: tope"), StepPredictedCap(Closed, false, true, 10.0, Frame).bApply);

	// Recién cogida en el servidor (ventana abierta en el reloj 10), el dueño aún no lo sabe: sin tope, como el dueño.
	const FPredictedCapGrace Open = OpenPredictedCapGrace(Closed, 10.0);
	TestTrue(TEXT("El cambio abre la ventana"), Open.bOpen);
	TestFalse(TEXT("Cogida hace 0,1 s de movimientos y el dueño sin enterarse: sin tope"), StepPredictedCap(Open, false, true, 10.1, Frame).bApply);
	TestTrue(TEXT("Soltada hace 0,1 s y el dueño aún con ella: tope"), StepPredictedCap(Open, true, false, 10.1, Frame).bApply);
	TestTrue(TEXT("Pasada la ventana: manda el servidor"), StepPredictedCap(Open, false, true, 10.0 + PredictedCapGraceSeconds + 0.01, Frame).bApply);

	// Reconocer el cambio cierra la ventana.
	const FPredictedCapStep Ack = StepPredictedCap(Open, true, true, 10.1, Frame);
	TestFalse(TEXT("El dueño pide lo mismo que el servidor: la ventana se cierra"), Ack.Grace.bOpen);
	TestTrue(TEXT("Y después ya no se libra"), StepPredictedCap(Ack.Grace, false, true, 10.12, Frame).bApply);

	// Un cambio con la ventana abierta no la alarga.
	const FPredictedCapGrace Again = OpenPredictedCapGrace(Open, 10.4);
	TestEqual(TEXT("Otro cambio con la ventana abierta: la misma ventana"), Again.OpenedAt, 10.0);

	// Presupuesto: lo concedido lo gasta; sin presupuesto, manda el servidor aunque la ventana esté abierta.
	FPredictedCapGrace Empty = Open;
	Empty.Budget = 0.f;
	TestTrue(TEXT("Sin presupuesto: manda el servidor"), StepPredictedCap(Empty, false, true, 10.1, Frame).bApply);
	const FPredictedCapStep Spent = StepPredictedCap(Open, false, true, 10.1, 0.1f);
	TestTrue(TEXT("Conceder gasta el tiempo del movimiento"), FMath::IsNearlyEqual(Spent.Grace.Budget, PredictedCapGraceBudgetSeconds - 0.1f));
	const FPredictedCapStep Refill = StepPredictedCap(Empty, true, true, 20.0, 1.f);
	TestTrue(TEXT("Sin conceder se recarga despacio"), FMath::IsNearlyEqual(Refill.Grace.Budget, PredictedCapGraceRefillPerSecond));

	// El tope del movimiento: el menor de los no predichos y los predichos que lleva.
	const float Values[NumPredictedCaps] = { 250.f, 330.f };
	TestEqual(TEXT("Sin predichos: el de los demás"), ResolveMoveSpeedCap(400.f, 0, Values), 400.f);
	TestEqual(TEXT("Llevando a otra: 330"), ResolveMoveSpeedCap(400.f, PredictedCapCarryBit, Values), 330.f);
	TestEqual(TEXT("Llevando y mareada: el menor"), ResolveMoveSpeedCap(NoCap, PredictedCapAllBits, Values), 250.f);
	TestEqual(TEXT("Una zona más lenta manda igual"), ResolveMoveSpeedCap(100.f, PredictedCapAllBits, Values), 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPredictedCapsTimelineTest,
	"Tortunabo.Movement.PredictedCaps.Timeline",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPredictedCapsTimelineTest::RunTest(const FString& Parameters)
{
	// El servidor coge en 0,5 s y suelta en 2 s. El dueño se entera medio RTT después y pide el tope en sus movimientos, que
	// llegan otro medio RTT después. El servidor simula cada movimiento con el mismo tope que el dueño: ninguna corrección.
	for (const double Rtt : { 0.0, 0.1, 0.25, 0.45 })
	{
		TNPredictedCapsTest::FNetScenario Net;
		Net.Uplink = Rtt * 0.5;
		Net.Downlink = Rtt * 0.5;
		const TNPredictedCapsTest::FNetResult Result = TNPredictedCapsTest::RunNet(Net);
		TestEqual(*FString::Printf(TEXT("RTT %.0f ms: el servidor usa el tope del dueño en todos los movimientos"), Rtt * 1000.0), Result.Mismatches, 0);
		TestTrue(*FString::Printf(TEXT("RTT %.0f ms: el tope dura lo que la carga (%d movimientos)"), Rtt * 1000.0, Result.CappedMoves),
			FMath::Abs(Result.CappedMoves - 90) <= 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPredictedCapsLateMovesTest,
	"Tortunabo.Movement.PredictedCaps.LateMoves",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPredictedCapsLateMovesTest::RunTest(const FString& Parameters)
{
	// Hallazgo de la revisión de la PR #803: la gracia se decidía con el reloj del servidor al recibir el movimiento, así
	// que uno hecho antes de enterarse pero que llegaba más de 0,5 s tarde se simulaba con otras reglas y se corregía. Ahora
	// se mide con el tiempo de los movimientos: lo que cuenta es cuándo se hizo.
	{
		// Atasco de 0,8 s en la subida justo al coger: los movimientos hechos sin saberlo llegan 0,8 s después del cambio.
		TNPredictedCapsTest::FNetScenario Net;
		Net.StallFrom = 0.45;
		Net.StallTo = 1.25;
		const TNPredictedCapsTest::FNetResult Result = TNPredictedCapsTest::RunNet(Net);
		TestTrue(*FString::Printf(TEXT("Atasco al coger: llegan movimientos viejos más de 0,5 s tarde (%.2f s)"), Result.LatestStaleArrival),
			Result.LatestStaleArrival > 0.5);
		TestEqual(TEXT("Atasco al coger: el servidor usa el tope del dueño en todos"), Result.Mismatches, 0);
	}
	{
		// Igual al soltar, con un atasco de 0,9 s.
		TNPredictedCapsTest::FNetScenario Net;
		Net.StallFrom = 1.95;
		Net.StallTo = 2.85;
		const TNPredictedCapsTest::FNetResult Result = TNPredictedCapsTest::RunNet(Net);
		TestTrue(TEXT("Atasco al soltar: llegan movimientos viejos más de 0,5 s tarde"), Result.LatestStaleArrival > 0.5);
		TestEqual(TEXT("Atasco al soltar: el servidor usa el tope del dueño en todos"), Result.Mismatches, 0);
	}
	{
		// Pérdidas: uno de cada tres movimientos perdido y 0,6 s sin que llegue nada justo al enterarse, con RTT de 200 ms.
		// El servidor recorta el DeltaTime tras el hueco y aun así juzga bien.
		TNPredictedCapsTest::FNetScenario Net;
		Net.Uplink = 0.1;
		Net.Downlink = 0.1;
		Net.LossFrom = 0.6;
		Net.LossTo = 1.2;
		Net.LossEvery = 3;
		const TNPredictedCapsTest::FNetResult Result = TNPredictedCapsTest::RunNet(Net);
		TestTrue(TEXT("Pérdidas: se procesan movimientos"), Result.Processed > 80);
		TestEqual(TEXT("Pérdidas: el servidor usa el tope del dueño en todos"), Result.Mismatches, 0);
	}
	{
		// Atasco y pérdida a la vez al soltar: 0,7 s atascado tras perder 0,2 s.
		TNPredictedCapsTest::FNetScenario Net;
		Net.Uplink = 0.08;
		Net.Downlink = 0.08;
		Net.LossFrom = 1.9;
		Net.LossTo = 2.0;
		Net.StallFrom = 2.0;
		Net.StallTo = 2.7;
		const TNPredictedCapsTest::FNetResult Result = TNPredictedCapsTest::RunNet(Net);
		TestTrue(TEXT("Pérdida y atasco: llegan movimientos viejos más de 0,5 s tarde"), Result.LatestStaleArrival > 0.5);
		TestEqual(TEXT("Pérdida y atasco: el servidor usa el tope del dueño en todos"), Result.Mismatches, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPredictedCapsGraceAbuseTest,
	"Tortunabo.Movement.PredictedCaps.GraceAbuse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPredictedCapsGraceAbuseTest::RunTest(const FString& Parameters)
{
	// Hallazgo de la revisión de la PR #803: el cliente podía pedir «sin tope» 0,5 s tras cada cambio aunque ya lo hubiera
	// reconocido, y coger y soltar sin espera reiniciaba el plazo: un cliente modificado encadenaba la exención.
	using namespace TNMovementLimits;
	using namespace TNPredictedCapsTest;
	{
		UTN_StaminaComponent* Stamina = NewStamina();
		FeedUncapped(Stamina, 0, 1.f);
		Stamina->SetSpeedCap(CarrySource(), 330.f);
		TestEqual(TEXT("Recién cogida: el movimiento que no lo sabe va sin tope"), Stamina->ConsumeClientPredictedCaps(0, Frame), static_cast<uint8>(0));
		TestEqual(TEXT("El dueño lo reconoce"), Stamina->ConsumeClientPredictedCaps(PredictedCapCarryBit, Frame), PredictedCapCarryBit);
		TestEqual(TEXT("Reconocido, ya no puede pedir sin tope aunque el cambio sea de hace 2 fotogramas"),
			Stamina->ConsumeClientPredictedCaps(0, Frame), PredictedCapCarryBit);
	}
	{
		// Soltar y volver a coger entre dos movimientos, sin que el dueño reconozca nada: la ventana sigue siendo la primera.
		UTN_StaminaComponent* Stamina = NewStamina();
		Stamina->SetSpeedCap(CarrySource(), 330.f);
		float Uncapped = FeedUncapped(Stamina, 0, 0.3f);
		Stamina->ClearSpeedCap(CarrySource());
		Stamina->SetSpeedCap(CarrySource(), 330.f);
		Uncapped += FeedUncapped(Stamina, 0, 1.f);
		TestTrue(*FString::Printf(TEXT("Cambios seguidos sin reconocer: no pasan de una ventana (%.2f s)"), Uncapped),
			Uncapped <= PredictedCapGraceSeconds + Frame + KINDA_SMALL_NUMBER);
	}
	{
		// El tramposo nunca pide el tope y coge y suelta sin parar (0,5 s con ella, 2 fotogramas sin ella) durante 20 s.
		UTN_StaminaComponent* Stamina = NewStamina();
		float Uncapped = 0.f;
		float Carrying = 0.f;
		for (int32 Cycle = 0; Cycle < 38; ++Cycle)
		{
			Stamina->SetSpeedCap(CarrySource(), 330.f);
			Uncapped += FeedUncapped(Stamina, 0, 0.5f);
			Carrying += 0.5f;
			Stamina->ClearSpeedCap(CarrySource());
			FeedUncapped(Stamina, 0, 2.f * Frame);
		}
		// El presupuesto entero, lo recargado y, como mucho, un fotograma de más por ventana (se concede el movimiento entero).
		const float Bound = PredictedCapGraceBudgetSeconds + Carrying * PredictedCapGraceRefillPerSecond + 38 * Frame;
		TestTrue(*FString::Printf(TEXT("Coger y soltar sin parar: sin tope %.2f s de %.1f s llevándola (máximo %.2f)"), Uncapped, Carrying, Bound),
			Uncapped <= Bound);
		TestTrue(TEXT("Y lejos de la exención entera de antes"), Uncapped < Carrying * 0.3f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPredictedCapsStaminaTest,
	"Tortunabo.Movement.PredictedCaps.Stamina",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPredictedCapsStaminaTest::RunTest(const FString& Parameters)
{
	using namespace TNMovementLimits;

	UTN_StaminaComponent* Stamina = TNPredictedCapsTest::NewStamina();
	// Corriendo (4 m/s, hoja Stats #856): andando (2 m/s) ya va por debajo de los topes de llevar y del mareo.
	const float Run = Stamina->ComputeMaxWalkSpeed(true, 1.f);
	TestTrue(TEXT("Corre más deprisa que el tope de llevar"), Run > 330.f);

	Stamina->SetSpeedCap(CarrySource(), 330.f);
	TestEqual(TEXT("Coger pone el bit de llevar"), Stamina->GetPredictedCapMask(), PredictedCapCarryBit);
	// El movimiento manda: uno que no lo pide anda a la velocidad normal aunque esta máquina ya tenga el tope.
	TestEqual(TEXT("Movimiento sin el bit: velocidad normal"), Stamina->ComputeMoveMaxWalkSpeed(true, 1.f, 1.f, 0), Run);
	TestEqual(TEXT("Movimiento con el bit: 330"), Stamina->ComputeMoveMaxWalkSpeed(true, 1.f, 1.f, PredictedCapCarryBit), 330.f);

	Stamina->SetSpeedCap(TEXT("SlowZoneTest"), 200.f);
	TestEqual(TEXT("Un tope no predicho manda en cualquier movimiento"), Stamina->ComputeMoveMaxWalkSpeed(true, 1.f, 1.f, 0), 200.f);
	Stamina->ClearSpeedCap(TEXT("SlowZoneTest"));

	Stamina->SetSpeedCap(MareoSource(), 250.f);
	TestEqual(TEXT("Mareada y llevando: los dos bits"), Stamina->GetPredictedCapMask(), PredictedCapAllBits);
	TestEqual(TEXT("Con los dos: el menor"), Stamina->ComputeMoveMaxWalkSpeed(true, 1.f, 1.f, PredictedCapAllBits), 250.f);

	Stamina->ClearSpeedCap(CarrySource());
	Stamina->ClearSpeedCap(MareoSource());
	TestEqual(TEXT("Soltada y sin mareo: sin bits"), Stamina->GetPredictedCapMask(), static_cast<uint8>(0));
	// Recién soltada (el dueño aún no lo sabe): el movimiento que aún lo pide lleva el último tope.
	TestEqual(TEXT("Movimiento que aún lo pide: el último valor"), Stamina->ComputeMoveMaxWalkSpeed(true, 1.f, 1.f, PredictedCapCarryBit), 330.f);
	TestEqual(TEXT("Recién soltada: el servidor acepta lo que pide el dueño"),
		Stamina->ConsumeClientPredictedCaps(PredictedCapCarryBit, TNPredictedCapsTest::Frame), PredictedCapCarryBit);
	TestTrue(TEXT("El reloj de movimientos avanza con lo validado"), FMath::IsNearlyEqual(Stamina->GetServerMoveClock(), TNPredictedCapsTest::Frame, 1.e-6));
	return true;
}

#endif
