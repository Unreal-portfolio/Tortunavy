#include "Vehicles/TN_BuggyInput.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace
{
	UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, FName(Name), RF_Transient);
		Action->ValueType = Type;
		return Action;
	}

	void MapNegated(UInputMappingContext* Context, const UInputAction* Action, const FKey& Key)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
	}

	void MapDeadZone(UInputMappingContext* Context, const UInputAction* Action, const FKey& Key)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		Mapping.Modifiers.Add(NewObject<UInputModifierDeadZone>(Context));
	}

	/** Rueda del ratón (arriba +1, abajo -1) y crucetas (derecha +1, izquierda -1). */
	void MapCycleAmmo(UInputMappingContext* Context, const UInputAction* Action)
	{
		Context->MapKey(Action, EKeys::MouseWheelAxis);
		Context->MapKey(Action, EKeys::Gamepad_DPad_Right);
		MapNegated(Context, Action, EKeys::Gamepad_DPad_Left);
	}

	UEnhancedInputLocalPlayerSubsystem* SubsystemOf(const APlayerController* PC)
	{
		if (!PC || !PC->IsLocalController())
		{
			return nullptr;
		}
		return ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
	}
}

UTN_BuggyInputSet* UTN_BuggyInputSet::Create(UObject* Outer)
{
	UTN_BuggyInputSet* Set = NewObject<UTN_BuggyInputSet>(Outer);
	Set->Throttle = MakeAction(Set, TEXT("IA_BuggyThrottle"), EInputActionValueType::Axis1D);
	Set->Brake = MakeAction(Set, TEXT("IA_BuggyBrake"), EInputActionValueType::Axis1D);
	Set->Steer = MakeAction(Set, TEXT("IA_BuggySteer"), EInputActionValueType::Axis1D);
	Set->Handbrake = MakeAction(Set, TEXT("IA_BuggyHandbrake"), EInputActionValueType::Boolean);
	Set->Boost = MakeAction(Set, TEXT("IA_BuggyBoost"), EInputActionValueType::Boolean);
	Set->FireBack = MakeAction(Set, TEXT("IA_BuggyFireBack"), EInputActionValueType::Boolean);
	Set->SelfRight = MakeAction(Set, TEXT("IA_BuggySelfRight"), EInputActionValueType::Boolean);
	Set->FireCoco = MakeAction(Set, TEXT("IA_BuggyFireCoco"), EInputActionValueType::Boolean);
	Set->FireSpecial = MakeAction(Set, TEXT("IA_BuggyFireSpecial"), EInputActionValueType::Boolean);
	Set->CycleAmmo = MakeAction(Set, TEXT("IA_BuggyCycleAmmo"), EInputActionValueType::Axis1D);
	Set->AimMouse = MakeAction(Set, TEXT("IA_BuggyAimMouse"), EInputActionValueType::Axis2D);
	Set->AimStick = MakeAction(Set, TEXT("IA_BuggyAimStick"), EInputActionValueType::Axis2D);
	Set->CallNote = MakeAction(Set, TEXT("IA_BuggyCallNote"), EInputActionValueType::Boolean);
	Set->QuickCall = MakeAction(Set, TEXT("IA_BuggyQuickCall"), EInputActionValueType::Axis1D);

	UInputMappingContext* Driver = NewObject<UInputMappingContext>(Set, TEXT("IMC_BuggyDriver"), RF_Transient);
	Driver->MapKey(Set->Throttle, EKeys::W);
	Driver->MapKey(Set->Throttle, EKeys::Gamepad_RightTriggerAxis);
	Driver->MapKey(Set->Brake, EKeys::S);
	Driver->MapKey(Set->Brake, EKeys::Gamepad_LeftTriggerAxis);
	Driver->MapKey(Set->Steer, EKeys::D);
	MapNegated(Driver, Set->Steer, EKeys::A);
	MapDeadZone(Driver, Set->Steer, EKeys::Gamepad_LeftX);
	// #294: Espacio y A son el turbo; el freno de mano pasa a Shift izquierdo y X, y disparar atrás, de X a B.
	Driver->MapKey(Set->Handbrake, EKeys::LeftShift);
	Driver->MapKey(Set->Handbrake, EKeys::Gamepad_FaceButton_Left);
	Driver->MapKey(Set->Boost, EKeys::SpaceBar);
	Driver->MapKey(Set->Boost, EKeys::Gamepad_FaceButton_Bottom);
	Driver->MapKey(Set->SelfRight, EKeys::R);
	Driver->MapKey(Set->SelfRight, EKeys::Gamepad_FaceButton_Top);
	Driver->MapKey(Set->FireCoco, EKeys::LeftMouseButton);
	Driver->MapKey(Set->FireCoco, EKeys::Gamepad_RightShoulder);
	Driver->MapKey(Set->FireSpecial, EKeys::RightMouseButton);
	Driver->MapKey(Set->FireSpecial, EKeys::Gamepad_LeftShoulder);
	Driver->MapKey(Set->FireBack, EKeys::Q);
	Driver->MapKey(Set->FireBack, EKeys::Gamepad_FaceButton_Right);
	MapCycleAmmo(Driver, Set->CycleAmmo);
	Set->DriverContext = Driver;

	UInputMappingContext* Gunner = NewObject<UInputMappingContext>(Set, TEXT("IMC_BuggyGunner"), RF_Transient);
	Gunner->MapKey(Set->AimMouse, EKeys::Mouse2D);
	MapDeadZone(Gunner, Set->AimStick, EKeys::Gamepad_Right2D);
	Gunner->MapKey(Set->FireCoco, EKeys::LeftMouseButton);
	Gunner->MapKey(Set->FireCoco, EKeys::Gamepad_RightTriggerAxis);
	Gunner->MapKey(Set->FireSpecial, EKeys::RightMouseButton);
	Gunner->MapKey(Set->FireSpecial, EKeys::Gamepad_LeftTriggerAxis);
	Gunner->MapKey(Set->SelfRight, EKeys::R);
	Gunner->MapKey(Set->SelfRight, EKeys::Gamepad_FaceButton_Top);
	MapCycleAmmo(Gunner, Set->CycleAmmo);
	// Cantos a la conductora (#330). La cruceta abajo es «pulsar para hablar» y la izquierda y la derecha cambian la munición.
	Gunner->MapKey(Set->CallNote, EKeys::F);
	Gunner->MapKey(Set->CallNote, EKeys::Gamepad_FaceButton_Bottom);
	Gunner->MapKey(Set->QuickCall, EKeys::One);
	Gunner->MapKey(Set->QuickCall, EKeys::Gamepad_DPad_Up);
	MapNegated(Gunner, Set->QuickCall, EKeys::Two);
	MapNegated(Gunner, Set->QuickCall, EKeys::Gamepad_FaceButton_Right);
	Set->GunnerContext = Gunner;
	return Set;
}

int32 UTN_BuggyInputSet::CycleDirection(const FInputActionValue& Value)
{
	const float Axis = Value.Get<float>();
	return FMath::IsNearlyZero(Axis) ? 0 : (Axis > 0.f ? 1 : -1);
}

void UTN_BuggyInputSet::AddContext(const APlayerController* PC, const UInputMappingContext* Context)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = SubsystemOf(PC);
	if (Subsystem && Context)
	{
		Subsystem->AddMappingContext(Context, ContextPriority);
	}
}

void UTN_BuggyInputSet::RemoveContext(const APlayerController* PC, const UInputMappingContext* Context)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = SubsystemOf(PC);
	if (Subsystem && Context)
	{
		Subsystem->RemoveMappingContext(Context);
	}
}
