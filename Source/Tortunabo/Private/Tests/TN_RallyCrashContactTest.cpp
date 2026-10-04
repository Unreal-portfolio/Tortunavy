// Qué contactos del chasis cuentan como choque (UTN_BuggyHealthComponent::IsCrashContact). Un coco que da en el
// chasis quitaba 10 de munición y 35 más de choque (#296). Mundo de juego vacío, sin BeginPlay. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Combat.CrashContact; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Vehicles/TN_BuggyHealthComponent.h"
#include "Vehicles/TN_RallyProjectile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyCrashContactTest
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNRallyCrashContactTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCrashContactTest,
	"Tortunabo.Rally.Combat.CrashContact",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyCrashContactTest::RunTest(const FString& Parameters)
{
	TNRallyCrashContactTest::FScopedTestWorld Scoped;
	if (!TestNotNull(TEXT("Mundo de prueba"), Scoped.World))
	{
		return false;
	}
	UWorld* World = Scoped.World;
	AActor* Owner = World->SpawnActor<AStaticMeshActor>();
	AActor* Wall = World->SpawnActor<AStaticMeshActor>(FVector(1000.0, 0.0, 0.0), FRotator::ZeroRotator);
	ATN_RallyProjectile* Coco = World->SpawnActor<ATN_RallyProjectile>(FVector(500.0, 0.0, 0.0), FRotator::ZeroRotator);
	AActor* Thrown = World->SpawnActor<AStaticMeshActor>(FVector(0.0, 800.0, 0.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Dueño"), Owner) || !TestNotNull(TEXT("Pared"), Wall) || !TestNotNull(TEXT("Coco"), Coco)
		|| !TestNotNull(TEXT("Objeto lanzado"), Thrown))
	{
		return false;
	}
	UProjectileMovementComponent* ThrownMovement = NewObject<UProjectileMovementComponent>(Thrown);
	ThrownMovement->RegisterComponent();

	TestTrue(TEXT("una pared es un choque"), UTN_BuggyHealthComponent::IsCrashContact(Owner, Wall));
	TestFalse(TEXT("un proyectil de la torreta no es un choque (#296)"), UTN_BuggyHealthComponent::IsCrashContact(Owner, Coco));
	TestFalse(TEXT("algo con movimiento de proyectil no es un choque"), UTN_BuggyHealthComponent::IsCrashContact(Owner, Thrown));
	TestFalse(TEXT("el propio buggy no es un choque"), UTN_BuggyHealthComponent::IsCrashContact(Owner, Owner));
	TestFalse(TEXT("sin actor no hay choque"), UTN_BuggyHealthComponent::IsCrashContact(Owner, nullptr));
	return true;
}

#endif
