#include "VR/TN_VRControls.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "VR/TN_VRMode.h"

namespace TNVRControlsDetail
{
	/**
	 * Cómo se llama cada botón Touch. Estático local (no de archivo): los NSLOCTEXT se crean con el sistema de localización ya
	 * en marcha. Los sticks comparten clave con los del mando (TNKeys/PadLeftStick...: mismo texto). Las letras de los
	 * botones son nombres de marca (INVTEXT).
	 */
	const TMap<FName, FText>& Names()
	{
		static const TMap<FName, FText> Table = []
		{
			TMap<FName, FText> Out;
			const FText LeftStick = NSLOCTEXT("TNKeys", "PadLeftStick", "Stick izquierdo");
			const FText RightStick = NSLOCTEXT("TNKeys", "PadRightStick", "Stick derecho");
			const FText LeftTrigger = NSLOCTEXT("TNKeys", "VRTriggerLeft", "Gatillo izquierdo");
			const FText RightTrigger = NSLOCTEXT("TNKeys", "VRTriggerRight", "Gatillo derecho");
			const FText LeftGrip = NSLOCTEXT("TNKeys", "VRGripLeft", "Agarre izquierdo");
			const FText RightGrip = NSLOCTEXT("TNKeys", "VRGripRight", "Agarre derecho");
			for (const FKey& Key : { FTNVRKeys::LeftStickX, FTNVRKeys::LeftStickY, FTNVRKeys::LeftStickUp, FTNVRKeys::LeftStickDown,
				FTNVRKeys::LeftStickLeft, FTNVRKeys::LeftStickRight })
			{
				Out.Add(Key.GetFName(), LeftStick);
			}
			for (const FKey& Key : { FTNVRKeys::RightStickX, FTNVRKeys::RightStickY, FTNVRKeys::RightStickUp, FTNVRKeys::RightStickDown,
				FTNVRKeys::RightStickLeft, FTNVRKeys::RightStickRight })
			{
				Out.Add(Key.GetFName(), RightStick);
			}
			Out.Add(FTNVRKeys::LeftStickClick.GetFName(), NSLOCTEXT("TNKeys", "PadLeftStickClick", "Clic stick izquierdo"));
			Out.Add(FTNVRKeys::RightStickClick.GetFName(), NSLOCTEXT("TNKeys", "PadRightStickClick", "Clic stick derecho"));
			Out.Add(FTNVRKeys::LeftTrigger.GetFName(), LeftTrigger);
			Out.Add(FTNVRKeys::LeftTriggerAxis.GetFName(), LeftTrigger);
			Out.Add(FTNVRKeys::RightTrigger.GetFName(), RightTrigger);
			Out.Add(FTNVRKeys::RightTriggerAxis.GetFName(), RightTrigger);
			Out.Add(FTNVRKeys::LeftGrip.GetFName(), LeftGrip);
			Out.Add(FTNVRKeys::LeftGripAxis.GetFName(), LeftGrip);
			Out.Add(FTNVRKeys::RightGrip.GetFName(), RightGrip);
			Out.Add(FTNVRKeys::RightGripAxis.GetFName(), RightGrip);
			Out.Add(FTNVRKeys::A.GetFName(), INVTEXT("A"));
			Out.Add(FTNVRKeys::B.GetFName(), INVTEXT("B"));
			Out.Add(FTNVRKeys::X.GetFName(), INVTEXT("X"));
			Out.Add(FTNVRKeys::Y.GetFName(), INVTEXT("Y"));
			Out.Add(FTNVRKeys::Menu.GetFName(), NSLOCTEXT("TNKeys", "VRMenu", "Menú"));
			return Out;
		}();
		return Table;
	}
}

FText TNVRControls::KeyName(const FKey& Key)
{
	if (!Key.IsValid() || !FTNVRKeys::IsVRKey(Key))
	{
		return FText::GetEmpty();
	}
	if (const FText* Found = TNVRControlsDetail::Names().Find(Key.GetFName()))
	{
		return *Found;
	}
	// Un botón Touch que el juego no usa (tocar, sistema...): el nombre que da el motor.
	return Key.GetDisplayName();
}

