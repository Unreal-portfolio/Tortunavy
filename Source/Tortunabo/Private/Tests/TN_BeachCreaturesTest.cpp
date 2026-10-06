// Criaturas y peligros del Excel de diseño (lote #691, #683-#690): las reglas puras de TN_BeachCreatureRules.h y de los
// tentáculos (TN_BeachTrampolineRules.h), sin mundo ni actores. Una prueba por criatura; ninguna regla quita vida.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Beach.Creatures; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/Beach/TN_BeachCreatureRules.h"
#include "World/Beach/TN_BeachTrampolineRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNBeachCreaturesTest
{
	/** Cápsula de la tortuga con margen (cm): radio y medio alto. */
	constexpr double TurtleRadius = 60.0;
	constexpr double TurtleHalfHeight = 70.0;
	/** Velocidades de la tortuga (UTN_StaminaComponent): andar y correr. */
	constexpr float WalkSpeed = 450.f;
	constexpr float RunSpeed = 800.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// #683 Medusa: tentáculos
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachCreaturesTentaclesTest, "Tortunabo.Beach.Creatures.Tentacles",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachCreaturesTentaclesTest::RunTest(const FString& Parameters)
{
	using namespace TNTrampolineRules;
	constexpr double Bell = 500.0;
	constexpr double Reach = 670.0;
	constexpr double Top = 70.0;
	TestTrue(TEXT("en el anillo, desde la arena: pica"), TentacleContact(600.0, 0.0, Bell, Reach, Top) == ETentacleContact::Sting);
	TestTrue(TEXT("pegada a la campana, en la arena: pica"), TentacleContact(480.0, 5.0, Bell, Reach, Top) == ETentacleContact::Sting);
	TestTrue(TEXT("encima de la campana: rebota sin picar"), TentacleContact(200.0, 250.0, Bell, Reach, Top) == ETentacleContact::Bell);
	TestTrue(TEXT("fuera del alcance: nada"), TentacleContact(700.0, 0.0, Bell, Reach, Top) == ETentacleContact::None);
	TestTrue(TEXT("saltando por encima del anillo: nada"), TentacleContact(600.0, 200.0, Bell, Reach, Top) == ETentacleContact::None);
	TestTrue(TEXT("efecto corto: aturde menos de 1 s"), StingStunSeconds > 0.f && StingStunSeconds < 1.f);
	TestTrue(TEXT("ralentiza (60 %) un rato (3 s)"), FMath::IsNearlyEqual(StingSpeedFactor, 0.6f) && FMath::IsNearlyEqual(StingSlowSeconds, 3.f));
	TestTrue(TEXT("no vuelve a picar antes de acabar la ralentización"), StingCooldown >= StingStunSeconds);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// #684 Arenas movedizas
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachCreaturesQuicksandTest, "Tortunabo.Beach.Creatures.Quicksand",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachCreaturesQuicksandTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachCreatureRules;
	float Prev = 2.f;
	bool bMonotonic = true;
	for (float T = 0.f; T <= 4.f; T += 0.25f)
	{
		const float F = Quicksand::SpeedFactor(T);
		bMonotonic &= F <= Prev + KINDA_SMALL_NUMBER;
		Prev = F;
	}
	TestTrue(TEXT("la velocidad baja de forma progresiva"), bMonotonic);
	TestTrue(TEXT("al entrar, 70 %"), FMath::IsNearlyEqual(Quicksand::SpeedFactor(0.f), 0.7f));
	TestTrue(TEXT("a media rampa, entre los dos"), Quicksand::SpeedFactor(1.25f) < 0.7f && Quicksand::SpeedFactor(1.25f) > 0.25f);
	TestTrue(TEXT("al final de la rampa, el mínimo (25 %) y ahí se queda"), FMath::IsNearlyEqual(Quicksand::SpeedFactor(2.5f), 0.25f)
		&& FMath::IsNearlyEqual(Quicksand::SpeedFactor(10.f), 0.25f));
	TestFalse(TEXT("antes de N s no atrapa"), Quicksand::ShouldTrap(2.9f, 3.f));
	TestTrue(TEXT("a los N s atrapa"), Quicksand::ShouldTrap(3.f, 3.f));
	TestFalse(TEXT("atrapada y sin escapar: sigue"), Quicksand::ShouldRelease(1.f, 4.f, false));
	TestTrue(TEXT("machacar salto la libera antes del tope"), Quicksand::ShouldRelease(1.f, 4.f, true));
	TestTrue(TEXT("al tope de tiempo sale sola (nunca muere)"), Quicksand::ShouldRelease(4.f, 4.f, false));

	// Forcejeo: pulsar despacio no basta; machacar, sí.
	FMashCounter Slow;
	for (int32 k = 0; k < 10; ++k) { Slow.Press(k * 1.0, EscapeDecay); }
	TestFalse(TEXT("una pulsación por segundo no suelta"), HasEscaped(Slow, 9.0));
	FMashCounter Fast;
	for (int32 k = 0; k < 10; ++k) { Fast.Press(k * 0.2, EscapeDecay); }
	TestTrue(TEXT("cinco pulsaciones por segundo sueltan en menos de 2 s"), HasEscaped(Fast, 1.8));
	Fast.Reset();
	TestFalse(TEXT("al rearmar, la cuenta vuelve a cero"), HasEscaped(Fast, 1.5));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// #685 Cangrejo arrastrador
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachCreaturesDragCrabTest, "Tortunabo.Beach.Creatures.DragCrab",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachCreaturesDragCrabTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachCreatureRules::DragCrab;
	TestTrue(TEXT("persigue más despacio de lo que corre la tortuga (380 < 800)"), IsEscapableChaseSpeed(380.f, TNBeachCreaturesTest::RunSpeed));
	// Arrastre de 600 cm a 230 cm/s en pasos de 1/60 s: nunca se pasa.
	float Dragged = 0.f;
	int32 Steps = 0;
	while (DragEnd(Dragged, 600.f, false, false, true) == EDragEnd::None && Steps < 10000)
	{
		Dragged += DragStep(Dragged, 600.f, 230.f, 1.f / 60.f);
		++Steps;
	}
	TestTrue(TEXT("arrastra como mucho la distancia máxima"), Dragged <= 600.f + KINDA_SMALL_NUMBER && Dragged >= 599.f);
	TestTrue(TEXT("al llegar, la suelta derribada"), DragEnd(600.f, 600.f, false, false, true) == EDragEnd::Distance);
	TestTrue(TEXT("machacar salto la libera"), DragEnd(100.f, 600.f, true, false, true) == EDragEnd::Escaped);
	TestTrue(TEXT("un golpe lanzado la libera (y manda sobre el escape)"), DragEnd(100.f, 600.f, true, true, true) == EDragEnd::HitStunned);
	TestTrue(TEXT("delante no es seguro: la suelta antes"), DragEnd(100.f, 600.f, false, false, false) == EDragEnd::Unsafe);
	TestFalse(TEXT("sin suelo delante no es seguro"), IsSafeAhead(false, 0.f, false));
	TestFalse(TEXT("un desnivel de 2 m no es seguro"), IsSafeAhead(true, -200.f, false));
	TestFalse(TEXT("una zona de muerte o de rescate no es segura"), IsSafeAhead(true, 0.f, true));
	TestTrue(TEXT("arena llana: seguro"), IsSafeAhead(true, -20.f, false));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// #686 Cangrejo subterráneo
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachCreaturesBurrowCrabTest, "Tortunabo.Beach.Creatures.BurrowCrab",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachCreaturesBurrowCrabTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachCreatureRules::BurrowCrab;
	const FTimes T;
	FInput In;
	TestTrue(TEXT("enterrado sin nadie: sigue enterrado"), Next(EState::Buried, In, T) == EState::Buried);
	In.bTargetNear = true;
	TestTrue(TEXT("con una tortuga cerca: avisa (tiembla)"), Next(EState::Buried, In, T) == EState::Tell);
	In = FInput();
	In.Age = T.Tell * 0.5f;
	TestTrue(TEXT("el aviso dura"), Next(EState::Tell, In, T) == EState::Tell);
	In.Age = T.Tell;
	TestTrue(TEXT("tras el aviso, saca la pinza"), Next(EState::Tell, In, T) == EState::Strike);
	In.Age = T.Strike;
	TestTrue(TEXT("quien siguió corriendo lo esquiva: recarga"), Next(EState::Strike, In, T) == EState::Recharge);
	In.bTargetInGrab = true;
	TestTrue(TEXT("al alcance de la pinza: la atrapa"), Next(EState::Strike, In, T) == EState::Hold);
	In = FInput();
	In.bHolding = true;
	In.Age = 1.f;
	TestTrue(TEXT("sujetándola"), Next(EState::Hold, In, T) == EState::Hold);
	In.bEscaped = true;
	TestTrue(TEXT("machacar salto la suelta (lanzada)"), Next(EState::Hold, In, T) == EState::Recharge);
	In.bEscaped = false;
	In.Age = T.HoldMax;
	TestTrue(TEXT("al acabar el tiempo la lanza"), Next(EState::Hold, In, T) == EState::Recharge);
	In = FInput();
	In.bTargetNear = true;
	In.Age = T.Recharge * 0.5f;
	TestTrue(TEXT("en recarga no vuelve a atacar aunque haya alguien"), Next(EState::Recharge, In, T) == EState::Recharge);
	TestFalse(TEXT("en recarga no empieza un ataque"), CanStartAttack(EState::Recharge));
	In.Age = T.Recharge;
	TestTrue(TEXT("recargado: se entierra"), Next(EState::Recharge, In, T) == EState::Buried);
	In = FInput();
	In.bHitStunned = true;
	In.bTargetNear = true;
	TestTrue(TEXT("un objeto lanzado lo esconde sin atacar"), Next(EState::Buried, In, T) == EState::Hide);
	TestTrue(TEXT("también si estaba sujetando"), Next(EState::Hold, In, T) == EState::Hide);
	In.Age = T.Hide + 1.f;
	TestTrue(TEXT("escondido mientras dura el mareo"), Next(EState::Hide, In, T) == EState::Hide);
	In.bHitStunned = false;
	TestTrue(TEXT("se le pasa: recarga antes de volver"), Next(EState::Hide, In, T) == EState::Recharge);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// #687 Erizo enterrado
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachCreaturesUrchinSpikesTest, "Tortunabo.Beach.Creatures.UrchinSpikes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachCreaturesUrchinSpikesTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachCreatureRules::UrchinSpikes;
	FTimes T;
	T.Tell = 0.15f;
	T.Out = 1.2f;
	T.Recharge = 3.f;
	TestTrue(TEXT("nunca ha saltado: listo"), CanTrigger(0.0, -1.0, T));
	TestTrue(TEXT("aviso muy corto"), PhaseAt(10.1, 10.0, T) == EPhase::Tell);
	TestTrue(TEXT("pinchos fuera"), PhaseAt(10.5, 10.0, T) == EPhase::Out);
	TestTrue(TEXT("fuera derriban"), Strikes(PhaseAt(10.5, 10.0, T)));
	TestTrue(TEXT("recargando"), PhaseAt(12.0, 10.0, T) == EPhase::Recharge);
	TestFalse(TEXT("durante la recarga no derriba"), Strikes(PhaseAt(12.0, 10.0, T)));
	TestFalse(TEXT("durante la recarga no salta otra vez"), CanTrigger(12.0, 10.0, T));
	TestFalse(TEXT("durante el aviso no derriba todavía"), Strikes(PhaseAt(10.1, 10.0, T)));
	TestTrue(TEXT("tras la recarga, listo otra vez"), CanTrigger(10.0 + T.Tell + T.Out + T.Recharge + 0.01, 10.0, T));
	T.Recharge = 6.f;
	TestFalse(TEXT("recarga configurable"), CanTrigger(14.5, 10.0, T));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// #688 Erizos checos
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachCreaturesTankTrapTest, "Tortunabo.Beach.Creatures.TankTrap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachCreaturesTankTrapTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachCreatureRules::TankTrap;
	TestTrue(TEXT("parada a su lado: nada"), Impact(0.f) == EImpact::None);
	TestTrue(TEXT("andando contra él: solo bloquea"), Impact(TNBeachCreaturesTest::WalkSpeed) == EImpact::Block);
	TestTrue(TEXT("corriendo contra él: rebote y derribo"), Impact(TNBeachCreaturesTest::RunSpeed) == EImpact::KnockDown);
	TestTrue(TEXT("justo en el umbral: derribo"), Impact(KnockSpeed) == EImpact::KnockDown);
	TestTrue(TEXT("el umbral está entre andar y correr"), KnockSpeed > TNBeachCreaturesTest::WalkSpeed && KnockSpeed < TNBeachCreaturesTest::RunSpeed);
	// #698: también derriba la bola de caparazón, con su respuesta; despacio, ninguno.
	TestTrue(TEXT("a pie y deprisa: derribo con ragdoll"), ResponseFor(EBody::Walker, TNBeachCreaturesTest::RunSpeed) == EResponse::KnockDownWalker);
	TestTrue(TEXT("bola deprisa: rebote y mareo en la bola"), ResponseFor(EBody::Ball, TNBeachCreaturesTest::RunSpeed) == EResponse::StunBall);
	for (const EBody Body : { EBody::Walker, EBody::Ball })
	{
		TestTrue(TEXT("despacio no derriba nada"), ResponseFor(Body, TNBeachCreaturesTest::WalkSpeed) == EResponse::None);
		TestTrue(TEXT("parado no derriba nada"), ResponseFor(Body, 0.f) == EResponse::None);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// #689 Búnker refugio
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachCreaturesBunkerTest, "Tortunabo.Beach.Creatures.Bunker",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachCreaturesBunkerTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachCreatureRules::Shelter;
	TArray<FBunkerDims> Cases;
	Cases.Add(FBunkerDims());
	for (const double R : { 350.0, 400.0, 450.0 })
	{
		Cases.Add(FormationBunker(R, 260.0));
	}
	for (const FBunkerDims& D : Cases)
	{
		const FString Ctx = FString::Printf(TEXT("búnker %.0f x %.0f"), D.Interior.X, D.Interior.Y);
		TestTrue(Ctx + TEXT(": una tortuga cabe por la puerta y dentro"), TurtleFitsDoor(D, TNBeachCreaturesTest::TurtleRadius, TNBeachCreaturesTest::TurtleHalfHeight));
		const TArray<FBunkerBox> Boxes = BunkerBoxes(D);
		// La puerta (en -X, a ras de suelo) queda libre: ninguna pared se mete en el hueco por donde pasa la cápsula.
		const FVector DoorCenter(-D.Interior.X - D.Wall * 0.5, 0.0, TNBeachCreaturesTest::TurtleHalfHeight);
		const FVector DoorHalf(D.Wall, TNBeachCreaturesTest::TurtleRadius, TNBeachCreaturesTest::TurtleHalfHeight - 5.0);
		bool bDoorFree = true;
		for (const FBunkerBox& B : Boxes)
		{
			const bool bOverlap = FMath::Abs(B.Center.X - DoorCenter.X) < B.Half.X + DoorHalf.X && FMath::Abs(B.Center.Y - DoorCenter.Y) < B.Half.Y + DoorHalf.Y
				&& FMath::Abs(B.Center.Z - DoorCenter.Z) < B.Half.Z + DoorHalf.Z;
			bDoorFree &= !bOverlap;
		}
		TestTrue(Ctx + TEXT(": la puerta queda libre"), bDoorFree);
		TestTrue(Ctx + TEXT(": tiene techo sobre el interior"), Boxes.Num() > 0 && Boxes.Last().Center.Z - Boxes.Last().Half.Z >= D.Interior.Z - 1.0
			&& Boxes.Last().Half.X >= D.Interior.X && Boxes.Last().Half.Y >= D.Interior.Y);
		const FVector Half(D.Interior.X, D.Interior.Y, D.Interior.Z * 0.5);
		TestTrue(Ctx + TEXT(": de pie dentro, en el refugio"), IsInside(FVector(0.0, 0.0, TNBeachCreaturesTest::TurtleHalfHeight), Half));
		TestFalse(Ctx + TEXT(": fuera, no"), IsInside(FVector(D.Interior.X + D.Wall + 100.0, 0.0, TNBeachCreaturesTest::TurtleHalfHeight), Half));
		TestFalse(Ctx + TEXT(": en el techo, no"), IsInside(FVector(0.0, 0.0, D.Interior.Z + D.Roof + TNBeachCreaturesTest::TurtleHalfHeight), Half));
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// #690 Basura y trinchera
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachCreaturesTrashTrenchTest, "Tortunabo.Beach.Creatures.TrashTrench",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachCreaturesTrashTrenchTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachCreatureRules;
	TestFalse(TEXT("basura: andando contra ella no se tropieza"), TrashPile::Trips(TNBeachCreaturesTest::WalkSpeed, false));
	TestTrue(TEXT("basura: corriendo, tropezón"), TrashPile::Trips(TNBeachCreaturesTest::RunSpeed, false));
	TestFalse(TEXT("basura rota: ya no hace tropezar"), TrashPile::Trips(TNBeachCreaturesTest::RunSpeed, true));
	TestTrue(TEXT("basura: un objeto lanzado la rompe"), TrashPile::BreaksFrom(true, false, 0.f));
	TestTrue(TEXT("basura: una bola de caparazón deprisa la rompe"), TrashPile::BreaksFrom(false, true, 1000.f));
	TestFalse(TEXT("basura: una bola despacio, no"), TrashPile::BreaksFrom(false, true, 300.f));

	constexpr float Wall = 130.f;
	constexpr float GravityZ = -2400.f;
	const float JumpCap = Trench::MaxJumpSpeedInPit(Wall, GravityZ);
	const float Apex = JumpCap * JumpCap / (2.f * FMath::Abs(GravityZ));
	TestTrue(FString::Printf(TEXT("trinchera: dentro, el salto (vértice %.0f cm) no llega al borde (%.0f)"), Apex, Wall), Apex < Wall - 20.f);
	TestTrue(TEXT("trinchera: la rampa de salida se sube andando"), Trench::IsRampWalkable(Wall, 210.f));
	TestFalse(TEXT("trinchera: una pared vertical no"), Trench::IsRampWalkable(Wall, 20.f));
	const FVector2D Pit(380.0, 240.0);
	TestTrue(TEXT("trinchera: en el fondo, dentro del hoyo"), Trench::IsInPit(FVector(0.0, 0.0, 0.0), Pit, Wall));
	TestFalse(TEXT("trinchera: encima del caballón, fuera"), Trench::IsInPit(FVector(0.0, 0.0, Wall), Pit, Wall));
	TestFalse(TEXT("trinchera: al lado, fuera"), Trench::IsInPit(FVector(0.0, 400.0, 0.0), Pit, Wall));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
