// La chapa (#858): se apila en un contador aparte de los dos huecos del inventario, se lanza como un frisbee (disco de canto
// que gira sobre su eje, arco parabólico predecible) y, al caer, la recoge otra tortuga (quien la lanzó, pasado un momento).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Economy.Chapa; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/TN_ChapaRules.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TN_ThrowArc.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_EconomySettings.h"
#include "World/TN_Chapa.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNChapaTest
{
	/** Mundo de juego con BeginPlay ya hecho: los actores que se crean después lo reciben al aparecer. */
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNChapaTestWorld"));
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

	ATortugaCharacter* SpawnTurtle(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), Where, FRotator::ZeroRotator, Params);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNChapaRulesTest,
	"Tortunabo.Economy.Chapa.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNChapaRulesTest::RunTest(const FString& Parameters)
{
	using namespace TNChapaRules;
	TestEqual(TEXT("Caben todas con sitio"), AcceptedChapas(3, 4, 99), 4);
	TestEqual(TEXT("Solo hasta el tope"), AcceptedChapas(97, 4, 99), 2);
	TestEqual(TEXT("Lleno: ninguna"), AcceptedChapas(99, 1, 99), 0);
	TestEqual(TEXT("Cantidad negativa: ninguna"), AcceptedChapas(3, -2, 99), 0);
	TestTrue(TEXT("Con 4 se pagan 4"), CanSpend(4, 4));
	TestFalse(TEXT("Con 3 no se pagan 4"), CanSpend(3, 4));
	TestFalse(TEXT("Un coste negativo no se paga"), CanSpend(3, -1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNChapaStackTest,
	"Tortunabo.Economy.Chapa.StacksWithoutSlots",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNChapaStackTest::RunTest(const FString& Parameters)
{
	using namespace TNChapaTest;
	FPlayWorld Play;
	ATortugaCharacter* Turtle = SpawnTurtle(Play.World, FVector(0.0, 0.0, 200.0));
	UTN_InventoryComponent* Inventory = Turtle ? Turtle->GetInventoryComponent() : nullptr;
	if (!TestNotNull(TEXT("Inventario"), Inventory))
	{
		return false;
	}
	TestEqual(TEXT("Se apilan 3"), Inventory->AddChapas(3), 3);
	TestEqual(TEXT("Se apilan 2 más"), Inventory->AddChapas(2), 2);
	TestEqual(TEXT("Lleva 5"), Inventory->GetChapaCount(), 5);
	TestFalse(TEXT("No ocupan la mano"), Inventory->HasEquippedItem());
	TestFalse(TEXT("No ocupan el caparazón"), Inventory->HasStoredItem());
	TestEqual(TEXT("No pesan"), Inventory->GetTotalCarriedWeight(), 0.f);

	// Caso negativo: no llegan, no se gasta ninguna.
	TestFalse(TEXT("No se pagan 6 con 5"), Inventory->TrySpendChapas(6));
	TestEqual(TEXT("Siguen 5"), Inventory->GetChapaCount(), 5);
	TestTrue(TEXT("Se pagan 4"), Inventory->TrySpendChapas(4));
	TestEqual(TEXT("Queda 1"), Inventory->GetChapaCount(), 1);

	const int32 Max = UTN_EconomySettings::Get().MaxChapas;
	TestEqual(TEXT("Hasta el tope"), Inventory->AddChapas(Max + 10), Max - 1);
	TestEqual(TEXT("Lleno"), Inventory->GetChapaCount(), Max);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNChapaFrisbeeTest,
	"Tortunabo.Economy.Chapa.FrisbeeArc",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNChapaFrisbeeTest::RunTest(const FString& Parameters)
{
	using namespace TNChapaRules;
	// De canto: el eje del disco (Z de la malla) queda horizontal y de lado respecto al rumbo, gire lo que gire.
	const FVector Velocity(800.0, 600.0, 300.0);
	const FVector Heading = FVector(Velocity.X, Velocity.Y, 0.0).GetSafeNormal();
	for (const double Spin : { 0.0, 37.0, 90.0, 215.0 })
	{
		const FVector Axle = DiscRotation(Velocity, Spin).GetAxisZ();
		TestTrue(FString::Printf(TEXT("Eje horizontal con giro %.0f"), Spin), FMath::Abs(Axle.Z) < 1e-6);
		TestTrue(FString::Printf(TEXT("Eje de lado con giro %.0f"), Spin), FMath::Abs(FVector::DotProduct(Axle, Heading)) < 1e-6);
	}
	// Gira sobre su eje: la cara del disco cambia con el giro y el eje no.
	TestFalse(TEXT("El disco gira"), DiscRotation(Velocity, 0.0).GetAxisX().Equals(DiscRotation(Velocity, 90.0).GetAxisX(), 1e-3));

	// Arco predecible: parábola con la gravedad de la chapa (altura máxima v²/2g y simétrica).
	const FVector Origin(0.0, 0.0, 100.0);
	const FVector Launch(1000.0, 0.0, 500.0);
	constexpr double G = 500.0;
	const double Apex = 500.0 / G;
	TestEqual(TEXT("Altura máxima"), FlightPoint(Origin, Launch, G, Apex).Z, 100.0 + 500.0 * 500.0 / (2.0 * G), 0.01);
	TestEqual(TEXT("Vuelve a la altura de salida"), FlightPoint(Origin, Launch, G, 2.0 * Apex).Z, 100.0, 0.01);
	TestEqual(TEXT("Sin velocidad vertical en lo alto"), FlightVelocity(Launch, G, Apex).Z, 0.0, 0.01);

	// Con el ángulo del lanzamiento al punto de mira (el mismo cálculo que ATortugaCharacter), el arco pasa por el punto.
	constexpr double Speed = 1300.0;
	const double Dist = 900.0;
	const double DeltaZ = 40.0;
	const double Theta = TNThrowArc::LaunchPitch(Dist, DeltaZ, Speed, G, 0.0);
	const FVector Dir(FMath::Cos(Theta), 0.0, FMath::Sin(Theta));
	const double T = Dist / (Speed * Dir.X);
	TestEqual(TEXT("Llega al punto de mira"), FlightPoint(FVector::ZeroVector, Dir * Speed, G, T).Z, DeltaZ, 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNChapaThrowCollectTest,
	"Tortunabo.Economy.Chapa.ThrowAndCollect",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNChapaThrowCollectTest::RunTest(const FString& Parameters)
{
	using namespace TNChapaTest;
	FPlayWorld Play;
	ATortugaCharacter* Thrower = SpawnTurtle(Play.World, FVector(0.0, 0.0, 200.0));
	ATortugaCharacter* Other = SpawnTurtle(Play.World, FVector(0.0, 500.0, 200.0));
	UTN_InventoryComponent* MyInventory = Thrower ? Thrower->GetInventoryComponent() : nullptr;
	UTN_InventoryComponent* OtherInventory = Other ? Other->GetInventoryComponent() : nullptr;
	if (!TestNotNull(TEXT("Inventario"), MyInventory) || !TestNotNull(TEXT("Otro inventario"), OtherInventory))
	{
		return false;
	}

	// Caso negativo: sin chapas no se lanza nada.
	TestNull(TEXT("Sin chapas no lanza"), ATN_Chapa::ServerThrowFrom(Thrower));

	MyInventory->AddChapas(2);
	ATN_Chapa* Chapa = ATN_Chapa::ServerThrowFrom(Thrower);
	if (!TestNotNull(TEXT("Lanza una chapa"), Chapa))
	{
		return false;
	}
	TestEqual(TEXT("Le queda 1"), MyInventory->GetChapaCount(), 1);
	TestEqual(TEXT("Sale a su velocidad"), Chapa->GetFlight().Velocity.Size(), static_cast<double>(Chapa->GetThrowSpeed()), 1.0);
	TestEqual(TEXT("Con la gravedad de la chapa"), Chapa->GetFlight().Gravity, Chapa->GetFlightGravity());
	TestTrue(TEXT("Quien la lanzó"), Chapa->GetThrower() == Thrower);

	// Sin suelo en el mundo de prueba: acaba el vuelo y se queda donde está.
	Chapa->ServerStepFlight(0.0, 100.0);
	TestTrue(TEXT("Cae y se queda en el suelo"), Chapa->IsResting());

	// Quien la lanzó espera un momento; otra tortuga la recoge al momento.
	TestFalse(TEXT("Quien la lanzó no la recoge enseguida"), Chapa->ServerTryCollect(Thrower));
	TestTrue(TEXT("Otra tortuga la recoge"), Chapa->ServerTryCollect(Other));
	TestEqual(TEXT("La otra lleva 1"), OtherInventory->GetChapaCount(), 1);
	TestFalse(TEXT("No se recoge dos veces"), Chapa->ServerTryCollect(Other));
	TestEqual(TEXT("Sigue con 1"), OtherInventory->GetChapaCount(), 1);

	// Las que sueltan las cajas y el airdrop: N chapas en vuelo, sin dueño.
	TestEqual(TEXT("Suelta 3 chapas"), ATN_Chapa::SpawnChapas(Play.World, FVector(300.0, 0.0, 100.0), 3), 3);
	TestEqual(TEXT("Ninguna con 0"), ATN_Chapa::SpawnChapas(Play.World, FVector(300.0, 0.0, 100.0), 0), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
