// Objetos del coop (GDD oficial; TN_CoopItemRules.h): ItemId, apilado en el inventario y tabla de botín del coop y del charco
// de pesca. Lógica pura, sin mundo.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Coop.Items; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Game/TN_CoopItemRules.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/TN_CoopItemComponent.h"
#include "GameFramework/Character.h"
#include "World/Beach/TN_RaceItems.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopItemsIdTest,
	"Tortunabo.Coop.Items.ItemId",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopItemsIdTest::RunTest(const FString& Parameters)
{
	ETNCoopItem Kind = ETNCoopItem::None;
	int32 Count = 0;
	TestTrue(TEXT("Sin objeto no hay ItemId"), TNCoopItemRules::MakeItemId(ETNCoopItem::None, 1).IsNone());
	TestFalse(TEXT("Un ItemId de TcT no es del coop"), TNCoopItemRules::ParseItemId(FName(TEXT("Tct_Shovel_2")), Kind, Count));
	TestFalse(TEXT("Sin cuenta no vale"), TNCoopItemRules::ParseItemId(FName(TEXT("Coop_Harpoon_")), Kind, Count));
	TestFalse(TEXT("Cuenta cero no vale"), TNCoopItemRules::ParseItemId(FName(TEXT("Coop_Harpoon_0")), Kind, Count));
	TestFalse(TEXT("Código desconocido no vale"), TNCoopItemRules::ParseItemId(FName(TEXT("Coop_Nada_2")), Kind, Count));
	TestTrue(TEXT("Tras el último uso no queda nada"), TNCoopItemRules::ItemIdAfterUse(FName(TEXT("Coop_Nada_1"))).IsNone());

	for (const ETNCoopItem Each : TNCoopItemRules::AllKinds())
	{
		const FTNCoopItemSpec& Spec = TNCoopItemRules::Spec(Each);
		TestEqual(FString::Printf(TEXT("Ficha de %s en su sitio"), Spec.Code), Spec.Kind, Each);
		const FName Id = TNCoopItemRules::MakeItemId(Each, TNCoopItemRules::InitialCount(Each));
		TestTrue(FString::Printf(TEXT("%s se lee"), Spec.Code), TNCoopItemRules::ParseItemId(Id, Kind, Count));
		TestEqual(FString::Printf(TEXT("%s: mismo objeto"), Spec.Code), Kind, Each);
		TestEqual(FString::Printf(TEXT("%s: misma cuenta"), Spec.Code), Count, TNCoopItemRules::InitialCount(Each));
		const FName After = TNCoopItemRules::ItemIdAfterUse(TNCoopItemRules::MakeItemId(Each, 2));
		TestEqual(FString::Printf(TEXT("%s: un uso menos"), Spec.Code), After, TNCoopItemRules::MakeItemId(Each, 1));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopItemsStackTest,
	"Tortunabo.Coop.Items.Stack",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopItemsStackTest::RunTest(const FString& Parameters)
{
	int32 Count = 0;
	// Apilables (límite 3).
	TestEqual(TEXT("1 + 1 de 3: se apila"), TNCoopItemRules::DecideStackCounts(3, 1, 1, 1, Count), ETNCoopStack::Merge);
	TestEqual(TEXT("1 + 1 de 3: quedan 2"), Count, 2);
	TestEqual(TEXT("2 + 2 de 3: se apila hasta el tope"), TNCoopItemRules::DecideStackCounts(3, 1, 2, 2, Count), ETNCoopStack::Merge);
	TestEqual(TEXT("2 + 2 de 3: quedan 3"), Count, 3);
	TestEqual(TEXT("3 de 3: lleno, no se coge"), TNCoopItemRules::DecideStackCounts(3, 1, 3, 1, Count), ETNCoopStack::Full);
	TestEqual(TEXT("3 de 3: siguen 3"), Count, 3);
	// Sin apilado (límite 1).
	TestEqual(TEXT("Límite 1: el segundo no se coge"), TNCoopItemRules::DecideStackCounts(1, 1, 1, 1, Count), ETNCoopStack::Full);
	// Herramientas de varios usos: coger otra recarga, nunca suma.
	TestEqual(TEXT("Herramienta gastada: recarga"), TNCoopItemRules::DecideStackCounts(1, 15, 4, 15, Count), ETNCoopStack::Merge);
	TestEqual(TEXT("Herramienta gastada: vuelve a 15"), Count, 15);
	TestEqual(TEXT("Herramienta llena: no se coge"), TNCoopItemRules::DecideStackCounts(1, 15, 15, 15, Count), ETNCoopStack::Full);
	TestEqual(TEXT("Una con menos usos no recarga"), TNCoopItemRules::DecideStackCounts(1, 15, 10, 3, Count), ETNCoopStack::Full);
	TestEqual(TEXT("Una con menos usos: siguen 10"), Count, 10);
	// Objetos distintos o que no son del coop: otro hueco.
	TestEqual(TEXT("Sin objeto del coop: otro hueco"), TNCoopItemRules::DecideStack(ETNCoopItem::None, 1, ETNCoopItem::None, 1, Count),
		ETNCoopStack::Separate);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopItemsLootTableTest,
	"Tortunabo.Coop.Items.LootTable",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopItemsLootTableTest::RunTest(const FString& Parameters)
{
	// Filas de DT_Items: peso de quien sortea x 15; las de los catálogos de código, nunca.
	TestEqual(TEXT("Fila normal: 15"), TNCoopItemRules::CatalogWeight(ETN_ItemUseType::Throwable, 1.f), 15.f);
	TestEqual(TEXT("Tótem a 0,3: 4,5"), TNCoopItemRules::CatalogWeight(ETN_ItemUseType::Totem, 0.3f), 4.5f, 1.e-4f);
	TestEqual(TEXT("Sin uso: fuera"), TNCoopItemRules::CatalogWeight(ETN_ItemUseType::None, 1.f), 0.f);
	TestEqual(TEXT("De carrera: fuera"), TNCoopItemRules::CatalogWeight(ETN_ItemUseType::RaceItem, 1.f), 0.f);
	TestEqual(TEXT("De TcT: fuera"), TNCoopItemRules::CatalogWeight(ETN_ItemUseType::TctItem, 1.f), 0.f);
	TestEqual(TEXT("Del coop: fuera (salen por su peso)"), TNCoopItemRules::CatalogWeight(ETN_ItemUseType::CoopItem, 1.f), 0.f);
	TestEqual(TEXT("Peso negativo: fuera"), TNCoopItemRules::CatalogWeight(ETN_ItemUseType::Throwable, -1.f), 0.f);

	// Sorteo ponderado.
	TestEqual(TEXT("Sin pesos: nada"), TNCoopItemRules::PickWeighted({}, 0.5f), INDEX_NONE);
	TestEqual(TEXT("Solo pesos 0: nada"), TNCoopItemRules::PickWeighted({ 0.f, 0.f }, 0.5f), INDEX_NONE);
	TestEqual(TEXT("El único con peso sale siempre"), TNCoopItemRules::PickWeighted({ 0.f, 2.f, 0.f }, 0.99f), 1);
	TestEqual(TEXT("Primera mitad"), TNCoopItemRules::PickWeighted({ 1.f, 1.f }, 0.25f), 0);
	TestEqual(TEXT("Segunda mitad"), TNCoopItemRules::PickWeighted({ 1.f, 1.f }, 0.75f), 1);
	TestEqual(TEXT("Tirada de 1 recortada: el último"), TNCoopItemRules::PickWeighted({ 1.f, 1.f }, 1.f), 1);

	// La tabla del coop: primero las filas de DT_Items y después los objetos de código con su peso.
	const TArray<float> Catalog = { 15.f, 15.f };
	float CodeTotal = 0.f;
	for (const ETNCoopItem Kind : TNCoopItemRules::AllKinds())
	{
		CodeTotal += TNCoopItemRules::Spec(Kind).LootWeight;
	}
	const float Total = 30.f + CodeTotal;
	const FTNCoopLootPick First = TNCoopItemRules::PickLoot(Catalog, 0.f);
	TestEqual(TEXT("Tirada 0: la primera fila"), First.CatalogIndex, 0);
	const FTNCoopLootPick Second = TNCoopItemRules::PickLoot(Catalog, 20.f / Total);
	TestEqual(TEXT("La segunda fila"), Second.CatalogIndex, 1);
	const FTNCoopLootPick Empty = TNCoopItemRules::PickLoot({}, 0.5f);
	TestEqual(TEXT("Sin filas: sale un objeto de código si hay alguno con peso"), Empty.IsValid(), CodeTotal > 0.f);
	TestEqual(TEXT("Sin filas nunca sale una fila"), Empty.CatalogIndex, INDEX_NONE);
	if (CodeTotal > 0.f)
	{
		const FTNCoopLootPick Last = TNCoopItemRules::PickLoot(Catalog, 0.9999f);
		TestTrue(TEXT("Tirada alta: un objeto de código"), Last.Kind != ETNCoopItem::None && Last.CatalogIndex == INDEX_NONE);
	}
	float ChanceSum = 0.f;
	for (const ETNCoopItem Kind : TNCoopItemRules::AllKinds())
	{
		ChanceSum += TNCoopItemRules::LootChance(Catalog, Kind);
	}
	TestEqual(TEXT("Probabilidad de los de código: su parte de la tabla"), ChanceSum, CodeTotal / Total, 1.e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopItemsPufferTest,
	"Tortunabo.Coop.Items.PufferFish",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopItemsPufferTest::RunTest(const FString& Parameters)
{
	// Ficha: un uso, sin apilado, peso 15 en la tabla del coop.
	const FTNCoopItemSpec& Spec = TNCoopItemRules::Spec(ETNCoopItem::PufferFish);
	TestEqual(TEXT("Un uso"), Spec.Uses, 1);
	TestEqual(TEXT("Límite de apilado 1"), Spec.MaxStack, 1);
	TestEqual(TEXT("Peso 15"), Spec.LootWeight, 15.f);
	int32 Count = 0;
	TestEqual(TEXT("No se apila: el segundo no se coge"),
		TNCoopItemRules::DecideStack(ETNCoopItem::PufferFish, 1, ETNCoopItem::PufferFish, 1, Count), ETNCoopStack::Full);
	TestTrue(TEXT("Sale en la tabla del coop"), TNCoopItemRules::LootChance({ 15.f, 15.f }, ETNCoopItem::PufferFish) > 0.f);

	// Duración: 5 s de protección y después el mareo corto.
	FTNPufferState State;
	TestFalse(TEXT("Sin comerlo no protege"), State.IsProtected(10.0));
	TestTrue(TEXT("Se come"), State.Start(10.0, TNCoopItemTuning::PufferSeconds, TNCoopItemTuning::PufferDizzySeconds));
	TestTrue(TEXT("Protegida al empezar"), State.IsProtected(10.0));
	TestTrue(TEXT("Protegida a los 4,9 s"), State.IsProtected(14.9));
	TestFalse(TEXT("Sin protección a los 5 s"), State.IsProtected(15.0));
	TestFalse(TEXT("Sin mareo mientras protege"), State.IsDizzy(12.0));
	TestTrue(TEXT("Mareada al acabar"), State.IsDizzy(15.1));
	TestFalse(TEXT("El mareo es corto"), State.IsDizzy(15.0 + TNCoopItemTuning::PufferDizzySeconds + 0.01));
	TestTrue(TEXT("Mareo corto (menos que la protección)"), TNCoopItemTuning::PufferDizzySeconds < TNCoopItemTuning::PufferSeconds);

	// No se apila ni se alarga mientras dura.
	TestFalse(TEXT("Otro durante la protección: no"), State.Start(12.0, TNCoopItemTuning::PufferSeconds, TNCoopItemTuning::PufferDizzySeconds));
	TestEqual(TEXT("La protección no se alarga"), State.ProtectEnd, 15.0);
	TestTrue(TEXT("Acabada, se puede comer otro"), State.Start(16.0, TNCoopItemTuning::PufferSeconds, TNCoopItemTuning::PufferDizzySeconds));
	TestFalse(TEXT("El nuevo quita el mareo"), State.IsDizzy(16.5));
	return true;
}

namespace TNCoopItemsTestDetail
{
	/** Mundo de juego sin ventana (con autoridad) para las pruebas con actores; DestroyWorld lo quita. */
	UWorld* CreateWorld(const TCHAR* Name)
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, Name);
		if (World)
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}
		return World;
	}

	void DestroyWorld(UWorld* World)
	{
		if (World)
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopItemsPufferWorldTest,
	"Tortunabo.Coop.Items.PufferWorld",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopItemsPufferWorldTest::RunTest(const FString& Parameters)
{
	UWorld* World = TNCoopItemsTestDetail::CreateWorld(TEXT("TNCoopPufferTestWorld"));
	if (!TestNotNull(TEXT("Mundo de prueba"), World))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Turtle = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FVector(0.0, 0.0, 100.0)), Params);
	if (TestNotNull(TEXT("Personaje"), Turtle))
	{
		TestFalse(TEXT("Sin pez globo se le puede derribar"), TNRaceItems::IsInvulnerable(Turtle));
		UTN_CoopItemComponent* Effects = UTN_CoopItemComponent::FindOrAddOn(Turtle);
		if (TestNotNull(TEXT("El servidor le añade el componente"), Effects))
		{
			TestTrue(TEXT("Come el pez globo"), Effects->GrantPuffer());
			TestTrue(TEXT("Protegida: nada la derriba ni la aturde"), TNRaceItems::IsInvulnerable(Turtle));
			TestFalse(TEXT("Otro mientras dura: no se apila"), Effects->GrantPuffer());
			Effects->ClearEffects();
			TestFalse(TEXT("Sin efectos vuelve a ser vulnerable"), TNRaceItems::IsInvulnerable(Turtle));
		}
	}
	TNCoopItemsTestDetail::DestroyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopItemsPeelTest,
	"Tortunabo.Coop.Items.SlipperyPeel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopItemsPeelTest::RunTest(const FString& Parameters)
{
	// Ficha: se apilan dos, peso 15.
	const FTNCoopItemSpec& Spec = TNCoopItemRules::Spec(ETNCoopItem::SlipperyPeel);
	TestEqual(TEXT("Límite de apilado 2"), Spec.MaxStack, 2);
	TestEqual(TEXT("Peso 15"), Spec.LootWeight, 15.f);
	int32 Count = 0;
	TestEqual(TEXT("La segunda se apila"), TNCoopItemRules::DecideStack(ETNCoopItem::SlipperyPeel, 1, ETNCoopItem::SlipperyPeel, 1, Count), ETNCoopStack::Merge);
	TestEqual(TEXT("Dos en el hueco"), Count, 2);
	TestEqual(TEXT("La tercera no"), TNCoopItemRules::DecideStack(ETNCoopItem::SlipperyPeel, 2, ETNCoopItem::SlipperyPeel, 1, Count), ETNCoopStack::Full);

	// Alcance: 8 m en planta como mucho; lo que está más cerca se queda donde se apunta.
	const FVector Origin(0.0, 0.0, 50.0);
	const FVector Far = TNCoopItemRules::ClampThrowTarget(Origin, FVector(1500.0, 0.0, 30.0), TNCoopItemTuning::PeelRange);
	TestTrue(TEXT("Lejos: a 8 m"), Far.Equals(FVector(800.0, 0.0, 30.0), 0.1));
	const FVector Diagonal = TNCoopItemRules::ClampThrowTarget(Origin, FVector(1000.0, 1000.0, 0.0), TNCoopItemTuning::PeelRange);
	TestEqual(TEXT("En diagonal, también 8 m"), FVector::Dist2D(Origin, Diagonal), 800.0, 0.1);
	const FVector Near(500.0, 300.0, -20.0);
	TestTrue(TEXT("Cerca: donde se apunta"), TNCoopItemRules::ClampThrowTarget(Origin, Near, TNCoopItemTuning::PeelRange).Equals(Near));
	TestEqual(TEXT("El alcance de la cáscara es 8 m"), TNCoopItemTuning::PeelRange, 800.f);

	// Arco determinista (cada máquina lo dibuja igual con la misma hora).
	const FVector To(800.0, 0.0, 0.0);
	TestTrue(TEXT("Sale de la mano"), TNCoopItemRules::ArcPoint(Origin, To, 0.f, 200.f).Equals(Origin));
	TestTrue(TEXT("Cae donde debe"), TNCoopItemRules::ArcPoint(Origin, To, 1.f, 200.f).Equals(To));
	TestTrue(TEXT("Por encima en la mitad"), TNCoopItemRules::ArcPoint(Origin, To, 0.5f, 200.f).Equals(FVector(400.0, 0.0, 225.0)));
	TestTrue(TEXT("Más de 1 se queda en el suelo"), TNCoopItemRules::ArcPoint(Origin, To, 3.f, 200.f).Equals(To));
	TestTrue(TEXT("Vuelo mínimo"), TNCoopItemRules::ThrowFlightSeconds(0.f) >= TNCoopItemTuning::ThrowMinSeconds);

	// El parche: quien lo pisa (dentro del radio y a su altura).
	const FVector Patch(1000.0, 0.0, 0.0);
	TestTrue(TEXT("Encima: lo pisa"), TNCoopItemRules::IsOnPatch(Patch, Patch + FVector(20.0, 0.0, 0.0)));
	TestFalse(TEXT("Fuera del radio: no"), TNCoopItemRules::IsOnPatch(Patch, Patch + FVector(TNCoopItemTuning::PeelRadius + 1.0, 0.0, 0.0)));
	TestFalse(TEXT("Saltando por encima: no"), TNCoopItemRules::IsOnPatch(Patch, Patch + FVector(0.0, 0.0, TNCoopItemTuning::PeelStepHeight + 10.0)));

	// Resbalón: hacia donde iba, algo en el aire y sin derribo (solo velocidad).
	const FVector Running = TNCoopItemRules::SlipVelocity(FVector(400.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0));
	TestTrue(TEXT("Sigue hacia donde iba"), Running.X > 0.0 && FMath::IsNearlyZero(Running.Y));
	TestTrue(TEXT("Nunca menos que el resbalón mínimo"), FVector(Running.X, Running.Y, 0.0).Size() >= TNCoopItemTuning::PeelSlipSpeed - 0.1);
	TestEqual(TEXT("Algo hacia arriba (sin agarre)"), Running.Z, static_cast<double>(TNCoopItemTuning::PeelSlipUp), 0.1);
	const FVector Standing = TNCoopItemRules::SlipVelocity(FVector::ZeroVector, FVector(0.0, 1.0, 0.0));
	TestTrue(TEXT("Parada: hacia donde mira"), Standing.Y > 0.0 && FMath::IsNearlyZero(Standing.X));
	const FVector Fast = TNCoopItemRules::SlipVelocity(FVector(1000.0, 0.0, 0.0), FVector::ForwardVector);
	TestEqual(TEXT("Corriendo: algo más rápido que iba"), Fast.X, 1200.0, 0.1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopItemsShellTest,
	"Tortunabo.Coop.Items.StunShell",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopItemsShellTest::RunTest(const FString& Parameters)
{
	// Ficha y apilado: hasta 3 en el inventario, peso 10, ItemId propio (no la concha trampa de DT_Items).
	const FTNCoopItemSpec& Spec = TNCoopItemRules::Spec(ETNCoopItem::StunShell);
	TestEqual(TEXT("Límite de apilado 3"), Spec.MaxStack, 3);
	TestEqual(TEXT("Peso 10"), Spec.LootWeight, 10.f);
	TestEqual(TEXT("ItemId propio"), TNCoopItemRules::MakeItemId(ETNCoopItem::StunShell, 3), FName(TEXT("Coop_StunShell_3")));
	int32 Count = 0;
	TestEqual(TEXT("2 + 1: se apila"), TNCoopItemRules::DecideStack(ETNCoopItem::StunShell, 2, ETNCoopItem::StunShell, 1, Count), ETNCoopStack::Merge);
	TestEqual(TEXT("Tres en el hueco"), Count, 3);
	TestEqual(TEXT("La cuarta no se coge"), TNCoopItemRules::DecideStack(ETNCoopItem::StunShell, 3, ETNCoopItem::StunShell, 1, Count), ETNCoopStack::Full);
	TestEqual(TEXT("Con otra cosa: otro hueco"), TNCoopItemRules::DecideStack(ETNCoopItem::SlipperyPeel, 1, ETNCoopItem::StunShell, 1, Count),
		ETNCoopStack::Separate);
	TestEqual(TEXT("Tras lanzar una de tres quedan dos"), TNCoopItemRules::ItemIdAfterUse(FName(TEXT("Coop_StunShell_3"))), FName(TEXT("Coop_StunShell_2")));

	// Objetivo: solo enemigos que se dejan aturdir; nunca tortugas.
	TestTrue(TEXT("Enemigo que se marea: sí"), TNCoopItemRules::CanShellStun(false, true, true));
	TestFalse(TEXT("Enemigo que no se marea: no"), TNCoopItemRules::CanShellStun(false, true, false));
	TestFalse(TEXT("Tortuga: nunca"), TNCoopItemRules::CanShellStun(true, false, true));
	TestFalse(TEXT("Tortuga aunque implemente la interfaz: nunca"), TNCoopItemRules::CanShellStun(true, true, true));
	TestFalse(TEXT("Decorado: no"), TNCoopItemRules::CanShellStun(false, false, true));

	// Alcance de 10 m y acierto al llegar.
	TestEqual(TEXT("Alcance de 10 m"), TNCoopItemTuning::ShellRange, 1000.f);
	const FVector Origin(0.0, 0.0, 50.0);
	TestEqual(TEXT("Más lejos se recorta a 10 m"),
		FVector::Dist2D(Origin, TNCoopItemRules::ClampThrowTarget(Origin, FVector(3000.0, 0.0, 0.0), TNCoopItemTuning::ShellRange)), 1000.0, 0.1);
	TestTrue(TEXT("El enemigo sigue ahí: le da"), TNCoopItemRules::IsShellHit(FVector(500.0, 0.0, 0.0), FVector(560.0, 40.0, 0.0)));
	TestFalse(TEXT("Se ha ido lejos: falla"), TNCoopItemRules::IsShellHit(FVector(500.0, 0.0, 0.0), FVector(900.0, 0.0, 0.0)));
	TestTrue(TEXT("Aturde unos segundos"), TNCoopItemTuning::ShellStunSeconds >= 2.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
