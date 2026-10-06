// Rebote de lo lanzado contra una rampa (#708): lo lanzado se simula en pasos fijos en todas las máquinas y acaba en el mismo
// sitio con fotogramas de distinta duración.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Tct.Rampa; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/TN_TctItemRules.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TortugaCharacter.h"
#include "World/TN_TctProjectile.h"
#include "World/TN_ThrowableItemActor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTctBounceTest
{
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNTctBounceTestWorld"));
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

	/** Un suelo llano y una rampa de 25° delante (cajas del motor, con colisión). */
	void BuildRamp(UWorld* World)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube) { return; }
		auto Add = [&](const FVector& At, const FRotator& Rot, const FVector& Scale)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(At, Rot, Params);
			if (Actor && Actor->GetStaticMeshComponent())
			{
				Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
				Actor->GetStaticMeshComponent()->SetWorldScale3D(Scale);
				Actor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			}
		};
		Add(FVector(0.0, 0.0, -50.0), FRotator::ZeroRotator, FVector(200.0, 100.0, 1.0));
		Add(FVector(900.0, 0.0, 120.0), FRotator(25.0, 0.0, 0.0), FVector(8.0, 8.0, 0.5));
	}

	/** Lanza el balón hacia la rampa y lo deja volar Seconds a fotogramas de Dt; el sitio en que acaba. */
	FVector FlyBall(float Dt, float Seconds, bool& bOutLaunched)
	{
		FPlayWorld Play;
		BuildRamp(Play.World);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATortugaCharacter* Thrower = Play.World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator, Params);
		bOutLaunched = Thrower && ATN_TctProjectile::ServerLaunch(Thrower, static_cast<uint8>(ETNTctItem::BeachBall), FVector(1.0, 0.0, 0.25).GetSafeNormal());
		FVector Last = FVector::ZeroVector;
		for (TActorIterator<ATN_TctProjectile> It(Play.World); It; ++It) { Last = It->GetActorLocation(); }
		for (float Elapsed = 0.f; bOutLaunched && Elapsed < Seconds; Elapsed += Dt)
		{
			Play.World->Tick(LEVELTICK_All, Dt);
			for (TActorIterator<ATN_TctProjectile> It(Play.World); It; ++It) { Last = It->GetActorLocation(); }
		}
		return Last;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctRampBounceTest,
	"Tortunabo.Tct.Rampa.Rebote",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctRampBounceTest::RunTest(const FString& Parameters)
{
	using namespace TNTctBounceTest;

	// Lo lanzado se simula en pasos fijos en todas las máquinas (#708): sin ellos, el fotograma de cada una cambia el rebote.
	{
		FPlayWorld Play;
		ATN_ThrowableItemActor* Stone = Play.World->SpawnActor<ATN_ThrowableItemActor>(ATN_ThrowableItemActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		const UProjectileMovementComponent* Stone_PM = Stone ? Stone->FindComponentByClass<UProjectileMovementComponent>() : nullptr;
		if (TestNotNull(TEXT("Lanzable"), Stone_PM))
		{
			TestTrue(TEXT("Lanzable: pasos fijos"), Stone_PM->bForceSubStepping);
			TestTrue(TEXT("Lanzable: de 1/60 s como mucho"), Stone_PM->MaxSimulationTimeStep <= 1.f / 59.f);
		}
	}

	bool bLaunched = false;
	const FVector At30 = FlyBall(1.f / 30.f, 2.5f, bLaunched);
	if (!TestTrue(TEXT("El balón sale"), bLaunched))
	{
		return false;
	}
	bool bLaunched144 = false;
	const FVector At144 = FlyBall(1.f / 144.f, 2.5f, bLaunched144);
	TestTrue(TEXT("El balón sale (otra vez)"), bLaunched144);
	AddInfo(FString::Printf(TEXT("Balón contra la rampa a 30 Hz: (%.0f, %.0f, %.0f); a 144 Hz: (%.0f, %.0f, %.0f)"), At30.X, At30.Y, At30.Z, At144.X, At144.Y, At144.Z));
	TestTrue(TEXT("Con 30 o 144 fotogramas, acaba en el mismo sitio (±1,5 m)"), FVector::Dist(At30, At144) < 150.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
