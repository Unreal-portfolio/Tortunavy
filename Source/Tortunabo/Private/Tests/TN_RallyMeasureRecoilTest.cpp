// Retroceso de la torreta y vuelcos del buggy del Rally con física y sin ventana (#695).
//  - RecoilNoFlip: cada munición especial disparada hacia delante y hacia atrás, parado y a 50 km/h en llano, como la
//    dispara el piloto IA (UTN_BuggyTurretComponent::TryFire con su retroceso): el buggy no vuelca en los 3 s siguientes.
//    Caso negativo: el levantamiento del mortero de antes de #695 (700 · 0,8 = 560 cm/s en el morro) sí lo vuelca.
// Las carreras de 3 vueltas sin ventana de #695 dieron 22 vuelcos, todos 0,2-0,4 s después de que el bot gastara un
// mortero (20 disparos): el retroceso del mortero levantaba el morro lo bastante para darle la vuelta.
// Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Measure.Recoil; Quit" -nullrhi -unattended -NoSteam

#include "Misc/AutomationTest.h"
#include "TN_RallyPhysicsTestKit.h"
#include "Components/SkeletalMeshComponent.h"
#include "Vehicles/TN_RallyCombatLogic.h"
#include "Vehicles/TN_RallyTurretLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyMeasureRecoil
{
	using namespace TNRallyPhysicsMeasure;

	constexpr float CruiseKmh = 50.f;
	constexpr float WatchSeconds = 3.f;
	/** Levantamiento del mortero antes de #695: RecoilCms 700 por RecoilLiftRatio 0,8, sin tope. */
	constexpr float LegacyMortarLiftCms = 700.f * 0.8f;

	struct FShot
	{
		bool bFlipped = false;
		float MaxTiltDeg = 0.f;
		bool bReachedSpeed = true;
	};

	/** Acelera en línea recta hasta Kmh (0: se queda parado). */
	bool ReachSpeed(const FPhysicsWorld& Test, ATN_Buggy& Buggy, float TargetKmh)
	{
		for (int32 Step = 0; Step < 15 * StepsPerSecond && Kmh(Buggy) < TargetKmh; ++Step)
		{
			Buggy.SetAIDriveInput(1.f, 0.f, HoldHeadingSteer(Buggy), false);
			Test.Step();
		}
		return Kmh(Buggy) >= TargetKmh;
	}

	/** Mira WatchSeconds manteniendo la velocidad de crucero (o parado) y anota el vuelco y la inclinación máxima. */
	void Watch(const FPhysicsWorld& Test, ATN_Buggy& Buggy, float TargetKmh, FShot& Out)
	{
		for (int32 Step = 0; Step < FMath::RoundToInt32(WatchSeconds * StepsPerSecond); ++Step)
		{
			const bool bMoving = TargetKmh > 0.f;
			Buggy.SetAIDriveInput(bMoving && Kmh(Buggy) < TargetKmh ? 1.f : 0.f, 0.f, HoldHeadingSteer(Buggy), !bMoving);
			Test.Step();
			const double UpZ = FMath::Clamp(Buggy.GetActorUpVector().Z, -1.0, 1.0);
			Out.MaxTiltDeg = FMath::Max(Out.MaxTiltDeg, static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(UpZ))));
			Out.bFlipped |= Buggy.IsFlipped();
		}
	}

	/** Buggy nuevo en una losa nueva, a TargetKmh; Fire dispara (o empuja) y se mira qué pasa. */
	template <typename FFire>
	FShot ShotOnFreshGround(float TargetKmh, FFire&& Fire)
	{
		FShot Out;
		FPhysicsWorld Test(TEXT("TNRallyRecoilWorld"));
		if (!Test.World || !SpawnFlatGround(*Test.World))
		{
			Out.bReachedSpeed = false;
			return Out;
		}
		ATN_Buggy* Buggy = SpawnBuggy(*Test.World, FTransform(FVector(-120000.0, 0.0, SpawnLiftCm)));
		if (!Buggy)
		{
			Out.bReachedSpeed = false;
			return Out;
		}
		Settle(Test, *Buggy);
		Out.bReachedSpeed = ReachSpeed(Test, *Buggy, TargetKmh);
		Fire(*Buggy);
		Watch(Test, *Buggy, TargetKmh, Out);
		return Out;
	}

	/** Como el bot: AIFire hacia Direction (en espacio del buggy: +X delante) con una carga de Ammo. */
	FShot FireOnFreshGround(ETNRallyAmmo Ammo, float TargetKmh, bool bBackward)
	{
		return ShotOnFreshGround(TargetKmh, [Ammo, bBackward](ATN_Buggy& Buggy)
		{
			Buggy.GiveSpecialAmmo(Ammo, 1);
			Buggy.AIFire(Buggy.GetActorForwardVector() * (bBackward ? -1.0 : 1.0), true);
		});
	}

	/** El retroceso del mortero de antes de #695: el frenazo de 700 cm/s y 560 cm/s hacia arriba en el morro. */
	FShot LegacyMortarOnFreshGround(float TargetKmh)
	{
		return ShotOnFreshGround(TargetKmh, [](ATN_Buggy& Buggy)
		{
			const FVector Forward = Buggy.GetActorForwardVector();
			Buggy.ApplyVelocityImpulse(TNRallyTurret::RecoilVelocity(Forward, TNRallyTurret::SpecFor(ETNRallyAmmo::Mortero).RecoilCms));
			USkeletalMeshComponent* Chassis = Buggy.GetMesh();
			const FVector Nose = Buggy.GetActorTransform().TransformPosition(FVector(0.9f * TNRallyCombat::DefaultHalfLengthCm, 0.f, 0.f));
			Chassis->AddImpulseAtLocation(Buggy.GetActorUpVector() * LegacyMortarLiftCms * Chassis->GetMass(), Nose);
		});
	}

	FString Describe(const TCHAR* Label, const FShot& Shot)
	{
		return FString::Printf(TEXT("%s: inclinación máxima %.0f grados, %s%s"), Label, Shot.MaxTiltDeg,
			Shot.bFlipped ? TEXT("VUELCA") : TEXT("no vuelca"), Shot.bReachedSpeed ? TEXT("") : TEXT(" (no llegó a la velocidad)"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyMeasureRecoilNoFlipTest, "Tortunabo.Rally.Measure.RecoilNoFlip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyMeasureRecoilNoFlipTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyMeasureRecoil;
	const ETNRallyAmmo Specials[] = { ETNRallyAmmo::Mortero, ETNRallyAmmo::Ancla, ETNRallyAmmo::Concha, ETNRallyAmmo::Alga,
		ETNRallyAmmo::Tinta };
	for (const ETNRallyAmmo Ammo : Specials)
	{
		for (const float Speed : { 0.f, CruiseKmh })
		{
			for (const bool bBackward : { false, true })
			{
				const FShot Shot = FireOnFreshGround(Ammo, Speed, bBackward);
				const FString Label = FString::Printf(TEXT("%s hacia %s a %.0f km/h"), *UEnum::GetValueAsString(Ammo),
					bBackward ? TEXT("atrás") : TEXT("delante"), Speed);
				AddInfo(Describe(*Label, Shot));
				TestTrue(*FString::Printf(TEXT("%s: llega a la velocidad"), *Label), Shot.bReachedSpeed);
				TestFalse(*FString::Printf(TEXT("%s: no vuelca"), *Label), Shot.bFlipped);
			}
		}
	}
	const FShot Legacy = LegacyMortarOnFreshGround(CruiseKmh);
	AddInfo(Describe(TEXT("Mortero con el levantamiento de antes de #695 a 50 km/h"), Legacy));
	TestTrue(TEXT("caso negativo: el levantamiento de antes de #695 vuelca el buggy"), Legacy.bFlipped);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
