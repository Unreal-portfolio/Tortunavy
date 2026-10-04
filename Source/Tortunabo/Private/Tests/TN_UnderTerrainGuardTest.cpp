// Red de seguridad bajo el terreno de Coop y Clásico (#633, TN_UnderTerrainGuard.h): la regla de las miradas que comparte con
// la carrera de la playa y, con un mundo de juego, una tortuga de verdad hundida bajo una losa que hace de terreno: vuelve a
// la superficie sin morir; no se la toca encima del terreno, en una cueva (suelo debajo), nadando, en una zona de muerte ni
// cayendo al vacío sin nada encima. Además, que la llevan el Coop y el Clásico y que la playa la apaga (tiene la suya).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.SafetyNet.UnderTerrain; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/TN_BeachRaceGameMode.h"
#include "Game/TN_ProcMapGameMode.h"
#include "Game/TN_RunGameMode.h"
#include "Game/TN_UnderTerrainGuard.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TortugaCharacter.h"
#include "World/TN_DeathZoneVolume.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNUnderTerrainGuardTest
{
	/** Altura (cm) de la cara de arriba de la losa que hace de terreno. */
	constexpr double SurfaceZ = 50.0;

	/** Mundo de juego con BeginPlay ya hecho: los actores que se crean después lo reciben al aparecer. */
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNUnderTerrainGuardTestWorld"));
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

	/** Una losa con colisión que para a las tortugas (BlockAll), con su centro en Center. */
	AActor* SpawnSlab(UWorld* World, const FVector& Center, const FVector& HalfExtent)
	{
		AActor* Slab = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Slab, TEXT("Slab"));
		Box->SetBoxExtent(HalfExtent);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Slab->SetRootComponent(Box);
		Box->RegisterComponent();
		Slab->SetActorLocation(Center);
		return Slab;
	}

	/** Una tortuga con los pies (lo más bajo de la cápsula) en FeetZ, en (X, Y), con el modo de movimiento Mode. */
	ATortugaCharacter* SpawnTurtle(UWorld* World, double X, double Y, double FeetZ, EMovementMode Mode)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATortugaCharacter* Turtle = World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), FVector(X, Y, 0.0), FRotator::ZeroRotator, Params);
		if (!Turtle)
		{
			return nullptr;
		}
		const double HalfHeight = Turtle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Turtle->SetActorLocation(FVector(X, Y, FeetZ + HalfHeight), false, nullptr, ETeleportType::TeleportPhysics);
		Turtle->GetCharacterMovement()->SetMovementMode(Mode);
		return Turtle;
	}

	/** Tantas miradas como Looks, separadas 0,1 s; true si alguna la ha recolocado. */
	bool Look(UTN_UnderTerrainGuardComponent* Guard, ATortugaCharacter* Turtle, int32 Looks, float& InOutNow)
	{
		bool bRescued = false;
		for (int32 Index = 0; Index < Looks; ++Index)
		{
			InOutNow += TNUnderTerrain::WatchInterval;
			bRescued |= Guard->WatchTurtle(Turtle, InOutNow);
		}
		return bRescued;
	}

	const UTN_UnderTerrainGuardComponent* GuardOf(const UClass* GameModeClass)
	{
		UObject* Defaults = GameModeClass->GetDefaultObject();
		return Cast<UTN_UnderTerrainGuardComponent>(Defaults->GetDefaultSubobjectByName(TEXT("UnderTerrainGuard")));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNUnderTerrainRulesTest,
	"Tortunabo.SafetyNet.UnderTerrain.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNUnderTerrainRulesTest::RunTest(const FString& Parameters)
{
	using namespace TNUnderTerrain;
	constexpr double Margin = 160.0;

	TestEqual(TEXT("Se mira 10 veces por segundo"), WatchInterval, 0.1f);

	int32 Strikes = 0;
	TestFalse(TEXT("Encima del terreno: nada"), RegisterLook(Strikes, -50.0, Margin));
	TestFalse(TEXT("Dentro de la holgura: nada"), RegisterLook(Strikes, Margin, Margin));
	TestEqual(TEXT("Dentro de la holgura no cuenta"), Strikes, 0);
	TestFalse(TEXT("Una mirada por debajo: aún no (un fotograma de la física no cuenta)"), RegisterLook(Strikes, 250.0, Margin));
	TestTrue(TEXT("Dos seguidas: rescate"), RegisterLook(Strikes, 250.0, Margin));

	Strikes = 0;
	RegisterLook(Strikes, 250.0, Margin);
	RegisterLook(Strikes, 0.0, Margin);
	TestFalse(TEXT("Una por debajo, otra encima y otra por debajo: aún no"), RegisterLook(Strikes, 250.0, Margin));

	Strikes = 0;
	TestTrue(TEXT("Muy honda: rescate a la primera"), RegisterLook(Strikes, DeepDepth + 1.0, Margin));

	// Quién la lleva: el Coop y el Clásico, encendida; la playa, apagada (tiene la suya).
	const UTN_UnderTerrainGuardComponent* RunGuard = TNUnderTerrainGuardTest::GuardOf(ATN_RunGameMode::StaticClass());
	const UTN_UnderTerrainGuardComponent* ProcGuard = TNUnderTerrainGuardTest::GuardOf(ATN_ProcMapGameMode::StaticClass());
	const UTN_UnderTerrainGuardComponent* BeachGuard = TNUnderTerrainGuardTest::GuardOf(ATN_BeachRaceGameMode::StaticClass());
	TestTrue(TEXT("Clásico (ATN_RunGameMode): con red de seguridad encendida"), RunGuard && RunGuard->IsGuardEnabled());
	TestTrue(TEXT("Coop (ATN_ProcMapGameMode): con red de seguridad encendida"), ProcGuard && ProcGuard->IsGuardEnabled());
	TestTrue(TEXT("Playa: la red común, apagada (usa GuardUnderSand)"), BeachGuard && !BeachGuard->IsGuardEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNUnderTerrainWorldTest,
	"Tortunabo.SafetyNet.UnderTerrain.World",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNUnderTerrainWorldTest::RunTest(const FString& Parameters)
{
	using namespace TNUnderTerrainGuardTest;
	FPlayWorld Play;
	UWorld* World = Play.World;

	// El terreno: un bloque macizo de 60 × 80 × 20 m (X de -20 a 40 m) con la cara de arriba a SurfaceZ. Al lado, una cueva: un
	// techo de 1 m de grueso a la misma altura y su suelo 10 m más abajo. Más allá, un puente de 50 cm sobre una sima sin fondo y
	// una meseta maciza de 10 m con el fondo del río 5 m por debajo de ella.
	SpawnSlab(World, FVector(1000.0, 0.0, SurfaceZ - 1000.0), FVector(3000.0, 4000.0, 1000.0));
	SpawnSlab(World, FVector(-3000.0, 0.0, SurfaceZ - 50.0), FVector(800.0, 800.0, 50.0));
	SpawnSlab(World, FVector(-3000.0, 0.0, SurfaceZ - 1050.0), FVector(800.0, 800.0, 50.0));
	SpawnSlab(World, FVector(-6000.0, 0.0, SurfaceZ - 25.0), FVector(300.0, 800.0, 25.0));
	SpawnSlab(World, FVector(-10000.0, 0.0, SurfaceZ - 500.0), FVector(800.0, 800.0, 500.0));
	SpawnSlab(World, FVector(-10000.0, 0.0, SurfaceZ - 1550.0), FVector(2000.0, 2000.0, 50.0));

	AActor* Host = World->SpawnActor<AActor>();
	UTN_UnderTerrainGuardComponent* Guard = NewObject<UTN_UnderTerrainGuardComponent>(Host, TEXT("Guard"));
	Guard->RegisterComponent();
	float Now = World->GetTimeSeconds();

	// 1) Hundida 2,5 m bajo el terreno, cayendo: a la segunda mirada vuelve encima, de pie y viva.
	ATortugaCharacter* Sunk = SpawnTurtle(World, 0.0, 0.0, SurfaceZ - 250.0, MOVE_Falling);
	if (!TestNotNull(TEXT("Se crea la tortuga"), Sunk))
	{
		return false;
	}
	const double HalfHeight = Sunk->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	TestFalse(TEXT("Hundida 2,5 m: a la primera mirada aún no"), Look(Guard, Sunk, 1, Now));
	TestTrue(TEXT("Hundida 2,5 m: a la segunda, rescatada"), Look(Guard, Sunk, 1, Now));
	const FVector After = Sunk->GetActorLocation();
	TestTrue(*FString::Printf(TEXT("Vuelve de pie sobre la superficie (pies a %.1f cm; el terreno, a %.1f)"), After.Z - HalfHeight, SurfaceZ),
		After.Z - HalfHeight >= SurfaceZ && After.Z - HalfHeight <= SurfaceZ + 20.0);
	TestTrue(TEXT("En la misma vertical (la superficie más cercana)"), FVector::Dist2D(After, FVector::ZeroVector) < 1.0);
	TestFalse(TEXT("No muere"), Sunk->IsDead());
	TestFalse(TEXT("Ya encima: no se la vuelve a mover"), Look(Guard, Sunk, 3, Now));

	// 2) Muy honda (5 m): a la primera mirada.
	ATortugaCharacter* Deep = SpawnTurtle(World, 1000.0, 0.0, SurfaceZ - 500.0, MOVE_Falling);
	TestTrue(TEXT("Hundida 5 m: rescatada a la primera mirada"), Deep && Look(Guard, Deep, 1, Now));
	TestTrue(TEXT("Hundida 5 m: vuelve encima"), Deep && Deep->GetActorLocation().Z - HalfHeight >= SurfaceZ);

	// 3) De pie encima del terreno: nada.
	ATortugaCharacter* OnTop = SpawnTurtle(World, 2000.0, 0.0, SurfaceZ + 2.0, MOVE_Walking);
	TestFalse(TEXT("Encima del terreno: no se la toca"), OnTop && Look(Guard, OnTop, 3, Now));

	// 4) En una cueva (terreno encima y suelo debajo): nada.
	ATortugaCharacter* InCave = SpawnTurtle(World, -3000.0, 0.0, SurfaceZ - 1000.0 + 2.0, MOVE_Walking);
	TestFalse(TEXT("En una cueva, con suelo debajo: no se la toca"), InCave && Look(Guard, InCave, 3, Now));

	// 4b) Cayendo bajo un puente hacia una sima sin suelo en 50 m: es una caída al aire libre, no se la sube al puente.
	ATortugaCharacter* UnderBridge = SpawnTurtle(World, -6000.0, 0.0, SurfaceZ - 400.0, MOVE_Falling);
	TestFalse(TEXT("Bajo un puente, cayendo a una sima: no se la toca"), UnderBridge && Look(Guard, UnderBridge, 3, Now));

	// 4c) Hundida 3 m en una meseta con el fondo del río debajo: está dentro de la meseta, se la rescata.
	ATortugaCharacter* InMesa = SpawnTurtle(World, -10000.0, 0.0, SurfaceZ - 300.0, MOVE_Falling);
	TestTrue(TEXT("Hundida en una meseta con suelo debajo: rescatada"), InMesa && Look(Guard, InMesa, 2, Now));
	TestTrue(TEXT("Hundida en una meseta: vuelve encima"), InMesa && InMesa->GetActorLocation().Z - HalfHeight >= SurfaceZ);

	// 5) Nadando bajo el terreno (agua profunda): nada.
	ATortugaCharacter* Swimming = SpawnTurtle(World, 0.0, 1500.0, SurfaceZ - 250.0, MOVE_Swimming);
	TestFalse(TEXT("Nadando: no se la toca"), Swimming && Look(Guard, Swimming, 3, Now));

	// 6) Dentro de una zona de muerte: muere como debe, no se la salva.
	ATortugaCharacter* InDeathZone = SpawnTurtle(World, 0.0, -1500.0, SurfaceZ - 250.0, MOVE_Falling);
	ATN_DeathZoneVolume* Zone = World->SpawnActor<ATN_DeathZoneVolume>(ATN_DeathZoneVolume::StaticClass(), FVector(0.0, -1500.0, SurfaceZ - 250.0), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("Se crea la zona de muerte"), Zone))
	{
		Zone->ConfigureZone(FVector(400.0, 400.0, 400.0), 3.f);
	}
	TestFalse(TEXT("En una zona de muerte: no se la salva"), InDeathZone && Look(Guard, InDeathZone, 3, Now));

	// 7) Cayendo al vacío sin nada encima (fuera del mapa): es una caída, no se la toca.
	ATortugaCharacter* Void = SpawnTurtle(World, 10000.0, 0.0, -5000.0, MOVE_Falling);
	TestFalse(TEXT("Cayendo al vacío sin terreno encima: no se la toca"), Void && Look(Guard, Void, 3, Now));

	// 8) Con la red apagada: nada.
	ATortugaCharacter* Disabled = SpawnTurtle(World, 0.0, 3000.0, SurfaceZ - 500.0, MOVE_Falling);
	Guard->SetGuardEnabled(false);
	TestFalse(TEXT("Con la red apagada: no se la toca"), Disabled && Look(Guard, Disabled, 3, Now));
	return true;
}

#endif
