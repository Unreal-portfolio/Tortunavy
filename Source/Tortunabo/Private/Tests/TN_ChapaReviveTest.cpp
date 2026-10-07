// Revivir a una compañera muerta pagando chapas (#862): junto al cuerpo (ATN_RescuePickup), manteniendo la tecla, con 4 chapas
// (dato: UTN_EconomySettings::ReviveChapaCost) revive y el servidor las cobra; con 3 ni empieza ni cobra. Si no se puede revivir,
// no se cobra nada.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Economy.Revive; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Core/TN_CoopPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_EconomySettings.h"
#include "World/TN_RescuePickup.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNChapaReviveTest
{
	/** Mundo de juego con BeginPlay ya hecho y un GameState (el rescate busca ahí al muerto). */
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNChapaReviveTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->SetGameState(World->SpawnActor<AGameStateBase>());
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

	/** Una tortuga con Chapas chapas. */
	ATortugaCharacter* SpawnTurtleWith(UWorld* World, const FVector& Where, int32 Chapas)
	{
		ATortugaCharacter* Turtle = Spawn<ATortugaCharacter>(World, Where);
		if (UTN_InventoryComponent* Inventory = Turtle ? Turtle->GetInventoryComponent() : nullptr)
		{
			Inventory->AddChapas(Chapas);
		}
		return Turtle;
	}

	int32 ChapasOf(const ATortugaCharacter* Turtle)
	{
		const UTN_InventoryComponent* Inventory = Turtle ? Turtle->GetInventoryComponent() : nullptr;
		return Inventory ? Inventory->GetChapaCount() : -1;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNChapaReviveChargeTest,
	"Tortunabo.Economy.Revive.ChargeFourChapas",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNChapaReviveChargeTest::RunTest(const FString& Parameters)
{
	using namespace TNChapaReviveTest;
	FPlayWorld Play;
	const int32 Cost = UTN_EconomySettings::Get().ReviveChapaCost;
	TestEqual(TEXT("Revivir cuesta 4 chapas (hoja Economía)"), Cost, 4);

	ATortugaCharacter* Rich = SpawnTurtleWith(Play.World, FVector(0.0, 0.0, 200.0), 4);
	ATortugaCharacter* Poor = SpawnTurtleWith(Play.World, FVector(0.0, 300.0, 200.0), 3);
	if (!TestNotNull(TEXT("Tortuga con 4"), Rich) || !TestNotNull(TEXT("Tortuga con 3"), Poor))
	{
		return false;
	}

	// Con 4 revive y se cobran.
	bool bRevivedRich = false;
	TestTrue(TEXT("Con 4 chapas revive"), ATN_RescuePickup::ChargeAndRevive(Rich, Cost, [&]() { bRevivedRich = true; return true; }));
	TestTrue(TEXT("Se ha revivido"), bRevivedRich);
	TestEqual(TEXT("Se cobran las 4"), ChapasOf(Rich), 0);

	// Caso negativo: con 3 no revive ni se cobra nada.
	bool bRevivedPoor = false;
	TestFalse(TEXT("Con 3 chapas no revive"), ATN_RescuePickup::ChargeAndRevive(Poor, Cost, [&]() { bRevivedPoor = true; return true; }));
	TestFalse(TEXT("No se llega a revivir"), bRevivedPoor);
	TestEqual(TEXT("Conserva sus 3"), ChapasOf(Poor), 3);

	// Si no se ha podido revivir (el muerto ya no está), no se cobra.
	Poor->GetInventoryComponent()->AddChapas(1);
	TestFalse(TEXT("Revivir que falla"), ATN_RescuePickup::ChargeAndRevive(Poor, Cost, []() { return false; }));
	TestEqual(TEXT("No se cobra si no revive"), ChapasOf(Poor), 4);

	// Con coste 0 (dato) es el rescate gratis de siempre.
	TestTrue(TEXT("Gratis con coste 0"), ATN_RescuePickup::ChargeAndRevive(Poor, 0, []() { return true; }));
	TestEqual(TEXT("Sin cobrar"), ChapasOf(Poor), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNChapaReviveHoldTest,
	"Tortunabo.Economy.Revive.HoldNeedsChapas",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNChapaReviveHoldTest::RunTest(const FString& Parameters)
{
	using namespace TNChapaReviveTest;
	FPlayWorld Play;
	// Una compañera muerta (eliminada, en el GameState) y su rescate junto al cuerpo.
	ATN_CoopPlayerState* DeadState = Spawn<ATN_CoopPlayerState>(Play.World, FVector::ZeroVector);
	ATN_RescuePickup* Rescue = Spawn<ATN_RescuePickup>(Play.World, FVector(100.0, 0.0, 100.0));
	ATortugaCharacter* Rich = SpawnTurtleWith(Play.World, FVector(0.0, 0.0, 200.0), 4);
	ATortugaCharacter* Poor = SpawnTurtleWith(Play.World, FVector(0.0, 150.0, 200.0), 3);
	if (!TestNotNull(TEXT("Muerta"), DeadState) || !TestNotNull(TEXT("Rescate"), Rescue) || !TestNotNull(TEXT("Tortugas"), Poor))
	{
		return false;
	}
	DeadState->SetPlayerId(77);
	DeadState->bIsAlive = false;
	DeadState->bIsEliminated = true;
	if (!Play.World->GetGameState()->PlayerArray.Contains(DeadState))
	{
		Play.World->GetGameState()->AddPlayerState(DeadState);
	}
	Rescue->SetDeadPlayerId(77);

	TestEqual(TEXT("Hay que mantener la tecla"), Rescue->GetHoldDuration(), UTN_EconomySettings::Get().ReviveHoldSeconds);
	TestTrue(TEXT("Se puede intentar"), Rescue->CanInteract(Poor));

	// Caso negativo: con 3 no empieza.
	Rescue->BeginHoldInteract(Poor);
	TestTrue(TEXT("Con 3 no empieza"), Rescue->GetHoldProgress(Poor) < 0.f);
	// Con 4 empieza a contar.
	Rescue->BeginHoldInteract(Rich);
	TestTrue(TEXT("Con 4 empieza"), Rescue->GetHoldProgress(Rich) >= 0.f);
	Rescue->EndHoldInteract(Rich);
	TestTrue(TEXT("Soltar lo para"), Rescue->GetHoldProgress(Rich) < 0.f);
	TestEqual(TEXT("Empezar no cobra nada"), ChapasOf(Rich), 4);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
