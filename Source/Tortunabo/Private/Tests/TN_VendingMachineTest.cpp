// Máquina expendedora (#859): la chapa que cruza la ranura suma crédito a quien la lanzó (en el servidor); con crédito
// suficiente se compra el objeto elegido, que sale por la bandeja; sin él no se cobra ni sale nada. Lista de objetos y precios
// en dato (UTN_EconomySettings / UTN_VendingStockData).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Economy.Vending; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/TN_ChapaRules.h"
#include "Game/TN_CoopItems.h"
#include "Game/TN_VendingStock.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_EconomySettings.h"
#include "World/TN_Chapa.h"
#include "World/TN_PickupInteractableBase.h"
#include "World/TN_VendingMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNVendingMachineTest
{
	/** Mundo de juego con BeginPlay ya hecho: los actores que se crean después lo reciben al aparecer. */
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNVendingTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
		}

		~FPlayWorld()
		{
			if (World->HasBegunPlay()) { World->EndPlay(EEndPlayReason::Quit); }
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	template <typename T>
	T* Spawn(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<T>(T::StaticClass(), Where, FRotator::ZeroRotator, Params);
	}

	int32 CountPickups(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ATN_PickupInteractableBase> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	/** Una lista de prueba: el arpón a 5 y el pez globo a 1. */
	UTN_VendingStockData* MakeStock()
	{
		UTN_VendingStockData* Stock = NewObject<UTN_VendingStockData>();
		Stock->Offers.Add(FTNVendingOffer{FName(TEXT("Harpoon")), 5, FText::GetEmpty()});
		Stock->Offers.Add(FTNVendingOffer{FName(TEXT("PufferFish")), 1, FText::GetEmpty()});
		return Stock;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVendingRulesTest,
	"Tortunabo.Economy.Vending.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVendingRulesTest::RunTest(const FString& Parameters)
{
	using namespace TNChapaRules;
	TestTrue(TEXT("Con crédito justo, compra"), DecideBuy(5, 5, true) == EBuy::Bought);
	TestTrue(TEXT("Sin crédito, no"), DecideBuy(4, 5, true) == EBuy::NotEnoughCredit);
	TestTrue(TEXT("Objeto que no existe, no"), DecideBuy(9, 5, false) == EBuy::NoOffer);
	TestEqual(TEXT("Una chapa suma su valor"), CreditAfterInsert(3, 1, 99), 4);
	TestEqual(TEXT("Sin pasar del tope"), CreditAfterInsert(99, 1, 99), 99);
	TestEqual(TEXT("Siguiente objeto"), NextOffer(2, 4), 3);
	TestEqual(TEXT("Vuelve al primero"), NextOffer(3, 4), 0);
	TestEqual(TEXT("Sin objetos"), NextOffer(0, 0), -1);
	TestTrue(TEXT("Soltar pronto es pulsar"), IsTap(0.2, 0.8));
	TestFalse(TEXT("Mantener es comprar"), IsTap(0.8, 0.8));

	const FVector Extent(10.0, 14.0, 20.0);
	TestTrue(TEXT("Cruza la ranura"), SegmentHitsBox(FVector(100.0, 0.0, 5.0), FVector(-100.0, 0.0, -5.0), Extent));
	TestTrue(TEXT("Empieza dentro"), SegmentHitsBox(FVector(0.0, 0.0, 0.0), FVector(100.0, 100.0, 100.0), Extent));
	TestFalse(TEXT("Pasa por encima"), SegmentHitsBox(FVector(100.0, 0.0, 30.0), FVector(-100.0, 0.0, 25.0), Extent));
	TestFalse(TEXT("Pasa de lado"), SegmentHitsBox(FVector(100.0, 20.0, 0.0), FVector(-100.0, 16.0, 0.0), Extent));
	TestFalse(TEXT("Se queda corta"), SegmentHitsBox(FVector(100.0, 0.0, 0.0), FVector(20.0, 0.0, 0.0), Extent));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVendingDefaultStockTest,
	"Tortunabo.Economy.Vending.DefaultStock",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVendingDefaultStockTest::RunTest(const FString& Parameters)
{
	// La lista por defecto (Config/DefaultGame.ini): 4 objetos que existen, con los precios de la hoja Economía (plan §4).
	const TArray<FTNVendingOffer>& Offers = UTN_EconomySettings::Get().DefaultVendingOffers;
	TestEqual(TEXT("4 objetos a la venta"), Offers.Num(), TNVending::OffersPerMachine);
	const TMap<FName, int32> ExcelPrices = { { TEXT("StaminaBoost"), 2 }, { TEXT("Totem"), 2 }, { TEXT("Harpoon"), 5 } };
	for (const FTNVendingOffer& Offer : Offers)
	{
		FTN_InventoryItem Item;
		TestTrue(FString::Printf(TEXT("%s existe"), *Offer.ItemId.ToString()), TNVending::ResolveOfferItem(Offer.ItemId, Item));
		TestFalse(FString::Printf(TEXT("%s tiene nombre"), *Offer.ItemId.ToString()), TNVending::OfferName(Offer).IsEmpty());
		if (const int32* Price = ExcelPrices.Find(Offer.ItemId))
		{
			TestEqual(FString::Printf(TEXT("Precio de %s"), *Offer.ItemId.ToString()), Offer.Price, *Price);
		}
	}
	FTN_InventoryItem Missing;
	TestFalse(TEXT("Un objeto que no existe no se vende"), TNVending::ResolveOfferItem(TEXT("NoExiste"), Missing));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVendingSlotTest,
	"Tortunabo.Economy.Vending.ChapaThroughSlot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVendingSlotTest::RunTest(const FString& Parameters)
{
	using namespace TNVendingMachineTest;
	FPlayWorld Play;
	ATN_VendingMachine* Machine = Spawn<ATN_VendingMachine>(Play.World, FVector::ZeroVector);
	ATortugaCharacter* Thrower = Spawn<ATortugaCharacter>(Play.World, FVector(600.0, 300.0, 200.0));
	if (!TestNotNull(TEXT("Máquina"), Machine) || !TestNotNull(TEXT("Tortuga"), Thrower))
	{
		return false;
	}
	// Hacia la cara de la máquina (+X), a 1000 cm/s: llega a la ranura en 0,3 s, tras caer lo que manda su gravedad.
	const FVector Slot = Machine->GetSlotLocation();
	const ATN_Chapa* Defaults = GetDefault<ATN_Chapa>();
	const double Drop = 0.5 * Defaults->GetFlightGravity() * 0.3 * 0.3;
	ATN_Chapa* Chapa = ATN_Chapa::SpawnFlying(Play.World, Slot + FVector(300.0, 0.0, Drop), FVector(-1000.0, 0.0, 0.0), Thrower);
	if (!TestNotNull(TEXT("Chapa"), Chapa))
	{
		return false;
	}
	Chapa->ServerStepFlight(0.0, 0.2);
	TestEqual(TEXT("Aún no ha llegado"), Machine->GetCreditFor(Thrower), 0);
	Chapa->ServerStepFlight(0.2, 0.45);
	TestEqual(TEXT("Entra por la ranura: 1 de crédito para quien la lanzó"), Machine->GetCreditFor(Thrower), 1);
	TestTrue(TEXT("La chapa se gasta"), Chapa->IsConsumed());

	// Caso negativo: por encima de la ranura no cuenta.
	ATN_Chapa* High = ATN_Chapa::SpawnFlying(Play.World, Slot + FVector(300.0, 0.0, Drop + 120.0), FVector(-1000.0, 0.0, 0.0), Thrower);
	if (!TestNotNull(TEXT("Chapa alta"), High))
	{
		return false;
	}
	High->ServerStepFlight(0.0, 0.45);
	TestEqual(TEXT("Por encima no suma"), Machine->GetCreditFor(Thrower), 1);
	TestFalse(TEXT("No se gasta"), High->IsConsumed());

	// Sin quien la lanzara (las de las cajas) no se apunta a nadie.
	ATN_Chapa* Loose = ATN_Chapa::SpawnFlying(Play.World, Slot + FVector(300.0, 0.0, Drop), FVector(-1000.0, 0.0, 0.0), nullptr);
	if (TestNotNull(TEXT("Chapa suelta"), Loose))
	{
		Loose->ServerStepFlight(0.0, 0.45);
		TestFalse(TEXT("Sin dueño no entra"), Loose->IsConsumed());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVendingBuyTest,
	"Tortunabo.Economy.Vending.Buy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVendingBuyTest::RunTest(const FString& Parameters)
{
	using namespace TNVendingMachineTest;
	using TNChapaRules::EBuy;
	FPlayWorld Play;
	ATN_VendingMachine* Machine = Spawn<ATN_VendingMachine>(Play.World, FVector::ZeroVector);
	ATortugaCharacter* Buyer = Spawn<ATortugaCharacter>(Play.World, FVector(200.0, 0.0, 100.0));
	ATortugaCharacter* Other = Spawn<ATortugaCharacter>(Play.World, FVector(200.0, 150.0, 100.0));
	if (!TestNotNull(TEXT("Máquina"), Machine) || !TestNotNull(TEXT("Tortuga"), Buyer) || !TestNotNull(TEXT("Otra"), Other))
	{
		return false;
	}
	Machine->SetStock(MakeStock());
	TestEqual(TEXT("Vende el arpón primero"), Machine->GetOffers()[Machine->GetSelectedIndex()].ItemId, FName(TEXT("Harpoon")));
	const int32 PickupsBefore = CountPickups(Play.World);

	// Caso negativo: 4 de crédito no llegan para el arpón (5).
	for (int32 i = 0; i < 4; ++i)
	{
		Machine->ServerInsertChapa(Buyer, 1);
	}
	TestTrue(TEXT("Con 4 no compra"), Machine->ServerTryBuy(Buyer) == EBuy::NotEnoughCredit);
	TestEqual(TEXT("No se cobra nada"), Machine->GetCreditFor(Buyer), 4);
	TestEqual(TEXT("No sale nada"), CountPickups(Play.World), PickupsBefore);

	Machine->ServerInsertChapa(Buyer, 1);
	TestTrue(TEXT("Con 5 compra"), Machine->ServerTryBuy(Buyer) == EBuy::Bought);
	TestEqual(TEXT("Se cobra el precio"), Machine->GetCreditFor(Buyer), 0);
	TestEqual(TEXT("Sale un objeto"), CountPickups(Play.World), PickupsBefore + 1);
	bool bHarpoonOnTray = false;
	for (TActorIterator<ATN_PickupInteractableBase> It(Play.World); It; ++It)
	{
		const bool bHarpoon = TNCoopItems::KindOf(It->GetPickupItem()) == ETNCoopItem::Harpoon;
		bHarpoonOnTray |= bHarpoon && FVector::Dist(It->GetActorLocation(), Machine->GetTrayLocation()) < 1.0;
	}
	TestTrue(TEXT("Es el arpón, en la bandeja"), bHarpoonOnTray);

	// El crédito es de cada jugador: la otra no tiene.
	Machine->ServerCycleOffer();
	TestEqual(TEXT("Pasa al pez globo"), Machine->GetOffers()[Machine->GetSelectedIndex()].ItemId, FName(TEXT("PufferFish")));
	TestTrue(TEXT("La otra no tiene crédito"), Machine->ServerTryBuy(Other) == EBuy::NotEnoughCredit);

	// Pulsar (mantener y soltar enseguida) cambia de objeto; no compra.
	Machine->BeginHoldInteract(Other);
	Machine->EndHoldInteract(Other);
	TestEqual(TEXT("Pulsar vuelve al arpón"), Machine->GetSelectedIndex(), 0);
	TestEqual(TEXT("Sin compras de más"), CountPickups(Play.World), PickupsBefore + 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
