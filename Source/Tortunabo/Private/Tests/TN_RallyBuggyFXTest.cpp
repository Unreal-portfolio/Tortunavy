// Efectos del buggy del Rally (#301): reglas del polvo, las marcas, el aterrizaje y el vadeo (TNBuggyFX) y las partículas
// de cada impacto (TNRallyParticles). Sin mundo. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.BuggyFX; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Vehicles/TN_BuggyDustComponent.h"
#include "Vehicles/TN_RallyFXParticles.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyBuggyFXWheelsTest,
	"Tortunabo.Rally.BuggyFX.Wheels",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyBuggyFXWheelsTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggyFX;
	TestEqual(TEXT("parado no hay polvo"), DustRate(0.f, 0.f), 0.f);
	TestEqual(TEXT("casi parado tampoco, aunque derrape"), DustRate(MinDustSpeedCms - 1.f, 1.f), 0.f);
	TestTrue(TEXT("más deprisa, más polvo"), DustRate(1200.f, 0.f) > DustRate(500.f, 0.f));
	TestEqual(TEXT("rodando a tope"), DustRate(FullDustSpeedCms * 2.f, 0.f), RollDustRate, 1e-3f);
	TestTrue(TEXT("derrapando, más denso"), DustRate(800.f, 1.f) > DustRate(800.f, 0.f) * 2.f);
	TestEqual(TEXT("marcha atrás también levanta"), DustRate(-1000.f, 0.f), DustRate(1000.f, 0.f), 1e-3f);

	TestFalse(TEXT("sin derrape no marca"), LeavesMark(0.f, true));
	TestTrue(TEXT("derrape moderado: marcan las traseras"), LeavesMark(0.4f, true));
	TestFalse(TEXT("...pero no las delanteras"), LeavesMark(0.4f, false));
	TestTrue(TEXT("derrape fuerte: marcan todas"), LeavesMark(0.8f, false));

	TestEqual(TEXT("un bache no suena"), LandingVolume(MinLandingFallCms - 10.f), 0.f);
	TestTrue(TEXT("un salto mediano suena a medias"), LandingVolume(800.f) > 0.f && LandingVolume(800.f) < 1.f);
	TestEqual(TEXT("un salto grande suena entero"), LandingVolume(FullLandingFallCms + 500.f), 1.f);

	TestTrue(TEXT("rueda bajo el nivel del agua: vadea"), IsWading(-5.f, true, 0.0));
	TestFalse(TEXT("por encima, no"), IsWading(5.f, true, 0.0));
	TestFalse(TEXT("pista sin agua, nunca"), IsWading(-500.f, false, 0.0));
	TestEqual(TEXT("vadeando parado no salpica"), SplashRateAt(0.f), 0.f);
	TestEqual(TEXT("vadeando deprisa salpica a tope"), SplashRateAt(FullSplashSpeedCms), SplashRate, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyBuggyFXImpactsTest,
	"Tortunabo.Rally.BuggyFX.Impacts",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyBuggyFXImpactsTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyParticles;
	const ETNRallyBurstKind Impacts[] = { ETNRallyBurstKind::CocoHit, ETNRallyBurstKind::Explosion, ETNRallyBurstKind::Ink,
		ETNRallyBurstKind::BubblePop, ETNRallyBurstKind::Sand, ETNRallyBurstKind::Sparks };
	TSet<uint32> FirstColors;
	for (const ETNRallyBurstKind Kind : Impacts)
	{
		const TArray<FBurstLayer> Layers = LayersFor(Kind, 100.f);
		TestTrue(TEXT("cada impacto tiene sus partículas"), Layers.Num() > 0 && Layers[0].Count > 0);
		TestFalse(TEXT("las partículas sustituyen a la esfera"), KeepsSphere(Kind));
		for (const FBurstLayer& Layer : Layers)
		{
			TestTrue(TEXT("caben en su emisor"), Layer.Count <= Layer.Desc.MaxParticles);
			TestEqual(TEXT("ráfaga, sin goteo continuo"), Layer.Desc.Rate, 0.f);
		}
		if (Layers.Num() > 0)
		{
			FirstColors.Add(Layers[0].Desc.Color.ToFColor(false).DWColor());
		}
	}
	TestTrue(TEXT("coco, explosión, tinta, burbuja y chispas se distinguen por el color"), FirstColors.Num() >= 5);
	TestTrue(TEXT("el escudo conserva la burbuja"), KeepsSphere(ETNRallyBurstKind::Shield));
	TestTrue(TEXT("el fogonazo conserva el destello"), KeepsSphere(ETNRallyBurstKind::MuzzleFlash));

	const TArray<FBurstLayer> Small = LayersFor(ETNRallyBurstKind::Explosion, 100.f);
	const TArray<FBurstLayer> Big = LayersFor(ETNRallyBurstKind::Explosion, 400.f);
	float SmallMax = 0.f;
	float BigMax = 0.f;
	for (const FBurstLayer& Layer : Small) { SmallMax = FMath::Max(SmallMax, Layer.Desc.SizeEnd); }
	for (const FBurstLayer& Layer : Big) { BigMax = FMath::Max(BigMax, Layer.Desc.SizeEnd); }
	TestTrue(TEXT("una explosión más ancha levanta una nube más grande"), BigMax > SmallMax);
	return true;
}

#endif
