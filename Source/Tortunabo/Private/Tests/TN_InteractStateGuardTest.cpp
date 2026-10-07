// Guarda del servidor al interactuar (#891): ServerTryInteract, ServerBeginHoldInteract y
// UTN_InventoryComponent::ServerRotateItems rechazan la petición si la tortuga está noqueada, muerta, en el caparazón o en
// brazos de otra, aunque el cliente la haya mandado (con latencia pide antes de recibir el estado). Un caso negativo por
// estado y uno positivo de control, en un mundo de juego sin red (los RPC de servidor se ejecutan en el acto).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Net.InteractGuard; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/TN_PickupInteractableBase.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNInteractStateGuardTest
{
	/** Mundo de juego con BeginPlay ya hecho: los actores que se crean después lo reciben al aparecer. */
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNInteractStateGuardTestWorld"));
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

	/** Estados en los que la tortuga no puede usar las aletas. None es el control positivo. */
	enum class EState : uint8 { None, KnockedDown, Dead, InShell, Carried };

	const TCHAR* StateName(EState State)
	{
		switch (State)
		{
			case EState::KnockedDown: return TEXT("noqueada");
			case EState::Dead:        return TEXT("muerta");
			case EState::InShell:     return TEXT("en el caparazón");
			case EState::Carried:     return TEXT("en brazos");
			default:                  return TEXT("normal");
		}
	}

	ATortugaCharacter* SpawnTurtle(UWorld* World, double X)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), FVector(X, 0.0, 200.0), FRotator::ZeroRotator, Params);
	}

	FTN_InventoryItem MakeBar(FName Id)
	{
		FTN_InventoryItem Item;
		Item.ItemId = Id;
		Item.UseType = ETN_ItemUseType::SelfStaminaFull;
		return Item;
	}

	ATN_PickupInteractableBase* SpawnPickup(UWorld* World, double X, const FTN_InventoryItem& Item)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATN_PickupInteractableBase* Pickup = World->SpawnActor<ATN_PickupInteractableBase>(ATN_PickupInteractableBase::StaticClass(),
			FVector(X + 60.0, 0.0, 120.0), FRotator::ZeroRotator, Params);
		if (Pickup) { Pickup->InitializeFromInventoryItem(Item); }
		return Pickup;
	}

	bool SetBoolProperty(UObject* Object, FName Name, bool bValue)
	{
		FBoolProperty* Property = Object ? FindFProperty<FBoolProperty>(Object->GetClass(), Name) : nullptr;
		if (!Property) { return false; }
		Property->SetPropertyValue_InContainer(Object, bValue);
		return true;
	}

	bool SetObjectProperty(UObject* Object, FName Name, UObject* Value)
	{
		FObjectPropertyBase* Property = Object ? FindFProperty<FObjectPropertyBase>(Object->GetClass(), Name) : nullptr;
		if (!Property) { return false; }
		Property->SetObjectPropertyValue_InContainer(Object, Value);
		return true;
	}

	/** Pone el estado tal como lo tiene el servidor (las variables replicadas), sin pasar por la lógica que lo provoca. */
	bool ApplyState(ATortugaCharacter* Turtle, ATortugaCharacter* Carrier, EState State)
	{
		switch (State)
		{
			case EState::KnockedDown: return SetBoolProperty(Turtle, TEXT("bIsKnockedDown"), true);
			case EState::Dead:        return SetBoolProperty(Turtle, TEXT("bIsDead"), true);
			case EState::InShell:     return SetBoolProperty(Turtle->GetShellComponent(), TEXT("bIsInShell"), true);
			case EState::Carried:     return SetObjectProperty(Turtle->GetCarryComponent(), TEXT("CarriedBy"), Carrier);
			default:                  return true;
		}
	}

	/** Llama a un RPC de servidor por reflexión: en un mundo sin red se ejecuta en el acto, como en el servidor. */
	bool CallRpc(UObject* Target, FName Name, void* Params)
	{
		UFunction* Function = Target ? Target->FindFunction(Name) : nullptr;
		if (!Function) { return false; }
		Target->ProcessEvent(Function, Params);
		return true;
	}

	const EState AllStates[] = { EState::None, EState::KnockedDown, EState::Dead, EState::InShell, EState::Carried };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNInteractGuardRuleTest,
	"Tortunabo.Net.InteractGuard.Rule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNInteractGuardRuleTest::RunTest(const FString& Parameters)
{
	using namespace TNInteractStateGuardTest;
	FPlayWorld Play;
	ATortugaCharacter* Carrier = SpawnTurtle(Play.World, -1000.0);
	if (!TestNotNull(TEXT("Portadora"), Carrier)) { return false; }

	double X = 0.0;
	for (const EState State : AllStates)
	{
		ATortugaCharacter* Turtle = SpawnTurtle(Play.World, X);
		X += 400.0;
		if (!TestNotNull(TEXT("Tortuga"), Turtle) || !TestTrue(FString::Printf(TEXT("Estado %s aplicado"), StateName(State)), ApplyState(Turtle, Carrier, State)))
		{
			return false;
		}
		TestEqual(FString::Printf(TEXT("Aletas libres %s"), StateName(State)), Turtle->CanUseHandsForInteraction(), State == EState::None);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNInteractGuardPickupTest,
	"Tortunabo.Net.InteractGuard.ServerTryInteract",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNInteractGuardPickupTest::RunTest(const FString& Parameters)
{
	using namespace TNInteractStateGuardTest;
	FPlayWorld Play;
	ATortugaCharacter* Carrier = SpawnTurtle(Play.World, -1000.0);
	if (!TestNotNull(TEXT("Portadora"), Carrier)) { return false; }

	double X = 0.0;
	for (const EState State : AllStates)
	{
		ATortugaCharacter* Turtle = SpawnTurtle(Play.World, X);
		ATN_PickupInteractableBase* Pickup = SpawnPickup(Play.World, X, MakeBar(TEXT("Test_Floor")));
		X += 400.0;
		UTN_InventoryComponent* Inventory = Turtle ? Turtle->GetInventoryComponent() : nullptr;
		if (!TestNotNull(TEXT("Inventario"), Inventory) || !TestNotNull(TEXT("Pickup"), Pickup)
			|| !TestTrue(FString::Printf(TEXT("Estado %s aplicado"), StateName(State)), ApplyState(Turtle, Carrier, State)))
		{
			return false;
		}

		struct FParams { ATN_InteractableBase* Interactable; } Params{ Pickup };
		TestTrue(TEXT("ServerTryInteract existe"), CallRpc(Turtle, TEXT("ServerTryInteract"), &Params));
		const bool bShouldPick = State == EState::None;
		TestEqual(FString::Printf(TEXT("Coge el objeto %s"), StateName(State)), Inventory->HasEquippedItem(), bShouldPick);
		TestEqual(FString::Printf(TEXT("El pickup sigue en el suelo %s"), StateName(State)), Pickup->CanInteract(Carrier), !bShouldPick);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNInteractGuardRotateTest,
	"Tortunabo.Net.InteractGuard.ServerRotateItems",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNInteractGuardRotateTest::RunTest(const FString& Parameters)
{
	using namespace TNInteractStateGuardTest;
	FPlayWorld Play;
	ATortugaCharacter* Carrier = SpawnTurtle(Play.World, -1000.0);
	if (!TestNotNull(TEXT("Portadora"), Carrier)) { return false; }

	double X = 0.0;
	for (const EState State : AllStates)
	{
		ATortugaCharacter* Turtle = SpawnTurtle(Play.World, X);
		X += 400.0;
		UTN_InventoryComponent* Inventory = Turtle ? Turtle->GetInventoryComponent() : nullptr;
		if (!TestNotNull(TEXT("Inventario"), Inventory))
		{
			return false;
		}
		Inventory->TryAddItem(MakeBar(TEXT("Test_Hand")));
		Inventory->TryAddItem(MakeBar(TEXT("Test_Shell")));
		if (!TestTrue(FString::Printf(TEXT("Estado %s aplicado"), StateName(State)), ApplyState(Turtle, Carrier, State)))
		{
			return false;
		}

		TestTrue(TEXT("ServerRotateItems existe"), CallRpc(Inventory, TEXT("ServerRotateItems"), nullptr));
		const FName Expected = State == EState::None ? FName(TEXT("Test_Shell")) : FName(TEXT("Test_Hand"));
		TestEqual(FString::Printf(TEXT("En la mano tras el RPC %s"), StateName(State)), Inventory->GetEquippedItem().ItemId, Expected);

		// La ruta del anfitrión (RotateItems con autoridad) aplica la misma guarda: el control vuelve a rotar y los demás
		// siguen con el de la mano.
		Inventory->RotateItems();
		TestEqual(FString::Printf(TEXT("En la mano tras rotar en el anfitrión %s"), StateName(State)), Inventory->GetEquippedItem().ItemId,
			FName(TEXT("Test_Hand")));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
