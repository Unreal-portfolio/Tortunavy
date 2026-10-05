// Municiones de la torreta del Rally (y objetos de Karts que las reutilizan): alga (#770). Lógica pura (TNRallyTurret) y,
// para el charco, un mundo con física sin ventana (TN_RallyPhysicsTestKit.h). Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Turret; Quit" -nullrhi -unattended -NoSteam

#include "Misc/AutomationTest.h"
#include "TN_RallyPhysicsTestKit.h"
#include "EngineUtils.h"
#include "Vehicles/TN_RallyProjectile.h"
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

#endif
