// Estadísticas de la tortuga de la hoja Stats (#856): velocidades, salto, estamina corriendo y panzazo, leídas de
// BP_TortugaCharacter (la tortuga que se juega) y de los valores de serie de C++. Sin mundo.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Stats; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Player/TN_DiveDecisions.h"
#include "Player/TN_StaminaComponent.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "Player/TN_TurtleStats.h"
#include "Player/TortugaCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTurtleStatsTest
{
	const TCHAR* const TurtleBlueprintPath = TEXT("/Game/Blueprints/Characters/BP_TortugaCharacter.BP_TortugaCharacter_C");

	/** Un float protegido (EditDefaultsOnly) por reflexión, como lo ve el editor. */
	float ReadFloat(const UObject* Object, const TCHAR* Name)
	{
		const FFloatProperty* Prop = Object ? FindFProperty<FFloatProperty>(Object->GetClass(), Name) : nullptr;
		return Prop ? Prop->GetPropertyValue_InContainer(Object) : -1.f;
	}

	/**
	 * Lo que se arrastra por la tripa sobre arena llana tras el panzazo (cm): entra a LandingSpeed recortada como al tocar el
	 * suelo (BellyLandingKeep y BellyMaxEntrySpeed) y frena con el rozamiento de la arena, su rampa y el freno por velocidad
	 * (TNDiveLogic::IntegrateBellyVelocity) hasta BellyStopSpeed.
	 */
	float BellySlideDistance(const UTN_TurtleMovementComponent& Move, float LandingSpeed, float Gravity)
	{
		constexpr float Step = 1.f / 60.f;
		FVector Velocity(FMath::Min(LandingSpeed * Move.BellyLandingKeep, Move.BellyMaxEntrySpeed), 0.0, 0.0);
		TNDiveLogic::FBellyStepInput In;
		In.Gravity = Gravity;
		In.Drag = Move.BellyDrag;
		In.Slope.MaxSpeed = Move.BellyMaxSpeed;
		float Distance = 0.f;
		for (float Time = 0.f; Time < Move.BellyMaxSeconds; Time += Step)
		{
			const float Speed = static_cast<float>(Velocity.Size());
			if (Speed < Move.BellyStopSpeed && Time >= Move.BellyMinSeconds)
			{
				break;
			}
			In.Friction = Move.BellyFrictionSand * (1.f + Move.BellyFrictionRamp * FMath::Max(0.f, Time - Move.BellyFrictionRampStart));
			const FVector Next = TNDiveLogic::IntegrateBellyVelocity(Velocity, In, Step);
			Distance += 0.5f * static_cast<float>(Velocity.Size() + Next.Size()) * Step;
			Velocity = Next;
		}
		return Distance;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTurtleStatsTest,
	"Tortunabo.Stats.Turtle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTurtleStatsTest::RunTest(const FString& Parameters)
{
	using namespace TNTurtleStats;
	UClass* TurtleClass = LoadClass<ATortugaCharacter>(nullptr, TNTurtleStatsTest::TurtleBlueprintPath);
	if (!TestNotNull(TEXT("BP_TortugaCharacter carga"), TurtleClass))
	{
		return false;
	}

	// Andar, correr, salto y estamina: en la tortuga que se juega y en los valores de serie de C++.
	for (UClass* Class : { TurtleClass, ATortugaCharacter::StaticClass() })
	{
		const ATortugaCharacter* Turtle = Class->GetDefaultObject<ATortugaCharacter>();
		const UTN_StaminaComponent* Stamina = Turtle->GetStaminaComponent();
		const UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
		const FString Who = Class->GetName();
		if (!TestNotNull(*(Who + TEXT(": estamina")), Stamina) || !TestNotNull(*(Who + TEXT(": movimiento")), Move))
		{
			return false;
		}
		const float Gravity = FMath::Abs(UPhysicsSettings::Get()->DefaultGravityZ) * Move->GravityScale;
		const float JumpZ = Move->JumpZVelocity;
		const float Run = Stamina->ComputeMaxWalkSpeed(true, 1.f);
		const float Drain = TNTurtleStatsTest::ReadFloat(Stamina, TEXT("SprintDrainPerSecond"));

		TestEqual(*(Who + TEXT(": andar a 2 m/s")), Stamina->GetWalkSpeed(), WalkSpeed);
		TestEqual(*(Who + TEXT(": correr a 4 m/s")), Run, RunSpeed);
		TestTrue(*(Who + TEXT(": salto de 1,2 m")), FMath::IsNearlyEqual(JumpApex(JumpZ, Gravity), JumpHeight, 1.f));
		TestTrue(*(Who + TEXT(": 0,99 s en el aire")), FMath::IsNearlyEqual(AirSeconds(JumpZ, Gravity), JumpAirSeconds, 0.01f));
		TestTrue(*(Who + TEXT(": 1,98 m de salto andando")), FMath::IsNearlyEqual(JumpDistance(Stamina->GetWalkSpeed(), JumpZ, Gravity), WalkJumpDistance, 2.f));
		TestTrue(*(Who + TEXT(": 3,96 m de salto corriendo")), FMath::IsNearlyEqual(JumpDistance(Run, JumpZ, Gravity), RunJumpDistance, 2.f));
		TestTrue(*(Who + TEXT(": 10 s de estamina corriendo")), FMath::IsNearlyEqual(SprintDuration(Stamina->GetMaxStamina(), Drain), SprintSeconds, 0.05f));
	}

	// Tamaño y panzazo: solo en la tortuga que se juega (el Blueprint pone su cápsula y su panzazo).
	const ATortugaCharacter* Turtle = TurtleClass->GetDefaultObject<ATortugaCharacter>();
	const UTN_TurtleMovementComponent* Move = Cast<UTN_TurtleMovementComponent>(Turtle->GetCharacterMovement());
	const UTN_StaminaComponent* Stamina = Turtle->GetStaminaComponent();
	if (!TestNotNull(TEXT("Movimiento de la tortuga"), Move) || !TestNotNull(TEXT("Estamina de la tortuga"), Stamina))
	{
		return false;
	}
	TestEqual(TEXT("La tortuga mide 1,4 m (cápsula de pie)"), 2.f * Turtle->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), TurtleHeight);

	// Panzazo desde el vértice: en el aire más el arrastre por la arena. Andando sale +2,2 m; corriendo, +2,6 m, porque el
	// panzazo conserva la inercia del salto (DiveMomentumForwardFactor 1). Diferencia anotada en #856: la hoja da +2 m a los dos.
	const float Gravity = FMath::Abs(UPhysicsSettings::Get()->DefaultGravityZ) * Move->GravityScale;
	const float Base = TNTurtleStatsTest::ReadFloat(Turtle, TEXT("DiveForwardSpeed"));
	const float Momentum = TNTurtleStatsTest::ReadFloat(Turtle, TEXT("DiveMomentumForwardFactor"));
	const float Down = TNTurtleStatsTest::ReadFloat(Turtle, TEXT("DiveDownwardSpeed"));
	const float Speeds[2] = { Stamina->GetWalkSpeed(), Stamina->ComputeMaxWalkSpeed(true, 1.f) };
	const float MaxGain[2] = { BellyFlopExtraDistance + 50.f, BellyFlopExtraDistance + 75.f };
	for (int32 i = 0; i < 2; ++i)
	{
		const float DiveSpeed = Base + Speeds[i] * Momentum;
		const float AirGain = BellyFlopAirGain(Speeds[i], DiveSpeed, Down, Move->JumpZVelocity, Gravity);
		const float Slide = TNTurtleStatsTest::BellySlideDistance(*Move, DiveSpeed, Gravity);
		const float Gain = AirGain + Slide;
		AddInfo(FString::Printf(TEXT("Panzazo a %.0f cm/s: +%.0f cm en el aire y %.0f cm arrastrándose (+%.0f cm)"), Speeds[i], AirGain, Slide, Gain));
		TestTrue(*FString::Printf(TEXT("El panzazo a %.0f cm/s alarga al menos ~2 m"), Speeds[i]), Gain >= BellyFlopExtraDistance - 25.f);
		TestTrue(*FString::Printf(TEXT("El panzazo a %.0f cm/s no se pasa"), Speeds[i]), Gain <= MaxGain[i]);
	}

	// Caso negativo: con el gasto de antes (15/s) la carrera duraba 13,3 s, fuera de la hoja.
	TestFalse(TEXT("Con 15/s no son 10 s"), FMath::IsNearlyEqual(SprintDuration(200.f, 15.f), SprintSeconds, 0.05f));
	return true;
}

#endif
