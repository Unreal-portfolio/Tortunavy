// Objetos de Todos contra Todos (#651): catálogo y cargas, reloj de los puntos de objetos, reparto de los puntos, cuánto
// empuja cada golpe (TN_TctItemRules.h) y un punto de objetos de verdad en un mundo de juego sin ventana (ATN_TctItemPad).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Tct.Items; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/TN_TctItemRules.h"
#include "Game/TN_TctItems.h"
#include "Game/TN_TctRules.h"
#include "World/TN_TctArena.h"
#include "World/TN_TctItemPad.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctItemsCatalogTest,
	"Tortunabo.Tct.Items.Catalog",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctItemsCatalogTest::RunTest(const FString& Parameters)
{
	const TArray<ETNTctItem> Kinds = TNTctItemRules::AllKinds();
	TestEqual(TEXT("Veintiocho objetos (los 18 de #651, #714 y #777 y los 10 de #830)"), Kinds.Num(), 28);
	// Sin malla IA de #600: llevan su malla en ejecución (TNTctItemMeshes).
	const TSet<ETNTctItem> OwnMesh = { ETNTctItem::Cocobomba, ETNTctItem::Alga, ETNTctItem::GaviotaLadrona, ETNTctItem::Flotador,
		ETNTctItem::MedusaTrampolin, ETNTctItem::Cohete, ETNTctItem::BotasMuelle, ETNTctItem::Aletas, ETNTctItem::Cambiazo,
		ETNTctItem::Burbuja, ETNTctItem::Puas, ETNTctItem::Red, ETNTctItem::Remolino, ETNTctItem::TaponMarea, ETNTctItem::Paraguas };

	int32 InPool = 0;
	TSet<FName> Ids;
	for (const ETNTctItem Kind : Kinds)
	{
		const FTNTctItemSpec& Spec = TNTctItemRules::Spec(Kind);
		TestEqual(FString::Printf(TEXT("Ficha de %s en su sitio"), Spec.Code), Spec.Kind, Kind);
		TestFalse(FString::Printf(TEXT("%s tiene nombre"), Spec.Code), TNTctItems::DisplayName(Kind).IsEmpty());
		InPool += Spec.PadWeight > 0.f ? 1 : 0;
		if (Spec.Source != ETNTctItemSource::Code)
		{
			continue;
		}
		// Cargas: «Tct_<Code>_<N>» de ida y vuelta, y una menos en cada uso hasta gastarse.
		FName Id = TNTctItemRules::MakeItemId(Kind, Spec.Charges);
		TestFalse(FString::Printf(TEXT("%s con ItemId único"), Spec.Code), Ids.Contains(Id));
		Ids.Add(Id);
		for (int32 Left = Spec.Charges; Left >= 1; --Left)
		{
			ETNTctItem Parsed = ETNTctItem::None;
			int32 Charges = 0;
			TestTrue(FString::Printf(TEXT("%s se lee"), *Id.ToString()), TNTctItemRules::ParseItemId(Id, Parsed, Charges));
			TestEqual(FString::Printf(TEXT("%s: objeto"), *Id.ToString()), Parsed, Kind);
			TestEqual(FString::Printf(TEXT("%s: cargas"), *Id.ToString()), Charges, Left);
			Id = TNTctItemRules::ItemIdAfterUse(Id);
		}
		TestTrue(FString::Printf(TEXT("%s se gasta con la última carga"), Spec.Code), Id.IsNone());

		FTN_InventoryItem Item;
		if (Kind != ETNTctItem::InkPistol)
		{
			TestTrue(FString::Printf(TEXT("%s se puede dar siempre"), Spec.Code), TNTctItems::MakeItem(Kind, Item));
			TestEqual(FString::Printf(TEXT("%s es TctItem"), Spec.Code), Item.UseType, ETN_ItemUseType::TctItem);
			TestEqual(FString::Printf(TEXT("%s: objeto de la fila"), Spec.Code), TNTctItems::KindOf(Item), Kind);
			TestEqual(FString::Printf(TEXT("%s: cargas de la fila"), Spec.Code), TNTctItems::ChargesOf(Item), Spec.Charges);
			TestNotNull(FString::Printf(TEXT("%s: se puede soltar"), Spec.Code), Item.PickupActorClass.Get());
		}
		TestTrue(FString::Printf(TEXT("%s: malla IA de #600 o malla propia"), Spec.Code),
			!TNTctItems::MeshPath(Kind).IsEmpty() || OwnMesh.Contains(Kind));
	}
	TestTrue(TEXT("Al menos siete objetos de combate en los puntos"), InPool >= 7);

	// Sin vida, veneno ni curas: nada de lo que sale cura, da energía o revive.
	for (const ETNTctItem Kind : TNTctItems::AvailableKinds())
	{
		FTN_InventoryItem Item;
		TestTrue(TEXT("Disponible se puede dar"), TNTctItems::MakeItem(Kind, Item));
		const bool bHealing = Item.UseType == ETN_ItemUseType::SelfStaminaBoost || Item.UseType == ETN_ItemUseType::SelfStaminaFull
			|| Item.UseType == ETN_ItemUseType::Totem;
		TestFalse(FString::Printf(TEXT("%s no cura"), TNTctItemRules::Spec(Kind).Code), bHealing);
	}
	AddInfo(FString::Printf(TEXT("Disponibles ahora: %d de %d (los de DT_Items, si está el catálogo)."), TNTctItems::AvailableKinds().Num(), Kinds.Num()));

	ETNTctItem Parsed = ETNTctItem::None;
	int32 Charges = 0;
	TestFalse(TEXT("Un objeto de la carrera no es de TcT"), TNTctItemRules::ParseItemId(TEXT("Race_Coconut"), Parsed, Charges));
	TestFalse(TEXT("Sin cargas no vale"), TNTctItemRules::ParseItemId(TEXT("Tct_Shovel_0"), Parsed, Charges));
	TestFalse(TEXT("Nombre desconocido"), TNTctItemRules::ParseItemId(TEXT("Tct_Laser_2"), Parsed, Charges));
	TestFalse(TEXT("Sin número"), TNTctItemRules::ParseItemId(TEXT("Tct_Shovel"), Parsed, Charges));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctItemsPadClockTest,
	"Tortunabo.Tct.Items.PadClock",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctItemsPadClockTest::RunTest(const FString& Parameters)
{
	FTNTctPadClock Clock;
	TestFalse(TEXT("Parado entre rondas: nada"), Clock.ShouldSpawn(100.0, false));

	Clock.StartRound(10.0, 0.75f);
	TestFalse(TEXT("Antes de la primera aparición, nada"), Clock.ShouldSpawn(10.5, false));
	TestTrue(TEXT("A su hora, sale"), Clock.ShouldSpawn(10.75, false));
	TestFalse(TEXT("Bajo el agua, no sale"), Clock.ShouldSpawn(11.0, true));
	Clock.MarkSpawned();
	TestFalse(TEXT("Con objeto puesto, no sale otro"), Clock.ShouldSpawn(50.0, false));

	// #778: los objetos reaparecen a los 6 s (antes, 12 s).
	TestEqual(TEXT("Reaparición de serie: 6 s"), TNTctItemTuning::PadRespawnSeconds, 6.f);
	TestEqual(TEXT("El punto de objetos usa la de serie"), GetDefault<ATN_TctItemPad>()->RespawnSeconds, 6.f);
	Clock.MarkTaken(20.0, TNTctItemTuning::PadRespawnSeconds);
	TestFalse(TEXT("Recién cogido, no reaparece"), Clock.ShouldSpawn(20.0, false));
	TestFalse(TEXT("A los 5,9 s, todavía no"), Clock.ShouldSpawn(25.9, false));
	TestTrue(TEXT("A los 6 s, reaparece"), Clock.ShouldSpawn(26.0, false));
	Clock.MarkSpawned();
	TestFalse(TEXT("Nunca más de un objeto por punto"), Clock.ShouldSpawn(40.0, false));

	// Seis puntos y alguien que coge un objeto cada 2 s del primero que lo tenga: media de puntos con objeto en 60 s.
	const auto AverageStocked = [](float Respawn)
	{
		TArray<FTNTctPadClock> Pads;
		Pads.SetNum(6);
		for (FTNTctPadClock& Pad : Pads) { Pad.StartRound(0.0, 0.f); }
		double Stocked = 0.0;
		int32 Samples = 0;
		for (int32 Tick = 0; Tick <= 600; ++Tick)
		{
			const double Now = Tick * 0.1;
			for (FTNTctPadClock& Pad : Pads)
			{
				if (Pad.ShouldSpawn(Now, false)) { Pad.MarkSpawned(); }
			}
			if (Tick % 20 == 10)
			{
				for (FTNTctPadClock& Pad : Pads)
				{
					if (Pad.bHasItem) { Pad.MarkTaken(Now, Respawn); break; }
				}
			}
			for (const FTNTctPadClock& Pad : Pads) { Stocked += Pad.bHasItem ? 1.0 : 0.0; }
			++Samples;
		}
		return Stocked / Samples;
	};
	const double Before = AverageStocked(12.f);
	const double After = AverageStocked(TNTctItemTuning::PadRespawnSeconds);
	AddInfo(FString::Printf(TEXT("Puntos con objeto de media: %.2f con 12 s, %.2f con 6 s"), Before, After));
	TestTrue(TEXT("Con 6 s hay más puntos con objeto a la vez"), After > Before + 0.5);
	TestTrue(TEXT("Nunca más objetos que puntos"), After <= 6.0);

	Clock.MarkSpawned();
	Clock.Stop();
	TestFalse(TEXT("Fin de la ronda: sin objeto"), Clock.bHasItem);
	TestFalse(TEXT("Fin de la ronda: no reaparece"), Clock.ShouldSpawn(1000.0, false));
	Clock.MarkTaken(1000.0, 12.f);
	TestFalse(TEXT("Coger sin objeto no arranca el reloj"), Clock.ShouldSpawn(2000.0, false));

	TestTrue(TEXT("Agua por encima del punto: inundado"), TNTctItemRules::IsPadSubmerged(100.f, 120.f));
	TestTrue(TEXT("Agua a menos del margen: inundado"), TNTctItemRules::IsPadSubmerged(100.f, 70.f));
	TestFalse(TEXT("Agua bien por debajo: seco"), TNTctItemRules::IsPadSubmerged(100.f, 0.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctItemsPadPicksTest,
	"Tortunabo.Tct.Items.PadPicks",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctItemsPadPicksTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Sorteo: primer tramo"), TNTctItemRules::PickWeighted({ 1.f, 1.f, 2.f }, 0.1f), 0);
	TestEqual(TEXT("Sorteo: segundo tramo"), TNTctItemRules::PickWeighted({ 1.f, 1.f, 2.f }, 0.3f), 1);
	TestEqual(TEXT("Sorteo: el pesado"), TNTctItemRules::PickWeighted({ 1.f, 1.f, 2.f }, 0.99f), 2);
	TestEqual(TEXT("Sorteo: peso cero nunca"), TNTctItemRules::PickWeighted({ 0.f, 1.f }, 0.f), 1);
	TestEqual(TEXT("Sorteo: sin pesos"), TNTctItemRules::PickWeighted({ 0.f, 0.f }, 0.5f), static_cast<int32>(INDEX_NONE));

	const TArray<ETNTctItem> Two = { ETNTctItem::Shovel, ETNTctItem::Grapple };
	for (float Roll = 0.f; Roll < 1.f; Roll += 0.05f)
	{
		TestEqual(TEXT("Nunca el mismo dos veces seguidas en un punto"), TNTctItemRules::PickPadItem(Two, ETNTctItem::Shovel, Roll), ETNTctItem::Grapple);
	}
	TestEqual(TEXT("Si solo hay uno, se repite"), TNTctItemRules::PickPadItem({ ETNTctItem::Shovel }, ETNTctItem::Shovel, 0.5f), ETNTctItem::Shovel);
	TestEqual(TEXT("Sin objetos, nada"), TNTctItemRules::PickPadItem({}, ETNTctItem::None, 0.5f), ETNTctItem::None);

	TestEqual(TEXT("Dos jugadoras: 12 puntos"), TNTctItemRules::ActivePadCount(2, 40), 12);
	TestEqual(TEXT("Seis jugadoras: 28 puntos"), TNTctItemRules::ActivePadCount(6, 40), 28);
	TestEqual(TEXT("Ocho jugadoras: los 36"), TNTctItemRules::ActivePadCount(8, 36), 36);
	TestEqual(TEXT("Con pocos puntos, todos"), TNTctItemRules::ActivePadCount(2, 8), 8);
	TestEqual(TEXT("Nunca más de los que hay"), TNTctItemRules::ActivePadCount(8, 3), 3);
	TestTrue(TEXT("Primera aparición escalonada"), TNTctItemRules::PadFirstSpawnDelay(1) > TNTctItemRules::PadFirstSpawnDelay(0));
	TestTrue(TEXT("Todo puesto antes de 2,5 s"), TNTctItemRules::PadFirstSpawnDelay(3) < 2.5f);

	// Rejilla de 21 x 21 cada 5 m centrada en el origen; dos salidas en los extremos del eje X.
	TArray<FVector> Grid;
	for (int32 X = -10; X <= 10; ++X)
	{
		for (int32 Y = -10; Y <= 10; ++Y)
		{
			Grid.Add(FVector(X * 500.0, Y * 500.0, 0.0));
		}
	}
	const TArray<FVector> Spawns = { FVector(5000.0, 0.0, 0.0), FVector(-5000.0, 0.0, 0.0) };
	const TArray<int32> Picks = TNTctItemRules::PickPadPoints(Grid, 8, FVector::ZeroVector, Spawns, 1200.f);
	TestEqual(TEXT("Ocho puntos"), Picks.Num(), 8);
	if (Picks.Num() == 8)
	{
		TestTrue(TEXT("El primero, en el centro"), Grid[Picks[0]].Size2D() < 1.0);
		double Closest = TNumericLimits<double>::Max();
		for (int32 A = 0; A < Picks.Num(); ++A)
		{
			for (const FVector& Spawn : Spawns)
			{
				TestTrue(TEXT("Lejos de las salidas"), FVector::Dist2D(Grid[Picks[A]], Spawn) >= 1200.0);
			}
			for (int32 B = A + 1; B < Picks.Num(); ++B)
			{
				Closest = FMath::Min(Closest, FVector::Dist2D(Grid[Picks[A]], Grid[Picks[B]]));
			}
		}
		TestTrue(TEXT("Repartidos (más de 25 m entre dos)"), Closest > 2500.0);
	}
	TestEqual(TEXT("Con pocos sitios, todos"), TNTctItemRules::PickPadPoints({ FVector::ZeroVector }, 8, FVector::ZeroVector, Spawns, 1200.f).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctItemsEffectsTest,
	"Tortunabo.Tct.Items.Effects",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctItemsEffectsTest::RunTest(const FString& Parameters)
{
	using namespace TNTctItemTuning;
	const FVector Origin = FVector::ZeroVector;
	const FVector Forward = FVector::ForwardVector;

	// Pistola de noqueo: el derribo empuja hacia donde iba el tiro y algo hacia arriba.
	const FVector Knock = TNTctItemRules::KnockoutImpulse(FVector(1.0, 0.0, -0.3));
	TestTrue(TEXT("Noqueo: hacia delante"), Knock.X > 0.0 && FMath::IsNearlyZero(Knock.Y));
	TestTrue(TEXT("Noqueo: hacia arriba aunque se apunte abajo"), Knock.Z > 0.0);

	// Trabuco de aire: cono delante; más fuerte cerca; nada detrás, fuera del ángulo ni lejos.
	FVector Near, Far, Miss;
	TestTrue(TEXT("Trabuco: de frente, cerca"), TNTctItemRules::BlunderbussPush(Origin, Forward, FVector(200.0, 0.0, 0.0), Near));
	TestTrue(TEXT("Trabuco: de frente, lejos"), TNTctItemRules::BlunderbussPush(Origin, Forward, FVector(900.0, 0.0, 0.0), Far));
	TestTrue(TEXT("Trabuco: cerca empuja más"), Near.Size2D() > Far.Size2D());
	TestTrue(TEXT("Trabuco: hacia fuera del cañón"), Near.X > 0.0 && Near.Z > 0.0);
	TestTrue(TEXT("Trabuco: a 30 grados, dentro"), TNTctItemRules::BlunderbussPush(Origin, Forward, FVector(500.0, 280.0, 0.0), Miss));
	TestTrue(TEXT("Trabuco: a 30 grados, empuja hacia ese lado"), Miss.Y > 0.0);
	TestFalse(TEXT("Trabuco: a 45 grados, fuera"), TNTctItemRules::BlunderbussPush(Origin, Forward, FVector(500.0, 500.0, 0.0), Miss));
	TestFalse(TEXT("Trabuco: detrás, nada"), TNTctItemRules::BlunderbussPush(Origin, Forward, FVector(-300.0, 0.0, 0.0), Miss));
	TestFalse(TEXT("Trabuco: fuera de alcance"), TNTctItemRules::BlunderbussPush(Origin, Forward, FVector(BlunderbussRange + 50.0, 0.0, 0.0), Miss));
	const FVector Recoil = TNTctItemRules::BlunderbussRecoil(Forward);
	TestTrue(TEXT("Trabuco: retrocede quien dispara"), Recoil.X < 0.0);

	// Garfio: lleva de From a To, más rápido cuanto más lejos y con tope.
	const FVector PullNear = TNTctItemRules::GrapplePull(FVector(600.0, 0.0, 0.0), Origin);
	const FVector PullFar = TNTctItemRules::GrapplePull(FVector(2500.0, 0.0, 0.0), Origin);
	TestTrue(TEXT("Garfio: hacia quien dispara"), PullNear.X < 0.0 && PullFar.X < 0.0);
	TestTrue(TEXT("Garfio: más lejos, más rápido"), PullFar.Size2D() > PullNear.Size2D());
	TestTrue(TEXT("Garfio: con tope"), PullFar.Size2D() <= GrapplePullMax + 1.0);
	TestTrue(TEXT("Garfio: mínimo para moverla"), PullNear.Size2D() >= GrapplePullMin - 1.0);
	TestTrue(TEXT("Garfio: sube más si el destino está alto"),
		TNTctItemRules::GrapplePull(Origin, FVector(1000.0, 0.0, 400.0)).Z > TNTctItemRules::GrapplePull(Origin, FVector(1000.0, 0.0, 0.0)).Z);

	// Pala: arco delante y al alcance.
	FVector Whack;
	TestTrue(TEXT("Pala: delante, al alcance"), TNTctItemRules::ShovelHit(Origin, Forward, FVector(180.0, 60.0, 0.0), Whack));
	TestTrue(TEXT("Pala: lanza hacia fuera y arriba"), Whack.X > 0.0 && Whack.Z > 0.0);
	TestFalse(TEXT("Pala: detrás, nada"), TNTctItemRules::ShovelHit(Origin, Forward, FVector(-150.0, 0.0, 0.0), Whack));
	TestFalse(TEXT("Pala: lejos, nada"), TNTctItemRules::ShovelHit(Origin, Forward, FVector(ShovelReach + 40.0, 0.0, 0.0), Whack));

	// Balón: empuja en su dirección; parado o casi, no.
	const FVector Ball = TNTctItemRules::BallPush(FVector(0.0, BallSpeed, -200.0));
	TestTrue(TEXT("Balón: empuja hacia donde va"), Ball.Y > 0.0 && Ball.Z > 0.0);
	TestTrue(TEXT("Balón: rodando despacio no empuja"), TNTctItemRules::BallPush(FVector(BallMinPushSpeed - 10.0, 0.0, 0.0)).IsZero());
	TestTrue(TEXT("Balón: más rápido, más fuerte"),
		TNTctItemRules::BallPush(FVector(BallSpeed, 0.0, 0.0)).Size() > TNTctItemRules::BallPush(FVector(BallSpeed * 0.5, 0.0, 0.0)).Size());

	// Ancla: círculo; más fuerte en el centro; fuera, nada.
	FVector Center, Edge;
	TestTrue(TEXT("Ancla: cerca del centro"), TNTctItemRules::AnchorSplash(Origin, FVector(50.0, 0.0, 0.0), Center));
	TestTrue(TEXT("Ancla: en el borde"), TNTctItemRules::AnchorSplash(Origin, FVector(AnchorSplashRadius - 10.0, 0.0, 0.0), Edge));
	TestTrue(TEXT("Ancla: más fuerte en el centro"), Center.Size2D() > Edge.Size2D());
	TestTrue(TEXT("Ancla: hacia fuera"), Center.X > 0.0 && Center.Z > 0.0);
	TestFalse(TEXT("Ancla: fuera del círculo"), TNTctItemRules::AnchorSplash(Origin, FVector(AnchorSplashRadius + 20.0, 0.0, 0.0), Edge));
	TestTrue(TEXT("Ancla: el lastre frena y casi no deja saltar"), AnchorHeavySpeedCap < 400.f && AnchorHeavyJumpMultiplier < 0.6f && AnchorHeavySeconds > 0.f);

	// Dardo de medusa y pistola de tinta: el mareo y la tinta de siempre, con duración y velocidad.
	TestTrue(TEXT("Dardo: marea unos segundos"), DartDizzySeconds >= 2.f);
	TestTrue(TEXT("Dardo: más rápido que el balón"), DartSpeed > BallSpeed);
	TestTrue(TEXT("Tinta: proyectil con velocidad"), InkPistolSpeed > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctItemsPadWorldTest,
	"Tortunabo.Tct.Items.PadWorld",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctItemsPadWorldTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNTctItemsTestWorld"));
	if (!TestNotNull(TEXT("Mundo de prueba"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATN_TctItemPad* Pad = World->SpawnActor<ATN_TctItemPad>(ATN_TctItemPad::StaticClass(), FTransform(FVector(0.0, 0.0, 100.0)), Params);
	if (TestNotNull(TEXT("Punto de objetos"), Pad))
	{
		const float DryWater = -1000.f;
		Pad->ServerStartRound(0.0, 0);
		Pad->ServerUpdate(0.1, DryWater);
		TestNull(TEXT("Al empezar la ronda, todavía nada"), Pad->GetCurrentPickup());
		Pad->ServerUpdate(TNTctItemRules::PadFirstSpawnDelay(0), DryWater);
		ATN_TctItemPickup* First = Pad->GetCurrentPickup();
		if (TestNotNull(TEXT("A su hora, sale un objeto"), First))
		{
			TestNotEqual(TEXT("Un objeto de verdad"), First->GetKind(), ETNTctItem::None);
			TestTrue(TEXT("Sobre el disco"), FVector::Dist2D(First->GetActorLocation(), Pad->GetActorLocation()) < 1.0);
			const ETNTctItem FirstKind = First->GetKind();

			// Lo coge alguien a los 2 s: el pickup avisa y se va.
			Pad->NotifyTaken(First);
			First->Destroy();
			TestNull(TEXT("Cogido: el punto se queda vacío"), Pad->GetCurrentPickup());
			Pad->ServerUpdate(2.0, DryWater);
			// NotifyTaken usa la hora del mundo (0 aquí): reaparece a los RespawnSeconds de ella.
			Pad->ServerUpdate(Pad->RespawnSeconds - 0.5, DryWater);
			TestNull(TEXT("Antes del tiempo de reaparición, nada"), Pad->GetCurrentPickup());
			Pad->ServerUpdate(Pad->RespawnSeconds + 0.01, DryWater);
			ATN_TctItemPickup* Second = Pad->GetCurrentPickup();
			if (TestNotNull(TEXT("Pasado el tiempo, reaparece"), Second))
			{
				TestNotEqual(TEXT("Otro objeto distinto"), Second->GetKind(), FirstKind);
			}
		}

		// El objeto desaparece sin avisar (lo destruye otra cosa): cuenta como cogido y vuelve a salir.
		if (ATN_TctItemPickup* Lost = Pad->GetCurrentPickup())
		{
			Lost->Destroy();
			Pad->ServerUpdate(100.0, DryWater);
			TestNull(TEXT("Perdido: vacío hasta la reaparición"), Pad->GetCurrentPickup());
			Pad->ServerUpdate(100.0 + Pad->RespawnSeconds + 0.01, DryWater);
			TestNotNull(TEXT("Perdido: reaparece"), Pad->GetCurrentPickup());
		}

		// El agua lo cubre: se lleva el objeto y no sale ninguno más en la ronda, aunque luego baje.
		Pad->ServerUpdate(200.0, 150.f);
		TestNull(TEXT("Inundado: sin objeto"), Pad->GetCurrentPickup());
		TestFalse(TEXT("Inundado: reloj parado"), Pad->GetClock().IsRunning());
		Pad->ServerUpdate(300.0, DryWater);
		TestNull(TEXT("Inundado: no vuelve en la ronda"), Pad->GetCurrentPickup());

		// Ronda nueva: vuelve a sacar; fin de ronda: se lo lleva.
		Pad->ServerStartRound(400.0, 0);
		Pad->ServerUpdate(400.0 + TNTctItemRules::PadFirstSpawnDelay(0), DryWater);
		TestNotNull(TEXT("Ronda nueva: vuelve a sacar"), Pad->GetCurrentPickup());
		Pad->ServerStopRound();
		TestNull(TEXT("Fin de ronda: vacío"), Pad->GetCurrentPickup());
		Pad->ServerUpdate(1000.0, DryWater);
		TestNull(TEXT("Entre rondas: nada"), Pad->GetCurrentPickup());
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctItemsDianaPadsTest,
	"Tortunabo.Tct.Items.DianaPads",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctItemsDianaPadsTest::RunTest(const FString& Parameters)
{
	// Los puntos de objetos en la arena de verdad (A01_diana), como los reparte ATN_TctGameMode::CreateItemPads.
	const FName Diana(TEXT("A01_diana"));
	if (!ATN_TctArena::VariantExists(Diana))
	{
		AddWarning(TEXT("Sin Scripts/terrain_volumes/Variants/A01_diana (build cocinada): se salta."));
		return true;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNTctItemsDianaWorld"));
	if (!TestNotNull(TEXT("Mundo de prueba"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATN_TctArena* Arena = World->SpawnActor<ATN_TctArena>(ATN_TctArena::StaticClass(), FTransform::Identity, Params);
	if (TestNotNull(TEXT("Arena"), Arena))
	{
		Arena->Variant = Diana;
		Arena->ServerSetArenaVariant(Diana);
		TestTrue(TEXT("Hay suelo pisable"), Arena->Survey(250.f));
		TArray<FVector> Spawns;
		for (const FTransform& Spawn : Arena->PickSpawnTransforms(TNTctRules::MaxPlayers, 120.f))
		{
			Spawns.Add(Spawn.GetLocation());
		}
		const TArray<FVector>& Candidates = Arena->GetSpawnCandidates();
		const FVector Center = Arena->GetGroundBox().GetCenter();
		const TArray<int32> Pads = TNTctItemRules::PickPadPoints(Candidates, 10, Center, Spawns, 700.f);
		TestEqual(TEXT("Diez puntos de objetos"), Pads.Num(), 10);
		if (Pads.Num() > 0)
		{
			AddInfo(FString::Printf(TEXT("Primer punto a %.0f uu del centro"), FVector::Dist2D(Candidates[Pads[0]], Center)));
			TestTrue(TEXT("El primero, en el centro (menos de 5 m)"), FVector::Dist2D(Candidates[Pads[0]], Center) < 500.0);
		}
		int32 NearSpawn = 0;
		double Closest = TNumericLimits<double>::Max();
		float Highest = -TNumericLimits<float>::Max();
		float Lowest = TNumericLimits<float>::Max();
		for (int32 A = 0; A < Pads.Num(); ++A)
		{
			const FVector& Pad = Candidates[Pads[A]];
			Highest = FMath::Max(Highest, static_cast<float>(Pad.Z));
			Lowest = FMath::Min(Lowest, static_cast<float>(Pad.Z));
			for (const FVector& Spawn : Spawns)
			{
				NearSpawn += FVector::Dist2D(Pad, Spawn) < 700.0 ? 1 : 0;
			}
			for (int32 B = A + 1; B < Pads.Num(); ++B)
			{
				Closest = FMath::Min(Closest, FVector::Dist2D(Pad, Candidates[Pads[B]]));
			}
		}
		AddInfo(FString::Printf(TEXT("Puntos: el más cercano a otro, a %.0f uu; alturas de %.0f a %.0f"), Closest, Lowest, Highest));
		TestEqual(TEXT("Ninguno pegado a una salida"), NearSpawn, 0);
		TestTrue(TEXT("Repartidos (más de 8 m entre dos)"), Closest > 800.0);
		TestTrue(TEXT("En pisos distintos de la diana (unos se inundan antes)"), Highest - Lowest > 150.f);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
