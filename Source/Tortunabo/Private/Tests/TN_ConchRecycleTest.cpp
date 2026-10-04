// Concha reciclada (#568): una concha trampa que se gasta (bDestroyAfterActivation) deja en su sitio un pickup de verdad
// (ATN_PickupInteractableBase de PickupActorClass) con el ítem que se gastó al colocarla, y quien lo coge con E vuelve a
// tener la concha. Antes dejaba otra ATN_ConchPickup sin colocar que desaparecía al pisarla sin dar nada.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Items.ConchRecycle; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"
#include "Lobby/TN_TutorialPractice.h"
#include "Player/TN_InventoryComponent.h"
#include "World/TN_ConchPickup.h"
#include "World/TN_PickupInteractableBase.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNConchRecycleTestDetail
{
	/** Mundo de juego mínimo con BeginPlay ya hecho (los actores que se crean después lo reciben al aparecer). */
	UWorld* CreateGameWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		World->GetWorldSettings()->NotifyBeginPlay();
		return World;
	}

	void DestroyGameWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	FTN_InventoryItem MakeConchItem()
	{
		FTN_InventoryItem Item;
		Item.ItemId = TEXT("Test_Conch");
		Item.UseType = ETN_ItemUseType::Conch;
		Item.PickupActorClass = ATN_PickupInteractableBase::StaticClass();
		return Item;
	}

	/** Concha colocada como trampa en el origen, que un cangrejo de prácticas pisa (el camino real del solape). */
	ATN_ConchPickup* PlaceAndTrigger(UWorld* World, const FTN_InventoryItem* Item)
	{
		ATN_ConchPickup* Conch = World->SpawnActor<ATN_ConchPickup>(ATN_ConchPickup::StaticClass(), FTransform::Identity);
		ATN_TutorialDummy* Enemy = World->SpawnActor<ATN_TutorialDummy>(ATN_TutorialDummy::StaticClass(), FTransform(FVector(2000.0, 0.0, 0.0)));
		if (!Conch || !Enemy) { return Conch; }
		if (Item) { Conch->SetRecycledItem(*Item); }
		Conch->PlaceAsTrap(FVector::ZeroVector);
		if (USphereComponent* Sphere = Conch->FindComponentByClass<USphereComponent>())
		{
			Sphere->OnComponentBeginOverlap.Broadcast(Sphere, Enemy, nullptr, 0, false, FHitResult());
		}
		return Conch;
	}

	int32 CountActors(UWorld* World, UClass* Class)
	{
		int32 Count = 0;
		for (TActorIterator<AActor> It(World, Class); It; ++It)
		{
			if (IsValid(*It)) { ++Count; }
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNConchRecycleTest,
	"Tortunabo.Items.ConchRecycle.PickupWithItem",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNConchRecycleTest::RunTest(const FString& Parameters)
{
	using namespace TNConchRecycleTestDetail;
	UWorld* World = CreateGameWorld();
	const FTN_InventoryItem Item = MakeConchItem();
	ATN_ConchPickup* Conch = PlaceAndTrigger(World, &Item);
	if (!TestNotNull(TEXT("Concha creada"), Conch))
	{
		DestroyGameWorld(World);
		return false;
	}

	// Al gastarse queda un único pickup de verdad y ninguna concha nueva sin colocar.
	TestEqual(TEXT("Queda un pickup recogible"), CountActors(World, ATN_PickupInteractableBase::StaticClass()), 1);
	TestEqual(TEXT("No aparece otra ATN_ConchPickup (solo la gastada, que se va)"), CountActors(World, ATN_ConchPickup::StaticClass()), 1);
	TestTrue(TEXT("La concha gastada tiene los segundos contados"), Conch->GetLifeSpan() > 0.f);

	ATN_PickupInteractableBase* Pickup = nullptr;
	for (TActorIterator<ATN_PickupInteractableBase> It(World); It; ++It) { Pickup = *It; }
	ACharacter* Taker = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FVector(100.0, 0.0, 0.0)));
	UTN_InventoryComponent* Inventory = Taker ? NewObject<UTN_InventoryComponent>(Taker, TEXT("TestInventory")) : nullptr;
	if (!TestNotNull(TEXT("Pickup"), Pickup) || !TestNotNull(TEXT("Inventario"), Inventory))
	{
		DestroyGameWorld(World);
		return false;
	}
	Inventory->RegisterComponent();

	// Con E (Interact) se coge y la concha vuelve al inventario de quien la recoge.
	TestTrue(TEXT("Se puede coger"), Pickup->CanInteract(Taker));
	Pickup->Interact(Taker);
	TestTrue(TEXT("Quien la coge la tiene equipada"), Inventory->HasEquippedItem());
	TestEqual(TEXT("Es la concha que se gastó"), Inventory->GetEquippedItem().ItemId, Item.ItemId);
	TestTrue(TEXT("Es de uso concha"), Inventory->GetEquippedItem().UseType == ETN_ItemUseType::Conch);

	DestroyGameWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNConchRecycleNoItemTest,
	"Tortunabo.Items.ConchRecycle.WithoutItem",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNConchRecycleNoItemTest::RunTest(const FString& Parameters)
{
	using namespace TNConchRecycleTestDetail;
	UWorld* World = CreateGameWorld();
	// Sin ítem (colocada a mano o desde un Blueprint sin SetRecycledItem): se gasta y no deja nada, ni otra concha.
	ATN_ConchPickup* Conch = PlaceAndTrigger(World, nullptr);
	if (TestNotNull(TEXT("Concha creada"), Conch))
	{
		TestEqual(TEXT("Sin ítem no hay pickup"), CountActors(World, ATN_PickupInteractableBase::StaticClass()), 0);
		TestEqual(TEXT("Sin ítem no aparece otra concha"), CountActors(World, ATN_ConchPickup::StaticClass()), 1);
		TestTrue(TEXT("La concha gastada se va igualmente"), Conch->GetLifeSpan() > 0.f);
	}
	DestroyGameWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
