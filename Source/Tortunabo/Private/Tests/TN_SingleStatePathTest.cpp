// Un único camino para el estado persistente (#78): el estado de la tortuga y sus cosméticos llegan por OnRep y los
// multicast quedan para los efectos de entrada, no fiables. Un multicast fiable que repite lo que ya hace el OnRep
// aplica el efecto dos veces en cada cliente (y en quien entra tarde, solo una de ellas). Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Net.SingleStatePath; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Core/TN_CoopPlayerState.h"
#include "Player/TortugaCharacter.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Nombres de los multicast fiables declarados en la propia clase (sin los de la base). */
	TArray<FString> ReliableMulticasts(const UClass* Class)
	{
		TArray<FString> Names;
		for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (It->HasAllFunctionFlags(FUNC_NetMulticast | FUNC_NetReliable))
			{
				Names.Add(It->GetName());
			}
		}
		return Names;
	}

	/** true si la propiedad replica y avisa con su OnRep. */
	bool HasRepNotify(const UClass* Class, const TCHAR* PropertyName)
	{
		const FProperty* Property = Class->FindPropertyByName(FName(PropertyName));
		return Property && Property->HasAnyPropertyFlags(CPF_Net | CPF_RepNotify) && Property->RepNotifyFunc != NAME_None;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSingleStatePathTest,
	"Tortunabo.Net.SingleStatePath",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSingleStatePathTest::RunTest(const FString& Parameters)
{
	const TArray<FString> TurtleReliable = ReliableMulticasts(ATortugaCharacter::StaticClass());
	TestEqual(FString::Printf(TEXT("Tortuga sin multicast fiable de estado (%s)"), *FString::Join(TurtleReliable, TEXT(", "))), TurtleReliable.Num(), 0);

	const TArray<FString> StateReliable = ReliableMulticasts(ATN_CoopPlayerState::StaticClass());
	TestEqual(FString::Printf(TEXT("PlayerState sin multicast fiable de cosméticos (%s)"), *FString::Join(StateReliable, TEXT(", "))), StateReliable.Num(), 0);

	// El estado que antes llegaba por los dos caminos llega ahora por su OnRep (también a quien entra tarde).
	const UClass* Turtle = ATortugaCharacter::StaticClass();
	TestTrue(TEXT("Derribo por OnRep"), HasRepNotify(Turtle, TEXT("bIsKnockedDown")));
	TestTrue(TEXT("Muerte por OnRep"), HasRepNotify(Turtle, TEXT("bIsDead")));
	TestTrue(TEXT("Panzazo por OnRep"), HasRepNotify(Turtle, TEXT("bIsDiving")));
	const FProperty* Ground = Turtle->FindPropertyByName(TEXT("DeathGroundLocation"));
	TestTrue(TEXT("El suelo de la muerte se replica con bIsDead"), Ground && Ground->HasAnyPropertyFlags(CPF_Net));
	const UClass* State = ATN_CoopPlayerState::StaticClass();
	TestTrue(TEXT("Casco por OnRep"), HasRepNotify(State, TEXT("EquippedHelmetId")));
	TestTrue(TEXT("Skin por OnRep"), HasRepNotify(State, TEXT("EquippedSkinId")));
	return true;
}

#endif
