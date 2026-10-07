// Arte de código en lugar de los marcadores del motor (#49, #50): la detección, el ajuste de tamaño y, en un mundo de
// juego de prueba, que cada Blueprint afectado nazca sin ninguna malla del
// motor a la vista y con arte propio.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Assets.PlaceholderArt; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/Package.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "World/TN_PlaceholderArt.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNPlaceholderArtTest
{
	/** Blueprints que el inventario marca con marcadores del motor (§2.1, §2.3 y §2.4). */
	const TCHAR* const Blueprints[] = {
		TEXT("/Game/Blueprints/Gameplay/Items/BP_JellyfishActor.BP_JellyfishActor_C"),
		TEXT("/Game/Blueprints/Gameplay/Items/BP_RescuePickUp.BP_RescuePickUp_C"),
		TEXT("/Game/Blueprints/Gameplay/Cosmetics/BP_HatStatue.BP_HatStatue_C"),
		TEXT("/Game/Blueprints/Gameplay/Cosmetics/BP_SkinStatue.BP_SkinStatue_C"),
	};

	/** Piezas visibles con malla del proyecto o de código (lo que sustituye al marcador). */
	int32 CountVisibleArt(const AActor* Actor)
	{
		int32 Count = 0;
		TInlineComponentArray<UMeshComponent*> Components(Actor);
		for (const UMeshComponent* Component : Components)
		{
			if (!Component->IsVisible() || Component->bHiddenInGame)
			{
				continue;
			}
			if (const UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(Component))
			{
				Count += (Static->GetStaticMesh() && !TNPlaceholderArt::IsPlaceholderMesh(Static->GetStaticMesh())) ? 1 : 0;
			}
			else if (const USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Component))
			{
				Count += Skeletal->GetSkeletalMeshAsset() ? 1 : 0;
			}
		}
		return Count;
	}

	/** Mundo de juego mínimo con BeginPlay hecho: los actores que nazcan en él ejecutan su BeginPlay. */
	UWorld* CreatePlayWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNPlaceholderArtTest"));
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		// Sin modo de juego nadie da la salida: la da la propia WorldSettings, como en una partida.
		if (AWorldSettings* Settings = World->GetWorldSettings())
		{
			Settings->NotifyBeginPlay();
		}
		return World;
	}

	void DestroyPlayWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPlaceholderArtDetectTest,
	"Tortunabo.Assets.PlaceholderArt.Detect",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPlaceholderArtDetectTest::RunTest(const FString& Parameters)
{
	const UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	const UStaticMesh* Help = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/EditorMeshes/EditorHelp.EditorHelp"));
	const UStaticMesh* Conch = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Blueprints/Characters/Meshes/ConchaCerrada.ConchaCerrada"));
	TestTrue(TEXT("El cubo del motor es un marcador"), TNPlaceholderArt::IsPlaceholderMesh(Cube));
	TestTrue(TEXT("La malla de ayuda del editor es un marcador"), TNPlaceholderArt::IsPlaceholderMesh(Help));
	TestNotNull(TEXT("La concha del proyecto existe"), Conch);
	TestFalse(TEXT("Una malla del proyecto no es un marcador"), TNPlaceholderArt::IsPlaceholderMesh(Conch));
	TestFalse(TEXT("Sin malla no hay marcador"), TNPlaceholderArt::IsPlaceholderMesh(nullptr));
	const UStaticMesh* CodeMesh = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Transient);
	TestFalse(TEXT("Una malla de código (paquete transitorio, /Engine/Transient) no es un marcador"), TNPlaceholderArt::IsPlaceholderMesh(CodeMesh));
	TestTrue(TEXT("Sin componente se pone arte de código"), TNPlaceholderArt::NeedsCodeArt(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPlaceholderArtFitTest,
	"Tortunabo.Assets.PlaceholderArt.FitScale",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPlaceholderArtFitTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Planta: 100 cm a 200 cm es x2"), TNPlaceholderArt::FitScale(FVector(100.0, 50.0, 10.0), FVector(200.0, 200.0, 1000.0)), 2.f);
	TestEqual(TEXT("La altura limita"), TNPlaceholderArt::FitScale(FVector(100.0, 100.0, 100.0), FVector(400.0, 400.0, 150.0)), 1.5f);
	TestEqual(TEXT("Sin altura de destino solo cuenta la planta"), TNPlaceholderArt::FitScale(FVector(50.0, 80.0, 30.0), FVector(160.0, 40.0, 0.0)), 2.f);
	TestEqual(TEXT("Medidas nulas: 1"), TNPlaceholderArt::FitScale(FVector::ZeroVector, FVector(100.0)), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPlaceholderArtBlueprintsTest,
	"Tortunabo.Assets.PlaceholderArt.Blueprints",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPlaceholderArtBlueprintsTest::RunTest(const FString& Parameters)
{
	using namespace TNPlaceholderArtTest;
	UWorld* World = CreatePlayWorld();
	if (!TestNotNull(TEXT("Mundo de prueba"), World))
	{
		return false;
	}
	double OffsetX = 0.0;
	for (const TCHAR* Path : Blueprints)
	{
		UClass* Class = LoadClass<AActor>(nullptr, Path);
		if (!TestNotNull(FString::Printf(TEXT("Se carga %s"), Path), Class))
		{
			continue;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(Class, FTransform(FVector(OffsetX, 0.0, 0.0)), Params);
		OffsetX += 3000.0;
		if (!TestNotNull(FString::Printf(TEXT("Nace %s"), Path), Actor))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s ha hecho su BeginPlay"), *Class->GetName()), Actor->HasActorBegunPlay());
		const int32 Placeholders = TNPlaceholderArt::CountVisiblePlaceholders(Actor);
		const int32 Art = CountVisibleArt(Actor);
		AddInfo(FString::Printf(TEXT("%s: marcadores visibles=%d, piezas de arte=%d"), *Class->GetName(), Placeholders, Art));
		TestEqual(FString::Printf(TEXT("%s sin mallas del motor a la vista"), *Class->GetName()), Placeholders, 0);
		TestTrue(FString::Printf(TEXT("%s con arte propio a la vista"), *Class->GetName()), Art > 0);
		Actor->Destroy();
	}
	DestroyPlayWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
