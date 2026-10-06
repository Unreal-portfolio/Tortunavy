#include "VR/TN_VRMode.h"
#include "VR/TN_VRRig.h"
#include "VR/TN_VRSubsystem.h"
#include "Multiplayer/TN_LocalPlaySubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SWidget.h"

// ─────────────────────────────────────────────────────────────────────────────
// Botones de los mandos (Meta Quest Touch, OpenXR). Por nombre: si el motor no los tiene registrados, la FKey no es válida
// y se ignora (sin romper la compilación ni la partida).
// ─────────────────────────────────────────────────────────────────────────────

const FKey FTNVRKeys::LeftStickX(TEXT("OculusTouch_Left_Thumbstick_X"));
const FKey FTNVRKeys::LeftStickY(TEXT("OculusTouch_Left_Thumbstick_Y"));
const FKey FTNVRKeys::RightStickX(TEXT("OculusTouch_Right_Thumbstick_X"));
const FKey FTNVRKeys::RightStickY(TEXT("OculusTouch_Right_Thumbstick_Y"));
const FKey FTNVRKeys::LeftStickUp(TEXT("OculusTouch_Left_Thumbstick_Up"));
const FKey FTNVRKeys::LeftStickDown(TEXT("OculusTouch_Left_Thumbstick_Down"));
const FKey FTNVRKeys::LeftStickLeft(TEXT("OculusTouch_Left_Thumbstick_Left"));
const FKey FTNVRKeys::LeftStickRight(TEXT("OculusTouch_Left_Thumbstick_Right"));
const FKey FTNVRKeys::RightStickUp(TEXT("OculusTouch_Right_Thumbstick_Up"));
const FKey FTNVRKeys::RightStickDown(TEXT("OculusTouch_Right_Thumbstick_Down"));
const FKey FTNVRKeys::RightStickLeft(TEXT("OculusTouch_Right_Thumbstick_Left"));
const FKey FTNVRKeys::RightStickRight(TEXT("OculusTouch_Right_Thumbstick_Right"));
const FKey FTNVRKeys::LeftStickClick(TEXT("OculusTouch_Left_Thumbstick_Click"));
const FKey FTNVRKeys::RightStickClick(TEXT("OculusTouch_Right_Thumbstick_Click"));
const FKey FTNVRKeys::LeftTrigger(TEXT("OculusTouch_Left_Trigger_Click"));
const FKey FTNVRKeys::RightTrigger(TEXT("OculusTouch_Right_Trigger_Click"));
const FKey FTNVRKeys::LeftTriggerAxis(TEXT("OculusTouch_Left_Trigger_Axis"));
const FKey FTNVRKeys::RightTriggerAxis(TEXT("OculusTouch_Right_Trigger_Axis"));
const FKey FTNVRKeys::LeftGrip(TEXT("OculusTouch_Left_Grip_Click"));
const FKey FTNVRKeys::RightGrip(TEXT("OculusTouch_Right_Grip_Click"));
const FKey FTNVRKeys::LeftGripAxis(TEXT("OculusTouch_Left_Grip_Axis"));
const FKey FTNVRKeys::RightGripAxis(TEXT("OculusTouch_Right_Grip_Axis"));
const FKey FTNVRKeys::A(TEXT("OculusTouch_Right_A_Click"));
const FKey FTNVRKeys::B(TEXT("OculusTouch_Right_B_Click"));
const FKey FTNVRKeys::X(TEXT("OculusTouch_Left_X_Click"));
const FKey FTNVRKeys::Y(TEXT("OculusTouch_Left_Y_Click"));
const FKey FTNVRKeys::Menu(TEXT("OculusTouch_Left_Menu_Click"));

