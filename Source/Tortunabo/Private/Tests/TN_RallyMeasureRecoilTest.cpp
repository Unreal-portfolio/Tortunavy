// Retroceso de la torreta y vuelcos del buggy del Rally con física y sin ventana (#695).
//  - RecoilNoFlip: cada munición especial disparada hacia delante y hacia atrás, parado y a 50 km/h en llano, como la
//    dispara el piloto IA (UTN_BuggyTurretComponent::TryFire con su retroceso): el buggy no vuelca en los 3 s siguientes.
//    El mortero solo se mide: su levantamiento ya no tiene tope (700 · 0,8 = 560 cm/s en el morro; Decisión del 06-10 en
//    #775) y puede volcarlo, que es lo natural.
// Las carreras de 3 vueltas sin ventana de #695 dieron 22 vuelcos, todos 0,2-0,4 s después de que el bot gastara un
// mortero (20 disparos): el retroceso del mortero levanta el morro lo bastante para darle la vuelta.
// Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Measure.Recoil; Quit" -nullrhi -unattended -NoSteam

#include "Misc/AutomationTest.h"
#include "TN_RallyPhysicsTestKit.h"
#include "Vehicles/TN_RallyTurretLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyMeasureRecoil
{
	using namespace TNRallyPhysicsMeasure;

	constexpr float CruiseKmh = 50.f;
	constexpr float WatchSeconds = 3.f;

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
				// El mortero puede volcarlo (sin tope, #695): solo se anota la medición.
				if (Ammo != ETNRallyAmmo::Mortero)
				{
					TestFalse(*FString::Printf(TEXT("%s: no vuelca"), *Label), Shot.bFlipped);
				}
			}
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
