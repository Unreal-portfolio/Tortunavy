// Colisión compleja de las mallas generadas del lobby (issue #344). Las piezas del lobby construyen sus UStaticMesh en
// ejecución (TNProcRuntimeMesh::MakeStaticMesh). Para que Chaos pueda cocinar su malla de triángulos, tanto en el cook
// como en la build empaquetada, cada malla necesita los datos en CPU (bAllowCPUAccess y el índice accesible) y su
// BodySetup. Sin ellos salen los avisos «GetPhysicsTriMeshData: CPU data not available» y «UBodySetup::GetCookInfo».
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Lobby.GeneratedMeshCollision; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Lobby/Playground/TN_JellyfishTrampoline.h"
#include "Lobby/Playground/TN_PlaygroundPiece.h"
#include "Lobby/Playground/TN_WobblyBridge.h"
#include "Lobby/TN_ChangingBooth.h"
#include "Lobby/TN_SandCastleLobby.h"
#include "Lobby/TN_ShopKeeper.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshResources.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNLobbyGeneratedMeshTestDetail
{
	/** Mundo de juego vacío: SpawnActor ejecuta OnConstruction, que es donde las piezas generan sus mallas. */
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNLobbyGeneratedMeshTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
		}

		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	/** Mallas generadas en ejecución (transitorias, no assets) de los componentes de un actor, sin repetir. */
	TArray<UStaticMesh*> GeneratedMeshesOf(const AActor* Actor)
	{
		TArray<UStaticMesh*> Out;
		TInlineComponentArray<UStaticMeshComponent*> Comps(Actor);
		for (const UStaticMeshComponent* Comp : Comps)
		{
			UStaticMesh* Mesh = Comp ? Comp->GetStaticMesh() : nullptr;
			if (Mesh && Mesh->HasAnyFlags(RF_Transient) && !Mesh->IsAsset())
			{
				Out.AddUnique(Mesh);
			}
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNLobbyGeneratedMeshCollisionTest,
	"Tortunabo.Lobby.GeneratedMeshCollision",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNLobbyGeneratedMeshCollisionTest::RunTest(const FString& Parameters)
{
	using namespace TNLobbyGeneratedMeshTestDetail;
	FTestWorld TestWorld;
	if (!TestNotNull(TEXT("Mundo de prueba"), TestWorld.World)) { return false; }

	const TArray<UClass*> Classes = {
		ATN_JellyfishTrampoline::StaticClass(),
		ATN_SandCastleLobby::StaticClass(),
		ATN_PlaygroundPiece::StaticClass(),
		ATN_ChangingBooth::StaticClass(),
		ATN_WobblyBridge::StaticClass(),
		ATN_ShopKeeper::StaticClass(),
	};

	for (UClass* Class : Classes)
	{
		const FString ClassName = Class->GetName();
		AActor* Actor = TestWorld.World->SpawnActor<AActor>(Class, FTransform::Identity);
		if (!TestNotNull(*FString::Printf(TEXT("%s: se crea"), *ClassName), Actor)) { continue; }

		const TArray<UStaticMesh*> Meshes = GeneratedMeshesOf(Actor);
		TestTrue(*FString::Printf(TEXT("%s: genera alguna malla en su construcción"), *ClassName), Meshes.Num() > 0);
		for (const UStaticMesh* Mesh : Meshes)
		{
			const FString What = FString::Printf(TEXT("%s/%s"), *ClassName, *Mesh->GetName());
			TestTrue(*FString::Printf(TEXT("%s: bAllowCPUAccess activado (colisión en la build empaquetada)"), *What),
				static_cast<bool>(Mesh->bAllowCPUAccess));
			TestNotNull(*FString::Printf(TEXT("%s: tiene BodySetup"), *What), Mesh->GetBodySetup());
			const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
			const bool bIndexOnCpu = RenderData && RenderData->LODResources.Num() > 0
				&& RenderData->LODResources[0].IndexBuffer.GetAllowCPUAccess();
			TestTrue(*FString::Printf(TEXT("%s: índices del LOD 0 accesibles en CPU (cook de la malla de triángulos)"), *What), bIndexOnCpu);
		}
		Actor->Destroy();
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
