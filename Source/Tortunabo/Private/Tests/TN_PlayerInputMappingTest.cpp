// Teclas y botones de serie de IMC_Player (#637): el caparazón con Ctrl izquierdo y B / Círculo, la rueda de bailes con
// Q y LT / L2, y ninguna asignación de antes perdida ni una tecla repartida entre dos acciones. Carga el asset; sin
// mundo. Correr desde Session Frontend (categoría "Tortunabo.Settings.PlayerInputMapping") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Settings.PlayerInputMapping; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "InputAction.h"
#include "InputMappingContext.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNPlayerInputMappingTest
{
	constexpr const TCHAR* MappingPath = TEXT("/Game/Blueprints/Gameplay/Controls/IMC_Player.IMC_Player");

	/** «Acción:tecla» de cada asignación del asset. */
	static TSet<FString> CollectPairs(const UInputMappingContext& Context)
	{
		TSet<FString> Pairs;
		for (const FEnhancedActionKeyMapping& Mapping : Context.GetMappings())
		{
			if (Mapping.Action)
			{
				Pairs.Add(Mapping.Action->GetName() + TEXT(":") + Mapping.Key.GetFName().ToString());
			}
		}
		return Pairs;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPlayerInputMappingTest,
	"Tortunabo.Settings.PlayerInputMapping",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPlayerInputMappingTest::RunTest(const FString& Parameters)
{
	using namespace TNPlayerInputMappingTest;
	const UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, MappingPath);
	if (!TestNotNull(TEXT("IMC_Player carga"), Context))
	{
		return false;
	}
	const TSet<FString> Pairs = CollectPairs(*Context);

	// Las 25 asignaciones que había antes de #637 y la nueva (IA_Shell con B / Círculo).
	const TCHAR* Expected[] = {
		TEXT("IA_Move:W"), TEXT("IA_Move:A"), TEXT("IA_Move:S"), TEXT("IA_Move:D"), TEXT("IA_Look:Mouse2D"),
		TEXT("IA_Jump:SpaceBar"), TEXT("IA_Sprint:LeftShift"), TEXT("IA_Interact:E"), TEXT("IA_DropItem:X"),
		TEXT("IA_RotateInventory:G"), TEXT("IA_OpenEmoteWheel:Q"), TEXT("IA_OpenChatWheel:C"), TEXT("IA_RadialNavigate:Mouse2D"),
		TEXT("IA_Move:Gamepad_Left2D"), TEXT("IA_Look:Gamepad_Right2D"), TEXT("IA_Jump:Gamepad_FaceButton_Bottom"),
		TEXT("IA_Sprint:Gamepad_RightTriggerAxis"), TEXT("IA_Interact:Gamepad_FaceButton_Left"),
		TEXT("IA_DropItem:Gamepad_FaceButton_Top"), TEXT("IA_RotateInventory:Gamepad_RightShoulder"),
		TEXT("IA_OpenEmoteWheel:Gamepad_LeftTriggerAxis"), TEXT("IA_OpenChatWheel:Gamepad_LeftShoulder"),
		TEXT("IA_RadialNavigate:Gamepad_Right2D"), TEXT("IA_Shell:LeftControl"), TEXT("IA_Shell:Gamepad_FaceButton_Right"),
	};
	for (const TCHAR* Pair : Expected)
	{
		TestTrue(FString::Printf(TEXT("IMC_Player tiene %s"), Pair), Pairs.Contains(Pair));
	}

	// Ninguna tecla en dos acciones, salvo mirar y navegar por las ruedas (la rueda abierta se come la cámara).
	TMap<FName, TSet<FString>> ActionsByKey;
	for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
	{
		if (Mapping.Action)
		{
			ActionsByKey.FindOrAdd(Mapping.Key.GetFName()).Add(Mapping.Action->GetName());
		}
	}
	const TSet<FString> LookAndWheel = { TEXT("IA_Look"), TEXT("IA_RadialNavigate") };
	for (const TPair<FName, TSet<FString>>& Entry : ActionsByKey)
	{
		const bool bShared = Entry.Value.Num() > 1;
		const bool bAllowed = Entry.Value.Num() == 2 && Entry.Value.Includes(LookAndWheel);
		TestFalse(FString::Printf(TEXT("%s no se reparte entre %s"), *Entry.Key.ToString(),
			*FString::Join(Entry.Value.Array(), TEXT(", "))), bShared && !bAllowed);
	}
	return true;
}

#endif
