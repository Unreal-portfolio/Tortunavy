// Instancias que se mueven cada fotograma (#566): las bandadas y los emisores de TNAmbientFX actualizan sus
// transformadas sin MarkRenderStateDirty. Con
// él, cada fotograma se destruía y se volvía a crear el proxy de render del ISM (149 por fotograma en el pico de
// TN.Stress caos). Se comprueba que el estado de render no queda sucio, que al final del fotograma el proxy es el
// mismo y que los límites del ISM incluyen las posiciones nuevas (no se recortan por frustum culling).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Perf.IsmPerFrame; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "World/ProcMap/TN_ProcMapAmbientFX.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNIsmPerFrameTestDetail
{
	/** Mundo de juego vacío (el mismo arnés que TN_ArtTest): tiene escena de render, así que los ISM crean su proxy. */
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNIsmPerFrameTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
		}

		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	/** ISM móvil como raíz de un actor nuevo, con Count instancias en el origen. */
	UInstancedStaticMeshComponent* MakeIsm(UWorld* World, UStaticMesh* Mesh, int32 Count)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		if (!Actor)
		{
			return nullptr;
		}
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(Actor, TEXT("Instances"));
		ISM->SetMobility(EComponentMobility::Movable);
		Actor->SetRootComponent(ISM);
		ISM->RegisterComponent();
		ISM->SetStaticMesh(Mesh);
		for (int32 i = 0; i < Count; ++i)
		{
			ISM->AddInstance(FTransform::Identity, true);
		}
		// Lo que ha marcado la creación (malla nueva, instancias) se aplica ya: a partir de aquí el proxy no debe cambiar.
		World->SendAllEndOfFrameUpdates();
		return ISM;
	}

	/**
	 * Llama a Move (una actualización de un fotograma) y comprueba que no marca el estado de render, que el proxy sigue
	 * siendo el mismo tras el final del fotograma y que los límites contienen Expected.
	 */
	void CheckMoveKeepsProxy(FAutomationTestBase& Test, const TCHAR* What, UWorld* World, UInstancedStaticMeshComponent* ISM,
		TFunctionRef<void()> Move, TFunctionRef<FVector()> Expected)
	{
		if (!ISM->IsRenderStateCreated())
		{
			Test.AddWarning(FString::Printf(TEXT("%s: sin estado de render en este mundo; solo se comprueba que no se marca"), What));
		}
		Test.TestFalse(FString::Printf(TEXT("%s: limpio antes de moverse"), What), ISM->IsRenderStateDirty());
		const FPrimitiveSceneProxy* ProxyBefore = ISM->SceneProxy;

		Move();
		Test.TestFalse(FString::Printf(TEXT("%s: mover las instancias no marca el estado de render (no rehace el proxy)"), What),
			ISM->IsRenderStateDirty());

		World->SendAllEndOfFrameUpdates();
		Test.TestTrue(FString::Printf(TEXT("%s: el proxy es el mismo tras el final del fotograma"), What), ISM->SceneProxy == ProxyBefore);
		if (ISM->SceneProxy)
		{
			const FVector Where = Expected();
			Test.TestTrue(FString::Printf(TEXT("%s: los límites incluyen la posición nueva (%s)"), What, *Where.ToCompactString()),
				ISM->Bounds.GetBox().ExpandBy(1.0).IsInsideOrOn(Where));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNIsmPerFrameTest,
	"Tortunabo.Perf.IsmPerFrame",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNIsmPerFrameTest::RunTest(const FString& Parameters)
{
	using namespace TNIsmPerFrameTestDetail;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Malla del motor"), Cube))
	{
		return false;
	}
	FTestWorld TestWorld;
	UWorld* World = TestWorld.World;

	// ── Bandada de TNAmbientFX: vuela lejos del origen, donde estaban las instancias ──
	{
		constexpr int32 Birds = 4;
		UInstancedStaticMeshComponent* ISM = MakeIsm(World, Cube, Birds);
		if (!TestNotNull(TEXT("ISM de la bandada"), ISM))
		{
			return false;
		}
		TNAmbientFX::FFlock Flock;
		Flock.ISM = ISM;
		Flock.Center = FVector(8000.0, 0.0, 2000.0);
		Flock.Radius = 1500.f;
		for (int32 b = 0; b < Birds; ++b)
		{
			Flock.Phase.Add(TNProcMap::TwoPi * b / Birds);
			Flock.Offset.Add(0.f);
			Flock.Lift.Add(0.f);
		}
		Flock.Xf.Init(FTransform::Identity, Birds);
		CheckMoveKeepsProxy(*this, TEXT("TickFlock"), World, ISM,
			[&Flock]() { TNAmbientFX::TickFlock(Flock, 0.5f); },
			[&Flock]() { return Flock.Xf[0].GetLocation(); });
	}

	// ── Emisor de TNAmbientFX despierto (la cámara al lado) ──
	{
		constexpr int32 MaxParticles = 8;
		UInstancedStaticMeshComponent* ISM = MakeIsm(World, Cube, MaxParticles);
		if (!TestNotNull(TEXT("ISM del emisor"), ISM))
		{
			return false;
		}
		TNAmbientFX::FEmitter Emitter;
		Emitter.ISM = ISM;
		Emitter.Origin = FVector(3000.0, 0.0, 0.0);
		Emitter.Desc.MaxParticles = MaxParticles;
		Emitter.Desc.Rate = 1000.f;
		Emitter.Desc.Gravity = 0.f;
		Emitter.Desc.LifeMin = 5.f;
		Emitter.Desc.LifeMax = 5.f;
		Emitter.Particles.SetNum(MaxParticles);
		Emitter.Xf.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), MaxParticles);
		CheckMoveKeepsProxy(*this, TEXT("TickEmitter"), World, ISM,
			[&Emitter]() { TNAmbientFX::TickEmitter(Emitter, 0.1f, Emitter.Origin); },
			[&Emitter]() { return Emitter.Particles[0].P; });
		TestTrue(TEXT("TickEmitter: han nacido partículas"), Emitter.Particles[0].bAlive);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
