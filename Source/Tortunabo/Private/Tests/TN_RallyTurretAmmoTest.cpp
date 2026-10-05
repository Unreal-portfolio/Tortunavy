// Municiones de la torreta del Rally (y objetos de Karts que las reutilizan): alga (#770), ráfaga de erizos (#715), medusa saltarina (#771), arpón (#772) y pez globo (#773). Lógica pura (TNRallyTurret) y,
// para el charco, un mundo con física sin ventana (TN_RallyPhysicsTestKit.h). Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Turret; Quit" -nullrhi -unattended -NoSteam

#include "Misc/AutomationTest.h"
#include "TN_RallyPhysicsTestKit.h"
#include "EngineUtils.h"
#include "Vehicles/TN_RallyHarpoon.h"
#include "Vehicles/TN_RallyProjectile.h"
#include "Vehicles/TN_RallyPufferMine.h"
#include "Rally/TN_RallyLogic.h"
#include "Vehicles/TN_RallyTurretLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTurretAmmoTest
{
	using namespace TNRallyPhysicsMeasure;

	/** Losa de 100 × 100 m inclinada SlopeDeg alrededor de Y, con el centro de su cara de arriba en el origen. */
	AStaticMeshActor* SpawnSlope(UWorld& World, float SlopeDeg)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FRotator Tilt(SlopeDeg, 0.f, 0.f);
		const FVector Center = -Tilt.RotateVector(FVector::UpVector) * 50.0;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Slope = Cube ? World.SpawnActor<AStaticMeshActor>(Center, Tilt, Params) : nullptr;
		if (!Slope)
		{
			return nullptr;
		}
		UStaticMeshComponent* Mesh = Slope->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(Cube);
		Mesh->SetWorldScale3D(FVector(100.0, 100.0, 1.0));
		Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		return Slope;
	}

	/** Proyectil de alga sin buggy que lo dispare, en Where con Velocity. */
	ATN_RallyProjectile* LaunchAlga(UWorld& World, const FVector& Where, const FVector& Velocity)
	{
		const FTransform Spawn(Velocity.IsNearlyZero() ? FRotator::ZeroRotator : Velocity.Rotation(), Where);
		ATN_RallyProjectile* Projectile = World.SpawnActorDeferred<ATN_RallyProjectile>(ATN_RallyProjectile::StaticClass(), Spawn,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Projectile)
		{
			Projectile->Init(ETNRallyAmmo::Alga, Velocity, nullptr);
			Projectile->FinishSpawning(Spawn);
		}
		return Projectile;
	}

	ATN_RallyAlgaPuddle* FindPuddle(UWorld& World)
	{
		TActorIterator<ATN_RallyAlgaPuddle> It(&World);
		return It ? *It : nullptr;
	}

	/** Avanza hasta que aparece un charco o pasan Seconds. */
	ATN_RallyAlgaPuddle* WaitForPuddle(const FPhysicsWorld& Test, float Seconds)
	{
		for (int32 Step = 0; Step < FMath::RoundToInt32(Seconds * StepsPerSecond); ++Step)
		{
			Test.Step();
			if (ATN_RallyAlgaPuddle* Puddle = FindPuddle(*Test.World))
			{
				return Puddle;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretAlgaTest, "Tortunabo.Rally.Turret.Alga",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretAlgaTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	// Primera pasada de #770: frena más que antes (×0,6 y ×0,5) y resbala.
	TestEqual(TEXT("agarre en el charco ×0,35"), PuddleGripMultiplier(true), 0.35f);
	TestEqual(TEXT("velocidad máxima en el charco ×0,5"), PuddleSpeedCapCms(true), BuggyTopSpeedCms * 0.5f, 0.01f);
	const FAmmoSpec Spec = SpecFor(ETNRallyAmmo::Alga);
	TestTrue(TEXT("el alga cae antes: más gravedad que la normal"), Spec.GravityScale > 1.f);
	TestTrue(TEXT("y sale más despacio que antes (3500 cm/s)"), Spec.SpeedCms < 3500.f);
	// Con el cabeceo de la conductora sola (12°) y desde la torreta, cae a menos de 30 m (antes, a unos 50).
	TestTrue(TEXT("alcance con 12° de cabeceo por debajo de 30 m"), LobRangeCm(12.f, 150.f, Spec.SpeedCms, 980.f * Spec.GravityScale) < 3000.f);

	// Derrape al entrar: lo decide el servidor, más fuerte cuanto más rápido, nada parado y en los dos sentidos.
	TestEqual(TEXT("parado no derrapa"), PuddleEntrySpinDegPerSecond(0.f, true), 0.f);
	TestEqual(TEXT("casi parado tampoco"), PuddleEntrySpinDegPerSecond(AlgaSpinMinSpeedCms - 1.f, true), 0.f);
	TestEqual(TEXT("a velocidad, el giro entero"), PuddleEntrySpinDegPerSecond(AlgaSpinFullSpeedCms, true), AlgaSpinYawDegPerSecond);
	TestEqual(TEXT("y hacia el otro lado"), PuddleEntrySpinDegPerSecond(AlgaSpinFullSpeedCms * 2.f, false), -AlgaSpinYawDegPerSecond);
	const float Half = PuddleEntrySpinDegPerSecond((AlgaSpinMinSpeedCms + AlgaSpinFullSpeedCms) * 0.5f, true);
	TestTrue(TEXT("a media velocidad, a medias"), Half > 0.f && Half < AlgaSpinYawDegPerSecond);

	// Quien lo suelta en Karts no lo pisa al soltarlo; el resto, siempre.
	TestFalse(TEXT("quien lo suelta, al principio, no"), PuddleAffects(true, 0.1f));
	TestTrue(TEXT("quien lo suelta, pasado el margen, sí"), PuddleAffects(true, AlgaDropperGraceSeconds));
	TestTrue(TEXT("cualquier otro, desde el principio"), PuddleAffects(false, 0.f));

	// Suelo y plano del disco.
	TestTrue(TEXT("suelo llano"), IsPuddleGround(FVector::UpVector));
	TestTrue(TEXT("cuesta de 40°"), IsPuddleGround(FRotator(40.f, 0.f, 0.f).RotateVector(FVector::UpVector)));
	TestFalse(TEXT("una pared no es suelo"), IsPuddleGround(FVector::ForwardVector));
	const FRotator Tilt(15.f, 0.f, 0.f);
	TArray<FVector> Points = { FVector::ZeroVector };
	for (int32 Index = 0; Index < PuddleRimSamples; ++Index)
	{
		const float Angle = 2.f * PI * Index / PuddleRimSamples;
		Points.Add(Tilt.RotateVector(FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 400.f));
	}
	FVector Center;
	FVector Normal;
	TestTrue(TEXT("con puntos hay plano"), FitGroundPlane(Points, Center, Normal));
	TestTrue(TEXT("la normal del plano ajustado es la de la cuesta"), Normal.Equals(Tilt.RotateVector(FVector::UpVector), 0.001));
	TestTrue(TEXT("y el centro, el del disco"), Center.Equals(FVector::ZeroVector, 0.01));
	TestFalse(TEXT("sin puntos no hay plano"), FitGroundPlane(TArray<FVector>(), Center, Normal));
	const FQuat Rotation = PuddleRotation(Tilt.RotateVector(FVector::UpVector), FVector::ForwardVector);
	TestTrue(TEXT("el disco se apoya en la cuesta"), Rotation.GetUpVector().Equals(Tilt.RotateVector(FVector::UpVector), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretAlgaGroundTest, "Tortunabo.Rally.Turret.AlgaGround",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretAlgaGroundTest::RunTest(const FString& Parameters)
{
	using namespace TNTurretAmmoTest;
	constexpr float SlopeDeg = 15.f;
	{
		// Cuesta: el alga cae en una ladera de 15° y el charco queda con su inclinación, pegado al suelo.
		FPhysicsWorld Test(TEXT("TNRallyAlgaSlopeWorld"));
		if (!Test.World || !SpawnSlope(*Test.World, SlopeDeg))
		{
			AddError(TEXT("No se ha podido montar la ladera"));
			return false;
		}
		LaunchAlga(*Test.World, FVector(0.0, 0.0, 600.0), FVector(800.0, 0.0, -400.0));
		const ATN_RallyAlgaPuddle* Puddle = WaitForPuddle(Test, 3.f);
		if (!TestNotNull(TEXT("el alga deja charco en la ladera"), Puddle))
		{
			return false;
		}
		const FVector SlopeUp = FRotator(SlopeDeg, 0.f, 0.f).RotateVector(FVector::UpVector);
		TestTrue(TEXT("el charco sigue la inclinación del suelo"), FVector::DotProduct(Puddle->GetActorUpVector(), SlopeUp) > 0.995);
		const double AbovePlane = FVector::DotProduct(Puddle->GetActorLocation(), SlopeUp);
		TestTrue(FString::Printf(TEXT("y queda pegado a ella (%.1f cm)"), AbovePlane), FMath::Abs(AbovePlane) <= 10.0);
	}
	{
		// Se acaba en el aire: cae desde muy alto y su vida termina antes de llegar; el charco va igual al suelo de debajo.
		FPhysicsWorld Test(TEXT("TNRallyAlgaAirWorld"));
		if (!Test.World || !SpawnFlatGround(*Test.World))
		{
			AddError(TEXT("No se ha podido montar el suelo"));
			return false;
		}
		const TNRallyTurret::FAmmoSpec Spec = TNRallyTurret::SpecFor(ETNRallyAmmo::Alga);
		const double Fall = 0.5 * 980.0 * Spec.GravityScale * FMath::Square(Spec.LifeSeconds);
		LaunchAlga(*Test.World, FVector(0.0, 0.0, Fall + 2000.0), FVector::ZeroVector);
		const ATN_RallyAlgaPuddle* Puddle = WaitForPuddle(Test, Spec.LifeSeconds + 0.5f);
		if (TestNotNull(TEXT("el alga que se acaba en el aire deja charco"), Puddle))
		{
			TestTrue(FString::Printf(TEXT("en el suelo (Z = %.1f)"), Puddle->GetActorLocation().Z), FMath::Abs(Puddle->GetActorLocation().Z) <= 10.0);
		}
	}
	{
		// Impacto en un buggy: el charco va al suelo bajo él, no encima.
		FPhysicsWorld Test(TEXT("TNRallyAlgaBuggyWorld"));
		if (!Test.World || !SpawnFlatGround(*Test.World))
		{
			AddError(TEXT("No se ha podido montar el suelo"));
			return false;
		}
		ATN_Buggy* Buggy = SpawnBuggy(*Test.World, FTransform(FVector(0.0, 0.0, SpawnLiftCm)));
		if (!TestNotNull(TEXT("buggy"), Buggy))
		{
			return false;
		}
		Settle(Test, *Buggy);
		LaunchAlga(*Test.World, Buggy->GetActorLocation() + FVector(-800.0, 0.0, 60.0), FVector(3000.0, 0.0, 0.0));
		const ATN_RallyAlgaPuddle* Puddle = WaitForPuddle(Test, 2.f);
		if (TestNotNull(TEXT("el alga que da en un buggy deja charco"), Puddle))
		{
			TestTrue(FString::Printf(TEXT("en el suelo (Z = %.1f)"), Puddle->GetActorLocation().Z), FMath::Abs(Puddle->GetActorLocation().Z) <= 10.0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretErizosTest, "Tortunabo.Rally.Turret.Erizos",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretErizosTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	const FAmmoSpec Spec = SpecFor(ETNRallyAmmo::Erizos);
	TestTrue(TEXT("los erizos son munición especial"), IsSpecial(ETNRallyAmmo::Erizos));
	TestTrue(TEXT("y de ráfaga"), IsBurstAmmo(ETNRallyAmmo::Erizos) && !IsBurstAmmo(ETNRallyAmmo::Coco) && !IsBurstAmmo(ETNRallyAmmo::Alga));
	TestEqual(TEXT("púas rápidas: 9000 cm/s"), Spec.SpeedCms, 9000.f);
	TestEqual(TEXT("que caen poco: gravedad 0,3"), Spec.GravityScale, 0.3f);
	TestEqual(TEXT("retroceso de cada púa: 40 cm/s"), Spec.RecoilCms, 40.f);
	TestEqual(TEXT("una carga por caja (una ráfaga)"), TNRally::ChargesFor(ETNRallyAmmo::Erizos), 1);
	TestEqual(TEXT("la cadencia es la de las púas"), Spec.FireInterval, ErizosSpikeInterval, 0.0001f);

	// Gatillo mantenido (una petición por fotograma, más de las que hacen falta): 12 púas en 1,5 s y se acaba.
	constexpr double Dt = 1.0 / 60.0;
	FBurst Burst;
	int32 Spikes = 0;
	double LastSpikeAt = -1.0;
	for (double Now = 0.0; Now < 3.0; Now += Dt)
	{
		if (Now < 2.5)
		{
			Burst = HoldBurst(Burst, Now, BurstHoldSeconds(true));
		}
		while (BurstSpikeDue(Burst, Now))
		{
			Burst = AfterBurstSpike(Burst, Now);
			++Spikes;
			LastSpikeAt = Now;
			if (!IsBurstActive(Burst))
			{
				break;
			}
		}
		if (!IsBurstActive(Burst) && Spikes > 0)
		{
			break;
		}
	}
	TestEqual(TEXT("una carga son 12 púas"), Spikes, ErizosSpikes);
	TestTrue(FString::Printf(TEXT("en 1,5 s (la última a los %.2f s)"), LastSpikeAt), LastSpikeAt <= ErizosBurstSeconds && LastSpikeAt >= ErizosBurstSeconds - 0.25);

	// El servidor lleva la cadencia: pedir muchas veces seguidas no adelanta la siguiente púa.
	FBurst Spam = HoldBurst(FBurst(), 0.0, BurstHoldSeconds(true));
	TestTrue(TEXT("la primera púa sale al apretar"), BurstSpikeDue(Spam, 0.0));
	Spam = AfterBurstSpike(Spam, 0.0);
	for (int32 Request = 0; Request < 10; ++Request)
	{
		Spam = HoldBurst(Spam, 0.01 * Request, BurstHoldSeconds(true));
	}
	TestFalse(TEXT("10 peticiones en 0,1 s no sacan otra púa"), BurstSpikeDue(Spam, 0.1));
	TestTrue(TEXT("a su hora, sí"), BurstSpikeDue(Spam, ErizosSpikeInterval));
	TestEqual(TEXT("y no empiezan otra ráfaga"), Spam.SpikesLeft, ErizosSpikes - 1);

	// Al soltar el gatillo la ráfaga se para y sigue donde iba al volver a apretar.
	FBurst Released = AfterBurstSpike(HoldBurst(FBurst(), 0.0, BurstHoldSeconds(true)), 0.0);
	TestFalse(TEXT("suelto, no sale nada"), BurstSpikeDue(Released, 1.0));
	Released = HoldBurst(Released, 1.0, BurstHoldSeconds(true));
	TestTrue(TEXT("al volver a apretar, sale la siguiente"), BurstSpikeDue(Released, 1.0));
	TestEqual(TEXT("de la misma carga"), Released.SpikesLeft, ErizosSpikes - 1);
	Released = AfterBurstSpike(Released, 1.0);
	TestEqual(TEXT("tras una pausa, la siguiente a su intervalo"), Released.NextSpikeAt, 1.0 + ErizosSpikeInterval, 0.0001);

	// Un bot no mantiene nada: una petición le vale para la ráfaga entera.
	TestTrue(TEXT("el gatillo de un bot dura la ráfaga entera"), BurstHoldSeconds(false) >= ErizosBurstSeconds);
	TestTrue(TEXT("el de una persona se acaba si deja de apretar"), BurstHoldSeconds(true) < ErizosBurstSeconds);

	// Empujón de cada púa: de lado, y hacia el lado contrario al que da.
	TestTrue(TEXT("una púa desde la izquierda empuja a la derecha"),
		SpikePushDir(FVector::ForwardVector, FVector(0.2, 1.0, 0.0)).Equals(FVector(0.0, 1.0, 0.0), 0.001));
	TestTrue(TEXT("desde atrás, de lado igualmente"), FMath::IsNearlyZero(SpikePushDir(FVector::ForwardVector, FVector::ForwardVector).X));
	TestEqual(TEXT("empujón lateral 120 cm/s"), ErizosLateralCms, 120.f);
	TestEqual(TEXT("bamboleo 0,15 s"), ErizosWobbleSeconds, 0.15f);

	// Reparto y bots.
	const TNRally::FAmmoWeights First = TNRally::AmmoWeightsForPlace(1, 8);
	const TNRally::FAmmoWeights Middle = TNRally::AmmoWeightsForPlace(4, 8);
	const TNRally::FAmmoWeights Last = TNRally::AmmoWeightsForPlace(8, 8);
	TestTrue(TEXT("más erizos delante que detrás"), First.Erizos > Last.Erizos);
	TestTrue(TEXT("y en la mitad de la tabla, como delante"), Middle.Erizos >= First.Erizos * 0.9f && Middle.Erizos > Last.Erizos);
	using TNRally::EBotSpecialShot;
	TestEqual(TEXT("bot: erizos al de delante cerca"), static_cast<int32>(TNRally::ShouldBotFireSpecial(ETNRallyAmmo::Erizos, 0.5f, 2500.f, -1.f)),
		static_cast<int32>(EBotSpecialShot::AtAhead));
	TestEqual(TEXT("bot: con el de delante lejos, se espera"), static_cast<int32>(TNRally::ShouldBotFireSpecial(ETNRallyAmmo::Erizos, 0.5f, 9000.f, -1.f)),
		static_cast<int32>(EBotSpecialShot::Hold));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretMedusaTest, "Tortunabo.Rally.Turret.Medusa",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretMedusaTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	constexpr float Gravity = 980.f;
	TestTrue(TEXT("la medusa es especial"), IsSpecial(ETNRallyAmmo::Medusa));
	TestTrue(TEXT("y no lanza nada: actúa sobre el propio buggy"), IsSelfAmmo(ETNRallyAmmo::Medusa) && !IsSelfAmmo(ETNRallyAmmo::Mortero));
	TestEqual(TEXT("dos cargas por caja"), TNRally::ChargesFor(ETNRallyAmmo::Medusa), 2);
	TestEqual(TEXT("sin retroceso"), SpecFor(ETNRallyAmmo::Medusa).RecoilCms, 0.f);
	// El impulso que sube 3 m en llano: sqrt(2 · 980 · 300) ≈ 767 cm/s.
	TestEqual(TEXT("el impulso de 3 m"), HopUpCms(JellyfishHopCm, Gravity), 766.8f, 0.5f);
	const float Apex = HopApexCm(JellyfishUpCms, Gravity);
	TestTrue(FString::Printf(TEXT("el de la medusa sube 3 m ± 0,5 (%.0f cm)"), Apex), FMath::Abs(Apex - 300.f) <= 50.f);
	TestTrue(TEXT("en el suelo se puede botar"), CanHop(false));
	TestFalse(TEXT("en el aire, no"), CanHop(true));
	TestFalse(TEXT("en el aire no le toca el charco"), PuddleAffects(false, 1.f, true));
	TestTrue(TEXT("en el suelo, sí"), PuddleAffects(false, 1.f, false));

	// Peligros que hacen botar a un bot.
	TestTrue(TEXT("una teledirigida que le persigue a 20 m"), IsShellThreat(FVector::ZeroVector, FVector(-2000.0, 0.0, 0.0), true));
	TestFalse(TEXT("la misma, si persigue a otro"), IsShellThreat(FVector::ZeroVector, FVector(-2000.0, 0.0, 0.0), false));
	TestFalse(TEXT("la suya, pero lejos"), IsShellThreat(FVector::ZeroVector, FVector(-6000.0, 0.0, 0.0), true));
	TestTrue(TEXT("un charco 20 m por delante"), IsPuddleAhead(FVector::ZeroVector, FVector::ForwardVector, FVector(2000.0, 300.0, 0.0)));
	TestFalse(TEXT("un charco detrás"), IsPuddleAhead(FVector::ZeroVector, FVector::ForwardVector, FVector(-1000.0, 0.0, 0.0)));
	TestFalse(TEXT("un charco a un lado"), IsPuddleAhead(FVector::ZeroVector, FVector::ForwardVector, FVector(1000.0, 2000.0, 0.0)));
	TestFalse(TEXT("un charco muy lejos"), IsPuddleAhead(FVector::ZeroVector, FVector::ForwardVector, FVector(9000.0, 0.0, 0.0)));

	using TNRally::EBotSpecialShot;
	auto Shot = [](float Held, bool bThreat) { return static_cast<int32>(TNRally::ShouldBotFireSpecial(ETNRallyAmmo::Medusa, Held, 2000.f, 2000.f, bThreat)); };
	TestEqual(TEXT("bot: con un peligro, bota ya"), Shot(0.2f, true), static_cast<int32>(EBotSpecialShot::Free));
	TestEqual(TEXT("bot: sin peligro, se espera"), Shot(0.2f, false), static_cast<int32>(EBotSpecialShot::Hold));
	TestEqual(TEXT("bot: sin peligro, al rato bota"), Shot(TNRally::BotJellyfishDelaySeconds, false), static_cast<int32>(EBotSpecialShot::Free));
	TestTrue(TEXT("más medusas detrás que delante"), TNRally::AmmoWeightsForPlace(8, 8).Medusa > TNRally::AmmoWeightsForPlace(1, 8).Medusa);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretMedusaHopTest, "Tortunabo.Rally.Turret.MedusaHop",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretMedusaHopTest::RunTest(const FString& Parameters)
{
	using namespace TNTurretAmmoTest;
	// Buggy parado en llano con el impulso de la medusa: sube unos 3 m (± 0,5) y vuelve al suelo.
	FPhysicsWorld Test(TEXT("TNRallyMedusaHopWorld"));
	if (!Test.World || !SpawnFlatGround(*Test.World))
	{
		AddError(TEXT("No se ha podido montar el suelo"));
		return false;
	}
	ATN_Buggy* Buggy = SpawnBuggy(*Test.World, FTransform(FVector(0.0, 0.0, SpawnLiftCm)));
	if (!TestNotNull(TEXT("buggy"), Buggy))
	{
		return false;
	}
	Settle(Test, *Buggy);
	TestFalse(TEXT("parado está en el suelo"), Buggy->IsAirborne());
	const double Rest = Buggy->GetActorLocation().Z;
	Buggy->ApplyVelocityImpulse(FVector::UpVector * TNRallyTurret::JellyfishUpCms);
	double Peak = Rest;
	bool bWasAirborne = false;
	for (int32 Step = 0; Step < 3 * StepsPerSecond; ++Step)
	{
		Buggy->SetAIDriveInput(0.f, 0.f, 0.f, true);
		Test.Step();
		Peak = FMath::Max(Peak, Buggy->GetActorLocation().Z);
		bWasAirborne |= Buggy->IsAirborne();
	}
	const double Rise = Peak - Rest;
	TestTrue(FString::Printf(TEXT("sube unos 3 m (%.0f cm)"), Rise), FMath::Abs(Rise - 300.0) <= 50.0);
	TestTrue(TEXT("y va por el aire"), bWasAirborne);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretArponTest, "Tortunabo.Rally.Turret.Arpon",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretArponTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	const FAmmoSpec Spec = SpecFor(ETNRallyAmmo::Arpon);
	TestEqual(TEXT("arpón rápido: 7000 cm/s"), Spec.SpeedCms, 7000.f);
	TestEqual(TEXT("que cae poco: gravedad 0,2"), Spec.GravityScale, 0.2f);
	TestEqual(TEXT("una carga por caja"), TNRally::ChargesFor(ETNRallyAmmo::Arpon), 1);
	TestEqual(TEXT("remolca 2 s"), HarpoonSeconds, 2.f);

	// Tira hacia el alcanzado, en horizontal, con 1800 cm/s².
	const float Dt = 1.f / 60.f;
	const FVector Pull = HarpoonPullAccel(FVector::ZeroVector, FVector(3000.0, 0.0, 500.0), Dt);
	TestTrue(TEXT("parado: 1800 cm/s² hacia el alcanzado"), Pull.Equals(FVector(HarpoonAccelCms2, 0.0, 0.0), 0.01));
	const FVector Side = HarpoonPullAccel(FVector(2000.0, 0.0, 0.0), FVector(0.0, -3000.0, 0.0), Dt);
	TestTrue(TEXT("hacia donde esté, también de lado"), Side.Equals(FVector(0.0, -HarpoonAccelCms2, 0.0), 0.01));
	TestTrue(TEXT("pegado a él ya no tira"), HarpoonPullAccel(FVector::ZeroVector, FVector(HarpoonMinDistanceCm - 1.f, 0.0, 0.0), Dt).IsNearlyZero());

	// Tope: el 115 % de la punta, sin pasarse nunca aunque se integre a pasos.
	const float Cap = HarpoonTopSpeedCms();
	TestEqual(TEXT("tope al 115 % de la punta"), Cap, BuggyTopSpeedCms * 1.15f, 0.01f);
	TestTrue(TEXT("en el tope no tira"), HarpoonPullAccel(FVector(Cap, 0.0, 0.0), FVector(3000.0, 0.0, 0.0), Dt).IsNearlyZero());
	TestTrue(TEXT("pasado el tope (cuesta abajo) tampoco"), HarpoonPullAccel(FVector(Cap + 500.f, 0.0, 0.0), FVector(3000.0, 0.0, 0.0), Dt).IsNearlyZero());
	FVector Velocity(BuggyTopSpeedCms, 0.0, 0.0);
	float MaxSpeed = 0.f;
	for (int32 Step = 0; Step < 2 * 60; ++Step)
	{
		Velocity += HarpoonPullAccel(Velocity, FVector(5000.0, 0.0, 0.0), Dt) * Dt;
		MaxSpeed = FMath::Max(MaxSpeed, static_cast<float>(Velocity.X));
	}
	TestTrue(FString::Printf(TEXT("a la punta, el remolque la sube (%.0f cm/s)"), MaxSpeed), MaxSpeed > BuggyTopSpeedCms + 100.f);
	TestTrue(TEXT("y nunca pasa del tope"), MaxSpeed <= Cap + 0.01f);

	// La cuerda se suelta con la reaparición de cualquiera de los dos o si se separan más que el alcance del arpón.
	TestTrue(TEXT("más lejos que el alcance del vuelo"), HarpoonMaxDistanceCm > HarpoonSpeedCms * HarpoonLifeSeconds);
	TestTrue(TEXT("a 60 m, sin reaparecer, sigue"), HarpoonHolds(6000.f, false, false));
	TestTrue(TEXT("justo en el máximo, sigue"), HarpoonHolds(HarpoonMaxDistanceCm, false, false));
	TestFalse(TEXT("pasado el máximo, se suelta"), HarpoonHolds(HarpoonMaxDistanceCm + 1.f, false, false));
	TestFalse(TEXT("reaparece el que tira, se suelta"), HarpoonHolds(1000.f, true, false));
	TestFalse(TEXT("reaparece el alcanzado, se suelta"), HarpoonHolds(1000.f, false, true));

	// Bots y reparto.
	TestTrue(TEXT("bot: al de delante a 30 m"), BotHarpoonInRange(3000.f));
	TestFalse(TEXT("bot: pegado (10 m), no"), BotHarpoonInRange(1000.f));
	TestFalse(TEXT("bot: lejos (80 m), no"), BotHarpoonInRange(8000.f));
	TestFalse(TEXT("bot: sin nadie delante, no"), BotHarpoonInRange(-1.f));
	using TNRally::EBotSpecialShot;
	TestEqual(TEXT("bot: arpón al de delante"), static_cast<int32>(TNRally::ShouldBotFireSpecial(ETNRallyAmmo::Arpon, 0.5f, 3000.f, -1.f)),
		static_cast<int32>(EBotSpecialShot::AtAhead));
	TestTrue(TEXT("más arpones detrás que delante"), TNRally::AmmoWeightsForPlace(8, 8).Arpon > TNRally::AmmoWeightsForPlace(1, 8).Arpon);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretArponTowTest, "Tortunabo.Rally.Turret.ArponTow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretArponTowTest::RunTest(const FString& Parameters)
{
	using namespace TNTurretAmmoTest;
	// Dos buggies parados a 30 m: el arpón que da en el de delante remolca al que lo dispara; con escudo, no.
	for (const bool bShielded : { false, true })
	{
		FPhysicsWorld Test(bShielded ? TEXT("TNRallyArponShieldWorld") : TEXT("TNRallyArponWorld"));
		if (!Test.World || !SpawnFlatGround(*Test.World))
		{
			AddError(TEXT("No se ha podido montar el suelo"));
			return false;
		}
		ATN_Buggy* Shooter = SpawnBuggy(*Test.World, FTransform(FVector(0.0, 0.0, SpawnLiftCm)));
		ATN_Buggy* Ahead = SpawnBuggy(*Test.World, FTransform(FVector(3000.0, 0.0, SpawnLiftCm)));
		if (!TestNotNull(TEXT("buggies"), Shooter) || !Ahead)
		{
			return false;
		}
		Settle(Test, *Shooter);
		if (bShielded)
		{
			Ahead->GrantShield();
		}
		const FVector Muzzle = Shooter->GetActorLocation() + FVector(300.0, 0.0, 60.0);
		const FTransform Spawn(FRotator::ZeroRotator, Muzzle);
		ATN_RallyProjectile* Harpoon = Test.World->SpawnActorDeferred<ATN_RallyProjectile>(ATN_RallyProjectile::StaticClass(), Spawn,
			Shooter, Shooter, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!TestNotNull(TEXT("arpón"), Harpoon))
		{
			return false;
		}
		Harpoon->Init(ETNRallyAmmo::Arpon, FVector(TNRallyTurret::HarpoonSpeedCms, 0.0, 0.0), Shooter);
		Harpoon->FinishSpawning(Spawn);
		bool bTethered = false;
		float MaxSpeed = 0.f;
		for (int32 Step = 0; Step < FMath::RoundToInt32(1.5f * StepsPerSecond); ++Step)
		{
			Test.Step();
			bTethered |= TActorIterator<ATN_RallyHarpoonTether>(Test.World) ? true : false;
			MaxSpeed = FMath::Max(MaxSpeed, static_cast<float>(Shooter->GetVelocity().X));
		}
		if (bShielded)
		{
			TestFalse(TEXT("con escudo no hay remolque"), bTethered);
			TestFalse(TEXT("y el escudo se gasta"), Ahead->IsShielded());
			TestTrue(FString::Printf(TEXT("el que dispara no se mueve (%.0f cm/s)"), MaxSpeed), MaxSpeed < 100.f);
		}
		else
		{
			TestTrue(TEXT("el arpón se clava y tira"), bTethered);
			TestTrue(FString::Printf(TEXT("el que dispara gana velocidad hacia el de delante (%.0f cm/s)"), MaxSpeed), MaxSpeed > 300.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretArponRespawnTest, "Tortunabo.Rally.Turret.ArponRespawn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretArponRespawnTest::RunTest(const FString& Parameters)
{
	using namespace TNTurretAmmoTest;
	const auto TetherAlive = [](UWorld* World) { return static_cast<bool>(TActorIterator<ATN_RallyHarpoonTether>(World)); };
	// La reaparición teletransporta el mismo actor: la cuerda se suelta si reaparece cualquiera de los dos (fantasma) o si
	// el salto los separa más que HarpoonMaxDistanceCm.
	enum class ECase : uint8 { PullerRespawns, TargetRespawns, TooFar };
	for (const ECase Case : { ECase::PullerRespawns, ECase::TargetRespawns, ECase::TooFar })
	{
		FPhysicsWorld Test(*FString::Printf(TEXT("TNRallyArponRespawnWorld%d"), static_cast<int32>(Case)));
		if (!Test.World || !SpawnFlatGround(*Test.World))
		{
			AddError(TEXT("No se ha podido montar el suelo"));
			return false;
		}
		ATN_Buggy* Puller = SpawnBuggy(*Test.World, FTransform(FVector(0.0, 0.0, SpawnLiftCm)));
		ATN_Buggy* Target = SpawnBuggy(*Test.World, FTransform(FVector(3000.0, 0.0, SpawnLiftCm)));
		if (!TestNotNull(TEXT("buggies"), Puller) || !Target)
		{
			return false;
		}
		Settle(Test, *Puller);
		if (!TestNotNull(TEXT("arpón clavado"), ATN_RallyHarpoonTether::Attach(Puller, Target, Target->GetActorLocation())))
		{
			return false;
		}
		Test.Advance(0.2f);
		TestTrue(TEXT("antes de reaparecer, la cuerda sigue"), TetherAlive(Test.World));
		switch (Case)
		{
		case ECase::PullerRespawns:
			Puller->RallyTeleport(FTransform(FVector(0.0, -1500.0, SpawnLiftCm)), 0.f, 2.f);
			break;
		case ECase::TargetRespawns:
			Target->RallyTeleport(FTransform(FVector(3000.0, 1500.0, SpawnLiftCm)), 0.f, 2.f);
			break;
		case ECase::TooFar:
			// Sin fantasma: solo la distancia la suelta.
			Target->RallyTeleport(FTransform(FVector(3000.0, TNRallyTurret::HarpoonMaxDistanceCm + 2000.0, SpawnLiftCm)), 0.f, 0.f);
			break;
		}
		Test.Advance(0.1f);
		TestFalse(*FString::Printf(TEXT("caso %d: la cuerda se suelta"), static_cast<int32>(Case)), TetherAlive(Test.World));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretPezGloboTest, "Tortunabo.Rally.Turret.PezGlobo",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretPezGloboTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	TestTrue(TEXT("el pez globo deja una mina"), IsMineAmmo(ETNRallyAmmo::PezGlobo) && !IsMineAmmo(ETNRallyAmmo::Mortero));
	TestEqual(TEXT("dos cargas por caja"), TNRally::ChargesFor(ETNRallyAmmo::PezGlobo), 2);
	TestEqual(TEXT("se queda 15 s"), PufferLifeSeconds, 15.f);
	TestFalse(TEXT("antes de armarse (0,4 s) no explota"), PufferTriggers(0.4f, 100.f, false));
	TestTrue(TEXT("armada, un buggy a 3 m la dispara"), PufferTriggers(0.6f, 300.f, false));
	TestFalse(TEXT("a 5 m, no"), PufferTriggers(5.f, 500.f, false));
	TestFalse(TEXT("quien la lanza, en su inmunidad (1 s), no"), PufferTriggers(1.f, 100.f, true));
	TestTrue(TEXT("pasada la inmunidad, también quien la lanza"), PufferTriggers(PufferThrowerImmuneSeconds, 100.f, true));
	TestEqual(TEXT("sin disparar no se hincha"), PufferInflate(-1.f), 1.f);
	TestEqual(TEXT("al explotar, hinchada del todo"), PufferInflate(PufferInflateSeconds), PufferInflateScale);
	TestEqual(TEXT("disparada con vida de sobra, no la cambia"), PufferLifeOnTrigger(10.f), 10.f);
	TestTrue(TEXT("disparada a 0,1 s del final, vive hasta explotar"), PufferLifeOnTrigger(0.1f) > PufferInflateSeconds);
	TestTrue(TEXT("sin vida por delante, también"), PufferLifeOnTrigger(0.f) > PufferInflateSeconds);
	using TNRally::EBotSpecialShot;
	TestEqual(TEXT("bot: con alguien detrás a 30 m, se la deja"),
		static_cast<int32>(TNRally::ShouldBotFireSpecial(ETNRallyAmmo::PezGlobo, 0.5f, 2000.f, 3000.f)), static_cast<int32>(EBotSpecialShot::AtBehind));
	TestEqual(TEXT("bot: sin nadie cerca detrás, se espera"),
		static_cast<int32>(TNRally::ShouldBotFireSpecial(ETNRallyAmmo::PezGlobo, 0.5f, 2000.f, 6000.f)), static_cast<int32>(EBotSpecialShot::Hold));
	TestTrue(TEXT("más peces globo delante que detrás"), TNRally::AmmoWeightsForPlace(1, 8).PezGlobo > TNRally::AmmoWeightsForPlace(8, 8).PezGlobo);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretPezGloboMineTest, "Tortunabo.Rally.Turret.PezGloboMine",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretPezGloboMineTest::RunTest(const FString& Parameters)
{
	using namespace TNTurretAmmoTest;
	const auto MineAlive = [](UWorld* World) { return static_cast<bool>(TActorIterator<ATN_RallyPufferMine>(World)); };
	for (const bool bThrower : { true, false })
	{
		// Un buggy parado a 2 m de la mina: si es quien la lanzó, no explota hasta pasada su inmunidad; si es otro, al armarse.
		FPhysicsWorld Test(bThrower ? TEXT("TNRallyPufferThrowerWorld") : TEXT("TNRallyPufferOtherWorld"));
		if (!Test.World || !SpawnFlatGround(*Test.World))
		{
			AddError(TEXT("No se ha podido montar el suelo"));
			return false;
		}
		ATN_Buggy* Buggy = SpawnBuggy(*Test.World, FTransform(FVector(200.0, 0.0, SpawnLiftCm)));
		if (!TestNotNull(TEXT("buggy"), Buggy))
		{
			return false;
		}
		Settle(Test, *Buggy);
		const ATN_RallyPufferMine* Mine = ATN_RallyPufferMine::SpawnOnGround(Test.World, FVector(0.0, 0.0, 50.0), bThrower ? Buggy : nullptr);
		if (!TestNotNull(TEXT("la mina se queda en el suelo"), Mine))
		{
			return false;
		}
		TestTrue(FString::Printf(TEXT("apoyada en él (Z = %.1f)"), Mine->GetActorLocation().Z), FMath::Abs(Mine->GetActorLocation().Z) <= 5.0);
		Test.Advance(0.4f);
		TestTrue(TEXT("sin armar no explota"), MineAlive(Test.World));
		Test.Advance(bThrower ? 0.8f : 0.6f);
		TestEqual(bThrower ? TEXT("quien la lanza, en su inmunidad, no la dispara") : TEXT("otro buggy la dispara al armarse"), MineAlive(Test.World), bThrower);
		float MaxUp = 0.f;
		for (int32 Step = 0; Step < 2 * StepsPerSecond; ++Step)
		{
			Test.Step();
			MaxUp = FMath::Max(MaxUp, static_cast<float>(Buggy->GetVelocity().Z));
		}
		TestFalse(TEXT("acaba explotando"), MineAlive(Test.World));
		if (bThrower)
		{
			TestTrue(FString::Printf(TEXT("y la explosión del mortero lo levanta (%.0f cm/s)"), MaxUp), MaxUp > 200.f);
		}
	}
	{
		// Nadie la pisa: desaparece a los 15 s.
		FPhysicsWorld Test(TEXT("TNRallyPufferLifeWorld"));
		if (!Test.World || !SpawnFlatGround(*Test.World))
		{
			AddError(TEXT("No se ha podido montar el suelo"));
			return false;
		}
		ATN_RallyPufferMine::SpawnOnGround(Test.World, FVector(0.0, 0.0, 50.0), nullptr);
		Test.Advance(TNRallyTurret::PufferLifeSeconds - 0.5f);
		TestTrue(TEXT("a los 14,5 s sigue"), MineAlive(Test.World));
		Test.Advance(1.f);
		TestFalse(TEXT("a los 15 s, fuera"), MineAlive(Test.World));
	}
	{
		// La pisan 0,2 s antes de que se acabe su vida (menos que el hinchado): explota igual.
		FPhysicsWorld Test(TEXT("TNRallyPufferLateWorld"));
		if (!Test.World || !SpawnFlatGround(*Test.World))
		{
			AddError(TEXT("No se ha podido montar el suelo"));
			return false;
		}
		ATN_Buggy* Buggy = SpawnBuggy(*Test.World, FTransform(FVector(5000.0, 0.0, SpawnLiftCm)));
		if (!TestNotNull(TEXT("buggy"), Buggy))
		{
			return false;
		}
		Settle(Test, *Buggy);
		ATN_RallyPufferMine::SpawnOnGround(Test.World, FVector(0.0, 0.0, 50.0), nullptr);
		Test.Advance(TNRallyTurret::PufferLifeSeconds - 0.2f);
		TestTrue(TEXT("a los 14,8 s sigue sin disparar"), MineAlive(Test.World));
		// A la altura a la que ya reposa: sin caer, el impulso de la explosión se mide entero.
		const FVector Rest = Buggy->GetActorLocation();
		Buggy->RallyTeleport(FTransform(Buggy->GetActorRotation(), FVector(200.0, 0.0, Rest.Z)), 0.f, 0.f);
		float MaxUp = 0.f;
		for (int32 Step = 0; Step < StepsPerSecond; ++Step)
		{
			Test.Step();
			MaxUp = FMath::Max(MaxUp, static_cast<float>(Buggy->GetVelocity().Z));
		}
		TestFalse(TEXT("pasado su tiempo, ya no está"), MineAlive(Test.World));
		TestTrue(FString::Printf(TEXT("pero ha explotado y lo levanta (%.0f cm/s)"), MaxUp), MaxUp > 200.f);
	}
	return true;
}

#endif
