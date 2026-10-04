// Límites de movimiento con nombre (#70): el tope que manda es el menor y cada sistema quita solo el suyo; el salto y la
// gravedad vuelven a su base al quitar el último límite, en cualquier orden.
// Se testean las funciones de TN_MovementLimits.h que usa UTN_StaminaComponent.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Movement.Limits; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_MovementLimits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMovementLimitsSpeedTest,
	"Tortunabo.Movement.Limits.SpeedCap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMovementLimitsSpeedTest::RunTest(const FString& Parameters)
{
	using namespace TNMovementLimits;

	TMap<FName, float> Caps;
	TestEqual(TEXT("Sin topes: sin límite"), ResolveSpeedCap(Caps), NoCap);

	Caps.Add(TEXT("Carry"), 330.f);
	Caps.Add(TEXT("Mareo"), 250.f);
	TestEqual(TEXT("Mareado y llevando a otra: manda el menor"), ResolveSpeedCap(Caps), 250.f);

	Caps.Remove(TEXT("Mareo"));
	TestEqual(TEXT("Acaba el mareo mientras lleva a otra: sigue el tope de llevar, no la velocidad completa"), ResolveSpeedCap(Caps), 330.f);

	Caps.Add(TEXT("Shell"), 0.f);
	TestEqual(TEXT("En el caparazón: quieta"), ResolveSpeedCap(Caps), 0.f);
	Caps.Remove(TEXT("Shell"));
	Caps.Remove(TEXT("Carry"));
	TestEqual(TEXT("Sin nada: sin límite"), ResolveSpeedCap(Caps), NoCap);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMovementLimitsJumpTest,
	"Tortunabo.Movement.Limits.Jump",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMovementLimitsJumpTest::RunTest(const FString& Parameters)
{
	using namespace TNMovementLimits;
	constexpr float Base = 600.f;

	FJumpLimit Syrup;
	Syrup.Cap = 150.f;

	// Dos zonas lentas solapadas: se sale primero de la que se entró primero y al revés.
	for (int32 Order = 0; Order < 2; ++Order)
	{
		TMap<FName, FJumpLimit> Limits;
		Limits.Add(TEXT("SlowZoneA"), Syrup);
		Limits.Add(TEXT("SlowZoneB"), Syrup);
		TestEqual(TEXT("En las dos zonas: el salto del sirope"), ResolveJumpZ(Base, Limits), 150.f);
		Limits.Remove(Order == 0 ? FName(TEXT("SlowZoneA")) : FName(TEXT("SlowZoneB")));
		TestEqual(TEXT("Aún en una: sigue el sirope"), ResolveJumpZ(Base, Limits), 150.f);
		Limits.Remove(Order == 0 ? FName(TEXT("SlowZoneB")) : FName(TEXT("SlowZoneA")));
		TestEqual(TEXT("Fuera de las dos, en cualquier orden: el salto de base"), ResolveJumpZ(Base, Limits), Base);
	}

	FJumpLimit Water;
	Water.Multiplier = 0.8f;
	FJumpLimit Held;
	Held.Cap = 0.f;
	TMap<FName, FJumpLimit> Limits;
	Limits.Add(TEXT("Wading"), Water);
	TestEqual(TEXT("En el agua: el multiplicador"), ResolveJumpZ(Base, Limits), 480.f, 0.01f);
	Limits.Add(TEXT("SlowZoneA"), Syrup);
	TestEqual(TEXT("En el agua y en el sirope: el menor"), ResolveJumpZ(Base, Limits), 150.f);
	Limits.Add(TEXT("Seaweed"), Held);
	TestEqual(TEXT("Enganchada en un alga: no salta"), ResolveJumpZ(Base, Limits), 0.f);
	Limits.Remove(TEXT("SlowZoneA"));
	Limits.Remove(TEXT("Seaweed"));
	TestEqual(TEXT("Solo el agua otra vez"), ResolveJumpZ(Base, Limits), 480.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMovementLimitsGravityTest,
	"Tortunabo.Movement.Limits.Gravity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMovementLimitsGravityTest::RunTest(const FString& Parameters)
{
	using namespace TNMovementLimits;

	TMap<FName, float> Overrides;
	TestEqual(TEXT("Sin sirope: la base"), ResolveGravityScale(1.f, Overrides), 1.f);
	Overrides.Add(TEXT("SlowZoneA"), 0.35f);
	Overrides.Add(TEXT("SlowZoneB"), 0.5f);
	TestEqual(TEXT("Dos siropes: el más espeso"), ResolveGravityScale(1.f, Overrides), 0.35f);
	Overrides.Remove(TEXT("SlowZoneA"));
	TestEqual(TEXT("Queda uno"), ResolveGravityScale(1.f, Overrides), 0.5f);
	Overrides.Remove(TEXT("SlowZoneB"));
	TestEqual(TEXT("Fuera: la base"), ResolveGravityScale(1.f, Overrides), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMovementLimitsWalkSpeedTest,
	"Tortunabo.Movement.Limits.WalkSpeed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMovementLimitsWalkSpeedTest::RunTest(const FString& Parameters)
{
	using namespace TNMovementLimits;

	FWalkSpeedInputs In;
	In.WalkSpeed = 450.f;
	In.SprintSpeed = 800.f;
	TestEqual(TEXT("Andando"), ResolveWalkSpeed(In), 450.f);
	In.bSprinting = true;
	TestEqual(TEXT("Corriendo"), ResolveWalkSpeed(In), 800.f);
	In.EnvironmentMultiplier = 0.5f;
	TestEqual(TEXT("Corriendo en el agua"), ResolveWalkSpeed(In), 400.f, 0.01f);
	In.PostBoostMultiplier = 0.75f;
	TestEqual(TEXT("Con la penalización tras el boost"), ResolveWalkSpeed(In), 300.f, 0.01f);
	In.bSprinting = false;
	In.RaceMultiplier = 1.5f;
	TestEqual(TEXT("Turbo: al menos la de correr, por el turbo"), ResolveWalkSpeed(In), 1200.f, 0.01f);
	In.Cap = 0.f;
	TestEqual(TEXT("En el caparazón: quieta aunque lleve turbo"), ResolveWalkSpeed(In), 0.f);
	return true;
}

#endif
