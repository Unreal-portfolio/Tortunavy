// Sombrilla con estado replicado (#893): bIsOpen se replica con OnRep_IsOpen, que aplica el estado visual; la multicast
// no fiable solo lleva el sonido, el VFX y el evento BP. Antes la apertura viajaba solo en esa multicast y quien entraba
// tarde, llegaba después a la zona de relevancia o perdía el paquete la veía cerrada.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.World.Umbrella; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/UnrealType.h"
#include "World/TN_UmbrellaInteractable.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNUmbrellaReplicationTest
{
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNUmbrellaTestWorld"));
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

	const FBoolProperty* FindIsOpen()
	{
		return CastField<FBoolProperty>(ATN_UmbrellaInteractable::StaticClass()->FindPropertyByName(TEXT("bIsOpen")));
	}

	bool IsCanopyVisible(const ATN_UmbrellaInteractable* Umbrella)
	{
		TArray<UStaticMeshComponent*> Meshes;
		Umbrella->GetComponents(Meshes);
		for (const UStaticMeshComponent* Comp : Meshes)
		{
			if (Comp->GetFName() == TEXT("CanopyMesh")) { return Comp->IsVisible(); }
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNUmbrellaReplicatedStateTest,
	"Tortunabo.World.Umbrella.ReplicatedState",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNUmbrellaReplicatedStateTest::RunTest(const FString& Parameters)
{
	using namespace TNUmbrellaReplicationTest;

	const FBoolProperty* IsOpen = FindIsOpen();
	if (!TestNotNull(TEXT("bIsOpen es una UPROPERTY"), IsOpen)) { return false; }
	TestTrue(TEXT("bIsOpen se replica"), IsOpen->HasAnyPropertyFlags(CPF_Net));
	TestTrue(TEXT("bIsOpen tiene OnRep"), IsOpen->HasAnyPropertyFlags(CPF_RepNotify));
	TestEqual(TEXT("El OnRep es OnRep_IsOpen"), IsOpen->RepNotifyFunc, FName(TEXT("OnRep_IsOpen")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNUmbrellaLateJoinTest,
	"Tortunabo.World.Umbrella.LateJoinSeesOpen",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNUmbrellaLateJoinTest::RunTest(const FString& Parameters)
{
	using namespace TNUmbrellaReplicationTest;

	const FBoolProperty* IsOpen = FindIsOpen();
	if (!TestNotNull(TEXT("bIsOpen es una UPROPERTY"), IsOpen)) { return false; }

	FPlayWorld Play;
	const FTransform Where(FVector(0.f, 0.f, 0.f));

	// Quien entra tarde recibe bIsOpen = true antes de BeginPlay: debe verla abierta.
	ATN_UmbrellaInteractable* Late = Play.World->SpawnActorDeferred<ATN_UmbrellaInteractable>(
		ATN_UmbrellaInteractable::StaticClass(), Where);
	if (!TestNotNull(TEXT("Se crea la sombrilla (entra tarde)"), Late)) { return false; }
	IsOpen->SetPropertyValue_InContainer(Late, true);
	Late->FinishSpawning(Where);
	TestTrue(TEXT("Entra tarde y la ve abierta"), IsCanopyVisible(Late));

	// Cliente ya presente: el OnRep aplica la apertura y el cierre.
	ATN_UmbrellaInteractable* Present = Play.World->SpawnActor<ATN_UmbrellaInteractable>(
		ATN_UmbrellaInteractable::StaticClass(), Where);
	if (!TestNotNull(TEXT("Se crea la sombrilla (presente)"), Present)) { return false; }
	TestFalse(TEXT("Empieza cerrada"), IsCanopyVisible(Present));

	UFunction* OnRep = Present->FindFunction(TEXT("OnRep_IsOpen"));
	if (!TestNotNull(TEXT("Existe OnRep_IsOpen"), OnRep)) { return false; }
	IsOpen->SetPropertyValue_InContainer(Present, true);
	Present->ProcessEvent(OnRep, nullptr);
	TestTrue(TEXT("El OnRep la abre"), IsCanopyVisible(Present));
	IsOpen->SetPropertyValue_InContainer(Present, false);
	Present->ProcessEvent(OnRep, nullptr);
	TestFalse(TEXT("El OnRep la cierra"), IsCanopyVisible(Present));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
