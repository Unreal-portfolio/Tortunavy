// Objetos de los karts (#304): reparto por puesto, ruleta, decisiones de los bots y guiado de la concha (lógica pura), y el
// corte de la ráfaga de erizos con el kart bloqueado (mundo con física sin ventana, TN_RallyPhysicsTestKit.h). La
// artillera está en TN_KartGunnerTest.cpp y la munición de las cajas «?» del Rally en TN_RallyItemBoxTest.cpp. Correr desde Session Frontend (categoría
// "Tortunabo.Kart") o headless con UnrealEditor-Win64-DebugGame-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Kart; Quit".

#include "Misc/AutomationTest.h"
#include "TN_RallyPhysicsTestKit.h"
#include "EngineUtils.h"
#include "Kart/TN_KartItemComponent.h"
#include "Kart/TN_KartItems.h"
#include "Vehicles/TN_RallyProjectile.h"
#include "Vehicles/TN_RallyTurretLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartItemWeightsTest, "Tortunabo.Kart.Items.WeightsByPlace",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartItemWeightsTest::RunTest(const FString& Parameters)
{
	using namespace TNKart;
	auto Weight = [](const FItemWeights& W, ETNKartItem Item) { return W.Weights[static_cast<int32>(Item)]; };
	for (int32 Karts = 1; Karts <= 8; ++Karts)
	{
		for (int32 Place = 1; Place <= Karts; ++Place)
		{
			const FItemWeights W = ItemWeightsForPlace(Place, Karts);
			TestTrue(FString::Printf(TEXT("Siempre hay algo que sacar (%d.º de %d)"), Place, Karts), W.Total() > 0.f);
			TestEqual(TEXT("Nunca sale «nada»"), Weight(W, ETNKartItem::None), 0.f);
		}
	}
	const FItemWeights Lead = ItemWeightsForPlace(1, 8);
	const FItemWeights Last = ItemWeightsForPlace(8, 8);
	TestEqual(TEXT("La primera no saca tinta (no tiene a nadie delante)"), Weight(Lead, ETNKartItem::Tinta), 0.f);
	TestEqual(TEXT("La primera no saca estrella"), Weight(Lead, ETNKartItem::Estrella), 0.f);
	TestTrue(TEXT("La última saca estrella"), Weight(Last, ETNKartItem::Estrella) > 0.f);
	TestTrue(TEXT("Delante, más alga que detrás"), Weight(Lead, ETNKartItem::Alga) > Weight(Last, ETNKartItem::Alga));
	TestTrue(TEXT("Detrás, más teledirigidas que delante"), Weight(Last, ETNKartItem::ConchaGuiada) > Weight(Lead, ETNKartItem::ConchaGuiada));
	TestTrue(TEXT("Detrás, triple coco; delante, no"), Weight(Last, ETNKartItem::TripleCoco) > 0.f && Weight(Lead, ETNKartItem::TripleCoco) == 0.f);
	// Los objetos buenos (estrella, teledirigida, triple coco) pesan más cuanto más atrás.
	auto Good = [&Weight](const FItemWeights& W)
	{
		return (Weight(W, ETNKartItem::Estrella) + Weight(W, ETNKartItem::ConchaGuiada) + Weight(W, ETNKartItem::TripleCoco)) / W.Total();
	};
	for (int32 Place = 2; Place <= 8; ++Place)
	{
		TestTrue(FString::Printf(TEXT("El %d.º saca objetos buenos con más frecuencia que el %d.º"), Place, Place - 1),
			Good(ItemWeightsForPlace(Place, 8)) >= Good(ItemWeightsForPlace(Place - 1, 8)));
	}
	TestEqual(TEXT("Sola en la carrera, como la primera"), Weight(ItemWeightsForPlace(1, 1), ETNKartItem::Tinta), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartItemPickTest, "Tortunabo.Kart.Items.PickAndCharges",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartItemPickTest::RunTest(const FString& Parameters)
{
	using namespace TNKart;
	FItemWeights Only;
	Only.Weights[static_cast<int32>(ETNKartItem::Alga)] = 1.f;
	TestEqual(TEXT("Un solo objeto posible: sale siempre"), PickItem(Only, 0.f), ETNKartItem::Alga);
	TestEqual(TEXT("También con la tirada más alta"), PickItem(Only, 1.f), ETNKartItem::Alga);
	TestEqual(TEXT("Sin pesos: nada"), PickItem(FItemWeights(), 0.5f), ETNKartItem::None);

	FItemWeights Two;
	Two.Weights[static_cast<int32>(ETNKartItem::Coco)] = 1.f;
	Two.Weights[static_cast<int32>(ETNKartItem::Estrella)] = 3.f;
	TestEqual(TEXT("Tirada baja: el primero"), PickItem(Two, 0.1f), ETNKartItem::Coco);
	TestEqual(TEXT("Tirada alta: el segundo"), PickItem(Two, 0.6f), ETNKartItem::Estrella);
	// La proporción de las tiradas sigue a los pesos (1 de cada 4 cocos).
	int32 Cocos = 0;
	for (int32 Step = 0; Step < 400; ++Step)
	{
		Cocos += PickItem(Two, (Step + 0.5f) / 400.f) == ETNKartItem::Coco ? 1 : 0;
	}
	TestEqual(TEXT("Un cuarto de cocos con pesos 1 y 3"), Cocos, 100);

	TestEqual(TEXT("Triple coco: tres usos"), ItemCharges(ETNKartItem::TripleCoco), 3);
	TestEqual(TEXT("Concha: un uso"), ItemCharges(ETNKartItem::Concha), 1);
	TestEqual(TEXT("Nada: ningún uso"), ItemCharges(ETNKartItem::None), 0);
	for (int32 Index = 1; Index < ItemKinds; ++Index)
	{
		TestFalse(TEXT("Cada objeto tiene nombre"), ItemName(static_cast<ETNKartItem>(Index)).IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartItemUseTest, "Tortunabo.Kart.Items.BotsAndShell",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartItemUseTest::RunTest(const FString& Parameters)
{
	using namespace TNKart;
	TestEqual(TEXT("La teledirigida va a por la de delante"), HomingTargetPlace(3), 2);
	TestEqual(TEXT("La primera no tiene a quién perseguir"), HomingTargetPlace(1), INDEX_NONE);

	TestTrue(TEXT("Concha con alguien a 30 m delante: se usa"), ShouldBotUseItem(ETNKartItem::Concha, 0.5f, 3000.f, -1.f));
	TestFalse(TEXT("Concha sin nadie cerca: se espera"), ShouldBotUseItem(ETNKartItem::Concha, 0.5f, 20000.f, -1.f));
	TestTrue(TEXT("Alga con alguien detrás: se suelta"), ShouldBotUseItem(ETNKartItem::Alga, 0.5f, -1.f, 2000.f));
	TestFalse(TEXT("Alga sin nadie detrás: se espera"), ShouldBotUseItem(ETNKartItem::Alga, 0.5f, 3000.f, -1.f));
	TestFalse(TEXT("La tinta no se usa en cabeza"), ShouldBotUseItem(ETNKartItem::Tinta, 1.f, -1.f, 500.f));
	TestTrue(TEXT("Coco al rato"), ShouldBotUseItem(ETNKartItem::Coco, 2.f, -1.f, -1.f));
	TestTrue(TEXT("Pasados 8 s, lo usa igual"), ShouldBotUseItem(ETNKartItem::Alga, 8.5f, -1.f, -1.f));
	TestFalse(TEXT("Sin objeto, nada"), ShouldBotUseItem(ETNKartItem::None, 20.f, 100.f, 100.f));

	// Guiado: gira como mucho lo pedido y acaba apuntando al blanco.
	const FVector Turned = SteerShell(FVector::ForwardVector, FVector(0.0, 1.0, 0.0), 30.f);
	TestEqual(TEXT("Gira 30° hacia el blanco"), FMath::RadiansToDegrees(FMath::Atan2(Turned.Y, Turned.X)), 30.0, 0.01);
	FVector Heading = FVector::ForwardVector;
	for (int32 Step = 0; Step < 10; ++Step)
	{
		Heading = SteerShell(Heading, FVector(-1.0, 1.0, 0.0), 30.f);
	}
	TestTrue(TEXT("Al final mira al blanco"), FVector::DotProduct(Heading, FVector(-1.0, 1.0, 0.0).GetSafeNormal()) > 0.999);
	TestEqual(TEXT("Siempre en el plano y unitaria"), SteerShell(FVector(1.0, 0.0, 0.5), FVector(0.0, 0.0, 1.0), 10.f).Size(), 1.0, 0.001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartItemTurretAmmoTest, "Tortunabo.Kart.Items.TurretAmmo",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartItemTurretAmmoTest::RunTest(const FString& Parameters)
{
	// #774: mortero, ráfaga de erizos, medusa, pez globo y arpón en las cajas de Karts.
	using namespace TNKart;
	auto Weight = [](const FItemWeights& W, ETNKartItem Item) { return W.Weights[static_cast<int32>(Item)]; };
	const ETNKartItem NewItems[] = { ETNKartItem::Mortero, ETNKartItem::Erizos, ETNKartItem::Medusa, ETNKartItem::PezGlobo, ETNKartItem::Arpon };
	for (const ETNKartItem Item : NewItems)
	{
		const FString Name = UEnum::GetValueAsString(Item);
		TestTrue(*(Name + TEXT(": antes de Count")), static_cast<int32>(Item) < ItemKinds);
		TestEqual(*(Name + TEXT(": un uso")), ItemCharges(Item), 1);
		TestFalse(*(Name + TEXT(": con nombre")), ItemName(Item).IsEmpty());
		TestTrue(*(Name + TEXT(": sale en alguna caja")), Weight(ItemWeightsForPlace(4, 8), Item) > 0.f);
	}
	const FItemWeights Lead = ItemWeightsForPlace(1, 8);
	const FItemWeights Last = ItemWeightsForPlace(8, 8);
	TestTrue(TEXT("el mortero, más para los de atrás"), Weight(Last, ETNKartItem::Mortero) > Weight(Lead, ETNKartItem::Mortero));
	TestTrue(TEXT("el arpón, más para los de atrás"), Weight(Last, ETNKartItem::Arpon) > Weight(Lead, ETNKartItem::Arpon));
	TestTrue(TEXT("la medusa, más para los de atrás"), Weight(Last, ETNKartItem::Medusa) > Weight(Lead, ETNKartItem::Medusa));
	TestTrue(TEXT("el pez globo, más para los de delante"), Weight(Lead, ETNKartItem::PezGlobo) > Weight(Last, ETNKartItem::PezGlobo));
	TestTrue(TEXT("los erizos, más para los de delante"), Weight(Lead, ETNKartItem::Erizos) > Weight(Last, ETNKartItem::Erizos));
	TestEqual(TEXT("la primera no saca arpón"), Weight(Lead, ETNKartItem::Arpon), 0.f);
	TestEqual(TEXT("ni sola en la carrera"), Weight(ItemWeightsForPlace(1, 1), ETNKartItem::Arpon), 0.f);

	// Bots.
	TestTrue(TEXT("mortero con alguien delante"), ShouldBotUseItem(ETNKartItem::Mortero, 0.5f, 9000.f, -1.f));
	TestFalse(TEXT("mortero en cabeza, se espera"), ShouldBotUseItem(ETNKartItem::Mortero, 0.5f, -1.f, 2000.f));
	TestTrue(TEXT("erizos con alguien delante a 30 m"), ShouldBotUseItem(ETNKartItem::Erizos, 0.5f, 3000.f, -1.f));
	TestFalse(TEXT("erizos con el de delante lejos"), ShouldBotUseItem(ETNKartItem::Erizos, 0.5f, 9000.f, -1.f));
	TestTrue(TEXT("arpón con el de delante a 30 m"), ShouldBotUseItem(ETNKartItem::Arpon, 0.5f, 3000.f, -1.f));
	TestFalse(TEXT("arpón pegado al de delante"), ShouldBotUseItem(ETNKartItem::Arpon, 0.5f, 800.f, -1.f));
	TestTrue(TEXT("pez globo con alguien detrás a 20 m"), ShouldBotUseItem(ETNKartItem::PezGlobo, 0.5f, -1.f, 2000.f));
	TestFalse(TEXT("pez globo sin nadie detrás"), ShouldBotUseItem(ETNKartItem::PezGlobo, 0.5f, 3000.f, -1.f));
	TestTrue(TEXT("medusa con una teledirigida detrás"), ShouldBotUseItem(ETNKartItem::Medusa, 0.5f, 3000.f, 3000.f, true));
	TestFalse(TEXT("medusa sin peligro, se espera"), ShouldBotUseItem(ETNKartItem::Medusa, 0.5f, 3000.f, 3000.f, false));
	TestTrue(TEXT("medusa al rato"), ShouldBotUseItem(ETNKartItem::Medusa, 4.5f, 3000.f, 3000.f, false));

	// Mortero: la parábola pasa por el blanco al acabar el vuelo y va por encima de los karts.
	const FVector Start(0.0, 0.0, 90.0);
	const FVector Target(5000.0, 800.0, -200.0);
	constexpr float GravityZ = -980.f;
	const float Flight = MortarFlightSeconds(static_cast<float>(FVector::Dist2D(Start, Target)));
	TestTrue(TEXT("vuelo entre el mínimo y el máximo"), Flight >= MortarMinFlightSeconds && Flight <= MortarMaxFlightSeconds);
	TestEqual(TEXT("muy cerca, el vuelo mínimo"), MortarFlightSeconds(100.f), MortarMinFlightSeconds);
	const FVector Launch = MortarLaunchVelocity(Start, Target, GravityZ, Flight);
	const FVector Landing = Start + Launch * Flight + FVector(0.0, 0.0, 0.5 * GravityZ * Flight * Flight);
	TestTrue(TEXT("cae en el blanco"), Landing.Equals(Target, 1.0));
	const double Apex = Start.Z + FMath::Square(Launch.Z) / (2.0 * -GravityZ);
	TestTrue(FString::Printf(TEXT("y sube por encima de los karts (%.0f cm)"), Apex), Apex > 300.0);
	TestEqual(TEXT("ráfaga de erizos de 3 s"), static_cast<float>(ErizosSpikes) * TNRallyTurret::ErizosSpikeInterval, ErizosSeconds, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartItemErizosLockTest, "Tortunabo.Kart.Items.ErizosStopWhenLocked",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartItemErizosLockTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyPhysicsMeasure;
	const auto CountSpikes = [](UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ATN_RallyProjectile> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	};
	// La ráfaga de 3 s se corta al bloquear el motor (meta) o las armas, como los demás objetos.
	for (const bool bWeapons : { false, true })
	{
		FPhysicsWorld Test(bWeapons ? TEXT("TNKartErizosWeaponsWorld") : TEXT("TNKartErizosEngineWorld"));
		if (!Test.World || !SpawnFlatGround(*Test.World))
		{
			AddError(TEXT("No se ha podido montar el suelo"));
			return false;
		}
		ATN_Buggy* Kart = SpawnBuggy(*Test.World, FTransform(FVector(0.0, 0.0, SpawnLiftCm)));
		if (!TestNotNull(TEXT("kart"), Kart))
		{
			return false;
		}
		UTN_KartItemComponent* Items = NewObject<UTN_KartItemComponent>(Kart, TEXT("KartItems"));
		Items->RegisterComponent();
		Settle(Test, *Kart);
		Items->GiveItem(ETNKartItem::Erizos, true);
		if (!TestTrue(TEXT("los erizos se usan"), Items->UseItem(false)))
		{
			return false;
		}
		Test.Advance(0.3f);
		TestTrue(TEXT("la ráfaga saca púas"), CountSpikes(Test.World) > 0);
		if (bWeapons)
		{
			Kart->SetWeaponsLocked(true);
		}
		else
		{
			Kart->SetEngineLocked(true);
		}
		// Las púas viven 1,5 s: pasado eso, sin ráfaga no queda ninguna (sin el corte seguirían saliendo hasta los 3 s).
		Test.Advance(TNRallyTurret::ErizosLifeSeconds + 0.2f);
		TestEqual(bWeapons ? TEXT("con las armas bloqueadas no salen más púas") : TEXT("con el motor bloqueado no salen más púas"),
			CountSpikes(Test.World), 0);
	}
	return true;
}

#endif
