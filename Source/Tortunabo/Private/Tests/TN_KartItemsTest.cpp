// Objetos de los karts (#304): reparto por puesto, ruleta, decisiones de los bots y guiado de la concha. Lógica pura. La
// artillera está en TN_KartGunnerTest.cpp y la munición de las cajas «?» del Rally en TN_RallyItemBoxTest.cpp. Correr desde Session Frontend (categoría
// "Tortunabo.Kart") o headless con UnrealEditor-Win64-DebugGame-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Kart; Quit".

#include "Misc/AutomationTest.h"
#include "Kart/TN_KartItems.h"

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

#endif