FKey TNVRControls::KeyForAction(const FString& ActionId)
{
	// Lo que hay en ATN_VRRig::EnsureVRMapping (IMC_VR) y en ATN_VRRig::UpdateGrips (agarres: coger, correr y soltar).
	static const TMap<FString, FKey> Table = []
	{
		TMap<FString, FKey> Out;
		Out.Add(TEXT("IA_Move"), FTNVRKeys::LeftStickX);
		Out.Add(TEXT("IA_Look"), FTNVRKeys::RightStickX);
		Out.Add(TEXT("IA_Jump"), FTNVRKeys::A);
		Out.Add(TEXT("IA_Shell"), FTNVRKeys::B);
		Out.Add(TEXT("IA_Interact"), FTNVRKeys::RightTrigger);
		Out.Add(TEXT("IA_Sprint"), FTNVRKeys::LeftGrip);
		Out.Add(TEXT("IA_DropItem"), FTNVRKeys::RightGrip);
		Out.Add(TEXT("IA_RotateInventory"), FTNVRKeys::X);
		Out.Add(TEXT("IA_OpenEmoteWheel"), FTNVRKeys::Y);
		Out.Add(TEXT("IA_OpenChatWheel"), FTNVRKeys::LeftTrigger);
		Out.Add(TEXT("IA_RadialNavigate"), FTNVRKeys::RightStickX);
		Out.Add(TEXT("Talk"), FTNVRKeys::LeftStickClick);
		Out.Add(TEXT("Pause"), FTNVRKeys::Menu);
		return Out;
	}();
	const FKey* Found = Table.Find(ActionId);
	return Found ? *Found : FKey();
}

TArray<TNVRControls::FGuideLine> TNVRControls::GetGuide()
{
	using UGS = UTN_GameSettingsSubsystem;
	auto Name = [](const FKey& Key) { return KeyName(Key); };
	auto ForAction = [&Name](const TCHAR* ActionId) { return Name(KeyForAction(ActionId)); };

	TArray<FGuideLine> Lines;
	Lines.Add({ UGS::ActionLabel(TEXT("IA_Move")), ForAction(TEXT("IA_Move")) });
	Lines.Add({ NSLOCTEXT("TNVRControls", "Turn", "Girar la vista"), ForAction(TEXT("IA_Look")) });
	Lines.Add({ NSLOCTEXT("TNVRControls", "Recenter", "Recentrar la vista"), Name(FTNVRKeys::RightStickClick) });
	Lines.Add({ UGS::ActionLabel(TEXT("IA_Jump")), ForAction(TEXT("IA_Jump")) });
	Lines.Add({ UGS::ActionLabel(TEXT("IA_Shell")), ForAction(TEXT("IA_Shell")) });
	Lines.Add({ UGS::ActionLabel(TEXT("IA_Interact")), ForAction(TEXT("IA_Interact")) });
	Lines.Add({ NSLOCTEXT("TNVRControls", "Grab", "Coger con la mano"),
		FText::Format(INVTEXT("{0} / {1}"), Name(FTNVRKeys::LeftGrip), Name(FTNVRKeys::RightGrip)) });
	Lines.Add({ UGS::ActionLabel(TEXT("IA_Sprint")), ForAction(TEXT("IA_Sprint")) });
	Lines.Add({ UGS::ActionLabel(TEXT("IA_RotateInventory")), ForAction(TEXT("IA_RotateInventory")) });
	Lines.Add({ UGS::ActionLabel(TEXT("IA_OpenEmoteWheel")), ForAction(TEXT("IA_OpenEmoteWheel")) });
	Lines.Add({ UGS::ActionLabel(TEXT("IA_OpenChatWheel")), ForAction(TEXT("IA_OpenChatWheel")) });
	Lines.Add({ UGS::ActionLabel(TEXT("IA_RadialNavigate")), ForAction(TEXT("IA_RadialNavigate")) });
	Lines.Add({ NSLOCTEXT("TNSettings", "TalkRow", "Hablar (pulsar para hablar)"), ForAction(TEXT("Talk")) });
	Lines.Add({ NSLOCTEXT("TNSettings", "PauseRow", "Abrir y cerrar este menú"), ForAction(TEXT("Pause")) });
	Lines.Add({ NSLOCTEXT("TNPause", "SpectateRow", "Espectador: cambiar de tortuga"), ForAction(TEXT("IA_Look")) });
	return Lines;
}