bool FTNVRKeys::IsVRKey(const FKey& Key)
{
	// Todos los de los Touch empiezan igual; así valen también los que no están en la lista (tocar, sistema...).
	return Key.GetFName().ToString().StartsWith(TEXT("OculusTouch_"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Modo
// ─────────────────────────────────────────────────────────────────────────────

namespace TNVRModeDetail
{
	ETNVRMode CurrentMode = ETNVRMode::Off;
	bool bRightStickReserved = false;
	FVector2D MenuRightStick = FVector2D::ZeroVector;
}

ETNVRMode TNVR::GetMode()
{
	return TNVRModeDetail::CurrentMode;
}

bool TNVR::IsEnabled()
{
	return TNVRModeDetail::CurrentMode != ETNVRMode::Off;
}

bool TNVR::IsHeadset()
{
	return TNVRModeDetail::CurrentMode == ETNVRMode::Headset;
}

bool TNVR::IsSimulated()
{
	return TNVRModeDetail::CurrentMode == ETNVRMode::Simulated;
}

void TNVR::SetMode(ETNVRMode NewMode)
{
	TNVRModeDetail::CurrentMode = NewMode;
}

void TNVR::SetRightStickReserved(bool bReserved)
{
	TNVRModeDetail::bRightStickReserved = bReserved;
}

bool TNVR::IsRightStickReserved()
{
	return TNVRModeDetail::bRightStickReserved;
}

void TNVR::SetMenuRightStick(const FVector2D& Stick)
{
	TNVRModeDetail::MenuRightStick = Stick;
}

FVector2D TNVR::GetMenuRightStick()
{
	return TNVRModeDetail::MenuRightStick;
}

// ─────────────────────────────────────────────────────────────────────────────
// Interfaz
// ─────────────────────────────────────────────────────────────────────────────

void TNVR::AddToScreen(UUserWidget* Widget, int32 ZOrder)
{
	if (!Widget)
	{
		return;
	}
	if (IsEnabled())
	{
		if (UTN_VRSubsystem* VR = UTN_VRSubsystem::Get(Widget))
		{
			// El widget de un jugador: al apagar la VR vuelve a su trozo de la pantalla partida (#639).
			if (VR->HostWidget(Widget, ZOrder, true))
			{
				return;
			}
		}
	}
	// Partida local (#311): el widget de un jugador, a su trozo de la pantalla partida (con uno solo, la pantalla entera).
	if (Widget->GetOwningLocalPlayer() && UTN_LocalPlaySubsystem::IsLocalGame(Widget))
	{
		if (Widget->AddToPlayerScreen(ZOrder))
		{
			return;
		}
	}
	// Sin VR, lo de siempre (y si con VR no hay mundo de juego todavía, al viewport: el rig lo recoge al aparecer).
	Widget->AddToViewport(ZOrder);
}

void TNVR::AddToFullScreen(UUserWidget* Widget, int32 ZOrder)
{
	if (!Widget)
	{
		return;
	}
	if (IsEnabled())
	{
		if (UTN_VRSubsystem* VR = UTN_VRSubsystem::Get(Widget))
		{
			if (VR->HostWidget(Widget, ZOrder))
			{
				return;
			}
		}
	}
	Widget->AddToViewport(ZOrder);
}

bool TNVR::IsOnScreen(const UUserWidget* Widget)
{
	if (!Widget)
	{
		return false;
	}
	if (Widget->IsInViewport())
	{
		return true;
	}
	if (!IsEnabled())
	{
		return false;
	}
	const UTN_VRSubsystem* VR = UTN_VRSubsystem::Get(Widget);
	return VR && VR->IsHostedWidget(Widget);
}

bool TNVR::AddSlateToScreen(UGameViewportClient* Viewport, const TSharedRef<SWidget>& Widget, int32 ZOrder)
{
	if (IsEnabled())
	{
		if (UTN_VRSubsystem* VR = UTN_VRSubsystem::Get(Viewport))
		{
			if (VR->HostSlate(Viewport, Widget, ZOrder))
			{
				return true;
			}
		}
	}
	if (Viewport)
	{
		Viewport->AddViewportWidgetContent(Widget, ZOrder);
	}
	return false;
}

void TNVR::RemoveSlateFromScreen(UGameViewportClient* Viewport, const TSharedRef<SWidget>& Widget)
{
	if (UTN_VRSubsystem* VR = UTN_VRSubsystem::Get(Viewport))
	{
		VR->UnhostSlate(Widget);
	}
	if (Viewport)
	{
		Viewport->RemoveViewportWidgetContent(Widget);
	}
}

bool TNVR::HasPanel(const UObject* WorldContext)
{
	if (!IsEnabled())
	{
		return false;
	}
	const UTN_VRSubsystem* VR = UTN_VRSubsystem::Get(WorldContext);
	const ATN_VRRig* Rig = VR ? VR->GetActiveRig() : nullptr;
	return Rig && Rig->GetWorld() == (WorldContext ? WorldContext->GetWorld() : nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cámara y manos
// ─────────────────────────────────────────────────────────────────────────────

bool TNVR::KeepFirstPersonView()
{
	return IsEnabled();
}

float TNVR::ViewBlendTime(float FlatSeconds)
{
	return IsEnabled() ? 0.f : FlatSeconds;
}

USceneComponent* TNVR::GetHand(const UObject* WorldContext, bool bRight)
{
	if (!IsEnabled())
	{
		return nullptr;
	}
	const UTN_VRSubsystem* VR = UTN_VRSubsystem::Get(WorldContext);
	const ATN_VRRig* Rig = VR ? VR->GetActiveRig() : nullptr;
	return Rig ? Rig->GetHand(bRight) : nullptr;
}
