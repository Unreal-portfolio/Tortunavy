// Punto de salida de la bola, la tinta y lo que se suelta (#571, ATortugaCharacter::GetItemSpawnLocation): pegada a un muro
// fino, el punto queda en su lado; sin obstáculo (o con otra tortuga delante) sigue siendo 120 cm delante y 40 cm arriba.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Items.SpawnClearance; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TortugaCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNItemSpawnClearanceTest
{
	/** Mundo de juego con BeginPlay ya hecho: los actores que se crean después lo reciben al aparecer. */
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNItemSpawnClearanceTestWorld"));
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

	/** Centro de la cápsula de la tortuga de prueba (mira hacia +X). */
	const FVector TurtleCenter(0.0, 0.0, 200.0);

	ATortugaCharacter* SpawnTurtle(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), Where, FRotator::ZeroRotator, Params);
	}

	/** Un muro (BlockAll, como la geometría del nivel) con su cara cercana en NearX y Thickness cm de grosor en X. */
	AActor* SpawnWall(UWorld* World, double NearX, double Thickness)
	{
		AActor* Wall = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall, TEXT("Wall"));
		Box->SetBoxExtent(FVector(Thickness * 0.5, 300.0, 300.0));
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Wall->SetRootComponent(Box);
		Box->RegisterComponent();
		Wall->SetActorLocation(FVector(NearX + Thickness * 0.5, 0.0, TurtleCenter.Z));
		return Wall;
	}

	FVector Unobstructed(const ATortugaCharacter* Turtle)
	{
		return Turtle->GetActorLocation() + Turtle->GetActorForwardVector() * 120.0 + FVector(0.0, 0.0, 40.0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNItemSpawnThinWallTest,
	"Tortunabo.Items.SpawnClearance.ThinWall",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNItemSpawnThinWallTest::RunTest(const FString& Parameters)
{
	using namespace TNItemSpawnClearanceTest;
	FPlayWorld Play;
	ATortugaCharacter* Turtle = SpawnTurtle(Play.World, TurtleCenter);
	if (!TestNotNull(TEXT("Tortuga"), Turtle))
	{
		return false;
	}

	// A 10 cm de un muro de 40 cm: sin el barrido el punto (120 cm delante) caía al otro lado.
	const double WallNearX = Turtle->GetActorLocation().X + Turtle->GetCapsuleComponent()->GetScaledCapsuleRadius() + 10.0;
	SpawnWall(Play.World, WallNearX, 40.0);
	const FVector Point = Turtle->GetItemSpawnLocation();
	TestTrue(FString::Printf(TEXT("Queda en su lado del muro (x=%.1f, cara del muro en x=%.1f)"), Point.X, WallNearX), Point.X < WallNearX);
	TestTrue(TEXT("No queda detrás de la tortuga"), Point.X >= Turtle->GetActorLocation().X - KINDA_SMALL_NUMBER);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNItemSpawnOpenTest,
	"Tortunabo.Items.SpawnClearance.Open",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNItemSpawnOpenTest::RunTest(const FString& Parameters)
{
	using namespace TNItemSpawnClearanceTest;
	FPlayWorld Play;
	ATortugaCharacter* Turtle = SpawnTurtle(Play.World, TurtleCenter);
	if (!TestNotNull(TEXT("Tortuga"), Turtle))
	{
		return false;
	}

	TestTrue(TEXT("Sin obstáculo: 120 cm delante y 40 cm arriba"), Turtle->GetItemSpawnLocation().Equals(Unobstructed(Turtle), 0.5));

	// Otra tortuga justo delante no recorta el punto (se le puede tirar la bola a bocajarro, como antes).
	ATortugaCharacter* Other = SpawnTurtle(Play.World, TurtleCenter + FVector(90.0, 0.0, 0.0));
	TestNotNull(TEXT("Otra tortuga"), Other);
	TestTrue(TEXT("Con otra tortuga delante, el mismo punto"), Turtle->GetItemSpawnLocation().Equals(Unobstructed(Turtle), 0.5));

	// Un muro más allá del punto tampoco lo cambia.
	SpawnWall(Play.World, Turtle->GetActorLocation().X + 200.0, 40.0);
	TestTrue(TEXT("Con un muro más lejos, el mismo punto"), Turtle->GetItemSpawnLocation().Equals(Unobstructed(Turtle), 0.5));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
