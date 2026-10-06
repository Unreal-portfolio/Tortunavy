// La tormenta enlaza sus overlaps una sola vez (#362). BP_StormVolume y sus instancias en los niveles traen serializado el
// enlace de cuando se hacía en el constructor; si BeginPlay lo vuelve a añadir con AddDynamic, salta el ensure de
// ScriptDelegates.h (InvocationList != InDelegate) y cada overlap se cuenta dos veces. Se comprueba con la clase nativa
// (enlace serializado simulado antes del BeginPlay: es lo que carga la instancia del nivel) y con BP_StormVolume creado en
// partida (una copia nueva no hereda el enlace de la plantilla: BeginPlay tiene que poner el suyo, una vez).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.World.StormVolume; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/SparseDelegate.h"
#include "World/TN_StormVolume.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNStormVolumeBindingTestDetail
{
	const FName BeginOverlapName(TEXT("OnComponentBeginOverlap"));
	const FName EndOverlapName(TEXT("OnComponentEndOverlap"));
	const TCHAR* StormBlueprintPath = TEXT("/Game/Blueprints/Gameplay/Interaction/BP_StormVolume.BP_StormVolume_C");

	/** Mundo de juego mínimo con BeginPlay ya hecho: los actores que se crean después lo reciben al terminar de aparecer. */
	UWorld* CreateGameWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		World->GetWorldSettings()->NotifyBeginPlay();
		return World;
	}

	void DestroyGameWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	/** Cuántas veces está la tormenta en la lista de invocación del delegado de overlap de su caja. */
	int32 CountBindings(const UBoxComponent* Box, FName DelegateName, const AActor* Storm)
	{
		const FMulticastScriptDelegate* Delegate = FSparseDelegateStorage::GetMulticastDelegate(Box, DelegateName);
		if (!Delegate)
		{
			return 0;
		}
		int32 Count = 0;
		for (const UObject* Bound : Delegate->GetAllObjects())
		{
			Count += Bound == Storm ? 1 : 0;
		}
		return Count;
	}

	/** El enlace que traen serializado el BP y la instancia del nivel (el AddDynamic que antes hacía el constructor). */
	void AddSerializedStyleBindings(UBoxComponent* Box, ATN_StormVolume* Storm)
	{
		FScriptDelegate Begin;
		Begin.BindUFunction(Storm, TEXT("OnBoxBeginOverlap"));
		Box->OnComponentBeginOverlap.Add(Begin);
		FScriptDelegate End;
		End.BindUFunction(Storm, TEXT("OnBoxEndOverlap"));
		Box->OnComponentEndOverlap.Add(End);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNStormVolumeBindingTest,
	"Tortunabo.World.StormVolume.OverlapBoundOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNStormVolumeBindingTest::RunTest(const FString& Parameters)
{
	using namespace TNStormVolumeBindingTestDetail;
	UWorld* World = CreateGameWorld();

	// 1) Clase nativa con el enlace ya puesto antes del BeginPlay, como llega desde el contenido serializado.
	ATN_StormVolume* Storm = World->SpawnActorDeferred<ATN_StormVolume>(ATN_StormVolume::StaticClass(), FTransform::Identity);
	UBoxComponent* Box = Storm ? Storm->FindComponentByClass<UBoxComponent>() : nullptr;
	if (!TestNotNull(TEXT("Tormenta nativa"), Storm) || !TestNotNull(TEXT("Caja de la tormenta"), Box))
	{
		DestroyGameWorld(World);
		return false;
	}
	AddSerializedStyleBindings(Box, Storm);
	Storm->FinishSpawning(FTransform::Identity);
	TestTrue(TEXT("BeginPlay ha corrido"), Storm->HasActorBegunPlay());
	TestEqual(TEXT("Nativa: entrada enlazada una vez"), CountBindings(Box, BeginOverlapName, Storm), 1);
	TestEqual(TEXT("Nativa: salida enlazada una vez"), CountBindings(Box, EndOverlapName, Storm), 1);

	Storm->Destroy();
	TestEqual(TEXT("Nativa: EndPlay quita la entrada"), CountBindings(Box, BeginOverlapName, Storm), 0);
	TestEqual(TEXT("Nativa: EndPlay quita la salida"), CountBindings(Box, EndOverlapName, Storm), 0);

	// 2) BP_StormVolume creado en partida: sin enlace heredado, BeginPlay pone uno de cada.
	UClass* BlueprintClass = LoadClass<ATN_StormVolume>(nullptr, StormBlueprintPath);
	if (TestNotNull(TEXT("BP_StormVolume carga"), BlueprintClass))
	{
		ATN_StormVolume* BpStorm = World->SpawnActorDeferred<ATN_StormVolume>(BlueprintClass, FTransform::Identity);
		UBoxComponent* BpBox = BpStorm ? BpStorm->FindComponentByClass<UBoxComponent>() : nullptr;
		if (TestNotNull(TEXT("Tormenta del BP"), BpStorm) && TestNotNull(TEXT("Caja del BP"), BpBox))
		{
			AddInfo(FString::Printf(TEXT("BP_StormVolume: %d enlace(s) de entrada antes del BeginPlay"),
				CountBindings(BpBox, BeginOverlapName, BpStorm)));
			BpStorm->FinishSpawning(FTransform::Identity);
			TestEqual(TEXT("BP: entrada enlazada una vez"), CountBindings(BpBox, BeginOverlapName, BpStorm), 1);
			TestEqual(TEXT("BP: salida enlazada una vez"), CountBindings(BpBox, EndOverlapName, BpStorm), 1);
		}
	}

	DestroyGameWorld(World);
	return true;
}

#endif
