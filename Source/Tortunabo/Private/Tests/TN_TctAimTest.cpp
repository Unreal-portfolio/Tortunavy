// Mira de las armas de Todos contra Todos (#707): qué objetos la llevan y hacia dónde sale el disparo (de la boca al punto de mira).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Tct.Aim; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Game/TN_TctItemRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctAimRulesTest,
	"Tortunabo.Tct.Aim.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctAimRulesTest::RunTest(const FString& Parameters)
{
	using namespace TNTctItemRules;

	// Qué objetos llevan mira: los que se disparan, lanzan o golpean hacia donde se mira.
	for (const ETNTctItem Kind : { ETNTctItem::KnockoutPistol, ETNTctItem::AirBlunderbuss, ETNTctItem::Grapple, ETNTctItem::Shovel,
		ETNTctItem::BeachBall, ETNTctItem::Anchor, ETNTctItem::JellyDart, ETNTctItem::InkPistol, ETNTctItem::Cocobomba, ETNTctItem::Alga })
	{
		TestTrue(FString::Printf(TEXT("%s lleva mira"), Spec(Kind).Code), UsesAim(Kind));
	}
	for (const ETNTctItem Kind : { ETNTctItem::None, ETNTctItem::Flotador, ETNTctItem::MedusaTrampolin, ETNTctItem::GaviotaLadrona })
	{
		TestFalse(FString::Printf(TEXT("%s no lleva mira"), Spec(Kind).Code), UsesAim(Kind));
	}

	// El disparo sale de la boca hacia el punto de mira, no en paralelo a la cámara.
	const FVector Muzzle(0.0, 0.0, 50.0);
	const FVector Camera(1.0, 0.0, 0.0);
	FVector Dir = AimToward(Muzzle, FVector(1000.0, 200.0, 50.0), Camera);
	TestTrue(TEXT("Hacia la mira: el tiro llega al punto"), (Muzzle + Dir * FVector::Dist(Muzzle, FVector(1000.0, 200.0, 50.0)) - FVector(1000.0, 200.0, 50.0)).Size() < 5.0);
	Dir = AimToward(Muzzle, FVector(1000.0, 0.0, 450.0), Camera);
	TestTrue(TEXT("Mirando arriba, el tiro sube"), Dir.Z > 0.3);
	TestTrue(TEXT("La inclinación se recorta: nunca al cielo ni al suelo"), AimToward(Muzzle, FVector(150.0, 0.0, 5000.0), Camera).Z <= FMath::Sin(FMath::DegreesToRadians(40.f)) + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("Con la mira pegada a la boca, la dirección de la cámara"), AimToward(Muzzle, FVector(60.0, 0.0, 50.0), Camera).Equals(Camera, 0.01));
	TestTrue(TEXT("Con la mira detrás, la de la cámara"), AimToward(Muzzle, FVector(-800.0, 0.0, 50.0), Camera).Equals(Camera, 0.01));
	TestTrue(TEXT("Con la mira de lado, la de la cámara"), AimToward(Muzzle, FVector(50.0, 800.0, 50.0), Camera).Equals(Camera, 0.01));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
