// Topes de velocidad predichos en el movimiento (#575 llevar a otra, #574 mareo): el dueño pide en cada movimiento los
// topes que conoce y el servidor simula ese movimiento con lo que pide mientras su propio cambio sea reciente; pasada la
// gracia manda lo del servidor. Se testean las reglas de TN_MovementLimits.h y UTN_StaminaComponent, que las usa
// UTN_TurtleMovementComponent::GetMaxSpeed.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Movement.PredictedCaps; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_MovementLimits.h"
#include "Player/TN_StaminaComponent.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPredictedCapsRuleTest,
	"Tortunabo.Movement.PredictedCaps.Rule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPredictedCapsRuleTest::RunTest(const FString& Parameters)
{
	using namespace TNMovementLimits;

	TestEqual(TEXT("Llevar a otra es predicho"), PredictedCapBit(CarrySource()), PredictedCapCarryBit);
	TestEqual(TEXT("El mareo es predicho"), PredictedCapBit(MareoSource()), PredictedCapMareoBit);
	TestEqual(TEXT("El caparazón no"), PredictedCapBit(ShellSource()), static_cast<uint8>(0));

	// De acuerdo: lo que haya.
	TestTrue(TEXT("Los dos con el tope: tope"), ShouldApplyPredictedCap(true, true, 10.0));
	TestFalse(TEXT("Ninguno: sin tope"), ShouldApplyPredictedCap(false, false, 0.0));
	// Recién cogida en el servidor, el dueño aún no lo sabe: sin tope, como el dueño.
	TestFalse(TEXT("Cogida hace 0,1 s y el dueño sin enterarse: sin tope"), ShouldApplyPredictedCap(false, true, 0.1));
	// Recién soltada en el servidor, el dueño aún la lleva: con tope, como el dueño.
	TestTrue(TEXT("Soltada hace 0,1 s y el dueño aún con ella: tope"), ShouldApplyPredictedCap(true, false, 0.1));
	// Criterio de #575: el bit sin carga válida en el servidor no aplica el tope pasada la gracia.
	TestFalse(TEXT("Pide el tope sin llevar a nadie, pasada la gracia: sin tope"), ShouldApplyPredictedCap(true, false, PredictedCapGraceSeconds + 0.01));
	TestFalse(TEXT("Pide el tope y el servidor nunca lo ha puesto: sin tope"), ShouldApplyPredictedCap(true, false, 1.e9));
	// Y al revés: un dueño que no lo pide no se libra pasada la gracia.
	TestTrue(TEXT("Lleva a otra y no lo pide, pasada la gracia: tope"), ShouldApplyPredictedCap(false, true, PredictedCapGraceSeconds + 0.01));

	// Los dos topes a la vez, cada uno con su gracia.
	const double Since[NumPredictedCaps] = { 0.05, 3.0 };
	TestEqual(TEXT("Mareo recién puesto sin pedir y llevar pedido sin carga desde hace 3 s: ninguno"),
		ResolvePredictedCaps(PredictedCapCarryBit, PredictedCapMareoBit, Since), static_cast<uint8>(0));
	TestEqual(TEXT("Basura en los bits altos: se ignoran"),
		ResolvePredictedCaps(0xFC, 0, Since), static_cast<uint8>(0));

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
	using namespace TNMovementLimits;

	// El servidor coge en GrabAt y suelta en ReleaseAt (su reloj). El dueño se entera medio RTT después y pide el tope en
	// sus movimientos (cada 1/60 s), que llegan al servidor otro medio RTT después. Con la regla, el servidor simula cada
	// movimiento con el mismo tope que el dueño: ninguna diferencia de velocidad, ninguna corrección.
	constexpr double GrabAt = 0.5;
	constexpr double ReleaseAt = 2.0;
	for (const double Rtt : { 0.0, 0.1, 0.25, 0.45 })
	{
		const double OneWay = Rtt * 0.5;
		int32 Mismatches = 0;
		int32 CappedMoves = 0;
		for (double ClientTime = 0.0; ClientTime < 3.0; ClientTime += 1.0 / 60.0)
		{
			const bool bClientKnows = ClientTime >= GrabAt + OneWay && ClientTime < ReleaseAt + OneWay;
			const double ArrivesAt = ClientTime + OneWay;
			const bool bServerActive = ArrivesAt >= GrabAt && ArrivesAt < ReleaseAt;
			const double ChangedAt = ArrivesAt >= ReleaseAt ? ReleaseAt : (ArrivesAt >= GrabAt ? GrabAt : -1.e9);
			const double Since[NumPredictedCaps] = { 1.e9, ArrivesAt - ChangedAt };
			const uint8 Claimed = bClientKnows ? PredictedCapCarryBit : 0;
			const uint8 Applied = ResolvePredictedCaps(Claimed, bServerActive ? PredictedCapCarryBit : 0, Since);
			Mismatches += Applied != Claimed ? 1 : 0;
			CappedMoves += Applied != 0 ? 1 : 0;
		}
		TestEqual(*FString::Printf(TEXT("RTT %.0f ms: el servidor usa el tope del dueño en todos los movimientos"), Rtt * 1000.0), Mismatches, 0);
		TestTrue(*FString::Printf(TEXT("RTT %.0f ms: el tope dura lo que la carga (%d movimientos)"), Rtt * 1000.0, CappedMoves),
			FMath::Abs(CappedMoves - 90) <= 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPredictedCapsStaminaTest,
	"Tortunabo.Movement.PredictedCaps.Stamina",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPredictedCapsStaminaTest::RunTest(const FString& Parameters)
{
	using namespace TNMovementLimits;

	UTN_StaminaComponent* Stamina = NewObject<UTN_StaminaComponent>(GetTransientPackage());
	const float Walk = Stamina->GetWalkSpeed();
	TestTrue(TEXT("Anda más deprisa que el tope de llevar"), Walk > 330.f);

	Stamina->SetSpeedCap(CarrySource(), 330.f);
	TestEqual(TEXT("Coger pone el bit de llevar"), Stamina->GetPredictedCapMask(), PredictedCapCarryBit);
	// El movimiento manda: uno que no lo pide anda a la velocidad normal aunque esta máquina ya tenga el tope.
	TestEqual(TEXT("Movimiento sin el bit: velocidad normal"), Stamina->ComputeMoveMaxWalkSpeed(false, 1.f, 1.f, 0), Walk);
	TestEqual(TEXT("Movimiento con el bit: 330"), Stamina->ComputeMoveMaxWalkSpeed(false, 1.f, 1.f, PredictedCapCarryBit), 330.f);

	Stamina->SetSpeedCap(TEXT("SlowZoneTest"), 200.f);
	TestEqual(TEXT("Un tope no predicho manda en cualquier movimiento"), Stamina->ComputeMoveMaxWalkSpeed(false, 1.f, 1.f, 0), 200.f);
	Stamina->ClearSpeedCap(TEXT("SlowZoneTest"));

	Stamina->SetSpeedCap(MareoSource(), 250.f);
	TestEqual(TEXT("Mareada y llevando: los dos bits"), Stamina->GetPredictedCapMask(), PredictedCapAllBits);
	TestEqual(TEXT("Con los dos: el menor"), Stamina->ComputeMoveMaxWalkSpeed(false, 1.f, 1.f, PredictedCapAllBits), 250.f);

	Stamina->ClearSpeedCap(CarrySource());
	Stamina->ClearSpeedCap(MareoSource());
	TestEqual(TEXT("Soltada y sin mareo: sin bits"), Stamina->GetPredictedCapMask(), static_cast<uint8>(0));
	// Recién soltada (el dueño aún no lo sabe): el movimiento que aún lo pide lleva el último tope.
	TestEqual(TEXT("Movimiento que aún lo pide: el último valor"), Stamina->ComputeMoveMaxWalkSpeed(false, 1.f, 1.f, PredictedCapCarryBit), 330.f);
	TestEqual(TEXT("Recién soltada: el servidor acepta lo que pide el dueño"), Stamina->ResolveClientPredictedCaps(PredictedCapCarryBit), PredictedCapCarryBit);
	return true;
}

#endif
