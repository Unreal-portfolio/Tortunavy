#include "Kart/TN_KartInput.h"

#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Vehicles/TN_BuggyInput.h"
#include "VR/TN_VRMode.h"

namespace
{
	UInputAction* MakeKartAction(UObject* Outer, const TCHAR* Name, EInputActionValueType Type, bool bConsume = true)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, FName(Name), RF_Transient);
		Action->ValueType = Type;
		Action->bConsumeInput = bConsume;
		return Action;
	}

	void MapKartNegated(UInputMappingContext* Context, const UInputAction* Action, const FKey& Key)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
	}

	void MapKartDeadZone(UInputMappingContext* Context, const UInputAction* Action, const FKey& Key)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		Mapping.Modifiers.Add(NewObject<UInputModifierDeadZone>(Context));
	}

	UEnhancedInputLocalPlayerSubsystem* KartSubsystemOf(const APlayerController* PC)
	{
		if (!PC || !PC->IsLocalController())
		{
			return nullptr;
		}
		return ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
	}
}

UTN_KartInputSet* UTN_KartInputSet::Create(UObject* Outer, bool bWithItems)
{
	UTN_KartInputSet* Set = NewObject<UTN_KartInputSet>(Outer);
	Set->UseItem = MakeKartAction(Set, TEXT("IA_KartUseItem"), EInputActionValueType::Boolean);
	Set->Backward = MakeKartAction(Set, TEXT("IA_KartBackward"), EInputActionValueType::Boolean, false);
	Set->LookMouse = MakeKartAction(Set, TEXT("IA_KartLookMouse"), EInputActionValueType::Axis2D);
	Set->LookStick = MakeKartAction(Set, TEXT("IA_KartLookStick"), EInputActionValueType::Axis2D);
	Set->Fire = MakeKartAction(Set, TEXT("IA_KartFire"), EInputActionValueType::Boolean);
	Set->Lean = MakeKartAction(Set, TEXT("IA_KartLean"), EInputActionValueType::Axis1D);

	UInputMappingContext* Driver = NewObject<UInputMappingContext>(Set, TEXT("IMC_KartDriver"), RF_Transient);
	// Sin objetos (Rally, #629): ni usar objeto ni disparo propio; disparar y la munición de las cajas «?» van con los
	// controles del buggy, que estas teclas dejan pasar.
	if (bWithItems)
	{
		Driver->MapKey(Set->UseItem, EKeys::E);
		Driver->MapKey(Set->UseItem, EKeys::RightMouseButton);
		Driver->MapKey(Set->UseItem, EKeys::Gamepad_LeftShoulder);
		Driver->MapKey(Set->Fire, EKeys::LeftMouseButton);
		Driver->MapKey(Set->Fire, EKeys::Gamepad_RightShoulder);
	}
	Driver->MapKey(Set->Backward, EKeys::Q);
	Driver->MapKey(Set->Backward, EKeys::Gamepad_FaceButton_Right);
	Driver->MapKey(Set->LookMouse, EKeys::Mouse2D);
	MapKartDeadZone(Driver, Set->LookStick, EKeys::Gamepad_Right2D);
	// Con gafas (Docs/Modo_VR.md, «Vehículos»): B usa el objeto, el stick derecho hacia delante dispara sola (mira con la
	// cabeza, ATN_KartBuggy) y cualquiera de los dos sticks hacia atrás es «hacia atrás».
	if (bWithItems)
	{
		UTN_BuggyInputSet::MapTouchButton(Driver, Set->UseItem, FTNVRKeys::B);
		UTN_BuggyInputSet::MapTouchStickDirection(Driver, Set->Fire, FTNVRKeys::RightStickY, false);
	}
	UTN_BuggyInputSet::MapTouchStickDirection(Driver, Set->Backward, FTNVRKeys::RightStickY, true);
	UTN_BuggyInputSet::MapTouchStickDirection(Driver, Set->Backward, FTNVRKeys::LeftStickY, true);
	Set->DriverContext = Driver;

	UInputMappingContext* Gunner = NewObject<UInputMappingContext>(Set, TEXT("IMC_KartGunner"), RF_Transient);
	if (bWithItems)
	{
		Gunner->MapKey(Set->UseItem, EKeys::E);
		Gunner->MapKey(Set->UseItem, EKeys::RightMouseButton);
		Gunner->MapKey(Set->UseItem, EKeys::Gamepad_LeftTriggerAxis);
		Gunner->MapKey(Set->Backward, EKeys::Q);
		Gunner->MapKey(Set->Backward, EKeys::Gamepad_LeftShoulder);
	}
	Gunner->MapKey(Set->Lean, EKeys::D);
	MapKartNegated(Gunner, Set->Lean, EKeys::A);
	MapKartDeadZone(Gunner, Set->Lean, EKeys::Gamepad_LeftX);
	// Con gafas: gatillo izquierdo, usar el objeto; B o el stick izquierdo hacia atrás, «hacia atrás»; el peso, con el stick
	// izquierdo a los lados y con la cabeza (ATN_KartGunnerPawn).
	if (bWithItems)
	{
		UTN_BuggyInputSet::MapTouchTrigger(Gunner, Set->UseItem, FTNVRKeys::LeftTriggerAxis);
		UTN_BuggyInputSet::MapTouchButton(Gunner, Set->Backward, FTNVRKeys::B);
		UTN_BuggyInputSet::MapTouchStickDirection(Gunner, Set->Backward, FTNVRKeys::LeftStickY, true);
	}
	UTN_BuggyInputSet::MapTouchAxis(Gunner, Set->Lean, FTNVRKeys::LeftStickX);
	Set->GunnerContext = Gunner;
	return Set;
}

void UTN_KartInputSet::AddContext(const APlayerController* PC, const UInputMappingContext* Context)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = KartSubsystemOf(PC);
	if (Subsystem && Context)
	{
		Subsystem->AddMappingContext(Context, ContextPriority);
	}
}

void UTN_KartInputSet::RemoveContext(const APlayerController* PC, const UInputMappingContext* Context)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = KartSubsystemOf(PC);
	if (Subsystem && Context)
	{
		Subsystem->RemoveMappingContext(Context);
	}
}
