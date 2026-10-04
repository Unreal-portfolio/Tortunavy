// Forcejeo de los cangrejos que agarran (#685, #686): si el cangrejo desaparece con una tortuga agarrada (p. ej., al cerrar
// una ronda), el forcejeo se desarma y el salto vuelve a ser salto. Además, la red de seguridad del componente: un forcejeo
// armado sin trampa ni enemigo que la sujete se desarma a la segunda mirada.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Beach.GrabEscape; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TortugaCharacter.h"
#include "UObject/UnrealType.h"
#include "World/Beach/TN_BeachBurrowCrab.h"
#include "World/Beach/TN_BeachDragCrab.h"
#include "World/Beach/TN_BeachTrapStatusComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNBeachGrabEscapeTest
{
	/** Mundo de juego con BeginPlay ya hecho: los actores que se crean después lo reciben al aparecer. */
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNBeachGrabEscapeTestWorld"));
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

	ATortugaCharacter* SpawnTurtle(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator, Params);
	}

	/** Pone Victim como la agarrada del cangrejo (Grabbed es protegido: se escribe por reflexión, como haría el servidor). */
	bool SetGrabbed(AActor* Crab, ATortugaCharacter* Victim)
	{
		const FObjectPropertyBase* Prop = FindFProperty<FObjectPropertyBase>(Crab->GetClass(), TEXT("Grabbed"));
		if (!Prop)
		{
			return false;
		}
		Prop->SetObjectPropertyValue_InContainer(Crab, Victim);
		return true;
	}

	/** Agarra a una tortuga con un cangrejo de la clase CrabClass, lo destruye y comprueba que el forcejeo queda desarmado. */
	template <typename TCrab>
	void CheckDestroyedWhileGrabbing(FAutomationTestBase& Test, const TCHAR* Label)
	{
		FPlayWorld Play;
		ATortugaCharacter* Turtle = SpawnTurtle(Play.World);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TCrab* Crab = Play.World->SpawnActor<TCrab>(TCrab::StaticClass(), FVector(300.0, 0.0, 0.0), FRotator::ZeroRotator, Params);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s: tortuga"), Label), Turtle) || !Test.TestNotNull(*FString::Printf(TEXT("%s: cangrejo"), Label), Crab))
		{
			return;
		}
		UTN_BeachTrapStatusComponent* Status = UTN_BeachTrapStatusComponent::FindOrAddTo(Turtle);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s: componente de forcejeo"), Label), Status)
			|| !Test.TestTrue(*FString::Printf(TEXT("%s: Grabbed existe"), Label), SetGrabbed(Crab, Turtle)))
		{
			return;
		}
		Status->ServerArmEscape(true);
		Test.TestTrue(*FString::Printf(TEXT("%s: agarrada, el salto es forcejeo"), Label), Status->IsEscapeArmed());
		Crab->Destroy();
		Test.TestFalse(*FString::Printf(TEXT("%s: destruido el cangrejo, el forcejeo queda desarmado"), Label), Status->IsEscapeArmed());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachGrabEscapeCrabsTest,
	"Tortunabo.Beach.GrabEscape.CrabDestroyed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachGrabEscapeCrabsTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachGrabEscapeTest;
	CheckDestroyedWhileGrabbing<ATN_BeachDragCrab>(*this, TEXT("Arrastrador"));
	CheckDestroyedWhileGrabbing<ATN_BeachBurrowCrab>(*this, TEXT("Subterráneo"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachGrabEscapeOrphanTest,
	"Tortunabo.Beach.GrabEscape.OrphanWatch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachGrabEscapeOrphanTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachGrabEscapeTest;
	FPlayWorld Play;
	ATortugaCharacter* Turtle = SpawnTurtle(Play.World);
	if (!TestNotNull(TEXT("Tortuga"), Turtle))
	{
		return false;
	}
	UTN_BeachTrapStatusComponent* Status = UTN_BeachTrapStatusComponent::FindOrAddTo(Turtle);
	if (!TestNotNull(TEXT("Componente de forcejeo"), Status))
	{
		return false;
	}
	TestFalse(TEXT("Sin armar: la mirada no hace nada"), Status->ServerCheckOrphanEscape());

	Status->ServerArmEscape(true);
	TestFalse(TEXT("Primera mirada sin nadie que la sujete: aún no (puede faltar un fotograma para la sujeción)"), Status->ServerCheckOrphanEscape());
	TestTrue(TEXT("Sigue armado tras una sola mirada"), Status->IsEscapeArmed());
	TestTrue(TEXT("Segunda mirada seguida: lo desarma"), Status->ServerCheckOrphanEscape());
	TestFalse(TEXT("Desarmado: el salto vuelve a ser salto"), Status->IsEscapeArmed());

	// Atrapada de verdad (arenas movedizas): la red de seguridad no la suelta.
	Status->ServerTrap(Turtle, Turtle->GetActorLocation());
	TestTrue(TEXT("Atrapada: forcejeo armado"), Status->IsEscapeArmed());
	Status->ServerCheckOrphanEscape();
	TestFalse(TEXT("Atrapada: la red de seguridad no desarma"), Status->ServerCheckOrphanEscape());
	TestTrue(TEXT("Atrapada: sigue armado"), Status->IsEscapeArmed());
	Status->ServerRelease(FVector::ZeroVector, 0.f);
	TestFalse(TEXT("Soltada: desarmado"), Status->IsEscapeArmed());
	return true;
}

#endif
