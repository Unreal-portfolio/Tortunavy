// Pulsar E sobre un pickup que no se puede coger (#570): ATortugaCharacter::ServerTryInteract usa el objeto de la mano
// solo si lo que lo impide es el inventario lleno (ATN_PickupInteractableBase::IsBlockedOnlyByFullInventory). Si otra
// tortuga lo acaba de coger (bTaken, que aún no había llegado a quien pulsa), no se gasta nada.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Items.FullInventory; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/TN_PickupInteractableBase.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNPickupFullInventoryTest
{
	/** Mundo de juego con BeginPlay ya hecho: los actores que se crean después lo reciben al aparecer. */
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNPickupFullInventoryTestWorld"));
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

	ATortugaCharacter* SpawnTurtle(UWorld* World, double X)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), FVector(X, 0.0, 200.0), FRotator::ZeroRotator, Params);
	}

	/** Una barrita (SelfStaminaFull): usarla la gasta sin necesitar nada más del mundo. */
	FTN_InventoryItem MakeBar(FName Id)
	{
		FTN_InventoryItem Item;
		Item.ItemId = Id;
		Item.UseType = ETN_ItemUseType::SelfStaminaFull;
		return Item;
	}

	ATN_PickupInteractableBase* SpawnPickup(UWorld* World, const FTN_InventoryItem& Item)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATN_PickupInteractableBase* Pickup = World->SpawnActor<ATN_PickupInteractableBase>(ATN_PickupInteractableBase::StaticClass(),
			FVector(60.0, 0.0, 120.0), FRotator::ZeroRotator, Params);
		if (Pickup) { Pickup->InitializeFromInventoryItem(Item); }
		return Pickup;
	}

	/** ServerTryInteract es un RPC privado: en un mundo sin red se ejecuta en el acto, como en el servidor. */
	bool CallServerTryInteract(ATortugaCharacter* Turtle, ATN_InteractableBase* Interactable)
	{
		UFunction* Function = Turtle->FindFunction(TEXT("ServerTryInteract"));
		if (!Function)
		{
			return false;
		}
		struct FParams { ATN_InteractableBase* Interactable; } Params{ Interactable };
		Turtle->ProcessEvent(Function, &Params);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPickupTakenKeepsItemTest,
	"Tortunabo.Items.FullInventory.TakenPickupKeepsEquipped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPickupTakenKeepsItemTest::RunTest(const FString& Parameters)
{
	using namespace TNPickupFullInventoryTest;
	FPlayWorld Play;
	ATortugaCharacter* Me = SpawnTurtle(Play.World, 0.0);
	ATortugaCharacter* Other = SpawnTurtle(Play.World, 150.0);
	ATN_PickupInteractableBase* Pickup = SpawnPickup(Play.World, MakeBar(TEXT("Test_Floor")));
	UTN_InventoryComponent* MyInventory = Me ? Me->GetInventoryComponent() : nullptr;
	if (!TestNotNull(TEXT("Tortuga"), Other) || !TestNotNull(TEXT("Inventario"), MyInventory) || !TestNotNull(TEXT("Pickup"), Pickup))
	{
		return false;
	}

	// Con un objeto en la mano y la ranura del caparazón libre; la otra tortuga coge el pickup justo antes.
	MyInventory->TryAddItem(MakeBar(TEXT("Test_Mine")));
	Pickup->Interact(Other);
	TestFalse(TEXT("El pickup ya está cogido"), Pickup->CanInteract(Me));
	TestFalse(TEXT("No es por el inventario lleno"), Pickup->IsBlockedOnlyByFullInventory(Me));

	TestTrue(TEXT("ServerTryInteract existe"), CallServerTryInteract(Me, Pickup));
	TestTrue(TEXT("Conserva el objeto de la mano"), MyInventory->HasEquippedItem());
	TestEqual(TEXT("Es el suyo"), MyInventory->GetEquippedItem().ItemId, FName(TEXT("Test_Mine")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPickupFullInventoryUsesItemTest,
	"Tortunabo.Items.FullInventory.FullUsesEquipped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPickupFullInventoryUsesItemTest::RunTest(const FString& Parameters)
{
	using namespace TNPickupFullInventoryTest;
	FPlayWorld Play;
	ATortugaCharacter* Me = SpawnTurtle(Play.World, 0.0);
	ATN_PickupInteractableBase* Pickup = SpawnPickup(Play.World, MakeBar(TEXT("Test_Floor")));
	UTN_InventoryComponent* MyInventory = Me ? Me->GetInventoryComponent() : nullptr;
	if (!TestNotNull(TEXT("Inventario"), MyInventory) || !TestNotNull(TEXT("Pickup"), Pickup))
	{
		return false;
	}

	// Las dos ranuras llenas: E sobre el pickup usa el de la mano (comportamiento de siempre) y el guardado sube.
	MyInventory->TryAddItem(MakeBar(TEXT("Test_Hand")));
	MyInventory->TryAddItem(MakeBar(TEXT("Test_Shell")));
	TestTrue(TEXT("Bloqueado solo por el inventario lleno"), Pickup->IsBlockedOnlyByFullInventory(Me));

	TestTrue(TEXT("ServerTryInteract existe"), CallServerTryInteract(Me, Pickup));
	TestEqual(TEXT("Se ha usado el de la mano: sube el guardado"), MyInventory->GetEquippedItem().ItemId, FName(TEXT("Test_Shell")));
	TestFalse(TEXT("La ranura del caparazón queda libre"), MyInventory->HasStoredItem());
	TestTrue(TEXT("El pickup sigue en el suelo"), Pickup->CanInteract(Me));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
